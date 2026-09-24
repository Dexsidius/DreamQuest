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
    Deliver,    // hand N of an item to an NPC
    // Make N of an item: at a bench, an anvil, a cauldron, a loom or a fire.
    // Counted as it is made, not by what is in the bag -- so it cannot be
    // bought, and it cannot be finished by the three cooked meat a new
    // character starts with. It is what a lesson asks for: do the thing.
    //
    // Last in the list on purpose. The number goes over the wire and into
    // nothing else, and everything before it keeps the value it had.
    Craft
};

enum class QuestSource { Board, Npc, Note };

struct QuestStage {
    string        description;
    ObjectiveType type = ObjectiveType::Talk;
    string        target;      // npc id / enemy type / item id / map id
    string        deliver_to;  // NPC for Deliver objectives
    string        map_id;      // optional location restriction for kill events
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
    // Which tab of the journal it belongs on. A story quest is the chain the
    // world is actually about; a tutorial teaches one skill and is kept apart
    // from both, so neither list is buried under the other.
    bool   major = false;
    bool   tutorial = false;
    map<int, int> requirements;   // SkillId -> level
    int    combat_level = 0;      // "Combat" in the file's req block
    // A daily quest can be taken again on any later day, and is one of a pool
    // that a board rotates through: a few of the pool are posted each day.
    bool   daily = false;
    string pool;
    // How many of its pool are posted a day; 0 means DAILY_PER_POOL. A pool
    // posts the largest number any of its quests asks for.
    int    posts = 0;
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
    int completed_day = -1;   // the quest day it was last finished on
    int completions = 0;
};

// Events the world raises; the log decides whether any of them matter.
struct QuestEvent {
    ObjectiveType type = ObjectiveType::Kill;
    string target;
    string secondary;         // NPC for Deliver
    int    amount = 1;
    string map_id;            // area in which the event happened
};

class QuestLog {
public:
    // How many of a pool's daily quests a board posts each day.
    static constexpr int DAILY_PER_POOL = 2;

    bool LoadDefinitions(const string& path);

    // The quest day, from the world clock; dailies reset when it changes.
    void SetDay(int day) { today = day; }
    int  Today() const { return today; }
    // The dailies a pool posts today: the same few all day, different ones on
    // other days, chosen from the pool by the day number. Given the player's
    // skills, a quest they could not take yet is passed over for the next one
    // in the day's order, so a board never posts nothing but work beyond them.
    vector<string> PoolToday(const string& pool, const class Skills* skills = nullptr) const;
    int  PostsPerDay(const string& pool) const;
    // Whether a daily is posted today (or is already taken); always true for
    // a quest that is not daily.
    bool OfferedToday(const string& id, const class Skills* skills = nullptr) const;
    int  Completions(const string& id) const;
    // Prerequisites, Combat level and skill levels, and nothing about whether
    // it has been taken.
    bool MeetsRequirements(const QuestDef& d, const class Skills& skills) const;

    // Orders: active dailies given by this NPC whose current stage is a
    // delivery to them that the bag can fill in full, right now.
    vector<string> ReadyToDeliver(const string& npc, const class Inventory& inv) const;

    const QuestDef* Definition(const string& id) const;
    const map<string, QuestDef>& Definitions() const { return defs; }

    QuestStatus Status(const string& id) const;
    int  Stage(const string& id) const;
    int  Counter(const string& id) const;
    bool IsActive(const string& id) const {
        return relay ? relay_active.count(id) > 0 : Status(id) == QuestStatus::Active;
    }

    // A friend's journal is on their own machine. What the host keeps in its
    // place only listens: events are kept to be sent on, and "is this quest
    // being done" is answered from the list their machine last sent.
    bool relay = false;
    vector<QuestEvent> relayed;
    std::set<string>   relay_active;
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
    // A strange thing found starts what it starts by being in the bag, however
    // it got there: every quest a thing held names (`starts_quest`) that has not
    // been begun is begun, and the things that began them are handed back so
    // the player can be told. Nothing is asked first -- no level, no quest
    // before it; what they can make of it is theirs to find out.
    vector<const struct ItemDef*> StartFromFinds(const class Inventory& bag, const class ItemDatabase& items);

    vector<string> Active() const;
    vector<string> Completed() const;

    // --- the quest being followed ----------------------------------------------------
    // One quest has the waypoint. It is whichever was taken last, unless the
    // player has chosen one in the journal, and then it is that one until it is
    // done. Never a quest that is not in hand: with the followed one finished or
    // gone, it is the newest that still is.
    string Followed() const;
    bool   Chosen() const { return chosen && IsActive(followed); }
    // Follow this one, by choice. Asked of the one already chosen, lets go of it.
    void   Follow(const string& id);
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
    int today = 1;
    string followed;
    bool   chosen = false;
    vector<string> taken_order;      // quests in the order they were taken, oldest first
};
