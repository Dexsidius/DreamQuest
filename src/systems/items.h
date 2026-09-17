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
//
// A cauldron brews: a recipe with a herb or a vial in it is a potion. What is
// made at each station trains its own skill -- Crafting at a workbench,
// Smithing at an anvil, Brewing at a cauldron -- and a potion has to be learned
// before it can be brewed.
enum class CraftStation { Workbench, Anvil, Cauldron };
CraftStation CraftStationFromName(const string& name);
const char*  CraftStationName(CraftStation s);
int          CraftSkill(CraftStation s);
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
    // A melee weapon's shape, as multipliers on each attack's own: how far its
    // strikes reach, how wide they sweep, and how hard they shove. A sword is
    // all ones. A spear reaches much further down a narrow line and pushes what
    // it hits back, so a fight stays at the end of the shaft.
    float reach = 1.0f, sweep = 1.0f, push = 1.0f;
    // The hero's clip for its strikes: "thrust" for a spear. Empty is the
    // ordinary swing.
    string attack_clip;
    // How far a worn light throws, in world pixels; 0 for everything that is
    // not a lamp. A lantern in the off hand is the only way to see down a
    // well, so the light is a property of the item rather than of the player.
    float light_radius = 0.0f;
    // What this becomes when a firestarter is used on it: an unlit lantern
    // names the lit one. Empty for everything that does not catch.
    string lights;
    // A recipe nobody can make until they have been shown how, whatever the
    // station. Brews have always worked this way; this puts the same lock on
    // anything else that is taught rather than worked out.
    bool  needs_recipe = false;
    // A named effect the item has while it is worn, and the line the journal
    // and the bag print for it. One legendary piece can do something no stat
    // block can say; everything that reads it asks for it by name.
    string passive, passive_text;
    // Colour the worn weapon layers take, so a bronze sword and a steel one
    // read differently on the character.
    SDL_Color tint{255, 255, 255, 255};
    // Which of the character's armour layers this piece paints -- "body",
    // "legs", "head", "hands" or "shield" -- or empty for anything with no
    // plate of its own. The layer is rendered once in pale steel and painted
    // with `tint`, so every tier is the same plate in its own metal and a
    // mismatched set draws as the mismatch it is.
    string armour_layer;
    // And which cut of it: "light" for the early tiers' hide and mail,
    // "ornate" for the last tiers' horned harness, empty for plain plate.
    string armour_cut;

    map<int, int> requirements;       // SkillId -> level needed to equip

    // Consumables.
    bool consumable = false;
    int  heal = 0;
    // Potions, on top of healing: mana back, a full breath of stamina, and
    // boosts to combat levels -- a flat amount plus a share of the level, the
    // OSRS way, wearing off a point at a time.
    int  mana = 0;
    bool stamina = false;
    map<int, pair<int, float>> boosts;   // SkillId -> (flat, fraction of level)
    // A recipe scroll: using it teaches the brew with this id.
    string learn;
    // Where the recipe for this brew is learned, for the cauldron to say.
    string recipe_from;

    // Cooking: raw -> cooked.
    string cook_result;
    int    cook_xp = 0, cook_level = 1;

    // A gathering tool: "axe", "pickaxe" or "rod", and how much faster than a
    // basic one it works.
    string tool;
    float  tool_speed = 1.0f;
    // A fish: the Fishing level it bites at and the XP it is worth.
    int    fish_level = 0;
    int    fish_xp = 0;
    // A herb: the Foraging level it is picked at, its XP, and where it grows
    // best ("meadow", "waterside", "woodland", "mire", "foothills", "deep
    // forest", "cursed", "reverie").
    int    forage_level = 0;
    int    forage_xp = 0;
    string grows;

    // What using it from the bag does, beyond eating and wearing: "camp"
    // pitches a camp where the player stands.
    string use;

    // A material that has to be worked hot, at an anvil.
    bool metal = false;

    // Crafting: what this turns into, at the station its materials call for.
    string craft_result;
    int    craft_qty = 1, craft_xp = 0, craft_level = 1;
    map<string, int> craft_inputs;    // item id -> quantity

    string icon;                      // image path, optional
    // What kind of thing it is to a trader, beyond what its fields already say:
    // "wood", "leather", "gem", "dream". See Trade::Tags.
    vector<string> tags;

    // --- material tiers ---------------------------------------------------------
    // Set on everything data/tiers.json made: which tier, which of the seven
    // pieces (or "ore" / "bar"), and for a weapon the model the hero carries.
    string tier;
    int    tier_index = -1;           // 0 wood .. 8 demonrite
    string piece;
    // The weapon art the hero draws in hand, "sword_iron" and so on. Empty
    // falls back to the rig's own sword layer, tinted.
    string model;

    // Optional art drawn on the character while this is worn. Left empty for
    // items that only tint, which is everything until layered armour art
    // exists for this rig.
    bool      worn = false;
    string    worn_sprite;
    LayerSlot worn_after = LayerSlot::Head;
    SDL_FRect worn_rect{24, 17, 16, 16};   // frame pixels
    bool      worn_facings[4] = {true, true, true, true};
};

// One material tier, in order from wood to demonrite.
struct TierDef {
    string id, name;
    int    level = 1;              // what wearing or wielding it needs
    SDL_Color colour{255, 255, 255, 255};
    bool   wood = false;           // worked from logs, with no ore or bar
    string ore, bar;               // item ids; empty for wood
    int    mining = 1;             // Mining level to work the ore
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

    // Builds every tier's ore, bar and seven pieces, and a recipe for each,
    // from data/tiers.json. Call after the item files: an item that already
    // exists (copper ore, say) keeps what it had and gains its tier.
    bool LoadTiers(const string& path);
    const vector<TierDef>& Tiers() const { return tiers; }
    const TierDef* Tier(const string& id) const;
    // The item a tier makes as a piece: "sword", "helm", ...; empty if none.
    string TierPiece(const string& tier_id, const string& piece) const;

    // Making something is worth doing: whatever a recipe makes is worth at
    // least this many times what went into it, so a sword sells for more than
    // its bars would have. LoadTiers settles it once every recipe is known.
    static constexpr float CRAFT_VALUE_ADD = 1.8f;
    void SettleCraftValues();
    // What a recipe's materials are worth, in coins.
    int  InputValue(const ItemDef& recipe) const;

private:
    map<string, ItemDef> defs;
    // Recipes that are not an item's own: one ingot makes seven things, and an
    // item can only carry one "craft".
    vector<ItemDef> recipes;
    vector<TierDef> tiers;
    map<string, string> tier_pieces;       // "iron/sword" -> "iron_sword"
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
    explicit Inventory(const ItemDatabase* db = nullptr, int slots = INVENTORY_SLOTS)
        : items(std::max(1, slots)), db(db) {}
    void SetDatabase(const ItemDatabase* d) { db = d; }
    // Grow or shrink the bag. Anything in a slot that is being cut away is
    // gathered back into what is left, so a chest whose capacity was reduced
    // in the data does not quietly eat what was in it.
    void Resize(int slots);

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
    // The weapon in hand, or null.
    const ItemDef* Weapon() const;
    // True when something worn carries this passive.
    bool HasPassive(const string& id) const;
    // The furthest a worn light throws; 0 when nothing worn is lit.
    float LightRadius() const;
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
