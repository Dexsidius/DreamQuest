#pragma once
#include "../headers.h"
#include "combat.h"

class Skills;

// ---------------------------------------------------------------------------
//  Skill trees
//
//  One tree for each combat style, in data/skill_trees.json, and a character
//  has one: their path's. A tree is three branches eight nodes deep. Its skill
//  -- Attack for melee, Ranged, Magic -- earns a point at every third level,
//  and a rank of a node costs one point, needs its milestone level (5, 15, 30,
//  40, 47, 54, 62, 70 -- closer together as levels come slower) and needs a
//  rank of the node above it in its branch. Thirty-three points by level 99
//  against forty-two ranks: two branches to the bottom and a little of the
//  third, or all three most of the way.
//
//  A branch, from the top: two passives of three ranks; a technique -- a new
//  move that, once chosen, the charged heavy attack comes out as, so it needs
//  no new button; an ability; a passive of two ranks that asks when; a second
//  ability; a second such passive; and a capstone.
// ---------------------------------------------------------------------------

struct TalentNode {
    string id, name, description;
    string technique;              // a move that replaces the charged heavy attack
    // An ability: a move of its own, on its own buttons, with a cooldown and a
    // cost. A tree teaches six and three can be carried at once -- guard held
    // and the light button, the heavy one, or lock on.
    string ability;
    float  cooldown = 0.0f;        // seconds
    int    stamina_cost = 0, mana_cost = 0;
    int    branch = 0, row = 0;
    int    level = 1;              // milestone in the tree's skill
    // How many times it can be bought. `effects` is what one rank gives.
    int    ranks = 1;
    map<string, float> effects;

    bool Passive() const { return technique.empty() && ability.empty(); }
};

// What a boss leaves the first time it is brought down: something small and
// for good, of a kind no tree teaches or that any path can use. `paths` is who
// may be given it -- nobody is handed mana they have no spell to spend on --
// and empty is anybody.
struct BoonDef {
    string id, name, text;
    vector<AttackStyle> paths;
    map<string, float> effects;

    bool For(AttackStyle path) const {
        return paths.empty() || std::find(paths.begin(), paths.end(), path) != paths.end();
    }
};

// What a boss leaves the fifteenth time: a totem, which is a thing in the bag
// (`item`) until it is stood in the ring in the house at Mossvale, and then for
// the rest of that day is a boon -- a bigger one than a boss's first, because it
// is one at a time, for a day, and has to be gone home for.
struct TotemDef {
    string item, boss, name, text;
    map<string, float> effects;
    // What the house at Mossvale is dressed in while this totem stands in its
    // ring: the boards, the walls, the rug and hangings, their trim, and the
    // light the totem gives off (see World::HouseDress). `house` is false for
    // a totem with no palette, which leaves the room as it is.
    bool house = false;
    SDL_Color floor{255, 255, 255, 255}, wall{255, 255, 255, 255}, cloth{255, 255, 255, 255},
              trim{255, 255, 255, 255}, light{255, 255, 255, 255};
};

struct TalentTree {
    string id, name;
    int    skill = 0;              // SkillId that earns points and gates nodes
    vector<string> branches;
    vector<TalentNode> nodes;

    const TalentNode* At(int branch, int row) const;
    // Columns this tree draws: its named branches, or as many as its nodes
    // reach into. Three for most; the melee tree has a fourth for footwork.
    int BranchCount() const;
};

class SkillTrees {
public:
    static constexpr int BRANCHES = 3;          // the full columns every tree has
    static constexpr int ROWS = 8;
    // A point every three levels: thirty-three by level 99, against the
    // forty-two ranks a tree holds. Two branches to the bottom and a little of
    // the third, or all three most of the way: a build, not a checklist.
    static constexpr int LEVELS_PER_POINT = 3;
    static constexpr int ABILITY_SLOTS = 3;

    bool Load(const string& path);
    const TalentTree& Tree(AttackStyle style) const { return trees[static_cast<int>(style)]; }
    // The node and the style whose tree it is in; null if nobody has that id.
    const TalentNode* Find(const string& id, AttackStyle* style = nullptr) const;

    // The boons, from the same file: see Talents::SlayBoss.
    const vector<BoonDef>& Boons() const { return boons; }
    const BoonDef* Boon(const string& id) const;

    // And the totems: see Talents::PlaceTotem. By the item it is, or the boss it is of.
    const vector<TotemDef>& Totems() const { return totems; }
    const TotemDef* Totem(const string& item) const;
    const TotemDef* TotemOf(const string& boss) const;

private:
    TalentTree trees[3];
    vector<BoonDef> boons;
    vector<TotemDef> totems;
};

// Effects that apply whatever the player is holding. Everything else only
// applies to attacks of the style whose tree it came from.
bool TalentEffectIsGlobal(const string& effect);

class Talents {
public:
    // Coins for each point taken back when a tree is unlearned.
    static constexpr int RESPEC_FEE = 60;
    // Learned: every rank it has is bought. OtherPath: it is in a tree that
    // is not this character's.
    enum class Why { Ok, Learned, NoPoints, Level, Prerequisite, Unknown, OtherPath };

    void SetDatabase(const SkillTrees* d) { db = d; }
    const SkillTrees* Database() const { return db; }

    // A character has one path -- the hero the blade, the warden the bow, the
    // wayfarer the staff -- and one tree, their path's. The other two cannot
    // be learned from, and a save from before this was so loses what it had
    // bought in them. Unset, every tree is open: the self-test's plain Talents.
    void SetPath(AttackStyle style);
    bool HasPath() const { return has_path; }
    AttackStyle Path() const { return path; }
    bool Open(AttackStyle style) const { return !has_path || style == path; }

    // --- what a boss leaves -----------------------------------------------------------
    // Killing a boss paid what it dropped and nothing else: the Pit Lord was a
    // long fight for a loot roll. The *first* time a character brings one down
    // it leaves them two things, for good: a point for their tree, over and
    // above the one every third level earns, and a boon -- one of
    // data/skill_trees.json's, by the dice, of those their path can use and they
    // do not already have. Eleven bosses, so eleven points against the nine a
    // finished tree is short of: someone who has killed everything in the game
    // can finish their tree, and nobody else can.
    //
    // The first time, and once: a boss is back the next dawn and leaves its
    // loot again, but this is kept count of by who it was. It lives here, with
    // the ranks, so it goes wherever they go -- the save, the character a
    // friend keeps on their own machine, the sheet their host rolls with.
    struct Trophy {
        bool first = false;              // false: they had killed it before
        const BoonDef* boon = nullptr;   // what it left; null with no boons to give
        int  kills = 0;                  // how many times, this one included
        const TotemDef* totem = nullptr; // the fifteenth, and only the fifteenth: its totem
    };
    // How many times a boss has to be brought down before it leaves its totem.
    // A boss is back once a day, so this is a fortnight of going back for it.
    static constexpr int TOTEM_KILLS = 15;
    int    Kills(const string& boss_id) const {
        const auto it = kills.find(boss_id);
        return it == kills.end() ? 0 : it->second;
    }
    Trophy SlayBoss(const string& boss_id, std::mt19937& rng);
    bool   HasSlain(const string& boss_id) const { return slain.count(boss_id) > 0; }
    const std::set<string>& BossesSlain() const { return slain; }
    // A point for each.
    int    BonusPoints() const { return static_cast<int>(slain.size()); }
    const vector<string>& Boons() const { return boons; }
    bool   HasBoon(const string& id) const { return std::find(boons.begin(), boons.end(), id) != boons.end(); }
    // What the boons come to for one effect: added to whatever the tree gives,
    // whatever is in hand.
    float  BoonEffect(const string& effect) const;

    // --- the ring in the house -----------------------------------------------------------
    // One totem stands in it at a time. Touched, it gives its boon for the rest
    // of that day -- the quest day, which turns at dawn -- wherever the
    // character goes, and through a death; the next day it is a carving in a
    // ring until it is touched again. Another can be stood in its place, and
    // the one that was there goes back in the bag: one boss's blessing at a
    // time, never two.
    //
    // `item` is the totem's item id. These do not touch the bag: whoever calls
    // them moves the thing itself. Placing returns what was standing there.
    string PlaceTotem(const string& item, int quest_day);
    // Lifted out: the ring is empty and the blessing, if there was one, is over.
    string TakeTotem();
    const string& PlacedTotem() const { return placed; }
    // Today, by the world's clock. The world says, whenever it changes.
    void   SetToday(int quest_day) { today = quest_day; }
    bool   TotemAwake() const { return !placed.empty() && totem_day == today; }
    const TotemDef* ActiveTotem() const;
    // What stands in the ring, awake or asleep: the house is dressed for it
    // either way, brighter while it is awake.
    const TotemDef* PlacedTotemDef() const;

    int  PointsEarned(AttackStyle style, const Skills& skills) const;
    int  PointsSpent(AttackStyle style) const;
    int  PointsFree(AttackStyle style, const Skills& skills) const {
        return PointsEarned(style, skills) - PointsSpent(style);
    }

    Why  CanLearn(const string& id, const Skills& skills) const;
    bool Learn(const string& id, const Skills& skills);
    bool Has(const string& id) const { return Rank(id) > 0; }
    int  Rank(const string& id) const {
        const auto it = ranks.find(id);
        return it == ranks.end() ? 0 : it->second;
    }
    // Unlearns a whole tree, points, technique and abilities together.
    void Reset(AttackStyle style);

    // The three abilities carried: slot 0 is guard + light, slot 1 guard +
    // heavy, slot 2 guard + lock on. Each holds a node id, or nothing.
    const string& AbilityNode(int slot) const { return ability[std::clamp(slot, 0, SkillTrees::ABILITY_SLOTS - 1)]; }
    const TalentNode* Ability(int slot) const;
    // Steps a learned ability through: the first slot with room, then on to the
    // next -- changing places with whatever is there -- and from the last, put
    // away. Returns the slot it is now in, or -1.
    int  CycleAbility(const string& node_id);
    int  SlotOf(const string& node_id) const;
    // Puts a learned ability in a slot outright -- or nothing, for no id. One
    // that was in another slot changes places with what was here, so choosing
    // never loses an ability off the bar.
    bool SetAbility(int slot, const string& node_id);

    // The technique the charged attack with this style's weapon comes out as;
    // empty for the plain charged attack. Only a learned technique can be set,
    // and setting the one already chosen clears it.
    const string& Technique(AttackStyle style) const { return technique[static_cast<int>(style)]; }
    bool ToggleTechnique(const string& node_id);
    // The same, said outright: this learned technique's node, or no id for
    // the plain charged attack.
    bool SetTechnique(AttackStyle style, const string& node_id);

    // Summed effect of every learned node: a style-scoped effect from that
    // style's tree, a global one from all three.
    float Effect(const string& effect, AttackStyle style) const;
    float Global(const string& effect) const;

    json ToJson() const;
    void FromJson(const json& j);

private:
    void DropOtherPaths();

    const SkillTrees* db = nullptr;
    std::map<string, int> ranks;
    std::set<string> slain;          // bosses, by id
    std::map<string, int> kills;     // and how many times each
    vector<string> boons;            // one for each, in the order they came
    string placed;                   // the totem in the ring, by item id
    int    totem_day = -1;           // the day it was last touched
    int    today = 0;
    string technique[3];
    string ability[SkillTrees::ABILITY_SLOTS];
    bool has_path = false;
    AttackStyle path = AttackStyle::Melee;
};
