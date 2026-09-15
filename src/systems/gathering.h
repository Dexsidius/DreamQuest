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
// --- foraging -------------------------------------------------------------------------
// The chance a plant gives two herbs instead of one: nothing at its level,
// rising a point for every level past it, to at most a half.
float ForageExtraChance(int level, int plant_level);

// The lowest level any fish at a spot can be caught at.
int SpotLevel(const vector<string>& fish, const ItemDatabase& db);

}
