#include "gathering.h"

namespace Gathering {

float ForageExtraChance(int level, int plant_level) {
    return std::clamp((level - plant_level) * 0.01f, 0.0f, 0.5f);
}

const char* ToolFor(const string& skill) {
    if (skill == "Woodcutting") return "axe";
    if (skill == "Mining")      return "pickaxe";
    if (skill == "Fishing")     return "rod";
    return "";
}

const char* ClipFor(const string& skill) {
    if (skill == "Woodcutting") return "chop";
    if (skill == "Mining")      return "mine";
    if (skill == "Fishing")     return "fish";
    return "idle";
}

const char* ToolNoun(const string& tool) {
    if (tool == "axe")     return "an axe";
    if (tool == "pickaxe") return "a pickaxe";
    if (tool == "rod")     return "a fishing rod";
    return "a tool";
}

float Speed(int level, float tool_speed) {
    return (1.0f + 0.02f * std::max(1, level)) * std::max(0.1f, tool_speed);
}

float WorkTime(float base_seconds, int level, float tool_speed) {
    // No quicker than this, however good the axe. It was 0.6, which a platinum
    // axe reached on the day it could first be held -- so the three tiers
    // above it, demonite, dracon and enchanted, cut no faster than it did.
    return std::max(0.42f, base_seconds / Speed(level, tool_speed));
}

const ItemDef* BestTool(const Inventory& bag, const Equipment& worn, const ItemDatabase& db,
                        const Skills& skills, const string& tool, const ItemDef** unusable) {
    const ItemDef* best = nullptr;
    const ItemDef* best_locked = nullptr;
    const auto consider = [&](const string& id) {
        const ItemDef* d = id.empty() ? nullptr : db.Get(id);
        if (!d || d->tool != tool) return;
        bool allowed = true;
        for (const auto& req : d->requirements)
            if (skills.Level(req.first) < req.second) allowed = false;
        const ItemDef*& slot = allowed ? best : best_locked;
        if (!slot || d->tool_speed > slot->tool_speed) slot = d;
    };
    for (int i = 0; i < bag.SlotCount(); ++i) consider(bag.Slot(i).id);
    for (int s = 0; s < SLOT_COUNT; ++s) consider(worn.InSlot(s));
    if (unusable) *unusable = best ? nullptr : best_locked;
    return best;
}

const vector<Milestone>& FishingMilestones() {
    static const vector<Milestone> table = {
        {20, 0.10f, 0.00f},
        {40, 0.20f, 0.00f},
        {60, 0.30f, 0.00f},
        {80, 0.30f, 0.10f},
        {99, 0.30f, 0.20f},
    };
    return table;
}

Milestone ExtraCatch(int level) {
    Milestone m{0, 0.0f, 0.0f};
    for (const Milestone& ms : FishingMilestones())
        if (level >= ms.level) m = ms;
    return m;
}

int CatchCount(int level, float roll) {
    const Milestone m = ExtraCatch(level);
    if (roll < m.three) return 3;
    if (roll < m.three + m.two) return 2;
    return 1;
}

string PickFish(const vector<string>& fish, int level, const ItemDatabase& db, std::mt19937& rng) {
    // Best first.
    vector<const ItemDef*> open;
    for (const string& id : fish)
        if (const ItemDef* d = db.Get(id))
            if (d->fish_level <= level) open.push_back(d);
    if (open.empty()) return "";
    std::sort(open.begin(), open.end(),
              [](const ItemDef* a, const ItemDef* b) { return a->fish_level > b->fish_level; });

    // Each fish in turn, best first, bites with a chance that grows the
    // further the level is past it; the least of them always bites.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t i = 0; i + 1 < open.size(); ++i) {
        const float chance = std::min(0.75f, 0.30f + (level - open[i]->fish_level) * 0.01f);
        if (unit(rng) < chance) return open[i]->id;
    }
    return open.back()->id;
}

bool Depletes(float chance, float roll) {
    return chance > 0.0f && roll < chance;
}

int SpotLevel(const vector<string>& fish, const ItemDatabase& db) {
    int lowest = 0;
    for (const string& id : fish)
        if (const ItemDef* d = db.Get(id))
            lowest = (lowest == 0) ? d->fish_level : std::min(lowest, d->fish_level);
    return std::max(1, lowest);
}

}
