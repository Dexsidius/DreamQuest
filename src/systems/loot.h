#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  Loot tables.
//
//  Every enemy and chest names a table in data/loot_tables.json. A table has
//  guaranteed drops plus a number of weighted rolls; a roll can point at
//  another table, which is how rare-drop tables hang off common ones.
//
//      "orc_warrior": {
//        "always": [{"item": "bones"}],
//        "rolls": 1,
//        "table": [
//          {"item": "coins", "qty": [15, 60], "weight": 45},
//          {"item": "nothing",               "weight": 30},
//          {"table": "rare_drops",           "weight": 3}
//        ]
//      }
// -----------------------------------------------------------------------------

struct LootEntry {
    string item;         // item id; "nothing" or empty rolls a blank
    string sub_table;    // when set, roll that table instead
    int    qty_min = 1, qty_max = 1;
    int    weight  = 1;
};

struct LootTable {
    string id;
    vector<LootEntry> always;
    vector<LootEntry> table;
    int rolls = 1;
    int total_weight = 0;
};

struct LootDrop {
    string item;
    int    qty = 1;
};

class LootSystem {
public:
    LootSystem();
    // A later file overrides tables of the same name, so optional content can
    // be layered on top. Set required to false for a file that may not exist.
    bool Load(const string& path, bool required = true);

    // Rolls a table into concrete drops. Unknown tables roll nothing.
    vector<LootDrop> Roll(const string& table_id);

    const LootTable* Get(const string& id) const;
    bool Has(const string& id) const { return tables.count(id) > 0; }

    // Shown on the drop-rate line in the bestiary panel.
    float ChanceOf(const string& table_id, const string& item_id) const;

    void Seed(unsigned int seed) { rng.seed(seed); }

private:
    // depth guards against a table that points back at itself.
    void RollInto(const string& table_id, vector<LootDrop>& out, int depth);
    int  RollQty(const LootEntry& e);

    map<string, LootTable> tables;
    std::mt19937 rng;
};
