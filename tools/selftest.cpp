// -----------------------------------------------------------------------------
//  selftest - loads everything the game loads and checks that it hangs together.
//
//  Screenshots prove the game runs; they do not prove that the mission board in
//  Havenbrook names a quest that exists, that every dialogue option leads
//  somewhere, or that a loot table only drops items in the item database. Those
//  are the mistakes that survive playtesting and then strand a player halfway
//  through a quest chain, so they are checked here instead.
//
//  Build and run:  build.ps1 -Test   (or compile.sh, which builds it too)
//  Exit code is the number of problems found, so CI can use it directly.
// -----------------------------------------------------------------------------

#include "../src/headers.h"
#include "../src/sprite.h"
#include "../src/world/map.h"
#include "../src/world/world.h"
#include "../src/systems/items.h"
#include "../src/systems/loot.h"
#include "../src/systems/quest.h"
#include "../src/systems/dialogue.h"
#include "../src/systems/skills.h"
#include "../src/systems/combat.h"
#include "../src/systems/save.h"
#include "../src/systems/projectile.h"
#include "../src/systems/spell.h"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        printf("  FAIL  %s\n", what.c_str());
    }
}

static void Section(const char* name) {
    printf("\n== %s ==\n", name);
}

static const char* kMaps[] = {
    "overworld", "town_havenbrook", "guild_hall",
    "house_elder", "house_inn", "house_smith",
    "dungeon_emberfell_1", "dungeon_emberfell_2", "dungeon_barrow",
};

int main() {
    printf("DreamQuest self-test\n");

    // --- data files -----------------------------------------------------------
    Section("data files");
    SpriteLibrary    sprites;
    ItemDatabase     items;
    EnemyDatabase    enemy_db;
    LootSystem       loot;
    QuestLog         quests;
    DialogueDatabase dialogue;
    ProjectileDatabase projectiles;
    SpellBook        spells;

    Check(sprites.Load("data/sprites.json"),        "data/sprites.json loads");
    Check(items.Load("data/items.json"),            "data/items.json loads");
    Check(enemy_db.Load("data/enemies.json"),       "data/enemies.json loads");
    Check(loot.Load("data/loot_tables.json"),       "data/loot_tables.json loads");
    Check(quests.LoadDefinitions("data/quests.json"), "data/quests.json loads");
    Check(dialogue.Load("data/dialogue.json"),      "data/dialogue.json loads");
    Check(projectiles.Load("data/projectiles.json"), "data/projectiles.json loads");
    Check(spells.Load("data/spells.json"),         "data/spells.json loads");

    // --- sprite art -----------------------------------------------------------
    Section("sprite sheets exist on disk");
    {
        std::ifstream in("data/sprites.json");
        json root;
        in >> root;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const string dir = it.value().value("dir", string(""));
            for (auto c = it.value()["clips"].begin(); c != it.value()["clips"].end(); ++c) {
                const string path = dir + c.value().value("sheet", string(""));
                Check(fs::exists(path), it.key() + "/" + c.key() + " -> " + path);

                // Ragged sheets must declare a count for every direction row,
                // or the short row plays into empty frames and the character
                // disappears for part of the loop.
                if (c.value().contains("row_frames")) {
                    const auto& rows = c.value()["row_frames"];
                    Check(rows.size() == 4,
                          it.key() + "/" + c.key() + " row_frames covers all four facings");
                    for (const auto& n : rows)
                        Check(n.get<int>() >= 1,
                              it.key() + "/" + c.key() + " has a non-empty row");
                }

                // Every layer of a paperdoll has to be on disk, or the
                // character loses a body part.
                if (c.value().contains("layers"))
                    for (const auto& l : c.value()["layers"]) {
                        const string lp = dir + l.value("sheet", string(""));
                        Check(fs::exists(lp),
                              it.key() + "/" + c.key() + " layer -> " + lp);
                    }
            }
        }
    }

    // --- item icons -----------------------------------------------------------
    Section("item icons exist on disk");
    for (const auto& kv : items.All())
        if (!kv.second.icon.empty())
            Check(fs::exists(kv.second.icon), kv.first + " icon " + kv.second.icon);

    // --- loot tables only drop real items -------------------------------------
    Section("loot tables reference real items");
    {
        std::ifstream in("data/loot_tables.json");
        json root;
        in >> root;
        for (auto it = root.begin(); it != root.end(); ++it) {
            for (const char* section : {"always", "table"}) {
                if (!it.value().contains(section)) continue;
                for (const auto& entry : it.value()[section]) {
                    const string item = entry.value("item", string(""));
                    const string sub  = entry.value("table", string(""));
                    if (!item.empty() && item != "nothing")
                        Check(items.Has(item), it.key() + " drops unknown item '" + item + "'");
                    if (!sub.empty())
                        Check(loot.Has(sub), it.key() + " chains to unknown table '" + sub + "'");
                }
            }
        }
    }

    // --- enemies --------------------------------------------------------------
    Section("enemies reference real sprites and loot");
    {
        std::ifstream in("data/enemies.json");
        json root;
        in >> root;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const EnemyDef* d = enemy_db.Get(it.key());
            Check(d != nullptr, it.key() + " parses");
            if (!d) continue;
            Check(sprites.Has(d->sprite), it.key() + " sprite '" + d->sprite + "' exists");
            if (!d->loot_table.empty())
                Check(loot.Has(d->loot_table), it.key() + " loot '" + d->loot_table + "' exists");
        }
    }

    // --- quests ---------------------------------------------------------------
    Section("quest objectives and rewards resolve");
    for (const auto& kv : quests.Definitions()) {
        const QuestDef& q = kv.second;
        Check(!q.stages.empty(), q.id + " has at least one stage");

        for (const auto& p : q.prerequisites)
            Check(quests.Definition(p) != nullptr,
                  q.id + " prerequisite '" + p + "' exists");

        for (const auto& st : q.stages) {
            switch (st.type) {
                case ObjectiveType::Collect:
                    Check(items.Has(st.target),
                          q.id + " collects unknown item '" + st.target + "'");
                    break;
                case ObjectiveType::Deliver:
                    Check(items.Has(st.target),
                          q.id + " delivers unknown item '" + st.target + "'");
                    break;
                case ObjectiveType::Reach:
                    Check(fs::exists("maps/" + st.target + ".mx"),
                          q.id + " sends you to unknown map '" + st.target + "'");
                    break;
                default:
                    break;
            }
            Check(!st.description.empty(), q.id + " stage has a description");
        }

        for (const auto& r : q.rewards.items)
            Check(items.Has(r.first),
                  q.id + " rewards unknown item '" + r.first + "'");
    }

    // --- dialogue -------------------------------------------------------------
    Section("dialogue graph is connected");
    {
        std::ifstream in("data/dialogue.json");
        json root;
        in >> root;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const json& node = it.value();
            Check(!node.value("text", string("")).empty(), it.key() + " has text");

            if (!node.contains("options")) continue;
            for (const auto& opt : node["options"]) {
                const string next = opt.value("next", string("end"));
                if (next != "end" && !next.empty())
                    Check(dialogue.Has(next),
                          it.key() + " -> unknown node '" + next + "'");

                if (opt.contains("action")) {
                    const json& a = opt["action"];
                    const string give = a.value("give", string(""));
                    const string take = a.value("take", string(""));
                    const string sq   = a.value("start_quest", string(""));
                    if (!give.empty()) Check(items.Has(give), it.key() + " gives unknown item '" + give + "'");
                    if (!take.empty()) Check(items.Has(take), it.key() + " takes unknown item '" + take + "'");
                    if (!sq.empty())   Check(quests.Definition(sq) != nullptr,
                                             it.key() + " starts unknown quest '" + sq + "'");
                }
                if (opt.contains("if")) {
                    const string q = opt["if"].value("quest", string(""));
                    if (!q.empty()) Check(quests.Definition(q) != nullptr,
                                          it.key() + " tests unknown quest '" + q + "'");
                }
            }
        }
    }

    // --- maps -----------------------------------------------------------------
    Section("maps load and their contents resolve");
    std::set<string> quest_givers;
    for (const char* id : kMaps) {
        Map map;
        const string path = string("maps/") + id + ".mx";
        Check(map.Load(path), string("map ") + id + " loads");
        if (!map.Loaded()) continue;

        Check(map.Width() > 0 && map.Height() > 0, string(id) + " has bounds");

        SDL_FPoint spawn;
        Check(map.Spawn("default", spawn) || map.Spawn("entrance", spawn) ||
              map.Spawn("start", spawn),
              string(id) + " has a spawn point");

        for (const auto& p : map.Portals()) {
            Check(fs::exists("maps/" + p.target_map + ".mx"),
                  string(id) + " portal to missing map '" + p.target_map + "'");
            if (!p.locked_by.empty())
                Check(items.Has(p.locked_by),
                      string(id) + " portal locked by unknown item '" + p.locked_by + "'");
        }

        for (const auto& e : map.Enemies())
            Check(enemy_db.Has(e.type),
                  string(id) + " spawns unknown enemy '" + e.type + "'");

        for (const auto& n : map.Npcs()) {
            Check(sprites.Has(n.sprite),
                  string(id) + " npc " + n.id + " sprite '" + n.sprite + "' exists");
            if (!n.dialogue.empty())
                Check(dialogue.Has(n.dialogue),
                      string(id) + " npc " + n.id + " dialogue '" + n.dialogue + "' exists");
            quest_givers.insert(n.id);
        }

        for (const auto& o : map.Objects()) {
            Check(!o.id.empty(), string(id) + " object has an id");
            if (!o.sprite.empty())
                Check(fs::exists(o.sprite),
                      string(id) + " object " + o.id + " art missing: " + o.sprite);
            if (!o.loot_table.empty())
                Check(loot.Has(o.loot_table),
                      string(id) + " object " + o.id + " loot '" + o.loot_table + "' exists");
            if (!o.starts_quest.empty()) {
                Check(quests.Definition(o.starts_quest) != nullptr,
                      string(id) + " object " + o.id + " starts unknown quest");
                // A note left in the world is a quest giver too.
                quest_givers.insert(o.id);
            }
            if (!o.yield.empty())
                Check(items.Has(o.yield),
                      string(id) + " node " + o.id + " yields unknown item '" + o.yield + "'");
            if (!o.skill.empty())
                Check(SkillFromName(o.skill) >= 0,
                      string(id) + " node " + o.id + " unknown skill '" + o.skill + "'");
            for (const auto& q : o.quests) {
                Check(quests.Definition(q) != nullptr,
                      string(id) + " board offers unknown quest '" + q + "'");
                quest_givers.insert(o.id);
            }
        }
    }

    // --- every quest is actually obtainable -----------------------------------
    Section("every quest has a giver somewhere in the world");
    for (const auto& kv : quests.Definitions())
        Check(quest_givers.count(kv.second.giver) > 0,
              kv.first + " giver '" + kv.second.giver + "' is not in any map");

    // --- projectiles ----------------------------------------------------------
    Section("projectiles");
    for (const auto& kv : projectiles.All()) {
        const ProjectileDef& d = kv.second;
        Check(!d.sprite.empty(), kv.first + " has a sprite");
        Check(fs::exists(d.sprite), kv.first + " art missing: " + d.sprite);
        Check(d.speed > 0.0f, kv.first + " moves");
        Check(d.life > 0.0f, kv.first + " expires");
        Check(d.radius > 0.0f, kv.first + " can hit something");
    }

    // --- spells ---------------------------------------------------------------
    Section("spells");
    {
        int per_element[static_cast<int>(Element::COUNT)] = {0};
        for (const auto& kv : spells.All()) {
            const SpellDef& sp = kv.second;
            Check(projectiles.Has(sp.projectile),
                  kv.first + " fires unknown projectile '" + sp.projectile + "'");
            Check(sp.mana > 0, kv.first + " costs mana");
            Check(sp.level >= 1 && sp.level <= MAX_SKILL_LEVEL,
                  kv.first + " has a sane level requirement");
            Check(sp.element != Element::None, kv.first + " has an element");
            Check(!sp.name.empty() && !sp.description.empty(),
                  kv.first + " is described");

            // A spell should fire something of its own element, or the
            // matchup the player is being asked to think about is a lie.
            if (const ProjectileDef* pd = projectiles.Get(sp.projectile))
                Check(pd->element == sp.element,
                      kv.first + " element does not match its projectile");

            if (sp.element != Element::None)
                per_element[static_cast<int>(sp.element)]++;
        }

        // Every element must be castable, or one of the four buttons is dead.
        for (int i = 1; i < static_cast<int>(Element::COUNT); ++i)
            Check(per_element[i] > 0,
                  string("element '") + ElementName(static_cast<Element>(i)) +
                  "' has no spell");

        // A fresh character must be able to cast something at all.
        Check(spells.BestFor(Element::Fire, 1) != nullptr,
              "a level 1 character knows a fire spell");
        Check(SpellBook::MaxMana(1) >= spells.BestFor(Element::Fire, 1)->mana,
              "a level 1 character has the mana to cast it");

        // And each element must eventually open up.
        for (int i = 1; i < static_cast<int>(Element::COUNT); ++i) {
            const Element e = static_cast<Element>(i);
            Check(spells.BestFor(e, MAX_SKILL_LEVEL) != nullptr,
                  string("element '") + ElementName(e) + "' is castable at 99");
        }
    }

    // --- elemental matchups ----------------------------------------------------
    Section("elemental matchups");
    {
        // The cycle must close: every element beats exactly one other, and
        // following it four times comes back to where it started.
        for (int i = 1; i < static_cast<int>(Element::COUNT); ++i) {
            Element e = static_cast<Element>(i);
            Element walk = e;
            for (int step = 0; step < 4; ++step) walk = ElementBeats(walk);
            Check(walk == e, string("the ") + ElementName(e) + " cycle closes");
            Check(ElementBeats(e) != e && ElementBeats(e) != Element::None,
                  string(ElementName(e)) + " beats something else");
        }

        Check(ElementMultiplier(Element::Water, Element::Fire) > 1.2f,
              "water is strong against fire");
        Check(ElementMultiplier(Element::Fire, Element::Water) < 0.8f,
              "fire is weak against water");
        Check(ElementMultiplier(Element::Fire, Element::Fire) < 1.0f,
              "an element resists itself");
        Check(ElementMultiplier(Element::None, Element::Fire) == 1.0f,
              "untyped damage is unmodified");
        Check(ElementMultiplier(Element::Fire, Element::None) == 1.0f,
              "damage to an untyped creature is unmodified");
    }

    // --- ranged and magic combat ----------------------------------------------
    Section("ranged and magic maths");
    {
        CombatProfile archer;
        archer.ranged_level = 1;
        archer.ranged_bonus = 12;      // training bow
        CombatProfile caster;
        caster.magic_level = 1;
        caster.magic_bonus = 14;       // novice staff

        Check(MaxHitFor(archer, AttackStyle::Ranged, 1.0f) >= 2,
              "a starting bow can roll more than 1");
        Check(MaxHitFor(caster, AttackStyle::Magic, 1.0f) >= 2,
              "a starting staff can roll more than 1");

        // Ranged and magic must read their own levels, not Strength.
        CombatProfile weak_arms = archer;
        weak_arms.strength_level = 1;
        weak_arms.strength_bonus = 0;
        CombatProfile strong_arms = archer;
        strong_arms.strength_level = 99;
        strong_arms.strength_bonus = 200;
        Check(MaxHitFor(weak_arms, AttackStyle::Ranged, 1.0f) ==
              MaxHitFor(strong_arms, AttackStyle::Ranged, 1.0f),
              "Strength does not affect a bow");

        CombatProfile trained = archer;
        trained.ranged_level = 60;
        Check(MaxHitFor(trained, AttackStyle::Ranged, 1.0f) >
              MaxHitFor(archer, AttackStyle::Ranged, 1.0f),
              "training Ranged makes a bow hit harder");
    }

    // --- skills ---------------------------------------------------------------
    Section("skill curve");
    Check(XpForLevel(1) == 0,       "level 1 costs 0 xp");
    Check(XpForLevel(2) == 83,      "level 2 costs 83 xp (OSRS)");
    Check(XpForLevel(10) == 1154,   "level 10 costs 1154 xp (OSRS)");
    Check(XpForLevel(50) == 101333, "level 50 costs 101333 xp (OSRS)");
    Check(XpForLevel(99) == 13034431, "level 99 costs 13034431 xp (OSRS)");
    Check(LevelForXp(82) == 1 && LevelForXp(83) == 2, "level boundary at 83 xp");

    {
        Skills s;
        Check(s.Level(SKILL_HITPOINTS) == 10, "hitpoints starts at 10");
        LevelUp up;
        Check(s.AddXp(SKILL_ATTACK, 83, up) && up.level == 2, "83 attack xp gives level 2");
        Check(s.CombatLevel() >= 3, "combat level is sane at the start");
    }

    // --- combat ---------------------------------------------------------------
    Section("combat maths");
    {
        CombatProfile fresh;
        fresh.attack_level = fresh.strength_level = fresh.defence_level = 1;
        fresh.attack_bonus = 10;
        fresh.strength_bonus = 18;   // bronze sword
        fresh.defence_bonus = 18;    // wooden shield

        const EnemyDef* boar = enemy_db.Get("boar");
        Check(boar != nullptr, "boar exists");
        if (boar) {
            CombatProfile b;
            b.attack_level = boar->attack_level;
            b.strength_level = boar->strength_level;
            b.defence_level = boar->defence_level;
            b.attack_bonus = boar->attack_bonus;
            b.strength_bonus = boar->strength_bonus;
            b.defence_bonus = boar->defence_bonus;

            const int player_max = MaxHit(fresh, 1.0f);
            const float player_acc = HitChance(fresh, b);
            const int boar_max = MaxHit(b, 1.0f);
            const float boar_acc = HitChance(b, fresh);

            printf("     starting character: max %d, accuracy %.0f%%\n",
                   player_max, player_acc * 100.0f);
            printf("     boar (%d hp):        max %d, accuracy %.0f%%\n",
                   boar->hp, boar_max, boar_acc * 100.0f);

            // A new character must be able to win the first fight the level 1
            // board quest sends them into.
            const float player_dps = player_acc * (player_max / 2.0f);
            const float boar_dps   = boar_acc * (boar_max / 2.0f) / boar->attack_cooldown;
            const float swings_to_kill = boar->hp / std::max(0.01f, player_dps);
            const float player_time = swings_to_kill * 0.25f;
            const float boar_time   = 10.0f / std::max(0.01f, boar_dps);

            printf("     time to kill: player %.1fs, boar %.1fs\n", player_time, boar_time);
            Check(player_time < boar_time,
                  "a starting character beats a boar before it beats them");
            Check(player_max >= 2, "a starting swing can roll more than 1");
        }

        // Charged attacks must actually be worth the hold.
        Check(ChargeRatio(0.0f) == 0.0f, "no charge before the threshold");
        Check(ChargeRatio(CHARGE_FULL_TIME) >= 0.99f, "full charge at the full time");
        Check(ChargeMultiplier(1.0f) > ChargeMultiplier(0.0f), "charging increases damage");
        Check(ChargeMultiplier(1.0f) >= 3.0f, "a full charge hits about three times as hard");
    }

    // --- loot rolls -----------------------------------------------------------
    Section("loot rolls produce valid drops");
    {
        loot.Seed(12345);
        for (const char* table : {"orc_grunt", "boar", "chest_dungeon",
                                  "orc_warchief", "key_emberfell", "seal_barrow"}) {
            bool all_valid = true;
            for (int i = 0; i < 200; ++i)
                for (const auto& d : loot.Roll(table))
                    if (!items.Has(d.item) || d.qty <= 0) all_valid = false;
            Check(all_valid, string(table) + " only ever drops real items");
        }
        // The quest chain depends on these being guaranteed.
        bool key_always = true, seal_always = true;
        for (int i = 0; i < 100; ++i) {
            bool found_key = false, found_seal = false;
            for (const auto& d : loot.Roll("key_emberfell"))
                if (d.item == "rusted_key") found_key = true;
            for (const auto& d : loot.Roll("seal_barrow"))
                if (d.item == "barrow_seal") found_seal = true;
            if (!found_key) key_always = false;
            if (!found_seal) seal_always = false;
        }
        Check(key_always, "the Emberfell chest always yields the rusted key");
        Check(seal_always, "the barrow chest always yields the barrow seal");
    }

    // --- inventory ------------------------------------------------------------
    Section("inventory");
    {
        Inventory inv(&items);
        Check(inv.Add("coins", 100) == 100, "coins stack");
        Check(inv.Add("coins", 50) == 50, "coins stack onto the same slot");
        Check(inv.Count("coins") == 150, "stacked count is right");
        Check(inv.SlotCount() - inv.FreeSlots() == 1, "stacks take one slot");

        Check(inv.Add("bronze_sword", 1) == 1, "a sword goes in");
        Check(inv.Add("bronze_sword", 1) == 1, "swords do not stack");
        Check(inv.SlotCount() - inv.FreeSlots() == 3, "two swords take two slots");

        Check(inv.Remove("coins", 200) == false, "cannot remove more than you hold");
        Check(inv.Remove("coins", 150), "can remove the whole stack");
        Check(inv.Count("coins") == 0, "stack is gone");
    }

    // --- save round trip ------------------------------------------------------
    Section("save round trip");
    {
        Skills before;
        LevelUp up;
        before.AddXp(SKILL_WOODCUTTING, 5000, up);
        before.AddXp(SKILL_ATTACK, 1200, up);

        Skills after;
        after.FromJson(before.ToJson());
        Check(after.Xp(SKILL_WOODCUTTING) == before.Xp(SKILL_WOODCUTTING),
              "skill xp survives a save");
        Check(after.Level(SKILL_ATTACK) == before.Level(SKILL_ATTACK),
              "skill level survives a save");

        Inventory inv(&items);
        inv.Add("coins", 742);
        inv.Add("iron_sword", 1);
        inv.Add("cooked_meat", 9);

        Inventory restored(&items);
        restored.FromJson(inv.ToJson());
        Check(restored.Count("coins") == 742, "coins survive a save");
        Check(restored.Count("iron_sword") == 1, "equipment survives a save");
        Check(restored.Count("cooked_meat") == 9, "food survives a save");

        Equipment eq(&items);
        eq.Equip(SLOT_WEAPON, "iron_sword");
        eq.Equip(SLOT_SHIELD, "wooden_shield");
        Equipment eq2(&items);
        eq2.FromJson(eq.ToJson());
        Check(eq2.InSlot(SLOT_WEAPON) == "iron_sword", "worn weapon survives a save");
        Check(eq2.AttackBonus() == eq.AttackBonus(), "worn bonuses survive a save");

        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        log.Start("q_thin_the_herd");
        QuestLog log2;
        log2.LoadDefinitions("data/quests.json");
        log2.FromJson(log.ToJson());
        Check(log2.IsActive("q_thin_the_herd"), "quest progress survives a save");
    }

    // --- summary --------------------------------------------------------------
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) printf("all good\n");
    return g_failures;
}
