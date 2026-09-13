#include "audio.h"

namespace {

constexpr int   RATE = 44100;
constexpr float TAU  = 6.2831853f;
using Buf = vector<float>;

// xorshift: fast, deterministic, and safe to run on the audio thread.
struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed = 1) : s(seed ? seed : 1) {}
    float Next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / 8388608.0f - 1.0f;
    }
    float Unit() { return (Next() + 1.0f) * 0.5f; }
    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
};

// One-pole low-pass coefficient for a cutoff in Hz.
float Coef(float hz) {
    return 1.0f - std::exp(-TAU * std::clamp(hz, 1.0f, RATE * 0.45f) / RATE);
}

Buf Blank(float seconds) { return Buf(static_cast<size_t>(seconds * RATE) + 1, 0.0f); }

float Env(float t, float attack, float decay) {
    if (t < 0.0f) return 0.0f;
    if (t < attack) return t / attack;
    return std::exp(-(t - attack) / decay);
}

enum Wave { SINE, TRI, SAW };

float Osc(Wave w, float ph) {
    switch (w) {
        case TRI: return 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        case SAW: return 2.0f * ph - 1.0f;
        default:  return std::sin(ph * TAU);
    }
}

// A tone gliding from f0 to f1 over its length.
void Tone(Buf& b, float start, float dur, float f0, float f1, float amp,
          float attack, float decay, Wave w = SINE) {
    const size_t s0 = static_cast<size_t>(start * RATE);
    const size_t n  = static_cast<size_t>(dur * RATE);
    float ph = 0.0f;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        const float t = static_cast<float>(i) / RATE;
        const float f = f0 * std::pow(f1 / f0, t / dur);
        ph += f / RATE;
        ph -= std::floor(ph);
        const float tail = std::min(1.0f, (dur - t) * 200.0f);   // no click at the end
        b[s0 + i] += Osc(w, ph) * amp * Env(t, attack, decay) * tail;
    }
}

// Filtered noise with a sweeping low-pass and an optional high-pass. The level
// is compensated for the filter, so amp means roughly the same at any cutoff.
void Hiss(Buf& b, float start, float dur, float amp, float attack, float decay,
          float lp0, float lp1, float hp = 0.0f, uint32_t seed = 7) {
    Noise n(seed);
    const size_t s0 = static_cast<size_t>(start * RATE);
    const size_t len = static_cast<size_t>(dur * RATE);
    float lp = 0.0f, hs = 0.0f;
    const float hc = hp > 0.0f ? Coef(hp) : 0.0f;
    for (size_t i = 0; i < len && s0 + i < b.size(); ++i) {
        const float t = static_cast<float>(i) / RATE;
        const float c = Coef(lp0 * std::pow(lp1 / lp0, t / dur));
        lp += (n.Next() - lp) * c;
        float v = lp * std::sqrt((2.0f - c) / c) * 0.6f;
        if (hp > 0.0f) { hs += (v - hs) * hc; v -= hs; }
        const float tail = std::min(1.0f, (dur - t) * 200.0f);
        b[s0 + i] += v * amp * Env(t, attack, decay) * tail;
    }
}

// Struck metal: inharmonic partials, the high ones dying first.
void Bell(Buf& b, float start, float f, float amp, float decay) {
    static const float kRatio[] = {1.0f, 2.76f, 5.40f, 8.93f};
    static const float kAmp[]   = {1.0f, 0.5f, 0.25f, 0.12f};
    for (int k = 0; k < 4; ++k)
        Tone(b, start, decay * 6.0f / (1 + k), f * kRatio[k], f * kRatio[k],
             amp * kAmp[k], 0.001f, decay / (1.0f + k * 0.8f));
}

// Karplus-Strong: a burst of noise in a delay line that averages itself into
// a plucked string.
void Pluck(Buf& b, float start, float f, float amp, float dur, float damp, uint32_t seed) {
    Noise n(seed);
    const int period = std::max(2, static_cast<int>(RATE / f));
    vector<float> ring(period);
    for (float& v : ring) v = n.Next();
    const size_t s0 = static_cast<size_t>(start * RATE);
    const size_t len = static_cast<size_t>(dur * RATE);
    int idx = 0;
    for (size_t i = 0; i < len && s0 + i < b.size(); ++i) {
        const float v = ring[idx];
        ring[idx] = (v + ring[(idx + 1) % period]) * 0.5f * damp;
        idx = (idx + 1) % period;
        const float tail = std::min(1.0f, static_cast<float>(len - i) / (RATE * 0.02f));
        b[s0 + i] += v * amp * tail;
    }
}

void LowpassAll(Buf& b, float hz) {
    const float c = Coef(hz);
    float lp = 0.0f;
    for (float& v : b) { lp += (v - lp) * c; v = lp; }
}

// A few decaying repeats: the cave does the rest.
void Echo(Buf& b, float delay, float feedback, int repeats) {
    const size_t d = static_cast<size_t>(delay * RATE);
    const size_t base = b.size();
    b.resize(base + d * repeats, 0.0f);
    for (size_t i = d; i < b.size(); ++i) b[i] += b[i - d] * feedback;
}

void Normalize(Buf& b, float peak) {
    float m = 0.0f;
    for (float v : b) m = std::max(m, std::fabs(v));
    if (m > 0.0f) for (float& v : b) v *= peak / m;
    // Trim the silent tail so a voice frees up as soon as it is inaudible.
    size_t last = b.size();
    while (last > 1 && std::fabs(b[last - 1]) < 0.0005f) --last;
    b.resize(last);
    const size_t fade = std::min<size_t>(b.size(), 44);
    for (size_t i = 0; i < fade; ++i) b[i] *= static_cast<float>(i) / fade;
}

// --- the sound bank -----------------------------------------------------------

Buf Make(Sfx s) {
    Buf b;
    switch (s) {
    case Sfx::Swing:
        b = Blank(0.22f);
        Hiss(b, 0.0f, 0.22f, 1.0f, 0.035f, 0.06f, 2600.0f, 700.0f, 350.0f, 11);
        Normalize(b, 0.30f);
        break;
    case Sfx::SwingHeavy:
        b = Blank(0.36f);
        Hiss(b, 0.0f, 0.36f, 1.0f, 0.07f, 0.11f, 1900.0f, 380.0f, 140.0f, 12);
        Tone(b, 0.02f, 0.3f, 95.0f, 60.0f, 0.3f, 0.05f, 0.1f);
        Normalize(b, 0.40f);
        break;
    case Sfx::Hit:
        b = Blank(0.16f);
        Tone(b, 0.0f, 0.16f, 150.0f, 55.0f, 1.0f, 0.002f, 0.045f);
        Hiss(b, 0.0f, 0.07f, 0.7f, 0.001f, 0.016f, 3200.0f, 1400.0f, 220.0f, 13);
        Normalize(b, 0.55f);
        break;
    case Sfx::HitCrit:
        b = Blank(0.3f);
        Tone(b, 0.0f, 0.18f, 170.0f, 50.0f, 1.0f, 0.002f, 0.055f);
        Hiss(b, 0.0f, 0.08f, 0.8f, 0.001f, 0.02f, 4200.0f, 1600.0f, 220.0f, 14);
        Bell(b, 0.0f, 1250.0f, 0.22f, 0.05f);
        Normalize(b, 0.65f);
        break;
    case Sfx::Block:
        b = Blank(0.25f);
        Bell(b, 0.0f, 820.0f, 0.5f, 0.045f);
        Hiss(b, 0.0f, 0.05f, 0.6f, 0.001f, 0.012f, 5000.0f, 2500.0f, 800.0f, 15);
        Normalize(b, 0.40f);
        break;
    case Sfx::EnemyDie:
        b = Blank(0.5f);
        Tone(b, 0.0f, 0.45f, 240.0f, 55.0f, 0.6f, 0.004f, 0.14f, TRI);
        Hiss(b, 0.0f, 0.42f, 0.5f, 0.005f, 0.12f, 1300.0f, 260.0f, 80.0f, 16);
        LowpassAll(b, 2600.0f);
        Normalize(b, 0.50f);
        break;
    case Sfx::PlayerHurt:
        b = Blank(0.22f);
        Tone(b, 0.0f, 0.2f, 280.0f, 150.0f, 0.7f, 0.004f, 0.07f, TRI);
        Tone(b, 0.0f, 0.14f, 140.0f, 60.0f, 0.8f, 0.002f, 0.04f);
        Hiss(b, 0.0f, 0.06f, 0.5f, 0.001f, 0.015f, 2500.0f, 1200.0f, 200.0f, 17);
        LowpassAll(b, 3000.0f);
        Normalize(b, 0.55f);
        break;
    case Sfx::PlayerDie:
        b = Blank(1.4f);
        Tone(b, 0.00f, 0.40f, 392.0f, 392.0f, 0.5f, 0.01f, 0.22f, TRI);
        Tone(b, 0.30f, 0.40f, 311.1f, 311.1f, 0.5f, 0.01f, 0.22f, TRI);
        Tone(b, 0.60f, 0.80f, 261.6f, 246.9f, 0.55f, 0.01f, 0.45f, TRI);
        Tone(b, 0.60f, 0.40f, 110.0f, 50.0f, 0.6f, 0.003f, 0.12f);
        LowpassAll(b, 2200.0f);
        Normalize(b, 0.45f);
        break;
    case Sfx::BowShot:
        b = Blank(0.4f);
        Pluck(b, 0.0f, 180.0f, 0.9f, 0.38f, 0.986f, 21);
        Hiss(b, 0.015f, 0.2f, 0.45f, 0.01f, 0.06f, 5000.0f, 1500.0f, 900.0f, 22);
        LowpassAll(b, 5000.0f);
        Normalize(b, 0.45f);
        break;
    case Sfx::SpellCast:
        b = Blank(0.45f);
        Tone(b, 0.0f, 0.42f, 480.0f, 1350.0f, 0.35f, 0.05f, 0.14f);
        Tone(b, 0.0f, 0.42f, 725.0f, 2000.0f, 0.18f, 0.06f, 0.14f);
        Hiss(b, 0.0f, 0.4f, 0.35f, 0.05f, 0.12f, 2800.0f, 7000.0f, 1600.0f, 23);
        Normalize(b, 0.38f);
        break;
    case Sfx::Impact:
        b = Blank(0.12f);
        Tone(b, 0.0f, 0.1f, 190.0f, 70.0f, 0.8f, 0.001f, 0.03f);
        Hiss(b, 0.0f, 0.09f, 0.6f, 0.001f, 0.022f, 2400.0f, 700.0f, 150.0f, 24);
        Normalize(b, 0.40f);
        break;
    case Sfx::Pickup:
        b = Blank(0.24f);
        Tone(b, 0.00f, 0.09f, 880.0f, 880.0f, 0.5f, 0.002f, 0.04f, TRI);
        Tone(b, 0.07f, 0.16f, 1318.5f, 1318.5f, 0.5f, 0.002f, 0.06f, TRI);
        LowpassAll(b, 4500.0f);
        Normalize(b, 0.28f);
        break;
    case Sfx::Coins: {
        b = Blank(0.5f);
        Noise n(31);
        for (int i = 0; i < 5; ++i)
            Bell(b, i * 0.05f + n.Range(0.0f, 0.02f), n.Range(2100.0f, 3000.0f),
                 n.Range(0.25f, 0.4f), 0.07f);
        Normalize(b, 0.32f);
        break;
    }
    case Sfx::Chop:
        b = Blank(0.14f);
        Tone(b, 0.0f, 0.13f, 230.0f, 150.0f, 1.0f, 0.001f, 0.035f, TRI);
        Hiss(b, 0.0f, 0.08f, 0.8f, 0.001f, 0.018f, 1700.0f, 900.0f, 260.0f, 32);
        LowpassAll(b, 3200.0f);
        Normalize(b, 0.50f);
        break;
    case Sfx::Mine:
        b = Blank(0.5f);
        Bell(b, 0.0f, 1580.0f, 0.6f, 0.08f);
        Hiss(b, 0.0f, 0.04f, 0.7f, 0.001f, 0.01f, 6000.0f, 3000.0f, 900.0f, 33);
        Tone(b, 0.0f, 0.08f, 130.0f, 90.0f, 0.4f, 0.001f, 0.025f);
        Normalize(b, 0.42f);
        break;
    case Sfx::Cook:
    case Sfx::Burn: {
        const bool burn = (s == Sfx::Burn);
        b = Blank(0.7f);
        Hiss(b, 0.0f, 0.7f, 0.5f, 0.05f, 0.28f, burn ? 3500.0f : 7000.0f,
             burn ? 2000.0f : 6000.0f, 2400.0f, 34);
        Noise n(35);
        for (int i = 0; i < 14; ++i)
            Hiss(b, n.Range(0.0f, 0.55f), 0.006f, n.Range(0.4f, 1.0f), 0.0005f, 0.002f,
                 6000.0f, 6000.0f, 1500.0f, 36 + i);
        if (burn) Tone(b, 0.1f, 0.4f, 300.0f, 140.0f, 0.3f, 0.02f, 0.15f, TRI);
        LowpassAll(b, 9000.0f);
        Normalize(b, burn ? 0.38f : 0.32f);
        break;
    }
    case Sfx::ChestOpen:
        b = Blank(0.7f);
        Tone(b, 0.0f, 0.34f, 105.0f, 150.0f, 0.5f, 0.06f, 0.18f, SAW);
        LowpassAll(b, 900.0f);
        Bell(b, 0.32f, 1760.0f, 0.22f, 0.12f);
        Bell(b, 0.40f, 2637.0f, 0.18f, 0.12f);
        Normalize(b, 0.42f);
        break;
    case Sfx::Eat:
        b = Blank(0.36f);
        Hiss(b, 0.00f, 0.07f, 0.9f, 0.002f, 0.02f, 2600.0f, 1800.0f, 400.0f, 41);
        Hiss(b, 0.12f, 0.07f, 0.8f, 0.002f, 0.02f, 2300.0f, 1600.0f, 400.0f, 42);
        Hiss(b, 0.25f, 0.07f, 0.7f, 0.002f, 0.02f, 2000.0f, 1400.0f, 400.0f, 43);
        Normalize(b, 0.35f);
        break;
    case Sfx::Equip:
        b = Blank(0.25f);
        Hiss(b, 0.0f, 0.13f, 0.6f, 0.01f, 0.04f, 3500.0f, 2500.0f, 900.0f, 44);
        Bell(b, 0.05f, 1900.0f, 0.3f, 0.05f);
        Normalize(b, 0.32f);
        break;
    case Sfx::Footstep:
        b = Blank(0.08f);
        Hiss(b, 0.0f, 0.075f, 1.0f, 0.003f, 0.018f, 800.0f, 380.0f, 60.0f, 51);
        Tone(b, 0.0f, 0.05f, 95.0f, 60.0f, 0.3f, 0.002f, 0.015f);
        Normalize(b, 0.16f);
        break;
    case Sfx::FootstepWood:
        b = Blank(0.08f);
        Tone(b, 0.0f, 0.07f, 170.0f, 125.0f, 0.8f, 0.001f, 0.02f, TRI);
        Hiss(b, 0.0f, 0.05f, 0.5f, 0.001f, 0.012f, 1800.0f, 1200.0f, 300.0f, 52);
        LowpassAll(b, 2500.0f);
        Normalize(b, 0.16f);
        break;
    case Sfx::FootstepStone:
        b = Blank(0.07f);
        Hiss(b, 0.0f, 0.065f, 0.9f, 0.001f, 0.013f, 3600.0f, 2000.0f, 650.0f, 53);
        Tone(b, 0.0f, 0.04f, 125.0f, 90.0f, 0.3f, 0.001f, 0.012f);
        Normalize(b, 0.14f);
        break;
    case Sfx::Jump:
        b = Blank(0.2f);
        Hiss(b, 0.0f, 0.2f, 0.6f, 0.02f, 0.06f, 700.0f, 2200.0f, 200.0f, 54);
        Normalize(b, 0.16f);
        break;
    case Sfx::Land:
        b = Blank(0.12f);
        Hiss(b, 0.0f, 0.1f, 1.0f, 0.002f, 0.025f, 900.0f, 350.0f, 50.0f, 55);
        Tone(b, 0.0f, 0.08f, 110.0f, 55.0f, 0.6f, 0.002f, 0.025f);
        Normalize(b, 0.26f);
        break;
    case Sfx::Door:
        b = Blank(0.5f);
        Tone(b, 0.0f, 0.2f, 100.0f, 70.0f, 0.9f, 0.002f, 0.06f, TRI);
        Tone(b, 0.06f, 0.34f, 135.0f, 185.0f, 0.3f, 0.05f, 0.14f, SAW);
        LowpassAll(b, 1100.0f);
        Normalize(b, 0.42f);
        break;
    case Sfx::Portal:
        b = Blank(0.75f);
        Hiss(b, 0.0f, 0.72f, 0.7f, 0.25f, 0.22f, 380.0f, 2400.0f, 150.0f, 56);
        Tone(b, 0.0f, 0.72f, 220.0f, 330.0f, 0.1f, 0.3f, 0.25f);
        Normalize(b, 0.26f);
        break;
    case Sfx::Locked:
        b = Blank(0.24f);
        Tone(b, 0.00f, 0.06f, 150.0f, 130.0f, 0.8f, 0.001f, 0.02f, TRI);
        Tone(b, 0.10f, 0.06f, 150.0f, 130.0f, 0.8f, 0.001f, 0.02f, TRI);
        Bell(b, 0.0f, 1200.0f, 0.25f, 0.03f);
        LowpassAll(b, 3000.0f);
        Normalize(b, 0.38f);
        break;
    case Sfx::UiMove:
        b = Blank(0.04f);
        Tone(b, 0.0f, 0.035f, 1500.0f, 1400.0f, 0.6f, 0.001f, 0.01f);
        Normalize(b, 0.10f);
        break;
    case Sfx::UiConfirm:
        b = Blank(0.2f);
        Tone(b, 0.00f, 0.07f, 659.3f, 659.3f, 0.5f, 0.002f, 0.04f, TRI);
        Tone(b, 0.06f, 0.13f, 987.8f, 987.8f, 0.5f, 0.002f, 0.06f, TRI);
        LowpassAll(b, 4000.0f);
        Normalize(b, 0.20f);
        break;
    case Sfx::UiBack:
        b = Blank(0.2f);
        Tone(b, 0.00f, 0.07f, 740.0f, 740.0f, 0.5f, 0.002f, 0.04f, TRI);
        Tone(b, 0.06f, 0.13f, 493.9f, 493.9f, 0.5f, 0.002f, 0.06f, TRI);
        LowpassAll(b, 4000.0f);
        Normalize(b, 0.18f);
        break;
    case Sfx::UiError:
        b = Blank(0.18f);
        Tone(b, 0.0f, 0.16f, 150.0f, 150.0f, 0.6f, 0.003f, 0.08f, SAW);
        Tone(b, 0.0f, 0.16f, 155.0f, 155.0f, 0.6f, 0.003f, 0.08f, SAW);
        LowpassAll(b, 1400.0f);
        Normalize(b, 0.22f);
        break;
    case Sfx::LevelUp: {
        b = Blank(1.3f);
        const float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
        for (int i = 0; i < 4; ++i) {
            const float t = i * 0.09f;
            const float len = (i == 3) ? 1.0f : 0.3f;
            Tone(b, t, len, notes[i], notes[i], 0.45f, 0.004f, i == 3 ? 0.4f : 0.12f, TRI);
            Tone(b, t, len, notes[i] * 2, notes[i] * 2, 0.12f, 0.004f, 0.1f);
        }
        Tone(b, 0.27f, 1.0f, 261.6f, 261.6f, 0.2f, 0.02f, 0.45f, TRI);
        Tone(b, 0.27f, 1.0f, 392.0f, 392.0f, 0.16f, 0.02f, 0.45f, TRI);
        LowpassAll(b, 6000.0f);
        Normalize(b, 0.42f);
        break;
    }
    case Sfx::QuestStart:
        b = Blank(0.8f);
        Tone(b, 0.00f, 0.35f, 392.0f, 392.0f, 0.5f, 0.02f, 0.2f, TRI);
        Tone(b, 0.14f, 0.6f, 587.3f, 587.3f, 0.5f, 0.02f, 0.28f, TRI);
        Tone(b, 0.14f, 0.6f, 293.7f, 293.7f, 0.2f, 0.03f, 0.28f);
        LowpassAll(b, 3500.0f);
        Normalize(b, 0.36f);
        break;
    case Sfx::QuestComplete: {
        b = Blank(1.6f);
        const float notes[] = {392.0f, 523.25f, 659.25f, 783.99f};
        for (int i = 0; i < 4; ++i) {
            const float t = i * 0.12f;
            const float len = (i == 3) ? 1.2f : 0.3f;
            Tone(b, t, len, notes[i], notes[i], 0.45f, 0.01f, i == 3 ? 0.55f : 0.13f, TRI);
        }
        Tone(b, 0.36f, 1.2f, 261.6f, 261.6f, 0.25f, 0.03f, 0.6f, TRI);
        Tone(b, 0.36f, 1.2f, 329.6f, 329.6f, 0.2f, 0.03f, 0.6f, TRI);
        Tone(b, 0.36f, 1.2f, 130.8f, 130.8f, 0.2f, 0.03f, 0.6f);
        LowpassAll(b, 4500.0f);
        Normalize(b, 0.45f);
        break;
    }
    default:
        b = Blank(0.01f);
        break;
    }
    return b;
}

// A songbird: a handful of quick whistled syllables, each a pitch sweep with
// a little vibrato. The variants differ in how many, how high and how fast.
Buf MakeBird(uint32_t seed) {
    Noise n(seed);
    Buf b = Blank(1.2f);
    const int syllables = 2 + static_cast<int>(n.Unit() * 5.0f);
    const bool trill = n.Unit() < 0.35f;
    const float base = n.Range(2400.0f, 4200.0f);
    float t = 0.0f;
    for (int i = 0; i < syllables && t < 1.0f; ++i) {
        const float dur = trill ? n.Range(0.025f, 0.04f) : n.Range(0.05f, 0.12f);
        const float f0 = base * n.Range(0.85f, 1.2f);
        const float f1 = f0 * (trill ? n.Range(0.9f, 1.1f) : n.Range(0.6f, 1.5f));
        const size_t s0 = static_cast<size_t>(t * RATE);
        const size_t len = static_cast<size_t>(dur * RATE);
        float ph = 0.0f;
        for (size_t k = 0; k < len && s0 + k < b.size(); ++k) {
            const float u = static_cast<float>(k) / len;
            const float vib = 1.0f + 0.04f * std::sin(u * dur * TAU * 32.0f);
            ph += f0 * std::pow(f1 / f0, u) * vib / RATE;
            ph -= std::floor(ph);
            const float env = std::sin(u * 3.14159f);
            b[s0 + k] += (std::sin(ph * TAU) + 0.12f * std::sin(ph * 2 * TAU)) * env;
        }
        t += dur + (trill ? n.Range(0.012f, 0.03f) : n.Range(0.03f, 0.1f));
    }
    Normalize(b, 0.16f);
    return b;
}

Buf MakeDrip(uint32_t seed) {
    Noise n(seed);
    Buf b = Blank(0.2f);
    const float f = n.Range(900.0f, 1600.0f);
    Tone(b, 0.0f, 0.12f, f, f * 0.55f, 1.0f, 0.001f, 0.025f);
    Echo(b, n.Range(0.07f, 0.11f), 0.38f, 5);
    Normalize(b, 0.15f);
    return b;
}

Buf MakeCrackle(uint32_t seed) {
    Noise n(seed);
    Buf b = Blank(0.3f);
    const int pops = 1 + static_cast<int>(n.Unit() * 4.0f);
    for (int i = 0; i < pops; ++i)
        Hiss(b, n.Range(0.0f, 0.25f), n.Range(0.002f, 0.007f), n.Range(0.3f, 1.0f),
             0.0005f, 0.002f, 5500.0f, 4000.0f, 900.0f, seed * 7 + i);
    Normalize(b, n.Range(0.06f, 0.13f));
    return b;
}

// --- mixer state --------------------------------------------------------------------

struct Voice {
    const Buf* buf = nullptr;
    double pos = 0.0;
    float step = 1.0f;
    float gl = 0.0f, gr = 0.0f;
    bool ambient = false;
};

struct Profile {
    float wind = 0.0f, wind_cut = 1.0f;
    float bird_lo = 0.0f, bird_hi = 0.0f;
    float drone = 0.0f;
    float drip_lo = 0.0f, drip_hi = 0.0f;
    float fire = 0.0f;
    float crackle_lo = 0.0f, crackle_hi = 0.0f;
};

struct State {
    SDL_AudioStream* stream = nullptr;
    bool enabled = false;
    bool built = false;

    vector<Buf> bank;
    vector<Buf> birds, drips, crackles;

    static constexpr int VOICES = 32;
    Voice voices[VOICES];

    float listener_x = 0.0f, listener_y = 0.0f;
    float master = 0.8f, sfx = 1.0f, amb = 0.8f;

    Profile target;
    float wind_g = 0.0f, drone_g = 0.0f, fire_g = 0.0f, cut_g = 1.0f;
    float bird_timer = 2.0f, drip_timer = 3.0f, crackle_timer = 0.5f;

    Noise nl{101}, nr{202}, fx{303}, play{404};
    float wl = 0.0f, wr = 0.0f, wl2 = 0.0f, wr2 = 0.0f;
    float lfo = 0.0f, drone_a = 0.0f, drone_b = 0.0f, fire_lp = 0.0f, fire_lp2 = 0.0f;
};

State g;

struct Lock {
    Lock()  { if (g.stream) SDL_LockAudioStream(g.stream); }
    ~Lock() { if (g.stream) SDL_UnlockAudioStream(g.stream); }
};

void Build() {
    if (g.built) return;
    g.bank.resize(static_cast<size_t>(Sfx::Count));
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i)
        g.bank[i] = Make(static_cast<Sfx>(i));
    for (uint32_t i = 0; i < 10; ++i) g.birds.push_back(MakeBird(1000 + i * 37));
    for (uint32_t i = 0; i < 5; ++i)  g.drips.push_back(MakeDrip(2000 + i * 53));
    for (uint32_t i = 0; i < 8; ++i)  g.crackles.push_back(MakeCrackle(3000 + i * 71));
    g.built = true;
}

// Must be called with the stream locked (or from the mixer itself).
void Start(const Buf& buf, float volume, float pitch, float pan, bool ambient) {
    int slot = -1;
    double most = -1.0;
    for (int i = 0; i < State::VOICES; ++i) {
        if (!g.voices[i].buf) { slot = i; break; }
        // Out of voices: take over the one nearest its end.
        const double done = g.voices[i].pos / g.voices[i].buf->size();
        if (done > most) { most = done; slot = i; }
    }
    Voice& v = g.voices[slot];
    v.buf = &buf;
    v.pos = 0.0;
    v.step = std::clamp(pitch, 0.25f, 4.0f);
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    v.gl = volume * std::cos(a) * 1.4142f;
    v.gr = volume * std::sin(a) * 1.4142f;
    v.ambient = ambient;
}

void SDLCALL Feed(void*, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0) return;
    const int frame_bytes = static_cast<int>(sizeof(float) * 2);
    const int frames = (additional + frame_bytes - 1) / frame_bytes;
    static vector<float> scratch;
    scratch.resize(static_cast<size_t>(frames) * 2);
    // SDL holds the stream's lock for the length of this callback.
    Audio::Mix(scratch.data(), frames);
    SDL_PutAudioStreamData(stream, scratch.data(), frames * 2 * static_cast<int>(sizeof(float)));
}

}  // namespace

namespace Audio {

bool Init() {
    if (g.enabled) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio: no audio subsystem (%s); playing silently", SDL_GetError());
        return false;
    }
    Build();
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = RATE;
    g.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
    if (!g.stream) {
        SDL_Log("Audio: could not open a playback device (%s); playing silently", SDL_GetError());
        return false;
    }
    g.enabled = true;
    SDL_ResumeAudioStreamDevice(g.stream);
    SDL_Log("Audio: %d Hz stereo, %d sounds synthesised", RATE, static_cast<int>(Sfx::Count));
    return true;
}

bool InitOffline() {
    Build();
    g.enabled = true;
    return true;
}

void Shutdown() {
    if (g.stream) {
        SDL_DestroyAudioStream(g.stream);
        g.stream = nullptr;
    }
    g.enabled = false;
    for (Voice& v : g.voices) v.buf = nullptr;
}

bool Enabled() { return g.enabled; }

void Play(Sfx s, float volume, float pitch) {
    if (!g.enabled || s >= Sfx::Count) return;
    Lock lock;
    // Menu sounds are exact; everything else varies a touch so a run of the
    // same hit does not sound like a machine.
    const bool ui = (s >= Sfx::UiMove && s <= Sfx::UiError) ||
                    s == Sfx::LevelUp || s == Sfx::QuestStart || s == Sfx::QuestComplete;
    if (!ui) pitch *= g.play.Range(0.95f, 1.05f);
    Start(g.bank[static_cast<size_t>(s)], volume, pitch, 0.0f, false);
}

void PlayAt(Sfx s, float x, float y, float volume, float pitch) {
    if (!g.enabled) return;
    const float dx = x - g.listener_x, dy = y - g.listener_y;
    const float d = std::sqrt(dx * dx + dy * dy);
    const float fall = std::clamp(1.0f - (d - 90.0f) / 420.0f, 0.0f, 1.0f);
    if (fall <= 0.01f) return;
    Lock lock;
    const float p = pitch * g.play.Range(0.94f, 1.06f);
    Start(g.bank[static_cast<size_t>(s)], volume * fall * fall, p,
          std::clamp(dx / 360.0f, -1.0f, 1.0f) * 0.7f, false);
}

void SetListener(float x, float y) {
    // Two floats read by the mixer only for new voices started from this
    // thread, so no lock is needed.
    g.listener_x = x;
    g.listener_y = y;
}

void SetAmbience(const string& kind, bool interior) {
    Profile p;
    if (kind == "dungeon") {
        p.wind = 0.05f; p.wind_cut = 0.45f;
        p.drone = 0.045f;
        p.drip_lo = 1.6f; p.drip_hi = 5.5f;
    } else if (interior) {
        p.fire = 0.05f;
        p.crackle_lo = 0.08f; p.crackle_hi = 0.7f;
    } else if (kind == "forest") {
        p.wind = 0.15f; p.bird_lo = 1.0f; p.bird_hi = 4.0f;
    } else if (kind == "grove") {
        p.wind = 0.11f; p.bird_lo = 2.0f; p.bird_hi = 6.5f;
    } else if (kind == "town") {
        p.wind = 0.08f; p.bird_lo = 3.0f; p.bird_hi = 9.0f;
    } else if (kind == "menu") {
        p.wind = 0.10f; p.wind_cut = 0.8f; p.bird_lo = 6.0f; p.bird_hi = 14.0f;
    } else if (!kind.empty()) {
        p.wind = 0.17f; p.wind_cut = 1.2f; p.bird_lo = 4.0f; p.bird_hi = 11.0f;
    }
    Lock lock;
    g.target = p;
}

void SetVolumes(float master, float sfx, float ambience) {
    Lock lock;
    g.master = std::clamp(master, 0.0f, 1.0f);
    g.sfx    = std::clamp(sfx, 0.0f, 1.0f);
    g.amb    = std::clamp(ambience, 0.0f, 1.0f);
}

const vector<float>& Samples(Sfx s) {
    Build();
    return g.bank[std::min(static_cast<size_t>(s), g.bank.size() - 1)];
}

int ActiveVoices() {
    Lock lock;
    int n = 0;
    for (const Voice& v : g.voices) if (v.buf) ++n;
    return n;
}

void Mix(float* out, int frames) {
    const Profile& t = g.target;
    const float block = static_cast<float>(frames) / RATE;

    // --- one-shot ambience, scheduled per block -----------------------------
    auto schedule = [&](float& timer, float lo, float hi, const vector<Buf>& set,
                        float vol_lo, float vol_hi, float spread) {
        if (hi <= 0.0f || set.empty()) return;
        timer -= block;
        if (timer > 0.0f) return;
        timer = g.fx.Range(lo, hi);
        const Buf& b = set[static_cast<size_t>(g.fx.Unit() * set.size()) % set.size()];
        Start(b, g.fx.Range(vol_lo, vol_hi), g.fx.Range(0.9f, 1.1f),
              g.fx.Range(-spread, spread), true);
    };
    if (g.built) {
        schedule(g.bird_timer, t.bird_lo, t.bird_hi, g.birds, 0.25f, 1.0f, 0.9f);
        schedule(g.drip_timer, t.drip_lo, t.drip_hi, g.drips, 0.3f, 1.0f, 0.8f);
        schedule(g.crackle_timer, t.crackle_lo, t.crackle_hi, g.crackles, 0.5f, 1.0f, 0.3f);
    }

    const float sfx_bus = g.master * g.sfx;
    const float amb_bus = g.master * g.amb;
    constexpr float SMOOTH = 1.0f / (0.8f * RATE);   // layers fade over most of a second

    for (int i = 0; i < frames; ++i) {
        g.wind_g  += (t.wind - g.wind_g) * SMOOTH;
        g.drone_g += (t.drone - g.drone_g) * SMOOTH;
        g.fire_g  += (t.fire - g.fire_g) * SMOOTH;
        g.cut_g   += (t.wind_cut - g.cut_g) * SMOOTH;

        float l = 0.0f, r = 0.0f;

        // Wind: two filtered noises, one per ear, with a slow swell and gusts.
        if (g.wind_g > 0.0005f) {
            g.lfo += 1.0f / RATE;
            const float swell = 0.55f + 0.3f * std::sin(g.lfo * TAU * 0.07f) +
                                0.15f * std::sin(g.lfo * TAU * 0.19f + 1.3f);
            const float cut = (220.0f + 420.0f * swell) * g.cut_g;
            const float c = Coef(cut);
            const float comp = std::sqrt((2.0f - c) / c) * 1.6f;
            g.wl += (g.nl.Next() - g.wl) * c;  g.wl2 += (g.wl - g.wl2) * c;
            g.wr += (g.nr.Next() - g.wr) * c;  g.wr2 += (g.wr - g.wr2) * c;
            l += g.wl2 * comp * swell * g.wind_g;
            r += g.wr2 * comp * swell * g.wind_g;
        }

        // The mine's hum: a low fifth that breathes.
        if (g.drone_g > 0.0005f) {
            g.drone_a += 55.0f / RATE;  g.drone_a -= std::floor(g.drone_a);
            g.drone_b += 82.6f / RATE;  g.drone_b -= std::floor(g.drone_b);
            const float breathe = 0.7f + 0.3f * std::sin(g.lfo * TAU * 0.11f);
            if (g.wind_g <= 0.0005f) g.lfo += 1.0f / RATE;
            const float d = (std::sin(g.drone_a * TAU) * 0.6f +
                             std::sin(g.drone_b * TAU) * 0.4f +
                             std::sin(g.drone_a * 2.0f * TAU) * 0.12f) * breathe * g.drone_g;
            l += d;
            r += d;
        }

        // A hearth: a low rumble under the crackles.
        if (g.fire_g > 0.0005f) {
            const float c = Coef(160.0f);
            g.fire_lp  += (g.fx.Next() - g.fire_lp) * c;
            g.fire_lp2 += (g.fire_lp - g.fire_lp2) * c;
            const float f = g.fire_lp2 * std::sqrt((2.0f - c) / c) * 1.8f * g.fire_g;
            l += f;
            r += f;
        }

        l *= amb_bus;
        r *= amb_bus;

        for (Voice& v : g.voices) {
            if (!v.buf) continue;
            const size_t i0 = static_cast<size_t>(v.pos);
            if (i0 + 1 >= v.buf->size()) { v.buf = nullptr; continue; }
            const float frac = static_cast<float>(v.pos - i0);
            const float s = (*v.buf)[i0] + ((*v.buf)[i0 + 1] - (*v.buf)[i0]) * frac;
            const float bus = v.ambient ? amb_bus : sfx_bus;
            l += s * v.gl * bus;
            r += s * v.gr * bus;
            v.pos += v.step;
        }

        // A soft ceiling instead of a hard clip when a lot happens at once.
        out[i * 2]     = std::tanh(l);
        out[i * 2 + 1] = std::tanh(r);
    }
}

}  // namespace Audio
