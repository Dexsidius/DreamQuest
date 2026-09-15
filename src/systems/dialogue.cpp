#include "dialogue.h"
#include "items.h"
#include "skills.h"
#include <fstream>

static constexpr float REVEAL_CPS = 45.0f;

static DialogueCondition ParseCondition(const json& o) {
    DialogueCondition c;
    if (!o.is_object()) return c;
    c.quest       = o.value("quest", string(""));
    c.quest_state = o.value("state", string(""));
    c.quest_stage = o.value("stage", -1);
    c.has_item    = o.value("has_item", string(""));
    c.has_qty     = o.value("qty", 1);
    c.skill       = o.value("skill", string(""));
    c.skill_level = o.value("level", 0);
    c.invert      = o.value("not", false);
    if (o.contains("after")) {
        if (o["after"].is_array())
            for (const auto& q : o["after"]) c.after.push_back(q.get<string>());
        else
            c.after.push_back(o["after"].get<string>());
    }
    c.flag    = o.value("flag", string(""));
    c.no_flag = o.value("no_flag", string(""));
    c.combat  = o.value("combat", 0);
    c.time    = o.value("time", string(""));
    c.order_ready = o.value("order_ready", false);
    return c;
}

static DialogueAction ParseAction(const json& o) {
    DialogueAction a;
    if (!o.is_object()) return a;
    a.start_quest   = o.value("start_quest", string(""));
    a.advance_quest = o.value("advance", string(""));
    a.give_item     = o.value("give", string(""));
    a.give_qty      = o.value("give_qty", 1);
    a.take_item     = o.value("take", string(""));
    a.take_qty      = o.value("take_qty", 1);
    a.open_shop     = o.value("shop", string(""));
    a.open_orders   = o.value("orders", string(""));
    a.hand_in       = o.value("hand_in", false);
    a.skill_xp      = o.value("xp_skill", string(""));
    a.xp_amount     = o.value("xp", 0);
    a.heal          = o.value("heal", false);
    return a;
}

bool DialogueDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("DialogueDatabase: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("DialogueDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        DialogueNode n;
        n.id      = it.key();
        n.speaker = o.value("speaker", string(""));
        n.text    = o.value("text", string(""));
        if (o.contains("action")) n.on_enter = ParseAction(o["action"]);

        if (o.contains("options"))
            for (const auto& opt : o["options"]) {
                DialogueOption d;
                d.text = opt.value("text", string("..."));
                d.next = opt.value("next", string("end"));
                if (opt.contains("if"))     d.condition = ParseCondition(opt["if"]);
                if (opt.contains("action")) d.action    = ParseAction(opt["action"]);
                n.options.push_back(d);
            }

        nodes[n.id] = n;
    }

    SDL_Log("DialogueDatabase: loaded %d dialogue nodes", static_cast<int>(nodes.size()));
    return true;
}

const DialogueNode* DialogueDatabase::Get(const string& id) const {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

bool EvaluateCondition(const DialogueCondition& c, const DialogueContext& ctx) {
    if (c.Empty()) return true;

    bool pass = true;

    if (!c.quest.empty() && ctx.quests) {
        const QuestStatus st = ctx.quests->Status(c.quest);
        if (c.quest_state == "available")
            pass = pass && ctx.skills && ctx.quests->CanStart(c.quest, *ctx.skills);
        else if (c.quest_state == "locked")
            pass = pass && st == QuestStatus::NotStarted && ctx.skills &&
                   !ctx.quests->CanStart(c.quest, *ctx.skills);
        else if (c.quest_state == "not_started") pass = pass && (st == QuestStatus::NotStarted);
        else if (c.quest_state == "active") pass = pass && (st == QuestStatus::Active);
        else if (c.quest_state == "complete") pass = pass && (st == QuestStatus::Complete);
        if (c.quest_stage >= 0)
            pass = pass && (ctx.quests->Stage(c.quest) == c.quest_stage);
    }

    if (!c.has_item.empty() && ctx.inventory)
        pass = pass && ctx.inventory->Has(c.has_item, c.has_qty);

    if (!c.skill.empty() && ctx.skills) {
        const int s = SkillFromName(c.skill);
        pass = pass && (s >= 0) && (ctx.skills->Level(s) >= c.skill_level);
    }

    // Missing context counts against a prerequisite rather than for it: a line
    // that cannot be checked is a line that stays hidden.
    for (const string& q : c.after)
        pass = pass && ctx.quests && ctx.quests->IsComplete(q);
    if (!c.flag.empty())    pass = pass && ctx.flags && ctx.flags->count(c.flag) > 0;
    if (!c.no_flag.empty()) pass = pass && !(ctx.flags && ctx.flags->count(c.no_flag) > 0);
    if (c.combat > 0)       pass = pass && ctx.skills && ctx.skills->CombatLevel() >= c.combat;
    if (c.time == "night")  pass = pass && ctx.night;
    if (c.time == "day")    pass = pass && !ctx.night;
    if (c.order_ready)
        pass = pass && ctx.quests && ctx.inventory && !ctx.npc.empty() &&
               !ctx.quests->ReadyToDeliver(ctx.npc, *ctx.inventory).empty();

    return c.invert ? !pass : pass;
}

void DialogueRunner::Begin(const DialogueDatabase* database, const string& node_id,
                           const string& id, const string& name, const DialogueContext& ctx) {
    db      = database;
    npc_id  = id;
    npc_name = name;
    active  = true;
    pending.clear();
    EnterNode(node_id, ctx);
}

void DialogueRunner::EnterNode(const string& id, const DialogueContext& ctx) {
    if (id.empty() || id == "end" || !db) { active = false; node = nullptr; return; }

    node = db->Get(id);
    if (!node) {
        SDL_Log("DialogueRunner: missing node '%s'", id.c_str());
        active = false;
        return;
    }

    if (!node->on_enter.Empty()) pending.push_back(node->on_enter);
    selected = 0;
    reveal   = 0.0f;
    RebuildVisible(ctx);
}

void DialogueRunner::RebuildVisible(const DialogueContext& ctx) {
    visible.clear();
    if (!node) return;
    DialogueContext c = ctx;
    if (c.npc.empty()) c.npc = npc_id;
    for (const auto& o : node->options)
        if (EvaluateCondition(o.condition, c)) visible.push_back(&o);
    selected = std::clamp(selected, 0, std::max(0, static_cast<int>(visible.size()) - 1));
}

const string& DialogueRunner::SpeakerName() const {
    if (node && !node->speaker.empty()) return node->speaker;
    return npc_name;
}

void DialogueRunner::MoveSelection(int delta) {
    if (visible.empty()) return;
    const int n = static_cast<int>(visible.size());
    selected = ((selected + delta) % n + n) % n;
}

bool DialogueRunner::Choose(const DialogueContext& ctx) {
    if (!active || !node) return false;

    // Re-evaluate here as well: an action taken on this node may have changed
    // quest state since the options were built.
    RebuildVisible(ctx);

    if (visible.empty()) { active = false; return false; }
    if (selected < 0 || selected >= static_cast<int>(visible.size())) { active = false; return false; }

    const DialogueOption* opt = visible[selected];
    if (!opt->action.Empty()) pending.push_back(opt->action);

    const string next = opt->next;
    EnterNode(next, ctx);
    return active;
}

vector<DialogueAction> DialogueRunner::TakeActions() {
    vector<DialogueAction> out;
    out.swap(pending);
    return out;
}

void DialogueRunner::Update(float dt) {
    if (active) reveal += dt * REVEAL_CPS;
}

string DialogueRunner::RevealedText() const {
    if (!node) return "";
    const size_t n = std::min(node->text.size(),
                              static_cast<size_t>(std::max(0.0f, reveal)));
    return node->text.substr(0, n);
}

bool DialogueRunner::FullyRevealed() const {
    if (!node) return true;
    return reveal >= static_cast<float>(node->text.size());
}
