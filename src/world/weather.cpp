#include "weather.h"

namespace Weather {
namespace {

float Hash01(long long k, int salt) {
    uint64_t h = static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(salt) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 31; h *= 0x7FB5D329728EA185ull; h ^= h >> 27; h *= 0x81DADEF4BC2DD44Dull; h ^= h >> 33;
    return static_cast<float>(h >> 40) / 16777216.0f;
}

constexpr double RISE = 30.0, FALL = 45.0;   // seconds the rain takes to come on, and to ease off
float g_forced = -1.0f;
constexpr double WETTING = 60.0, DRYING = 240.0;

}   // namespace

bool ShowerIn(long long slot, double& start, double& end, float& heavy) {
    // Some stretches have a shower in them and most do not; the first few
    // minutes of a new game never do, so nobody's first look at the world is
    // through rain.
    if (slot < 1 || Hash01(slot, 1) >= 0.36f) return false;
    const double length = 150.0 + 170.0 * Hash01(slot, 2);       // two and a half to five minutes
    start = static_cast<double>(slot) * SLOT + (SLOT - length - 20.0) * Hash01(slot, 3);
    end = start + length;
    heavy = 0.5f + 0.5f * Hash01(slot, 4);
    return true;
}

void Force(float rain) { g_forced = rain; }

float Rain(double seconds) {
    if (g_forced >= 0.0f) return std::min(g_forced, 1.0f);
    if (seconds < 0.0) return 0.0f;
    const long long slot = static_cast<long long>(seconds / SLOT);
    double start = 0.0, end = 0.0;
    float heavy = 0.0f;
    if (!ShowerIn(slot, start, end, heavy) || seconds < start || seconds > end) return 0.0f;
    const double in = std::clamp((seconds - start) / RISE, 0.0, 1.0);
    const double out = std::clamp((end - seconds) / FALL, 0.0, 1.0);
    const double k = std::min(in, out);
    return heavy * static_cast<float>(k * k * (3.0 - 2.0 * k));
}

float Wet(double seconds) {
    if (g_forced >= 0.0f) return g_forced > 0.0f ? 1.0f : 0.0f;
    if (seconds < 0.0) return 0.0f;
    const long long slot = static_cast<long long>(seconds / SLOT);
    float wet = 0.0f;
    // This stretch's shower, and the last one's, which may still be drying.
    for (long long s = slot - 1; s <= slot; ++s) {
        double start = 0.0, end = 0.0;
        float heavy = 0.0f;
        if (!ShowerIn(s, start, end, heavy) || seconds < start) continue;
        const double soaked = std::clamp((std::min(seconds, end) - start) / WETTING, 0.0, 1.0);
        const double drying = seconds > end ? std::clamp(1.0 - (seconds - end) / DRYING, 0.0, 1.0) : 1.0;
        wet = std::max(wet, static_cast<float>(soaked * drying));
    }
    return wet;
}

float MorningFog(float hour) {
    const auto ease = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };
    if (hour < 3.5f || hour > 9.5f) return 0.0f;
    if (hour < 5.5f) return ease((hour - 3.5f) / 2.0f);
    if (hour < 7.0f) return 1.0f;
    return 1.0f - ease((hour - 7.0f) / 2.5f);
}

bool RainsOn(const string& ambient) {
    return ambient == "overworld" || ambient == "town" || ambient == "forest" || ambient == "grove" || ambient.empty();
}

}   // namespace Weather
