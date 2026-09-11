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
#include "../src/entity/player.h"

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
    "house_elder", "house_inn", "house_inn_upper", "house_smith",
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
    items.Load("data/items_armour.json", false);   // optional armour pack
    Check(enemy_db.Load("data/enemies.json"),       "data/enemies.json loads");
    Check(loot.Load("data/loot_tables.json"),       "data/loot_tables.json loads");
    loot.Load("data/loot_tables_armour.json", false);  // optional armour drops
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

    Section("worn equipment overlays");
    for (const auto& kv : items.All()) {
        const ItemDef& d = kv.second;
        if (!d.worn) continue;
        Check(!d.worn_sprite.empty(), kv.first + " worn overlay names a sprite");
        Check(fs::exists(d.worn_sprite),
              kv.first + " worn art missing: " + d.worn_sprite);
        // The rectangle is in frame pixels, so it has to land inside the frame.
        Check(d.worn_rect.w > 0 && d.worn_rect.h > 0,
              kv.first + " worn rect has a size");
        Check(d.worn_rect.x >= 0 && d.worn_rect.y >= 0 &&
              d.worn_rect.x + d.worn_rect.w <= 64 &&
              d.worn_rect.y + d.worn_rect.h <= 64,
              kv.first + " worn rect sits inside the 64px frame");
        // Something has to be visible, or the overlay is dead weight.
        Check(d.worn_facings[0] || d.worn_facings[1] ||
              d.worn_facings[2] || d.worn_facings[3],
              kv.first + " worn overlay is visible from at least one side");

        // Inside the frame is not enough: the character only occupies part of
        // it. In an idle frame the rig runs x19..39 and y21..48, so a rectangle
        // that clears the frame check can still float in empty space beside the
        // sprite -- which is exactly what an early pass at these got wrong.
        Check(d.worn_rect.x + d.worn_rect.w > 19.0f && d.worn_rect.x < 39.0f &&
              d.worn_rect.y + d.worn_rect.h > 21.0f && d.worn_rect.y < 48.0f,
              kv.first + " worn rect overlaps the character, not empty frame");

        // Armour is placed against the part of the body it covers, so a helmet
        // that has slipped down to the knees shows up here rather than in a
        // screenshot.
        const float mid_y = d.worn_rect.y + d.worn_rect.h * 0.5f;
        if (d.slot == SLOT_HEAD)
            Check(mid_y < 36.0f, kv.first + " sits on the head, not the body");
        else if (d.slot == SLOT_FEET)
            Check(mid_y > 38.0f, kv.first + " sits at the feet, not the chest");
    }

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

            // The spawn it names has to exist, and must not stand inside a
            // step-through portal on the far side. A flight of stairs is two
            // step-through portals facing each other; arriving on top of the
            // one that leads back bounces the player between the floors.
            Map there;
            if (there.Load("maps/" + p.target_map + ".mx")) {
                SDL_FPoint arrive{};
                const bool has = there.Spawn(p.target_spawn, arrive);
                Check(has, string(id) + " portal '" + p.label + "' arrives at a spawn that exists ('"
                           + p.target_map + "/" + p.target_spawn + "')");
                if (has) {
                    const SDL_FRect feet = {arrive.x - 8.0f, arrive.y - 10.0f, 16.0f, 10.0f};
                    bool inside = false;
                    for (const Portal& back : there.Portals())
                        if (!back.requires_interact && RectsOverlap(feet, back.rect)) inside = true;
                    Check(!inside, string(id) + " portal '" + p.label
                                   + "' does not drop you onto a portal that sends you back");
                    Check(!there.Blocked(feet), string(id) + " portal '" + p.label
                                   + "' does not arrive inside a wall");
                }

                // And the way back has to bring you out where you went in.
                // Every building's exit used to arrive at the town's default
                // spawn -- the crossroads -- so leaving the forge put you in
                // the middle of the square. Of the portals on the far side
                // that lead here, the nearest arrival must be by this one.
                float nearest = -1.0f;
                for (const Portal& back : there.Portals()) {
                    if (back.target_map != id) continue;
                    SDL_FPoint home{};
                    if (!map.Spawn(back.target_spawn, home)) continue;
                    const float d = Length(home.x - (p.rect.x + p.rect.w / 2.0f),
                                           home.y - (p.rect.y + p.rect.h / 2.0f));
                    if (nearest < 0.0f || d < nearest) nearest = d;
                }
                if (nearest >= 0.0f)
                    Check(nearest < 128.0f, string(id) + " portal '" + p.label
                              + "': the way back arrives by it (" + std::to_string(int(nearest))
                              + "px away)");
            }
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

        // A ricochet that keeps all its speed never settles, and one that
        // keeps none stops dead on the first wall and is not a ricochet.
        Check(d.bounces >= 0, kv.first + " has a sane bounce count");
        if (d.bounces > 0)
            Check(d.bounce_damping > 0.0f && d.bounce_damping < 1.0f,
                  kv.first + " loses some, but not all, speed on a bounce");

        // Sub-stepping in UpdateProjectiles keeps each slice to half a radius,
        // capped at 32 slices. Past that cap a projectile moves further than
        // its own body in one slice and can pass through a wall.
        const float travel = d.speed / 30.0f;              // a slow frame
        const float slice  = std::max(2.0f, d.radius * 0.5f);
        Check(travel / slice <= 32.0f,
              kv.first + " is slow enough that its sub-steps cannot tunnel");
    }

    // --- you can reach what is in a room ---------------------------------------
    // Furniture is easy to place so that it blocks the way to the one thing in
    // the room that matters -- a counter across the smith, a bench between the
    // door and the hearth. Flood the floor from where you arrive, at the size of
    // the player's feet, and check every NPC and usable object is within reach
    // of somewhere the flood got to.
    Section("everything in a building can be reached");
    for (const char* id : {"house_smith", "guild_hall", "house_elder", "house_inn",
                           "house_inn_upper"}) {
        Map room;
        if (!room.Load(string("maps/") + id + ".mx")) continue;

        constexpr float STEP = 8.0f;
        constexpr float REACH = 58.0f;             // World's INTERACT_RANGE
        const int cols = static_cast<int>(room.Width() / STEP);
        const int rows = static_cast<int>(room.Height() / STEP);
        auto feet = [](float x, float y) { return SDL_FRect{x - 8.0f, y - 10.0f, 16.0f, 10.0f}; };

        vector<char> seen(static_cast<size_t>(cols) * rows, 0);
        vector<pair<int,int>> todo;
        const SDL_FPoint sp = room.DefaultSpawn();
        const int sx = static_cast<int>(sp.x / STEP), sy = static_cast<int>(sp.y / STEP);
        if (sx >= 0 && sy >= 0 && sx < cols && sy < rows) {
            seen[static_cast<size_t>(sy) * cols + sx] = 1;
            todo.push_back({sx, sy});
        }
        while (!todo.empty()) {
            const auto [cx, cy] = todo.back();
            todo.pop_back();
            static const int kDir[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (const auto& d : kDir) {
                const int nx = cx + d[0], ny = cy + d[1];
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                char& f = seen[static_cast<size_t>(ny) * cols + nx];
                if (f) continue;
                if (room.Blocked(feet(nx * STEP, ny * STEP))) continue;
                f = 1;
                todo.push_back({nx, ny});
            }
        }

        auto reachable = [&](float x, float y) {
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx)
                    if (seen[static_cast<size_t>(cy) * cols + cx] &&
                        Length(cx * STEP - x, cy * STEP - y) <= REACH)
                        return true;
            return false;
        };

        for (const NpcDef& n : room.Npcs())
            Check(reachable(n.x, n.y), string(id) + ": " + n.name + " can be walked up to");
        for (const MapObject& o : room.Objects())
            Check(reachable(o.x, o.y), string(id) + ": " + o.id + " can be walked up to");
    }

    // --- every rise is climbable ----------------------------------------------
    // The rule is that no piece of raised ground may be sealed off: each has to
    // be reachable by walking, by a ramp, or by a jump of up to CLIMB_LEVELS.
    // Checked by flooding outward from the spawn over the height grid and
    // counting what the flood never reaches -- a three-level shelf with no
    // stairs shows up here instead of as a player stuck at the foot of it.
    Section("every rise can be climbed");
    {
        Map ow;
        ow.Load("maps/overworld.mx");
        if (ow.HasElevation()) {
            const float cell = ow.ElevationCell();
            const int cols = static_cast<int>(ow.Width() / cell);
            const int rows = static_cast<int>(ow.Height() / cell);
            auto centre = [&](int cx, int cy) {
                return SDL_FPoint{(cx + 0.5f) * cell, (cy + 0.5f) * cell};
            };

            vector<char> seen(static_cast<size_t>(cols) * rows, 0);
            vector<pair<int,int>> todo;
            const SDL_FPoint sp = ow.DefaultSpawn();
            const int sx = std::clamp(static_cast<int>(sp.x / cell), 0, cols - 1);
            const int sy = std::clamp(static_cast<int>(sp.y / cell), 0, rows - 1);
            todo.push_back({sx, sy});
            seen[static_cast<size_t>(sy) * cols + sx] = 1;

            while (!todo.empty()) {
                const auto [cx, cy] = todo.back();
                todo.pop_back();
                const SDL_FPoint a = centre(cx, cy);
                const int la = ow.LevelAt(a.x, a.y);
                static const int kDir[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (const auto& d : kDir) {
                    const int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                    char& flag = seen[static_cast<size_t>(ny) * cols + nx];
                    if (flag) continue;
                    const SDL_FPoint b = centre(nx, ny);
                    const int lb = ow.LevelAt(b.x, b.y);
                    const bool walk  = (la == lb);
                    const bool ramp  = ow.RampAt(a.x, a.y) || ow.RampAt(b.x, b.y);
                    const bool jump  = std::abs(la - lb) <= Player::CLIMB_LEVELS;
                    if (walk || ramp || jump) {
                        flag = 1;
                        todo.push_back({nx, ny});
                    }
                }
            }

            int sealed = 0;
            for (char c : seen) if (!c) ++sealed;
            if (sealed) printf("     %d height cells cannot be reached\n", sealed);
            Check(sealed == 0, "no raised ground is sealed off from the spawn");
        }
    }

    // --- state does not leak between maps -------------------------------------
    // The game reuses one Map object for every map it ever loads. Loading each
    // map into a fresh object -- which every other check here does -- can never
    // see state left behind by the previous one, and that is exactly how the
    // overworld's height grid survived into every building: the guild hall
    // doorway sat on a hill that was really a field outside.
    Section("map state does not leak between loads");
    {
        Map shared;
        Check(shared.Load("maps/overworld.mx"), "overworld loads into a shared map");
        Check(shared.HasElevation(), "the overworld has a height grid");

        for (const char* inside : {"guild_hall", "house_smith", "house_elder", "house_inn",
                                   "house_inn_upper", "town_havenbrook"}) {
            Check(shared.Load(string("maps/") + inside + ".mx"),
                  string(inside) + " loads after the overworld");
            Check(!shared.HasElevation(),
                  string(inside) + " does not inherit the overworld's height grid");
            const SDL_FPoint sp = shared.DefaultSpawn();
            Check(shared.LevelAt(sp.x, sp.y) == 0,
                  string(inside) + " is flat where you arrive");

            // Reload the overworld between each, so every building is checked
            // straight after the map most likely to have polluted it.
            shared.Load("maps/overworld.mx");
        }
    }

    // --- attack speed and cooldown --------------------------------------------
    // "You cannot spam it" is a claim about time, so it is measured rather
    // than eyeballed: how many swings a weapon actually gets in ten seconds.
    Section("attack speed and cooldown");
    {
        // One full cycle of a swing: every phase of it, plus the gap after.
        auto cycle = [](AttackType type, int combo_index, float speed) {
            const AttackProfile p = ScaleForSpeed(ProfileFor(type, combo_index), speed);
            return p.Total() + p.cooldown;
        };

        Check(cycle(AttackType::Light, 0, 1.0f) > 0.0f, "a swing takes time");

        // A faster weapon has to actually swing faster, and a slower one
        // slower. This is the property that was declared in data and never
        // used until now, so it is worth checking it is wired up at all.
        const float fast = cycle(AttackType::Light, 0, 0.70f);
        const float even = cycle(AttackType::Light, 0, 1.00f);
        const float slow = cycle(AttackType::Light, 0, 1.30f);
        Check(fast < even && even < slow, "attack speed orders swings correctly");
        Check(fabsf(fast / even - 0.70f) < 0.01f,
              "a 0.70 speed weapon takes 70% as long, cooldown included");

        // Nonsense in the data must not turn into an attack every frame.
        Check(ScaleForSpeed(ProfileFor(AttackType::Light, 0), 0.0f).Total() > 0.02f,
              "a zero attack speed is clamped rather than swinging instantly");

        // Every attack must leave a gap. Without one the button is something
        // you hold down, which is the thing this exists to prevent.
        for (int i = 0; i < 3; ++i)
            Check(ProfileFor(AttackType::Light, i).cooldown > 0.0f,
                  "light " + std::to_string(i) + " has a cooldown");
        Check(ProfileFor(AttackType::Strong).cooldown >
              ProfileFor(AttackType::Light, 0).cooldown,
              "a strong attack costs more downtime than a light one");
        Check(ProfileFor(AttackType::Charged).cooldown >
              ProfileFor(AttackType::Strong).cooldown,
              "a charged attack costs the most downtime");

        // Continuing a chain has to be quicker than starting a fresh one, or
        // the combo is a damage bonus with no reason to reach for it.
        Check(ProfileFor(AttackType::Light, 0).cooldown <
              ProfileFor(AttackType::Light, 2).cooldown,
              "mid-chain links flow, and the finisher does not");

        // And the rate a player can actually achieve, which is the number that
        // matters. A full light chain plus its finisher gap.
        const float chain = cycle(AttackType::Light, 0, 1.0f) +
                            cycle(AttackType::Light, 1, 1.0f) +
                            cycle(AttackType::Light, 2, 1.0f);
        printf("     light chain: %.2fs for 3 hits (%.1f hits/sec sustained)\n",
               chain, 3.0f / chain);
        Check(3.0f / chain < 6.0f, "a bare-handed chain is not a machine gun");
        Check(3.0f / chain > 1.5f, "and is not so slow that combat drags");

        // Weapons declare a speed the game can use.
        for (const auto& kv : items.All()) {
            const ItemDef& d = kv.second;
            if (d.slot != SLOT_WEAPON) continue;
            Check(d.attack_speed >= 0.35f && d.attack_speed <= 3.0f,
                  kv.first + " has a sane attack speed");
        }
    }

    // --- projectiles are the right size next to a character --------------------
    // Arrows were once longer than the player firing them. The rig is about
    // 14 wide and 28 tall, so nothing thrown by it should approach that.
    Section("projectile scale");
    for (const auto& kv : projectiles.All()) {
        const ProjectileDef& d = kv.second;
        // Sprite dimensions come from the file, so this catches a change to
        // either the art or the scale.
        Check(d.scale > 0.0f && d.scale <= 2.0f, kv.first + " has a sane scale");
    }

    // --- elevation ------------------------------------------------------------
    // Elevation can strand a player: raise a plateau across the only route
    // north and the game is still perfectly playable right up until nobody can
    // reach the mine. These check the two things that cause that.
    Section("elevation");
    {
        Map ow;
        Check(ow.Load("maps/overworld.mx"), "the overworld loads");
        Check(ow.HasElevation(), "the overworld has a height grid");

        if (ow.HasElevation()) {
            // Every portal and spawn has to be somewhere you can walk to, so
            // the ground around each one must be one level, not a cliff edge.
            const float cell = ow.ElevationCell();
            auto flat_around = [&](float x, float y) {
                const int mid = ow.LevelAt(x, y);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const float px = x + dx * cell, py = y + dy * cell;
                        if (ow.LevelAt(px, py) == mid) continue;
                        if (ow.RampAt(px, py) || ow.RampAt(x, y)) continue;
                        return false;
                    }
                return true;
            };

            const SDL_FPoint start = ow.DefaultSpawn();
            Check(flat_around(start.x, start.y),
                  "the starting spawn is not on a cliff edge");

            for (const Portal& p : ow.Portals()) {
                const float px = p.rect.x + p.rect.w * 0.5f;
                const float py = p.rect.y + p.rect.h * 0.5f;
                Check(flat_around(px, py),
                      "portal '" + p.label + "' is reachable, not on a cliff edge");
            }

            // And the road has to run the length of the map, whatever it
            // climbs over: it is the only route from the town to the mine.
            int blocked_rows = 0;
            for (float y = cell; y < ow.Height() - cell; y += cell) {
                bool any_ramp = false;
                for (float x = 0.0f; x < ow.Width(); x += cell)
                    if (ow.RampAt(x, y)) { any_ramp = true; break; }
                if (!any_ramp) ++blocked_rows;
            }
            Check(blocked_rows == 0,
                  "every row of the map has a walkable crossing somewhere on it");

            // A ramp that leads nowhere is worse than no ramp: it looks like a
            // way up and is not one.
            Check(!ow.LevelChangeBlocked(start.x, start.y, start.x, start.y - cell) ||
                  ow.LevelAt(start.x, start.y) == ow.LevelAt(start.x, start.y - cell),
                  "the player can walk north out of the starting spawn");
        }
    }

    // --- wall collision -------------------------------------------------------
    // The interesting part of a projectile hitting a wall is not that it stops,
    // it is where it stops and which way the wall faces: get the normal wrong
    // and a ricochet leaves through the wall it just struck.
    Section("projectiles resolve against walls");
    {
        Map m;
        Check(m.Load("maps/guild_hall.mx"), "a walled map loads for collision tests");

        // Walk outwards from the middle of the room until a wall is found,
        // rather than assuming where one is.
        const SDL_FPoint start = m.DefaultSpawn();
        float wall_x = start.x;
        bool found = false;
        for (float x = start.x; x < start.x + 512.0f && !found; x += 2.0f) {
            if (m.Blocked({x - 4.0f, start.y - 4.0f, 8.0f, 8.0f})) {
                wall_x = x;
                found = true;
            }
        }
        Check(found, "found a wall to the east of the spawn");

        if (found) {
            const float from = wall_x - 40.0f;
            const Map::Contact c = m.SweepPoint(from, start.y, 80.0f, 0.0f, 4.0f);
            Check(c.hit, "a sweep into the wall reports a hit");
            // Stopped short of the wall, and not sent backwards.
            Check(c.x < wall_x && c.x >= from,
                  "the contact point is clear of the wall, not inside it");
            Check(!m.Blocked({c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f}),
                  "the contact point itself is not blocked");
            // Travelling east into a wall must give a normal pointing west.
            Check(c.nx < 0.0f, "the normal points back out of the wall");

            // Reflecting eastward travel about that normal must send it west.
            const float vx = 300.0f, vy = 0.0f;
            const float vn = vx * c.nx + vy * c.ny;
            Check(vx - 2.0f * vn * c.nx < 0.0f, "a bounce leaves the way it came");

            // Open floor must not report a contact, or nothing would ever move.
            const Map::Contact clear = m.SweepPoint(start.x, start.y, 4.0f, 0.0f, 4.0f);
            Check(!clear.hit, "a sweep across open floor reports no hit");
        }
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

    // --- collect objectives --------------------------------------------------
    // Found in a playtest: eight logs in the bag, and the tracker still read
    // "(0/12)", because a collect stage completes on the carried count but
    // the tracker prints a counter that nothing wrote.
    {
        Inventory inv(&items);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        log.Start("q_firewood");

        inv.Add("logs", 8);
        log.RefreshCollectObjectives(inv);
        Check(log.Counter("q_firewood") == 8, "collect objective counts what is carried");
        Check(log.CurrentObjectiveText("q_firewood").find("(8/12)") != string::npos,
              "collect objective prints the carried count");
        Check(log.IsActive("q_firewood"), "collect objective is not complete short of its count");

        inv.Remove("logs", 3);
        log.RefreshCollectObjectives(inv);
        Check(log.Counter("q_firewood") == 5, "collect objective falls when items leave the bag");

        inv.Add("logs", 20);
        log.RefreshCollectObjectives(inv);
        Check(log.Status("q_firewood") == QuestStatus::Complete,
              "collect objective completes once enough is carried");
    }

    // --- summary --------------------------------------------------------------
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) printf("all good\n");
    return g_failures;
}
