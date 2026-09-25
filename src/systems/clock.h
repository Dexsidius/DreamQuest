#pragma once
#include "../headers.h"

// ---------------------------------------------------------------------------
//  The time of day
//
//  An hour of the day in the Hollowmarch lasts half a real minute. Night runs
//  from eight in the evening to five in the morning, and its hours are slower:
//  the whole of it is twenty real minutes -- which is how long a dream lasts
//  for someone who goes to bed as it falls. The day between is seven and a
//  half, so a day and a night together are twenty-seven and a half minutes.
//
//  The clock only runs while the game is being played, and it is saved with
//  the rest of the world, so night is still night after a reload.
//
//  Everything here is plain arithmetic on the hour, so the self-test can set
//  the clock anywhere and check what it says. What moves by the clock in real
//  time -- a round walked, a bug's flight -- asks Seconds(), which counts the
//  night's hours at their own length, rather than multiplying the hour out.
// ---------------------------------------------------------------------------

class WorldClock {
public:
    static constexpr float DUSK        = 18.0f;   // the light starts to go
    static constexpr float NIGHT_START = 20.0f;   // fully dark by 20:30
    static constexpr float NIGHT_END   = 5.0f;    // dawn: dreams end here
    static constexpr float DAY_START   = 7.0f;    // full light again
    // Real seconds an hour takes: by day, and from NIGHT_START to NIGHT_END.
    static constexpr float SECONDS_PER_HOUR       = 30.0f;
    static constexpr float NIGHT_SECONDS          = 20.0f * 60.0f;
    static constexpr float NIGHT_HOURS            = 24.0f - NIGHT_START + NIGHT_END;
    static constexpr float NIGHT_SECONDS_PER_HOUR = NIGHT_SECONDS / NIGHT_HOURS;
    // A day and a night, in real seconds.
    static constexpr float DAY_SECONDS = (24.0f - NIGHT_HOURS) * SECONDS_PER_HOUR + NIGHT_SECONDS;
    // A bed will take you from an hour before night falls until an hour before
    // it ends. Any later and the dream would be over before it began.
    static constexpr float SLEEP_FROM  = 19.0f;
    static constexpr float SLEEP_UNTIL = 4.0f;

    void  Advance(float seconds);
    void  Set(int day, float hours);

    int   Day() const   { return day; }
    // The day as the quest boards count it: it turns over at dawn rather than
    // at midnight, so a night's sleep is what brings new notices.
    int   QuestDay() const { return hours < NIGHT_END ? day - 1 : day; }
    float Hours() const { return static_cast<float>(hours); }

    // Real seconds of play the clock has run since midnight of day zero,
    // counting each hour at its own length; and back, to hours on the same
    // count (day * 24 + the hour). They are what a round walked or a bug
    // flying is timed by, so neither slows to a crawl at night.
    double Seconds() const { return SecondsAt(day * 24.0 + hours); }
    static double SecondsAt(double game_hours);
    static double HoursAt(double seconds);

    // 20:00 to 05:00.
    bool  IsNight() const { return hours >= NIGHT_START || hours < NIGHT_END; }
    bool  CanSleep() const { return hours >= SLEEP_FROM || hours < SLEEP_UNTIL; }
    // True from dawn until a bed would take you again: a dream running into
    // this has run out of night.
    bool  DreamOver() const { return hours >= NIGHT_END && hours < SLEEP_FROM; }

    // 0 in daylight, 1 in the dead of night, eased through dusk and dawn.
    float Darkness() const;
    // How much of a sunrise or sunset colour the light has, 0..1.
    float Warmth() const;
    // Real seconds until dawn, or 0 in the day; and the hours on the clock.
    float SecondsToDawn() const;
    float HoursToDawn() const;

    // "Dawn", "Day", "Dusk" or "Night".
    const char* Phase() const;
    // "21:40".
    string TimeText() const;

    // Moves on to the next dawn: into tomorrow if it is evening, today if it is
    // after midnight. For a dream cut short.
    void  SkipToDawn();

    json  ToJson() const;
    void  FromJson(const json& j);

private:
    int    day = 1;
    // A double: a night hour goes by in steps a float at 20:00 could not add
    // up evenly, and the night would come out a few seconds long or short.
    double hours = 9.0;
};
