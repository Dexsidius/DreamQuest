#include "audio.h"
#include <atomic>
#include <thread>

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

// A bowstring let go: its note, thick and buzzing, settling from `f0` to `f1`
// as the limbs come to rest, and gone within a few dozen cycles. A string on a
// bow is short, taut and damped by the limbs and the arrow rest: it thrums and
// stops, where a harp's rings on -- which is what the bow used to be, a
// Karplus-Strong pluck left to ring.
void Thrum(Buf& b, float start, float f0, float f1, float amp, float decay, uint32_t seed) {
    Noise n(seed);
    const size_t s0 = static_cast<size_t>(start * RATE);
    const float dur = decay * 7.0f;
    const size_t len = static_cast<size_t>(dur * RATE);
    const float soft = Coef(1500.0f);
    float ph = 0.0f, lp = 0.0f;
    for (size_t i = 0; i < len && s0 + i < b.size(); ++i) {
        const float t = static_cast<float>(i) / RATE;
        const float f = f1 + (f0 - f1) * std::exp(-t / (decay * 0.8f));
        ph += f / RATE;
        ph -= std::floor(ph);
        // A saw for the buzz, a sine for the body, and a little noise: a string
        // is not a clean oscillator.
        const float raw = 0.8f * Osc(SAW, ph) + 0.6f * Osc(SINE, ph) + 0.15f * n.Next();
        lp += (raw - lp) * soft;
        const float tail = std::min(1.0f, (dur - t) * 200.0f);
        b[s0 + i] += lp * amp * Env(t, 0.0006f, decay) * tail;
    }
}

// A throat: a buzzing saw over a sine an octave down, its pitch climbing to
// a peak `peak_at` of the way through and falling away after, roughened by a
// fast flutter in its loudness and a slow wander in its pitch -- a growl, a
// snarl, a roar. `rough` 0 is a clean tone, 1 a beast.
void Growl(Buf& b, float start, float dur, float f0, float f_peak, float f1, float peak_at,
           float amp, float attack, float decay, float rough, uint32_t seed) {
    Noise n(seed);
    const size_t s0 = static_cast<size_t>(start * RATE);
    const size_t len = static_cast<size_t>(dur * RATE);
    const float flutter_c = Coef(38.0f), wander_c = Coef(5.0f);
    float ph = 0.0f, sub = 0.0f, flutter = 0.0f, wander = 0.0f;
    for (size_t i = 0; i < len && s0 + i < b.size(); ++i) {
        const float t = static_cast<float>(i) / RATE;
        const float k = t / dur;
        float f = k < peak_at ? f0 * std::pow(f_peak / f0, k / std::max(0.001f, peak_at))
                              : f_peak * std::pow(f1 / f_peak, (k - peak_at) / std::max(0.001f, 1.0f - peak_at));
        // The filtered noise is small: these bring the wander to about five
        // per cent of the pitch and the flutter to about a third of the level.
        wander += (n.Next() - wander) * wander_c;
        f *= 1.0f + 4.5f * rough * wander;
        ph  += f / RATE;          ph  -= std::floor(ph);
        sub += f * 0.5f / RATE;   sub -= std::floor(sub);
        flutter += (n.Next() - flutter) * flutter_c;
        const float am = std::clamp(1.0f + 12.0f * rough * flutter, 0.0f, 2.0f);
        const float v = 0.65f * Osc(SAW, ph) + 0.55f * Osc(SINE, sub);
        const float tail = std::min(1.0f, (dur - t) * 200.0f);
        b[s0 + i] += v * am * amp * Env(t, attack, decay) * tail;
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
        // A bow let go: the string slapping home against the limbs -- a crack
        // and a thump -- its note thrumming a moment and gone, and the arrow
        // tearing the air as it leaves, bright and then dulling as it goes. It
        // was a plucked string left to ring for four tenths of a second, which
        // is a harp and not a bow.
        // Nothing much below a hundred hertz, which a laptop's or a Deck's
        // speakers would throw away.
        b = Blank(0.3f);
        Hiss(b, 0.0f, 0.02f, 1.0f, 0.0004f, 0.004f, 10000.0f, 5000.0f, 2000.0f, 22);   // the crack
        Hiss(b, 0.0f, 0.04f, 0.8f, 0.0005f, 0.008f, 2600.0f, 1100.0f, 350.0f, 24);     // the slap of the string
        Tone(b, 0.0f, 0.07f, 210.0f, 85.0f, 0.55f, 0.0006f, 0.016f);                    // the limbs' thump
        Thrum(b, 0.001f, 150.0f, 112.0f, 0.5f, 0.03f, 21);                              // the string's note
        Hiss(b, 0.01f, 0.24f, 0.95f, 0.012f, 0.065f, 7500.0f, 1600.0f, 1100.0f, 25);    // the arrow going
        Tone(b, 0.012f, 0.12f, 2100.0f, 1300.0f, 0.05f, 0.012f, 0.04f);                 // its fletching's whistle
        LowpassAll(b, 11000.0f);
        // A touch louder at its peak than the old one: it is a punch now, not
        // a note held, and would otherwise sit under everything else.
        Normalize(b, 0.50f);
        break;
    case Sfx::KnifeThrow:
        // A blade leaving the hand: air, and a thin edge of steel turning in
        // it. There is no string on a knife, so there is nothing to pluck --
        // the bow's sound on a thrown knife was a bowstring with no bow.
        b = Blank(0.26f);
        Hiss(b, 0.0f, 0.17f, 0.75f, 0.004f, 0.05f, 900.0f, 6200.0f, 1300.0f, 61);
        Tone(b, 0.004f, 0.085f, 2700.0f, 1500.0f, 0.10f, 0.002f, 0.03f, TRI);
        LowpassAll(b, 9000.0f);
        Normalize(b, 0.34f);
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
    case Sfx::Winded:
        // Two heavy breaths, in and out, low and airy.
        b = Blank(0.95f);
        Hiss(b, 0.00f, 0.38f, 0.8f, 0.14f, 0.12f, 900.0f, 1500.0f, 180.0f, 61);
        Hiss(b, 0.42f, 0.50f, 1.0f, 0.06f, 0.18f, 1300.0f, 600.0f, 140.0f, 62);
        Normalize(b, 0.28f);
        break;
    case Sfx::Sleep: {
        // Falling asleep: a slow falling arpeggio over a soft low chord.
        b = Blank(2.6f);
        const float notes[] = {784.0f, 659.3f, 523.3f, 392.0f};
        for (int i = 0; i < 4; ++i)
            Tone(b, i * 0.28f, 1.6f, notes[i], notes[i] * 0.995f, 0.3f, 0.05f, 0.5f, TRI);
        Tone(b, 0.0f, 2.5f, 196.0f, 196.0f, 0.16f, 0.6f, 1.1f);
        Tone(b, 0.0f, 2.5f, 293.7f, 293.7f, 0.10f, 0.7f, 1.1f);
        LowpassAll(b, 2600.0f);
        Normalize(b, 0.3f);
        break;
    }
    case Sfx::Wake: {
        // Waking: the same notes rising, brighter, and a bell at the top.
        b = Blank(2.0f);
        const float notes[] = {392.0f, 523.3f, 659.3f, 784.0f};
        for (int i = 0; i < 4; ++i)
            Tone(b, i * 0.16f, 0.9f, notes[i], notes[i], 0.34f, 0.01f, 0.3f, TRI);
        Bell(b, 0.62f, 1568.0f, 0.2f, 0.35f);
        Tone(b, 0.5f, 1.4f, 261.6f, 261.6f, 0.12f, 0.1f, 0.6f);
        LowpassAll(b, 5000.0f);
        Normalize(b, 0.32f);
        break;
    }
    case Sfx::Splash: {
        // A line going into water: a soft plop, a hiss of spray, two bubbles.
        b = Blank(0.6f);
        Tone(b, 0.0f, 0.09f, 620.0f, 180.0f, 0.8f, 0.002f, 0.04f);
        Hiss(b, 0.01f, 0.30f, 0.6f, 0.005f, 0.08f, 3200.0f, 900.0f, 400.0f, 77);
        Tone(b, 0.18f, 0.05f, 900.0f, 1400.0f, 0.25f, 0.002f, 0.02f);
        Tone(b, 0.30f, 0.05f, 1100.0f, 1700.0f, 0.18f, 0.002f, 0.02f);
        LowpassAll(b, 4000.0f);
        Normalize(b, 0.30f);
        break;
    }
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
    case Sfx::Throw:
        // Something let go of by hand -- a rock, a lump of ice, a snowball --
        // and nothing but air: no string, no steel. The arm coming over is a
        // dark swell that brightens, and the thing leaving falls away from
        // it, so it reads as one "fwoo-sh". A Swing starts bright and only
        // falls; a knife's throw is thin and sharp at the front; this is
        // softer and fuller than either, with a muffled puff where it leaves.
        b = Blank(0.24f);
        Hiss(b, 0.00f, 0.13f, 1.0f, 0.060f, 0.050f, 700.0f, 2400.0f, 260.0f, 71);
        Hiss(b, 0.08f, 0.15f, 0.7f, 0.010f, 0.050f, 2400.0f, 900.0f, 320.0f, 72);
        Hiss(b, 0.07f, 0.05f, 0.45f, 0.004f, 0.015f, 520.0f, 300.0f, 80.0f, 73);
        LowpassAll(b, 6000.0f);
        Normalize(b, 0.34f);
        break;
    case Sfx::KnifeHit:
        // A blade going into something: a short, dull thunk -- higher and
        // shorter than Hit's blow, and muffled by what it went into -- with a
        // thin bright tick of the point on top of it.
        b = Blank(0.17f);
        Tone(b, 0.0f, 0.14f, 240.0f, 105.0f, 0.9f, 0.001f, 0.028f);
        Hiss(b, 0.0f, 0.045f, 0.6f, 0.0005f, 0.010f, 1400.0f, 600.0f, 150.0f, 81);
        Tone(b, 0.0f, 0.03f, 3300.0f, 2900.0f, 0.22f, 0.0005f, 0.008f, TRI);
        Hiss(b, 0.0f, 0.012f, 0.35f, 0.0003f, 0.003f, 9000.0f, 7000.0f, 3000.0f, 82);
        LowpassAll(b, 8000.0f);
        Normalize(b, 0.50f);
        break;
    case Sfx::Whiff:
        // A blade through empty air: the edge and not the arm, so thinner and
        // quicker than a Swing -- a swell and gone, falling as it goes by. It
        // is the knife's throw turned round: that one rises as it leaves.
        b = Blank(0.16f);
        Hiss(b, 0.0f, 0.15f, 1.0f, 0.030f, 0.030f, 6500.0f, 2600.0f, 1900.0f, 91);
        LowpassAll(b, 10000.0f);
        Normalize(b, 0.30f);
        break;
    case Sfx::Breath:
        // A dragon's breath going out of it -- fire, frost, stone, gale or
        // sparks, pitched and seasoned for each where it is played: a throaty
        // whoomph as the jaws open, then a long roaring rush of air forced out
        // hard, brightening to its height and darkening as it is spent, over
        // a growl in the throat and a rumble in the chest. Nothing like a
        // bowstring, which is what it used to be heard as.
        b = Blank(1.15f);
        Hiss(b, 0.00f, 0.14f, 0.9f, 0.004f, 0.050f, 380.0f, 900.0f, 60.0f, 111);
        Hiss(b, 0.03f, 0.45f, 1.0f, 0.060f, 0.400f, 1400.0f, 4200.0f, 300.0f, 112);
        Hiss(b, 0.40f, 0.70f, 0.85f, 0.020f, 0.300f, 4200.0f, 900.0f, 250.0f, 113);
        Growl(b, 0.0f, 0.95f, 62.0f, 84.0f, 52.0f, 0.25f, 0.45f, 0.05f, 0.45f, 1.0f, 114);
        Tone(b, 0.0f, 0.9f, 55.0f, 38.0f, 0.4f, 0.03f, 0.35f);
        LowpassAll(b, 6000.0f);
        Normalize(b, 0.60f);
        break;
    case Sfx::Roar:
        // A dragon rearing back into its heavy blow: a deep rattling throat
        // that climbs and falls away, a second voice a fifth over it for its
        // bulk, a gale of breath through both and a rumble under them.
        b = Blank(1.35f);
        Growl(b, 0.00f, 1.30f, 70.0f, 128.0f, 58.0f, 0.3f, 1.0f, 0.20f, 0.55f, 1.0f, 101);
        Growl(b, 0.02f, 1.25f, 105.0f, 190.0f, 88.0f, 0.3f, 0.45f, 0.22f, 0.50f, 0.8f, 102);
        Hiss(b, 0.00f, 0.42f, 0.55f, 0.12f, 0.40f, 600.0f, 2400.0f, 180.0f, 103);
        Hiss(b, 0.38f, 0.92f, 0.50f, 0.02f, 0.45f, 2400.0f, 700.0f, 180.0f, 104);
        Tone(b, 0.0f, 1.2f, 48.0f, 36.0f, 0.5f, 0.08f, 0.5f);
        LowpassAll(b, 2400.0f);
        Normalize(b, 0.60f);
        break;
    case Sfx::Bite:
        // A dragon's bite: a snarl as it lunges, then the jaws slamming shut --
        // a hard clack of teeth and the thud of the jaw behind it -- about when
        // the blow lands, a third of a second in (Enemy's SWING_WINDUP).
        b = Blank(0.46f);
        Growl(b, 0.00f, 0.30f, 120.0f, 175.0f, 110.0f, 0.4f, 0.5f, 0.03f, 0.16f, 1.0f, 121);
        Hiss(b, 0.00f, 0.28f, 0.25f, 0.04f, 0.12f, 1600.0f, 900.0f, 250.0f, 122);
        Tone(b, 0.29f, 0.09f, 420.0f, 150.0f, 1.1f, 0.0008f, 0.020f);
        Hiss(b, 0.29f, 0.03f, 1.4f, 0.0003f, 0.006f, 7000.0f, 4000.0f, 1800.0f, 123);
        Tone(b, 0.29f, 0.03f, 2600.0f, 2100.0f, 0.3f, 0.0005f, 0.006f, TRI);
        LowpassAll(b, 9000.0f);
        Normalize(b, 0.55f);
        break;
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

// A cricket: a train of very short high pulses, repeated two or three times.
Buf MakeCricket(uint32_t seed) {
    Noise n(seed);
    Buf b = Blank(0.9f);
    const float f = n.Range(4200.0f, 5200.0f);
    const int trains = 2 + static_cast<int>(n.Unit() * 2.0f);
    const int pulses = 3 + static_cast<int>(n.Unit() * 3.0f);
    float t = 0.0f;
    for (int k = 0; k < trains; ++k) {
        for (int p = 0; p < pulses; ++p)
            Tone(b, t + p * 0.022f, 0.014f, f, f * 0.98f, 1.0f, 0.002f, 0.006f);
        t += pulses * 0.022f + n.Range(0.08f, 0.16f);
    }
    Normalize(b, 0.07f);
    return b;
}

// A dream chime: a soft bell a long way off, with an echo.
Buf MakeChime(uint32_t seed) {
    Noise n(seed);
    Buf b = Blank(1.8f);
    static const float kScale[] = {523.3f, 587.3f, 659.3f, 783.99f, 880.0f, 1046.5f};
    const float f = kScale[static_cast<int>(n.Unit() * 6.0f) % 6];
    Bell(b, 0.0f, f, 0.5f, 0.4f);
    if (n.Unit() < 0.5f) Bell(b, 0.18f, f * 1.5f, 0.25f, 0.3f);
    LowpassAll(b, 4200.0f);
    Echo(b, n.Range(0.24f, 0.36f), 0.42f, 3);
    Normalize(b, 0.12f);
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

// --- the main menu's theme --------------------------------------------------------
//
// "Hollowmarch, after dark": the title screen's music, written for the game and
// played on what an eight-bit console had -- two square-wave voices, a stepped
// triangle for the bass and a noise channel for the drums -- over a slow,
// swung hip-hop beat. It goes from dusk into the night and out the other side:
//
//   bars  0-3   dusk      the chords swell in, an arpeggio, no drums
//   bars  4-11  night     the beat, and a hummed tune with its own echo
//   bars 12-19  the hook  the tune again, higher and rounder
//   bars 20-23  daybreak  the chords turn major, the beat thins, and the
//                         last bar leans back into the night
//
// After the dusk it loops from the night, for as long as the menu is open.
// Like every other sound it is built from nothing at start-up.

namespace theme {

constexpr float BPM   = 88.0f;
constexpr float STEP  = 60.0f / BPM / 4.0f;   // a sixteenth, in seconds
constexpr float SWING = 0.16f;                // the off sixteenths come late by this much of one
constexpr int   BARS  = 24, LOOP_BAR = 4;

float Hz(int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }
float At(int bar, int step) {
    return (bar * 16 + step) * STEP + ((step & 1) ? STEP * SWING : 0.0f);
}

// A square wave with its corners rounded off just enough not to alias into
// a hiss (PolyBLEP), and its DC taken out so a thin one is not lopsided.
float Blep(float t, float dt) {
    if (t < dt)        { t /= dt;          return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
float Pulse(float ph, float dt, float duty) {
    float v = ph < duty ? 1.0f : -1.0f;
    v += Blep(ph, dt);
    float p2 = ph - duty;
    if (p2 < 0.0f) p2 += 1.0f;
    v -= Blep(p2, dt);
    return v - (2.0f * duty - 1.0f);
}
// The console's triangle had sixteen steps, which is its buzz.
float Stepped(float ph) {
    const float tri = 4.0f * std::fabs(ph - 0.5f) - 1.0f;
    return std::floor((tri + 1.0f) * 7.5f + 0.5f) / 7.5f - 1.0f;
}

struct Stereo {
    Buf s;   // interleaved left, right
    void Add(size_t frame, float v, float gl, float gr) {
        if (frame * 2 + 1 >= s.size()) return;
        s[frame * 2]     += v * gl;
        s[frame * 2 + 1] += v * gr;
    }
};
void Pan(float pan, float& gl, float& gr) {
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    gl = std::cos(a) * 1.4142f;
    gr = std::sin(a) * 1.4142f;
}

struct Square {
    float duty = 0.25f, amp = 0.15f, pan = 0.0f;
    float attack = 0.01f, decay = 3.0f, release = 0.06f;
    float vibrato = 0.0f;   // semitones, after a moment held
    float scoop = 0.0f;     // semitones below, sliding up into the note: a hum
    bool  steps = true;     // the volume in sixteen steps, as the console had it
};

void PlaySquare(Stereo& out, float at, float len, int midi, float vel, const Square& v) {
    float gl, gr;
    Pan(v.pan, gl, gr);
    const float f0 = Hz(midi);
    const size_t s0 = static_cast<size_t>(at * RATE);
    const size_t n = static_cast<size_t>((len + v.release) * RATE);
    // Thousands of notes are built at start-up, so nothing per sample that can
    // be stepped instead: the decay is multiplied down, and a bend of a third
    // of a semitone is near enough a straight line in frequency.
    const float fall = std::exp(-1.0f / (v.decay * RATE));
    float ph = 0.0f, held = 1.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / RATE;
        float semis = 0.0f;
        if (v.scoop > 0.0f && t < 0.05f) semis -= v.scoop * (1.0f - t / 0.05f);
        if (v.vibrato > 0.0f && t > 0.2f)
            semis += v.vibrato * std::min(1.0f, (t - 0.2f) * 4.0f) * std::sin((t - 0.2f) * TAU * 5.2f);
        const float f = f0 * (1.0f + semis * 0.0577623f);
        const float dt = f / RATE;
        ph += dt;
        ph -= std::floor(ph);
        float e;
        if (t < v.attack) e = t / v.attack;
        else { e = held; held *= fall; }
        if (t > len) e *= std::max(0.0f, 1.0f - (t - len) / v.release);
        if (v.steps) e = std::floor(e * 15.0f + 0.5f) / 15.0f;
        out.Add(s0 + i, Pulse(ph, dt, v.duty) * e * v.amp * vel, gl, gr);
    }
}

void PlayBass(Stereo& out, float at, float len, int midi, float vel) {
    const float f = Hz(midi);
    const size_t s0 = static_cast<size_t>(at * RATE);
    const size_t n = static_cast<size_t>((len + 0.04f) * RATE);
    float ph = 0.25f;   // where the triangle crosses zero: no click at the start
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / RATE;
        ph += f / RATE;
        ph -= std::floor(ph);
        float e = std::min(1.0f, t / 0.004f) * (0.75f + 0.25f * std::exp(-t * 4.0f));
        if (t > len) e *= std::max(0.0f, 1.0f - (t - len) / 0.04f);
        out.Add(s0 + i, Stepped(ph) * e * 0.30f * vel, 1.0f, 1.0f);
    }
}

// The drums, as the console made them: a triangle dropped fast for the kick,
// noise held at a lower rate for the snare's body, plain noise for the hats.
void PlayKick(Stereo& out, float at, float vel) {
    const size_t s0 = static_cast<size_t>(at * RATE);
    const size_t n = static_cast<size_t>(0.16f * RATE);
    float ph = 0.25f;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / RATE;
        const float f = 48.0f + 150.0f * std::exp(-t / 0.025f);
        ph += f / RATE;
        ph -= std::floor(ph);
        const float e = std::exp(-t / 0.07f) * std::min(1.0f, (0.16f - t) * 100.0f);
        out.Add(s0 + i, Stepped(ph) * e * 0.42f * vel, 1.0f, 1.0f);
    }
}

void PlayNoise(Stereo& out, float at, float decay, float hold_hz, float amp, float pan,
               float tone_hz, uint32_t seed) {
    Noise rng(seed);
    float gl, gr;
    Pan(pan, gl, gr);
    const size_t s0 = static_cast<size_t>(at * RATE);
    const size_t n = static_cast<size_t>(decay * 5.0f * RATE);
    const int hold = std::max(1, static_cast<int>(RATE / hold_hz));
    float held = 0.0f, ph = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / RATE;
        if (i % hold == 0) held = rng.Next() > 0.0f ? 1.0f : -1.0f;
        float v = held;
        if (tone_hz > 0.0f) {
            ph += tone_hz * (1.0f - 0.3f * std::min(1.0f, t / 0.06f)) / RATE;
            ph -= std::floor(ph);
            v = v * 0.7f + Stepped(ph) * 0.6f * std::exp(-t / 0.04f);
        }
        const float e = std::floor(std::exp(-t / decay) * 15.0f + 0.5f) / 15.0f;
        out.Add(s0 + i, v * e * amp, gl, gr);
    }
}

struct Chord { int bass; int tones[4]; };

Buf Make() {
    // The night: Am7, Dm9, Fmaj7, E7 -- the last one's G sharp is what pulls
    // it round again. Daybreak: Fmaj7, G, Cmaj7, and E7 to fall back in.
    static const Chord night[4] = {
        {45, {57, 60, 64, 67}}, {38, {57, 60, 62, 65}}, {41, {57, 60, 64, 65}}, {40, {56, 59, 62, 64}},
    };
    static const Chord day[4] = {
        {41, {57, 60, 64, 65}}, {43, {55, 59, 62, 67}}, {48, {55, 59, 60, 64}}, {40, {56, 59, 62, 64}},
    };
    auto chord = [&](int bar) -> const Chord& { return bar >= 20 ? day[bar - 20] : night[bar % 4]; };

    // The tune: {bar, step, sixteenths long, note}.
    struct N { int bar, step, len, note; };
    static const N verse[] = {
        {0, 4, 2, 76}, {0, 6, 2, 79}, {0, 8, 6, 81}, {0, 14, 2, 79},
        {1, 0, 4, 76}, {1, 4, 2, 74}, {1, 6, 4, 72}, {1, 10, 6, 69},
        {2, 4, 2, 72}, {2, 6, 2, 76}, {2, 8, 4, 77}, {2, 12, 2, 76}, {2, 14, 2, 72},
        {3, 0, 6, 71}, {3, 6, 2, 74}, {3, 8, 8, 68},
        {4, 0, 2, 69}, {4, 2, 2, 72}, {4, 4, 4, 76}, {4, 8, 2, 81}, {4, 10, 2, 83}, {4, 12, 4, 84},
        {5, 0, 2, 83}, {5, 2, 4, 81}, {5, 6, 2, 77}, {5, 8, 4, 76}, {5, 12, 4, 74},
        {6, 0, 6, 72}, {6, 6, 2, 69}, {6, 8, 2, 72}, {6, 10, 6, 76},
        {7, 0, 4, 74}, {7, 4, 4, 71}, {7, 8, 4, 68}, {7, 12, 4, 64},
    };
    static const N hook[] = {
        {0, 0, 12, 81}, {0, 12, 4, 79},
        {1, 0, 8, 77}, {1, 8, 4, 76}, {1, 12, 4, 74},
        {2, 0, 12, 76}, {2, 12, 4, 72},
        {3, 0, 8, 71}, {3, 8, 4, 74}, {3, 12, 4, 80},
        {4, 0, 6, 81}, {4, 6, 2, 84}, {4, 8, 4, 83}, {4, 12, 4, 81},
        {5, 0, 6, 77}, {5, 6, 2, 81}, {5, 8, 4, 79}, {5, 12, 4, 77},
        {6, 0, 8, 76}, {6, 8, 2, 79}, {6, 10, 2, 76}, {6, 12, 4, 72},
        {7, 0, 8, 71},
    };
    static const N dawn[] = {
        {0, 0, 8, 76}, {0, 8, 4, 77}, {0, 12, 4, 79},
        {1, 0, 12, 74}, {1, 12, 4, 71},
        {2, 0, 6, 76}, {2, 6, 2, 79}, {2, 8, 8, 84},
        {3, 0, 4, 83}, {3, 4, 4, 80}, {3, 8, 8, 76},
    };

    const float length = BARS * 16 * STEP;
    const float tail = 2.0f;
    Stereo out;
    out.s.assign(static_cast<size_t>((length + tail) * RATE) * 2, 0.0f);

    // --- the chords, swelling, under the dusk and the daybreak -----------------
    Square pad;
    pad.duty = 0.5f; pad.amp = 0.035f; pad.attack = 0.6f; pad.decay = 6.0f; pad.release = 0.5f;
    for (int bar = 0; bar < BARS; ++bar) {
        if (bar >= LOOP_BAR && bar < 20) continue;
        const Chord& c = chord(bar);
        pad.duty = bar >= 20 ? 0.25f : 0.5f;
        for (int k = 0; k < 4; ++k) {
            pad.pan = -0.4f + 0.27f * k;
            PlaySquare(out, At(bar, 0), 16 * STEP - 0.1f, c.tones[k], bar < LOOP_BAR ? 0.6f + 0.13f * bar : 0.9f, pad);
        }
    }

    // --- the arpeggio: chord tones an octave up, a thin square plucked ---------
    static const int arp[16] = {0, 2, 1, 3, 2, 1, 3, 2, 0, 2, 1, 3, 2, 3, 1, 2};
    Square pluck;
    pluck.duty = 0.125f; pluck.amp = 0.075f; pluck.attack = 0.003f; pluck.decay = 0.09f;
    pluck.release = 0.03f; pluck.pan = 0.35f;
    for (int bar = 0; bar < BARS; ++bar) {
        const Chord& c = chord(bar);
        const float vel = bar < LOOP_BAR ? 0.35f + 0.18f * bar : (bar >= 12 && bar < 20 ? 0.95f : 0.8f);
        for (int s = 0; s < 16; ++s) {
            const float accent = (s % 4 == 0) ? 1.0f : 0.78f;
            PlaySquare(out, At(bar, s), STEP * 0.9f, c.tones[arp[s]] + 12, vel * accent, pluck);
        }
    }

    // --- the bass: a bounce on the root, the fifth, the octave -----------------
    for (int bar = 0; bar < BARS; ++bar) {
        const int root = chord(bar).bass;
        if (bar < LOOP_BAR) { PlayBass(out, At(bar, 0), 16 * STEP - 0.05f, root, 0.55f + 0.1f * bar); continue; }
        if (bar >= 20) {
            PlayBass(out, At(bar, 0), 6 * STEP, root, 0.9f);
            PlayBass(out, At(bar, 8), 6 * STEP, root + 7, 0.75f);
            continue;
        }
        PlayBass(out, At(bar, 0), 4 * STEP, root, 1.0f);
        PlayBass(out, At(bar, 6), 2 * STEP, root, 0.8f);
        PlayBass(out, At(bar, 8), 3 * STEP, root + 7, 0.85f);
        PlayBass(out, At(bar, 11), 1 * STEP, root, 0.7f);
        PlayBass(out, At(bar, 14), 2 * STEP, root + 12, 0.75f);
    }

    // --- the drums ---------------------------------------------------------------
    uint32_t seed = 7001;
    for (int bar = LOOP_BAR - 1; bar < BARS; ++bar) {
        if (bar == LOOP_BAR - 1) {
            // The dusk's last beat: a snare roll into the night.
            for (int s = 12; s < 16; ++s)
                PlayNoise(out, At(bar, s), 0.035f, 9000.0f, 0.10f + 0.04f * (s - 12), 0.0f, 190.0f, seed++);
            continue;
        }
        const bool dawn_bar = bar >= 20;
        const bool hook_bar = bar >= 12 && bar < 20;
        if (dawn_bar) {
            PlayKick(out, At(bar, 0), 0.85f);
            PlayNoise(out, At(bar, 8), 0.05f, 9000.0f, 0.16f, 0.0f, 180.0f, seed++);
            for (int s = 0; s < 16; s += 4) PlayNoise(out, At(bar, s), 0.012f, RATE, 0.035f, -0.3f, 0.0f, seed++);
            continue;
        }
        PlayKick(out, At(bar, 0), 1.0f);
        PlayKick(out, At(bar, 7), 0.75f);
        PlayKick(out, At(bar, 10), 0.9f);
        PlayNoise(out, At(bar, 4), 0.05f, 9000.0f, 0.20f, 0.0f, 180.0f, seed++);
        PlayNoise(out, At(bar, 12), 0.05f, 9000.0f, 0.20f, 0.0f, 180.0f, seed++);
        for (int s = 0; s < 16; s += hook_bar ? 1 : 2) {
            const bool open = (s == 14) && (bar % 4 == 3);
            const float amp = (s % 2 == 0 ? 0.05f : 0.026f) * (s % 4 == 2 ? 1.15f : 1.0f);
            PlayNoise(out, At(bar, s), open ? 0.06f : 0.011f, RATE, open ? 0.045f : amp, -0.3f, 0.0f, seed++);
        }
        if (bar % 8 == 7) PlayNoise(out, At(bar, 15), 0.03f, 9000.0f, 0.10f, 0.0f, 200.0f, seed++);
    }

    // --- the tune, hummed, with its echo across the room -----------------------
    Square hum;
    hum.amp = 0.13f; hum.attack = 0.03f; hum.decay = 2.5f; hum.release = 0.09f;
    hum.vibrato = 0.22f; hum.scoop = 0.35f; hum.pan = -0.15f;
    Square echo = hum;
    echo.scoop = 0.0f; echo.pan = 0.55f;
    auto sing = [&](const N* tune, size_t count, int first_bar, float duty, float vel) {
        hum.duty = echo.duty = duty;
        for (size_t i = 0; i < count; ++i) {
            const N& n = tune[i];
            const int bar = first_bar + n.bar;
            const float at = At(bar, n.step), len = n.len * STEP - 0.02f;
            PlaySquare(out, at, len, n.note, vel, hum);
            PlaySquare(out, at + 3 * STEP, len, n.note, vel * 0.30f, echo);
        }
    };
    sing(verse, std::size(verse), 4, 0.25f, 1.0f);
    sing(hook, std::size(hook), 12, 0.5f, 1.0f);
    sing(dawn, std::size(dawn), 20, 0.25f, 0.85f);

    // --- the loop: what rings past the end carries on over the night's start ---
    const size_t frames = static_cast<size_t>(length * RATE);
    const size_t loop = static_cast<size_t>(At(LOOP_BAR, 0) * RATE);
    for (size_t i = frames * 2; i < out.s.size(); ++i) out.s[loop * 2 + (i - frames * 2)] += out.s[i];
    out.s.resize(frames * 2);

    // The console's own output: no DC, and nothing much above 12 kHz.
    float hp[2] = {0, 0}, lp[2] = {0, 0}, peak = 0.0f;
    const float c_hp = Coef(30.0f), c_lp = Coef(12000.0f);
    for (size_t i = 0; i < out.s.size(); ++i) {
        float& v = out.s[i];
        const int ch = static_cast<int>(i & 1);
        hp[ch] += (v - hp[ch]) * c_hp;
        lp[ch] += ((v - hp[ch]) - lp[ch]) * c_lp;
        v = lp[ch];
        peak = std::max(peak, std::fabs(v));
    }
    if (peak > 0.0f) for (float& v : out.s) v *= 0.42f / peak;
    return out.s;
}

size_t LoopFrame() { return static_cast<size_t>(At(LOOP_BAR, 0) * RATE); }

}  // namespace theme

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
    // The dream: a slow shimmering chord, and chimes.
    float pad = 0.0f;
    float chime_lo = 0.0f, chime_hi = 0.0f;
    // The main menu's theme, at this level.
    float music = 0.0f;
};

struct State {
    SDL_AudioStream* stream = nullptr;
    bool enabled = false;
    bool built = false;

    vector<Buf> bank;
    vector<Buf> birds, drips, crackles, crickets, chimes;
    // The menu's theme takes a few hundred milliseconds to make, so it is made
    // on a thread of its own while the game starts, and the mixer leaves it
    // alone until it is ready. Written once, by that thread, before `ready`.
    Buf theme;                     // stereo, interleaved
    size_t theme_loop = 0;         // the frame it goes back to
    std::thread theme_maker;
    std::atomic<bool> theme_ready{false};
    void ThemeMade() { if (theme_maker.joinable()) theme_maker.join(); }
    ~State() { ThemeMade(); }

    static constexpr int VOICES = 32;
    Voice voices[VOICES];

    float listener_x = 0.0f, listener_y = 0.0f;
    float master = 0.8f, sfx = 1.0f, amb = 0.8f;

    Profile target;
    float wind_g = 0.0f, drone_g = 0.0f, fire_g = 0.0f, cut_g = 1.0f, pad_g = 0.0f, music_g = 0.0f;
    size_t theme_pos = 0;
    bool theme_on = false;         // playing, or still fading out
    float bird_timer = 2.0f, drip_timer = 3.0f, crackle_timer = 0.5f;
    float cricket_timer = 1.0f, chime_timer = 1.5f;
    float night = 0.0f;
    float pad_a = 0.0f, pad_b = 0.0f, pad_c = 0.0f;

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
    for (uint32_t i = 0; i < 6; ++i)  g.crickets.push_back(MakeCricket(4000 + i * 29));
    for (uint32_t i = 0; i < 8; ++i)  g.chimes.push_back(MakeChime(5000 + i * 41));
    g.theme_maker = std::thread([] {
        g.theme = theme::Make();
        g.theme_loop = theme::LoopFrame();
        g.theme_ready.store(true, std::memory_order_release);
    });
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
    g.ThemeMade();
    g.enabled = true;
    return true;
}

void Shutdown() {
    g.ThemeMade();
    if (g.stream) {
        SDL_DestroyAudioStream(g.stream);
        g.stream = nullptr;
    }
    g.enabled = false;
    for (Voice& v : g.voices) v.buf = nullptr;
}

bool Enabled() { return g.enabled; }

namespace { Tap g_tap; int g_muted = 0; }
void SetTap(Tap tap) { g_tap = std::move(tap); }
void SetMuted(int level) { g_muted = level; }

void Play(Sfx s, float volume, float pitch) {
    if (s >= Sfx::Count) return;
    if (g_tap) g_tap(s, false, 0.0f, 0.0f, volume, pitch);
    if (!g.enabled || g_muted >= 1) return;
    Lock lock;
    // Menu sounds are exact; everything else varies a touch so a run of the
    // same hit does not sound like a machine.
    const bool ui = (s >= Sfx::UiMove && s <= Sfx::UiError) ||
                    s == Sfx::LevelUp || s == Sfx::QuestStart || s == Sfx::QuestComplete;
    if (!ui) pitch *= g.play.Range(0.95f, 1.05f);
    Start(g.bank[static_cast<size_t>(s)], volume, pitch, 0.0f, false);
}

void PlayAt(Sfx s, float x, float y, float volume, float pitch) {
    if (s >= Sfx::Count) return;
    if (g_tap) g_tap(s, true, x, y, volume, pitch);
    if (!g.enabled || g_muted >= 2) return;
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
    } else if (kind == "dream") {
        p.wind = 0.035f; p.wind_cut = 0.6f;
        p.pad = 0.05f;
        p.chime_lo = 2.0f; p.chime_hi = 6.0f;
    } else if (kind == "snow") {
        // A hard wind over bare rock, and nothing singing in it.
        p.wind = 0.26f; p.wind_cut = 1.6f;
    } else if (kind == "ash") {
        p.wind = 0.12f; p.wind_cut = 0.55f;
        p.drone = 0.03f;
        p.fire = 0.03f; p.crackle_lo = 0.15f; p.crackle_hi = 1.2f;
    } else if (kind == "deep") {
        // Under a sea: the wind muffled to a pressure, a low drone, drops.
        p.wind = 0.06f; p.wind_cut = 0.35f;
        p.drone = 0.05f;
        p.drip_lo = 1.2f; p.drip_hi = 4.0f;
    } else if (kind == "gale") {
        // Nothing but the wind, and a great deal of it.
        p.wind = 0.30f; p.wind_cut = 2.0f;
    } else if (kind == "storm") {
        // Wind and a rumble under it, and the crackle of what is charged.
        p.wind = 0.22f; p.wind_cut = 1.0f;
        p.drone = 0.035f;
        p.fire = 0.02f; p.crackle_lo = 0.4f; p.crackle_hi = 2.0f;
    } else if (kind == "conflux") {
        // All of it at once, a long way off: a hum, a chime, a breath of wind.
        p.wind = 0.05f; p.wind_cut = 0.7f;
        p.drone = 0.03f;
        p.pad = 0.06f;
        p.chime_lo = 1.5f; p.chime_hi = 5.0f;
    } else if (kind == "menu") {
        // The theme, with a breath of wind and the odd bird behind it.
        p.music = 1.0f;
        p.wind = 0.05f; p.wind_cut = 0.8f; p.bird_lo = 9.0f; p.bird_hi = 20.0f;
    } else if (!kind.empty()) {
        p.wind = 0.17f; p.wind_cut = 1.2f; p.bird_lo = 4.0f; p.bird_hi = 11.0f;
    }
    Lock lock;
    g.target = p;
}

void SetNight(float amount) {
    Lock lock;
    g.night = std::clamp(amount, 0.0f, 1.0f);
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

const vector<float>& MenuTheme() {
    Build();
    g.ThemeMade();
    return g.theme;
}

size_t MenuThemeLoop() {
    Build();
    g.ThemeMade();
    return g.theme_loop;
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
        // Birds by day and crickets by night, wherever there are birds at all.
        if (g.night < 0.5f)
            schedule(g.bird_timer, t.bird_lo, t.bird_hi, g.birds, 0.25f, 1.0f, 0.9f);
        else
            schedule(g.cricket_timer, t.bird_hi > 0.0f ? 0.4f : 0.0f,
                     t.bird_hi > 0.0f ? 1.8f : 0.0f, g.crickets, 0.3f, 1.0f, 0.9f);
        schedule(g.chime_timer, t.chime_lo, t.chime_hi, g.chimes, 0.4f, 1.0f, 0.8f);
        schedule(g.drip_timer, t.drip_lo, t.drip_hi, g.drips, 0.3f, 1.0f, 0.8f);
        schedule(g.crackle_timer, t.crackle_lo, t.crackle_hi, g.crackles, 0.5f, 1.0f, 0.3f);
    }

    const float sfx_bus = g.master * g.sfx;
    const float amb_bus = g.master * g.amb;
    const bool theme_ready = g.theme_ready.load(std::memory_order_acquire);
    constexpr float SMOOTH = 1.0f / (0.8f * RATE);   // layers fade over most of a second

    for (int i = 0; i < frames; ++i) {
        g.wind_g  += (t.wind - g.wind_g) * SMOOTH;
        g.drone_g += (t.drone - g.drone_g) * SMOOTH;
        g.fire_g  += (t.fire - g.fire_g) * SMOOTH;
        g.cut_g   += (t.wind_cut - g.cut_g) * SMOOTH;
        g.pad_g   += (t.pad - g.pad_g) * SMOOTH;
        g.music_g += (t.music - g.music_g) * SMOOTH;

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

        // The dream's chord: three soft sines a fifth and an octave apart,
        // each drifting slightly out of tune with the next, so it shimmers.
        if (g.pad_g > 0.0005f) {
            if (g.wind_g <= 0.0005f) g.lfo += 1.0f / RATE;
            const float drift = std::sin(g.lfo * TAU * 0.05f);
            g.pad_a += (220.0f + drift * 0.8f) / RATE;  g.pad_a -= std::floor(g.pad_a);
            g.pad_b += (330.0f - drift * 1.1f) / RATE;  g.pad_b -= std::floor(g.pad_b);
            g.pad_c += (440.6f + drift * 1.3f) / RATE;  g.pad_c -= std::floor(g.pad_c);
            const float swell = 0.6f + 0.4f * std::sin(g.lfo * TAU * 0.09f);
            const float a = std::sin(g.pad_a * TAU), b = std::sin(g.pad_b * TAU), c = std::sin(g.pad_c * TAU);
            l += (a * 0.5f + b * 0.35f + c * 0.15f) * swell * g.pad_g;
            r += (a * 0.4f + b * 0.25f + c * 0.35f) * swell * g.pad_g;
        }

        // The menu's theme: from the dusk each time the menu opens, then round
        // and round the night; it fades out with everything else.
        if (theme_ready && !g.theme.empty() && (t.music > 0.0f || g.music_g > 0.0005f)) {
            if (!g.theme_on) { g.theme_on = true; g.theme_pos = 0; }
            l += g.theme[g.theme_pos * 2] * g.music_g;
            r += g.theme[g.theme_pos * 2 + 1] * g.music_g;
            if (++g.theme_pos * 2 >= g.theme.size()) g.theme_pos = g.theme_loop;
        } else {
            g.theme_on = false;
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
