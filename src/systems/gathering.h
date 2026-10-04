#pragma once
#include "../headers.h"
#include "items.h"
#include "skills.h"

// ---------------------------------------------------------------------------
//  Gathering: woodcutting, mining and fishing.
//
//  Each needs its tool in the bag (or, for an axe or pickaxe, in hand): an axe
//  for a tree, a pickaxe for a seam, a fishing rod for a fishing spot. With
//  more than one, the fastest the player may use is taken. An axe or pickaxe
//  asks for its tier's level in Woodcutting or Mining, and each tier works
//  faster than the one below; a rod asks for nothing, and the Fishing level
//  alone decides how quickly things bite.
//
//  Fishing also has milestones. From level 20 a cast can bring up two fish,
//  and from 80 three, more often as the level climbs.
//
//  Everything here is plain arithmetic and lookups, so the self-test can ask
//  it questions directly.
// ---------------------------------------------------------------------------

namespace Gathering {

// "axe", "pickaxe" or "rod" for "Woodcutting", "Mining" or "Fishing".
const char* ToolFor(const string& skill);
// The hero's animation for the work: "chop", "mine" or "fish".
const char* ClipFor(const string& skill);
// "an axe", "a pickaxe", "a fishing rod".
const char* ToolNoun(const string& tool);

// How much faster than a level 1 worker with a basic tool this is.
float Speed(int level, float tool_speed);
// Seconds a node with this base time takes, never under a floor.
float WorkTime(float base_seconds, int level, float tool_speed);

// The fastest tool of a kind the player carries and may use. When there is
// none, `unusable` (if given) is set to the best one they carry but lack the
// level for, so the refusal can say which.
const ItemDef* BestTool(const Inventory& bag, const Equipment& worn, const ItemDatabase& db,
                        const Skills& skills, const string& tool, const ItemDef** unusable = nullptr);

// --- fishing ------------------------------------------------------------------------
struct Milestone {
    int level;
    float two;      // chance a catch is two fish
    float three;    // chance it is three
};
const vector<Milestone>& FishingMilestones();
// The chances in force at a level: the highest milestone reached.
Milestone ExtraCatch(int level);
// How many fish a catch is, for a uniform roll in [0, 1).
int CatchCount(int level, float roll);

// Which fish a spot gives up: the best one the level allows a fair share of
// the time, otherwise something lesser. Empty if the level allows none.
string PickFish(const vector<string>& fish, int level, const ItemDatabase& db, std::mt19937& rng);

// --- the second dip, and the fight ------------------------------------------------------
//
// A cast floats a bobber on the water. After a while it dips once -- a nibble,
// nothing to strike at -- and a moment later it goes under: the bite. Strike
// then (Interact) and the fish is hooked. Strike at the nibble, or before it,
// and it takes fright; let the bite go by and it is gone. The angler in
// Havenbrook teaches exactly this: "Not yet. Wait for the second dip."
//
// Hooked, the fish fights. There is a gauge with a green band the fish drags
// back and forth along it, and on it the reel line, which the player holds:
// the button held winds the line up the gauge, let go it drops back. Kept in
// the green, the fish comes in a little at a time; out of the green too long,
// the line snaps and the fish is gone. Either way the cast is over, and the
// next one begins at the water again.
//
// The better the fish -- its Fishing level -- the narrower the band, the
// quicker and more sudden the fish, the longer it takes to bring in and the
// sooner the line goes. A fisher well past the fish's level has the band a
// little wider for it, and that is all their level does in the fight.

// One fish's fight, worked out from its level.
struct Fight {
    float band  = 0.24f;   // half the green band's width, as a share of the gauge
    float speed = 0.14f;   // how fast the band drifts, in gauges a second
    float darts = 0.25f;   // how often a second it bolts for somewhere new
    float land  = 2.0f;    // seconds in the green that bring it in from nothing
    float slip  = 0.3f;    // how much of that it wins back a second out of the green
    float snap  = 2.6f;    // seconds out of the green that the line holds for
    float bite  = 1.0f;    // seconds the bobber stays under, to strike in
};
Fight FightFor(int fish_level, int fishing_level);

class Angler {
public:
    enum class Phase : uint8_t { Idle, Waiting, Nibble, Lull, Bite, Reeling };
    enum class Outcome : uint8_t { None, Hooked, TooSoon, ReeledIn, Missed, Landed, Snapped };

    // How the reel line moves: wound up while the button is held, dropping
    // back while it is not, never faster than this.
    static constexpr float REEL_ACCEL = 2.0f, FALL_ACCEL = 1.6f, LINE_TOP_SPEED = 1.0f;
    // The first dip: how long it lasts. And the wait between it and the bite.
    static constexpr float NIBBLE_TIME = 0.45f, LULL_MIN = 0.7f, LULL_MAX = 1.4f;
    // Hooked, the fight starts this far in: a strike is worth something.
    static constexpr float HEAD_START = 0.2f;

    // A cast: the bobber afloat, the first dip `wait` seconds off, and the
    // fight it will be if it comes to one.
    void Cast(float wait, const Fight& f, std::mt19937& rng);
    // The button pressed. On the bite it hooks the fish; before it the cast
    // is spoilt (too soon) or simply wound in (before anything touched it).
    // While the fish is on, a press is only part of reeling: None.
    Outcome Strike();
    // A step, with the button held or not. Missed, Landed or Snapped the
    // moment one of them happens -- and the cast is over (Idle).
    Outcome Update(float dt, bool reel, std::mt19937& rng);
    void Stop() { phase = Phase::Idle; }

    bool  Active() const { return phase != Phase::Idle; }
    bool  Hooked() const { return phase == Phase::Reeling; }
    bool  InBand() const { return std::fabs(line - band) <= fight.band; }
    // How far under the bobber is, for drawing: 0 afloat, 1 right under.
    float Dip() const;
    // Out of the green: how near the line is to going, 0 to 1.
    float Strain() const { return fight.snap > 0.0f ? std::clamp(strain / fight.snap, 0.0f, 1.0f) : 0.0f; }

    Phase phase = Phase::Idle;
    float t = 0.0f;          // into this phase
    float length = 0.0f;     // how long this phase lasts, until the fish is on
    Fight fight;
    // The gauge, every part of it 0 at the left to 1 at the right.
    float line = 0.5f, line_v = 0.0f;
    float band = 0.5f, band_v = 0.0f, band_goal = 0.5f;
    float progress = 0.0f;   // how far in it is: landed at 1
    float strain = 0.0f;     // seconds out of the green, made up again in it

private:
    void NewGoal(std::mt19937& rng);
};
// --- foraging -------------------------------------------------------------------------
// The chance a plant gives two herbs instead of one: nothing at its level,
// rising a point for every level past it, to at most a half.
float ForageExtraChance(int level, int plant_level);

// The lowest level any fish at a spot can be caught at.
int SpotLevel(const vector<string>& fish, const ItemDatabase& db);

// --- depletion ------------------------------------------------------------------------
// Whether a tree comes down or a seam gives out on this log or ore: a plain
// dice roll against the node's own chance, for a uniform roll in [0, 1). A
// node with no chance never runs out.
bool Depletes(float chance, float roll);

}
