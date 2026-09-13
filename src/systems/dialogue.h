#pragma once
#include "../headers.h"
#include "quest.h"

// -----------------------------------------------------------------------------
//  Dialogue: a graph of nodes in data/dialogue.json.
//
//  Options can be gated on quest state, inventory or skill level, so one NPC
//  says different things before, during and after a quest without needing
//  separate conversations.
// -----------------------------------------------------------------------------

struct DialogueCondition {
    string quest;                 // quest id to test
    // "not_started" | "active" | "complete", or "available": the quest can be
    // taken right now -- its prerequisites done, its levels met, and for a
    // daily, posted today and not already done today. Offers use "available",
    // so nobody offers work the player is not ready for. "locked" is the
    // opposite: not taken, and not yet possible to take -- for "come back
    // when you are ready".
    string quest_state;
    int    quest_stage = -1;      // when >= 0, the active stage must match
    string has_item;
    int    has_qty = 1;
    string skill;                 // skill name
    int    skill_level = 0;
    // Prerequisites for a line, however it is otherwise gated.
    vector<string> after;         // quests that must be complete
    string flag;                  // a world flag that must be set, e.g. "visited:dreamworld"
    string no_flag;               // one that must not be
    int    combat = 0;            // combat level at least this
    string time;                  // "day" or "night"
    bool   invert = false;

    bool Empty() const {
        return quest.empty() && has_item.empty() && skill.empty() && after.empty() &&
               flag.empty() && no_flag.empty() && combat <= 0 && time.empty();
    }
};

struct DialogueAction {
    string start_quest;
    string advance_quest;         // fires a Talk objective against this NPC
    string give_item;
    int    give_qty = 1;
    string take_item;
    int    take_qty = 1;
    string open_shop;
    string skill_xp;              // skill name
    int    xp_amount = 0;
    bool   heal = false;

    bool Empty() const {
        return start_quest.empty() && advance_quest.empty() && give_item.empty() &&
               take_item.empty() && open_shop.empty() && skill_xp.empty() && !heal;
    }
};

struct DialogueOption {
    string text;
    string next;                  // "" or "end" closes the conversation
    DialogueCondition condition;
    DialogueAction    action;
};

struct DialogueNode {
    string id, speaker, text;
    DialogueAction on_enter;
    vector<DialogueOption> options;
};

class DialogueDatabase {
public:
    bool Load(const string& path);
    const DialogueNode* Get(const string& id) const;
    bool Has(const string& id) const { return nodes.count(id) > 0; }

private:
    map<string, DialogueNode> nodes;
};

// Everything a condition needs to be evaluated, passed in rather than reached
// for, so dialogue stays independent of the world.
struct DialogueContext {
    const QuestLog*  quests = nullptr;
    const class Inventory* inventory = nullptr;
    const class Skills*    skills = nullptr;
    const std::set<string>* flags = nullptr;
    bool night = false;
};

bool EvaluateCondition(const DialogueCondition& c, const DialogueContext& ctx);

// Drives one conversation. The owner pumps input into it and reads back which
// actions fired.
class DialogueRunner {
public:
    // The context is needed from the very first node: without it every
    // quest-gated option on an NPC's opening line used to show at once.
    void Begin(const DialogueDatabase* db, const string& node_id,
               const string& npc_id, const string& npc_name, const DialogueContext& ctx);
    void End() { active = false; }
    bool Active() const { return active; }

    const DialogueNode* Node() const { return node; }
    const string& NpcId() const { return npc_id; }
    const string& SpeakerName() const;

    // Options whose conditions pass, in display order.
    const vector<const DialogueOption*>& VisibleOptions() const { return visible; }
    int  Selected() const { return selected; }
    void MoveSelection(int delta);

    // Confirms the highlighted option; the chosen action is queued for the
    // world to apply. Returns false once the conversation has ended.
    bool Choose(const DialogueContext& ctx);

    // Drained by the world after each Choose / Begin.
    vector<DialogueAction> TakeActions();

    // Typewriter reveal, so text does not appear all at once.
    void Update(float dt);
    void SkipReveal() { reveal = 1e9f; }
    string RevealedText() const;
    bool  FullyRevealed() const;

private:
    void EnterNode(const string& id, const DialogueContext& ctx);
    void RebuildVisible(const DialogueContext& ctx);

    const DialogueDatabase* db = nullptr;
    const DialogueNode* node = nullptr;
    vector<const DialogueOption*> visible;
    vector<DialogueAction> pending;

    string npc_id, npc_name;
    int   selected = 0;
    bool  active = false;
    float reveal = 0.0f;          // characters revealed so far
};
