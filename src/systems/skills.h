#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  Skills, in the Old School RuneScape mould: every skill is trained by doing
//  the thing, XP is banked per skill, and levels come off the classic curve
//  (level 92 is roughly half the XP of level 99).
// -----------------------------------------------------------------------------

enum SkillId {
    SKILL_ATTACK = 0,
    SKILL_STRENGTH,
    SKILL_DEFENCE,
    SKILL_HITPOINTS,
    SKILL_RANGED,
    SKILL_MAGIC,
    SKILL_WOODCUTTING,
    SKILL_MINING,
    SKILL_CRAFTING,
    SKILL_COOKING,
    SKILL_FISHING,
    SKILL_SMITHING,      // smelting and smithing at an anvil
    SKILL_FORAGING,      // picking herbs and plants
    SKILL_BREWING,       // potions, at a cauldron
    // Added at the end, so every skill before them keeps its number: a
    // guest's experience drop names its skill by number (net::Delta::Xp).
    SKILL_TANNING,       // hide cut and sewn on a tanner's rack: once Crafting's
    SKILL_CLOTHIER,      // cloth woven and sewn at a loom: once Crafting's
    SKILL_ENCHANTING,    // charms worked into worn pieces: once Magic's
    SKILL_COUNT
};

// The skills page groups them. A category's skills are listed in the order
// they are shown; every skill is in exactly one.
enum SkillCategory { CATEGORY_FORGING = 0, CATEGORY_COMBAT, CATEGORY_GATHERING, CATEGORY_WITCHCRAFT, CATEGORY_COUNT };
const char* CategoryName(int category);
// A line on what a category holds, and on how a skill is trained, for the
// skills page to say.
const char* CategoryBlurb(int category);
const char* SkillBlurb(int skill);
const vector<int>& CategorySkills(int category);
int SkillCategoryOf(int skill);

static constexpr int MAX_SKILL_LEVEL = 99;

const char* SkillName(int skill);
// Case-insensitive; returns -1 when the name is not a skill.
int SkillFromName(const string& name);

// Cumulative XP required to reach a level, using the OSRS formula
//   xp(L) = floor( sum(i=1..L-1) floor(i + 300 * 2^(i/7)) / 4 )
int XpForLevel(int level);
int LevelForXp(int xp);

struct LevelUp {
    int skill = -1;
    int level = 0;
};

class Skills {
public:
    Skills();

    int Xp(int skill) const;
    int Level(int skill) const;
    // Current in-combat value, which drains (Hitpoints) or is boosted.
    int Current(int skill) const;
    void SetCurrent(int skill, int value);
    void ResetCurrent();                 // restore all drained/boosted levels
    // Puts back what has been drained and leaves what has been added: a healer
    // mends you, and does not pour your Emberfire Elixir out while she is at it.
    void RestoreDrained();

    // Returns a level-up if this award crossed a threshold, so the caller can
    // show the banner without polling.
    bool AddXp(int skill, int amount, LevelUp& out);
    void SetXp(int skill, int xp);

    // OSRS-style combat level from the melee/ranged/magic triangle.
    int CombatLevel() const;
    // Total level and total XP, for the skills panel header.
    int TotalLevel() const;
    long long TotalXp() const;

    json ToJson() const;
    void FromJson(const json& j);

private:
    int xp[SKILL_COUNT];
    int current[SKILL_COUNT];
};
