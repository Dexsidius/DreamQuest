#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  Quests.
//
//  A quest is an ordered list of stages; each stage has one objective that the
//  world reports progress against. Quests reach the player three ways: a
//  mission board in a town, an NPC conversation, or a note dropped in the world
//  that reads like a lead and starts the trail.
// -----------------------------------------------------------------------------

enum class ObjectiveType {
    Talk,       // speak to an NPC id
    Kill,       // defeat N enemies of a type
    Collect,    // hold N of an item
    Reach,      // enter a map
    Interact,   // use a specific world object
    Deliver     // hand N of an item to an NPC
};

enum class QuestSource { Board, Npc, Note };

struct QuestStage {
    string        description;
    ObjectiveType type = ObjectiveType::Talk;
    string        target;      // npc id / enemy type / item id / map id
    string        deliver_to;  // NPC for Deliver objectives
    int           count = 1;
    bool          hidden = false;   // not listed until it becomes current
};

struct QuestRewards {
    map<int, int>       xp;      // SkillId -> amount
    vector<pair<string,int>> items;
    int                 coins = 0;
};

struct QuestDef {
    string id, name, summary;
    QuestSource source = QuestSource::Board;
    string giver;                 // npc id, or board id
    int    recommended_level = 1;
    map<int, int> requirements;   // SkillId -> level
    vector<string> prerequisites; // quest ids that must be complete first
    vector<QuestStage> stages;
    QuestRewards rewards;
    string completion_text;
};

enum class QuestStatus { NotStarted, Active, Complete };

struct QuestProgress {
    QuestStatus status = QuestStatus::NotStarted;
    int stage = 0;
    int counter = 0;          // progress within the current stage
};

// Events the world raises; the log decides whether any of them matter.
struct QuestEvent {
    ObjectiveType type = ObjectiveType::Kill;
    string target;
    string secondary;         // NPC for Deliver
    int    amount = 1;
};

class QuestLog {
public:
    bool LoadDefinitions(const string& path);

    const QuestDef* Definition(const string& id) const;
    const map<string, QuestDef>& Definitions() const { return defs; }

    QuestStatus Status(const string& id) const;
    int  Stage(const string& id) const;
    int  Counter(const string& id) const;
    bool IsActive(const string& id) const { return Status(id) == QuestStatus::Active; }
    bool IsComplete(const string& id) const { return Status(id) == QuestStatus::Complete; }

    // True when every prerequisite and skill requirement is met and the quest
    // has not been taken yet.
    bool CanStart(const string& id, const class Skills& skills) const;
    bool Start(const string& id);

    // Feed world events in; anything that finished lands in the completed
    // queue for the caller to hand out rewards for.
    void Notify(const QuestEvent& e, const class Inventory& inv);
    // Collect objectives are satisfied by holding items, so re-check on pickup.
    void RefreshCollectObjectives(const class Inventory& inv);

    vector<string> Active() const;
    vector<string> Completed() const;
    // Quests a board or NPC can currently offer.
    vector<string> AvailableFrom(const string& giver, const Skills& skills) const;

    // Human-readable line for the quest log, e.g. "Slay cave slimes (3/5)".
    string CurrentObjectiveText(const string& id) const;

    // Drained by the world each frame; each entry needs rewards granted.
    vector<string> TakeJustCompleted();
    // Newly started quests, for the "Quest started" banner.
    vector<string> TakeJustStarted();

    json ToJson() const;
    void FromJson(const json& j);

private:
    void AdvanceStage(const string& id, const Inventory& inv);
    bool StageSatisfied(const QuestDef& def, const QuestProgress& p, const Inventory& inv) const;

    map<string, QuestDef>      defs;
    map<string, QuestProgress> progress;
    vector<string> just_completed;
    vector<string> just_started;
};
