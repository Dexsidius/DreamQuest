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

private:
    TalentTree trees[3];
};

// Effects that apply whatever the player is holding. Everything else only
// applies to attacks of the style whose tree it came from.
bool TalentEffectIsGlobal(const string& effect);

class Talents {
public:
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

    // The technique the charged attack with this style's weapon comes out as;
    // empty for the plain charged attack. Only a learned technique can be set,
    // and setting the one already chosen clears it.
    const string& Technique(AttackStyle style) const { return technique[static_cast<int>(style)]; }
    bool ToggleTechnique(const string& node_id);

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
    string technique[3];
    string ability[SkillTrees::ABILITY_SLOTS];
    bool has_path = false;
    AttackStyle path = AttackStyle::Melee;
};
