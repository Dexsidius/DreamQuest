#pragma once
#include "../headers.h"
#include "combat.h"

class Skills;

// ---------------------------------------------------------------------------
//  Skill trees
//
//  One tree for each combat style, in data/skill_trees.json. A tree is three
//  branches five nodes deep. Its skill -- Attack for melee, Ranged, Magic --
//  earns a point at every fifth level, and a node costs one point, needs its
//  milestone level (5, 15, 30, 50, 70) and needs the node above it in its
//  branch. So the first point is a choice of direction, and by level 70 a
//  character has most of one branch and a taste of the others; not until 75
//  do the points cover a whole tree.
//
//  Most nodes are passive: more damage, faster attacks, critical strikes,
//  healing on hit, defence, stamina. The middle of each branch is a
//  technique -- a new move. Learning one does nothing by itself; choosing it
//  as the style's technique makes the charged heavy attack with that style's
//  weapon come out as it instead, so the moves need no new buttons.
// ---------------------------------------------------------------------------

struct TalentNode {
    string id, name, description;
    string technique;              // empty for a passive
    int    branch = 0, row = 0;
    int    level = 1;              // milestone in the tree's skill
    map<string, float> effects;
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
    static constexpr int ROWS = 5;
    static constexpr int LEVELS_PER_POINT = 5;

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
    enum class Why { Ok, Learned, NoPoints, Level, Prerequisite, Unknown };

    void SetDatabase(const SkillTrees* d) { db = d; }
    const SkillTrees* Database() const { return db; }

    int  PointsEarned(AttackStyle style, const Skills& skills) const;
    int  PointsSpent(AttackStyle style) const;
    int  PointsFree(AttackStyle style, const Skills& skills) const {
        return PointsEarned(style, skills) - PointsSpent(style);
    }

    Why  CanLearn(const string& id, const Skills& skills) const;
    bool Learn(const string& id, const Skills& skills);
    bool Has(const string& id) const { return learned.count(id) > 0; }
    // Unlearns a whole tree, points and technique together.
    void Reset(AttackStyle style);

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
    const SkillTrees* db = nullptr;
    std::set<string> learned;
    string technique[3];
};
