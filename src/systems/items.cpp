#include "items.h"
#include "skills.h"
#include <fstream>

static const char* kSlotNames[SLOT_COUNT] = {
    "weapon", "shield", "head", "body", "hands", "legs", "feet", "amulet", "ring"
};

const char* EquipSlotName(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return "none";
    return kSlotNames[slot];
}

// What a weapon's line says about itself, in items.json and in a piece of
// tiers.json alike.
static void ReadArmoury(const json& o, ItemDef& d) {
    d.weapon_class  = o.value("class", string(""));
    d.two_handed    = o.value("two_handed", false);
    d.leech         = o.value("leech", 0.0f);
    d.thrown        = o.value("thrown", false);
    d.armour_pierce = std::clamp(o.value("armour_pierce", 0.0f), 0.0f, 0.9f);
    d.damage        = o.value("damage", 1.0f);
    d.charge_clip   = o.value("charge_clip", string(""));
    if (o.contains("charge") && o["charge"].is_object()) {
        d.charge_damage = o["charge"].value("damage", 1.0f);
        d.charge_reach  = o["charge"].value("reach", 1.0f);
        d.charge_sweep  = o["charge"].value("sweep", 1.0f);
    }
    // "dual": a second one can be held in the other hand, and what a pair is worth.
    if (o.contains("dual") && o["dual"].is_object()) {
        d.offhand      = true;
        d.dual_speed   = std::clamp(o["dual"].value("speed", 0.5f), 0.25f, 1.0f);
        d.dual_damage  = std::clamp(o["dual"].value("damage", 1.0f), 0.1f, 2.0f);
        d.offhand_clip = o["dual"].value("clip", string(""));
    }
    d.reload    = o.value("reload", 0.0f);
    d.shoots    = o.value("shoots", string(""));
    d.mana_mult = o.value("mana", 1.0f);
    d.homing    = o.value("homing", 0.0f);
    d.element   = ElementFromName(o.value("element", string("none")));
    // "spells": { "fire": [1, 2, 4], ... } -- which of each element's four this
    // weapon reaches. See ItemDef::spell_slots.
    if (o.contains("spells") && o["spells"].is_object()) {
        static const char* kElements[4] = {"fire", "water", "earth", "air"};
        for (int i = 0; i < 4; ++i) {
            if (!o["spells"].contains(kElements[i])) continue;
            d.spell_slots[i].clear();
            for (const auto& n : o["spells"][kElements[i]]) {
                const int slot = n.get<int>();
                if (slot >= 1 && slot <= 4) d.spell_slots[i].push_back(slot);
            }
        }
    }
    if (o.contains("combos") && o["combos"].is_object()) {
        static const char* kMoves[4] = {"crush", "cleave", "backhand", "cross_cut"};
        for (int i = 0; i < 4; ++i) {
            if (!o["combos"].contains(kMoves[i])) continue;
            const json& c = o["combos"][kMoves[i]];
            d.combos[i].name   = c.value("name", string(""));
            d.combos[i].status = StatusFromId(c.value("status", string("")));
            d.combos[i].pierce = std::clamp(c.value("pierce", 0.0f), 0.0f, 0.9f);
            d.combos[i].damage = c.value("damage", 1.0f);
        }
    }
}

bool ItemDef::FitsSlot(int equip_slot) const {
    if (equip_slot == SLOT_NONE) return false;
    return equip_slot == slot || (equip_slot == SLOT_SHIELD && slot == SLOT_WEAPON && offhand);
}

WeaponKind WeaponKindFromName(const string& name) {
    if (name == "bow")   return WeaponKind::Bow;
    if (name == "staff") return WeaponKind::Staff;
    return WeaponKind::Melee;
}

int EquipSlotFromName(const string& name) {
    for (int i = 0; i < SLOT_COUNT; ++i)
        if (name == kSlotNames[i]) return i;
    return SLOT_NONE;
}

bool ItemDatabase::Load(const string& path, bool required) {
    std::ifstream in(path);
    if (!in) {
        if (required) SDL_Log("ItemDatabase: cannot open '%s'", path.c_str());
        return !required;
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
        d.mana        = o.value("mana", 0);
        d.stamina     = o.value("stamina", false);
        d.learn       = o.value("learn", string(""));
        d.recipe_from = o.value("recipe_from", string(""));
        if (o.contains("boost"))
            for (auto b = o["boost"].begin(); b != o["boost"].end(); ++b) {
                const int s = SkillFromName(b.key());
                if (s < 0 || !b.value().is_array() || b.value().size() < 2) continue;
                d.boosts[s] = {b.value()[0].get<int>(), b.value()[1].get<float>()};
            }
        d.icon        = o.value("icon", string(""));
        d.attack_speed = o.value("speed", 1.0f);
        d.kind        = WeaponKindFromName(o.value("kind", string("melee")));
        d.reach       = o.value("reach", 1.0f);
        d.sweep       = o.value("sweep", 1.0f);
        d.push        = o.value("push", 1.0f);
        if (o.contains("on_hit")) d.on_hit = StatusProcFromJson(o["on_hit"]);
        d.attack_clip = o.value("clip", string(""));
        ReadArmoury(o, d);
        d.light_radius = o.value("light", 0.0f);
        d.lights       = o.value("lights", string(""));
        d.needs_recipe = o.value("needs_recipe", false);
        d.passive     = o.value("passive", string(""));
        d.passive_text = o.value("passive_text", string(""));
        // A shield from a data file rather than a tier says how well it blocks
        // itself; anything that does not say is not a shield.
        d.block         = o.value("block", 0.0f);
        d.block_stamina = o.value("block_stamina", 1.0f);
        d.move_speed    = o.value("move_speed", 0.0f);
        d.keep          = o.value("keep", false);

        if (o.contains("tint")) {
            const json& t = o["tint"];
            if (t.is_array() && t.size() >= 3)
                d.tint = {static_cast<Uint8>(t[0].get<int>()),
                          static_cast<Uint8>(t[1].get<int>()),
                          static_cast<Uint8>(t[2].get<int>()), 255};
        }

        if (o.contains("worn")) {
            const json& w = o["worn"];
            d.worn        = true;
            d.worn_sprite = w.value("sprite", d.icon);
            d.worn_after  = LayerSlotFromName(w.value("after", string("head")));
            if (w.contains("rect")) {
                const json& rr = w["rect"];
                if (rr.is_array() && rr.size() >= 4)
                    d.worn_rect = {rr[0].get<float>(), rr[1].get<float>(),
                                   rr[2].get<float>(), rr[3].get<float>()};
            }
            if (w.contains("facings")) {
                const json& f = w["facings"];
                for (size_t i = 0; i < 4 && i < f.size(); ++i)
                    d.worn_facings[i] = f[i].get<bool>();
            }
        }

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
        if (o.contains("dish")) {
            const json& j = o["dish"];
            d.dish_minutes     = j.value("minutes", 0.0f);
            d.dish_max_hp      = j.value("max_hp", 0.0f);
            d.dish_max_mana    = j.value("max_mana", 0.0f);
            d.dish_max_stamina = j.value("max_stamina", 0.0f);
            if (j.contains("levels"))
                for (auto b = j["levels"].begin(); b != j["levels"].end(); ++b) {
                    const int s = SkillFromName(b.key());
                    if (s >= 0) d.dish_levels[s] = b.value().get<int>();
                }
        }

        d.metal = o.value("metal", false);
        if (o.contains("tags"))
            for (const json& t : o["tags"]) d.tags.push_back(t.get<string>());
        d.use   = o.value("use", string(""));
        d.bag_slots = o.value("bag_slots", 0);
        d.model = o.value("model", string(""));
        d.tool  = o.value("tool", string(""));
        d.tool_speed = o.value("tool_speed", 1.0f);
        if (o.contains("forage")) {
            d.forage_level = o["forage"].value("level", 1);
            d.forage_xp    = o["forage"].value("xp", 10);
            d.grows        = o["forage"].value("grows", string(""));
        }
        if (o.contains("fish")) {
            d.fish_level = o["fish"].value("level", 1);
            d.fish_xp    = o["fish"].value("xp", 10);
        }

        const auto read_craft = [](ItemDef& into, const json& c) {
            into.craft_result = c.value("result", string(""));
            into.craft_qty    = c.value("qty", 1);
            into.craft_xp     = c.value("xp", 0);
            into.craft_level  = c.value("level", 1);
            into.craft_at     = c.value("station", string(""));
            into.craft_inputs.clear();
            if (c.contains("inputs"))
                for (auto i = c["inputs"].begin(); i != c["inputs"].end(); ++i)
                    into.craft_inputs[i.key()] = i.value().get<int>();
        };
        if (o.contains("craft")) read_craft(d, o["craft"]);
        // A material that makes more than one thing lists the rest under
        // "crafts"; each is a recipe of its own, filed like a tier's.
        if (o.contains("crafts") && o["crafts"].is_array())
            for (const json& c : o["crafts"]) {
                ItemDef r;
                read_craft(r, c);
                if (r.craft_result.empty()) continue;
                r.id = "recipe_" + r.craft_result;
                r.name = r.craft_result;
                data_recipes.erase(std::remove_if(data_recipes.begin(), data_recipes.end(),
                                                  [&](const ItemDef& x) { return x.id == r.id; }),
                                   data_recipes.end());
                data_recipes.push_back(r);
            }

        // Anything you can wear or eat only makes sense one at a time.
        if (d.slot != SLOT_NONE) d.stackable = false;

        defs[d.id] = d;
    }

    // What a fire can do with one raw thing, as a recipe, so the menu at a
    // fire lists the plain cooking beside the dishes rather than the player
    // having to know that pressing the button somewhere cooks whatever is
    // nearest the top of the bag.
    for (const auto& kv : defs) {
        const ItemDef& raw = kv.second;
        if (raw.cook_result.empty() || !defs.count(raw.cook_result)) continue;
        ItemDef r;
        r.id = "recipe_" + raw.cook_result;
        r.name = raw.cook_result;
        r.craft_result = raw.cook_result;
        r.craft_qty = 1;
        r.craft_xp = raw.cook_xp;
        r.craft_level = raw.cook_level;
        r.craft_at = "range";
        r.craft_inputs[raw.id] = 1;
        data_recipes.erase(std::remove_if(data_recipes.begin(), data_recipes.end(),
                                          [&](const ItemDef& x) { return x.id == r.id; }),
                           data_recipes.end());
        data_recipes.push_back(r);
    }

    SDL_Log("ItemDatabase: loaded %d items (%s)",
            static_cast<int>(defs.size()), path.c_str());
    return true;
}

const ItemDef* ItemDatabase::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

// --- material tiers -----------------------------------------------------------

const TierDef* ItemDatabase::Tier(const string& id) const {
    for (const TierDef& t : tiers) if (t.id == id) return &t;
    return nullptr;
}

string ItemDatabase::TierPiece(const string& tier_id, const string& piece) const {
    auto it = tier_pieces.find(tier_id + "/" + piece);
    return it == tier_pieces.end() ? string() : it->second;
}

bool ItemDatabase::LoadTiers(const string& path) {
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
    if (!root.contains("pieces") || !root.contains("tiers")) return false;

    tiers.clear();
    recipes.clear();
    tier_pieces.clear();

    // Pieces in a fixed order, so the recipe list reads the same every time.
    static const char* kPieces[] = {"sword", "spear", "bow", "staff", "shield", "helm", "body", "legs",
                                    "axe", "pickaxe",
                                    // The armoury: see README.
                                    "dagger", "mace", "greatsword", "greataxe", "crossbow", "knives",
                                    "wand", "grimoire", "orb",
                                    "fire_staff", "water_staff", "earth_staff", "air_staff"};
    const json& pieces = root["pieces"];

    const auto icon_for = [](const string& file) { return "assets/icons/tiers/" + file + ".png"; };
    const auto add_recipe = [&](const string& result, int level, int xp, const map<string, int>& inputs) {
        ItemDef r;
        r.id = "recipe_" + result;
        r.craft_result = result;
        r.craft_level = std::clamp(level, 1, 99);
        r.craft_xp = xp;
        r.craft_inputs = inputs;
        recipes.push_back(r);
    };

    int index = 0;
    for (const json& tj : root["tiers"]) {
        TierDef t;
        t.id     = tj.value("id", string(""));
        t.name   = tj.value("name", t.id);
        t.level  = tj.value("level", 1);
        t.wood   = tj.value("wood", false);
        t.ore    = tj.value("ore", string(""));
        t.bar    = tj.value("bar", string(""));
        t.mining = tj.value("mining", 1);
        if (tj.contains("colour") && tj["colour"].size() >= 3)
            t.colour = {static_cast<Uint8>(tj["colour"][0].get<int>()),
                        static_cast<Uint8>(tj["colour"][1].get<int>()),
                        static_cast<Uint8>(tj["colour"][2].get<int>()), 255};
        const int value_mult = tj.value("value", 1);
        const string flavour = tj.value("flavour", string(""));

        // --- the ore and the bar ------------------------------------------------
        if (!t.wood && !t.ore.empty()) {
            ItemDef& ore = defs[t.ore];
            if (ore.id.empty()) {
                ore.id = t.ore;
                ore.value = 6 * value_mult;
            }
            ore.name = tj.value("ore_name", ore.name.empty() ? t.ore : ore.name);
            ore.description = tj.value("ore_desc", ore.description);
            ore.stackable = true;
            ore.metal = true;
            ore.icon = icon_for(t.ore);
            ore.tier = t.id;
            ore.tier_index = index;
            ore.piece = "ore";
        }
        if (!t.wood && !t.bar.empty()) {
            ItemDef bar;
            bar.id = t.bar;
            bar.name = tj.value("bar_name", t.bar);
            bar.description = "A bar of " + t.name + ", ready for the anvil.";
            bar.stackable = true;
            bar.metal = true;
            bar.value = 16 * value_mult;
            bar.icon = icon_for(t.bar);
            bar.tier = t.id;
            bar.tier_index = index;
            bar.piece = "bar";
            defs[bar.id] = bar;

            map<string, int> smelt;
            if (tj.contains("smelt"))
                for (auto i = tj["smelt"].begin(); i != tj["smelt"].end(); ++i)
                    smelt[i.key()] = i.value().get<int>();
            add_recipe(bar.id, t.level, 10 + index * 12, smelt);
        }

        // --- the seven pieces ----------------------------------------------------
        for (const char* piece_name : kPieces) {
            if (!pieces.contains(piece_name)) continue;
            const json& pj = pieces[piece_name];
            const string piece = piece_name;

            ItemDef d;
            d.id = (tj.contains("ids") && tj["ids"].contains(piece))
                       ? tj["ids"][piece].get<string>() : (t.id + "_" + piece);
            d.name = (tj.contains("names") && tj["names"].contains(piece))
                         ? tj["names"][piece].get<string>()
                         : (t.name + " " + pj.value("noun", piece));
            d.description = flavour.empty() ? pj.value("desc", string(""))
                                            : flavour + " " + pj.value("desc", string(""));
            d.slot = static_cast<EquipSlot>(EquipSlotFromName(pj.value("slot", string("none"))));
            d.kind = WeaponKindFromName(pj.value("kind", string("melee")));
            d.attack_speed = pj.value("speed", 1.0f);
            d.reach = pj.value("reach", 1.0f);
            d.sweep = pj.value("sweep", 1.0f);
            d.push = pj.value("push", 1.0f);
            if (pj.contains("on_hit")) d.on_hit = StatusProcFromJson(pj["on_hit"]);
            d.attack_clip = pj.value("clip", string(""));
            ReadArmoury(pj, d);
            d.value = pj.value("value", 10) * value_mult;
            d.icon = icon_for(piece + "_" + t.id);
            d.tier = t.id;
            d.tier_index = index;
            d.piece = piece;

            const bool weapon = d.slot == SLOT_WEAPON;
            d.tool = pj.value("tool", string(""));
            if (!d.tool.empty()) d.tool_speed = tj.value("tool_speed", 1.0f);
            if (weapon || !d.tool.empty()) d.model = pj.value("model", piece) + "_" + t.id;
            if (weapon && pj.contains("tint") && pj["tint"].is_array() && pj["tint"].size() >= 3)
                d.tint = {static_cast<Uint8>(pj["tint"][0].get<int>()), static_cast<Uint8>(pj["tint"][1].get<int>()),
                          static_cast<Uint8>(pj["tint"][2].get<int>()), 255};
            // A weapon's model is drawn in its own colours; a piece of plate is
            // drawn from the character's own armour sheet for that slot, in the
            // tier's metal.
            if (!weapon && d.tool.empty()) {
                d.tint = t.colour;
                d.armour_layer = pj.value("layer", string(""));
                // Which cut of armour this tier wears. Plate is what the sheets
                // are named after, so it is the one that needs no suffix.
                d.armour_cut = tj.value("cut", string(""));
                if (d.armour_cut == "plate") d.armour_cut.clear();
                // Only the shield blocks, and how well is its tier's.
                if (d.slot == SLOT_SHIELD) {
                    d.block         = tj.value("block", 0.5f);
                    d.block_stamina = tj.value("block_stamina", 1.0f);
                }
            }

            const float power = pj.value("power", string("weapon")) == "armour"
                                    ? tj.value("armour_power", 10.0f)
                                    : tj.value("weapon_power", 10.0f);
            if (pj.contains("bonus")) {
                const json& b = pj["bonus"];
                const auto scaled = [&](const char* k) {
                    const float f = b.value(k, 0.0f);
                    return f > 0.0f ? std::max(1, static_cast<int>(std::lround(power * f))) : 0;
                };
                d.attack_bonus   = scaled("attack");
                d.strength_bonus = scaled("strength");
                d.defence_bonus  = scaled("defence");
                d.ranged_bonus   = scaled("ranged");
                d.magic_bonus    = scaled("magic");
            }
            // Plate is the melee set: what a piece adds to a blow, as a share of
            // the tier's weapon power rather than its armour's. Hides do the
            // same for a bow and robes for a staff; see "sets" below.
            if (pj.contains("style_bonus")) {
                const json& b = pj["style_bonus"];
                const float wp = tj.value("weapon_power", 10.0f);
                const auto share = [&](const char* k) {
                    const float f = b.value(k, 0.0f);
                    return f > 0.0f ? std::max(1, static_cast<int>(std::lround(wp * f))) : 0;
                };
                d.attack_bonus   += share("attack");
                d.strength_bonus += share("strength");
            }
            if (t.level > 1) {
                const int s = SkillFromName(pj.value("skill", string("")));
                if (s >= 0) d.requirements[s] = t.level;
            }
            defs[d.id] = d;
            tier_pieces[t.id + "/" + piece] = d.id;

            // How it is made.
            map<string, int> inputs;
            int amount = 0;
            if (t.wood) {
                const json* from = (tj.contains("wood_inputs") && tj["wood_inputs"].contains(piece)) ? &tj["wood_inputs"][piece]
                                 : pj.contains("wood") ? &pj["wood"] : nullptr;
                if (from)
                    for (auto i = from->begin(); i != from->end(); ++i) {
                        inputs[i.key()] = i.value().get<int>();
                        amount += i.value().get<int>();
                    }
            } else if (!t.bar.empty()) {
                amount = pj.value("bars", 1);
                inputs[t.bar] = amount;
                if (pj.contains("extra"))
                    for (auto i = pj["extra"].begin(); i != pj["extra"].end(); ++i)
                        inputs[i.key()] += i.value().get<int>();
            }
            if (!inputs.empty())
                add_recipe(d.id, t.wood ? t.level + pj.value("craft_offset", 0) : t.level,
                           (12 + index * 14) * std::max(1, amount), inputs);
        }

        // --- the sets that are not metal -----------------------------------------
        // The ranger's hides and the mage's robes: head, body and legs for every
        // tier, drawn in their own cut, needing the tier's level in the set's
        // skill and adding only to that style. Hides are cut from the tier's own
        // hide; robes from bolts of cloth and the tier's dye, which is brewed.
        if (root.contains("sets")) {
            static const char* kSetPieces[] = {"head", "body", "legs"};
            for (auto set_it = root["sets"].begin(); set_it != root["sets"].end(); ++set_it) {
                const string set_id = set_it.key();
                const json& sj = set_it.value();
                if (!sj.is_object() || !sj.contains("tiers") || !sj["tiers"].contains(t.id)) continue;
                const json& st = sj["tiers"][t.id];
                const string style = sj.value("style", string(""));
                const int skill = SkillFromName(sj.value("skill", string("")));
                SDL_Color colour = t.colour;
                if (st.contains("colour") && st["colour"].size() >= 3)
                    colour = {static_cast<Uint8>(st["colour"][0].get<int>()),
                              static_cast<Uint8>(st["colour"][1].get<int>()),
                              static_cast<Uint8>(st["colour"][2].get<int>()), 255};

                // The dye, where the set has one: an item and the brew that makes it.
                string dye_id;
                if (st.contains("dye")) {
                    const json& dj = st["dye"];
                    dye_id = dj.value("id", string(""));
                    ItemDef dye;
                    dye.id = dye_id;
                    dye.name = dj.value("name", dye_id);
                    dye.description = "A vat's worth, boiled down. It is what makes " + st.value("name", t.name) +
                                      " cloth the colour it is.";
                    dye.stackable = true;
                    dye.untaught = true;
                    dye.value = 5 * value_mult;
                    dye.icon = icon_for(dye_id);
                    dye.tier = t.id;
                    dye.tier_index = index;
                    dye.piece = "dye";
                    dye.tint = colour;
                    dye.tags = {"dye", "cloth"};
                    defs[dye.id] = dye;
                    map<string, int> brew;
                    if (dj.contains("inputs"))
                        for (auto i = dj["inputs"].begin(); i != dj["inputs"].end(); ++i)
                            brew[i.key()] = i.value().get<int>();
                    // Brewed at the Foraging level of its rarest herb, the way
                    // every potion is: whoever can pick it can boil it.
                    int herb_level = 1;
                    for (const auto& in : brew)
                        if (const ItemDef* mat = Get(in.first)) herb_level = std::max(herb_level, mat->forage_level);
                    add_recipe(dye.id, herb_level, 16 + index * 10, brew);
                }

                for (const char* piece_name : kSetPieces) {
                    if (!sj.contains("pieces") || !sj["pieces"].contains(piece_name)) continue;
                    const json& pj = sj["pieces"][piece_name];
                    const string piece = set_id + "_" + piece_name;

                    ItemDef d;
                    d.id = t.id + "_" + piece;
                    d.name = st.value("name", t.name) + " " + pj.value("noun", string(piece_name));
                    const string set_flavour = st.value("flavour", string(""));
                    d.description = set_flavour.empty() ? pj.value("desc", string(""))
                                                        : set_flavour + " " + pj.value("desc", string(""));
                    d.slot = static_cast<EquipSlot>(EquipSlotFromName(pj.value("slot", string("none"))));
                    d.value = pj.value("value", 10) * value_mult;
                    d.icon = icon_for(piece + "_" + t.id);
                    d.tier = t.id;
                    d.tier_index = index;
                    d.piece = piece;
                    d.tint = colour;
                    d.armour_layer = pj.value("layer", string(""));
                    d.armour_cut = sj.value("cut", string(""));
                    d.tags = {set_id == "robe" ? "cloth" : "leather"};
                    d.defence_bonus = std::max(1, static_cast<int>(std::lround(
                        tj.value("armour_power", 10.0f) * pj.value("defence", 0.5f))));
                    const int boost = std::max(1, static_cast<int>(std::lround(
                        tj.value("weapon_power", 10.0f) * pj.value("style", 0.2f))));
                    if (style == "ranged") d.ranged_bonus = boost;
                    else if (style == "magic") d.magic_bonus = boost;
                    if (t.level > 1 && skill >= 0) d.requirements[skill] = t.level;
                    defs[d.id] = d;
                    tier_pieces[t.id + "/" + piece] = d.id;

                    map<string, int> inputs;
                    const string material = st.value("material", sj.value("material", string("")));
                    const int amount = pj.value("material", 1);
                    if (!material.empty()) inputs[material] = amount;
                    if (st.contains("extra"))
                        for (auto i = st["extra"].begin(); i != st["extra"].end(); ++i)
                            inputs[i.key()] += i.value().get<int>();
                    const string thread = sj.value("thread", string(""));
                    if (!thread.empty() && pj.value("thread", 0) > 0) inputs[thread] += pj.value("thread", 0);
                    if (!dye_id.empty() && pj.value("dye", 0) > 0) inputs[dye_id] += pj.value("dye", 0);
                    add_recipe(d.id, t.level > 1 ? t.level : t.level + pj.value("craft_offset", 0),
                               (12 + index * 14) * std::max(1, amount), inputs);
                }
            }
        }

        tiers.push_back(t);
        ++index;
    }

    SettleCraftValues();
    SDL_Log("ItemDatabase: %d tiers, %d recipes (%s)",
            static_cast<int>(tiers.size()), static_cast<int>(recipes.size()), path.c_str());
    return true;
}

int ItemDatabase::InputValue(const ItemDef& recipe) const {
    int total = 0;
    for (const auto& in : recipe.craft_inputs)
        if (const ItemDef* mat = Get(in.first)) total += mat->value * in.second;
    return total;
}

void ItemDatabase::SettleCraftValues() {
    // A few passes, because a bar is an input to a sword: the bar has to be
    // settled before the sword can be.
    for (int pass = 0; pass < 4; ++pass)
        for (const ItemDef* r : Recipes()) {
            auto it = defs.find(r->craft_result);
            if (it == defs.end() || it->second.value <= 1) continue;
            const float per = static_cast<float>(InputValue(*r)) / std::max(1, r->craft_qty);
            const int floor_value = static_cast<int>(std::ceil(per * CRAFT_VALUE_ADD));
            it->second.value = std::max(it->second.value, floor_value);
        }
}

vector<ItemStat> ItemStatLines(const ItemDef& d, const ItemDef* worn, bool compare) {
    vector<ItemStat> rows;
    const ItemDef blank;                       // what "nothing worn" is worth
    const ItemDef& o = worn ? *worn : blank;

    // A row is kept when either piece has something to say about it. A stat
    // that is nothing on both is left off -- a helmet's block is one line, not
    // five zeroes -- but one the worn piece has and this one does not is kept,
    // because losing it is the whole point of showing the change.
    const auto stat = [&](const char* label, int mine, int theirs) {
        if (mine == 0 && theirs == 0) return;
        char v[32];
        SDL_snprintf(v, sizeof(v), "%+d", mine);
        ItemStat r;
        r.label = label;
        r.value = v;
        if (compare) {
            const int change = mine - theirs;
            char c[32];
            SDL_snprintf(c, sizeof(c), "(%+d)", change);
            r.delta = change == 0 ? "( -- )" : c;
            r.verdict = change > 0 ? 1 : (change < 0 ? -1 : 0);
        }
        rows.push_back(r);
    };
    // The same row for anything measured as a fraction: block, walk speed.
    const auto share = [&](const char* label, float mine, float theirs, const char* unit) {
        if (fabsf(mine) < 0.005f && fabsf(theirs) < 0.005f) return;
        char v[40];
        SDL_snprintf(v, sizeof(v), "%.0f%s", mine * 100.0f, unit);
        ItemStat r;
        r.label = label;
        r.value = v;
        if (compare) {
            const float change = mine - theirs;
            char c[40];
            SDL_snprintf(c, sizeof(c), "(%+.0f%s)", change * 100.0f, unit);
            r.delta = fabsf(change) < 0.005f ? "( -- )" : c;
            r.verdict = fabsf(change) < 0.005f ? 0 : (change > 0.0f ? 1 : -1);
        }
        rows.push_back(r);
    };

    stat("Attack",   d.attack_bonus,   o.attack_bonus);
    stat("Strength", d.strength_bonus, o.strength_bonus);
    stat("Defence",  d.defence_bonus,  o.defence_bonus);
    stat("Ranged",   d.ranged_bonus,   o.ranged_bonus);
    stat("Magic",    d.magic_bonus,    o.magic_bonus);
    share("Block",   d.block,          o.block, "%");
    share("Walk",    d.move_speed,     o.move_speed, "%");

    // A weapon's speed, said as a rate the way the bag says it: the stored
    // number is a multiplier on swing time, so smaller is faster, and a stat
    // where less is better has to be turned round before anybody reads it.
    if (d.slot == SLOT_WEAPON || (worn && worn->slot == SLOT_WEAPON)) {
        const float mine = d.attack_speed > 0.0f ? d.attack_speed : 1.0f;
        const float theirs = (worn && worn->attack_speed > 0.0f) ? worn->attack_speed : 1.0f;
        if (fabsf(mine - 1.0f) > 0.005f || fabsf(theirs - 1.0f) > 0.005f) {
            char v[40];
            SDL_snprintf(v, sizeof(v), "%.2fx", 1.0f / mine);
            ItemStat r;
            r.label = "Swing speed";
            r.value = v;
            if (compare) {
                const float change = (1.0f / mine) - (1.0f / theirs);
                char c[40];
                SDL_snprintf(c, sizeof(c), "(%+.2f)", change);
                r.delta = fabsf(change) < 0.005f ? "( -- )" : c;
                r.verdict = fabsf(change) < 0.005f ? 0 : (change > 0.0f ? 1 : -1);
            }
            rows.push_back(r);
        }
        if (d.reach > 1.005f || (worn && worn->reach > 1.005f)) {
            char v[40];
            SDL_snprintf(v, sizeof(v), "%.2fx", d.reach);
            ItemStat r;
            r.label = "Reach";
            r.value = v;
            rows.push_back(r);
        }
        // A dagger: what a second one in the other hand does to the first.
        if (d.offhand) {
            char v[40];
            SDL_snprintf(v, sizeof(v), "%.1fx", 1.0f / std::max(0.05f, d.dual_speed));
            ItemStat r;
            r.label = "Paired speed";
            r.value = v;
            rows.push_back(r);
        }
    }

    // A lamp is not a stat block, but how far it throws is the only number
    // anybody buys one for.
    if (d.light_radius > 0.0f) {
        ItemStat r;
        r.label = "Light";
        r.value = std::to_string(static_cast<int>(d.light_radius));
        rows.push_back(r);
    }
    return rows;
}

CraftStation CraftStationFromName(const string& name) {
    if (name == "cauldron") return CraftStation::Cauldron;
    if (name == "range" || name == "fire") return CraftStation::Range;
    if (name == "loom") return CraftStation::Loom;
    if (name == "rack" || name == "tanning_rack") return CraftStation::Rack;
    return name == "anvil" ? CraftStation::Anvil : CraftStation::Workbench;
}

const char* CraftStationName(CraftStation s) {
    switch (s) {
        case CraftStation::Anvil:    return "anvil";
        case CraftStation::Cauldron: return "cauldron";
        case CraftStation::Range:    return "range";
        case CraftStation::Loom:     return "loom";
        case CraftStation::Rack:     return "rack";
        default:                     return "workbench";
    }
}

int CraftSkill(CraftStation s) {
    switch (s) {
        case CraftStation::Anvil:    return SKILL_SMITHING;
        case CraftStation::Cauldron: return SKILL_BREWING;
        case CraftStation::Range:    return SKILL_COOKING;
        // The loom is the weaver's share of Crafting, the rack the tanner's
        // and the bench the carpenter's: three stations, one skill, which is
        // why Wynn's order book and Nessa's both pay into the same number.
        default:                     return SKILL_CRAFTING;
    }
}

// Decided when asked rather than when loaded: the materials of a recipe can be
// defined in a file loaded after the recipe itself.
CraftStation ItemDatabase::StationFor(const ItemDef& recipe) const {
    // A recipe that says where it belongs: everything cooked.
    if (!recipe.craft_at.empty()) return CraftStationFromName(recipe.craft_at);
    // Anything brewed is brewed, even with a metal in it. This has to come
    // before the loom: a dye is cloth's business and carries the cloth tag, but
    // it is boiled in a vat and not woven.
    for (const auto& in : recipe.craft_inputs)
        if (const ItemDef* mat = Get(in.first))
            if (std::find(mat->tags.begin(), mat->tags.end(), "brewing") != mat->tags.end())
                return CraftStation::Cauldron;
    // Anything whose result is cloth is woven: the bolts themselves, whichever
    // fibre they are spun from, and every piece of the mage's set, which the
    // tiers tag "cloth" where the ranger's is tagged "leather". Asking the
    // result rather than the inputs is what keeps the bags and the bedroll at
    // the bench -- they are cloth and hide together, and a hide is sewn.
    if (const ItemDef* made = Get(recipe.craft_result))
        if (std::find(made->tags.begin(), made->tags.end(), "cloth") != made->tags.end())
            return CraftStation::Loom;
    // And anything whose result is leather is worked on a tanner's frame: the
    // ranger's hides in every tier, the jerkin and the chaps and the boots, the
    // bags, the bedroll. The tanneries had frames of hide standing all round a
    // carpenter's bench, and the bench was where the hide was worked. Asked of
    // the result again, and before the metal: a banded jerkin has iron in it
    // and is still a jerkin, cut by a tanner and not beaten out by a smith --
    // and a Barkwood Helm has a hide in it and is still wood.
    if (const ItemDef* made = Get(recipe.craft_result))
        if (std::find(made->tags.begin(), made->tags.end(), "leather") != made->tags.end())
            return CraftStation::Rack;
    for (const auto& in : recipe.craft_inputs)
        if (const ItemDef* mat = Get(in.first))
            if (mat->metal) return CraftStation::Anvil;
    return CraftStation::Workbench;
}

vector<const ItemDef*> ItemDatabase::Recipes(CraftStation station) const {
    vector<const ItemDef*> out;
    for (const ItemDef* r : Recipes())
        if (StationFor(*r) == station) out.push_back(r);
    return out;
}

vector<const ItemDef*> ItemDatabase::Recipes() const {
    vector<const ItemDef*> out;
    for (const auto& kv : defs)
        if (!kv.second.craft_result.empty()) out.push_back(&kv.second);
    for (const ItemDef& r : recipes) out.push_back(&r);
    for (const ItemDef& r : data_recipes) out.push_back(&r);
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

void Inventory::Resize(int slots) {
    slots = std::max(1, slots);
    if (slots == static_cast<int>(items.size())) return;
    if (slots > static_cast<int>(items.size())) { items.resize(slots); return; }
    vector<ItemStack> spill(items.begin() + slots, items.end());
    items.resize(slots);
    for (const ItemStack& s : spill)
        if (!s.Empty()) Add(s.id, s.qty);
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
    for (int s = 0; s < SLOT_COUNT; ++s) {
        if (slots[s].empty()) continue;
        const ItemDef* d = db->Get(slots[s]);
        if (!d) continue;
        // A second dagger is speed, not a second set of bonuses: counted, a pair
        // would be twice as quick and half as accurate again on top of it.
        if (s == SLOT_SHIELD && d->slot == SLOT_WEAPON) continue;
        total += d->*field;
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
    if (const ItemDef* w = db->Get(slots[SLOT_WEAPON]))
        return w->attack_speed * (DualWielding() ? w->dual_speed : 1.0f);
    return 1.0f;
}

const ItemDef* Equipment::Offhand() const {
    if (!db) return nullptr;
    const ItemDef* main = db->Get(slots[SLOT_WEAPON]);
    const ItemDef* off  = db->Get(slots[SLOT_SHIELD]);
    if (!main || !off || !main->offhand) return nullptr;
    return (off->slot == SLOT_WEAPON && off->offhand) ? off : nullptr;
}

float Equipment::DualDamage() const {
    const ItemDef* main = Weapon();
    return (main && DualWielding()) ? main->dual_damage : 1.0f;
}

float Equipment::MoveSpeed() const {
    if (!db) return 0.0f;
    float total = 0.0f;
    for (const auto& id : slots)
        if (const ItemDef* d = id.empty() ? nullptr : db->Get(id)) total += d->move_speed;
    return total;
}

const ItemDef* Equipment::Weapon() const {
    return db ? db->Get(slots[SLOT_WEAPON]) : nullptr;
}

float Equipment::LightRadius() const {
    if (!db) return 0.0f;
    float best = 0.0f;
    for (int s = 0; s < SLOT_COUNT; ++s)
        if (const ItemDef* d = db->Get(slots[s])) best = std::max(best, d->light_radius);
    return best;
}

float Equipment::Leech() const {
    if (!db) return 0.0f;
    float sum = 0.0f;
    for (int s = 0; s < SLOT_COUNT; ++s)
        if (const ItemDef* d = db->Get(slots[s])) sum += d->leech;
    return sum;
}

bool Equipment::HasPassive(const string& id) const {
    if (!db || id.empty()) return false;
    for (int s = 0; s < SLOT_COUNT; ++s)
        if (const ItemDef* d = db->Get(slots[s]))
            if (d->passive == id) return true;
    return false;
}

WeaponKind Equipment::Kind() const {
    if (!db) return WeaponKind::Melee;
    if (const ItemDef* w = db->Get(slots[SLOT_WEAPON])) return w->kind;
    return WeaponKind::Melee;
}

SDL_Color Equipment::WeaponTint() const {
    if (!db) return {255, 255, 255, 255};
    if (const ItemDef* w = db->Get(slots[SLOT_WEAPON])) return w->tint;
    return {255, 255, 255, 255};
}

// Armour has no art of its own in these packs, so what is worn shows as a
// tint over the body and head layers. Heavier pieces pull the colour further,
// which is enough to tell bronze from iron from steel at a glance.
SDL_Color Equipment::ArmourTint() const {
    if (!db) return {255, 255, 255, 255};

    float r = 0, g = 0, b = 0, weight = 0;
    for (int slot : {SLOT_HEAD, SLOT_BODY, SLOT_HANDS,
                     SLOT_LEGS, SLOT_FEET, SLOT_SHIELD}) {
        const ItemDef* d = db->Get(slots[slot]);
        if (!d) continue;
        if (d->slot == SLOT_WEAPON) continue;          // a dagger in the off hand is not armour
        // Recolouring stands in for armour we have no art for. A piece that
        // brings its own overlay -- an icon-pack attachment, or one of the
        // character's own plate layers -- is already visible, and tinting the
        // body underneath it as well would wash the whole character in its
        // colour.
        if (d->worn && !d->worn_sprite.empty()) continue;
        if (!d->armour_layer.empty()) continue;
        // Bigger pieces dominate the look.
        const float w = 1.0f + d->defence_bonus * 0.05f;
        r += d->tint.r * w;
        g += d->tint.g * w;
        b += d->tint.b * w;
        weight += w;
    }
    if (weight <= 0.0f) return {255, 255, 255, 255};

    // Blend toward the armour colour rather than replacing the skin outright.
    const float mix = std::min(0.80f, 0.34f + weight * 0.13f);
    const float br = r / weight, bg = g / weight, bb = b / weight;
    return {static_cast<Uint8>(255 + (br - 255) * mix),
            static_cast<Uint8>(255 + (bg - 255) * mix),
            static_cast<Uint8>(255 + (bb - 255) * mix), 255};
}

vector<Attachment> Equipment::Attachments() const {
    vector<Attachment> out;
    if (!db) return out;

    // Feet upward, so a helmet ends up over a gorget and a gauntlet over a
    // sleeve rather than the other way round.
    for (int slot : {SLOT_FEET, SLOT_LEGS, SLOT_BODY, SLOT_HANDS,
                     SLOT_HEAD, SLOT_WEAPON}) {
        const ItemDef* d = db->Get(slots[slot]);
        if (!d || !d->worn || d->worn_sprite.empty()) continue;

        Attachment a;
        a.sprite = d->worn_sprite;
        a.after  = d->worn_after;
        // Only a held weapon swaps sides with the character; armour is worn
        // facing the same way whichever direction they walk.
        a.mirror_facing_right = (slot == SLOT_WEAPON);
        a.rect   = d->worn_rect;
        for (int i = 0; i < 4; ++i) a.facings[i] = d->worn_facings[i];
        out.push_back(a);
    }
    return out;
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

// --- enchantments -----------------------------------------------------------------

bool ItemDatabase::LoadEnchantments(const string& path) {
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
    if (!root.contains("enchantments") || !root["enchantments"].is_object()) return false;

    // Twins from an earlier load go first, so a second load does not enchant
    // an enchanted piece.
    for (auto it = defs.begin(); it != defs.end();)
        it = it->second.enchant.empty() ? std::next(it) : defs.erase(it);
    enchants.clear();

    const json& all = root["enchantments"];
    for (auto it = all.begin(); it != all.end(); ++it) {
        const json& o = it.value();
        EnchantDef e;
        e.id     = it.key();
        e.name   = o.value("name", e.id);
        e.suffix = o.value("suffix", "of " + e.name);
        e.text   = o.value("text", string(""));
        e.level  = std::clamp(o.value("level", 1), 1, MAX_SKILL_LEVEL);
        e.xp     = o.value("xp", 0);
        e.value  = o.value("value", 0);
        e.from   = o.value("from", string(""));
        e.move_speed = o.value("move_speed", 0.0f);
        if (o.contains("bonus")) {
            const json& b = o["bonus"];
            e.attack_bonus   = b.value("attack", 0);
            e.strength_bonus = b.value("strength", 0);
            e.defence_bonus  = b.value("defence", 0);
            e.ranged_bonus   = b.value("ranged", 0);
            e.magic_bonus    = b.value("magic", 0);
        }
        if (o.contains("slots"))
            for (const json& s : o["slots"]) {
                const int slot = EquipSlotFromName(s.get<string>());
                if (slot != SLOT_NONE) e.slots.push_back(static_cast<EquipSlot>(slot));
            }
        if (o.contains("inputs"))
            for (auto i = o["inputs"].begin(); i != o["inputs"].end(); ++i)
                e.inputs[i.key()] = i.value().get<int>();
        enchants.push_back(e);
    }
    std::sort(enchants.begin(), enchants.end(), [](const EnchantDef& a, const EnchantDef& b) {
        if (a.level != b.level) return a.level < b.level;
        return a.name < b.name;
    });

    // The twins. Built from a list of the plain pieces taken first, since
    // adding to the map while walking it is asking for trouble.
    vector<string> plain;
    for (const auto& kv : defs) if (kv.second.slot != SLOT_NONE) plain.push_back(kv.first);
    int twins = 0;
    for (const EnchantDef& e : enchants)
        for (const string& id : plain) {
            const ItemDef& base = defs.at(id);
            if (!Takes(base, e)) continue;
            ItemDef v = base;
            v.id        = id + "+" + e.id;
            v.name      = base.name + " " + e.suffix;
            v.enchant   = e.id;
            v.base_item = id;
            v.value     = base.value + e.value;
            v.attack_bonus   += e.attack_bonus;
            v.strength_bonus += e.strength_bonus;
            v.defence_bonus  += e.defence_bonus;
            v.ranged_bonus   += e.ranged_bonus;
            v.magic_bonus    += e.magic_bonus;
            v.move_speed     += e.move_speed;
            // What it does, printed where a legendary piece prints its own.
            const string line = e.name + ": " + e.text;
            v.passive_text = base.passive_text.empty() ? line : base.passive_text + "\n" + line;
            // A twin is not a recipe, is not taught, and is nobody's quest item.
            v.craft_result.clear();
            v.craft_inputs.clear();
            v.needs_recipe = false;
            v.recipe_from.clear();
            v.tags.push_back("enchanted");
            defs[v.id] = v;
            ++twins;
        }
    SDL_Log("ItemDatabase: %d enchantments, %d enchanted pieces (%s)",
            static_cast<int>(enchants.size()), twins, path.c_str());
    return !enchants.empty();
}

vector<const EnchantDef*> ItemDatabase::Enchantments() const {
    vector<const EnchantDef*> out;
    for (const EnchantDef& e : enchants) out.push_back(&e);
    return out;
}

const EnchantDef* ItemDatabase::Enchantment(const string& id) const {
    for (const EnchantDef& e : enchants) if (e.id == id) return &e;
    return nullptr;
}

bool ItemDatabase::Takes(const ItemDef& piece, const EnchantDef& e) const {
    if (piece.slot == SLOT_NONE || !piece.enchant.empty()) return false;
    if (std::find(e.slots.begin(), e.slots.end(), piece.slot) == e.slots.end()) return false;
    // Only a shield takes a shield's charm: a lantern is worn in the same
    // hand and is not one.
    if (piece.slot == SLOT_SHIELD && piece.block <= 0.0f) return false;
    return true;
}

string ItemDatabase::EnchantedId(const string& piece, const string& enchant) const {
    const string id = piece + "+" + enchant;
    return defs.count(id) ? id : string();
}

namespace Enchanting {

vector<int> Targets(const ItemDatabase& db, const EnchantDef& e, const Inventory& bag) {
    vector<int> out;
    for (int i = 0; i < bag.SlotCount(); ++i) {
        const ItemStack& s = bag.Slot(i);
        if (s.Empty()) continue;
        const ItemDef* d = db.Get(s.id);
        if (d && db.Takes(*d, e)) out.push_back(i);
    }
    return out;
}

bool Work(const ItemDatabase& db, const EnchantDef& e, Inventory& bag, int slot, string& why) {
    why.clear();
    if (slot < 0 || slot >= bag.SlotCount() || bag.Slot(slot).Empty()) {
        why = "Nothing in your pack takes this enchantment.";
        return false;
    }
    const string piece = bag.Slot(slot).id;
    const ItemDef* d = db.Get(piece);
    if (!d || !db.Takes(*d, e)) {
        why = d && !d->enchant.empty() ? "It already carries an enchantment."
                                       : "This enchantment does not fit that.";
        return false;
    }
    const string made = db.EnchantedId(piece, e.id);
    if (made.empty()) {
        why = "This enchantment does not fit that.";
        return false;
    }
    for (const auto& in : e.inputs)
        if (!bag.Has(in.first, in.second)) {
            why = "You are missing materials.";
            return false;
        }
    for (const auto& in : e.inputs) bag.Remove(in.first, in.second);
    // The piece leaves the bag before its twin arrives, so the twin lands in
    // the slot it left and the bag never needs a spare one.
    bag.RemoveSlot(slot, 1);
    bag.Add(made, 1);
    return true;
}

}
