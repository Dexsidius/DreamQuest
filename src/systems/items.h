#pragma once
#include "../headers.h"
#include "../sprite.h"

// -----------------------------------------------------------------------------
//  Items, inventory and equipment.
//
//  Item definitions are data (data/items.json) so drops, shops, quest rewards
//  and cooking recipes all refer to items by id and nothing is hard-coded.
// -----------------------------------------------------------------------------

enum EquipSlot {
    SLOT_NONE = -1,
    SLOT_WEAPON = 0, SLOT_SHIELD, SLOT_HEAD, SLOT_BODY, SLOT_HANDS,
    SLOT_LEGS, SLOT_FEET, SLOT_AMULET, SLOT_RING,
    SLOT_COUNT
};

const char* EquipSlotName(int slot);
int  EquipSlotFromName(const string& name);

// What a weapon does when you attack with it. The attack buttons are the same
// either way; the weapon decides whether the swing is a swing, a shot or a cast.
enum class WeaponKind { Melee, Bow, Staff };

// Where a recipe is made. Anything that needs metal is smithed at an anvil;
// wood, leather and thread are worked at a bench. Nothing declares its station:
// it follows from the materials, so a new recipe cannot end up in the wrong
// place.
enum class CraftStation { Workbench, Anvil };
CraftStation CraftStationFromName(const string& name);
const char*  CraftStationName(CraftStation s);
WeaponKind WeaponKindFromName(const string& name);

struct ItemDef {
    string id, name, description;
    bool   stackable = false;
    int    value = 1;                 // shop / alch value in coins
    EquipSlot slot = SLOT_NONE;

    // Equipment bonuses, applied on top of skill levels in combat maths.
    int attack_bonus = 0, strength_bonus = 0, defence_bonus = 0;
    int ranged_bonus = 0, magic_bonus = 0;
    float attack_speed = 1.0f;        // multiplier on swing time; <1 is faster
    WeaponKind kind = WeaponKind::Melee;
    // Colour the worn weapon layers take, so a bronze sword and a steel one
    // read differently on the character.
    SDL_Color tint{255, 255, 255, 255};

    map<int, int> requirements;       // SkillId -> level needed to equip

    // Consumables.
    bool consumable = false;
    int  heal = 0;

    // Cooking: raw -> cooked.
    string cook_result;
    int    cook_xp = 0, cook_level = 1;

    // A material that has to be worked hot, at an anvil.
    bool metal = false;

    // Crafting: what this turns into, at the station its materials call for.
    string craft_result;
    int    craft_qty = 1, craft_xp = 0, craft_level = 1;
    map<string, int> craft_inputs;    // item id -> quantity

    string icon;                      // image path, optional

    // Optional art drawn on the character while this is worn. Left empty for
    // items that only tint, which is everything until layered armour art
    // exists for this rig.
    bool      worn = false;
    string    worn_sprite;
    LayerSlot worn_after = LayerSlot::Head;
    SDL_FRect worn_rect{24, 17, 16, 16};   // frame pixels
    bool      worn_facings[4] = {true, true, true, true};
};

class ItemDatabase {
public:
    // Loads a file of item definitions. Later files merge over earlier ones,
    // which is how the optional armour pack adds itself without the base file
    // ever referring to art that may not be installed.
    bool Load(const string& path, bool required = true);
    const ItemDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }
    const map<string, ItemDef>& All() const { return defs; }
    // Everything craftable, for the crafting panel.
    vector<const ItemDef*> Recipes() const;
    // Only what can be made at one station.
    vector<const ItemDef*> Recipes(CraftStation station) const;
    CraftStation StationFor(const ItemDef& recipe) const;

private:
    map<string, ItemDef> defs;
};

struct ItemStack {
    string id;
    int    qty = 0;
    bool Empty() const { return id.empty() || qty <= 0; }
    void Clear() { id.clear(); qty = 0; }
};

static constexpr int INVENTORY_SLOTS = 28;

class Inventory {
public:
    explicit Inventory(const ItemDatabase* db = nullptr) : items(INVENTORY_SLOTS), db(db) {}
    void SetDatabase(const ItemDatabase* d) { db = d; }

    // Returns how many were actually added (0 when full).
    int  Add(const string& id, int qty = 1);
    bool Remove(const string& id, int qty = 1);
    bool RemoveSlot(int slot, int qty = 1);
    int  Count(const string& id) const;
    bool Has(const string& id, int qty = 1) const { return Count(id) >= qty; }
    bool Full() const;
    int  FreeSlots() const;

    const ItemStack& Slot(int i) const { return items[i]; }
    ItemStack& Slot(int i) { return items[i]; }
    int SlotCount() const { return static_cast<int>(items.size()); }
    void Clear();

    int  Coins() const { return Count("coins"); }
    bool SpendCoins(int amount) { return Remove("coins", amount); }
    void AddCoins(int amount) { Add("coins", amount); }

    json ToJson() const;
    void FromJson(const json& j);

private:
    vector<ItemStack> items;
    const ItemDatabase* db;
};

class Equipment {
public:
    explicit Equipment(const ItemDatabase* db = nullptr) : db(db) {}
    void SetDatabase(const ItemDatabase* d) { db = d; }

    const string& InSlot(int slot) const;
    // Swaps whatever was there out; returns the displaced item id (may be empty).
    string Equip(int slot, const string& item_id);
    string Unequip(int slot);
    void Clear();

    // Summed bonuses across every worn piece.
    int AttackBonus() const, StrengthBonus() const, DefenceBonus() const;
    int RangedBonus() const, MagicBonus() const;
    float AttackSpeed() const;
    // What the equipped weapon is; Melee when nothing is held.
    WeaponKind Kind() const;
    // Colour for the worn weapon layers, and for the body when armour is worn.
    SDL_Color WeaponTint() const;
    SDL_Color ArmourTint() const;
    // Worn overlays for everything currently equipped, in draw order.
    vector<Attachment> Attachments() const;

    json ToJson() const;
    void FromJson(const json& j);

private:
    int SumBonus(int ItemDef::* field) const;

    string slots[SLOT_COUNT];
    const ItemDatabase* db;
};
