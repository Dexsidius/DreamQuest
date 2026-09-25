#include "clock.h"

namespace {
float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

// Real seconds from midnight to a time of day: the small hours at the night's
// pace, dawn to nightfall at the day's, and the evening at the night's again.
static double SecondsIntoDay(double h) {
    const double night = WorldClock::NIGHT_SECONDS_PER_HOUR, day = WorldClock::SECONDS_PER_HOUR;
    if (h < WorldClock::NIGHT_END) return h * night;
    const double to_dawn = WorldClock::NIGHT_END * night;
    if (h < WorldClock::NIGHT_START) return to_dawn + (h - WorldClock::NIGHT_END) * day;
    return to_dawn + (WorldClock::NIGHT_START - WorldClock::NIGHT_END) * day + (h - WorldClock::NIGHT_START) * night;
}

double WorldClock::SecondsAt(double game_hours) {
    const double d = std::floor(game_hours / 24.0);
    return d * DAY_SECONDS + SecondsIntoDay(game_hours - d * 24.0);
}

double WorldClock::HoursAt(double seconds) {
    const double night = NIGHT_SECONDS_PER_HOUR, day_rate = SECONDS_PER_HOUR;
    const double d = std::floor(seconds / DAY_SECONDS);
    const double s = seconds - d * DAY_SECONDS;
    const double to_dawn = NIGHT_END * night;
    const double to_dusk = to_dawn + (NIGHT_START - NIGHT_END) * day_rate;
    double h = s < to_dawn ? s / night
             : s < to_dusk ? NIGHT_END + (s - to_dawn) / day_rate
                           : NIGHT_START + (s - to_dusk) / night;
    return d * 24.0 + std::min(h, 24.0);
}

void WorldClock::Advance(float seconds) {
    if (seconds <= 0.0f) return;
    // On the count of seconds, so a step across nightfall or dawn is taken at
    // each side's pace.
    const double t = HoursAt(Seconds() + seconds);
    const double d = std::floor(t / 24.0);
    day = static_cast<int>(d);
    hours = t - d * 24.0;
    if (hours >= 24.0) { hours -= 24.0; ++day; }
}

void WorldClock::Set(int d, float h) {
    day = std::max(1, d);
    hours = fmodf(h, 24.0f);
    if (hours < 0.0f) hours += 24.0f;
}

float WorldClock::Darkness() const {
    const float h = static_cast<float>(hours);
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
        float d = fabsf(static_cast<float>(hours) - centre);
        d = std::min(d, 24.0f - d);
        return 1.0f - Ease(d / 1.5f);
    };
    return std::max(bump(19.0f), bump(5.8f));
}

float WorldClock::HoursToDawn() const {
    if (!IsNight() && !CanSleep()) return 0.0f;
    double until = NIGHT_END - hours;
    if (until < 0.0) until += 24.0;
    return static_cast<float>(until);
}

float WorldClock::SecondsToDawn() const {
    const double now = day * 24.0 + hours;
    return static_cast<float>(SecondsAt(now + HoursToDawn()) - SecondsAt(now));
}

const char* WorldClock::Phase() const {
    if (hours >= NIGHT_END && hours < DAY_START) return "Dawn";
    if (hours >= DAY_START && hours < DUSK)      return "Day";
    if (hours >= DUSK && hours < NIGHT_START)    return "Dusk";
    return "Night";
}

string WorldClock::TimeText() const {
    // A hair over, so a time arrived at by the second reads as the minute it
    // is: a night's hour is not a whole number of seconds, and half past can
    // come out a millionth of a minute short of it.
    const int total = static_cast<int>(hours * 60.0 + 1e-3);
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
