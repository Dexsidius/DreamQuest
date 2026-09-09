#include "items.h"
#include "skills.h"
#include <fstream>

static const char* kSlotNames[SLOT_COUNT] = {
    "weapon", "shield", "head", "body", "legs", "amulet", "ring"
};

const char* EquipSlotName(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return "none";
    return kSlotNames[slot];
}

int EquipSlotFromName(const string& name) {
    for (int i = 0; i < SLOT_COUNT; ++i)
        if (name == kSlotNames[i]) return i;
    return SLOT_NONE;
}

bool ItemDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("ItemDatabase: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("ItemDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        ItemDef d;
        d.id          = it.key();
        d.name        = o.value("name", d.id);
        d.description = o.value("desc", string(""));
        d.stackable   = o.value("stack", false);
        d.value       = o.value("value", 1);
        d.slot        = static_cast<EquipSlot>(EquipSlotFromName(o.value("slot", string("none"))));
        d.consumable  = o.value("consume", false);
        d.heal        = o.value("heal", 0);
        d.icon        = o.value("icon", string(""));
        d.attack_speed = o.value("speed", 1.0f);

        if (o.contains("bonus")) {
            const json& b = o["bonus"];
            d.attack_bonus   = b.value("attack", 0);
            d.strength_bonus = b.value("strength", 0);
            d.defence_bonus  = b.value("defence", 0);
            d.ranged_bonus   = b.value("ranged", 0);
            d.magic_bonus    = b.value("magic", 0);
        }

        if (o.contains("req"))
            for (auto r = o["req"].begin(); r != o["req"].end(); ++r) {
                const int s = SkillFromName(r.key());
                if (s >= 0) d.requirements[s] = r.value().get<int>();
            }

        if (o.contains("cook")) {
            const json& c = o["cook"];
            d.cook_result = c.value("result", string(""));
            d.cook_xp     = c.value("xp", 0);
            d.cook_level  = c.value("level", 1);
        }

        if (o.contains("craft")) {
            const json& c = o["craft"];
            d.craft_result = c.value("result", string(""));
            d.craft_qty    = c.value("qty", 1);
            d.craft_xp     = c.value("xp", 0);
            d.craft_level  = c.value("level", 1);
            if (c.contains("inputs"))
                for (auto i = c["inputs"].begin(); i != c["inputs"].end(); ++i)
                    d.craft_inputs[i.key()] = i.value().get<int>();
        }

        // Anything you can wear or eat only makes sense one at a time.
        if (d.slot != SLOT_NONE) d.stackable = false;

        defs[d.id] = d;
    }

    SDL_Log("ItemDatabase: loaded %d items", static_cast<int>(defs.size()));
    return true;
}

const ItemDef* ItemDatabase::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

vector<const ItemDef*> ItemDatabase::Recipes() const {
    vector<const ItemDef*> out;
    for (const auto& kv : defs)
        if (!kv.second.craft_result.empty()) out.push_back(&kv.second);
    std::sort(out.begin(), out.end(), [](const ItemDef* a, const ItemDef* b) {
        if (a->craft_level != b->craft_level) return a->craft_level < b->craft_level;
        return a->name < b->name;
    });
    return out;
}

// --- Inventory ---------------------------------------------------------------

int Inventory::Add(const string& id, int qty) {
    if (id.empty() || qty <= 0) return 0;
    const ItemDef* def = db ? db->Get(id) : nullptr;
    const bool stackable = def ? def->stackable : true;

    int remaining = qty;

    if (stackable) {
        for (auto& s : items)
            if (s.id == id && s.qty > 0) { s.qty += remaining; return qty; }
        for (auto& s : items)
            if (s.Empty()) { s.id = id; s.qty = remaining; return qty; }
        return 0;
    }

    // Non-stackable items take a slot each, so a partial add is possible.
    for (auto& s : items) {
        if (remaining <= 0) break;
        if (s.Empty()) { s.id = id; s.qty = 1; --remaining; }
    }
    return qty - remaining;
}

bool Inventory::Remove(const string& id, int qty) {
    if (Count(id) < qty) return false;
    int remaining = qty;
    for (auto& s : items) {
        if (remaining <= 0) break;
        if (s.id != id) continue;
        const int take = std::min(s.qty, remaining);
        s.qty -= take;
        remaining -= take;
        if (s.qty <= 0) s.Clear();
    }
    return true;
}

bool Inventory::RemoveSlot(int slot, int qty) {
    if (slot < 0 || slot >= SlotCount() || items[slot].Empty()) return false;
    items[slot].qty -= qty;
    if (items[slot].qty <= 0) items[slot].Clear();
    return true;
}

int Inventory::Count(const string& id) const {
    int total = 0;
    for (const auto& s : items)
        if (s.id == id) total += s.qty;
    return total;
}

bool Inventory::Full() const { return FreeSlots() == 0; }

int Inventory::FreeSlots() const {
    int free = 0;
    for (const auto& s : items)
        if (s.Empty()) ++free;
    return free;
}

void Inventory::Clear() {
    for (auto& s : items) s.Clear();
}

json Inventory::ToJson() const {
    json arr = json::array();
    for (const auto& s : items) {
        if (s.Empty()) arr.push_back(nullptr);
        else           arr.push_back(json{{"id", s.id}, {"qty", s.qty}});
    }
    return arr;
}

void Inventory::FromJson(const json& j) {
    Clear();
    if (!j.is_array()) return;
    for (size_t i = 0; i < j.size() && i < items.size(); ++i) {
        if (j[i].is_null()) continue;
        items[i].id  = j[i].value("id", string(""));
        items[i].qty = j[i].value("qty", 0);
        if (items[i].qty <= 0) items[i].Clear();
    }
}

// --- Equipment ---------------------------------------------------------------

static const string kEmpty;

const string& Equipment::InSlot(int slot) const {
    if (slot < 0 || slot >= SLOT_COUNT) return kEmpty;
    return slots[slot];
}

string Equipment::Equip(int slot, const string& item_id) {
    if (slot < 0 || slot >= SLOT_COUNT) return item_id;
    const string previous = slots[slot];
    slots[slot] = item_id;
    return previous;
}

string Equipment::Unequip(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return kEmpty;
    const string previous = slots[slot];
    slots[slot].clear();
    return previous;
}

void Equipment::Clear() {
    for (auto& s : slots) s.clear();
}

int Equipment::SumBonus(int ItemDef::* field) const {
    int total = 0;
    if (!db) return total;
    for (const auto& id : slots) {
        if (id.empty()) continue;
        if (const ItemDef* d = db->Get(id)) total += d->*field;
    }
    return total;
}

int Equipment::AttackBonus() const   { return SumBonus(&ItemDef::attack_bonus); }
int Equipment::StrengthBonus() const { return SumBonus(&ItemDef::strength_bonus); }
int Equipment::DefenceBonus() const  { return SumBonus(&ItemDef::defence_bonus); }
int Equipment::RangedBonus() const   { return SumBonus(&ItemDef::ranged_bonus); }
int Equipment::MagicBonus() const    { return SumBonus(&ItemDef::magic_bonus); }

float Equipment::AttackSpeed() const {
    if (!db) return 1.0f;
    if (const ItemDef* w = db->Get(slots[SLOT_WEAPON])) return w->attack_speed;
    return 1.0f;
}

json Equipment::ToJson() const {
    json j;
    for (int i = 0; i < SLOT_COUNT; ++i)
        if (!slots[i].empty()) j[kSlotNames[i]] = slots[i];
    return j;
}

void Equipment::FromJson(const json& j) {
    Clear();
    if (!j.is_object()) return;
    for (int i = 0; i < SLOT_COUNT; ++i)
        if (j.contains(kSlotNames[i])) slots[i] = j[kSlotNames[i]].get<string>();
}
