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
    Craft,
    // Done when the world's flag `target` is set: a story's beat -- the Mayor
    // heard out, a sleeper tried, the dream walked. See RefreshFlagObjectives.
    // After Craft for the same reason Craft is last.
    Flag
};

enum class QuestSource { Board, Npc, Note };

struct QuestStage {
    string        description;
    ObjectiveType type = ObjectiveType::Talk;
    string        target;      // npc id / enemy type / item id / map id
    string        deliver_to;  // NPC for Deliver objectives
    string        map_id;      // optional location restriction for kill events
    int           count = 1;
    // Where its counter starts, for a count that began before the stage did:
    // "five rituals" begun by one already done reads (1/5), not (0/4).
    int           start = 0;
    // What the waypoint points at while this stage is current, for a stage
    // whose target is something that happens rather than something that is
    // there -- a ritual's waves are fought at a witch's table: an object's id,
    // or a kind of object. Empty: the target itself. For a kill it can be a
    // map: where the waypoint looks for what is to be killed, without the kill
    // having to be made there -- a Guild bounty's beast lairs in one place and
    // counts wherever it falls (`map_id` is the one that restricts the kill).
    string        where;
    bool          hidden = false;   // not listed until it becomes current
    // Done, whatever its count, once this world flag is set: a kill count
    // whose monsters are a story's squad (EnemySpawnDef::squad), so a stage
    // that was not yet current when they fell -- a friend got there first --
    // is not left waiting on kills that can never come again.
    string        or_flag;
};

// One of the rewards a quest lets the player choose between: a sword for a
// fighter, a bow for an archer, a staff for a caster -- or whatever else the
// file puts there. The whole of an option is given: several things, coins
// and experience together, if that is what it holds.
struct QuestRewardChoice {
    string label;             // what the option is called; empty: its way of fighting, or its first thing
    string style;             // "melee", "ranged" or "magic": whose option it is; empty for anyone's
    string calling;           // "knight", "ranger", "weaver", "warden": whose, more exactly -- the
                              // Knight and the Warden both fight in melee (Player::CallingKey)
    map<int, int> xp;         // SkillId -> amount
    vector<pair<string,int>> items;
    int    coins = 0;
};

struct QuestRewards {
    map<int, int>       xp;      // SkillId -> amount
    vector<pair<string,int>> items;
    int                 coins = 0;
    // And one of these, the player's pick -- see QuestLog::ChoicesOwed. Empty
    // for a quest that simply pays what it pays.
    vector<QuestRewardChoice> choices;
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
    // A bounty: a daily the Dreamer's Slate posts only on a night the kind it
    // names is out on its dream land in the numbers it asks for (World::
    // DreamBounties), in no pool's turn, and which lapses at dawn if it is not
    // done -- the monsters it was for are not there the next night.
    bool   bounty = false;
    // A Guild bounty: one page of the Guild's ledger of named beasts (Act II,
    // Guild Master Orlend), posted on the board beside his desk. Unlike the
    // Slate's it is not daily and never lapses: taken once, closed once, the
    // beast it names killed wherever it is found. Its first stage's `where`
    // is the beast's lair, for the waypoint and the board.
    bool   guild_bounty = false;
    vector<string> prerequisites; // quest ids that must be complete first
    // A flag set in the world when it is done: what a story waits on -- the
    // foyer's doors stand locked until both suits of armour are down.
    string sets_flag;
    // And one set when its reward is chosen (rewards.choices): the weapon in
    // the cell's chest taken, not merely found.
    string choice_flag;
    vector<QuestStage> stages;
    QuestRewards rewards;
    string completion_text;
    // The quest that begins the moment this one ends, without anybody having
    // to offer it: the trail goes on. Empty for most.
    string then;
};

enum class QuestStatus { NotStarted, Active, Complete };

struct QuestProgress {
    QuestStatus status = QuestStatus::NotStarted;
    int stage = 0;
    int counter = 0;          // progress within the current stage
    int completed_day = -1;   // the quest day it was last finished on
    int started_day = -1;     // and the one it was last taken on
    int completions = 0;
    // A quest with reward choices owes one pick each time it is finished,
    // kept in the save until it is made, so putting it off costs nothing.
    // `chosen` is the last one taken, for the journal to say.
    int owed = 0;
    int chosen = -1;
};

// Events the world raises; the log decides whether any of them matter.
struct QuestEvent {
    ObjectiveType type = ObjectiveType::Kill;
    string target;
    string secondary;         // NPC for Deliver
    int    amount = 1;
    string map_id;            // area in which the event happened
    // When set, only this quest hears it: a line said for one quest, an order
    // handed in. Without it every active quest at a matching stage moves on,
    // which is what a kill or a picked herb wants -- and what moved Oona's
    // poppet past its binding when another quest's line was said to her.
    string quest;
};

class QuestLog {
public:
    // How many of a pool's daily quests a board posts each day.
    static constexpr int DAILY_PER_POOL = 2;

    bool LoadDefinitions(const string& path);

    // The quest day, from the world clock; dailies reset when it changes, and
    // a bounty taken on an earlier day lapses.
    void SetDay(int day);
    int  Today() const { return today; }
    // Bounties that lapsed since this was last asked, for the player to be told.
    int  TakeLapsed() { const int n = lapsed; lapsed = 0; return n; }

    // The bounties posted on `day`, as World::DreamBounties worked them out:
    // the only ones a board offers that day, besides any already taken.
    void PostBounties(int day, const vector<string>& ids);
    int  BountiesDay() const { return bounty_day; }
    // Whether a quest of `level` is within reach of a character of `combat`:
    // no more than LEVEL_RANGE levels either way of it. A board can be set to
    // show only those.
    static constexpr int LEVEL_RANGE = 10;
    static bool InRange(int level, int combat) { return std::abs(level - combat) <= LEVEL_RANGE; }
    // The dailies a pool posts today: the same few all day, different ones on
    // other days, chosen from the pool by the day number. Given the player's
    // skills, a quest they could not take yet is passed over for the next one
    // in the day's order, so a board never posts nothing but work beyond them.
    //
    // And given the skills, it posts work at the player's level first: see
    // ORDER_BAND.
    vector<string> PoolToday(const string& pool, const class Skills* skills = nullptr) const;
    // An order more than this many levels below the player, in its own trade,
    // is posted only when there is not enough nearer their level to fill a
    // day -- so the book a smith is shown climbs with their Smithing.
    static constexpr int ORDER_BAND = 20;
    // The trade an order is for: the skill it pays most in, or paying none,
    // the one it asks most of. And the level it asks of that trade.
    static int TradeOf(const QuestDef& d);
    static int OrderLevel(const QuestDef& d);
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
    // Flag stages done by what the world's flags say now: asked every frame,
    // so a stage begun after its flag was set is done the moment it begins.
    void RefreshFlagObjectives(const std::function<bool(const string&)>& has, const class Inventory& inv);
    // Flags quests finished since this was last asked want set (QuestDef::sets_flag).
    vector<string> TakeFlagsToSet() { vector<string> out; out.swap(flags_to_set); return out; }
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

    // --- rewards to choose ---------------------------------------------------------------
    // A quest whose rewards have `choices` pays the rest as it completes and
    // owes one of the choices until the player takes it: the reward panel asks
    // at once, and the journal keeps asking until it is answered.
    int  ChoicesOwed(const string& id) const;
    vector<string> WithChoicesOwed() const;
    // Takes choice `index` of a quest that owes one, and hands it back for the
    // caller to give; nullptr if nothing is owed or there is no such choice.
    const QuestRewardChoice* TakeChoice(const string& id, int index);
    int  LastChosen(const string& id) const;
    // The way of fighting an option is for: what the file says, or failing
    // that what its first weapon -- or piece of armour -- is for. Empty if
    // neither says.
    static string StyleOf(const QuestRewardChoice& c, const class ItemDatabase& items);

    json ToJson() const;
    void FromJson(const json& j);

private:
    void AdvanceStage(const string& id, const Inventory& inv);
    bool StageSatisfied(const QuestDef& def, const QuestProgress& p, const Inventory& inv) const;
    // Quests finished this pass whose `then` is waiting to be begun.
    vector<string> follow_ups;
    void BeginFollowUps();

    map<string, QuestDef>      defs;
    map<string, QuestProgress> progress;
    vector<string> flags_to_set;
    vector<string> just_completed;
    vector<string> just_started;
    int today = 1;
    // Tonight's bounties, and the day they are for.
    std::set<string> bounties;
    int  bounty_day = -1;
    int  lapsed = 0;
    // A journal just read in has not had its bounties looked at: the next
    // SetDay does, whatever day it is.
    bool sweep = false;
    string followed;
    bool   chosen = false;
    vector<string> taken_order;      // quests in the order they were taken, oldest first
};
