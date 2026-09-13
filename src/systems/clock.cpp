#include "clock.h"

namespace {
float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

void WorldClock::Advance(float seconds) {
    if (seconds <= 0.0f) return;
    hours += seconds / SECONDS_PER_HOUR;
    while (hours >= 24.0f) { hours -= 24.0f; ++day; }
}

void WorldClock::Set(int d, float h) {
    day = std::max(1, d);
    hours = fmodf(h, 24.0f);
    if (hours < 0.0f) hours += 24.0f;
}

float WorldClock::Darkness() const {
    const float h = hours;
    if (h >= DAY_START && h < DUSK) return 0.0f;
    if (h >= DUSK && h < NIGHT_START + 0.5f) return Ease((h - DUSK) / (NIGHT_START + 0.5f - DUSK));
    if (h >= NIGHT_END - 1.0f && h < DAY_START)
        return 1.0f - Ease((h - (NIGHT_END - 1.0f)) / (DAY_START - (NIGHT_END - 1.0f)));
    return 1.0f;
}

float WorldClock::Warmth() const {
    // A bump centred on sunset and another on sunrise, each an hour and a half
    // either side.
    const auto bump = [&](float centre) {
        float d = fabsf(hours - centre);
        d = std::min(d, 24.0f - d);
        return 1.0f - Ease(d / 1.5f);
    };
    return std::max(bump(19.0f), bump(5.8f));
}

float WorldClock::SecondsToDawn() const {
    if (!IsNight() && !CanSleep()) return 0.0f;
    float until = NIGHT_END - hours;
    if (until < 0.0f) until += 24.0f;
    return until * SECONDS_PER_HOUR;
}

const char* WorldClock::Phase() const {
    if (hours >= NIGHT_END && hours < DAY_START) return "Dawn";
    if (hours >= DAY_START && hours < DUSK)      return "Day";
    if (hours >= DUSK && hours < NIGHT_START)    return "Dusk";
    return "Night";
}

string WorldClock::TimeText() const {
    const int total = static_cast<int>(hours * 60.0f);
    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%02d:%02d", (total / 60) % 24, total % 60);
    return buf;
}

void WorldClock::SkipToDawn() {
    if (hours >= NIGHT_END) ++day;
    hours = NIGHT_END;
}

json WorldClock::ToJson() const {
    return json{{"day", day}, {"hours", hours}};
}

void WorldClock::FromJson(const json& j) {
    Set(j.value("day", 1), j.value("hours", 9.0f));
}
