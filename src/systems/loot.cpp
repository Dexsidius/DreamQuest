#include "loot.h"
#include <fstream>

LootSystem::LootSystem() : rng(std::random_device{}()) {}

static LootEntry ParseEntry(const json& o) {
    LootEntry e;
    e.item      = o.value("item", string(""));
    e.sub_table = o.value("table", string(""));
    e.weight    = std::max(0, o.value("weight", 1));

    if (o.contains("qty")) {
        const json& q = o["qty"];
        if (q.is_array() && q.size() >= 2) {
            e.qty_min = q[0].get<int>();
            e.qty_max = q[1].get<int>();
        } else if (q.is_number()) {
            e.qty_min = e.qty_max = q.get<int>();
        }
    }
    if (e.qty_max < e.qty_min) std::swap(e.qty_min, e.qty_max);
    return e;
}

bool LootSystem::Load(const string& path, bool required) {
    std::ifstream in(path);
    if (!in) {
        if (required) SDL_Log("LootSystem: cannot open '%s'", path.c_str());
        return !required;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("LootSystem: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        LootTable t;
        t.id    = it.key();
        t.rolls = o.value("rolls", 1);

        if (o.contains("always"))
            for (const auto& e : o["always"]) t.always.push_back(ParseEntry(e));

        if (o.contains("table"))
            for (const auto& e : o["table"]) {
                LootEntry entry = ParseEntry(e);
                t.total_weight += entry.weight;
                t.table.push_back(entry);
            }

        tables[t.id] = t;
    }

    SDL_Log("LootSystem: loaded %d loot tables", static_cast<int>(tables.size()));
    return true;
}

const LootTable* LootSystem::Get(const string& id) const {
    auto it = tables.find(id);
    return it == tables.end() ? nullptr : &it->second;
}

int LootSystem::RollQty(const LootEntry& e) {
    if (e.qty_max <= e.qty_min) return e.qty_min;
    std::uniform_int_distribution<int> d(e.qty_min, e.qty_max);
    return d(rng);
}

void LootSystem::RollInto(const string& table_id, vector<LootDrop>& out, int depth) {
    if (depth > 4) return;                 // cycle guard
    const LootTable* t = Get(table_id);
    if (!t) return;

    for (const auto& e : t->always) {
        if (e.item.empty() || e.item == "nothing") continue;
        out.push_back({e.item, RollQty(e)});
    }

    if (t->total_weight <= 0) return;

    for (int roll = 0; roll < t->rolls; ++roll) {
        std::uniform_int_distribution<int> pick(1, t->total_weight);
        int target = pick(rng);

        for (const auto& e : t->table) {
            target -= e.weight;
            if (target > 0) continue;

            if (!e.sub_table.empty())      RollInto(e.sub_table, out, depth + 1);
            else if (!e.item.empty() && e.item != "nothing")
                                           out.push_back({e.item, RollQty(e)});
            break;
        }
    }
}

vector<LootDrop> LootSystem::Roll(const string& table_id) {
    vector<LootDrop> out;
    RollInto(table_id, out, 0);

    // Merge duplicate ids so a table that rolls coins twice yields one pile.
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = out.size(); j-- > i + 1;)
            if (out[j].item == out[i].item) {
                out[i].qty += out[j].qty;
                out.erase(out.begin() + j);
            }
    return out;
}

float LootSystem::ChanceOf(const string& table_id, const string& item_id) const {
    const LootTable* t = Get(table_id);
    if (!t) return 0.0f;

    for (const auto& e : t->always)
        if (e.item == item_id) return 1.0f;

    if (t->total_weight <= 0) return 0.0f;

    float chance = 0.0f;
    for (const auto& e : t->table) {
        const float p = static_cast<float>(e.weight) / t->total_weight;
        if (e.item == item_id)          chance += p;
        else if (!e.sub_table.empty())  chance += p * ChanceOf(e.sub_table, item_id);
    }
    return chance;
}
