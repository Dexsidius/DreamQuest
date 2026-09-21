#pragma once
#include "status.h"
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
// Rack: a tanner's frame, where hide is cut and sewn -- see StationFor.
enum class CraftStation { Workbench, Anvil, Cauldron, Range, Loom, Rack };
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
    // What a blow from it can leave: a sword's edge opens a wound some of the
    // time. See systems/status.h.
    StatusProc on_hit;

    // --- the armoury ----------------------------------------------------------------
    // Everything past sword, spear, bow and staff is told apart by these and
    // nothing else: there is no "if it is a mace" anywhere, only what a mace's
    // line in data/tiers.json says a mace is.
    //
    // What it is called among weapons: "dagger", "greataxe", "crossbow", "orb".
    // For the combo names and the HUD; empty is the plain weapon of its kind.
    string weapon_class;
    bool   two_handed = false;        // no shield with it, as with a bow
    // The share of a target's Defence its point goes past: a dagger's, a bolt's.
    float  armour_pierce = 0.0f;
    // What everything it does is worth, beside a plain weapon of its tier: a
    // wand is quick and light.
    float  damage = 1.0f;
    // A charged heavy with a clip and a shape of its own: a greataxe's chop is
    // longer, narrower and harder than its swing.
    string charge_clip;
    float  charge_damage = 1.0f, charge_reach = 1.0f, charge_sweep = 1.0f;
    // A crossbow: let off at once, and then this long spanning it again.
    float  reload = 0.0f;
    // What it throws, where that is not an arrow or a spell: a bolt, a knife.
    string shoots;
    // A caster's: the share of a spell's mana it asks, and how hard what it
    // throws turns after its target.
    float  mana_mult = 1.0f, homing = 0.0f;
    // The four combos, as this weapon makes them: what each is called, and what
    // is different about it -- a status it always leaves, armour it goes past,
    // what it is worth beside the sword's. In the order of ComboMove: crush,
    // cleave, backhand, cross cut. A name left empty is the sword's.
    struct ComboTwist {
        string name;
        Status status = Status::COUNT;
        float  pierce = 0.0f, damage = 1.0f;
    };
    ComboTwist combos[4];

    // A staff given over to one element: 1 to 4 choose that element's four
    // spells instead of the four elements. None for a staff that is not.
    Element element = Element::None;
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

    // A shield's block: the share of a blow it turns aside, and the multiplier
    // on the stamina that costs. Zero block is anything worn in the off hand
    // that is not a shield -- a lantern is not something to stop a sword with.
    float block = 0.0f;
    float block_stamina = 1.0f;
    // How much quicker the wearer walks, as a fraction: hide boots are 0.05,
    // a twentieth. Summed over everything worn.
    float move_speed = 0.0f;
    // Cannot be dropped from the bag: keys, letters, seals -- anything a
    // quest handed over exactly once and could not hand over again.
    bool  keep = false;
    // An enchanted piece: which enchantment it carries, and the plain piece
    // it was worked into. Both empty on everything else. See EnchantDef.
    string enchant, base_item;

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
    // What one of those is worth to somebody at this level. Written once
    // because two places need it and they must not drift: the panel that says
    // what a potion will do, and the draught that does it.
    static int BoostGain(const pair<int, float>& boost, int level) {
        return boost.first + static_cast<int>(level * boost.second);
    }
    // A recipe scroll: using it teaches the brew with this id.
    string learn;
    // Where the recipe for this brew is learned, for the cauldron to say.
    string recipe_from;

    // Cooking: raw -> cooked.
    string cook_result;
    int    cook_xp = 0, cook_level = 1;

    // --- a dish -------------------------------------------------------------------
    // A cooked thing that is worth more than the hit points in it: eat it and
    // it sits with you for a while. Max health, mana and breath are shares of
    // what they already are -- a tenth more of a big pool is worth more than a
    // tenth more of a small one, which is what makes a good dinner worth
    // cooking at fifty and not only at five -- and the levels are the flat
    // amounts a potion gives, held steady until the meal wears off rather than
    // draining a point at a time.
    //
    // One meal at a time: a second dish replaces the first, whatever was left
    // of it. See Player::Eat and Player::Meal.
    float dish_minutes = 0.0f;
    float dish_max_hp = 0.0f, dish_max_mana = 0.0f, dish_max_stamina = 0.0f;
    map<int, int> dish_levels;        // SkillId -> levels, for as long as it lasts
    bool  IsDish() const { return dish_minutes > 0.0f; }

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
    // For something with `use` "bag": how many slots putting it on adds.
    int    bag_slots = 0;

    // A material that has to be worked hot, at an anvil.
    bool metal = false;
    // Brewed at a cauldron, but nobody has to be shown how: a dye is a herb
    // boiled in water, and every other brew is a recipe somebody guards.
    bool untaught = false;

    // Crafting: what this turns into, at the station its materials call for.
    string craft_result;
    int    craft_qty = 1, craft_xp = 0, craft_level = 1;
    // Where it is made, when it is not decided by what goes into it: "range"
    // for anything cooked at a fire. Empty leaves it to ItemDatabase::StationFor.
    string craft_at;
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

// An enchantment: a charm worked into a worn piece at an enchanting table,
// with Magic, from data/enchantments.json. Every piece it fits gets an
// enchanted twin built at load -- "copper_ring+keenness", the Copper Ring of
// Keenness -- so an enchanted ring is an item like any other: carried, worn,
// sold and saved by its id, and nothing else in the game needs to know.
struct EnchantDef {
    string id, name, suffix, text;      // "Keenness", "of Keenness", what it does
    vector<EquipSlot> slots;            // what it can be worked into
    int   level = 1;                    // Magic level to work it
    int   xp = 0;                       // Magic XP for working it
    int   value = 0;                    // added to the piece's worth
    int   attack_bonus = 0, strength_bonus = 0, defence_bonus = 0;
    int   ranged_bonus = 0, magic_bonus = 0;
    float move_speed = 0.0f;
    map<string, int> inputs;            // item id -> quantity
    string from;                        // where it is learned, for the table to say
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

    // --- enchantments -------------------------------------------------------------
    // Reads data/enchantments.json and builds the enchanted twin of every
    // piece each one fits. Call after every item file and the tiers, so the
    // twins are built from everything there is.
    bool LoadEnchantments(const string& path);
    // Every enchantment, cheapest first.
    vector<const EnchantDef*> Enchantments() const;
    const EnchantDef* Enchantment(const string& id) const;
    // Whether this piece can take this enchantment: it goes in a slot the
    // enchantment fits, it carries none yet, and an off-hand piece is a
    // shield rather than a lantern.
    bool Takes(const ItemDef& piece, const EnchantDef& e) const;
    // The enchanted twin's id, "<piece>+<enchantment>", or empty if there is none.
    string EnchantedId(const string& piece, const string& enchant) const;

private:
    map<string, ItemDef> defs;
    // Recipes that are not an item's own: one ingot makes seven things, and an
    // item can only carry one "craft".
    vector<ItemDef> recipes;
    // And the odd extra recipe an item file lists under "crafts", for when a
    // material already carries its one "craft": hide makes a jerkin, and boots.
    vector<ItemDef> data_recipes;
    vector<TierDef> tiers;
    vector<EnchantDef> enchants;
    map<string, string> tier_pieces;       // "iron/sword" -> "iron_sword"
};

struct ItemStack {
    string id;
    int    qty = 0;
    bool Empty() const { return id.empty() || qty <= 0; }
    void Clear() { id.clear(); qty = 0; }
};

static constexpr int INVENTORY_SLOTS = 28;
// A bag put on adds a row to that, and there are four of them to find or make:
// eight rows of seven is as much as the inventory screen has room to show.
static constexpr int BAG_ROW = 7;
static constexpr int MAX_INVENTORY_SLOTS = INVENTORY_SLOTS + 4 * BAG_ROW;

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

// One line of an item's stat block: what the stat is, what this piece gives,
// and -- when it is being weighed against something already worn -- what that
// would change. Built here rather than in the panel that draws it, because
// three panels draw it and they must not disagree about which numbers an item
// has. `verdict` is +1 for a change for the better, -1 for worse and 0 for no
// change or no comparison; the colour that goes with it is the UI's business.
struct ItemStat {
    string label, value, delta;
    int    verdict = 0;
};

// `worn` is whatever is in the same slot -- possibly this very piece, in which
// case every change is nothing, which is the honest answer. `compare` is false
// for anything with no slot of its own: a potion is not instead of anything.
vector<ItemStat> ItemStatLines(const ItemDef& d, const ItemDef* worn, bool compare);

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
    // How much quicker everything worn makes the wearer walk, summed: 0.05
    // for hide boots alone.
    float MoveSpeed() const;
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

// Working an enchantment into a piece from the bag. The panel at the table
// and the self-test both come through here, so what it costs and what it
// makes are decided once.
namespace Enchanting {
// The bag slots holding something this enchantment could be worked into,
// in bag order.
vector<int> Targets(const ItemDatabase& db, const EnchantDef& e, const Inventory& bag);
// Works it into the piece in `slot`: takes the materials and the piece, and
// puts the enchanted piece back. Says why it could not, otherwise. Whether
// the enchantment is known and whether the Magic level is enough are the
// caller's to check: neither lives in a bag.
bool Work(const ItemDatabase& db, const EnchantDef& e, Inventory& bag, int slot, string& why);
}
