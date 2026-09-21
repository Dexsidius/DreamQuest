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

#include "../src/systems/gathering.h"
#include "../src/headers.h"
#include "../src/sprite.h"
#include "../src/world/map.h"
#include "../src/world/world.h"
#include "../src/systems/items.h"
#include "../src/systems/loot.h"
#include "../src/systems/quest.h"
#include "../src/systems/waypoint.h"
#include "../src/systems/dialogue.h"
#include "../src/systems/skills.h"
#include "../src/systems/combat.h"
#include "../src/systems/save.h"
#include "../src/ui/ui.h"
#include "../src/systems/projectile.h"
#include "../src/systems/spell.h"
#include "../src/systems/audio.h"
#include "../src/systems/shop.h"
#include "../src/entity/player.h"
#include "../src/ui/minimap.h"
#include "../src/ui/worldmap.h"
#include "../src/ui/titlescreen.h"
#include "../src/net/session.h"
#include "../src/coop/coop.h"

#include <fstream>
#include <set>
#include <filesystem>

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        printf("  FAIL  %s\n", what.c_str());
    } else if (getenv("DQ_VERBOSE")) {
        // Every pass too, for reading the numbers some checks carry.
        printf("  ok    %s\n", what.c_str());
    }
}

static void Section(const char* name) {
    printf("\n== %s ==\n", name);
}

static const char* kMaps[] = {
    "overworld", "town_havenbrook", "guild_hall",
    "house_elder", "house_inn", "house_inn_upper", "house_smith",
    "dungeon_emberfell_1", "dungeon_emberfell_2", "dungeon_barrow",
    "well_shallow", "well_deep",
    "whisperwood_trail", "mossvale", "fernhollow",
    "westwold", "brackenwood",
    "mossvale_lodge_hall", "mossvale_herbalist", "fernhollow_cottage", "fernhollow_college",
    "mossvale_weavers", "college_grounds", "college_training", "college_classroom",
    "dreamworld", "dreamworld_2", "dreamworld_3",
    "house_inn_cellar", "ice_spire_peak", "ashen_path", "dungeon_infernal",
};

int main(int argc, char** argv) {
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
    SkillTrees       trees;

    Check(sprites.Load("data/sprites.json"),        "data/sprites.json loads");
    Check(items.Load("data/items.json"),            "data/items.json loads");
    items.Load("data/items_armour.json", false);   // optional armour pack
    Check(items.LoadTiers("data/tiers.json"),       "data/tiers.json loads");
    Check(items.LoadEnchantments("data/enchantments.json"), "data/enchantments.json loads");
    Check(enemy_db.Load("data/enemies.json"),       "data/enemies.json loads");
    Check(loot.Load("data/loot_tables.json"),       "data/loot_tables.json loads");
    loot.Load("data/loot_tables_armour.json", false);  // optional armour drops
    Check(quests.LoadDefinitions("data/quests.json"), "data/quests.json loads");
    Check(dialogue.Load("data/dialogue.json"),      "data/dialogue.json loads");
    Check(projectiles.Load("data/projectiles.json"), "data/projectiles.json loads");
    Check(spells.Load("data/spells.json"),         "data/spells.json loads");
    Check(trees.Load("data/skill_trees.json"),     "data/skill_trees.json loads");

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
            if (!st.map_id.empty()) {
                Check(st.type == ObjectiveType::Kill, q.id + " location filter is on a kill stage");
                Check(fs::exists("maps/" + st.map_id + ".mx"),
                      q.id + " location filter names a real map");
            }
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
                Check(nearest >= 0.0f, string(id) + " portal '" + p.label + "' has a way back");
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
            // A board also posts every quest that names it as giver: the dailies.
            if (o.type == "board") quest_givers.insert(o.id);
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
    Section("world routes and usable locations can be reached");
    for (const char* id : {"overworld", "town_havenbrook",
                           "house_smith", "guild_hall", "house_elder", "house_inn",
                           "house_inn_upper", "mossvale_lodge_hall", "mossvale_herbalist",
                           "fernhollow_cottage", "fernhollow_college", "mossvale", "fernhollow", "whisperwood_trail", "dreamworld",
                           "mossvale_weavers", "college_grounds", "college_training", "college_classroom",
                           "dreamworld_2", "dreamworld_3",
                           "house_inn_cellar", "ice_spire_peak", "ashen_path"}) {
        Map room;
        if (!room.Load(string("maps/") + id + ".mx")) continue;

        constexpr float STEP = 8.0f;
        constexpr float REACH = 58.0f;             // World's INTERACT_RANGE
        const int cols = static_cast<int>(room.Width() / STEP);
        const int rows = static_cast<int>(room.Height() / STEP);
        const Player walker;
        auto feet = [&](float x, float y) {
            return SDL_FRect{x + walker.foot_box.x, y + walker.foot_box.y,
                             walker.foot_box.w, walker.foot_box.h};
        };

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
                const SDL_FPoint moved = room.MoveWithCollision(
                    feet(cx * STEP, cy * STEP), d[0] * STEP, d[1] * STEP);
                const SDL_FRect goal = feet(nx * STEP, ny * STEP);
                if (fabsf(moved.x - goal.x) > 0.1f || fabsf(moved.y - goal.y) > 0.1f) continue;
                // The overworld has climbable banks. A route may use a ramp
                // or the player's normal jump, but never an unclimbable cliff.
                if (room.LevelChangeBlocked(cx * STEP, cy * STEP, nx * STEP, ny * STEP) &&
                    std::abs(room.LevelAt(cx * STEP, cy * STEP) -
                             room.LevelAt(nx * STEP, ny * STEP)) > Player::CLIMB_LEVELS) continue;
                f = 1;
                todo.push_back({nx, ny});
            }
        }

        auto reachable = [&](float x, float y) {
            // Only the cells within reach can matter.
            const int r = static_cast<int>(REACH / STEP) + 1;
            const int x0 = std::max(0, static_cast<int>(x / STEP) - r), x1 = std::min(cols - 1, static_cast<int>(x / STEP) + r);
            const int y0 = std::max(0, static_cast<int>(y / STEP) - r), y1 = std::min(rows - 1, static_cast<int>(y / STEP) + r);
            for (int cy = y0; cy <= y1; ++cy)
                for (int cx = x0; cx <= x1; ++cx)
                    if (seen[static_cast<size_t>(cy) * cols + cx] &&
                        Length(cx * STEP - x, cy * STEP - y) <= REACH)
                        return true;
            return false;
        };

        for (const NpcDef& n : room.Npcs())
            Check(reachable(n.x, n.y), string(id) + ": " + n.name + " can be walked up to");
        for (const MapObject& o : room.Objects()) {
            // Preserve the existing gathering layout; this pass adds route
            // coverage to the old overworld rather than auditing every tree.
            if (string(id) == "overworld" && o.id != "sign_trailhead" && o.type != "herb") continue;
            // The flood walks but does not jump, so it never climbs onto the
            // overworld's plateaus -- the player does, and "no raised ground is
            // sealed off" proves it. A herb up there only has to stand on open
            // ground.
            // So does one in a hollow ringed by plateaus, which is reached by
            // climbing over them.
            bool ringed = false;
            if (string(id) == "overworld" && o.type == "herb")
                for (float a = 0.0f; a < 6.28f && !ringed; a += 0.5f)
                    for (float d = 64.0f; d <= 256.0f && !ringed; d += 64.0f)
                        if (room.LevelAt(o.x + cosf(a) * d, o.y + sinf(a) * d) > 0) ringed = true;
            if (string(id) == "overworld" && o.type == "herb" &&
                (room.LevelAt(o.x, o.y) > 0 || (ringed && !reachable(o.x, o.y)))) {
                bool open = false;
                for (float a = 0.0f; a < 6.28f && !open; a += 0.4f)
                    if (!room.Blocked(feet(o.x + cosf(a) * 30.0f, o.y + sinf(a) * 30.0f))) open = true;
                Check(open, string(id) + ": " + o.id + " stands on open raised ground");
                continue;
            }
            Check(reachable(o.x, o.y), string(id) + ": " + o.id + " can be walked up to");
        }

        // Every way out, too. On an outdoor zone this is what proves the path
        // is actually a path: a forest dense enough to look right is also dense
        // enough to seal a trail off with one badly placed trunk.
        for (const Portal& portal : room.Portals()) {
            bool crossed = false;
            const int x0 = std::max(0, static_cast<int>((portal.rect.x - 24) / STEP));
            const int x1 = std::min(cols - 1, static_cast<int>((portal.rect.x + portal.rect.w + 24) / STEP));
            const int y0 = std::max(0, static_cast<int>((portal.rect.y - 24) / STEP));
            const int y1 = std::min(rows - 1, static_cast<int>((portal.rect.y + portal.rect.h + 48) / STEP));
            for (int y = y0; y <= y1 && !crossed; ++y)
                for (int x = x0; x <= x1 && !crossed; ++x) {
                    if (!seen[static_cast<size_t>(y) * cols + x]) continue;
                    const SDL_FRect box = portal.requires_interact
                        ? SDL_FRect{x * STEP + walker.body_box.x, y * STEP + walker.body_box.y,
                                    walker.body_box.w, walker.body_box.h}
                        : feet(x * STEP, y * STEP);
                    crossed = RectsOverlap(box, portal.rect) &&
                        (!portal.requires_interact ||
                         Length(x * STEP - (portal.rect.x + portal.rect.w / 2),
                                y * STEP - (portal.rect.y + portal.rect.h / 2)) <= REACH);
                }
            Check(crossed, string(id) + ": exit '" + portal.label +
                           "' can actually be triggered, not merely approached");
        }

        // Every named arrival must belong to that same connected floor.
        std::ifstream map_file(string("maps/") + id + ".mx");
        json authored;
        map_file >> authored;
        for (auto it = authored["dreamquest"]["spawns"].begin();
             it != authored["dreamquest"]["spawns"].end(); ++it) {
            const float x = it.value()[0].get<float>(), y = it.value()[1].get<float>();
            bool connected = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int cx = static_cast<int>(x / STEP) + dx;
                    const int cy = static_cast<int>(y / STEP) + dy;
                    if (cx < 0 || cy < 0 || cx >= cols || cy >= rows) continue;
                    if (!seen[static_cast<size_t>(cy) * cols + cx]) continue;
                    const SDL_FPoint moved = room.MoveWithCollision(feet(x, y), cx * STEP - x, cy * STEP - y);
                    const SDL_FRect goal = feet(cx * STEP, cy * STEP);
                    if (fabsf(moved.x - goal.x) < 0.1f && fabsf(moved.y - goal.y) < 0.1f) connected = true;
                }
            Check(connected && !room.Blocked(feet(x, y)),
                  string(id) + ": arrival '" + it.key() + "' connects to the route network");
        }
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
            // The ancient magic is a school, not an element: outside the cycle.
            if (e == Element::Arcane) continue;
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

    // --- two hands -------------------------------------------------------------
    Section("a bow takes both hands");
    {
        GameContext ctx;
        ctx.sprites = &sprites;  ctx.items = &items;
        const auto slot_of = [](Player& p, const string& id) {
            for (int s = 0; s < p.inventory.SlotCount(); ++s)
                if (p.inventory.Slot(s).id == id) return s;
            return -1;
        };
        Player p;
        p.Init(ctx, "player_hero");
        p.inventory.Add("bronze_sword", 1);
        p.inventory.Add("wooden_shield", 1);
        p.inventory.Add("training_bow", 1);
        string why;
        p.EquipFromInventory(slot_of(p, "bronze_sword"), why);
        p.EquipFromInventory(slot_of(p, "wooden_shield"), why);
        Check(p.equipment.InSlot(SLOT_SHIELD) == "wooden_shield", "sword and shield go on together");

        Check(p.EquipFromInventory(slot_of(p, "training_bow"), why), "a bow can be equipped over sword and shield");
        Check(p.equipment.InSlot(SLOT_WEAPON) == "training_bow", "the bow is in hand");
        Check(p.equipment.InSlot(SLOT_SHIELD).empty(), "the shield comes off for the bow");
        Check(p.inventory.Has("wooden_shield") && p.inventory.Has("bronze_sword"),
              "the shield and sword go back in the bag");

        Check(p.EquipFromInventory(slot_of(p, "wooden_shield"), why), "a shield can be taken up again");
        Check(p.equipment.InSlot(SLOT_WEAPON).empty() && p.inventory.Has("training_bow"),
              "taking up the shield puts the bow away");

        // No room for what would come off: refuse rather than lose it.
        p.EquipFromInventory(slot_of(p, "training_bow"), why);    // bow on, shield in bag
        Player full;
        full.Init(ctx, "player_hero");
        full.inventory.Add("training_bow", 1);
        full.inventory.Add("wooden_shield", 1);
        full.EquipFromInventory(slot_of(full, "wooden_shield"), why);
        full.inventory.Add("bronze_sword", 1);
        full.EquipFromInventory(slot_of(full, "bronze_sword"), why);   // sword + shield worn, bow in bag
        while (!full.inventory.Full()) full.inventory.Add("bronze_sword", 1);
        const bool refused = !full.EquipFromInventory(slot_of(full, "training_bow"), why);
        Check(refused && !why.empty(), "a full bag refuses a bow that would unseat the shield");
        Check(full.equipment.InSlot(SLOT_SHIELD) == "wooden_shield" &&
              full.equipment.InSlot(SLOT_WEAPON) == "bronze_sword", "and nothing moved");

        // Exactly full, with the shield's own slot the only room there is:
        // the bow must land in the space the shield leaves, not vanish.
        Player tight;
        tight.Init(ctx, "player_hero");
        tight.inventory.Add("training_bow", 1);
        tight.EquipFromInventory(slot_of(tight, "training_bow"), why);
        tight.inventory.Add("wooden_shield", 1);
        while (!tight.inventory.Full()) tight.inventory.Add("bronze_sword", 1);
        Check(tight.EquipFromInventory(slot_of(tight, "wooden_shield"), why),
              "a full bag can still swap a bow for the shield it holds");
        Check(tight.inventory.Has("training_bow") && tight.equipment.InSlot(SLOT_SHIELD) == "wooden_shield",
              "the bow takes the shield's place in the bag");
    }

    // --- crafting stations ----------------------------------------------------------
    Section("each recipe is made at the station that fits it");
    {
        const auto all = items.Recipes();
        const auto bench = items.Recipes(CraftStation::Workbench);
        const auto anvil = items.Recipes(CraftStation::Anvil);
        Check(!bench.empty() && !anvil.empty(), "both the workbench and the anvil have something to make");
        const auto cauldron = items.Recipes(CraftStation::Cauldron);
        const auto fire = items.Recipes(CraftStation::Range);
        const auto loom = items.Recipes(CraftStation::Loom);
        const auto rack = items.Recipes(CraftStation::Rack);
        Check(!cauldron.empty(), "and the cauldron has brews");
        Check(!fire.empty(), "and the fire has things to cook");
        Check(!loom.empty(), "and the loom has cloth to weave");
        Check(!rack.empty(), "and the tanning rack has hide to cut");
        Check(bench.size() + anvil.size() + cauldron.size() + fire.size() + loom.size() + rack.size() == all.size(),
              "every recipe belongs to exactly one station");
        for (const ItemDef* r : all) {
            bool metal = false, brewed = false;
            for (const auto& in : r->craft_inputs)
                if (const ItemDef* mat = items.Get(in.first)) {
                    metal |= mat->metal;
                    brewed |= std::find(mat->tags.begin(), mat->tags.end(), "brewing") != mat->tags.end();
                }
            // Woven is decided by what comes off it, not what goes in: a bag is
            // part cloth and still sewn, a dye is cloth's business and still
            // boiled.
            const ItemDef* out = items.Get(r->craft_result);
            const bool woven = out && std::find(out->tags.begin(), out->tags.end(), "cloth") != out->tags.end();
            // A recipe that says where it is made is made there: a stew has
            // mint in it and is still not a potion.
            const bool at_fire = std::find(fire.begin(), fire.end(), r) != fire.end();
            Check(at_fire == (r->craft_at == "range"),
                  r->craft_result + (at_fire ? " is cooked at a fire, because it says so" : " is not cooked"));
            if (at_fire) continue;
            const bool at_cauldron = std::find(cauldron.begin(), cauldron.end(), r) != cauldron.end();
            Check(brewed == at_cauldron, r->craft_result + (brewed ? " is brewed at a cauldron" : " is not brewed"));
            if (brewed) continue;
            const bool at_loom = std::find(loom.begin(), loom.end(), r) != loom.end();
            Check(woven == at_loom, r->craft_result + (woven ? " is cloth, so it is woven at a loom"
                                                             : " is not cloth, so it is not woven"));
            if (woven) continue;
            // Leather the same way, by what comes off it: a banded jerkin has
            // iron in it and is still cut by a tanner, and a Barkwood Helm has a
            // hide in it and is still wood.
            const bool leather = out && std::find(out->tags.begin(), out->tags.end(), "leather") != out->tags.end();
            const bool at_rack = std::find(rack.begin(), rack.end(), r) != rack.end();
            Check(leather == at_rack, r->craft_result + (leather ? " is leather, so it is cut on a tanning rack"
                                                                 : " is not leather, so it is not"));
            if (leather) continue;
            const bool at_anvil = std::find(anvil.begin(), anvil.end(), r) != anvil.end();
            Check(metal == at_anvil, r->craft_result + (metal ? " needs metal, so it is smithed at the anvil"
                                                              : " needs no metal, so it is made at a workbench"));
        }
        const auto made_at = [&](const string& result, CraftStation st) {
            for (const ItemDef* r : items.Recipes(st)) if (r->craft_result == result) return true;
            return false;
        };
        Check(made_at("iron_shield", CraftStation::Anvil) && !made_at("iron_shield", CraftStation::Workbench),
              "the iron shield is only smithed");
        Check(made_at("copper_ring", CraftStation::Anvil), "the copper ring is smithed");
        for (const char* simple : {"wooden_shield", "wood_helm", "oak_shortbow"})
            Check(made_at(simple, CraftStation::Workbench) && !made_at(simple, CraftStation::Anvil) &&
                  !made_at(simple, CraftStation::Rack),
                  string(simple) + " is made at a workbench, not the anvil or the rack");
        // The loom took the cloth, and the tanning rack the leather: everything
        // worn that is cut from hide, in every tier, and what is sewn from it.
        Check(made_at("bolt_cloth", CraftStation::Loom) && !made_at("bolt_cloth", CraftStation::Workbench),
              "a bolt of cloth is only woven");
        for (const char* cut : {"leather_body", "hide_boots", "wood_hide_head", "wood_hide_body", "wood_hide_legs",
                                "bag_satchel", "bag_pack", "bedroll"})
            Check(made_at(cut, CraftStation::Rack) && !made_at(cut, CraftStation::Workbench) &&
                  !made_at(cut, CraftStation::Loom) && !made_at(cut, CraftStation::Anvil),
                  string(cut) + " is leather, so it is made on a tanning rack and nowhere else");
        {
            // Every piece of the ranger's set, in every tier, whatever else is in it.
            int pieces = 0, elsewhere = 0;
            for (const TierDef& t : items.Tiers())
                for (const char* piece : {"hide_head", "hide_body", "hide_legs"}) {
                    const string id = items.TierPiece(t.id, piece);
                    if (id.empty()) continue;
                    ++pieces;
                    elsewhere += !made_at(id, CraftStation::Rack);
                }
            Check(pieces >= 30 && elsewhere == 0, "all " + std::to_string(pieces) + " pieces of the ranger's hides are cut on the rack");
            // And none of the hero's plate or the mage's cloth has strayed onto it.
            bool strayed = false;
            for (const ItemDef* r : rack) {
                const ItemDef* made = items.Get(r->craft_result);
                strayed |= made && (made->armour_cut == "robe" || (made->slot != SLOT_NONE && made->armour_cut.empty() &&
                                                                   !made->tier.empty()));
            }
            Check(!strayed, "and no plate and no robe is");
        }
        for (const char* dyed : {"dye_marigold", "dye_brookmint"})
            Check(made_at(dyed, CraftStation::Cauldron) && !made_at(dyed, CraftStation::Loom),
                  string(dyed) + " is boiled, not woven");
        for (const char* ore : {"copper_ore", "iron_ore"})
            Check(items.Get(ore) && items.Get(ore)->metal, string(ore) + " counts as metal");
        for (const char* soft : {"logs", "oak_logs", "hide", "thread"})
            Check(items.Get(soft) && !items.Get(soft)->metal, string(soft) + " is not metal");

        // What stands in the world agrees with what it is called and drawn as.
        int anvils = 0, benches = 0, cauldrons = 0, looms = 0, racks = 0;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "workbench") continue;
                Check(o.station == "workbench" || o.station == "anvil" || o.station == "cauldron" ||
                      o.station == "loom" || o.station == "rack", o.id + " is a known crafting station");
                const bool drawn_as_anvil = o.sprite.find("anvil") != string::npos;
                const bool drawn_as_cauldron = o.sprite.find("cauldron") != string::npos;
                const bool drawn_as_loom = o.sprite.find("loom") != string::npos;
                const bool drawn_as_rack = o.sprite.find("tanning_rack") != string::npos;
                Check(drawn_as_anvil == (o.station == "anvil") && drawn_as_cauldron == (o.station == "cauldron") &&
                      drawn_as_loom == (o.station == "loom") && drawn_as_rack == (o.station == "rack"),
                      o.id + " works as the station it looks like");
                if (o.station == "anvil") ++anvils;
                else if (o.station == "workbench") ++benches;
                else if (o.station == "loom") ++looms;
                else if (o.station == "rack") ++racks;
                else ++cauldrons;
            }
        }
        Check(anvils >= 1, "there is an anvil somewhere to smith at");
        Check(benches >= 1, "there is a workbench somewhere to make simple things");
        Check(cauldrons >= 3, "there are cauldrons to brew at (" + std::to_string(cauldrons) + ")");
        Check(looms >= 1, "and a loom to weave at (" + std::to_string(looms) + ")");
        Check(racks >= 3, "and tanning racks to cut hide on (" + std::to_string(racks) + ")");

        // A tanner's station is the tanner's own. Both yards had a carpenter's
        // bench standing among the frames, and the frames were scenery.
        for (const auto& yard : {std::pair<const char*, const char*>{"town_havenbrook", "npc_nessa"},
                                 std::pair<const char*, const char*>{"westwold", "npc_orla"}}) {
            Map m;
            if (!m.Load(string("maps/") + yard.first + ".mx")) continue;
            const NpcDef* tanner = nullptr;
            for (const NpcDef& n : m.Npcs()) if (n.id == yard.second) tanner = &n;
            Check(tanner != nullptr, string(yard.second) + " keeps a yard");
            if (!tanner) continue;
            int near_racks = 0, near_benches = 0, scenery = 0;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "workbench" || std::hypot(o.x - tanner->x, o.y - tanner->y) > 260.0f) continue;
                near_racks += o.station == "rack";
                near_benches += o.station == "workbench";
            }
            json mx;
            { std::ifstream in(string("maps/") + yard.first + ".mx"); in >> mx; }
            if (mx["tiles"].contains("tanning_rack")) scenery = static_cast<int>(mx["tiles"]["tanning_rack"]["locations"].size());
            Check(near_racks >= 3, tanner->name + " has frames to work at (" + std::to_string(near_racks) + ")");
            Check(near_benches == 0, tanner->name + " has no carpenter's bench among them");
            Check(scenery == 0, tanner->name + "'s frames are all of them the station, and none of them scenery");
        }
        // And everything in Nessa's book can be made in her yard.
        {
            int orders = 0, elsewhere = 0;
            for (const auto& kv : quests.Definitions()) {
                const QuestDef& d = kv.second;
                if (d.pool != "nessa_orders" || d.stages.empty()) continue;
                ++orders;
                if (!made_at(d.stages[0].target, CraftStation::Rack)) { ++elsewhere; Check(false, d.name + " asks for something her frames cannot make"); }
            }
            Check(orders >= 10 && elsewhere == 0, "all " + std::to_string(orders) + " orders in Nessa's book are made on her own frames");
        }

        // Each station trains its own skill, and smithing a tier asks for the
        // same level as its tier: the level its gear needs to be worn.
        Check(CraftSkill(CraftStation::Workbench) == SKILL_CRAFTING && CraftSkill(CraftStation::Anvil) == SKILL_SMITHING &&
              CraftSkill(CraftStation::Cauldron) == SKILL_BREWING, "workbench, anvil and cauldron train Crafting, Smithing and Brewing");
        Check(CraftSkill(CraftStation::Loom) == SKILL_CRAFTING,
              "and the loom trains Crafting too: two stations, one trade");
        Check(CraftStationFromName("loom") == CraftStation::Loom &&
              string(CraftStationName(CraftStation::Loom)) == "loom",
              "a map that says \"loom\" gets one");
        Check(CraftSkill(CraftStation::Rack) == SKILL_CRAFTING && CraftStationFromName("rack") == CraftStation::Rack &&
              string(CraftStationName(CraftStation::Rack)) == "rack",
              "the rack trains Crafting as well, and a map that says \"rack\" gets one");
        for (const TierDef& t : items.Tiers()) {
            if (t.wood) continue;
            for (const char* piece : {"sword", "spear", "bow", "staff", "shield", "helm", "body", "legs", "axe", "pickaxe"}) {
                const string id = items.TierPiece(t.id, piece);
                for (const ItemDef* r : anvil)
                    if (r->craft_result == id)
                        Check(r->craft_level == t.level, id + " is smithed at Smithing " + std::to_string(t.level) +
                              ", its tier's level");
            }
            for (const ItemDef* r : anvil)
                if (r->craft_result == t.bar)
                    Check(r->craft_level == t.level, t.bar + " is smelted at Smithing " + std::to_string(t.level));
        }
    }

    // --- dangerous doors -----------------------------------------------------------
    Section("dungeon doors warn a new character");
    {
        Map ow;
        Check(ow.Load("maps/overworld.mx"), "overworld loads for its doors");
        Skills fresh;
        for (const char* target : {"dungeon_emberfell_1", "dungeon_barrow"}) {
            const Portal* door = nullptr;
            for (const Portal& p : ow.Portals()) if (p.target_map == target) door = &p;
            Check(door && door->danger_level > fresh.CombatLevel(),
                  string("the door to ") + target + " is marked beyond a new character");
        }
        for (const Portal& p : ow.Portals())
            if (p.target_map == "town_havenbrook" || p.target_map == "whisperwood_trail")
                Check(p.danger_level == 0, "the way to " + p.target_map + " carries no warning");
    }

    // --- sound -------------------------------------------------------------------
    Section("sound");
    {
        Check(!Audio::Enabled(), "sound is off until it is started");
        Audio::Play(Sfx::Hit);                         // must be harmless
        Check(Audio::ActiveVoices() == 0, "playing before start does nothing");

        Audio::InitOffline();
        for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
            const auto& b = Audio::Samples(static_cast<Sfx>(i));
            float peak = 0.0f;
            bool finite = true;
            for (float v : b) { peak = std::max(peak, std::fabs(v)); finite &= std::isfinite(v); }
            const string name = "sound " + std::to_string(i);
            Check(b.size() > 400, name + " is long enough to hear");
            Check(b.size() < 44100u * 3u, name + " is short enough to be an effect");
            Check(finite, name + " has no NaN or infinity");
            Check(peak > 0.05f && peak <= 0.7f, name + " is audible and well under clipping");
        }

        // Each ambience, mixed for a few seconds: never silent outdoors, never
        // loud, never broken, and quiet again once the ambience is cleared.
        const auto listen = [&](const string& kind, bool interior, float seconds) {
            Audio::SetAmbience(kind, interior);
            Audio::SetVolumes(0.8f, 1.0f, 0.8f);
            vector<float> out(512 * 2);
            float peak = 0.0f, energy = 0.0f; bool finite = true; long n = 0;
            for (int f = 0; f < static_cast<int>(44100 * seconds); f += 512) {
                Audio::Mix(out.data(), 512);
                if (f < 44100 * 2) continue;          // let the layers fade in
                for (float v : out) { peak = std::max(peak, std::fabs(v)); energy += v * v; finite &= std::isfinite(v); ++n; }
            }
            return std::tuple<float, float, bool>(peak, n ? std::sqrt(energy / n) : 0.0f, finite);
        };
        for (const char* kind : {"forest", "grove", "town", "overworld", "dungeon", "dream"}) {
            auto [peak, rms, finite] = listen(kind, false, 8.0f);
            Check(finite, string(kind) + " ambience has no NaN");
            Check(rms > 0.004f, string(kind) + " ambience is audible");
            Check(peak < 0.6f, string(kind) + " ambience stays in the background");
        }
        {
            auto [peak, rms, finite] = listen("town", true, 8.0f);
            Check(finite && rms > 0.002f && peak < 0.5f, "a house has a quiet hearth");
        }
        {
            // Night outdoors: the birds stop and the crickets take over, and
            // the air is no louder for it.
            Audio::SetNight(1.0f);
            auto [peak, rms, finite] = listen("overworld", false, 8.0f);
            Check(finite && rms > 0.004f && peak < 0.6f, "a night outdoors is audible and stays in the background");
            Audio::SetNight(0.0f);
        }
        {
            auto [peak, rms, finite] = listen("", false, 10.0f);
            Check(finite && rms < 0.003f, "clearing the ambience fades it out");
        }

        // A crowd of effects at once is limited, not clipped.
        for (int i = 0; i < 40; ++i) Audio::Play(Sfx::HitCrit, 1.0f);
        Check(Audio::ActiveVoices() <= 32, "voices are capped");
        vector<float> out(2048 * 2);
        Audio::Mix(out.data(), 2048);
        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        Check(peak <= 1.0f, "forty hits at once stay inside full scale");
        Audio::Shutdown();
        Check(Audio::ActiveVoices() == 0, "shutting down silences everything");
    }

    // --- the title screen and the application icon -----------------------------
    Section("title art and icons");
    {
        // The cover painting is the game's own art, so unlike everything under
        // assets/ it is in the repository and the menus can count on it.
        Check(fs::exists(TitleScreen::kArtPath),
              string("the title painting is in the repository: ") + TitleScreen::kArtPath);
        Check(fs::exists(TitleScreen::kIconPath),
              string("the window icon is beside it: ") + TitleScreen::kIconPath);
        Check(fs::exists("art/dreamquest.ico"), "the .exe's icon is built and committed");

        if (SDL_Surface* icon = IMG_Load(TitleScreen::kIconPath)) {
            Check(icon->w == icon->h, "the window icon is square");
            Check(icon->w >= 256, "the window icon is at least 256px, for a hi-dpi taskbar");
            SDL_DestroySurface(icon);
        } else {
            Check(false, "the window icon loads");
        }

        // An .ico Windows will actually use: the header says icon, and the
        // sizes it lists include the 16 and 32 Explorer asks for.
        std::ifstream ico("art/dreamquest.ico", std::ios::binary);
        vector<unsigned char> bytes((std::istreambuf_iterator<char>(ico)),
                                    std::istreambuf_iterator<char>());
        const bool header = bytes.size() > 6 && bytes[0] == 0 && bytes[1] == 0 &&
                            bytes[2] == 1 && bytes[3] == 0;
        Check(header, "the .ico has an icon header");
        int entries = header ? bytes[4] | (bytes[5] << 8) : 0;
        bool has16 = false, has32 = false, has256 = false;
        for (int i = 0; i < entries && 6 + 16 * (i + 1) <= static_cast<int>(bytes.size()); ++i) {
            const int w = bytes[6 + 16 * i];
            if (w == 16) has16 = true;
            if (w == 32) has32 = true;
            if (w == 0)  has256 = true;      // 0 means 256 in an icon directory
        }
        Check(has16 && has32, "the .ico carries the 16 and 32px sizes Explorer asks for");
        Check(has256, "and a 256px one for large icon views");

        // The resource script the build compiles has to name that file, or the
        // .exe ends up with the toolchain's default icon and nobody notices.
        std::ifstream rc("tools/appicon.rc");
        const string rc_text((std::istreambuf_iterator<char>(rc)),
                             std::istreambuf_iterator<char>());
        Check(rc_text.find("art/dreamquest.ico") != string::npos,
              "tools/appicon.rc points at the icon that is built");
    }

    // --- the HUD --------------------------------------------------------------
    Section("hud fittings");
    {
        for (const char* art : {"assets/ui/minimap_ring.png",
                                "assets/icons/hud_heart.png",
                                "assets/icons/hud_drop.png",
                                "assets/icons/hud_stamina.png",
                                "assets/icons/hud_sun.png", "assets/icons/hud_moon.png"})
            Check(fs::exists(art), string("hud art on disk: ") + art);

        // The minimap draws the world as a stack of rows clipped to the baked
        // image. Standing in a corner of the world, most of those rows hang off
        // an edge, and getting this wrong smears the last column of the map
        // across the glass.
        struct Case { float src, dst, width; int img; float want_src, want_dst, want_width; };
        const Case cases[] = {
            //  src   dst   width  img    expected
            {  10.0f, 100.0f, 20.0f, 200,  10.0f, 100.0f,  20.0f },   // fully inside
            {  -5.0f, 100.0f, 20.0f, 200,   0.0f, 105.0f,  15.0f },   // off the west edge
            { 190.0f, 100.0f, 20.0f, 200, 190.0f, 100.0f,  10.0f },   // off the east edge
            {  -5.0f, 100.0f, 30.0f,  20,   0.0f, 105.0f,  20.0f },   // map narrower than the glass
            { -40.0f, 100.0f, 20.0f, 200,   0.0f, 140.0f, -20.0f },   // entirely west of the map
            { 210.0f, 100.0f, 20.0f, 200, 210.0f, 100.0f, -10.0f },   // entirely east of it
        };
        bool clipped = true, nothing_drawn = true;
        for (const Case& c : cases) {
            float src = c.src, dst = c.dst;
            const float w = MinimapClipSpan(src, dst, c.width, c.img);
            if (fabsf(src - c.want_src) > 0.001f || fabsf(dst - c.want_dst) > 0.001f ||
                fabsf(w - c.want_width) > 0.001f) clipped = false;
            // Whatever it returns, it must never ask for pixels the image does
            // not have.
            if (w > 0.0f && (src < 0.0f || src + w > static_cast<float>(c.img)))
                nothing_drawn = false;
        }
        Check(clipped, "minimap rows clip to the edges of the map");
        Check(nothing_drawn, "minimap never samples outside the baked image");
    }

    // --- the hero's sprite set ---------------------------------------------------
    // --- worn plate ------------------------------------------------------------
    // --- what a clone has -------------------------------------------------------
    Section("nothing depends on the optional packs");
    {
        // data/items_armour.json is written by the importer from four optional
        // CraftPix icon packs. This machine may have it; a clone does not. So
        // nothing in the committed data may name one of those items directly --
        // the dungeon chests reach them through the armour_cache table, which
        // the importer fills and which is empty without it.
        //
        // This is here because a fresh clone once failed on exactly that: the
        // barrow wight dropped steel greaves and the thing in the spring
        // dropped sabatons, and both look fine on the machine that imported the
        // packs.
        std::ifstream f("data/items_armour.json");
        std::set<string> optional;
        if (f) {
            json j;
            try { f >> j; } catch (...) { j = json::object(); }
            for (auto it = j.begin(); it != j.end(); ++it) optional.insert(it.key());
        }
        Check(true, "checked the optional item list" + string(optional.empty() ? " (not installed)" : ""));

        const auto committed = [&](const char* path) {
            std::ifstream in(path);
            return string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        struct Where { const char* path; const char* what; };
        const Where files[] = {
            {"data/loot_tables.json", "a loot table"},
            {"data/shops.json",       "a shop"},
            {"data/quests.json",      "a quest reward"},
            {"data/dialogue.json",    "a line of dialogue"},
        };
        int named = 0;
        for (const Where& w : files) {
            const string text = committed(w.path);
            if (text.empty()) continue;
            for (const string& id : optional) {
                // Quoted, so "steel_legs" does not match inside "steel_legspan".
                if (text.find("\"" + id + "\"") != string::npos) {
                    ++named;
                    Check(false, string(w.what) + " names '" + id +
                                 "', which only exists once the optional packs are imported");
                }
            }
        }
        Check(named == 0, "the committed data names no item a clone would not have");

        // And the indirection those drops are supposed to use is present and
        // resolvable whether or not the packs are.
        Check(loot.Has("armour_cache"), "armour_cache is defined for a clone to find");
    }

    Section("armour is drawn, a piece at a time");
    {
        // Every playable character has all five plate layers for every clip,
        // and they are on disk. Without these the armour silently does nothing.
        for (const char* who : {"player_hero", "player_warden", "player_wayfarer"}) {
            const SpriteDef* def = sprites.Get(who);
            Check(def != nullptr, string(who) + " is defined");
            if (!def) continue;
            for (const char* clip : {"idle", "walk", "run", "attack", "hurt", "death"}) {
                const AnimClip* c = def->Find(clip);
                if (!c) { Check(false, string(who) + " has a " + clip + " clip"); continue; }
                bool legs = false, body = false, hands = false, head = false, shield = false;
                bool on_disk = true;
                for (const AnimLayer& l : c->layers) {
                    legs   |= l.slot == LayerSlot::ArmourLegs;
                    body   |= l.slot == LayerSlot::ArmourBody;
                    hands  |= l.slot == LayerSlot::ArmourHands;
                    head   |= l.slot == LayerSlot::ArmourHead;
                    shield |= l.slot == LayerSlot::ArmourShield;
                    if (ArmourLayerOf(l.slot) >= 0) on_disk &= fs::exists(l.sheet);
                }
                Check(legs && body && hands && head && shield,
                      string(who) + "'s " + clip + " has all five plate layers");
                Check(on_disk, string(who) + "'s " + clip + " plate sheets are on disk");
            }
        }

        // The plate is drawn after the character, so a helm covers the hair
        // rather than the other way round.
        if (const SpriteDef* hero = sprites.Get("player_hero")) {
            if (const AnimClip* c = hero->Find("idle")) {
                int body_at = -1, head_at = -1, plate_at = -1;
                for (size_t i = 0; i < c->layers.size(); ++i) {
                    if (c->layers[i].slot == LayerSlot::Body) body_at = static_cast<int>(i);
                    if (c->layers[i].slot == LayerSlot::Head) head_at = static_cast<int>(i);
                    if (ArmourLayerOf(c->layers[i].slot) >= 0 && plate_at < 0)
                        plate_at = static_cast<int>(i);
                }
                Check(body_at >= 0 && head_at > body_at && plate_at > head_at,
                      "plate is drawn over the body and the head");
            }
        }

        // Layer names map to slots, and the tier weapon sheets -- which all sit
        // at the weapon's own index -- are recognised as alternates rather than
        // as layers of their own. Drawing them all is what used to put a sword,
        // a spear, a bow and a staff in the same hand at once.
        Check(LayerSlotFromName("armour_body") == LayerSlot::ArmourBody, "armour_body names the body plate");
        Check(LayerSlotFromName("armour_head") == LayerSlot::ArmourHead, "armour_head names the helm");
        Check(LayerSlotFromName("weapon_front") == LayerSlot::WeaponFront, "weapon_front is the weapon layer");
        Check(LayerSlotFromName("weapon_sword_iron") == LayerSlot::WeaponAlt,
              "a tier weapon sheet is an alternate, not a layer");
        Check(LayerSlotFromName("weapon_bow_wood") == LayerSlot::WeaponAlt,
              "so is a bow's");
        Check(LayerSlotFromName("armour_body_light") == LayerSlot::ArmourAlt,
              "the light cut of the cuirass is an alternate, not a second cuirass");
        Check(LayerSlotFromName("armour_head_ornate") == LayerSlot::ArmourAlt,
              "and so is the horned helm");
        Check(ArmourLayerOf(LayerSlot::ArmourAlt) < 0,
              "an alternate cut paints no layer of its own");
        Check(ArmourLayerOf(LayerSlot::Body) < 0, "the body layer is not plate");

        // Every tier's helm, cuirass, greaves and shield paints the layer for
        // its own slot; weapons and tools paint none.
        struct Piece { const char* suffix; const char* layer; EquipSlot slot; };
        const Piece pieces[] = {
            {"_helm", "head", SLOT_HEAD}, {"_body", "body", SLOT_BODY},
            {"_legs", "legs", SLOT_LEGS},
        };
        int checked = 0;
        for (const char* tier : {"bronze", "iron", "steel", "azuryte", "damascus",
                                 "orichalcum", "diamond", "platinum", "demonite",
                                 "dracon", "enchanted"}) {
            for (const Piece& piece : pieces) {
                const ItemDef* d = items.Get(string(tier) + piece.suffix);
                if (!d) continue;
                ++checked;
                Check(d->armour_layer == piece.layer,
                      string(tier) + piece.suffix + " paints the " + piece.layer + " plate");
                Check(d->slot == piece.slot, string(tier) + piece.suffix + " is worn on that slot");
            }
            if (const ItemDef* w = items.Get(string(tier) + "_sword"))
                Check(w->armour_layer.empty(), string(tier) + " sword paints no plate");
        }
        Check(checked >= 20, "every metal tier has its three plate pieces");

        // And the whole point: a mismatched set draws as the mismatch. Each
        // slot turns on its own layer in its own metal.
        {
            Player p;
            GameContext ctx;
            ctx.items = &items; ctx.sprites = &sprites; ctx.trees = &trees;
            p.Init(ctx, "player_hero");
            p.equipment.SetDatabase(&items);
            p.equipment.Equip(SLOT_HEAD, "bronze_helm");
            p.equipment.Equip(SLOT_BODY, "iron_body");
            p.equipment.Equip(SLOT_LEGS, "steel_legs");

            const LayerStyle s = p.BuildLayerStyle(&items);
            Check(s.armour[ARMOUR_HEAD].show && s.armour[ARMOUR_BODY].show &&
                  s.armour[ARMOUR_LEGS].show, "three worn pieces turn on three plate layers");
            Check(!s.armour[ARMOUR_SHIELD].show, "an empty off hand draws no shield");
            const ItemDef* bronze = items.Get("bronze_helm");
            const ItemDef* iron   = items.Get("iron_body");
            Check(bronze && s.armour[ARMOUR_HEAD].tint.r == bronze->tint.r &&
                  s.armour[ARMOUR_HEAD].tint.g == bronze->tint.g,
                  "the helm is painted its own metal");
            Check(iron && s.armour[ARMOUR_BODY].tint.r == iron->tint.r,
                  "the cuirass is painted its own, not an average of the set");
            Check(!(s.armour[ARMOUR_HEAD].tint.r == s.armour[ARMOUR_BODY].tint.r &&
                    s.armour[ARMOUR_HEAD].tint.g == s.armour[ARMOUR_BODY].tint.g &&
                    s.armour[ARMOUR_HEAD].tint.b == s.armour[ARMOUR_BODY].tint.b),
                  "bronze over iron is drawn as two metals");
            // Plate has art of its own, so the skin underneath is not washed in
            // the armour's colour any more.
            Check(s.body.r == 255 && s.body.g == 255 && s.body.b == 255,
                  "a character in plate keeps their own colouring underneath");
        }
    }

    Section("armour comes in three cuts, not one in twelve colours");
    {
        // A tier's "cut" decides which sheets its plate is drawn from. Plate is
        // the one the sheets are named after, so it carries no suffix at all --
        // an item whose cut is "plate" must end up with an empty armour_cut or
        // the engine would look for a sheet that was never rendered.
        struct Cut { const char* tier; const char* cut; };
        const Cut kCuts[] = {
            {"wood", "light"}, {"bronze", "light"}, {"iron", "light"},
            {"steel", ""}, {"azuryte", ""}, {"damascus", ""}, {"orichalcum", ""},
            {"diamond", ""}, {"platinum", ""},
            {"demonite", "ornate"}, {"dracon", "ornate"}, {"enchanted", "ornate"},
        };
        std::set<string> cuts_used;
        for (const Cut& c : kCuts) {
            const ItemDef* d = items.Get(string(c.tier) + "_body");
            if (!d) { Check(false, string(c.tier) + " has a cuirass"); continue; }
            Check(d->armour_cut == c.cut,
                  string(c.tier) + " plate is cut " + (c.cut[0] ? c.cut : "plain"));
            if (!d->armour_cut.empty()) cuts_used.insert(d->armour_cut);
            if (const ItemDef* w = items.Get(string(c.tier) + "_sword"))
                Check(w->armour_cut.empty(), string(c.tier) + " sword has no cut of armour");
        }
        Check(cuts_used.size() == 2, "both alternate cuts are worn by some tier");

        // Each alternate cut has its own sheets on disk, for every character
        // and every clip -- a missing one falls back to plate silently, which
        // is exactly the kind of thing nobody notices for a month.
        for (const char* who : {"player_hero", "player_warden", "player_wayfarer"}) {
            const SpriteDef* def = sprites.Get(who);
            if (!def) continue;
            for (const char* cut : {"light", "ornate"}) {
                int found = 0, missing = 0;
                for (const char* clip : {"idle", "walk", "run", "sprint", "attack",
                                         "jump", "hurt", "death"}) {
                    const AnimClip* c = def->Find(clip);
                    if (!c) continue;
                    for (const AnimLayer& l : c->layers) {
                        if (ArmourLayerOf(l.slot) < 0) continue;
                        string path = l.sheet;
                        const size_t dot = path.rfind(".png");
                        if (dot == string::npos) continue;
                        path.insert(dot, string("_") + cut);
                        if (fs::exists(path)) ++found; else ++missing;
                    }
                }
                Check(missing == 0 && found >= 40,
                      string(who) + " has every " + cut + " plate sheet");
            }
        }

        // And the cut reaches the draw: a bronze jerkin over a demonite
        // warplate is two cuts as well as two metals.
        {
            Player p;
            GameContext ctx;
            ctx.items = &items; ctx.sprites = &sprites; ctx.trees = &trees;
            p.Init(ctx, "player_hero");
            p.equipment.SetDatabase(&items);
            p.equipment.Equip(SLOT_HEAD, "bronze_helm");
            p.equipment.Equip(SLOT_BODY, "demonite_body");
            p.equipment.Equip(SLOT_LEGS, "steel_legs");
            const LayerStyle st = p.BuildLayerStyle(&items);
            Check(st.armour[ARMOUR_HEAD].cut == "light", "a bronze helm is drawn as a cap");
            Check(st.armour[ARMOUR_BODY].cut == "ornate", "a demonite cuirass is drawn horned");
            Check(st.armour[ARMOUR_LEGS].cut.empty(), "steel greaves are drawn as plain plate");
        }
    }

    Section("every creature stands at the same height in all four facings");
    {
        // A sprite sheet is a grid: one row per facing, one column per frame,
        // every cell the same square. What is drawn in each cell has to line up
        // with the cell, and the way to tell that it does not is to measure
        // where the creature's feet land in each row and compare that pattern
        // between clips of the same creature.
        //
        // This is here because every hurt clip in the game was wrong. A hurt is
        // three frames against four facings, so its sheet is taller than it is
        // wide -- and the render camera's ortho_scale was left on AUTO, which
        // means "the longer side of the image". Every other clip has at least
        // four frames and rendered correctly; the three-frame ones were scaled
        // to their height instead of their width, so each row was drawn lower
        // in its cell than the one above it and the bottom row's feet hung out
        // of the frame entirely.
        const auto row_feet = [](const string& path, int rows, int frames) {
            vector<int> out(rows, 0);
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return out;
            SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
            if (!c) return out;
            const int fw = c->w / std::max(1, frames);
            const int fh = c->h / std::max(1, rows);
            const Uint8* px = static_cast<const Uint8*>(c->pixels);
            for (int r = 0; r < rows; ++r)
                for (int y = fh - 1; y >= 0; --y) {
                    bool any = false;
                    for (int x = 0; x < fw && !any; ++x)
                        any = px[(r * fh + y) * c->pitch + x * 4 + 3] > 0;
                    if (any) { out[r] = y; break; }
                }
            SDL_DestroySurface(c);
            return out;
        };

        int checked = 0, worst = 0;
        string worst_name;
        for (const string& id : sprites.Ids()) {
            const SpriteDef* def = sprites.Get(id);
            if (!def || def->rows < 2) continue;
            const AnimClip* idle = def->Find("idle");
            if (!idle || !fs::exists(idle->sheet)) continue;
            const vector<int> base = row_feet(idle->sheet, def->rows, idle->frames);
            // Only clips the creature is still standing up in: a death lies
            // down, and where it lies is the point of it.
            for (const char* name : {"walk", "run", "attack", "hurt"}) {
                const AnimClip* clip = def->Find(name);
                if (!clip || !fs::exists(clip->sheet)) continue;
                const vector<int> feet = row_feet(clip->sheet, def->rows, clip->frames);
                int off = 0;
                for (int r = 1; r < def->rows; ++r)
                    off = std::max(off, std::abs((feet[r] - feet[0]) - (base[r] - base[0])));
                ++checked;
                if (off > worst) { worst = off; worst_name = id + "/" + name; }
            }
        }
        Check(checked >= 60, "there are sheets to measure");
        Check(worst <= 6, "no clip's rows drift down the sheet  -  worst is " +
                          worst_name + " at " + std::to_string(worst) + "px");
    }

    Section("every creature stays inside its own frames");
    {
        // A sheet is a grid of cells and the engine draws exactly one cell.
        // Anything drawn across a cell's edge shows up twice: cut off in the
        // frame it belongs to, and as a stray scrap in the frame next door. The
        // Warchief is a head taller than the other two orcs and his topknot
        // crossed the top of the cell facing right and facing away -- in every
        // frame of every clip -- until he was stood lower in his frame, into
        // the empty strip under his feet (FRAME_DROP in blender_creatures.py).
        // Four-legged deaths, a falling bat and a lizardman's thrust did the
        // same; the renderer now measures each posed rig and slides it back
        // inside its cell (keep_in_cell), and this holds every sheet to it.
        const auto crosses_edge = [](const string& path, int rows, int frames, int& bad) {
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return false;
            SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
            if (!c) return false;
            const int fw = c->w / std::max(1, frames), fh = c->h / std::max(1, rows);
            const Uint8* px = static_cast<const Uint8*>(c->pixels);
            const auto lit = [&](int x, int y) { return px[y * c->pitch + x * 4 + 3] > 0; };
            for (int r = 0; r < rows; ++r)
                for (int f = 0; f < frames; ++f) {
                    const int x0 = f * fw, y0 = r * fh;
                    bool edge = false;
                    for (int i = 0; i < fw && !edge; ++i)
                        edge = lit(x0 + i, y0) || lit(x0 + i, y0 + fh - 1);
                    for (int i = 0; i < fh && !edge; ++i)
                        edge = lit(x0, y0 + i) || lit(x0 + fw - 1, y0 + i);
                    if (edge) ++bad;
                }
            SDL_DestroySurface(c);
            return true;
        };
        for (const string& id : sprites.Ids()) {
            const char* who = id.c_str();
            const SpriteDef* def = sprites.Get(who);
            if (!def || def->rows != 4) continue;
            for (const auto& kv : def->clips) {
                int bad = 0;
                const bool read = crosses_edge(kv.second.sheet, def->rows, kv.second.frames, bad);
                Check(read, string(who) + "/" + kv.first + " sheet loads");
                Check(bad == 0, string(who) + "/" + kv.first + " draws nothing across a cell edge  -  " +
                                std::to_string(bad) + " frames do");
            }
        }
    }

    Section("the hero is drawn in every gear");
    {
        const SpriteDef* hero = sprites.Get("player_hero");
        Check(hero != nullptr, "player_hero is defined");
        if (hero) {
            for (const char* clip : {"idle", "walk", "run", "sprint", "attack", "jump", "hurt", "death"}) {
                const AnimClip* c = hero->Find(clip);
                Check(c && c->frames >= 4, string("player_hero has a ") + clip + " clip");
                if (!c) continue;
                bool has_body = false, has_head = false, has_shadow = false, on_disk = true;
                for (const AnimLayer& l : c->layers) {
                    has_body   |= l.slot == LayerSlot::Body;
                    has_head   |= l.slot == LayerSlot::Head;
                    has_shadow |= l.slot == LayerSlot::Shadow;
                    on_disk    &= fs::exists(l.sheet);
                }
                Check(has_body && has_head && has_shadow, string(clip) + " is split into shadow, body and head");
                Check(on_disk, string(clip) + " layer sheets are on disk");
            }
            const AnimClip* run = hero->Find("run");
            const AnimClip* sprint = hero->Find("sprint");
            Check(run && sprint && sprint->fps > run->fps, "the sprint cycles faster than the run");
        }

        // Worn armour is placed against the CraftPix rig's box, so the hero has
        // to stand in it: the head in the same band, the feet not far below.
        const auto bounds = [](const string& path, int row) {
            SDL_Rect box{64, 64, -1, -1};   // x0, y0, x1, y1
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return box;
            SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
            if (!c) return box;
            const Uint8* px = static_cast<const Uint8*>(c->pixels);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    if (px[(row * 64 + y) * c->pitch + x * 4 + 3] > 0) {
                        box.x = std::min(box.x, x); box.y = std::min(box.y, y);
                        box.w = std::max(box.w, x); box.h = std::max(box.h, y);
                    }
            SDL_DestroySurface(c);
            return box;
        };
        const string dir = "assets/characters/player_hero/layers/";
        const SDL_Rect head = bounds(dir + "idle_5_head.png", 0);
        const SDL_Rect body = bounds(dir + "idle_3_body.png", 0);
        Check(head.y >= 19 && head.y <= 25, "the hero's head starts where the CraftPix head does");
        Check(head.x >= 22 && head.w <= 42, "the hero's head is as wide as the rig's, give or take");
        Check(body.h >= 42 && body.h <= 50, "the hero's feet are within a few pixels of the rig's");
    }

    // --- leaders' heavy attacks --------------------------------------------------------
    Section("leaders wind up a heavy attack no shield stops");
    {
        static const char* kLeaders[] = {"orc3", "nightmare_brute", "broodmother", "lizardman_chief",
                                         "wyvern", "wyvern_matriarch", "barrow_wight", "well_warden",
                                         "pit_lord", "frost_dragon"};
        bool leaders_have = true;
        for (const char* id : kLeaders) {
            const EnemyDef* d = enemy_db.Get(id);
            leaders_have &= d && d->heavy.enabled && d->heavy.windup >= 1.0f && d->heavy.damage >= 2.0f &&
                            d->heavy.cooldown >= 6.0f;
        }
        Check(leaders_have, "the Warchief, the wyverns, the dragon and every other leader have a heavy attack");
        bool ordinary_do_not = true;
        for (const char* id : {"orc1", "orc2", "deer", "rat", "lizardman", "ice_troll", "imp", "skeleton"})
            if (const EnemyDef* d = enemy_db.Get(id)) ordinary_do_not &= !d->heavy.enabled;
        Check(ordinary_do_not, "ordinary monsters do not");

        Input input;
        std::mt19937 rng(23);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the Warchief");
        w.enemies.clear();
        w.player.y -= 200.0f;
        {
            LevelUp up;
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(90), up);
            w.player.Rest();
        }
        const auto frames = [&](int n) {
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
        };

        // A Warchief with no wait before his first heavy, so the test does not
        // have to sit through his ordinary swings to see it.
        EnemyDef warchief = *enemy_db.Get("orc3");
        warchief.heavy.opening = 0.0f;
        const auto spawn_warchief = [&](float dx, float dy) -> Enemy* {
            w.enemies.clear();
            EnemySpawnDef def;
            def.type = "orc3"; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(&warchief, def, ctx);
            Enemy* raw = e.get();
            w.enemies.push_back(std::move(e));
            return raw;
        };
        const auto wait_for_charge = [&](Enemy* e) {
            for (int f = 0; f < 60 && !e->ChargingHeavy(); ++f) frames(1);
            return e->ChargingHeavy();
        };

        // --- the wind-up, the bar, and the blow --------------------------------
        Enemy* orc = spawn_warchief(40.0f, 0.0f);
        const int least = static_cast<int>(std::lround(MaxHit(orc->Profile(), 1.0f) * warchief.heavy.damage * 0.8f));
        Check(wait_for_charge(orc), "close to the player, the Warchief starts winding up");
        const int hp0 = w.player.hp;
        float last = 0.0f;
        bool rising = true, reached_full = false, no_damage_yet = true;
        int charge_frames = 0;
        while (orc->ChargingHeavy() && charge_frames < 240) {
            const float c = orc->HeavyCharge();
            rising &= c >= last;
            last = c;
            reached_full |= c > 0.9f;
            no_damage_yet &= w.player.hp == hp0;
            frames(1);
            ++charge_frames;
        }
        Check(rising && reached_full, "the charge bar fills steadily from empty to full");
        Check(std::fabs(charge_frames / 60.0f - warchief.heavy.windup) < 0.1f,
              "over the whole wind-up (" + std::to_string(charge_frames) + " frames)");
        Check(no_damage_yet, "and nothing lands until it is full");
        frames(2);
        Check(hp0 - w.player.hp >= least,
              "then the blow lands, for well over an ordinary hit (" + std::to_string(hp0 - w.player.hp) + ")");
        frames(60);
        Check(!orc->ChargingHeavy() && orc->HeavyCooldown() > warchief.heavy.cooldown - 1.0f,
              "and it rests before the next one");

        // --- a raised shield is punished ---------------------------------------
        w.player.Rest();
        w.player.equipment.Equip(SLOT_SHIELD, items.TierPiece("enchanted", "shield"));
        w.player.facing = FACE_RIGHT;
        orc = spawn_warchief(40.0f, 0.0f);
        key(SDLK_H, true);
        frames(2);
        Check(w.player.Blocking(), "the guard is up against it");
        Check(wait_for_charge(orc), "the Warchief winds up at a raised shield too");
        const int hp1 = w.player.hp;
        for (int f = 0; f < 240 && orc->ChargingHeavy(); ++f) frames(1);
        frames(2);
        const int taken = hp1 - w.player.hp;
        // The shield stops none of it; what is worn still takes its share
        // first, and the shield in this test is the best in the game.
        const CombatProfile worn = w.player.Profile();
        const int through = SoakHeavy(least, worn.defence_level, worn.defence_bonus);
        Check(taken >= static_cast<int>(through * World::HEAVY_BLOCK_PUNISH) - 1,
              "the best shield there is stops none of it, and what gets past the armour lands half as hard again (" +
              std::to_string(taken) + ")");
        Check(w.player.GuardBroken() && !w.player.Blocking() && w.player.Stamina() == 0.0f,
              "and the guard shatters, taking every bit of stamina with it");
        key(SDLK_H, false);
        frames(2);

        // --- it can be stepped out of --------------------------------------------
        w.player.Rest();
        w.player.facing = FACE_RIGHT;
        orc = spawn_warchief(40.0f, 0.0f);
        Check(wait_for_charge(orc), "another wind-up");
        // Early in the charge it turns to follow.
        w.player.x = orc->x;
        w.player.y = orc->y + 44.0f;
        frames(3);
        Check(orc->facing == FACE_DOWN, "early in the wind-up it turns to follow the player");
        // Once committed, it does not.
        while (orc->ChargingHeavy() && orc->HeavyCharge() < Enemy::HEAVY_LOCK + 0.05f) frames(1);
        const int hp2 = w.player.hp;
        w.player.x = orc->x + 80.0f;
        w.player.y = orc->y;
        frames(2);
        Check(orc->facing == FACE_DOWN, "late in it, it is committed to where the player was");
        for (int f = 0; f < 120 && orc->ChargingHeavy(); ++f) frames(1);
        frames(2);
        Check(w.player.hp == hp2, "and a player who stepped out of the line takes nothing");

        // --- braced ---------------------------------------------------------------
        orc = spawn_warchief(40.0f, 0.0f);
        Check(wait_for_charge(orc), "one more wind-up");
        const float ox = orc->x;
        orc->knock_x += 600.0f;
        orc->Damage(1);
        frames(6);
        Check(std::fabs(orc->x - ox) < 12.0f, "a charging leader is braced against being knocked about");
        Check(orc->ChargingHeavy(), "and being hit does not stop the charge");
    }

    // --- Rushing Strike --------------------------------------------------------------
    Section("Rushing Strike");
    {
        Input input;
        std::mt19937 rng(5);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the leap");
        w.enemies.clear();
        w.player.y -= 200.0f;
        w.player.equipment.Equip(SLOT_WEAPON, "iron_sword");
        const auto frames = [&](int n) {
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
        };
        // A tap: the key goes in after the input's frame begins and before the
        // world reads it, the way a real event arrives. Input::Update clears
        // the last frame's presses, so a key sent before it is never seen.
        const auto tap = [&](SDL_Keycode k) {
            input.Update(1.0f / 60.0f);
            key(k, true);
            w.Update(1.0f / 60.0f, ctx);
            input.Update(1.0f / 60.0f);
            key(k, false);
            w.Update(1.0f / 60.0f, ctx);
        };
        // Sprinting right and pressing the light attack: the leap is made at a
        // sprint, with the button held, not at a walk.
        const auto running_light = [&]() {
            w.player.Rest();
            key(SDLK_LSHIFT, true);
            key(SDLK_D, true);
            frames(12);
            tap(SDLK_J);
        };
        // The same at a walk: the stick pushed as far, and no sprint.
        const auto walking_light = [&]() {
            w.player.Rest();
            key(SDLK_D, true);
            frames(12);
            tap(SDLK_J);
        };
        const auto stop = [&]() {
            key(SDLK_D, false);
            key(SDLK_LSHIFT, false);
            frames(50);
        };

        running_light();
        Check(!w.player.Rushing() && w.player.Attacking(),
              "without the skill, a running light attack is an ordinary one");
        stop();

        {
            LevelUp up;
            w.player.skills.AddXp(SKILL_ATTACK, XpForLevel(15), up);
        }
        Check(w.player.talents.CanLearn("rushing_strike", w.player.skills) == Talents::Why::Ok,
              "at Attack 15 it can be learned, with nothing above it to learn first");
        Check(w.player.talents.Learn("rushing_strike", w.player.skills), "and it is learned");

        // Standing still it is the ordinary attack.
        tap(SDLK_J);
        Check(!w.player.Rushing() && w.player.Attacking(), "standing still, the light attack does not leap");
        frames(50);
        // Walking, with the leap learned and rested: an ordinary light attack.
        // On a keyboard a walk pushes the stick as far as a run does, and this
        // used to leap.
        walking_light();
        Check(!w.player.Rushing() && w.player.Attacking() && w.player.RushCooldown() <= 0.0f,
              "walking, with the leap ready, the light attack is an ordinary one");
        stop();
        frames(50);

        const float x0 = w.player.x;
        running_light();
        Check(w.player.Rushing(), "at a run, the light attack leaps");
        Check(w.player.sprite.current == "rush", "and plays the leap");
        Check(std::fabs(w.player.Attack().damage_mult - ProfileFor(AttackType::Light, 0).damage_mult * 1.4f) < 0.001f,
              "for 1.4 times a light attack's damage");
        Check(std::fabs(w.player.RushCooldown() - Player::RUSH_COOLDOWN) < 0.1f, "and starts its three-second rest");
        float peak = 0.0f;
        for (int f = 0; f < 20; ++f) { frames(1); peak = std::max(peak, w.player.RushLift()); }
        Check(peak > 8.0f, "the character leaves the ground");
        Check(w.player.x - x0 > 12.0f * (78.0f / 60.0f) + 70.0f,
              "and covers the leap's distance on top of the run up to it");
        frames(30);
        Check(!w.player.Rushing(), "the leap ends");

        // Resting: a second running light attack straight after is ordinary.
        running_light();
        Check(!w.player.Rushing() && w.player.Attacking(), "inside three seconds a running light attack does not leap");
        stop();
        frames(3 * 60);
        running_light();
        Check(w.player.Rushing(), "after three seconds it leaps again");
        stop();
        // And having leapt, a walk is still a walk: rested again, without the
        // sprint button, every light attack is an ordinary one.
        frames(static_cast<int>(Player::RUSH_COOLDOWN * 60.0f) + 30);
        bool leapt_walking = false;
        for (int i = 0; i < 3; ++i) {
            walking_light();
            leapt_walking |= w.player.Rushing();
            stop();
        }
        Check(!leapt_walking && w.player.RushCooldown() <= 0.0f,
              "having leapt once, walking light attacks never leap, however rested");
        stop();

        // Only with something to swing.
        w.player.equipment.Equip(SLOT_WEAPON, items.TierPiece("iron", "bow"));
        frames(4 * 60);
        running_light();
        Check(!w.player.Rushing(), "a bow does not leap");
        stop();
        w.player.equipment.Equip(SLOT_WEAPON, "iron_sword");

        // At a target: the leap goes at the monster rather than along the run.
        frames(4 * 60);
        {
            const EnemyDef* stats = enemy_db.Get("orc1");
            EnemySpawnDef def;
            def.type = "orc1"; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + 20.0f; def.y = w.player.y + 90.0f;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            Enemy* orc = e.get();
            w.enemies.push_back(std::move(e));
            tap(SDLK_L);
            const float y0 = w.player.y;
            running_light();
            Check(w.player.Rushing() && w.player.facing == FACE_DOWN,
                  "with a monster targeted, the leap turns to it");
            frames(18);
            Check(w.player.y - y0 > 50.0f, "and carries the character at it");
            const SDL_FRect hit = AttackHitbox(w.player.x, w.player.y, w.player.facing, w.player.Attack().profile, 1.0f);
            Check(RectsOverlap(hit, orc->BodyBox()), "landing with the monster inside the blow");
            stop();
        }
    }

    // --- blocking -------------------------------------------------------------------
    Section("blocking with a shield");
    {
        // The rule on its own: the damage, times the root of the attacker's
        // level, times 1.6, times the shield's own multiplier. A blow of 10
        // from a level 4 is 10 x 2 x 1.6 = 32, and a shield that turns aside
        // half of it stops 5.
        BlockOutcome b = ResolveBlock(10, 4, 0.5f, 1.0f, 100.0f);
        Check(b.blocked == 5 && b.taken == 5 && std::fabs(b.stamina - 32.0f) < 0.01f && !b.broke,
              "a blow of 10 from a level 4 costs 32 stamina and half of it is stopped");
        b = ResolveBlock(6, 16, 0.5f, 1.0f, 1000.0f);
        Check(std::fabs(b.stamina - 38.4f) < 0.01f, "stamina is the damage by the root of the attacker's level");
        b = ResolveBlock(6, 16, 0.5f, 0.5f, 1000.0f);
        Check(std::fabs(b.stamina - 19.2f) < 0.01f, "and a shield's multiplier takes its share off that");
        // Short of stamina: the block holds for what could be paid, all the
        // stamina goes, and the guard breaks. 20 from a level 25 is 160.
        b = ResolveBlock(20, 25, 0.5f, 1.0f, 40.0f);
        Check(b.broke && std::fabs(b.stamina - 40.0f) < 0.01f,
              "a blow costing more than is left empties the bar and breaks the guard");
        Check(b.blocked == 3 && b.taken == 17, "and only the share that was paid for is stopped");
        // What the change was for: a beginner's shield against something well
        // past the meadow is worth raising, and against a dragon it is not.
        b = ResolveBlock(15, 28, 0.5f, 1.0f, 100.0f);
        Check(b.blocked >= 5, "a wooden shield turns a fair part of a Warchief's blow (" +
              std::to_string(b.blocked) + " of 15), where it used to turn two");
        b = ResolveBlock(30, 64, 0.5f, 1.0f, 100.0f);
        Check(b.broke && b.blocked <= 5, "and is still matchwood to a dragon");
        Check(BlockCost(30, 64, 0.086f) < 40.0f, "which the best shield in the game stops for a third of a bar");
        Check(ResolveBlock(10, 5, 0.0f, 1.0f, 100.0f).blocked == 0, "something that does not block stops nothing");
        Check(ResolveBlock(0, 5, 0.5f, 1.0f, 100.0f).stamina == 0.0f, "a blow that did no damage costs nothing");

        // Each tier blocks better and for less.
        int shields = 0;
        bool better = true, cheaper = true;
        float last_block = 0.0f, last_cost = 2.0f;
        for (const TierDef& t : items.Tiers()) {
            const ItemDef* d = items.Get(items.TierPiece(t.id, "shield"));
            if (!d) continue;
            ++shields;
            better  &= d->block > last_block;
            cheaper &= d->block_stamina < last_cost;
            last_block = d->block;
            last_cost = d->block_stamina;
        }
        Check(shields == 12, "every tier makes a shield that blocks");
        Check(better, "each tier's shield turns aside more of a blow than the last");
        Check(cheaper, "and each one costs less stamina to block with");
        if (const ItemDef* wood = items.Get("wooden_shield"))
            Check(std::fabs(wood->block - 0.5f) < 0.001f && std::fabs(wood->block_stamina - 1.0f) < 0.001f,
                  "a wooden shield stops half, at full cost");
        if (const ItemDef* top = items.Get(items.TierPiece("enchanted", "shield")))
            Check(top->block >= 0.95f && top->block_stamina < 0.1f,
                  "an enchanted shield stops nineteen parts in twenty for under a tenth of the cost");
        if (const ItemDef* lamp = items.Get("lantern"))
            Check(lamp->slot == SLOT_SHIELD && lamp->block == 0.0f, "a lantern in the off hand is not a shield");

        Check(InFrontOf(FACE_RIGHT, 30.0f, 0.0f) && InFrontOf(FACE_RIGHT, 20.0f, 20.0f),
              "a blow from ahead, or ahead and to one side, is in front");
        Check(!InFrontOf(FACE_RIGHT, -30.0f, 0.0f) && !InFrontOf(FACE_UP, 0.0f, 30.0f),
              "a blow from behind is not");
        CombatProfile brute;
        brute.attack_level = 12; brute.strength_level = 30; brute.defence_level = 20;
        Check(CombatLevelOf(brute) == 30, "an attacker's level is the highest of its combat levels");

        // --- played through ----------------------------------------------------
        Input input;
        std::mt19937 rng(11);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the guard");
        w.enemies.clear();
        w.player.y -= 200.0f;                 // open road, clear of the town gate
        {
            LevelUp up;
            w.player.skills.AddXp(SKILL_HITPOINTS, 20000, up);
            w.player.Rest();
        }
        w.player.facing = FACE_RIGHT;
        const auto frames = [&](int n) {
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
        };

        key(SDLK_H, true);
        frames(6);
        Check(!w.player.Blocking(), "with no shield there is no guard to raise");
        w.player.equipment.Equip(SLOT_SHIELD, "lantern");
        frames(6);
        Check(!w.player.Blocking(), "nor with a lantern");
        w.player.equipment.Equip(SLOT_SHIELD, "wooden_shield");
        frames(6);
        Check(w.player.Blocking(), "holding the button with a shield raises the guard");
        Check(w.player.sprite.current == "block", "and the character holds the shield up");

        // Pressed the way a real key arrives: after the input's frame begins.
        input.Update(1.0f / 60.0f);
        key(SDLK_J, true);
        w.Update(1.0f / 60.0f, ctx);
        frames(2);
        key(SDLK_J, false);
        frames(2);
        Check(!w.player.Attacking(), "a swing does not start from behind a raised shield");

        CombatProfile grunt;
        grunt.attack_level = 5; grunt.strength_level = 5;
        const int   hp0 = w.player.hp;
        const float st0 = w.player.Stamina();
        const int   xp0 = w.player.skills.Xp(SKILL_DEFENCE);
        int taken = w.HitPlayer(6, grunt, w.player.x + 30.0f, w.player.y);
        Check(taken == 3 && w.player.hp == hp0 - 3, "a wooden shield turns aside half of a blow from the front");
        Check(std::fabs(st0 - w.player.Stamina() - BlockCost(6, 5, 1.0f)) < 0.01f,
              "for the breath BlockCost says: about 21");
        Check(w.player.skills.Xp(SKILL_DEFENCE) - xp0 >= 12 + 3,
              "stopping it trains Defence, on top of what the blow that got through does");

        const int hp1 = w.player.hp;
        taken = w.HitPlayer(4, grunt, w.player.x - 30.0f, w.player.y);
        Check(taken == 4 && w.player.hp == hp1 - 4, "a blow from behind is not blocked");

        CombatProfile dragon;
        dragon.attack_level = 60; dragon.strength_level = 64;
        const float before_break = w.player.Stamina();
        // A glancing blow from a dragon: twelve, which at the root of sixty-four
        // is still half as much again as a whole bar. (A square one is thirty,
        // and this character would not be standing to have the rest checked.)
        taken = w.HitPlayer(12, dragon, w.player.x + 30.0f, w.player.y);
        Check(w.player.Stamina() == 0.0f && before_break > 0.0f, "a dragon's blow empties the bar");
        Check(w.player.GuardBroken() && !w.player.Blocking(), "and breaks the guard");
        frames(10);
        Check(!w.player.Blocking(), "a broken guard does not come straight back up");
        frames(4 * 60);
        Check(!w.player.GuardBroken() && w.player.Blocking(),
              "it comes back once the bar has refilled far enough");

        // A better shield takes the same blow for far less.
        w.player.Rest();
        w.player.equipment.Equip(SLOT_SHIELD, items.TierPiece("enchanted", "shield"));
        frames(2);
        const float st2 = w.player.Stamina();
        taken = w.HitPlayer(6, grunt, w.player.x + 30.0f, w.player.y);
        Check(taken == 0, "an enchanted shield stops all of a small blow");
        Check(st2 - w.player.Stamina() < 3.0f, "for under a tenth of what the wooden one paid");

        // A raised guard is a slow step.
        const auto walk = [&](bool guard) {
            w.player.Rest();
            key(SDLK_H, guard);
            key(SDLK_D, true);
            frames(4);
            const float x0 = w.player.x;
            frames(60);
            key(SDLK_D, false);
            key(SDLK_H, false);
            frames(4);
            return w.player.x - x0;
        };
        const float open_walk = walk(false);
        const float guarded   = walk(true);
        Check(guarded > 1.0f && guarded < open_walk * 0.6f, "walking with the guard up is a slow step");
    }

    // --- sprinting ------------------------------------------------------------------
    Section("sprinting");
    {
        Input input;
        std::mt19937 rng(7);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        // Walks right for a second on open floor and says how far it got.
        struct Run { float distance = 0; bool sprinted = false; string clip; bool dust = false;
                     float lead = 0; };
        const auto run_for = [&](const string& rig, bool shift, float seconds,
                                 const std::function<void(World&, int)>& each = nullptr) {
            Run out;
            World w;
            w.player.Init(ctx, rig);
            if (!w.LoadMap("overworld", "start", ctx)) return out;
            w.enemies.clear();
            const float x0 = w.player.x;
            key(SDLK_D, true);
            if (shift) key(SDLK_LSHIFT, true);
            const int frames = static_cast<int>(seconds * 60.0f);
            for (int f = 0; f < frames; ++f) {
                input.Update(1.0f / 60.0f);
                if (each) each(w, f);
                w.Update(1.0f / 60.0f, ctx);
                out.sprinted |= w.player.Sprinting();
                out.dust |= !w.dust.empty();
            }
            out.distance = w.player.x - x0;
            out.clip = w.player.sprite.current;
            out.lead = w.player.LookAhead().x;
            key(SDLK_D, false);
            key(SDLK_LSHIFT, false);
            input.Update(1.0f / 60.0f);
            return out;
        };

        const Run jog = run_for("player_hero", false, 1.0f);
        const Run dash = run_for("player_hero", true, 1.0f);
        Check(jog.distance > 40.0f, "the hero moves at all without sprinting");
        Check(!jog.sprinted && jog.clip == "run", "without the button it is a run");
        Check(dash.sprinted && dash.clip == "sprint", "holding sprint plays the sprint");
        Check(dash.distance > jog.distance * 1.45f && dash.distance < jog.distance * 1.75f,
              "a sprint covers about half as much ground again");
        Check(dash.dust, "a sprint kicks up dust outdoors");
        Check(dash.lead > 20.0f && fabsf(jog.lead) < 1.0f, "the camera leads a sprint and centres on a run");

        // A rig with no sprint clip still speeds up, and plays whatever it does
        // have rather than freezing on a missing animation. The rig used to be
        // one of the pack characters; with those gone it is a monster rig,
        // which is the case that matters anyway -- every one of them is a rig
        // the player could be given and none of them has a sprint.
        const Run fallback = run_for("zombie", true, 1.0f);
        Check(fallback.sprinted && fallback.clip != "sprint",
              "a rig without a sprint clip speeds up and plays what it has instead");
        Check(fallback.distance > jog.distance * 1.45f, "and still covers the ground");

        // Attacking stops a sprint; the swing has its own footwork.
        bool stopped_for_swing = false;
        run_for("player_hero", true, 1.0f, [&](World& w, int f) {
            if (f == 30) key(SDLK_J, true);
            if (f == 31) key(SDLK_J, false);
            if (f > 32 && f < 40 && !w.player.Sprinting()) stopped_for_swing = true;
        });
        Check(stopped_for_swing, "a sprint stops for an attack");

        // A hit knocks the player out of a sprint for a moment, then it resumes.
        bool broken = false, resumed = false;
        run_for("player_hero", true, 2.0f, [&](World& w, int f) {
            if (f == 20) w.player.Damage(1);
            if (f > 22 && f < 60 && w.player.Sprinting()) broken = true;   // should NOT happen
            if (f > 20 + static_cast<int>(Player::SPRINT_LOCKOUT * 60.0f) + 6 && w.player.Sprinting())
                resumed = true;
        });
        Check(!broken, "taking a hit breaks the sprint");
        Check(resumed, "and it picks up again once the lockout passes");

        // --- stamina --------------------------------------------------------
        // Sprint flat out until the bar runs dry: that should take about
        // MAX / DRAIN seconds, end the sprint, and leave the player winded.
        {
            float ran_dry_at = -1.0f, sprinted_winded = 0.0f, lowest = 1e9f;
            bool winded_seen = false;
            const float expect = Player::MAX_STAMINA / Player::STAMINA_DRAIN;
            run_for("player_hero", true, expect + 2.0f, [&](World& w, int f) {
                const Player& p = w.player;
                lowest = std::min(lowest, p.Stamina());
                if (p.Winded()) {
                    winded_seen = true;
                    if (ran_dry_at < 0.0f) ran_dry_at = f / 60.0f;
                    if (p.Sprinting()) sprinted_winded += 1.0f / 60.0f;
                }
            });
            Check(lowest <= 0.0f, "holding sprint empties the stamina bar");
            Check(winded_seen, "running it dry leaves the player winded");
            Check(ran_dry_at > expect - 0.25f && ran_dry_at < expect + 0.25f,
                  "a full bar lasts about " + std::to_string(static_cast<int>(expect * 10) / 10.0f).substr(0, 3) + " seconds of sprint");
            Check(sprinted_winded == 0.0f, "nobody sprints while winded, however hard the key is held");
        }
        // Winded clears only past the recovery threshold, then sprinting works again.
        {
            bool cleared_early = false, cleared = false, sprinted_after = false;
            run_for("player_hero", false, 12.0f, [&](World& w, int f) {
                Player& p = w.player;
                if (f == 0) {
                    // Burn the bar down by sprinting in place of the key.
                    while (p.Stamina() > 0.0f) {
                        key(SDLK_LSHIFT, true);
                        input.Update(1.0f / 60.0f);
                        w.Update(1.0f / 60.0f, ctx);
                    }
                    key(SDLK_LSHIFT, false);
                }
                // Only until it first clears: sprinting again afterwards
                // spends the bar back down, which is the point.
                if (!cleared && !p.Winded() &&
                    p.Stamina() < Player::MAX_STAMINA * Player::STAMINA_RECOVER - 1.0f)
                    cleared_early = true;
                if (f > 5 && !p.Winded()) {
                    cleared = true;
                    key(SDLK_LSHIFT, true);
                }
                if (cleared && p.Sprinting()) sprinted_after = true;
            });
            Check(!cleared_early, "winded lasts until the bar is refilled past the threshold");
            Check(cleared && sprinted_after, "and the sprint comes back once it clears");
        }
        // Regeneration waits for a breather, and is quicker standing still.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx)) {
                w.enemies.clear();
                key(SDLK_D, true);
                key(SDLK_LSHIFT, true);
                for (int f = 0; f < 60; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                key(SDLK_LSHIFT, false);
                const float after_sprint = w.player.Stamina();
                for (int f = 0; f < 20; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                Check(fabsf(w.player.Stamina() - after_sprint) < 0.01f,
                      "stamina does not come back the instant a sprint ends");
                for (int f = 0; f < 60; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                const float moving_gain = w.player.Stamina() - after_sprint;
                key(SDLK_D, false);
                const float before_rest = w.player.Stamina();
                for (int f = 0; f < 30; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                const float rest_rate = (w.player.Stamina() - before_rest) * 2.0f;
                Check(moving_gain > 0.0f, "stamina refills while walking");
                Check(rest_rate > moving_gain * 1.2f, "and refills faster standing still");
                w.player.Respawn(w.player.x, w.player.y);
                Check(w.player.Stamina() == Player::MAX_STAMINA && !w.player.Winded(),
                      "respawning restores a full bar");
                input.Update(1.0f / 60.0f);
            }
        }
    }

    // --- keyboard controls and targeting ---------------------------------------------
    // The game is played on the keyboard alone. Nothing is aimed by hand: out
    // of combat a shot flies the way the character faces, and in combat it
    // goes to whoever the fight is with.
    Section("keyboard controls and targeting");
    {
        Input input;
        std::mt19937 rng(20260913);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        // --- the bindings ----------------------------------------------------
        {
            const auto pressed_by = [&](SDL_Keycode k, Action a) {
                input.Update(dt);
                key(k, true);
                const bool pressed = input.Pressed(a);
                input.Update(dt);
                key(k, false);
                input.Update(dt);
                return pressed;
            };
            Check(pressed_by(SDLK_J, Action::LightAttack), "J is the light attack");
            Check(pressed_by(SDLK_K, Action::StrongAttack), "K is the heavy attack");
            Check(pressed_by(SDLK_L, Action::Target), "L is the target lock");
            Check(pressed_by(SDLK_E, Action::Interact) && pressed_by(SDLK_SPACE, Action::Jump) &&
                  pressed_by(SDLK_LSHIFT, Action::Sprint), "E interacts, Space jumps and Shift sprints");
            Check(pressed_by(SDLK_I, Action::Inventory) && pressed_by(SDLK_O, Action::Skills) &&
                  pressed_by(SDLK_P, Action::QuestLog) && pressed_by(SDLK_ESCAPE, Action::Pause),
                  "I, O and P open the bag, skills and quests, and Esc the menu");
            Check(pressed_by(SDLK_J, Action::Confirm) && pressed_by(SDLK_K, Action::Back) &&
                  pressed_by(SDLK_E, Action::Confirm), "in menus J or E confirms and K backs out");
            Check(!pressed_by(SDLK_K, Action::Skills), "K no longer opens the skills panel");
            Check(!pressed_by(SDLK_Z, Action::LightAttack) && !pressed_by(SDLK_X, Action::StrongAttack),
                  "Z and X no longer attack");

            input.Update(dt);
            SDL_Event m{};
            m.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            m.button.button = SDL_BUTTON_LEFT;
            input.HandleEvent(m);
            const bool clicked = input.Pressed(Action::LightAttack) || input.Pressed(Action::Confirm);
            m.type = SDL_EVENT_MOUSE_BUTTON_UP;
            input.HandleEvent(m);
            input.Update(dt);
            Check(!clicked, "a mouse click does not attack");

            input.SetMode(InputMode::KeyboardMouse);
            Check(input.PromptFor(Action::LightAttack) == "J" && input.PromptFor(Action::StrongAttack) == "K" &&
                  input.PromptFor(Action::Target) == "L" && input.PromptFor(Action::Skills) == "O" &&
                  input.PromptFor(Action::QuestLog) == "P", "the on-screen prompts name the keys");
            input.SetMode(InputMode::Auto);
        }

        // --- a small arena ---------------------------------------------------
        // The overworld start, where the sprint tests run: open ground to the
        // right with nothing in the way.
        const auto arena = [&](World& w, const string& weapon) {
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap("overworld", "start", ctx)) return false;
            w.enemies.clear();
            // Up the road, clear of Havenbrook's gate: these checks need open
            // ground on every side of the player, and the start spawn is now a
            // few strides from a gatehouse with walls either side of it.
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.sprite.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, weapon);
            return true;
        };
        // A monster placed so the middle of its body is dx, dy from the
        // player's chest, where shots leave from.
        const auto spawn = [&](World& w, const string& type, float dx, float dy) -> Enemy* {
            const EnemyDef* stats = enemy_db.Get(type);
            if (!stats) return nullptr;
            EnemySpawnDef def;
            def.type = type; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            const SDL_FPoint aim = Targeting::AimPoint(*e);
            const SDL_FPoint muzzle = Targeting::Muzzle(w.player);
            e->x += (muzzle.x + dx) - aim.x;
            e->y += (muzzle.y + dy) - aim.y;
            e->home_x = e->x; e->home_y = e->y;
            Enemy* raw = e.get();
            w.enemies.push_back(std::move(e));
            return raw;
        };
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        const auto tap = [&](World& w, SDL_Keycode k) {
            input.Update(dt); key(k, true);  w.Update(dt, ctx);
            input.Update(dt); key(k, false); w.Update(dt, ctx);
        };
        // Attacks with the equipped weapon and returns the direction the first
        // shot left in, or zero if nothing was loosed.
        const auto shoot = [&](World& w) -> Vec2 {
            w.projectiles.clear();
            tap(w, SDLK_J);
            for (int f = 0; f < 90 && w.projectiles.empty(); ++f) frames(w, 1);
            if (w.projectiles.empty()) return {0.0f, 0.0f};
            const Projectile& pr = w.projectiles.front();
            const float len = std::max(0.001f, Length(pr.vx, pr.vy));
            return {pr.vx / len, pr.vy / len};
        };

        // Out of combat: the way the character faces, whatever is nearby.
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                spawn(w, "hare", 90.0f, 50.0f);
                frames(w, 2);
                Check(!w.targeting.InCombat() && !w.targeting.Current(),
                      "a grazing hare nearby is not a fight");
                const Vec2 d = shoot(w);
                Check(d.x > 0.99f, "out of combat, an arrow flies straight the way the character faces");
            }
            World up;
            if (arena(up, "oak_shortbow")) {
                up.player.facing = FACE_UP;
                const Vec2 d = shoot(up);
                Check(d.y < -0.99f, "and facing up, straight up");
            }
        }

        // Striking something starts a fight with it.
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                Enemy* deer = spawn(w, "deer", 80.0f, 0.0f);
                shoot(w);
                for (int f = 0; f < 60 && deer && !deer->HealthBarVisible(); ++f) frames(w, 1);
                frames(w, 2);
                Check(deer && deer->HealthBarVisible(), "a straight shot hits the deer in front");
                Check(deer && w.targeting.Current() == deer, "and hitting it puts the player in combat with it");
            }
        }

        // In combat: at the monster, not the way the character faces.
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                Enemy* orc = spawn(w, "orc1", 60.0f, 95.0f);
                frames(w, 6);
                Check(orc && orc->Engaged() && w.targeting.Current() == orc,
                      "a monster coming for the player is the combat target");
                const Vec2 d = shoot(w);
                Vec2 want{0.0f, 0.0f};
                if (orc) {
                    const SDL_FPoint a = Targeting::AimPoint(*orc), m = Targeting::Muzzle(w.player);
                    const float len = std::max(0.001f, Length(a.x - m.x, a.y - m.y));
                    want = {(a.x - m.x) / len, (a.y - m.y) / len};
                }
                Check(d.x * want.x + d.y * want.y > 0.9f && d.y > 0.3f,
                      "in combat, the arrow is loosed at the monster rather than straight ahead");
                Check(w.player.facing == FACE_DOWN, "and the character turns to shoot it");
            }
        }

        // An arrow steers after its target; one with none flies straight past.
        {
            const auto loose = [&](bool aimed) {
                World w;
                if (!arena(w, "oak_shortbow")) return false;
                Enemy* deer = spawn(w, "deer", 170.0f, 56.0f);
                const SDL_FPoint m = Targeting::Muzzle(w.player);
                w.SpawnProjectile("arrow", m.x + 12.0f, m.y, 1.0f, 0.0f, w.player.Profile(),
                                  AttackStyle::Ranged, 1.0f, true, ctx);
                if (aimed && !w.projectiles.empty()) w.projectiles.back().target = deer;
                for (int f = 0; f < 70 && deer && !deer->HealthBarVisible(); ++f) frames(w, 1);
                return deer && deer->HealthBarVisible();
            };
            Check(loose(true), "an arrow loosed at a monster off the line steers into it");
            Check(!loose(false), "the same arrow with no target flies past");
        }

        // The lock: L steps nearest to furthest, then lets go.
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                Enemy* near_deer = spawn(w, "deer", 60.0f, 45.0f);
                Enemy* far_deer  = spawn(w, "deer", 170.0f, -30.0f);
                frames(w, 1);
                Check(!w.targeting.InCombat(), "two deer nearby are no fight until one is locked");
                tap(w, SDLK_L);
                const bool first = w.targeting.Locked() == near_deer;
                tap(w, SDLK_L);
                const bool second = w.targeting.Locked() == far_deer;
                tap(w, SDLK_L);
                const bool off = !w.targeting.IsLocked() && !w.targeting.InCombat();
                Check(first, "L locks the nearest");
                Check(second, "L again moves the lock to the next one out");
                Check(off, "and past the last it lets go");

                tap(w, SDLK_L);
                const Vec2 d = shoot(w);
                Check(d.y > 0.4f && w.targeting.Locked() == near_deer,
                      "a locked deer is shot at although it is not fighting");
            }
        }

        // A lock turns a standing player to face it, and lets go when the
        // monster dies or gets away.
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                Enemy* deer = spawn(w, "deer", -80.0f, 0.0f);
                tap(w, SDLK_L);
                frames(w, 2);
                Check(w.targeting.Locked() == deer && w.player.facing == FACE_LEFT,
                      "standing still with a lock, the character faces the locked monster");
                if (deer) { deer->x -= Targeting::LOCK_BREAK + 40.0f; deer->home_x = deer->x; }
                frames(w, 1);
                Check(!w.targeting.IsLocked(), "the lock lets go when the monster is out of range");

                Enemy* other = spawn(w, "deer", 60.0f, 0.0f);
                tap(w, SDLK_L);
                const bool locked = w.targeting.Locked() == other;
                if (other) other->Damage(1000);
                frames(w, 3);
                Check(locked && !w.targeting.IsLocked() && !w.targeting.Current(),
                      "and when it dies");

                tap(w, SDLK_L);
                spawn(w, "deer", 50.0f, 0.0f);
                tap(w, SDLK_L);
                w.LoadMap("overworld", "start", ctx);
                Check(!w.targeting.Current(), "a map change clears the target");
            }
        }

        // A sword turns toward a target in reach, and not toward one across the field.
        {
            World w;
            if (arena(w, "bronze_sword")) {
                Enemy* orc = spawn(w, "orc1", 0.0f, -44.0f);
                frames(w, 3);
                tap(w, SDLK_J);
                Check(orc && w.targeting.Current() == orc && w.player.facing == FACE_UP,
                      "a sword swing turns to the monster beside the player");
            }
            World far_w;
            if (arena(far_w, "bronze_sword")) {
                Enemy* orc = spawn(far_w, "orc1", 0.0f, -120.0f);
                frames(far_w, 3);
                tap(far_w, SDLK_J);
                Check(orc && far_w.targeting.Current() == orc && far_w.player.facing == FACE_RIGHT,
                      "but not to one too far away to reach");
            }
        }
        input.Update(dt);
    }

    // --- a real fight ---------------------------------------------------------
    // The combat maths above is expected values. This runs the actual world --
    // AI, swing timings, hitboxes, knockback -- frame by frame, with the
    // player's buttons pressed through the same Input the game reads.
    Section("a fight, frame by frame");
    {
        Input input;
        std::mt19937 rng(20260911);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;

        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        // The health bar's pixels: exact to a pixel, never empty while alive,
        // never full while hurt, and only ever shrinking as damage lands.
        {
            bool exact = true, alive_shows = true, hurt_shows = true, shrinks = true, ends = true;
            for (int max_hp : {3, 6, 10, 14, 34, 95, 100}) {
                for (int inner : {18, 26, 68}) {
                    int prev = inner + 1;
                    for (int hp = max_hp; hp >= 0; --hp) {
                        const int fill = HealthBarFillPixels(hp, max_hp, inner);
                        if (fabsf(fill - static_cast<float>(inner) * hp / max_hp) > 1.0f) exact = false;
                        if (hp > 0 && fill < 1) alive_shows = false;
                        if (hp > 0 && hp < max_hp && fill >= inner) hurt_shows = false;
                        if (fill > prev) shrinks = false;
                        if ((hp == max_hp && fill != inner) || (hp == 0 && fill != 0)) ends = false;
                        prev = fill;
                    }
                }
            }
            Check(exact,       "health bar fill is within a pixel of hp / max_hp");
            Check(alive_shows, "a living monster's health bar is never empty");
            Check(hurt_shows,  "a wounded monster's health bar is never full");
            Check(shrinks,     "health bar fill only shrinks as hp falls");
            Check(ends,        "health bar is full at full hp and empty at none");
        }

        struct Outcome { float seconds = 0; int hp_lost = 0; bool enemy_dead = false;
                         bool player_dead = false; int swings = 0; int enemy_hp_left = 0;
                         int frames = 0; int on_top = 0;
                         bool bar_before_attack = false, bar_missing = false, bar_wrong = false;
                         float corpse_gone_after = -1.0f; bool revive_clean = false; };

        const auto fight = [&](const string& type, int level, bool fight_back, float limit) {
            Outcome out;
            World w;
            w.player.Init(ctx, "player_hero");
            w.player.inventory.Add("bronze_sword", 1);
            w.player.inventory.Add("wooden_shield", 1);
            string why;
            for (int s = 0; s < w.player.inventory.SlotCount(); ++s) {
                const string id = w.player.inventory.Slot(s).id;
                if (id == "bronze_sword" || id == "wooden_shield") w.player.EquipFromInventory(s, why);
            }
            // The mine's first room: open floor, no height, nothing in the way.
            if (!w.LoadMap("dungeon_emberfell_1", "entrance", ctx)) return out;
            w.enemies.clear();
            w.player.y += 48.0f;   // off the exit portal

            const EnemyDef* stats = enemy_db.Get(type);
            if (!stats) return out;
            EnemySpawnDef def;
            def.type = type; def.level = level;
            def.x = w.player.x + 44.0f; def.y = w.player.y;
            def.leash = 400.0f; def.respawn = 0.0f;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            Enemy* enemy = e.get();
            w.enemies.push_back(std::move(e));

            const int start_hp = w.player.hp;
            const float dt = 1.0f / 60.0f;
            int frame = 0;
            SDL_Keycode held = 0;
            float died_at = 0.0f;
            // A few seconds past the limit, so a late kill can still be
            // watched until its corpse goes.
            for (float t = 0; t < limit + 5.0f; t += dt, ++frame) {
                if (!out.enemy_dead && t >= limit) break;
                input.Update(dt);
                if (held) { key(held, false); held = 0; }

                if (fight_back && !w.player.IsDead() && !out.enemy_dead) {
                    // Turn to face it, one tap, then swing whenever allowed.
                    const float dx = enemy->x - w.player.x, dy = enemy->y - w.player.y;
                    const Facing want = fabsf(dx) > fabsf(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT)
                                                              : (dy > 0 ? FACE_DOWN : FACE_UP);
                    if (w.player.facing != want) {
                        held = want == FACE_RIGHT ? SDLK_D : want == FACE_LEFT ? SDLK_A
                             : want == FACE_DOWN ? SDLK_S : SDLK_W;
                        key(held, true);
                    } else if (w.player.CanAttack() && frame % 2 == 0) {
                        held = SDLK_J;
                        key(held, true);
                        ++out.swings;
                    }
                }

                w.Update(dt, ctx);

                // The health bar: nowhere until the first swing, there for as
                // long as the monster is hurt and alive, and never claiming
                // more health than it has.
                if (out.swings == 0 && enemy->HealthBarVisible()) out.bar_before_attack = true;
                if (enemy->CurrentState() != Enemy::State::Dead && enemy->hp < enemy->max_hp &&
                    !enemy->HealthBarVisible()) out.bar_missing = true;
                if (enemy->hp > enemy->max_hp ||
                    enemy->HealthTrail() + 1e-4f < enemy->HealthFraction()) out.bar_wrong = true;

                if (enemy->CurrentState() == Enemy::State::Dead) {
                    // Watch the corpse until it goes, then bring it back.
                    if (!out.enemy_dead) { out.enemy_dead = true; died_at = t; }
                    if (enemy->CorpseGone()) {
                        out.corpse_gone_after = t - died_at;
                        enemy->Revive();
                        out.revive_clean = !enemy->HealthBarVisible() && !enemy->CorpseGone() &&
                                           enemy->HealthFraction() == 1.0f &&
                                           enemy->HealthTrail() == 1.0f;
                        break;
                    }
                    continue;
                }

                out.seconds = t;
                // Standing inside the player: drawn after them, it hides them.
                ++out.frames;
                if (Length(enemy->x - w.player.x, enemy->y - w.player.y) < 8.0f) ++out.on_top;
                if (w.player.IsDead()) { out.player_dead = true; break; }
            }
            out.hp_lost = start_hp - w.player.hp;
            out.enemy_hp_left = enemy->hp;
            return out;
        };

        for (const char* type : {"fox", "boar"}) {
            for (bool back : {false, true}) {
                int deaths = 0, kills = 0, lost = 0, frames = 0, on_top = 0;
                int bar_early = 0, bar_missing = 0, bar_wrong = 0, corpses_gone = 0, revived = 0;
                float time = 0, slowest_corpse = 0;
                for (int run = 0; run < 20; ++run) {
                    const Outcome o = fight(type, 1, back, back ? 60.0f : 30.0f);
                    deaths += o.player_dead; kills += o.enemy_dead; lost += o.hp_lost; time += o.seconds;
                    frames += o.frames; on_top += o.on_top;
                    bar_early += o.bar_before_attack; bar_missing += o.bar_missing;
                    bar_wrong += o.bar_wrong; revived += o.revive_clean;
                    if (o.corpse_gone_after >= 0.0f) {
                        ++corpses_gone;
                        slowest_corpse = std::max(slowest_corpse, o.corpse_gone_after);
                    }
                }
                printf("     %-4s %-13s  player died %2d/20  killed it %2d/20  hp lost %.1f  over %.1fs"
                       "  on top of the player %.0f%% of the time\n",
                       type, back ? "fighting back" : "standing still",
                       deaths, kills, lost / 20.0f, time / 20.0f,
                       100.0f * on_top / std::max(1, frames));

                const string who = string("a ") + type + (back ? " fought" : " left alone");
                // It used to walk right onto the player between swings and
                // stay there. Found in a playtest where the boar hid the
                // character completely.
                Check(on_top <= frames / 50, who + " does not stand on top of the player");
                Check(bar_early == 0, who + " shows no health bar before it is attacked");
                Check(bar_wrong == 0, who + " never shows more health than it has");
                if (back) {
                    Check(bar_missing == 0, who + " shows its health bar once it is hurt");
                    Check(corpses_gone == kills, who + ": every corpse despawns");
                    Check(slowest_corpse < 3.0f, who + ": its corpse is gone within three seconds (" +
                              std::to_string(slowest_corpse).substr(0, 4) + "s)");
                    Check(revived == kills, who + " comes back with its bar hidden and full health");
                    Check(kills >= 18, who + " by a new character is usually killed");
                    Check(deaths <= 1, who + " by a new character rarely kills them");
                }
            }
        }
    }

    // --- save round trip ------------------------------------------------------
    // --- material tiers --------------------------------------------------------------
    Section("material tiers");
    {
        static const char* kOrder[] = {"wood", "bronze", "iron", "steel", "azuryte",
                                       "damascus", "orichalcum", "diamond", "platinum",
                                       "demonite", "dracon", "enchanted"};
        static const int kTierCount = 12;
        static const char* kPieces[] = {"sword", "spear", "bow", "staff", "shield", "helm", "body", "legs"};
        const auto& tiers = items.Tiers();
        Check(tiers.size() == kTierCount, "there are twelve tiers");
        bool order = tiers.size() == kTierCount;
        for (size_t i = 0; i < tiers.size() && i < kTierCount; ++i) order &= tiers[i].id == kOrder[i];
        Check(order, "wood through enchanted, in that order");

        bool levels_rise = true, stats_rise = true, reqs_right = true, recipes_ok = true;
        bool stations_ok = true, models_ok = true, ores_ok = true, value_rises = true;
        const auto recipe_for = [&](const string& id) -> const ItemDef* {
            for (const ItemDef* r : items.Recipes()) if (r->craft_result == id) return r;
            return nullptr;
        };
        const auto main_stat = [](const ItemDef* d) {
            return d->attack_bonus + d->strength_bonus + d->defence_bonus + d->ranged_bonus + d->magic_bonus;
        };
        for (size_t i = 0; i < tiers.size(); ++i) {
            const TierDef& t = tiers[i];
            if (i > 0 && t.level < tiers[i - 1].level) levels_rise = false;
            for (const char* piece : kPieces) {
                const ItemDef* d = items.Get(items.TierPiece(t.id, piece));
                if (!d) { recipes_ok = false; continue; }
                if (i > 0) {
                    const ItemDef* prev = items.Get(items.TierPiece(tiers[i - 1].id, piece));
                    if (prev && main_stat(d) <= main_stat(prev)) stats_rise = false;
                    if (prev && d->value <= prev->value) value_rises = false;
                }
                const string skill = (string(piece) == "sword" || string(piece) == "spear") ? "Attack" : string(piece) == "bow" ? "Ranged"
                                   : string(piece) == "staff" ? "Magic" : "Defence";
                const int s_id = SkillFromName(skill);
                if (t.level > 1 && (d->requirements.size() != 1 || d->requirements.count(s_id) == 0 ||
                                    d->requirements.at(s_id) != t.level)) reqs_right = false;
                if (t.level <= 1 && !d->requirements.empty()) reqs_right = false;
                const ItemDef* r = recipe_for(d->id);
                if (!r) { recipes_ok = false; continue; }
                if (items.StationFor(*r) != (t.wood ? CraftStation::Workbench : CraftStation::Anvil)) stations_ok = false;
                if (d->slot == SLOT_WEAPON && d->model != string(piece) + "_" + t.id) models_ok = false;
            }
            if (!t.wood) {
                // Dracon is beaten out of a dragon's fang and enchanted is
                // quenched in the Reverie: both have a bar and no ore.
                const ItemDef* bar = items.Get(t.bar);
                if (!bar || !bar->metal || !recipe_for(t.bar)) ores_ok = false;
                if (!t.ore.empty()) {
                    const ItemDef* ore = items.Get(t.ore);
                    if (!ore || !ore->metal) ores_ok = false;
                }
            }
        }
        Check(levels_rise, "each tier needs at least the level of the one before");
        Check(stats_rise, "every piece is stronger than the same piece a tier down");
        Check(value_rises, "and worth more");
        Check(reqs_right, "every piece needs its tier's level in Attack, Ranged, Magic or Defence");
        Check(recipes_ok, "every tier makes all eight pieces, each with a recipe");
        Check(stations_ok, "wooden pieces are made at a workbench and metal ones at an anvil");
        Check(ores_ok, "every metal tier has a bar with a recipe, and an ore if it is mined");
        Check(models_ok, "every tier's sword, spear, bow and staff name their own model");

        // The items that were there before tiers are the tier pieces now.
        for (const char* old : {"bronze_sword", "iron_sword", "steel_longsword", "oak_shortbow",
                                "wooden_shield", "iron_shield", "iron_helm", "iron_body"}) {
            const ItemDef* d = items.Get(old);
            Check(d && !d->tier.empty(), string(old) + " is still an item, and has a tier");
        }

        // Every ore can be dug up somewhere, by a miner of the right level.
        std::map<string, int> seams;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects())
                if (o.skill == "Mining" && !o.yield.empty())
                    seams[o.yield] = seams.count(o.yield) ? std::min(seams[o.yield], o.skill_level) : o.skill_level;
        }
        for (const TierDef& t : tiers) {
            if (t.wood || t.ore.empty()) continue;
            Check(seams.count(t.ore) > 0, t.ore + " can be mined somewhere in the world");
            if (seams.count(t.ore)) Check(seams[t.ore] <= t.mining, t.ore + " can be mined at Mining " + std::to_string(t.mining));
        }
        // And what the two smelted tiers are made of has to be gettable too.
        for (const char* what : {"dragon_fang", "dream_shard"})
            Check(items.Get(what) != nullptr, string(what) + " exists, for the tiers that are not mined");
        Check(seams.count("coal") > 0 && seams.count("dream_shard") > 0, "coal and dream crystals are both out there");

        // Art: every icon is its own picture, and every weapon has a layer for
        // every one of the hero's clips.
        {
            std::map<size_t, string> seen;
            int dupes = 0, icons = 0;
            for (const auto& kv : items.All()) {
                if (kv.second.tier.empty() || kv.second.icon.find("/tiers/") == string::npos) continue;
                std::ifstream f(kv.second.icon, std::ios::binary);
                const string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const size_t h = std::hash<string>{}(bytes);
                if (bytes.empty()) continue;
                ++icons;
                if (seen.count(h) && seen[h] != kv.second.icon) ++dupes;
                seen[h] = kv.second.icon;
            }
            Check(icons >= 112 && dupes == 0, "all " + std::to_string(icons) + " tier icons are different pictures");
        }
        {
            const SpriteDef* hero = sprites.Get("player_hero");
            int missing = 0, sheets = 0;
            std::set<size_t> attack_sheets;
            for (const TierDef& t : tiers)
                for (const char* kind : {"sword", "spear", "bow", "staff"}) {
                    const string model = string(kind) + "_" + t.id;
                    const bool spear = string(kind) == "spear";
                    for (const auto& clip : hero ? hero->clips : map<string, AnimClip>{}) {
                        // The work clips hold a tool, not a weapon, and picking herbs holds nothing.
                        if (clip.first == "chop" || clip.first == "mine" || clip.first == "fish" ||
                            clip.first == "gather") continue;
                        // A spear strikes with the thrust and nothing else does.
                        if (clip.first == (spear ? "attack" : "thrust")) continue;
                        // Rushing Strike is a melee move; a bow or a staff never leaps.
                        // Nor do they make the combos.
                        const bool melee_only = clip.first == "rush" || clip.first == "crush" || clip.first == "cleave" ||
                                                clip.first == "backhand" || clip.first == "spin";
                        if (melee_only && (string(kind) == "bow" || string(kind) == "staff")) continue;
                        const string path = "assets/characters/player_hero/layers/" + clip.first +
                                            "_4_weapon_" + model + ".png";
                        if (!fs::exists(path)) { ++missing; continue; }
                        ++sheets;
                        if (clip.first == "attack" || clip.first == "thrust") {
                            std::ifstream f(path, std::ios::binary);
                            attack_sheets.insert(std::hash<string>{}(string(
                                (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>())));
                        }
                    }
                }
            Check(hero && missing == 0, "every tier weapon has a layer sheet for every hero clip (" +
                  std::to_string(sheets) + ")");
            Check(attack_sheets.size() == 48, "all 48 weapons look different in the hero's hand");
        }

        // --- the spear ------------------------------------------------------------------------
        // A thrust that keeps a fight at the end of the shaft: it reaches much
        // further than a sword down a narrower line, and shoves harder.
        {
            const ItemDef* sword = items.Get(items.TierPiece("iron", "sword"));
            const ItemDef* spear = items.Get(items.TierPiece("iron", "spear"));
            Check(spear && spear->name == "Iron Spear" && spear->kind == WeaponKind::Melee && spear->slot == SLOT_WEAPON,
                  "every metal tier has a spear: the Iron Spear is a melee weapon");
            Check(items.Get(items.TierPiece("wood", "spear")) && items.Get(items.TierPiece("demonite", "spear")),
                  "from a fire-hardened wooden spear to a demonite one");
            if (sword && spear) {
                Check(spear->reach >= 1.5f && sword->reach == 1.0f, "a spear reaches half as far again as a sword and more");
                Check(spear->sweep < 1.0f && spear->push > 1.0f, "down a narrower line, and it shoves harder");
                Check(spear->attack_clip == "thrust" && sprites.Get("player_hero") &&
                      sprites.Get("player_hero")->Find("thrust"), "it strikes with the hero's thrust clip");
                Check(main_stat(spear) < main_stat(sword) * 1.2f && spear->attack_speed > sword->attack_speed,
                      "the reach is paid for: a spear is slower than a sword of its metal");

                GameContext hand;
                hand.sprites = &sprites;  hand.items = &items;
                Player lancer, fencer;
                lancer.Init(hand, "player_hero");
                fencer.Init(hand, "player_hero");
                string why;
                lancer.inventory.Add(spear->id, 1);
                fencer.inventory.Add(sword->id, 1);
                lancer.skills.SetXp(SKILL_ATTACK, XpForLevel(20));
                fencer.skills.SetXp(SKILL_ATTACK, XpForLevel(20));
                lancer.EquipFromInventory(0, why);
                fencer.EquipFromInventory(0, why);
                Check(lancer.equipment.InSlot(SLOT_WEAPON) == spear->id && fencer.equipment.InSlot(SLOT_WEAPON) == sword->id,
                      "a spear and a sword are taken in hand");
                for (AttackType type : {AttackType::Light, AttackType::Strong, AttackType::Charged}) {
                    AttackProfile a = ProfileFor(type), b = ProfileFor(type);
                    lancer.ShapeForWeapon(a);
                    fencer.ShapeForWeapon(b);
                    const SDL_FRect ha = AttackHitbox(0, 0, FACE_RIGHT, a), hb = AttackHitbox(0, 0, FACE_RIGHT, b);
                    Check(ha.w > hb.w * 1.5f && ha.h < hb.h, "a spear's strike reaches further down a narrower line than a sword's");
                }
                Check(lancer.WeaponReach() > 1.5f && fencer.WeaponReach() == 1.0f, "and the reach is the weapon's");
                Check(lancer.AttackClip() == "thrust" && fencer.AttackClip() == "attack", "the spear thrusts and the sword swings");
                // An enemy at spear's length: out of a sword's reach, inside a spear's.
                AttackProfile light_spear = ProfileFor(AttackType::Light), light_sword = ProfileFor(AttackType::Light);
                lancer.ShapeForWeapon(light_spear);
                fencer.ShapeForWeapon(light_sword);
                const SDL_FRect body = {44.0f, -26.0f, 14.0f, 18.0f};
                Check(RectsOverlap(AttackHitbox(0, 0, FACE_RIGHT, light_spear), body) &&
                      !RectsOverlap(AttackHitbox(0, 0, FACE_RIGHT, light_sword), body),
                      "a monster a spear's length away is hit by a spear and not by a sword");
                // Smithed like a sword, at the anvil, at its tier's level.
                bool recipe = false;
                for (const ItemDef* r : items.Recipes())
                    if (r->craft_result == spear->id && r->craft_inputs.count("iron_bar")) recipe = true;
                Check(recipe, "an iron spear is smithed from iron bars");
            }
        }

        // Requirements hold, and the hero draws what is in hand.
        {
            Input input;
            std::mt19937 rng(3);
            GameContext ctx;
            ctx.sprites = &sprites; ctx.items = &items; ctx.trees = &trees; ctx.input = &input; ctx.rng = &rng;
            Player p;
            p.Init(ctx, "player_hero");
            p.inventory.Add("iron_sword", 1);
            p.inventory.Add("demonite_staff", 1);
            const auto slot_of = [&](const string& id) {
                for (int i = 0; i < p.inventory.SlotCount(); ++i) if (p.inventory.Slot(i).id == id) return i;
                return -1;
            };
            string why;
            Check(!p.EquipFromInventory(slot_of("iron_sword"), why) && why.find("Attack") != string::npos,
                  "a new character cannot wield an iron sword: " + why);
            LevelUp up;
            p.skills.AddXp(SKILL_ATTACK, XpForLevel(10), up);
            Check(p.EquipFromInventory(slot_of("iron_sword"), why), "at Attack 10 they can");
            Check(p.BuildLayerStyle(&items).weapon_model == "sword_iron", "and the hero holds the iron sword's model");
            Check(!p.EquipFromInventory(slot_of("demonite_staff"), why), "demonite needs Magic 70");
        }
    }

    // --- skill trees ------------------------------------------------------------------------
    Section("skill trees");
    {
        bool shape = true, milestones = true, techniques = true;
        bool ranks_ok = true, gaps = true, effects_known = true;
        for (int st = 0; st < 3; ++st) {
            const TalentTree& t = trees.Tree(static_cast<AttackStyle>(st));
            // Three full branches eight deep. The melee tree also has Footwork,
            // a fourth branch for moves made on the run, which is not counted.
            size_t core = 0;
            for (const TalentNode& n : t.nodes) core += n.branch < 3 ? 1 : 0;
            if (core != 24 || t.branches.size() < 3) shape = false;
            int tech = 0;
            int abilities = 0;
            for (int b = 0; b < 3; ++b)
                for (int r = 0; r < SkillTrees::ROWS; ++r) {
                    const TalentNode* n = t.At(b, r);
                    if (!n) { shape = false; continue; }
                    const TalentNode* row0 = t.At(0, r);
                    if (row0 && row0->level != n->level) milestones = false;
                    if (r > 0 && t.At(b, r - 1) && t.At(b, r - 1)->level >= n->level) milestones = false;
                    if (!n->technique.empty()) { ++tech; if (r != 2) techniques = false; }
                    if (!n->ability.empty()) { ++abilities; if ((r != 3 && r != 5) || n->cooldown <= 0.0f || (n->stamina_cost <= 0 && n->mana_cost <= 0)) techniques = false; }
                    if (n->Passive() && n->effects.empty()) techniques = false;
                    if (n->Passive() && (r == 0 || r == 1) && n->ranks != 3) ranks_ok = false;
                    if (n->Passive() && (r == 4 || r == 6) && n->ranks != 2) ranks_ok = false;
                    if ((r == 4 || r == 6) && !n->Passive()) ranks_ok = false;
                    if (!n->Passive() && n->ranks != 1) ranks_ok = false;
                    if (r == SkillTrees::ROWS - 1 && (n->ranks != 1 || !n->Passive())) ranks_ok = false;
                }
            if (tech != 3 || abilities != 6) techniques = false;
            int total = 0;
            for (const TalentNode& n : t.nodes) total += n.ranks;
            if (total < 42 || total > 43) ranks_ok = false;
            // Levels come slower the higher they are, so the rows come closer:
            // nothing past the first ability is more than eight levels on.
            for (int r = 4; r < SkillTrees::ROWS; ++r)
                if (t.At(0, r) && t.At(0, r - 1) && t.At(0, r)->level - t.At(0, r - 1)->level > 8) gaps = false;
            // Every effect a node names is one the game reads.
            static const std::set<string> known = {
                "damage", "speed", "crit", "crit_damage", "execute", "momentum", "charge", "knockback", "charged_damage",
                "kill_stamina", "defence", "stamina_regen", "riposte", "low_hp_damage", "lifesteal", "rushing_strike",
                "projectile_speed", "long_shot", "move_speed", "hit_run", "pierce", "stamina", "first_blood", "mana_cost",
                "attunement", "mana_regen", "crit_mana", "homing", "elemental",
                "bleed", "punish", "block_cost", "weak_point", "evade", "echo", "max_mana", "hurt_mana"};
            for (const TalentNode& n : t.nodes)
                for (const auto& [effect, amount] : n.effects)
                    if (!known.count(effect)) { effects_known = false; SDL_Log("  unknown effect '%s' on %s", effect.c_str(), n.id.c_str()); }
        }
        Check(shape, "each style has a tree of three branches eight nodes deep");
        Check(ranks_ok, "two passives of three ranks, a technique, an ability, a passive of two, a second ability, a second passive of two and a capstone: forty-two ranks a tree");
        Check(99 / SkillTrees::LEVELS_PER_POINT == 33, "against thirty-three points by level 99: a build, not a checklist");
        Check(gaps, "past the first ability no row is more than eight levels after the one before");
        Check(effects_known, "every effect a node names is one the game reads");
        {
            const TalentTree& melee = trees.Tree(AttackStyle::Melee);
            const TalentNode* rush = melee.At(3, 1);
            Check(melee.BranchCount() == 4 && melee.branches.size() == 4 && melee.branches[3] == "Footwork",
                  "the melee tree has a fourth branch, Footwork");
            Check(rush && rush->id == "rushing_strike" && rush->level == 15 &&
                  rush->effects.count("rushing_strike"), "Rushing Strike is in it, at Attack 15");
            Check(trees.Tree(AttackStyle::Ranged).BranchCount() == 3 && trees.Tree(AttackStyle::Magic).BranchCount() == 3,
                  "the ranged and magic trees keep their three");
        }
        Check(milestones, "every row is one milestone level, rising down the tree");
        Check(techniques, "each tree teaches three techniques and six abilities, each ability with a cooldown and a cost, and every other node does something");
        Check(trees.Tree(AttackStyle::Melee).skill == SKILL_ATTACK && trees.Tree(AttackStyle::Ranged).skill == SKILL_RANGED &&
              trees.Tree(AttackStyle::Magic).skill == SKILL_MAGIC, "melee, ranged and magic are earned by Attack, Ranged and Magic");

        Skills sk;
        Talents t;
        t.SetDatabase(&trees);
        Check(t.PointsEarned(AttackStyle::Melee, sk) == 0, "a level 1 character has no points");
        LevelUp up;
        sk.AddXp(SKILL_ATTACK, XpForLevel(17), up);
        Check(t.PointsEarned(AttackStyle::Melee, sk) == 5 && t.PointsEarned(AttackStyle::Ranged, sk) == 0,
              "Attack 17 earns five melee points, one every three levels, and no ranged ones");
        Check(t.CanLearn("flurry", sk) == Talents::Why::Prerequisite, "a node needs the one above it");
        Check(t.CanLearn("whirlwind", sk) == Talents::Why::Level, "and its milestone level");
        Check(t.Learn("keen_edge", sk) && t.Learn("flurry", sk) && t.Learn("heavy_hand", sk) && t.Learn("keen_edge", sk) &&
              t.Learn("flurry", sk), "five points buy three nodes and a second rank of two of them");
        Check(t.Rank("keen_edge") == 2 && t.Rank("flurry") == 2 && t.PointsSpent(AttackStyle::Melee) == 5, "a rank is a point");
        Check(t.CanLearn("thick_skin", sk) == Talents::Why::NoPoints, "and then there are none left");
        Check(fabsf(t.Effect("damage", AttackStyle::Melee) - 0.08f) < 1e-4f && t.Effect("damage", AttackStyle::Ranged) == 0.0f,
              "two ranks of a melee damage node are twice one, and help melee and not the bow");
        Check(fabsf(t.Global("charge") - 0.08f) < 1e-4f, "a global node applies whatever is held");
        {
            Skills high;
            LevelUp lu;
            high.AddXp(SKILL_ATTACK, XpForLevel(60), lu);
            Talents full;
            full.SetDatabase(&trees);
            full.Learn("keen_edge", high); full.Learn("keen_edge", high); full.Learn("keen_edge", high);
            Check(full.Rank("keen_edge") == 3 && full.CanLearn("keen_edge", high) == Talents::Why::Learned && !full.Learn("keen_edge", high),
                  "a node with every rank bought takes no more");
            const json saved = full.ToJson();
            Talents back;
            back.SetDatabase(&trees);
            back.FromJson(saved);
            Check(back.Rank("keen_edge") == 3, "ranks survive a save");
            Talents old;
            old.SetDatabase(&trees);
            old.FromJson(json{{"learned", {"keen_edge", "flurry", "bloodlust"}}});
            Check(old.Rank("keen_edge") == 1 && old.Rank("flurry") == 1 && !old.Has("bloodlust"),
                  "a save from before ranks has one of each, and a node that has left the tree is let go");
        }
        Check(!t.ToggleTechnique("whirlwind"), "an unlearned technique cannot be chosen");
        t.Reset(AttackStyle::Melee);
        Check(t.PointsSpent(AttackStyle::Melee) == 0 && t.PointsFree(AttackStyle::Melee, sk) == 5,
              "unlearning a tree gives every point back");

        // --- one path, one tree ------------------------------------------------------------
        {
            Skills all;
            LevelUp lu;
            for (int skill : {SKILL_ATTACK, SKILL_RANGED, SKILL_MAGIC}) all.AddXp(skill, XpForLevel(50), lu);
            Talents hero;
            hero.SetDatabase(&trees);
            hero.SetPath(AttackStyle::Melee);
            Check(hero.HasPath() && hero.Open(AttackStyle::Melee) && !hero.Open(AttackStyle::Ranged) && !hero.Open(AttackStyle::Magic),
                  "the hero's path is the blade, and only its tree is open");
            Check(hero.CanLearn("keen_edge", all) == Talents::Why::Ok && hero.CanLearn("steady_aim", all) == Talents::Why::OtherPath &&
                  hero.CanLearn("potency", all) == Talents::Why::OtherPath && !hero.Learn("steady_aim", all),
                  "nothing can be learned from a tree that is another character's, whatever the levels");
            // A save from before the paths: a hero who had bought into the bow.
            Talents before;
            before.SetDatabase(&trees);
            before.SetPath(AttackStyle::Melee);
            before.FromJson(json{{"learned", {"keen_edge", "steady_aim", "eagle_eye", "volley"}}, {"technique", {{"ranged", "volley"}, {"melee", ""}}}});
            Check(before.Has("keen_edge") && !before.Has("steady_aim") && !before.Has("volley") && before.Technique(AttackStyle::Ranged).empty() &&
                  before.PointsSpent(AttackStyle::Ranged) == 0,
                  "a save from before the paths keeps what is its path's and loses the rest");
            for (const char* who : {"player_hero", "player_warden", "player_wayfarer"}) {
                Player p;
                GameContext pc;
                pc.sprites = &sprites; pc.items = &items; pc.trees = &trees;
                p.Init(pc, who);
                Check(p.talents.HasPath() && p.talents.Path() == Player::AffinityFor(who),
                      string(who) + "'s tree is the tree of their affinity");
            }
        }

        // --- abilities: learned, carried, put away ----------------------------------------------
        {
            Skills high;
            LevelUp lu;
            high.AddXp(SKILL_ATTACK, XpForLevel(60), lu);
            Talents a;
            a.SetDatabase(&trees);
            a.SetPath(AttackStyle::Melee);
            Check(a.CycleAbility("bash") == -1 && a.Ability(0) == nullptr, "an ability that is not learned cannot be carried");
            for (const char* id : {"thick_skin", "second_wind", "lunge", "bash", "keen_edge", "flurry", "whirlwind", "sunder"}) a.Learn(id, high);
            Check(a.Has("bash") && a.Has("sunder") && a.CycleAbility("bash") == 0 && a.Ability(0) && a.Ability(0)->ability == "bash",
                  "a learned ability goes into the first slot");
            Check(a.CycleAbility("bash") == 1 && a.Ability(0) == nullptr && a.SlotOf("bash") == 1, "then the second");
            Check(SkillTrees::ABILITY_SLOTS == 3 && a.CycleAbility("bash") == 2 && a.SlotOf("bash") == 2, "then the third");
            Check(a.CycleAbility("sunder") == 0 && a.CycleAbility("bash") == -1 && a.SlotOf("bash") == -1 && a.SlotOf("sunder") == 0,
                  "then away; and another takes a slot of its own");
            const json saved = a.ToJson();
            Talents back;
            back.SetDatabase(&trees);
            back.SetPath(AttackStyle::Melee);
            back.FromJson(saved);
            Check(back.SlotOf("sunder") == 0 && back.Ability(0) && back.Ability(0)->cooldown > 0.0f, "what is carried survives a save");
            a.Reset(AttackStyle::Melee);
            Check(a.Ability(0) == nullptr && a.PointsSpent(AttackStyle::Melee) == 0, "and unlearning the tree empties the slots");
        }

        // --- in the world ---------------------------------------------------------------
        Input input;
        std::mt19937 rng(91);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        // A character with a weapon, a skill at a level, and the nodes named.
        const auto fighter = [&](World& w, const string& weapon, int skill, int level,
                                 std::initializer_list<const char*> nodes, const char* technique) {
            // Whoever's tree it is: the bow's nodes are the warden's to learn.
            w.player.Init(ctx, skill == SKILL_RANGED ? "player_warden" : skill == SKILL_MAGIC ? "player_wayfarer" : "player_hero");
            if (!w.LoadMap("overworld", "start", ctx)) return false;
            w.enemies.clear();
            w.clock.Set(1, 12.0f);
            LevelUp lu;
            w.player.skills.AddXp(skill, XpForLevel(level), lu);
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(40), lu);
            w.player.SyncHitpoints();
            w.player.hp = w.player.max_hp;
            w.player.SyncMana();
            w.player.RestoreMana();
            w.player.equipment.Equip(SLOT_WEAPON, weapon);
            for (const char* n : nodes) w.player.talents.Learn(n, w.player.skills);
            if (technique) w.player.talents.ToggleTechnique(technique);
            w.player.facing = FACE_RIGHT;
            return true;
        };
        const auto spawn = [&](World& w, const string& type, float dx, float dy) -> Enemy* {
            const EnemyDef* stats = enemy_db.Get(type);
            if (!stats) return nullptr;
            EnemySpawnDef def;
            def.type = type; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            Enemy* raw = e.get();
            w.enemies.push_back(std::move(e));
            return raw;
        };
        // Holds the heavy button long enough for a full charge, then lets go.
        const auto charged = [&](World& w) {
            input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
            frames(w, 80);
            input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
        };

        // Whirlwind: everything around, not just what is in front.
        {
            const auto round = [&](bool with_technique) {
                World w;
                if (!fighter(w, "bronze_sword", SKILL_ATTACK, 30, {"keen_edge", "flurry", "whirlwind"},
                             with_technique ? "whirlwind" : nullptr)) return 0;
                // One in front and three behind: straight back and over each
                // shoulder. A plain charged swing is 58 pixels wide, which is
                // wide enough to catch a deer standing level with the player
                // 28 pixels above or below -- so those two were inside the
                // "in front" arc all along, and the check only passed when they
                // had wandered out of it. Behind the player, only a spin reaches.
                const float marks[4][2] = {{30, 0}, {-30, 0}, {-22, 22}, {-22, -22}};
                vector<Enemy*> ring;
                for (const auto& m : marks) ring.push_back(spawn(w, "deer", m[0], m[1]));
                // Held for a full charge, the deer are put back on their marks
                // before the release. Idle deer amble, and in the eighty frames
                // of a charge one could wander thirty pixels into the arc in
                // front -- which is a test of where a deer went, not of what the
                // swing reaches, and it passed or failed on the random numbers.
                input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                frames(w, 80);
                for (size_t i = 0; i < ring.size(); ++i)
                    if (ring[i]) { ring[i]->x = w.player.x + marks[i][0]; ring[i]->y = w.player.y + marks[i][1]; }
                input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                frames(w, 40);
                int struck = 0;
                for (Enemy* e : ring) if (e && e->HealthBarVisible()) ++struck;
                return struck;
            };
            const int plain = round(false), spun = round(true);
            Check(plain <= 2, "a plain charged swing strikes what is in front (" + std::to_string(plain) + " of 4)");
            Check(spun == 4, "a whirlwind strikes all four deer around the player (" + std::to_string(spun) + ")");
        }
        // Lunge: the player covers ground.
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 30, {"thick_skin", "second_wind", "lunge"}, "lunge")) {
                const float x0 = w.player.x;
                charged(w);
                frames(w, 40);
                Check(w.player.x - x0 > 35.0f, "a lunge carries the player forward (" +
                      std::to_string(static_cast<int>(w.player.x - x0)) + " px)");
                Check(w.player.Profile().defence_bonus >= 4, "thick skin adds defence");
            }
        }
        // Volley and piercing shot.
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 30, {"quick_draw", "fleet_foot", "volley"}, "volley")) {
                w.projectiles.clear();
                charged(w);
                size_t most = 0;
                for (int f = 0; f < 60; ++f) { frames(w, 1); most = std::max(most, w.projectiles.size()); }
                Check(most == 5, "a volley looses five arrows (" + std::to_string(most) + ")");
            }
            World w2;
            if (fighter(w2, "oak_shortbow", SKILL_RANGED, 30, {"steady_aim", "eagle_eye", "piercing_shot"}, "piercing_shot")) {
                w2.projectiles.clear();
                charged(w2);
                int pierce = -1;
                for (int f = 0; f < 60 && pierce < 0; ++f) {
                    frames(w2, 1);
                    if (!w2.projectiles.empty()) pierce = w2.projectiles.front().pierce_left;
                }
                Check(pierce >= 8, "a piercing shot passes through a crowd");
            }
            World w3;
            if (fighter(w3, "oak_shortbow", SKILL_RANGED, 30, {"trail_legs", "broadheads", "arrow_rain"}, "arrow_rain")) {
                spawn(w3, "deer", 90, 0);
                charged(w3);
                bool rain = false;
                for (int f = 0; f < 60 && !rain; ++f) {
                    frames(w3, 1);
                    for (const GroundEffect& g : w3.ground_effects) if (g.hit_mult >= 0.0f) rain = true;
                }
                Check(rain, "arrow rain calls a strike down ahead of the player");
                Check(w3.player.MaxStamina() > Player::MAX_STAMINA * 1.05f, "trail legs adds stamina");
            }
            // And it is a rain, not a thump. It used to be one hit and a disc
            // that was gone in a third of a second.
            World w4;
            if (fighter(w4, "oak_shortbow", SKILL_RANGED, 30, {"trail_legs", "broadheads", "arrow_rain"}, "arrow_rain")) {
                // Three deer that will not die of it and are held where they are
                // put: one under the middle of where a charged shot comes down,
                // one well outside it, and one that walks in late.
                struct Mark { Enemy* who; float x, y; int hp_at_start = 0, hits = 0, last_hp = 0; };
                vector<Mark> marks;
                const float cx = w4.player.x + 110.0f, cy = w4.player.y + 8.0f;
                for (const auto& at : {std::pair<float, float>{0.0f, 0.0f}, {GroundEffect::RAIN_RADIUS + 60.0f, 0.0f},
                                       {0.0f, GroundEffect::RAIN_RADIUS + 90.0f}}) {
                    Enemy* e = spawn(w4, "deer", 0, 0);
                    if (!e) continue;
                    e->hp = e->max_hp = 5000;
                    marks.push_back({e, cx + at.first, cy + at.second});
                }
                const auto hold = [&] {
                    for (Mark& m : marks) { m.who->x = m.x; m.who->y = m.y; }
                };
                Check(marks.size() == 3, "three deer to rain on");
                if (marks.size() == 3) {
                    hold();
                    input.Update(dt); key(SDLK_K, true); w4.Update(dt, ctx);
                    for (int f = 0; f < 80; ++f) { frames(w4, 1); hold(); }
                    input.Update(dt); key(SDLK_K, false); w4.Update(dt, ctx);
                    for (Mark& m : marks) m.hp_at_start = m.last_hp = m.who->hp;

                    float raining = 0.0f, seen = 0.0f, telegraph = 0.0f;
                    int volleys = 0, hp_when_it_stopped = -1;
                    bool found = false, right_shape = true, walked_in = false;
                    for (int f = 0; f < 60 * 6; ++f) {
                        frames(w4, 1);
                        hold();
                        const GroundEffect* rain = nullptr;
                        for (const GroundEffect& g : w4.ground_effects) if (g.rain) rain = &g;
                        if (rain) {
                            found = true;
                            seen += dt;
                            right_shape &= rain->radius == GroundEffect::RAIN_RADIUS && rain->hit_mult > 0.0f &&
                                           rain->knockback < 10.0f && rain->from_player;
                            if (!rain->Active()) telegraph += dt;
                            else if (rain->life > GroundEffect::RAIN_LINGER) raining += dt;
                            else if (hp_when_it_stopped < 0 && rain->life < GroundEffect::RAIN_LINGER - 0.12f)
                                hp_when_it_stopped = marks[0].who->hp;
                            volleys = std::max(volleys, rain->volleys);
                            // Half way through, the third deer walks in under it.
                            if (!walked_in && rain->Active() && rain->max_life - rain->life > 1.2f) {
                                walked_in = true;
                                Check(marks[2].who->hp == marks[2].hp_at_start, "a deer that is not under it yet has not been touched");
                                marks[2].x = cx + 20.0f; marks[2].y = cy;
                                hold();
                            }
                        }
                        for (Mark& m : marks) {
                            if (m.who->hp < m.last_hp) ++m.hits;
                            m.last_hp = m.who->hp;
                        }
                        if (found && !rain) break;
                    }
                    Check(found && right_shape, "a charged shot with Arrow Rain is a rain: a wide circle, the player's, that pins and does not throw");
                    Check(telegraph > 0.2f && telegraph < 0.6f, "it is seen coming");
                    Check(raining >= 2.0f, "and it comes down for two seconds and more (" + std::to_string(raining) + "s)");
                    Check(volleys == 7, "in seven volleys (" + std::to_string(volleys) + ")");
                    Check(marks[0].hits >= 4 && marks[0].hits <= volleys,
                          "what stands under it is hit again and again, a volley at a time (" + std::to_string(marks[0].hits) + ")");
                    Check(marks[1].hits == 0 && marks[1].who->hp == marks[1].hp_at_start, "what stands outside it is not hit at all");
                    Check(walked_in && marks[2].hits >= 1 && marks[2].hits < marks[0].hits,
                          "what walks in half way through catches the rest of it, and only the rest");
                    Check(hp_when_it_stopped >= 0 && marks[0].who->hp == hp_when_it_stopped,
                          "and when the arrows stop, they stop: the last half second is the ones standing in the ground");
                    Check(seen < GroundEffect::RAIN_TIME + GroundEffect::RAIN_LINGER + 0.8f && w4.ground_effects.empty(),
                          "then it is gone");
                }

                // Take Aim makes one sure shot, not seven.
                GroundEffect aimed;
                aimed.x = cx; aimed.y = cy; aimed.radius = GroundEffect::RAIN_RADIUS;
                aimed.life = aimed.max_life = 1.5f; aimed.tick_interval = 0.4f; aimed.rain = true; aimed.sure_crit = true;
                aimed.hit_mult = 0.3f; aimed.style = AttackStyle::Ranged; aimed.owner = w4.player.Profile();
                w4.AddGroundEffect(aimed);
                frames(w4, 2);
                bool first_only = !w4.ground_effects.empty();
                for (const GroundEffect& g : w4.ground_effects) first_only &= g.volleys == 1 && !g.sure_crit;
                Check(first_only, "a rain loosed with Take Aim has its sure hit in the first volley, and the rest are arrows");

                // A friend's screen is told it is a rain, so it can draw one.
                Check(net::PROTOCOL_VERSION >= 4, "the line knows patches come in kinds");
                net::Snapshot told;
                net::PatchState patch;
                patch.x = 120; patch.y = -40; patch.radius = 56; patch.life = 21; patch.max_life = 29; patch.kind = 1;
                told.patches.push_back(patch);
                told.patches.push_back(net::PatchState{});
                net::Snapshot heard;
                Check(net::Decode(net::Encode(told), heard) && heard.patches.size() == 2 && heard.patches[0].kind == 1 &&
                      heard.patches[0].life == 21 && heard.patches[1].kind == 0,
                      "and a rain crosses it as a rain, beside a patch that is only a patch");
            }
        }
        // Nova and barrage, and what they cost.
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 30, {"potency", "focus", "nova"}, "nova")) {
                const SpellDef* spell = spells.BestFor(w.player.SelectedElement(), 30);
                const int mana0 = w.player.Mana();
                w.projectiles.clear();
                charged(w);
                size_t most = 0;
                for (int f = 0; f < 60; ++f) { frames(w, 1); most = std::max(most, w.projectiles.size()); }
                Check(most == 8, "a nova bursts into eight bolts (" + std::to_string(most) + ")");
                const int spent = mana0 - w.player.Mana();
                const int expect = spell ? static_cast<int>(std::lround(spell->mana * 2 * 0.95f)) : -1;
                Check(spell && spent >= expect - 1 && spent <= expect + 1,
                      "for twice a bolt's mana, less focus (" + std::to_string(spent) + " of " + std::to_string(expect) + ")");
            }
            World w2;
            if (fighter(w2, "novice_staff", SKILL_MAGIC, 30, {"ward", "seeker", "meteor"}, "meteor")) {
                spawn(w2, "deer", 90, 0);
                charged(w2);
                bool meteor = false;
                for (int f = 0; f < 60 && !meteor; ++f) {
                    frames(w2, 1);
                    for (const GroundEffect& g : w2.ground_effects) if (g.hit_mult >= 0.0f && g.element != Element::None) meteor = true;
                }
                Check(meteor, "a meteor brings the element down as a strike");
            }
        }
        // Passives reach the numbers the game uses.
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 50, {"keen_edge", "flurry"}, nullptr)) {
                const float fast = w.player.WeaponSpeed();
                w.player.equipment.Equip(SLOT_WEAPON, "oak_shortbow");
                const float bow = w.player.WeaponSpeed();
                w.player.equipment.Equip(SLOT_WEAPON, "bronze_sword");
                Check(fast < 1.0f && bow == items.Get("oak_shortbow")->attack_speed,
                      "flurry quickens a sword and not a bow");
                Check(w.player.TalentDamage(AttackStyle::Melee, AttackType::Light) > 1.05f &&
                      w.player.TalentDamage(AttackStyle::Ranged, AttackType::Light) == 1.0f,
                      "keen edge adds melee damage and not ranged");
                const json saved = w.player.ToJson();
                Player back;
                back.FromJson(saved, ctx);
                Check(back.talents.Has("keen_edge") && back.talents.Has("flurry") && !back.talents.Has("whirlwind"),
                      "learned nodes survive a save");
            }
        }

        // --- abilities, in the world ---------------------------------------------------------------
        // Guard held and an attack button. H is the guard, J the light, K the heavy.
        const auto ability = [&](World& w, SDL_Keycode button) {
            input.Update(dt); key(SDLK_H, true); w.Update(dt, ctx);
            input.Update(dt); key(button, true); w.Update(dt, ctx);
            input.Update(dt); key(button, false); key(SDLK_H, false); w.Update(dt, ctx);
        };
        const auto carry = [&](World& w, const char* node, int slot) {
            while (w.player.talents.SlotOf(node) != slot && w.player.talents.Has(node)) w.player.talents.CycleAbility(node);
            return w.player.talents.SlotOf(node) == slot;
        };
        // Bash and Sunder: the hero.
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 60,
                        {"thick_skin", "second_wind", "lunge", "bash", "keen_edge", "flurry", "whirlwind", "sunder"}, nullptr)) {
                Check(carry(w, "bash", 0) && carry(w, "sunder", 1), "the hero carries Bash on the light button and Sunder on the heavy");
                Enemy* boar = spawn(w, "boar", 34, 0);
                const float stamina = w.player.Stamina();
                const int hp = boar ? boar->hp : 0;
                ability(w, SDLK_J);
                Check(boar && boar->Staggered() && w.player.AbilityCooldown(0) > 7.0f && w.player.Stamina() < stamina - 10.0f,
                      "guard and light: the bash staggers what is in front, costs breath and starts its cooldown");
                Check(!w.player.Attacking(), "and the press was the ability's, not a swing");
                const float cd = w.player.AbilityCooldown(0);
                const float after = w.player.Stamina();
                ability(w, SDLK_J);
                Check(w.player.AbilityCooldown(0) <= cd && w.player.Stamina() >= after - 0.01f, "pressed again before it is back, nothing happens and nothing is spent");
                if (boar) {
                    boar->hp = boar->max_hp;
                    const int defence = boar->Profile().defence_level + boar->Profile().defence_bonus;
                    w.player.GainStamina(100.0f);
                    frames(w, 40);
                    boar->x = w.player.x + 34.0f; boar->y = w.player.y;
                    ability(w, SDLK_K);
                    const int sundered = boar->Profile().defence_level + boar->Profile().defence_bonus;
                    Check(boar->Sundered() && sundered < defence && w.player.AbilityCooldown(1) > 10.0f,
                          "guard and heavy: Sunder leaves its defence down by a third (" + std::to_string(defence) + " to " + std::to_string(sundered) + ")");
                    (void)hp;
                }
                input.Update(dt); key(SDLK_J, true); w.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx);
                Check(w.player.Attacking(), "without the guard held, the light button is still a swing");
            }
        }
        // War Cry, and the passives that ask when.
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 60, {"heavy_hand", "bruiser", "ground_slam", "war_cry", "thick_skin", "second_wind", "lunge", "bash", "riposte"}, nullptr)) {
                carry(w, "war_cry", 0);
                Enemy* near = spawn(w, "boar", 60, 0);
                const float before = w.player.TalentDamage(AttackStyle::Melee, AttackType::Light);
                ability(w, SDLK_J);
                Check(w.player.WarCry() && fabsf(w.player.TalentDamage(AttackStyle::Melee, AttackType::Light) - before - Player::WAR_CRY_DAMAGE) < 1e-4f &&
                      near && near->Staggered(), "a war cry staggers what is near and leaves the hero hitting a quarter harder");
                frames(w, static_cast<int>(Player::WAR_CRY_TIME * 60.0f) + 10);
                Check(!w.player.WarCry() && fabsf(w.player.TalentDamage(AttackStyle::Melee, AttackType::Light) - before) < 1e-4f, "for eight seconds");
                Check(!w.player.RiposteReady(), "no riposte is owed until a blow is caught");
                w.player.NoteBlock();
                Check(w.player.RiposteReady(), "a blow caught on the shield owes one");
                frames(w, static_cast<int>(Player::RIPOSTE_WINDOW * 60.0f) + 5);
                Check(!w.player.RiposteReady(), "for three seconds");
                World plain;
                if (fighter(plain, "bronze_sword", SKILL_ATTACK, 60, {"keen_edge"}, nullptr)) {
                    plain.player.NoteBlock();
                    Check(!plain.player.RiposteReady(), "and a hero who has not learned Riposte is owed nothing");
                }
            }
        }
        // Tumble, Hunter's Mark and Caltrops: the warden.
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 60, {"quick_draw", "fleet_foot", "volley", "tumble", "steady_aim", "eagle_eye", "piercing_shot", "hunters_mark"}, nullptr)) {
                carry(w, "tumble", 0); carry(w, "hunters_mark", 1);
                const float x0 = w.player.x;
                w.player.facing = FACE_RIGHT;
                ability(w, SDLK_J);
                Check(w.player.Untouchable(), "a tumble is untouchable while it lasts");
                Check(w.HitPlayer(9, CombatProfile{}, w.player.x + 20.0f, w.player.y) == 0, "a blow that lands mid-roll lands on nothing");
                frames(w, 40);
                Check(!w.player.Untouchable() && x0 - w.player.x > 45.0f, "standing still, the roll goes back the way the warden came (" +
                      std::to_string(static_cast<int>(x0 - w.player.x)) + " px)");
                Enemy* deer = spawn(w, "deer", 120, 0);
                frames(w, 2);
                ability(w, SDLK_K);
                Check(deer && deer->Marked() && !deer->Sundered(), "Hunter's Mark marks what is in reach");
                World w2;
                if (fighter(w2, "oak_shortbow", SKILL_RANGED, 60, {"trail_legs", "broadheads", "arrow_rain", "caltrops"}, nullptr)) {
                    carry(w2, "caltrops", 0);
                    ability(w2, SDLK_J);
                    bool iron = false;
                    for (const GroundEffect& g : w2.ground_effects) iron |= g.stagger > 0.0f && g.from_player && g.max_life > 5.0f;
                    Check(iron, "caltrops lie on the ground for six seconds, stopping what crosses them");
                }
            }
        }
        // Blink, Arcane Pulse and Mana Shield: the wayfarer.
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 60, {"flow", "swift_casting", "barrage", "blink", "potency", "focus", "nova", "arcane_pulse"}, nullptr)) {
                carry(w, "blink", 0); carry(w, "arcane_pulse", 1);
                w.player.facing = FACE_RIGHT;
                const float x0 = w.player.x;
                const int mana = w.player.Mana();
                ability(w, SDLK_J);
                Check(w.player.x - x0 > 60.0f && w.player.Mana() < mana && !w.CurrentMap().Blocked(w.player.Bounds()),
                      "a blink is a step through the air, to somewhere that can be stood on (" + std::to_string(static_cast<int>(w.player.x - x0)) + " px)");
                w.projectiles.clear();
                ability(w, SDLK_K);
                Check(w.projectiles.size() == 10, "an arcane pulse is ten bolts in a ring (" + std::to_string(w.projectiles.size()) + ")");
                bool wayfarers = !w.projectiles.empty();
                for (const Projectile& p : w.projectiles) wayfarers &= p.from_player && p.owner_local;
                Check(wayfarers, "and they are the wayfarer's own");

                World w2;
                if (fighter(w2, "novice_staff", SKILL_MAGIC, 60, {"ward", "seeker", "meteor", "mana_shield"}, nullptr)) {
                    carry(w2, "mana_shield", 0);
                    ability(w2, SDLK_J);
                    const int hp = w2.player.hp, mp = w2.player.Mana();
                    Check(w2.player.ManaShield(), "a mana shield is up for ten seconds");
                    Check(w2.player.AbsorbWithMana(10) == 5 && w2.player.Mana() == mp - 5 * Player::MANA_PER_HP,
                          "half of a blow is paid in mana, two a point");
                    w2.player.SetMana(1);
                    Check(w2.player.AbsorbWithMana(10) == 10 && w2.player.hp == hp, "and with no mana to pay with, all of it is blood");
                }
                // Attunement: the same element, again.
                w.player.NoteCast(Element::Water);
                for (int i = 0; i < 9; ++i) w.player.NoteCast(Element::Water);
                Check(w.player.AttuneStacks() == Player::ATTUNE_MAX, "casting one element over and over deepens it, up to five");
                w.player.NoteCast(Element::Fire);
                Check(w.player.AttuneStacks() == 0, "and changing element starts again");
            }
        }
        // --- the deeper rows ---------------------------------------------------------------------
        // A technique's strike lands once, the moment it goes off. It landed a
        // second time a frame later, on the effect's first tick.
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 30, {"trail_legs"}, nullptr)) {
                Enemy* deer = spawn(w, "deer", 90, 0);
                if (deer) {
                    deer->max_hp = deer->hp = 5000;
                    GroundEffect g;
                    g.x = deer->x; g.y = deer->y;
                    g.radius = 50.0f;
                    g.life = g.max_life = 0.35f;
                    g.burst = true;
                    g.owner = w.player.Profile();
                    g.style = AttackStyle::Ranged;
                    g.hit_mult = 1.0f;
                    w.AddGroundEffect(g);
                    const size_t before = w.texts.size();
                    frames(w, 6);
                    Check(w.texts.size() == before + 1, "a strike from above lands once on what it lands on (" +
                          std::to_string(w.texts.size() - before) + ")");
                }
            }
        }
        // One shot or one cast: how many came out, and what the first carried.
        const auto loose_one = [&](World& w, float& mult, bool& sure, int settle) {
            w.projectiles.clear();
            input.Update(dt); key(SDLK_J, true); w.Update(dt, ctx);
            input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx);
            for (int f = 0; f < 90 && w.projectiles.empty(); ++f) { input.Update(dt); w.Update(dt, ctx); }
            const int n = static_cast<int>(w.projectiles.size());
            if (n > 0) { mult = w.projectiles.front().damage_mult; sure = w.projectiles.front().sure_crit; }
            frames(w, settle);
            return n;
        };
        // The hero: Frenzy, Open Wounds, Shockwave, Stand Fast, and the third slot.
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 70,
                        {"keen_edge", "flurry", "whirlwind", "sunder", "momentum", "frenzy", "open_wounds", "open_wounds"}, nullptr)) {
                carry(w, "frenzy", 0);
                const float plain = w.player.WeaponSpeed();
                ability(w, SDLK_J);
                Check(w.player.Frenzied() && w.player.WeaponSpeed() < plain * 0.75f, "frenzied, the blade is a third faster");
                w.player.CountChainHit("Light");
                frames(w, static_cast<int>((Player::CHAIN_HOLD + 1.0f) * 60.0f));
                Check(w.player.ChainHits() == 1, "and the chain does not lapse between blows");
                frames(w, static_cast<int>((Player::FRENZY_TIME + Player::CHAIN_HOLD) * 60.0f));
                Check(!w.player.Frenzied() && fabsf(w.player.WeaponSpeed() - plain) < 1e-4f && w.player.ChainHits() == 0,
                      "for six seconds");

                // A wound bleeds out what it owes over four seconds.
                Enemy* deer = spawn(w, "deer", 200, 0);
                if (deer) {
                    deer->max_hp = deer->hp = 5000;
                    deer->Bleed(40.0f);
                    Check(deer->Bleeding(), "a wound left open bleeds");
                    frames(w, static_cast<int>((Enemy::BLEED_TIME + 0.3f) * 60.0f));
                    Check(!deer->Bleeding() && deer->hp <= 5000 - 39 && deer->hp >= 5000 - 41,
                          "for what it owes, over four seconds (" + std::to_string(5000 - deer->hp) + ")");
                    // And a chain three deep opens one.
                    bool opened = false;
                    int hits_in = 0;
                    for (int swing = 0; swing < 14 && !opened; ++swing) {
                        deer->x = w.player.x + 30.0f; deer->y = w.player.y;
                        deer->knock_x = deer->knock_y = 0.0f;
                        w.player.facing = FACE_RIGHT;
                        input.Update(dt); key(SDLK_J, true); w.Update(dt, ctx);
                        input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx);
                        for (int f = 0; f < 26; ++f) {
                            deer->x = w.player.x + 30.0f; deer->y = w.player.y;
                            input.Update(dt); w.Update(dt, ctx);
                        }
                        opened = deer->Bleeding();
                        hits_in = w.player.ChainHits();
                    }
                    Check(opened && hits_in >= Player::BLEED_CHAIN, "Open Wounds: a chain three deep leaves them open, and not before (" +
                          std::to_string(hits_in) + " hits in)");
                }
            }
        }
        {
            World w;
            if (fighter(w, "bronze_sword", SKILL_ATTACK, 70,
                        {"heavy_hand", "bruiser", "ground_slam", "war_cry", "brute_force", "shockwave",
                         "thick_skin", "second_wind", "lunge", "bash", "riposte", "stand_fast"}, nullptr)) {
                carry(w, "bash", 0); carry(w, "shockwave", 1);
                Check(carry(w, "stand_fast", 2), "a third ability is carried on the lock-on button");
                Enemy* ahead = spawn(w, "deer", 60, 0);
                Enemy* far_ahead = spawn(w, "deer", 170, 0);
                Enemy* beside = spawn(w, "deer", 60, 90);
                Enemy* behind = spawn(w, "deer", -60, 0);
                for (Enemy* e : {ahead, far_ahead, beside, behind}) if (e) e->max_hp = e->hp = 5000;
                w.player.facing = FACE_RIGHT;
                ability(w, SDLK_K);
                Check(ahead && far_ahead && ahead->Staggered() && far_ahead->Staggered(),
                      "a shockwave reaches what is straight ahead, near and far, and leaves it reeling");
                Check(beside && behind && !beside->Staggered() && !behind->Staggered(), "and nothing beside or behind");

                // Guard and lock on: the third slot, and the target stays who it was.
                input.Update(dt); key(SDLK_L, true); w.Update(dt, ctx);
                input.Update(dt); key(SDLK_L, false); w.Update(dt, ctx);
                const Enemy* locked = w.targeting.Current();
                ability(w, SDLK_L);
                Check(w.player.StandingFast() && w.player.AbilityCooldown(2) > 30.0f, "guard and lock on: Stand Fast");
                Check(locked && w.targeting.Current() == locked, "and the press was the ability's: the target is who it was");
                w.player.hp = w.player.max_hp;
                w.player.knock_x = w.player.knock_y = 0.0f;
                const int taken = w.HitPlayer(20, CombatProfile{}, w.player.x - 20.0f, w.player.y, 200.0f, 0.0f);
                Check(taken == static_cast<int>(std::lround(20 * Player::STAND_FAST_SHARE)) && w.player.knock_x == 0.0f,
                      "feet set, a blow of twenty is a blow of twelve and moves nobody");
                frames(w, static_cast<int>(Player::STAND_FAST_TIME * 60.0f) + 5);
                w.player.hp = w.player.max_hp;
                Check(!w.player.StandingFast() && w.HitPlayer(20, CombatProfile{}, w.player.x - 20.0f, w.player.y, 200.0f, 0.0f) == 20,
                      "for six seconds");
                if (ahead) {
                    ahead->Taunt(2, 1.0f);
                    Check(ahead->TauntedBy() == 2, "what is called out is after whoever called it");
                    frames(w, 70);
                    Check(ahead->TauntedBy() == -1, "until it wears off");
                }
            }
        }
        // The warden: Take Aim, Weak Point, Rapid Fire, Slippery, Snare.
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 70,
                        {"steady_aim", "eagle_eye", "piercing_shot", "hunters_mark", "long_shot", "take_aim", "weak_point"}, nullptr)) {
                carry(w, "take_aim", 0);
                float plain = 0.0f, aimed = 0.0f;
                bool sure = false;
                Check(loose_one(w, plain, sure, 120) == 1 && !sure, "a plain shot is a plain shot");
                ability(w, SDLK_J);
                Check(w.player.Aiming(), "a breath held");
                Check(loose_one(w, aimed, sure, 120) == 1 && sure && fabsf(aimed - plain * Player::AIM_DAMAGE) < 1e-3f,
                      "goes into the next shot: it will strike critically, and half as hard again");
                Check(!w.player.Aiming(), "and is spent by it");
                Check(loose_one(w, aimed, sure, 10) == 1 && !sure, "the one after is a plain shot again");
                int a = 0, b = 0;
                for (int i = 0; i < 6; ++i) w.player.NoteShotOn(&a);
                Check(w.player.WeakPointStacks() == Player::WEAK_POINT_MAX, "Weak Point: shots in a row on one target count up to four");
                w.player.NoteShotOn(&b);
                Check(w.player.WeakPointStacks() == 0, "and another target starts again");
            }
        }
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 70,
                        {"quick_draw", "fleet_foot", "volley", "tumble", "hit_and_run", "rapid_fire", "slippery", "slippery"}, nullptr)) {
                carry(w, "rapid_fire", 0);
                const float plain = w.player.WeaponSpeed();
                ability(w, SDLK_J);
                Check(w.player.RapidFire() && w.player.WeaponSpeed() < plain * 0.65f, "rapid fire: the bow is two fifths faster");
                frames(w, static_cast<int>(Player::RAPID_TIME * 60.0f) + 5);
                Check(!w.player.RapidFire() && fabsf(w.player.WeaponSpeed() - plain) < 1e-4f, "for five seconds");

                int slipped = 0;
                for (int i = 0; i < 100; ++i) {
                    w.player.hp = w.player.max_hp;
                    if (w.HitPlayer(1, CombatProfile{}, w.player.x + 20.0f, w.player.y) == 0) ++slipped;
                }
                Check(slipped == 0, "Slippery: standing still, nothing misses");
                input.Update(dt); key(SDLK_D, true); w.Update(dt, ctx);
                frames(w, 3);
                for (int i = 0; i < 300; ++i) {
                    w.player.hp = w.player.max_hp;
                    if (w.HitPlayer(1, CombatProfile{}, w.player.x + 20.0f, w.player.y) == 0) ++slipped;
                }
                input.Update(dt); key(SDLK_D, false); w.Update(dt, ctx);
                Check(slipped > 10 && slipped < 90, "on the move, about one blow in eight does (" + std::to_string(slipped) + " of 300)");
            }
        }
        {
            World w;
            if (fighter(w, "oak_shortbow", SKILL_RANGED, 70, {"trail_legs", "broadheads", "arrow_rain", "caltrops", "first_blood", "snare"}, nullptr)) {
                carry(w, "snare", 0);
                ability(w, SDLK_J);
                bool set = false;
                for (const GroundEffect& g : w.ground_effects) set |= g.once && g.stagger > 2.5f && g.max_life > 15.0f;
                Check(set, "a snare lies where it was set, for twenty seconds");
                Enemy* first = spawn(w, "deer", 0, 0);
                Enemy* second = spawn(w, "deer", 4, 0);
                for (Enemy* e : {first, second}) if (e) e->max_hp = e->hp = 5000;
                frames(w, 12);
                const int held = (first && first->Staggered() ? 1 : 0) + (second && second->Staggered() ? 1 : 0);
                bool still = false;
                for (const GroundEffect& g : w.ground_effects) still |= g.once;
                Check(held == 1 && !still, "the first thing to step in it is held, and the snare is sprung (" + std::to_string(held) + " held)");
            }
        }
        // The wayfarer: Overload, Spell Echo, Invoke, Deep Well, Repulse, Resolve.
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 70,
                        {"potency", "focus", "nova", "arcane_pulse", "attunement", "overload"}, nullptr)) {
                carry(w, "overload", 0);
                float plain = 0.0f, loaded = 0.0f;
                bool sure = false;
                w.player.RestoreMana();
                const int full = w.player.Mana();
                Check(loose_one(w, plain, sure, 0) == 1 && w.player.Mana() < full, "a plain cast costs mana");
                frames(w, 120);
                ability(w, SDLK_J);
                Check(w.player.Overloaded(), "overloaded");
                w.player.RestoreMana();
                Check(loose_one(w, loaded, sure, 0) == 1 && w.player.Mana() == full && fabsf(loaded - plain * Player::OVERLOAD_DAMAGE) < 1e-3f,
                      "the next spell costs nothing and hits twice as hard");
                Check(!w.player.Overloaded(), "and that is the one it was for");
                int echoes = 0;
                for (int i = 0; i < 40; ++i) { w.player.RestoreMana(); if (loose_one(w, plain, sure, 40) >= 2) ++echoes; }
                Check(echoes == 0, "with no Spell Echo, a bolt is one bolt");
                w.player.talents.Learn("spell_echo", w.player.skills);
                w.player.talents.Learn("spell_echo", w.player.skills);
                for (int i = 0; i < 70; ++i) { w.player.RestoreMana(); if (loose_one(w, plain, sure, 40) >= 2) ++echoes; }
                Check(echoes >= 2 && echoes < 35, "with it, about one in six is followed by a second (" + std::to_string(echoes) + " of 70)");
            }
        }
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 70, {"flow", "swift_casting", "barrage", "blink", "surge", "invoke"}, nullptr)) {
                carry(w, "invoke", 0);
                const float breath = w.player.Stamina();
                ability(w, SDLK_J);
                Check(!w.player.Invoking() && w.player.AbilityCooldown(0) == 0.0f && w.player.Stamina() >= breath - 0.01f,
                      "with nothing to draw back, Invoke does not happen and costs nothing");
                w.player.SetMana(0);
                ability(w, SDLK_J);
                Check(w.player.Invoking(), "with mana spent, it does");
                frames(w, static_cast<int>(Player::INVOKE_TIME * 60.0f) + 5);
                Check(!w.player.Invoking() && w.player.Mana() >= w.player.MaxMana() / 2 - 1,
                      "and half of all the mana there is comes back over four seconds (" + std::to_string(w.player.Mana()) + " of " +
                      std::to_string(w.player.MaxMana()) + ")");
                const int before = w.player.MaxMana();
                w.player.talents.Learn("deep_well", w.player.skills);
                w.player.talents.Learn("deep_well", w.player.skills);
                w.player.SyncMana();
                Check(w.player.MaxMana() == static_cast<int>(std::lround(before * 1.2f)), "Deep Well: a fifth more mana at two ranks (" +
                      std::to_string(before) + " to " + std::to_string(w.player.MaxMana()) + ")");
            }
        }
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 70, {"ward", "seeker", "meteor", "mana_shield", "siphon", "repulse", "resolve", "resolve"}, nullptr)) {
                carry(w, "repulse", 0);
                Enemy* near_a = spawn(w, "deer", 50, 0);
                Enemy* near_b = spawn(w, "deer", -40, 30);
                Enemy* far_off = spawn(w, "deer", 300, 0);
                for (Enemy* e : {near_a, near_b, far_off}) if (e) e->max_hp = e->hp = 5000;
                const float x0 = near_a ? near_a->x : 0.0f;
                ability(w, SDLK_J);
                Check(near_a && near_b && near_a->Staggered() && near_b->Staggered() && far_off && !far_off->Staggered(),
                      "a repulse leaves everything near reeling, and nothing far");
                frames(w, 20);
                Check(near_a && near_a->x > x0 + 20.0f, "and throws it back (" + std::to_string(near_a ? static_cast<int>(near_a->x - x0) : 0) + " px)");
                w.player.SetMana(0);
                w.player.hp = w.player.max_hp;
                w.HitPlayer(8, CombatProfile{}, w.player.x + 20.0f, w.player.y);
                Check(w.player.Mana() == 6, "Resolve: a blow that draws blood gives back three mana a rank (" + std::to_string(w.player.Mana()) + ")");
            }
        }
        // A blink with nowhere to go spends nothing.
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 60, {"flow", "swift_casting", "barrage", "blink"}, nullptr)) {
                carry(w, "blink", 0);
                // Walled in: find a spot hard against something solid on the right.
                bool found = false;
                for (float step = 0.0f; step < 1200.0f && !found; step += 16.0f) {
                    SDL_FRect probe = w.player.Bounds();
                    probe.x += step;
                    bool open_here = !w.CurrentMap().Blocked(probe);
                    bool all_blocked = open_here;
                    for (float d = 12.0f; d <= Player::BLINK_DISTANCE && all_blocked; d += 8.0f) {
                        SDL_FRect there = probe; there.x += d;
                        all_blocked = w.CurrentMap().Blocked(there);
                    }
                    if (all_blocked) { w.player.x += step; found = true; }
                }
                if (found) {
                    w.player.facing = FACE_RIGHT;
                    const float x0 = w.player.x;
                    const int mana = w.player.Mana();
                    ability(w, SDLK_J);
                    Check(w.player.x == x0 && w.player.Mana() == mana && w.player.AbilityCooldown(0) == 0.0f,
                          "a blink with nowhere to land does not happen, and costs nothing");
                }
            }
        }
        // --- where a blow lands ------------------------------------------------------------------
        // A sector on the ground out from whoever swings, against where the other
        // stands; and what is drawn is that sector. See StrikeArc.
        {
            const AttackProfile& light = ProfileFor(AttackType::Light, 0);
            const AttackProfile& cleave = ProfileForCombo(ComboMove::Cleave);
            const StrikeArc east = ArcFor(0.0f, 0.0f, FACE_RIGHT, light);
            Check(ArcHits(east, light.reach + 8.0f, 0.0f, 12.0f) && !ArcHits(east, light.reach + 14.0f, 0.0f, 12.0f),
                  "a swing reaches as far as its reach and half the width of what it meets, and no further");
            Check(!ArcHits(east, 0.0f, 34.0f, 12.0f) && !ArcHits(east, -30.0f, 0.0f, 12.0f),
                  "a light swing does not reach what stands beside the character, or behind");
            const StrikeArc north = ArcFor(0.0f, 0.0f, FACE_UP, light), south = ArcFor(0.0f, 0.0f, FACE_DOWN, light);
            bool even = true;
            for (float d = 20.0f; d < 60.0f; d += 2.0f)
                even &= ArcHits(north, 0.0f, -d, 12.0f) == ArcHits(south, 0.0f, d, 12.0f) &&
                        ArcHits(north, 0.0f, -d, 12.0f) == ArcHits(east, d, 0.0f, 12.0f);
            Check(even, "and reaches as far up the screen as down it, and as far as across: it hung from the sprite's box, and did not");
            const StrikeArc wide = ArcFor(0.0f, 0.0f, FACE_RIGHT, cleave);
            Check(ArcHits(wide, 0.0f, 40.0f, 12.0f) && ArcHits(wide, 0.0f, -40.0f, 12.0f) && !ArcHits(wide, -40.0f, 0.0f, 12.0f),
                  "the Cleave goes from shoulder to shoulder, and not behind");
            Check(fabsf(light.HalfAngle(light.reach) - atanf(light.width * 0.5f / light.reach)) < 1e-5f &&
                  fabsf(cleave.HalfAngle(cleave.reach) - 95.0f * 3.14159265f / 180.0f) < 1e-4f,
                  "the arc drawn and the arc struck ask the same question of the same swing");
            StrikeArc round = east;
            round.all_round = true;
            Check(ArcHits(round, -30.0f, 0.0f, 12.0f), "a full turn reaches behind");

            // A monster's swing reaches the range it swings from. It reached
            // thirty-two pixels whatever that was: a wyvern never landed a bite.
            bool lands = true;
            string short_one;
            for (const char* id : {"boar", "wolf", "bear", "hound", "ice_troll", "wyvern", "wyvern_matriarch", "demon",
                                   "pit_lord", "frost_dragon", "ankou", "dire_bear", "greatwolf", "lizardman_chief"}) {
                const EnemyDef* stats = enemy_db.Get(id);
                if (!stats) { lands = false; short_one = id; continue; }
                EnemySpawnDef at;
                at.type = id; at.level = 1; at.x = 0; at.y = 0;
                Enemy e;
                e.Init(stats, at, ctx);
                e.facing = FACE_RIGHT;
                const SDL_FPoint from = e.GroundCentre();
                // Someone stood still at the edge of its range, where it began the swing.
                if (!ArcHits(e.SwingArc(), from.x + stats->attack_range, from.y, 13.0f)) { lands = false; short_one = id; }
            }
            Check(lands, "every monster's swing lands on whoever stands still at the range it swung from" +
                  (short_one.empty() ? string() : " (" + short_one + ")"));
        }
        // On the ground, a burst is a circle against where things stand -- not a
        // square against the box a sprite fills.
        {
            World w;
            if (fighter(w, "novice_staff", SKILL_MAGIC, 30, {"potency"}, nullptr)) {
                const float r = 58.0f;
                Enemy* inside = spawn(w, "deer", 300, 0);
                Enemy* corner = spawn(w, "deer", 300, 0);
                Enemy* below = spawn(w, "deer", 300, 0);
                if (inside && corner && below) {
                    const float gx = w.player.x + 300.0f, gy = w.player.y;
                    inside->x = gx + r * 0.8f;  inside->y = gy;
                    corner->x = gx + r * 0.92f; corner->y = gy + r * 0.92f;      // in the old square, outside the circle
                    below->x = gx;              below->y = gy + r + 40.0f;       // its sprite reaches up into the old square
                    for (Enemy* e : {inside, corner, below}) e->max_hp = e->hp = 5000;
                    GroundEffect g;
                    g.x = gx; g.y = gy; g.radius = r;
                    g.life = g.max_life = 0.35f;
                    g.burst = true;
                    g.owner = w.player.Profile();
                    g.style = AttackStyle::Magic;
                    g.hit_mult = 1.0f;
                    w.AddGroundEffect(g);
                    frames(w, 3);
                    Check(inside->HealthBarVisible() && !corner->HealthBarVisible() && !below->HealthBarVisible(),
                          "a burst on the ground strikes what stands inside its circle, and not the corners of a square round it");
                }
            }
        }

        // --- the ground lifts everything on it -----------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx) && w.map.HasElevation()) {
                w.enemies.clear();
                // Somewhere raised, clear to stand on, with level ground beside it.
                float hx = -1.0f, hy = -1.0f;
                const float cell = w.map.ElevationCell();
                for (float y = cell * 2.5f; y < w.map.Height() - cell * 2 && hx < 0.0f; y += cell)
                    for (float x = cell * 2.5f; x < w.map.Width() - cell * 2; x += cell)
                        if (w.map.LevelAt(x, y) >= 2 && !w.map.Blocked({x - 8.0f, y - 10.0f, 16.0f, 10.0f})) { hx = x; hy = y; break; }
                Check(hx > 0.0f, "the Hollowmarch has high ground to stand on");
                if (hx > 0.0f) {
                    const float lift = w.map.HeightAt(hx, hy);
                    w.player.x = hx; w.player.y = hy;
                    w.texts.clear();
                    w.AddText("12", hx, hy - 46.0f, {255, 255, 255, 255});
                    Check(!w.texts.empty() && fabsf(w.texts.back().y - (hy - 46.0f - lift)) < 0.01f,
                          "a number over someone's head is lifted with the ground they stand on (" +
                          std::to_string(static_cast<int>(lift)) + " px)");
                    Check(fabsf(w.LiftAt(hx, hy) - lift) < 0.01f && lift >= ELEVATION_RISE * 2.0f,
                          "and so is what is drawn there: a shot, a drop, burning ground");
                    // A walker's lift closes on the ground's rather than snapping to it.
                    w.player.ground_lift = lift - ELEVATION_RISE;
                    w.player.draw_lift = w.player.ground_lift;
                    input.Update(dt); w.Update(dt, ctx);
                    const float after_one = w.player.draw_lift;
                    Check(after_one > lift - ELEVATION_RISE + 0.5f && after_one < lift - 0.5f,
                          "stepping up a level, the lift closes on the ground's rather than jumping to it");
                    frames(w, 20);
                    Check(fabsf(w.player.draw_lift - lift) < 0.01f, "and is there within a third of a second");
                    w.visiting = true;
                    w.AddText("12", hx, hy - 46.0f, {255, 255, 255, 255});
                    Check(fabsf(w.texts.back().y - (hy - 46.0f)) < 0.01f, "a friend's window is told where the host already put it");
                    w.visiting = false;
                }
                // Not up or down a cliff: two levels apart, a blade does not reach.
                float cx0 = -1.0f, cy0 = -1.0f;
                for (float y = cell * 2.5f; y < w.map.Height() - cell * 2 && cx0 < 0.0f; y += cell)
                    for (float x = cell * 1.0f; x < w.map.Width() - cell * 3; x += cell) {
                        const float edge = (floorf(x / cell) + 1.0f) * cell;
                        if (abs(w.map.LevelAt(edge + 10.0f, y) - w.map.LevelAt(edge - 10.0f, y)) >= 2) { cx0 = edge; cy0 = y; break; }
                    }
                if (cx0 > 0.0f) {
                    w.player.equipment.Equip(SLOT_WEAPON, "bronze_sword");
                    w.player.x = cx0 - 10.0f; w.player.y = cy0;
                    w.player.facing = FACE_RIGHT;
                    Enemy* up = spawn(w, "deer", 20.0f, 0.0f);
                    if (up) {
                        up->max_hp = up->hp = 5000;
                        for (int k = 0; k < 3; ++k) {
                            up->x = cx0 + 10.0f; up->y = cy0;
                            input.Update(dt); key(SDLK_J, true); w.Update(dt, ctx);
                            input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx);
                            for (int f = 0; f < 24; ++f) { up->x = cx0 + 10.0f; up->y = cy0; input.Update(dt); w.Update(dt, ctx); }
                        }
                        Check(!up->HealthBarVisible(), "a blade does not reach up or down a cliff two levels high");
                    }
                }
            }
        }

        // --- people with somewhere to be -------------------------------------------------------------
        {
            const auto find = [](World& w, const string& id) -> Npc* {
                for (auto& n : w.npcs) if (n->Id() == id) return n.get();
                return nullptr;
            };
            World a, b;
            a.player.Init(ctx, "player_hero");
            b.player.Init(ctx, "player_hero");
            if (a.LoadMap("town_havenbrook", "default", ctx) && b.LoadMap("town_havenbrook", "default", ctx)) {
                int walkers = 0;
                bool clear = true;
                string blocked_at;
                for (auto& n : a.npcs) {
                    if (!n->Walks()) continue;
                    ++walkers;
                    // Every step of every round is somewhere that can be stood on.
                    for (float t = 0.0f; t < n->RoundTime(); t += 0.25f) {
                        const SDL_FPoint at = n->PlaceAt(t);
                        if (a.map.Blocked({at.x - 6.0f, at.y - 8.0f, 12.0f, 8.0f})) {
                            clear = false;
                            blocked_at = n->Id() + " at " + std::to_string(static_cast<int>(at.x)) + "," + std::to_string(static_cast<int>(at.y));
                            break;
                        }
                    }
                    const SDL_FPoint first = n->PlaceAt(0.0f), last = n->PlaceAt(n->RoundTime() - 0.01f);
                    Check(Length(first.x - last.x, first.y - last.y) < 2.0f, n->Id() + "'s round ends where it began");
                }
                Check(walkers >= 8, "Havenbrook has people walking its streets (" + std::to_string(walkers) + ")");
                Check(clear, "and nobody's round takes them through a wall" + (blocked_at.empty() ? string() : ": " + blocked_at));

                a.clock.Set(1, 10.5f);
                b.clock.Set(1, 10.5f);
                frames(a, 1);
                input.Update(dt); b.Update(dt, ctx);
                Npc* brask = find(a, "npc_brask");
                Npc* brask_b = find(b, "npc_brask");
                Check(brask && brask_b && !brask->Away() && Length(brask->x - brask_b->x, brask->y - brask_b->y) < 1.5f,
                      "where someone is on their round is the clock's to say: two machines put the watchman in the same place");
                if (brask) {
                    const float x0 = brask->x, y0 = brask->y;
                    frames(a, 120);
                    Check(Length(brask->x - x0, brask->y - y0) > 20.0f, "and he walks");
                    brask->talking = true;
                    const float tx = brask->x, ty = brask->y;
                    frames(a, 180);
                    Check(Length(brask->x - tx, brask->y - ty) < 0.01f, "spoken to, he stands still");
                    brask->talking = false;
                    frames(a, 1);
                    Check(Length(brask->x - tx, brask->y - ty) < 6.0f, "let go, he is not flung to where the clock has him");
                    frames(a, 60 * 12);
                    // The other machine, where nobody spoke to him, for as long.
                    input.Update(dt);
                    for (int f = 0; f < 120 + 180 + 1 + 60 * 12; ++f) b.Update(dt, ctx);
                    Check(brask_b && Length(brask->x - brask_b->x, brask->y - brask_b->y) < 3.0f,
                          "but hurries along his round until he is back on it");
                }
                a.clock.Set(1, 23.5f);
                frames(a, 2);
                Npc* wenna = find(a, "npc_wenna");
                Check(wenna && wenna->Away() && brask && !brask->Away(), "at night the streets are the watch's: everyone else has gone in");
                if (wenna) {
                    a.player.x = wenna->x; a.player.y = wenna->y + 20.0f;
                    frames(a, 2);
                    Check(a.player.interact.kind != InteractTarget::Npc, "and nobody who has gone in can be spoken to");
                }
            }
        }

        // --- three kinds of armour, one for each way of fighting -----------------------------------------
        {
            int pieces = 0;
            bool only_its_own = true, ordered = true, made = true, drawn = true, got = true;
            string wrong;
            json loot_tables;
            { std::ifstream f("data/loot_tables.json"); f >> loot_tables; }
            const string loot_text = loot_tables.dump();
            for (const TierDef& t : items.Tiers()) {
                const ItemDef* plate[3] = {items.Get(items.TierPiece(t.id, "helm")), items.Get(items.TierPiece(t.id, "body")),
                                           items.Get(items.TierPiece(t.id, "legs"))};
                const char* slots[3] = {"head", "body", "legs"};
                for (int i = 0; i < 3; ++i) {
                    const ItemDef* hide = items.Get(items.TierPiece(t.id, string("hide_") + slots[i]));
                    const ItemDef* robe = items.Get(items.TierPiece(t.id, string("robe_") + slots[i]));
                    if (!hide || !robe || !plate[i]) { only_its_own = false; wrong = t.id + " is missing a piece"; continue; }
                    pieces += 2;
                    // Each helps only its own style.
                    if (!(hide->ranged_bonus > 0 && hide->magic_bonus == 0 && hide->attack_bonus == 0 && hide->strength_bonus == 0) ||
                        !(robe->magic_bonus > 0 && robe->ranged_bonus == 0 && robe->attack_bonus == 0 && robe->strength_bonus == 0) ||
                        !(plate[i]->attack_bonus > 0 && plate[i]->strength_bonus > 0 && plate[i]->ranged_bonus == 0 && plate[i]->magic_bonus == 0)) {
                        only_its_own = false; wrong = t.id + " " + slots[i];
                    }
                    // Plate keeps out the most, robes the least -- and carry the most.
                    if (!(plate[i]->defence_bonus > hide->defence_bonus && hide->defence_bonus > robe->defence_bonus &&
                          robe->magic_bonus >= hide->ranged_bonus)) { ordered = false; wrong = t.id + " " + slots[i]; }
                    // Worn at the tier's level in the set's own skill, and drawn in its own cut.
                    if (t.level > 1 && (hide->requirements.count(SKILL_RANGED) == 0 || hide->requirements.at(SKILL_RANGED) != t.level ||
                                        robe->requirements.count(SKILL_MAGIC) == 0 || robe->requirements.at(SKILL_MAGIC) != t.level)) {
                        only_its_own = false; wrong = t.id + " requirement";
                    }
                    if (hide->armour_cut != "hide" || robe->armour_cut != "robe" || hide->armour_layer != slots[i] ||
                        !fs::exists(hide->icon) || !fs::exists(robe->icon)) { drawn = false; wrong = hide->id; }
                }
            }
            // How they are made: a hide piece from its tier's hide on a tanner's
            // rack, a robe from cloth and its tier's dye at a loom, and the dye in
            // a pot.
            std::set<string> hides;
            for (const ItemDef* r : items.Recipes()) {
                const ItemDef* out = items.Get(r->craft_result);
                if (!out || out->armour_cut.empty() || (out->armour_cut != "hide" && out->armour_cut != "robe")) continue;
                // The ranger's are cut and sewn on a tanner's frame and the mage's
                // are woven, which is the whole difference between the two trades.
                const CraftStation where = out->armour_cut == "robe" ? CraftStation::Loom : CraftStation::Rack;
                if (items.StationFor(*r) != where || !r->craft_inputs.count("thread")) { made = false; wrong = out->id; }
                if (out->armour_cut == "robe") {
                    bool dyed = false;
                    for (const auto& in : r->craft_inputs) if (const ItemDef* m = items.Get(in.first)) dyed |= m->untaught && m->tier == out->tier;
                    if (!dyed || !r->craft_inputs.count("bolt_cloth")) { made = false; wrong = out->id; }
                } else {
                    for (const auto& in : r->craft_inputs) if (in.first != "thread" && in.first != "dream_shard") hides.insert(in.first);
                }
                if (out->tier_index >= 2 && r->craft_level != items.Tiers()[out->tier_index].level) { made = false; wrong = out->id + " level"; }
            }
            for (const string& h : hides)
                if (loot_text.find("\"" + h + "\"") == string::npos) { got = false; wrong = h; }
            Check(pieces == 72, "every tier has a ranger's hides and a mage's robes: head, body and legs (" + std::to_string(pieces) + ")");
            Check(only_its_own, "plate adds to a blade, hides to a bow and robes to a staff, and none of them to anything else" +
                  (only_its_own ? string() : ": " + wrong));
            Check(ordered, "plate keeps out the most and robes the least, and robes carry a spell furthest" + (ordered ? string() : ": " + wrong));
            Check(drawn, "each piece has its icon and is drawn in its own cut" + (drawn ? string() : ": " + wrong));
            Check(made, "hides are cut on a tanning rack from the tier's hide, robes woven at a loom from cloth and the tier's dye, "
                  "each at the tier's level" + (made ? string() : ": " + wrong));
            Check(hides.size() == 11 && got, "eleven hides, one a tier and the last two tiers sharing the dragon's, and something drops every one (" +
                  std::to_string(hides.size()) + ")" + (got ? string() : ": nothing drops " + wrong));
            const ItemDef* bolt = items.Get("bolt_cloth");
            int weaves = 0;
            for (const ItemDef* r : items.Recipes())
                if (r->craft_result == "bolt_cloth" && items.StationFor(*r) == CraftStation::Loom) ++weaves;
            Check(bolt && weaves == 3, "a bolt of cloth is woven at a loom, from flax, spider silk or a fleece");
            // Every last robe, hat and skirt went with it, and nothing else did.
            int woven_pieces = 0;
            for (const ItemDef* r : items.Recipes(CraftStation::Loom)) {
                const ItemDef* out = items.Get(r->craft_result);
                if (out && out->armour_cut == "robe") ++woven_pieces;
                else if (!out || r->craft_result != "bolt_cloth") woven_pieces = -999;
            }
            Check(woven_pieces == 36, "and all thirty-six pieces of the mage's sets, and nothing that is not cloth (" +
                  std::to_string(woven_pieces) + ")");
            // The cuts are rendered for all three characters.
            bool sheets = true;
            for (const char* look : {"player_hero", "player_warden", "player_wayfarer"})
                for (const char* cut : {"hide", "robe"})
                    for (const char* clip : {"idle", "walk", "attack", "block", "death"})
                        for (const char* layer : {"6_armour_legs", "7_armour_body", "9_armour_head"}) {
                            const string file = string("assets/characters/") + look + "/layers/" + clip + "_" + layer + "_" + cut + ".png";
                            if (!fs::exists(file)) { sheets = false; wrong = file; }
                        }
            Check(sheets, "hides and robes are drawn on all three characters" + (sheets ? string() : ": " + wrong));
        }

        // --- the Westwold and the Brackenwood ---------------------------------------------------------------
        {
            std::map<string, int> wold, bracken;
            Map west, wood, town;
            Check(west.Load("maps/westwold.mx") && wood.Load("maps/brackenwood.mx") && town.Load("maps/town_havenbrook.mx"),
                  "the Westwold and the Brackenwood load");
            for (const auto& e : west.Enemies()) ++wold[e.type];
            for (const auto& e : wood.Enemies()) ++bracken[e.type];
            Check(wold["wolf"] >= 12 && wold["greatwolf"] >= 6, "wolves run on the Westwold, and greatwolves in its Fells (" +
                  std::to_string(wold["wolf"]) + ", " + std::to_string(wold["greatwolf"]) + ")");
            Check(bracken["bear"] >= 12 && bracken["den_mother"] == 1 && bracken["dire_bear"] >= 4,
                  "bears in the Brackenwood, one Den Mother, and dire bears in the Old Growth (" +
                  std::to_string(bracken["bear"]) + ", " + std::to_string(bracken["dire_bear"]) + ")");
            bool gate = false, back = false;
            for (const auto& p : town.Portals()) gate |= p.target_map == "westwold" && p.rect.x < 64.0f;
            for (const auto& p : west.Portals()) back |= p.target_map == "town_havenbrook";
            Check(gate && back, "Havenbrook has a west gate, and the road comes back to it");
            Check(west.Width() >= 4000.0f && wood.Width() >= 4000.0f, "and neither of them is small");
        }
        // --- what starts a fight, and what ends one ---------------------------------------------------
        {
            const EnemyDef* boar_stats = enemy_db.Get("boar");
            // A boar with a leash of its own choosing, some way north of the player.
            const auto boar_at = [&](World& w, float dy, float leash) -> Enemy* {
                if (!boar_stats) return nullptr;
                EnemySpawnDef def;
                def.type = "boar"; def.level = 1; def.leash = leash; def.respawn = 0.0f;
                def.x = w.player.x; def.y = w.player.y + dy;
                auto e = std::make_unique<Enemy>();
                e->Init(boar_stats, def, ctx);
                Enemy* raw = e.get();
                raw->max_hp = raw->hp = 900;
                w.enemies.push_back(std::move(e));
                return raw;
            };
            {
                World w;
                if (fighter(w, "bronze_sword", SKILL_ATTACK, 30, {"keen_edge"}, nullptr)) {
                    Enemy* boar = boar_at(w, -320.0f, 400.0f);
                    frames(w, 40);
                    Check(boar && !boar->Engaged() && boar_stats->aggro_range < 200.0f,
                          "a boar does not notice someone three hundred pixels off");
                    if (boar) {
                        const float y0 = boar->y;
                        boar->Damage(3);
                        frames(w, 60);
                        Check(boar->Engaged() && boar->Provoked() && boar->y > y0 + 12.0f,
                              "hurt from out of its sight, it comes for whoever did it (it used to stand and be shot)");
                    }
                    Enemy* near = boar_at(w, -60.0f, 400.0f);
                    frames(w, 10);
                    Check(near && near->Engaged() && !near->Provoked(), "and someone inside its range is a fight without a blow struck");
                }
            }
            {
                // Run from, with nothing happening: it gives up after the ground
                // its leash allows, and not for having left its post.
                World w;
                if (fighter(w, "bronze_sword", SKILL_ATTACK, 30, {"keen_edge"}, nullptr)) {
                    Enemy* boar = boar_at(w, 120.0f, 200.0f);
                    if (boar) {
                        const float budget = boar->ChaseBudget();
                        frames(w, 2);                 // so the blow is a fall from what it had
                        boar->Damage(1);
                        float most = 0.0f;
                        int gave_up = -1;
                        bool past_the_ring = false;
                        for (int f = 0; f < 1200 && gave_up < 0; ++f) {
                            // Always a hundred and ten pixels ahead of it, going north.
                            w.player.x = boar->x; w.player.y = boar->y - 110.0f;
                            input.Update(dt); w.Update(dt, ctx);
                            most = std::max(most, boar->ChaseRun());
                            if (boar->Engaged() && boar->ChaseRun() > 200.0f + 10.0f) past_the_ring = true;
                            if (f > 5 && !boar->Engaged()) gave_up = f;
                        }
                        Check(past_the_ring, "a chase does not end for being further from its post than its leash is long");
                        Check(gave_up > 0 && most >= budget - 4.0f && most <= budget + 4.0f,
                              "it ends when it has covered its budget with nothing happening (" +
                              std::to_string(static_cast<int>(most)) + " of " + std::to_string(static_cast<int>(budget)) + " px)");
                        // On the way home, someone stepping close is a fight again, wherever that is.
                        w.player.x = boar->x + 18.0f; w.player.y = boar->y;
                        frames(w, 3);
                        Check(boar->Engaged(), "and walking home it turns on anyone who steps close, however far from home that is");
                    }
                }
            }
            {
                // A fight in it, and the count starts again: a blow taken, or a swing begun.
                World w;
                if (fighter(w, "bronze_sword", SKILL_ATTACK, 30, {"keen_edge"}, nullptr)) {
                    Enemy* boar = boar_at(w, 120.0f, 200.0f);
                    if (boar) {
                        frames(w, 2);
                        boar->Damage(1);
                        const int give_up_frames = static_cast<int>(boar->ChaseBudget() / boar_stats->speed * 60.0f);
                        bool kept_on = true;
                        for (int f = 0; f < give_up_frames * 2; ++f) {
                            w.player.x = boar->x; w.player.y = boar->y - 110.0f;
                            if (f % 120 == 119) boar->Damage(1);
                            input.Update(dt); w.Update(dt, ctx);
                            if (f > 5) kept_on &= boar->Engaged() || boar->Staggered();
                        }
                        Check(kept_on, "hurt along the way, it keeps coming for twice as long as it would have");
                        // Let it catch up: the swing it starts is a fight too.
                        w.player.hp = w.player.max_hp;
                        w.player.x = boar->x; w.player.y = boar->y - 110.0f;
                        frames(w, 8);
                        const bool counting = boar->ChaseRun() > 0.0f;
                        for (int f = 0; f < 600 && boar->ChaseRun() > 0.0f; ++f) {
                            w.player.hp = w.player.max_hp;
                            input.Update(dt); w.Update(dt, ctx);
                        }
                        Check(counting && boar->ChaseRun() == 0.0f && boar->Engaged(), "and a swing begun starts the count again");
                    }
                }
            }
        }
        input.Update(dt);
    }

    // --- gathering tools and fishing ---------------------------------------------------------
    Section("gathering tools and fishing");
    {
        const auto& tiers = items.Tiers();
        bool speeds_rise = true, tools_ok = true, reqs_ok = true, models_ok = true, stations_ok = true;
        float last_axe = 0.0f, last_pick = 0.0f;
        const auto recipe_for = [&](const string& id) -> const ItemDef* {
            for (const ItemDef* r : items.Recipes()) if (r->craft_result == id) return r;
            return nullptr;
        };
        for (const TierDef& t : tiers) {
            const ItemDef* axe = items.Get(items.TierPiece(t.id, "axe"));
            const ItemDef* pick = items.Get(items.TierPiece(t.id, "pickaxe"));
            if (!axe || !pick || axe->tool != "axe" || pick->tool != "pickaxe" || axe->slot != SLOT_NONE) { tools_ok = false; continue; }
            if (axe->tool_speed <= last_axe || pick->tool_speed <= last_pick) speeds_rise = false;
            last_axe = axe->tool_speed;
            last_pick = pick->tool_speed;
            if (t.level > 1 && (axe->requirements.count(SKILL_WOODCUTTING) == 0 || axe->requirements.at(SKILL_WOODCUTTING) != t.level ||
                                pick->requirements.count(SKILL_MINING) == 0 || pick->requirements.at(SKILL_MINING) != t.level))
                reqs_ok = false;
            if (axe->model != "axe_" + t.id || pick->model != "pickaxe_" + t.id) models_ok = false;
            for (const ItemDef* tool : {axe, pick}) {
                const ItemDef* r = recipe_for(tool->id);
                if (!r || items.StationFor(*r) != (t.wood ? CraftStation::Workbench : CraftStation::Anvil)) stations_ok = false;
            }
        }
        Check(tools_ok, "every tier makes an axe and a pickaxe, carried rather than worn");
        Check(speeds_rise, "each tier's axe and pickaxe work faster than the tier below");
        Check(reqs_ok, "and need the tier's level in Woodcutting or Mining");
        Check(models_ok, "each tool names its own model");
        Check(stations_ok, "wooden tools are made at a workbench and metal ones at an anvil");
        const ItemDef* rod = items.Get("fishing_rod");
        const ItemDef* rod_recipe = recipe_for("fishing_rod");
        Check(rod && rod->tool == "rod" && rod_recipe && items.StationFor(*rod_recipe) == CraftStation::Workbench,
              "a fishing rod is a tool made at a workbench");

        // The hero has the work animations, and holds each tool through them.
        {
            const SpriteDef* hero = sprites.Get("player_hero");
            Check(hero && hero->Find("chop") && hero->Find("mine") && hero->Find("fish"),
                  "the hero has chop, mine and fish animations");
            int missing = 0;
            for (const TierDef& t : tiers) {
                if (!fs::exists("assets/characters/player_hero/layers/chop_4_weapon_axe_" + t.id + ".png")) ++missing;
                if (!fs::exists("assets/characters/player_hero/layers/mine_4_weapon_pickaxe_" + t.id + ".png")) ++missing;
            }
            if (!fs::exists("assets/characters/player_hero/layers/fish_4_weapon_rod.png")) ++missing;
            Check(missing == 0, "every axe, pickaxe and the rod are drawn in the hero's hands at work");
        }

        // The arithmetic.
        Check(Gathering::Speed(50, 1.0f) > Gathering::Speed(1, 1.0f) &&
              Gathering::Speed(1, 2.25f) > Gathering::Speed(1, 1.0f), "a higher level and a better tool are both faster");
        Check(Gathering::WorkTime(3.0f, 99, 10.0f) >= 0.42f, "however good, work takes a moment");
        // The floor used to be 0.6, which a platinum axe reached the day it
        // could be held: the three tiers above it cut no faster. Each does now.
        {
            float last = 99.0f;
            bool each_faster = true;
            for (const char* tier : {"platinum", "demonite", "dracon", "enchanted"}) {
                const ItemDef* axe = items.Get(items.TierPiece(tier, "axe"));
                const int need = axe && axe->requirements.count(SKILL_WOODCUTTING) ? axe->requirements.at(SKILL_WOODCUTTING) : 1;
                const float t = axe ? Gathering::WorkTime(3.8f, need, axe->tool_speed) : 99.0f;
                if (!(t < last)) each_faster = false;
                last = t;
            }
            Check(each_faster, "every axe past platinum cuts faster than the one before it, on the day it can be held");
        }
        Check(string(SkillName(SKILL_FISHING)) == "Fishing" && SkillFromName("Fishing") == SKILL_FISHING, "Fishing is a skill");
        {
            bool rising = true;
            float last = -1.0f;
            for (const auto& m : Gathering::FishingMilestones()) {
                const float total = m.two + m.three;
                if (total < last) rising = false;
                last = total;
            }
            Check(rising && Gathering::FishingMilestones().front().level == 20, "fishing milestones start at 20 and only improve");
            Check(Gathering::CatchCount(1, 0.0f) == 1 && Gathering::CatchCount(19, 0.01f) == 1,
                  "below level 20 a catch is always one fish");
            Check(Gathering::CatchCount(99, 0.05f) == 3 && Gathering::CatchCount(99, 0.3f) == 2 &&
                  Gathering::CatchCount(99, 0.9f) == 1, "at 99 a catch can be three, two or one");
            std::mt19937 roll_rng(5);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            int doubles = 0;
            for (int i = 0; i < 10000; ++i) doubles += Gathering::CatchCount(40, unit(roll_rng)) == 2;
            Check(doubles > 1800 && doubles < 2200, "at Fishing 40 about one catch in five is two fish (" +
                  std::to_string(doubles / 100) + "%)");
        }
        {
            std::mt19937 fish_rng(9);
            const vector<string> pond = {"raw_minnow", "raw_trout", "raw_pike"};
            bool only_minnow = true, above = false;
            std::map<string, int> seen;
            for (int i = 0; i < 400; ++i) {
                if (Gathering::PickFish(pond, 1, items, fish_rng) != "raw_minnow") only_minnow = false;
                const string f = Gathering::PickFish(pond, 30, items, fish_rng);
                ++seen[f];
                if (items.Get(f) && items.Get(f)->fish_level > 30) above = true;
            }
            Check(only_minnow, "a level 1 fisher only ever lands minnows at the pond");
            Check(seen["raw_pike"] > 0 && seen["raw_trout"] > 0 && seen["raw_minnow"] > 0 && !above,
                  "at level 30 the pond gives pike, trout and minnows, and nothing above the level");
        }
        for (const char* fish : {"raw_minnow", "raw_trout", "raw_pike", "raw_salmon", "raw_eel"}) {
            const ItemDef* raw = items.Get(fish);
            const ItemDef* cooked = raw ? items.Get(raw->cook_result) : nullptr;
            Check(raw && raw->fish_level >= 1 && cooked && cooked->heal > 0, string(fish) + " can be caught and cooked into food");
        }

        // Fishing spots: in the pond, the stream and the lake, each reachable from a bank.
        int spots = 0;
        std::set<string> where;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "fishing_spot") continue;
                ++spots;
                where.insert(id);
                bool fish_ok = !o.fish.empty();
                for (const string& f : o.fish) if (!items.Get(f) || items.Get(f)->fish_level <= 0) fish_ok = false;
                Check(fish_ok && o.skill == "Fishing", o.id + " has real fish in it");
                bool bank = false;
                for (float a = 0.0f; a < 6.28f && !bank; a += 0.3f)
                    for (float d = 16.0f; d <= 50.0f && !bank; d += 6.0f) {
                        const float px = o.x + cosf(a) * d, py = o.y + sinf(a) * d;
                        if (!m.Blocked({px - 8.0f, py - 10.0f, 16.0f, 10.0f})) bank = true;
                    }
                Check(bank, o.id + " can be reached from dry land");
            }
        }
        Check(spots >= 8 && where.count("fernhollow") && where.count("whisperwood_trail") && where.count("overworld"),
              "there is fishing at Fernhollow's pond, the Whisperwood stream and the Hollowmarch lake (" +
              std::to_string(spots) + " spots)");

        // --- in the world -------------------------------------------------------------------
        Input input;
        std::mt19937 rng(44);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        // Stands the player beside the first object of a kind on a map and
        // presses Interact on it; returns the object, or null.
        const auto work_at = [&](World& w, const string& map_id, const std::function<bool(const MapObject&)>& pick,
                                 int skill, int level) -> const MapObject* {
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap(map_id, "", ctx)) return nullptr;
            w.enemies.clear();
            LevelUp lu;
            if (level > 1) w.player.skills.AddXp(skill, XpForLevel(level), lu);
            for (const MapObject& o : w.CurrentMap().Objects()) {
                if (!pick(o)) continue;
                // A spot on dry ground within reach of it.
                for (float a = 1.57f; a < 1.57f + 6.28f; a += 0.3f)
                    for (float d = 14.0f; d <= 44.0f; d += 6.0f) {
                        const float px = o.x + cosf(a) * d, py = o.y + sinf(a) * d;
                        if (w.CurrentMap().Blocked({px - 8.0f, py - 10.0f, 16.0f, 10.0f})) continue;
                        w.player.x = px;
                        w.player.y = py;
                        frames(w, 2);
                        if (w.player.interact.kind == InteractTarget::Object) return &o;
                    }
            }
            return nullptr;
        };
        const auto is_tree = [](const MapObject& o) { return o.skill == "Woodcutting" && o.skill_level <= 1; };
        const auto is_copper = [](const MapObject& o) { return o.yield == "copper_ore" && o.skill_level <= 1; };
        const auto is_pond = [](const MapObject& o) { return o.type == "fishing_spot"; };

        // Seconds from pressing Interact until the first thing lands in the bag.
        const auto time_to_first = [&](World& w, const string& id, float limit) {
            const int before = w.player.inventory.Count(id);
            w.TryInteract(ctx);
            for (int f = 0; f < static_cast<int>(limit * 60.0f); ++f) {
                frames(w, 1);
                if (w.player.inventory.Count(id) > before) return f / 60.0f;
            }
            return -1.0f;
        };

        {
            World w;
            const MapObject* tree = work_at(w, "overworld", is_tree, SKILL_WOODCUTTING, 1);
            Check(tree != nullptr, "there is a tree to test on");
            if (tree) {
                Check(w.player.interact.label.find("needs an axe") != string::npos,
                      "without an axe the tree says so before the button is pressed");
                w.TryInteract(ctx);
                frames(w, 2);
                Check(!w.Gathering() && w.player.GatherClip().empty(), "and pressing it chops nothing");
                w.player.inventory.Add("bronze_axe", 1);
                frames(w, 1);
                const float bronze = time_to_first(w, "logs", 12.0f);
                Check(bronze > 0.0f, "with a bronze axe the tree gives logs (" + std::to_string(bronze).substr(0, 4) + "s)");
                w.TryInteract(ctx);
                frames(w, 1);
                w.TryInteract(ctx);
                frames(w, 1);
                Check(w.Gathering() && w.player.GatherClip() == "chop", "the hero chops while it works");
                Check(w.player.BuildLayerStyle(&items).weapon_model == "axe_bronze", "holding the axe, not a weapon");
                w.TryInteract(ctx);   // stop
                frames(w, 2);
                Check(!w.Gathering() && w.player.GatherClip().empty(), "and stops when told to");
            }
            World w2;
            if (work_at(w2, "overworld", is_tree, SKILL_WOODCUTTING, 70)) {
                w2.player.inventory.Add("bronze_axe", 1);
                frames(w2, 1);
                const float slow = time_to_first(w2, "logs", 12.0f);
                w2.player.inventory.Add("platinum_axe", 1);
                w2.TryInteract(ctx);
                frames(w2, 1);
                const float fast = time_to_first(w2, "logs", 12.0f);
                Check(slow > 0.0f && fast > 0.0f && fast < slow * 0.7f,
                      "a platinum axe fells the same tree much faster than bronze (" +
                      std::to_string(fast).substr(0, 4) + "s against " + std::to_string(slow).substr(0, 4) + "s)");
            }
        }
        {
            World w;
            if (work_at(w, "overworld", is_copper, SKILL_MINING, 1)) {
                w.player.inventory.Add("iron_pickaxe", 1);
                frames(w, 1);
                w.TryInteract(ctx);
                frames(w, 2);
                Check(!w.Gathering(), "an iron pickaxe is no use at Mining 1");
                w.player.inventory.Add("bronze_pickaxe", 1);
                frames(w, 1);
                // The animation is read while the work is going: an outcrop
                // can give out on its very first ore, and then there is none.
                w.TryInteract(ctx);
                frames(w, 2);
                const bool mining = w.player.GatherClip() == "mine";
                w.TryInteract(ctx);
                frames(w, 2);
                const float t = time_to_first(w, "copper_ore", 12.0f);
                Check(t > 0.0f && mining, "a bronze pickaxe mines copper, with the mining animation");
                w.player.x += 200.0f;
                frames(w, 3);
                Check(!w.Gathering() && w.player.GatherClip().empty(), "walking away stops the work");
            }
        }
        {
            World w;
            if (work_at(w, "fernhollow", is_pond, SKILL_FISHING, 1)) {
                Check(w.player.interact.label.find("needs a fishing rod") != string::npos, "the pond wants a rod");
                w.player.inventory.Add("fishing_rod", 1);
                frames(w, 1);
                const float t = time_to_first(w, "raw_minnow", 15.0f);
                Check(t > 0.0f, "with a rod, a level 1 fisher catches a minnow (" + std::to_string(t).substr(0, 4) + "s)");
                Check(w.player.GatherClip() == "fish" && w.player.skills.Xp(SKILL_FISHING) > 0,
                      "fishing plays its animation and trains Fishing");
            }
            World w99;
            if (work_at(w99, "fernhollow", is_pond, SKILL_FISHING, 99)) {
                w99.player.inventory.Add("fishing_rod", 1);
                frames(w99, 1);
                w99.TryInteract(ctx);
                int catches = 0, multi = 0, last = 0;
                for (int f = 0; f < 60 * 60 && catches < 30; ++f) {
                    frames(w99, 1);
                    int total = 0;
                    for (int s2 = 0; s2 < w99.player.inventory.SlotCount(); ++s2) {
                        const ItemDef* d = items.Get(w99.player.inventory.Slot(s2).id);
                        if (d && d->fish_level > 0) total += w99.player.inventory.Slot(s2).qty;
                    }
                    if (total > last) { ++catches; if (total - last > 1) ++multi; last = total; }
                }
                Check(catches >= 20 && multi > 0, "at Fishing 99 some casts land more than one fish (" +
                      std::to_string(multi) + " of " + std::to_string(catches) + ")");
            }
        }
        input.Update(dt);
    }

    // --- quests and dialogue: nothing early, dailies, and the dream --------------------------------
    Section("quests and dialogue open when they should");
    {
        // Every NPC's opening node, from the maps.
        std::map<string, string> npc_root;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const auto& n : m.Npcs()) if (!n.dialogue.empty()) npc_root[n.id] = n.dialogue;
        }
        // Nodes reachable from each NPC's root.
        std::map<string, std::set<string>> reach;
        {
            std::ifstream in("data/dialogue.json");
            json root;
            in >> root;
            for (const auto& kv : npc_root) {
                std::set<string>& seen = reach[kv.first];
                vector<string> todo = {kv.second};
                while (!todo.empty()) {
                    const string nid = todo.back();
                    todo.pop_back();
                    if (nid.empty() || nid == "end" || seen.count(nid) || !root.contains(nid)) continue;
                    seen.insert(nid);
                    for (const auto& o : root[nid].value("options", json::array()))
                        todo.push_back(o.value("next", string("end")));
                }
            }
        }
        const auto node_owner_has = [&](const string& npc, const string& node) {
            return reach.count(npc) && reach[npc].count(node);
        };

        // 1. An offer is only reachable through a line that asks whether the
        //    quest can be taken right now, not merely whether it has been.
        {
            std::ifstream in("data/dialogue.json");
            json root;
            in >> root;
            int offers = 0;
            for (auto it = root.begin(); it != root.end(); ++it)
                for (const auto& o : it.value().value("options", json::array())) {
                    const string next = o.value("next", string(""));
                    if (!root.contains(next)) continue;
                    for (const auto& inner : root[next].value("options", json::array())) {
                        if (!inner.contains("action") || !inner["action"].contains("start_quest")) continue;
                        const string quest = inner["action"]["start_quest"].get<string>();
                        ++offers;
                        const bool gated = o.contains("if") && o["if"].value("quest", string("")) == quest &&
                                           o["if"].value("state", string("")) == "available";
                        Check(gated, it.key() + " -> " + next + " offers " + quest +
                                     " only when it can be taken");
                    }
                }
            Check(offers >= 8, "the dialogue offers quests (" + std::to_string(offers) + ")");
        }

        // 2. Every talk stage is finished by a line, and every delivery by a line
        //    that takes the goods -- in that NPC's own conversation, only while
        //    the quest is at that stage.
        {
            std::ifstream in("data/dialogue.json");
            json root;
            in >> root;
            for (const auto& kv : quests.Definitions()) {
                const QuestDef& q = kv.second;
                for (size_t si = 0; si < q.stages.size(); ++si) {
                    const QuestStage& st = q.stages[si];
                    if (st.type != ObjectiveType::Talk && st.type != ObjectiveType::Deliver) continue;
                    const string npc = st.type == ObjectiveType::Talk ? st.target : st.deliver_to;
                    bool found = false;
                    // An order is handed in by the giver's one "I have your order" line,
                    // which is only there while an order of theirs can be filled.
                    const bool order = q.daily && q.giver == npc && st.type == ObjectiveType::Deliver;
                    for (auto it = root.begin(); it != root.end() && !found; ++it) {
                        if (!node_owner_has(npc, it.key())) continue;
                        for (const auto& o : it.value().value("options", json::array())) {
                            // The action sits on this option or on the one line of the node it opens.
                            vector<json> actions;
                            if (o.contains("action")) actions.push_back(o["action"]);
                            const string next = o.value("next", string(""));
                            if (root.contains(next))
                                for (const auto& inner : root[next].value("options", json::array()))
                                    if (inner.contains("action")) actions.push_back(inner["action"]);
                            bool does = false;
                            if (order && o.contains("action") && o["action"].value("hand_in", false) &&
                                o.contains("if") && o["if"].value("order_ready", false)) {
                                found = true;
                                break;
                            }
                            for (const json& a : actions) {
                                if (st.type == ObjectiveType::Talk && a.value("advance", string("")) == npc) does = true;
                                if (st.type == ObjectiveType::Deliver && a.value("take", string("")) == st.target &&
                                    a.value("take_qty", 1) >= st.count) does = true;
                            }
                            if (!does || !o.contains("if")) continue;
                            const json& c = o["if"];
                            const bool staged = c.value("quest", string("")) == q.id &&
                                                c.value("state", string("")) == "active" &&
                                                (q.stages.size() == 1 || c.value("stage", -1) == static_cast<int>(si) ||
                                                 st.type == ObjectiveType::Deliver);
                            const bool holds = st.type != ObjectiveType::Deliver ||
                                               (c.value("has_item", string("")) == st.target && c.value("qty", 1) >= st.count);
                            if (staged && holds) found = true;
                        }
                    }
                    Check(found, q.id + " stage " + std::to_string(si + 1) + " is finished by a line in " +
                                 npc + "'s conversation, only while it is that stage");
                }
            }
        }

        // 3. The first thing anyone says respects the player's progress.
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            const auto shown = [&](const string& root_node) {
                DialogueRunner r;
                r.Begin(&dialogue, root_node, "npc", "Someone", dc);
                std::set<string> out;
                for (const DialogueOption* o : r.VisibleOptions()) out.insert(o->next);
                return out;
            };
            auto maren = shown("maren_root");
            Check(maren.count("maren_letter_offer") && !maren.count("maren_road_offer") &&
                  !maren.count("maren_depths_offer") && !maren.count("maren_letter_done") &&
                  !maren.count("maren_not_yet"),
                  "a new character is offered Maren's letter and nothing further, from her first line");
            log.Start("q_marens_letter");
            QuestEvent talk;
            talk.type = ObjectiveType::Talk;
            talk.target = "npc_guildmaster";
            log.Notify(talk, inv);
            maren = shown("maren_root");
            Check(maren.count("maren_letter_done") && !maren.count("maren_letter_offer"),
                  "with the letter delivered, Maren's hand-in line is there and her offer is gone");
            talk.target = "npc_maren";
            log.Notify(talk, inv);
            maren = shown("maren_root");
            Check(maren.count("maren_not_yet") && !maren.count("maren_road_offer"),
                  "too weak for the Sunken Road, Maren says to come back later instead of offering it");
            LevelUp up;
            for (int skill : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE}) sk.AddXp(skill, XpForLevel(8), up);
            sk.AddXp(SKILL_HITPOINTS, XpForLevel(14), up);
            maren = shown("maren_root");
            Check(maren.count("maren_road_offer") && !maren.count("maren_depths_offer") && !maren.count("maren_not_yet"),
                  "strong enough, she offers the Sunken Road, and still not the depths");

            auto hesper = shown("hesper_root");
            Check(hesper.count("hesper_lights") && !hesper.count("hesper_dreamed") && !hesper.count("hesper_brute_offer"),
                  "Hesper wonders about the lights until the player has dreamed");
            flags.insert("visited:dreamworld");
            hesper = shown("hesper_root");
            Check(!hesper.count("hesper_lights") && hesper.count("hesper_dreamed"),
                  "and once they have, she talks about where the lights come from");
            auto smith = shown("smith_root");
            Check(smith.count("smith_needs") && !smith.count("smith_orders"), "Halda asks for copper before the forge is lit");
            auto wendel = shown("wendel_root");
            Check(wendel.count("wendel_fish") && !wendel.count("wendel_fish_later"),
                  "Wendel does not credit a beginner's fishing");
        }

        // 4. Dailies: posted a few at a time, the same all day, reset at dawn.
        {
            WorldClock c;
            c.Set(3, 4.9f);
            const int before_dawn = c.QuestDay();
            c.Set(3, 5.0f);
            Check(before_dawn == 2 && c.QuestDay() == 3, "the quest day turns over at dawn, not midnight");

            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            std::map<string, int> pool_size;
            for (const auto& kv : log.Definitions()) {
                if (!kv.second.daily) continue;
                ++pool_size[kv.second.pool];
                bool gathers = false;
                for (const QuestStage& st2 : kv.second.stages) if (st2.type == ObjectiveType::Collect) gathers = true;
                Check(!gathers, kv.first + " is spent, not merely held: a daily cannot be a collect quest");
            }
            Check(pool_size["havenbrook"] >= 4 && pool_size["mossvale"] >= 3 && pool_size["reverie"] >= 3,
                  "Havenbrook, Mossvale and the dream each have a pool of dailies");
            for (const auto& pool : pool_size) {
                std::set<string> ever;
                bool steady = true, sized = true;
                for (int day = 1; day <= 21; ++day) {
                    log.SetDay(day);
                    const auto today = log.PoolToday(pool.first);
                    if (today != log.PoolToday(pool.first)) steady = false;
                    if (static_cast<int>(today.size()) != std::min(pool.second, log.PostsPerDay(pool.first))) sized = false;
                    ever.insert(today.begin(), today.end());
                }
                Check(steady && sized, pool.first + " posts " + std::to_string(log.PostsPerDay(pool.first)) +
                                       " dailies a day, the same ones all day");
                Check(static_cast<int>(ever.size()) == pool.second, "and over three weeks every one of them comes up");
            }

            Skills sk;
            Inventory inv(&items);
            int day = 1;
            const auto posted = [&](const string& id) {
                log.SetDay(day);
                const auto t = log.PoolToday("havenbrook", &sk);
                return std::find(t.begin(), t.end(), id) != t.end();
            };
            while (!posted("q_daily_boar") && day < 60) ++day;
            Check(log.CanStart("q_daily_boar", sk), "a posted daily can be taken");
            log.Start("q_daily_boar");
            QuestEvent kill;
            kill.type = ObjectiveType::Kill;
            kill.target = "boar";
            kill.map_id = "overworld";
            kill.amount = 5;
            log.Notify(kill, inv);
            Check(log.IsComplete("q_daily_boar") && log.Completions("q_daily_boar") == 1, "and done");
            Check(!log.CanStart("q_daily_boar", sk), "not twice in one day");
            const int done_on = day;
            ++day;
            while (!posted("q_daily_boar") && day < 90) ++day;
            Check(day > done_on && log.CanStart("q_daily_boar", sk), "but again on a later day it is posted");
            log.Start("q_daily_boar");
            Check(log.IsActive("q_daily_boar") && log.Counter("q_daily_boar") == 0, "starting fresh");
            log.Notify(kill, inv);
            Check(log.Completions("q_daily_boar") == 2, "and counting how often it has been done");
            ++day;
            while (posted("q_daily_boar") && day < 120) ++day;
            Check(!log.CanStart("q_daily_boar", sk), "on a day it is not posted, it cannot be taken");

            QuestLog back;
            back.LoadDefinitions("data/quests.json");
            back.FromJson(log.ToJson());
            back.SetDay(done_on);
            Check(back.Completions("q_daily_boar") == 2 && !back.CanStart("q_daily_boar", sk),
                  "when a daily was last done survives a save");
        }

        // 5. The dream quests, through the world.
        {
            Input input;
            std::mt19937 rng(77);
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            GameContext ctx;
            ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
            ctx.quests = &log;        ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
            ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
            ctx.input = &input;       ctx.rng = &rng;
            Skills sk;
            Check(!log.CanStart("q_the_water_remembers", sk), "the dream quest waits on Mira's offering");
            Inventory inv(&items);
            log.Start("q_old_offering");
            QuestEvent give;
            give.type = ObjectiveType::Deliver;
            give.target = "raw_boar";
            give.secondary = "npc_mira";
            give.amount = 2;
            log.Notify(give, inv);
            Check(log.CanStart("q_the_water_remembers", sk), "and opens once the offering is made");
            log.Start("q_the_water_remembers");

            World w;
            w.player.Init(ctx, "player_hero");
            w.clock.Set(1, 22.0f);   // a dream in daylight wakes straight up
            Check(w.LoadMap("dreamworld", "arrival", ctx) && log.Stage("q_the_water_remembers") == 1,
                  "arriving in the dream counts");
            Check(w.Flagged("visited:dreamworld"), "and is remembered");
            const MapObject* voice = nullptr;
            for (const MapObject& o : w.CurrentMap().Objects()) if (o.id == "dream_voice") voice = &o;
            if (voice) {
                w.player.x = voice->x;
                w.player.y = voice->y + 18.0f;
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
                w.TryInteract(ctx);
            }
            Check(voice && log.Stage("q_the_water_remembers") == 2, "reading the voice in the dream counts");
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &w.player.inventory; dc.skills = &w.player.skills;
            dc.flags = &w.Flags();
            DialogueRunner mira;
            mira.Begin(&dialogue, "mira_root", "npc_mira", "Mira", dc);
            bool hand_in = false;
            for (const DialogueOption* o : mira.VisibleOptions())
                if (o->next == "mira_dream_done") hand_in = true;
            Check(hand_in, "and waking to tell Mira is a line she offers");

            QuestEvent brute;
            brute.type = ObjectiveType::Kill;
            brute.target = "nightmare_brute";
            brute.map_id = "dreamworld";
            QuestEvent tell;
            tell.type = ObjectiveType::Talk;
            tell.target = "npc_mira";
            log.Notify(tell, inv);
            Check(log.IsComplete("q_the_water_remembers"), "telling her completes it");
            LevelUp up;
            for (int skill : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE}) sk.AddXp(skill, XpForLevel(20), up);
            sk.AddXp(SKILL_HITPOINTS, XpForLevel(22), up);
            Check(log.CanStart("q_lights_on_the_pond", sk), "Hesper's hunt opens after it, for a fighter of 18");
            log.Start("q_lights_on_the_pond");
            brute.map_id = "overworld";
            log.Notify(brute, inv);
            Check(log.Stage("q_lights_on_the_pond") == 0, "the brute only counts in the dream");
            brute.map_id = "dreamworld";
            log.Notify(brute, inv);
            Check(log.Stage("q_lights_on_the_pond") == 1, "the Nightmare Brute put down in the dream counts");
            give.target = "dream_shard";
            give.secondary = "npc_hesper";
            give.amount = 6;
            log.Notify(give, inv);
            Check(log.IsComplete("q_lights_on_the_pond"), "and the shards carried back to Hesper finish it");
            log.SetDay(1);
            int posted = 0;
            for (int day = 1; day <= 10; ++day) {
                log.SetDay(day);
                for (const string& id : log.PoolToday("reverie")) posted += log.CanStart(id, sk);
            }
            Check(posted > 0, "and the Dreamer's Slate posts its dailies to someone who has dreamed");
        }
    }

    // --- day, night and dreams ----------------------------------------------------------
    Section("day, night and dreams");
    {
        // --- the clock ---------------------------------------------------------------
        {
            WorldClock c;
            c.Set(1, 12.0f);
            Check(c.Darkness() == 0.0f && !c.IsNight() && !c.CanSleep() && string(c.Phase()) == "Day",
                  "noon is light, not night, and no time for bed");
            c.Set(1, 23.0f);
            Check(c.Darkness() == 1.0f && c.IsNight() && c.CanSleep() && string(c.Phase()) == "Night",
                  "eleven at night is dark, and a bed will take you");
            c.Set(1, 3.0f);
            Check(c.IsNight() && c.CanSleep(), "three in the morning is still night");
            c.Set(1, 4.5f);
            Check(c.IsNight() && !c.CanSleep(), "half an hour before dawn is too late to start a dream");
            c.Set(1, 19.5f);
            Check(!c.IsNight() && c.CanSleep(), "dusk is early enough for bed");

            bool rising = true;
            float last = -1.0f;
            for (float h = 17.5f; h <= 21.0f; h += 0.1f) {
                c.Set(1, h);
                if (c.Darkness() + 1e-4f < last) rising = false;
                last = c.Darkness();
            }
            Check(rising, "the dark only deepens through dusk");
            c.Set(1, 5.5f);
            const float dawn = c.Darkness();
            Check(dawn > 0.0f && dawn < 1.0f && c.Warmth() > 0.5f, "dawn is half light, and warm");
            c.Set(1, 19.0f);
            Check(c.Warmth() > 0.9f, "sunset is warm");

            c.Set(1, 23.5f);
            c.Advance(WorldClock::SECONDS_PER_HOUR);
            Check(c.Day() == 2 && fabsf(c.Hours() - 0.5f) < 0.01f,
                  "half a minute is an hour, and midnight starts the next day");
            Check(c.TimeText() == "00:30", "the clock reads 00:30");

            c.Set(3, 23.0f);
            Check(fabsf(c.SecondsToDawn() - 6.0f * WorldClock::SECONDS_PER_HOUR) < 0.1f,
                  "from eleven, dawn is six hours of real half-minutes away");
            Check(!c.DreamOver(), "a dream at eleven is not over");
            c.SkipToDawn();
            Check(c.Day() == 4 && c.Hours() == WorldClock::NIGHT_END && c.DreamOver(),
                  "skipping to dawn from the evening lands on the next morning");
            c.Set(3, 2.0f);
            c.SkipToDawn();
            Check(c.Day() == 3 && c.Hours() == WorldClock::NIGHT_END,
                  "and from after midnight, on the same morning");

            WorldClock back;
            c.Set(7, 21.25f);
            back.FromJson(c.ToJson());
            Check(back.Day() == 7 && fabsf(back.Hours() - 21.25f) < 0.001f, "the clock survives a save");
        }

        // --- what sleep needs --------------------------------------------------------
        {
            int beds = 0, campsites = 0;
            bool dream_ok = false;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const MapObject& o : m.Objects()) {
                    if (o.type == "bed") {
                        ++beds;
                        Check(m.IsInterior(), o.id + " is a bed indoors");
                        Check(!o.sprite.empty() && fs::exists(o.sprite), o.id + " has bed art on disk");
                    }
                    if (o.type == "campsite") {
                        ++campsites;
                        Check(!m.IsInterior(), o.id + " is a campsite under the sky");
                    }
                }
                if (string(id) == "dreamworld") {
                    SDL_FPoint arrival;
                    int wake = 0, crystals = 0, nightmares = 0;
                    for (const MapObject& o : m.Objects()) {
                        if (o.type == "dream_wake") ++wake;
                        if (o.yield == "dream_shard" && o.skill == "Mining") ++crystals;
                    }
                    for (const auto& e : m.Enemies()) {
                        const EnemyDef* d = enemy_db.Get(e.type);
                        if (d && d->tint.r != 255) ++nightmares;
                    }
                    dream_ok = m.Spawn("arrival", arrival) && m.Ambient() == "dream" && !m.IsInterior();
                    Check(wake == 1, "the dream has one waking stone");
                    Check(crystals >= 5, "the dream has dream crystals to mine");
                    Check(nightmares >= 8, "the dream is full of nightmares in their own colours");
                    bool only_deeper = !m.Portals().empty();
                    for (const Portal& out : m.Portals()) {
                        Map beyond;
                        only_deeper &= beyond.Load("maps/" + out.target_map + ".mx") && beyond.Ambient() == "dream";
                    }
                    Check(only_deeper, "there is no walking out of a dream: its one way on is a ladder, further in");
                }
            }
            Check(beds >= 6, "there are beds in the houses and the inn");
            Check(campsites >= 2, "there are campsites on the woodland trails");
            Check(dream_ok, "the dreamworld has an arrival point and a dream's air");

            const ItemDef* bedroll = items.Get("bedroll");
            const ItemDef* shard = items.Get("dream_shard");
            const ItemDef* catcher = items.Get("dreamcatcher");
            Check(bedroll && bedroll->use == "camp" && fs::exists(bedroll->icon), "a bedroll pitches a camp and has an icon");
            Check(shard && fs::exists(shard->icon) && catcher && catcher->slot == SLOT_AMULET && fs::exists(catcher->icon),
                  "dream shards and the dreamcatcher exist, with icons");
            bool bedroll_recipe = false, catcher_recipe = false;
            for (const ItemDef* r : items.Recipes(CraftStation::Workbench))
                if (r->craft_result == "dreamcatcher") catcher_recipe = true;
            // Two hides and the thread to sew them: a tanner's work, on a tanner's frame.
            for (const ItemDef* r : items.Recipes(CraftStation::Rack))
                if (r->craft_result == "bedroll") bedroll_recipe = true;
            Check(bedroll_recipe, "a bedroll is made on a tanning rack");
            Check(catcher_recipe, "a dreamcatcher is made at a workbench from dream shards");
            for (const char* table : {"nightmare_shade", "dread_boar", "nightmare_brute", "chest_dream"})
                Check(loot.Has(table), string("loot table ") + table + " exists");
        }

        // --- going to sleep, and waking up ----------------------------------------------
        Input input;
        std::mt19937 rng(20260914);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        // Runs frames until the test says stop or the time runs out.
        const auto until = [&](World& w, float seconds, const std::function<bool()>& done) {
            for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) {
                if (done()) return true;
                frames(w, 1);
            }
            return done();
        };
        // Uses whatever bed is underfoot and says whether it asked its
        // question: a Sleep request, and nothing begun until it is answered.
        const auto asks = [&](World& w) {
            w.TakeRequests();
            w.TryInteract(ctx); frames(w, 1);
            bool asked = false;
            for (const WorldRequest& r : w.TakeRequests())
                asked |= r.type == WorldRequest::Type::Sleep && !r.title.empty();
            return asked && !w.TransitionPending();
        };
        // And answers it, the way the panel does.
        const auto lie_down = [&](World& w, World::SleepChoice how) {
            return asks(w) && w.Sleep(how, ctx);
        };
        // The inn's first guest room, standing at the foot of the bed.
        const auto at_inn_bed = [&](World& w) -> const MapObject* {
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap("house_inn_upper", "default", ctx)) return nullptr;
            for (const MapObject& o : w.CurrentMap().Objects())
                if (o.id == "bed_inn_1") {
                    w.player.x = o.x;
                    w.player.y = o.y + 18.0f;
                    frames(w, 30);        // settle, and let the arrival portals arm
                    return &o;
                }
            return nullptr;
        };

        {
            World w;
            const MapObject* bed = at_inn_bed(w);
            Check(bed != nullptr, "the inn has a bed to test with");
            if (bed) {
                w.clock.Set(1, 12.0f);
                frames(w, 1);
                Check(w.player.interact.kind == InteractTarget::Object &&
                      w.player.interact.label.find("after dusk") != string::npos,
                      "by day the bed says you can sleep after dusk");
                Check(!asks(w), "and by day it does not ask how you would spend the night");
                frames(w, 90);
                Check(!w.InDream() && !w.TransitionPending(), "and by day it will not let you sleep");
                Check(!w.Sleep(World::SleepChoice::Through, ctx) && !w.Sleep(World::SleepChoice::Reverie, ctx) &&
                      !w.TransitionPending(), "either way");

                w.clock.Set(1, 21.0f);
                w.player.Damage(4);
                frames(w, 1);
                Check(w.player.interact.label == "Go to bed", "at night the bed offers itself");
                const float bx = w.player.x, by = w.player.y;
                Check(asks(w), "pressing E at night asks the question, and nothing has begun");
                Check(w.player.hp < w.player.max_hp && w.clock.IsNight() && w.MapId() == "house_inn_upper",
                      "backing out of it leaves the evening as it was");
                Check(lie_down(w, World::SleepChoice::Reverie) && w.TransitionPending() && !w.FadeCaption().empty(),
                      "choosing the Reverie starts to fall asleep");
                Check(until(w, 5.0f, [&] { return w.InDream() && !w.TransitionPending(); }),
                      "and the player arrives in the dreamworld");
                Check(w.MapId() == "dreamworld" && w.Dream().active && w.Dream().map == "house_inn_upper" &&
                      fabsf(w.Dream().x - bx) < 0.5f && fabsf(w.Dream().y - by) < 0.5f,
                      "the dream remembers exactly where the player lay down");
                Check(w.player.hp == w.player.max_hp, "sleeping restores health");
                Check(w.AmbientLight().b > w.AmbientLight().g, "the dream's light is violet");

                // The night runs out.
                w.clock.Set(1, WorldClock::NIGHT_END - 0.01f);
                Check(until(w, 6.0f, [&] { return !w.InDream() && !w.TransitionPending(); }),
                      "dawn ends the dream");
                Check(w.MapId() == "house_inn_upper" && fabsf(w.player.x - bx) < 1.0f &&
                      fabsf(w.player.y - by) < 1.0f,
                      "and the player wakes where they went to sleep");
                Check(!w.Dream().active && w.TakeWake() == World::WakeReason::Dawn,
                      "waking at dawn is reported as dawn");

                // Dying in a dream is a rude awakening, not a death.
                w.clock.Set(2, 22.0f);
                frames(w, 1);
                lie_down(w, World::SleepChoice::Reverie);
                until(w, 5.0f, [&] { return w.InDream() && !w.TransitionPending(); });
                const bool dreaming = w.InDream();
                w.player.Damage(9999);
                Check(dreaming && until(w, 6.0f, [&] { return !w.InDream() && !w.TransitionPending(); }),
                      "a nightmare that kills the dreamer throws them awake");
                Check(!w.player.IsDead() && w.player.hp == w.player.max_hp && w.MapId() == "house_inn_upper",
                      "alive, whole, and back in bed");
                // A second or two of fade has passed since, so allow a few minutes.
                Check(w.clock.Day() == 3 && w.clock.Hours() >= WorldClock::NIGHT_END &&
                      w.clock.Hours() < WorldClock::NIGHT_END + 0.2f,
                      "and the rest of the night is gone");
                Check(w.TakeWake() == World::WakeReason::Nightmare, "waking from it is reported as a nightmare");

                // The waking stone wakes you sooner.
                w.clock.Set(3, 21.0f);
                frames(w, 1);
                lie_down(w, World::SleepChoice::Reverie);
                until(w, 5.0f, [&] { return w.InDream() && !w.TransitionPending(); });
                for (const MapObject& o : w.CurrentMap().Objects())
                    if (o.type == "dream_wake") { w.player.x = o.x; w.player.y = o.y + 20.0f; }
                frames(w, 1);
                Check(w.player.interact.kind == InteractTarget::Object, "the waking stone can be reached");
                w.TryInteract(ctx); frames(w, 1);
                Check(until(w, 6.0f, [&] { return !w.InDream() && !w.TransitionPending(); }) &&
                      w.clock.IsNight() && w.TakeWake() == World::WakeReason::Stone,
                      "touching the waking stone wakes the player in the night");
            }
        }

        // --- the other answer: sleeping the night through -------------------------------
        {
            World w;
            const MapObject* bed = at_inn_bed(w);
            if (bed) {
                w.clock.Set(4, 21.5f);
                w.player.Damage(6);
                w.player.SpendMana(w.player.Mana());
                frames(w, 1);
                const float bx = w.player.x, by = w.player.y;
                const int quest_day = w.clock.QuestDay();
                w.TakeWake();
                Check(lie_down(w, World::SleepChoice::Through) && w.TransitionPending() &&
                      w.FadeCaption() == "You sleep the night through...",
                      "choosing to sleep the night through lies down too");
                Check(!w.Dream().active, "with no dream to come back from");
                bool dreamt = false;
                const bool up = until(w, 6.0f, [&] {
                    dreamt |= w.InDream();
                    return !w.TransitionPending() && w.FadeAmount() <= 0.0f;
                });
                Check(up && !dreamt && w.MapId() == "house_inn_upper", "the night passes without ever leaving the room");
                Check(fabsf(w.player.x - bx) < 1.0f && fabsf(w.player.y - by) < 1.0f,
                      "and the player wakes where they lay down");
                // A second or two of fade has passed since dawn, so allow a few minutes.
                Check(w.clock.Day() == 5 && w.clock.Hours() >= WorldClock::NIGHT_END &&
                      w.clock.Hours() < WorldClock::NIGHT_END + 0.2f && !w.clock.IsNight(),
                      "at dawn of the next day (" + w.clock.TimeText() + ", day " + std::to_string(w.clock.Day()) + ")");
                Check(w.clock.QuestDay() == quest_day + 1, "which turns the quest day over");
                Check(w.player.hp == w.player.max_hp && w.player.Mana() == w.player.MaxMana() && w.player.MaxMana() > 0,
                      "rested: health and mana are whole");
                Check(w.TakeWake() == World::WakeReason::Slept && w.TakeWake() == World::WakeReason::None,
                      "waking is reported as a night slept through, once");
                Check(w.FadeCaption().empty(), "and the caption is gone with the dark");
                Check(w.player.interact.label.find("after dusk") != string::npos,
                      "the bed, in the morning, is a bed for after dusk again");

                // After midnight the dawn is today's, not tomorrow's.
                w.clock.Set(7, 2.0f);
                frames(w, 1);
                Check(lie_down(w, World::SleepChoice::Through) &&
                      until(w, 6.0f, [&] { return !w.TransitionPending() && w.FadeAmount() <= 0.0f; }) &&
                      w.clock.Day() == 7 && w.clock.Hours() >= WorldClock::NIGHT_END,
                      "gone to bed after midnight, the dawn is the same day's");
                w.TakeWake();
            }
        }

        // Nobody sleeps with an orc at the door.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx)) {
                w.enemies.clear();
                w.clock.Set(1, 22.0f);
                const EnemyDef* stats = enemy_db.Get("orc1");
                if (stats) {
                    EnemySpawnDef def;
                    def.type = "orc1"; def.x = w.player.x + 90.0f; def.y = w.player.y;
                    auto e = std::make_unique<Enemy>();
                    e->Init(stats, def, ctx);
                    w.enemies.push_back(std::move(e));
                }
                Check(!w.AskToSleep("A bed") && w.TakeRequests().empty(),
                      "a bed does not ask with a monster nearby");
                Check(!w.Sleep(World::SleepChoice::Reverie, ctx) && !w.Sleep(World::SleepChoice::Through, ctx) &&
                      !w.TransitionPending(), "you cannot sleep with a monster nearby, either way");
                w.enemies.clear();
                Check(w.AskToSleep("A bed") && w.TakeRequests().size() == 1, "and once it is gone, it asks");
                Check(w.Sleep(World::SleepChoice::Through, ctx), "and you can");
            }
        }

        // --- the player's own camp -------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx)) {
                w.enemies.clear();
                w.clock.Set(1, 12.0f);
                w.player.y -= 60.0f;
                w.player.inventory.Add("bedroll", 1);
                const auto bedroll_slot = [&] {
                    for (int i = 0; i < w.player.inventory.SlotCount(); ++i)
                        if (w.player.inventory.Slot(i).id == "bedroll") return i;
                    return -1;
                };
                const auto camp_objects = [&] {
                    int n = 0;
                    for (const MapObject& o : w.CurrentMap().Objects())
                        if (o.id.rfind("player_camp", 0) == 0) ++n;
                    return n;
                };
                const string why = w.PitchCamp(bedroll_slot(), ctx);
                Check(why.empty(), "a bedroll pitches a camp on open ground" + (why.empty() ? string("") : " (" + why + ")"));
                Check(w.PlayerCamp().pitched && w.PlayerCamp().map == "overworld" && camp_objects() == 2,
                      "the camp is a tent and a fire on this map");
                Check(bedroll_slot() < 0, "the bedroll leaves the bag");

                const World::Camp pitched = w.PlayerCamp();
                w.LoadMap("town_havenbrook", "", ctx);
                Check(camp_objects() == 0, "the camp is not in town");
                w.LoadMap("overworld", "start", ctx);
                Check(camp_objects() == 2, "and is still there when you come back");

                // By night it is a bed; by day, packed away again.
                w.enemies.clear();
                w.player.x = pitched.x;
                w.player.y = pitched.y + 18.0f;
                w.clock.Set(1, 21.0f);
                frames(w, 30);
                Check(w.player.interact.label == "Sleep at your camp", "at night the camp offers sleep");
                {
                    w.TakeRequests();
                    w.TryInteract(ctx); frames(w, 1);
                    const vector<WorldRequest> reqs = w.TakeRequests();
                    Check(reqs.size() == 1 && reqs.front().type == WorldRequest::Type::Sleep &&
                          reqs.front().title == "Your camp" && w.PlayerCamp().pitched,
                          "and asks the same question a bed does, without packing up");
                }
                w.clock.Set(1, 12.0f);
                frames(w, 1);
                Check(w.player.interact.label == "Pack up your camp", "by day it offers to be packed up");
                w.TryInteract(ctx); frames(w, 1);
                Check(!w.PlayerCamp().pitched && camp_objects() == 0 && bedroll_slot() >= 0,
                      "packing up returns the bedroll");

                World inside;
                inside.player.Init(ctx, "player_hero");
                if (inside.LoadMap("house_inn", "default", ctx)) {
                    inside.player.inventory.Add("bedroll", 1);
                    int slot = -1;
                    for (int i = 0; i < inside.player.inventory.SlotCount(); ++i)
                        if (inside.player.inventory.Slot(i).id == "bedroll") slot = i;
                    Check(!inside.PitchCamp(slot, ctx).empty() && !inside.PlayerCamp().pitched,
                          "there is no pitching a tent indoors");
                }
            }
        }

        // --- the light --------------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx)) {
                w.clock.Set(1, 12.0f);
                const SDL_Color noon = w.AmbientLight();
                w.clock.Set(1, 23.0f);
                const SDL_Color night = w.AmbientLight();
                w.clock.Set(1, 19.0f);
                const SDL_Color dusk = w.AmbientLight();
                Check(noon.r == 255 && noon.g == 255 && noon.b == 255, "noon is untinted");
                Check(night.r < 140 && night.b > night.r, "midnight is dark and blue");
                Check(dusk.r > dusk.b, "sunset is warm");
                bool player_lit = false;
                for (const Light& l : w.CollectLights())
                    if (fabsf(l.x - w.player.x) < 1.0f) player_lit = true;
                Check(player_lit, "at night the player carries a little light");
                w.clock.Set(1, 12.0f);
                Check(w.CollectLights().empty(), "by day nothing needs lighting");

                w.clock.Set(1, 23.0f);
                w.LoadMap("house_inn", "default", ctx);
                const SDL_Color indoors = w.AmbientLight();
                Check(indoors.r > night.r, "indoors at night is not as dark as outside");
                bool hearth = false;
                for (const Light& l : w.CollectLights()) if (l.color.g < 200) hearth = true;
                Check(hearth, "the inn's fire casts light at night");
                w.LoadMap("dungeon_emberfell_1", "entrance", ctx);
                const SDL_Color mine = w.AmbientLight();
                Check(mine.r == 255 && mine.b == 255, "the mine keeps its own dark, whatever the hour");
            }
        }
        input.Update(dt);

        // --- saving a dream -------------------------------------------------------------------
        // Only into a slot nobody is using, and cleaned up after.
        if (!SaveSystem::Exists(3)) {
            World w;
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("dreamworld", "arrival", ctx)) {
                w.clock.Set(4, 23.5f);
                w.SetDream({true, "fernhollow", 400.0f, 500.0f});
                w.SetCamp({true, "whisperwood_trail", 1200.0f, 700.0f});
                Check(SaveSystem::Save(3, w, log, 12.0f), "a save made in a dream writes");
                World loaded;
                QuestLog log2;
                log2.LoadDefinitions("data/quests.json");
                float playtime = 0.0f;
                Check(SaveSystem::Load(3, loaded, log2, ctx, playtime), "and loads");
                Check(loaded.InDream() && loaded.Dream().active && loaded.Dream().map == "fernhollow" &&
                      fabsf(loaded.Dream().x - 400.0f) < 0.1f, "still dreaming, with the way back remembered");
                Check(loaded.clock.Day() == 4 && fabsf(loaded.clock.Hours() - 23.5f) < 0.01f,
                      "at the same hour of the same night");
                Check(loaded.PlayerCamp().pitched && loaded.PlayerCamp().map == "whisperwood_trail",
                      "with the camp where it was pitched");
                SaveSystem::Delete(3);
            }
        }

        // --- a save from before the Hollowmarch grew ---------------------------------------------
        // Written as the old layout would have had it -- version 1, twenty
        // cells further left -- and loaded back onto the same ground.
        if (!SaveSystem::Exists(3)) {
            World w;
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            w.player.Init(ctx, "player_hero");
            if (w.LoadMap("overworld", "start", ctx)) {
                const float px = w.player.x, py = w.player.y;
                w.SetCamp({true, "overworld", 1500.0f, 900.0f});
                Check(SaveSystem::Save(3, w, log, 5.0f), "an overworld save writes");
                json j;
                { std::ifstream in(SaveSystem::SlotPath(3)); in >> j; }
                j["version"] = 1;
                j["player"]["x"] = px - 640.0f;
                j["camp"]["x"] = 1500.0f - 640.0f;
                { std::ofstream out(SaveSystem::SlotPath(3), std::ios::trunc); out << j.dump(); }
                World loaded;
                QuestLog log2;
                log2.LoadDefinitions("data/quests.json");
                float playtime = 0.0f;
                Check(SaveSystem::Load(3, loaded, log2, ctx, playtime), "an old overworld save loads");
                Check(fabsf(loaded.player.x - px) < 0.5f && fabsf(loaded.player.y - py) < 0.5f,
                      "an old save stands the player on the same ground after the map grew west");
                Check(fabsf(loaded.PlayerCamp().x - 1500.0f) < 0.5f, "and its camp where it was pitched");
                SaveSystem::Delete(3);
            }
        }
    }

    Section("traders");
    {
        ShopDatabase shops;
        Check(shops.Load("data/shops.json"), "data/shops.json loads");
        Check(shops.All().size() >= 12, "there are at least a dozen traders (" + std::to_string(shops.All().size()) + ")");

        // Which town a map belongs to, for the one-general-store-and-one-more rule.
        const std::map<string, string> town_of = {
            {"town_havenbrook", "havenbrook"}, {"house_smith", "havenbrook"}, {"house_inn", "havenbrook"},
            {"house_inn_upper", "havenbrook"}, {"house_elder", "havenbrook"}, {"guild_hall", "havenbrook"},
            {"mossvale", "mossvale"}, {"mossvale_lodge_hall", "mossvale"}, {"mossvale_herbalist", "mossvale"},
            {"mossvale_weavers", "mossvale"},
            {"fernhollow", "fernhollow"}, {"fernhollow_cottage", "fernhollow"}, {"fernhollow_college", "fernhollow"},
            {"college_grounds", "fernhollow"}, {"college_training", "fernhollow"}, {"college_classroom", "fernhollow"},
            {"whisperwood_trail", "whisperwood"}, {"dreamworld", "reverie"},
            {"westwold", "westwold"}, {"brackenwood", "brackenwood"},
        };

        // Every trader on every map.
        struct Keeper { string map, shop, root; float x, y; };
        std::map<string, Keeper> keepers;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const auto& n : m.Npcs()) {
                if (n.shop.empty()) continue;
                keepers[n.id] = {id, n.shop, n.dialogue, n.x, n.y};
                Check(shops.Get(n.shop) != nullptr, string(id) + " " + n.id + " keeps shop '" + n.shop + "', which exists");
                // Somewhere to stand within talking distance.
                bool reachable = false;
                for (int a = 0; a < 16 && !reachable; ++a) {
                    const float ang = a / 16.0f * 6.2831853f;
                    const float px = n.x + cosf(ang) * 40.0f, py = n.y + sinf(ang) * 40.0f;
                    if (!m.Blocked({px - 7.0f, py - 4.0f, 14.0f, 8.0f}) &&
                        px > 0 && py > 0 && px < m.Width() && py < m.Height()) reachable = true;
                }
                Check(reachable, string(id) + " " + n.id + " can be walked up to");
            }
        }

        json dlg;
        {
            std::ifstream in("data/dialogue.json");
            in >> dlg;
        }
        std::map<string, int> generals, others;
        for (const auto& kv : shops.All()) {
            const ShopDef& s = kv.second;
            const string sid = "shop " + s.id;
            Check(!s.name.empty() && !s.type.empty() && !s.town.empty(), sid + " has a name, a type and a town");
            Check(s.markup >= 1.0f, sid + " never charges below value");
            for (const auto& b : s.buys)
                Check(b.second > 0.0f && b.second < 1.0f, sid + " pays less than value for '" + b.first + "'");
            Check(!s.buys.empty(), sid + " buys something");
            Check(!s.sells.empty(), sid + " sells something");
            for (const ShopStock& line : s.sells) {
                const ItemDef* d = items.Get(line.item);
                Check(d != nullptr, sid + " sells '" + line.item + "', which exists");
                if (d) Check(Trade::Tradeable(*d), sid + " sells '" + line.item + "', which has a price");
                Check(line.stock >= 1 && line.stock <= 20, sid + " keeps a limited stock of " + line.item);
                for (const string& q : line.after)
                    Check(quests.Definition(q) != nullptr, sid + " gates " + line.item + " on quest '" + q + "', which exists");
            }

            // Kept by someone who is standing in the town it belongs to, and
            // who offers to trade from the first thing they say.
            auto k = keepers.find(s.keeper);
            Check(k != keepers.end() && k->second.shop == s.id, sid + " is kept by " + s.keeper + ", who stands on a map");
            if (k != keepers.end()) {
                auto t = town_of.find(k->second.map);
                Check(t != town_of.end() && t->second == s.town,
                      sid + " is in " + s.town + " (" + k->second.map + ")");
                bool offers = false;
                if (dlg.contains(k->second.root))
                    for (const auto& o : dlg[k->second.root].value("options", json::array()))
                        if (o.contains("action") && o["action"].value("shop", string("")) == s.id &&
                            !o.contains("if")) offers = true;
                Check(offers, sid + ": " + s.keeper + " offers to trade from their first line, always");
            }
            (s.General() ? generals : others)[s.town]++;
        }
        for (const char* town : {"havenbrook", "mossvale", "fernhollow", "whisperwood", "reverie"}) {
            Check(generals[town] >= 1, string(town) + " has a general store");
            Check(others[town] >= 1, string(town) + " has another kind of shop as well (" +
                  std::to_string(others[town]) + ")");
        }

        // The forges: a limited supply of materials, and they buy materials and
        // metalwork.
        int forges = 0;
        for (const auto& kv : shops.All()) {
            const ShopDef& s = kv.second;
            if (s.type != "forge") continue;
            ++forges;
            int materials = 0;
            for (const ShopStock& line : s.sells)
                if (const ItemDef* d = items.Get(line.item))
                    if (d->piece == "ore" || d->piece == "bar") ++materials;
            Check(materials >= 3, "shop " + s.id + " sells crafting materials (" + std::to_string(materials) + " lines)");
            for (const char* what : {"copper_ore", "bronze_bar", "bronze_sword", "iron_body", "iron_pickaxe"})
                if (const ItemDef* d = items.Get(what))
                    Check(Trade::SellPrice(s, items, *d) > 0, "shop " + s.id + " buys " + what);
            if (const ItemDef* fish = items.Get("raw_trout"))
                Check(Trade::SellPrice(s, items, *fish) == 0, "shop " + s.id + " does not buy fish");
        }
        Check(forges >= 2, "there is more than one forge");

        // Nothing on a shelf does a quest's work for it: no shop sells what a
        // quest asks the player to gather or hand over until that quest is
        // done, and never what a daily asks for.
        for (const auto& qkv : quests.Definitions())
            for (const QuestStage& st : qkv.second.stages) {
                if (st.type != ObjectiveType::Collect && st.type != ObjectiveType::Deliver) continue;
                for (const auto& kv : shops.All())
                    for (const ShopStock& line : kv.second.sells) {
                        if (line.item != st.target) continue;
                        const ItemDef* want = items.Get(st.target);
                        if (qkv.second.daily && want) {
                            // A repeatable order may ask for what is on a shelf, so long
                            // as buying it to fill the order costs more than it pays.
                            const int cost = Trade::BuyPrice(kv.second, *want) * st.count;
                            Check(qkv.second.rewards.coins < cost,
                                  qkv.first + " pays " + std::to_string(qkv.second.rewards.coins) + "c, less than " +
                                  std::to_string(cost) + "c for its " + st.target + " at " + kv.first);
                            continue;
                        }
                        const bool gated =
                            std::find(line.after.begin(), line.after.end(), qkv.first) != line.after.end();
                        Check(gated, "shop " + kv.first + " does not sell " + st.target + " for " + qkv.first);
                    }
            }

        // --- prices -------------------------------------------------------------
        // Nothing bought anywhere sells anywhere for what it cost.
        int pairs = 0;
        bool arbitrage = false;
        for (const auto& a : shops.All())
            for (const ShopStock& line : a.second.sells)
                if (const ItemDef* d = items.Get(line.item))
                    for (const auto& b : shops.All()) {
                        ++pairs;
                        if (Trade::SellPrice(b.second, items, *d) >= Trade::BuyPrice(a.second, *d)) {
                            arbitrage = true;
                            Check(false, line.item + " bought at " + a.first + " sells at " + b.first + " for no less");
                        }
                    }
        Check(!arbitrage, "nothing can be bought and sold straight back for a profit (" + std::to_string(pairs) + " pairs)");
        if (const ItemDef* letter = items.Get("elder_letter"))
            Check(Trade::SellPrice(*shops.Get("havenbrook_general"), items, *letter) == 0,
                  "quest items cannot be sold, even to a general store");
        if (const ItemDef* coin = items.Get("coins"))
            Check(!Trade::Tradeable(*coin), "coins are not for sale");

        // Making things adds value.
        int recipes = 0, short_value = 0;
        for (const ItemDef* r : items.Recipes()) {
            const ItemDef* made = items.Get(r->craft_result);
            if (!made || made->value <= 1) continue;
            ++recipes;
            if (made->value * r->craft_qty + 1 < ItemDatabase::CRAFT_VALUE_ADD * items.InputValue(*r)) {
                ++short_value;
                Check(false, made->id + " is worth " + std::to_string(made->value) + ", less than its materials warrant");
            }
        }
        Check(short_value == 0, "every one of " + std::to_string(recipes) + " recipes makes something worth more than its materials");

        const auto best_offer = [&](const ItemDef& d) {
            int best = 0;
            for (const auto& kv : shops.All()) best = std::max(best, Trade::SellPrice(kv.second, items, d));
            return best;
        };
        const auto cheapest = [&](const string& id) {
            int best = 0;
            for (const auto& kv : shops.All())
                for (const ShopStock& line : kv.second.sells)
                    if (line.item == id)
                        if (const ItemDef* d = items.Get(id)) {
                            const int p = Trade::BuyPrice(kv.second, *d);
                            if (best == 0 || p < best) best = p;
                        }
            return best;
        };

        // What the land gives sells for something, and working it sells for more.
        for (const auto& kv : items.All()) {
            const ItemDef& d = kv.second;
            const bool gathered = d.piece == "ore" || d.fish_level > 0 || d.id == "logs" ||
                                  d.id == "oak_logs" || d.id == "hide" || d.id == "dream_shard" || d.id == "bones" ||
                                  d.forage_level > 0;
            if (!gathered) continue;
            Check(best_offer(d) > 0, "some trader buys " + d.id + " (" + std::to_string(best_offer(d)) + "c)");
            if (!d.cook_result.empty())
                if (const ItemDef* cooked = items.Get(d.cook_result))
                    Check(best_offer(*cooked) > best_offer(d),
                          "cooking " + d.id + " sells for more (" + std::to_string(best_offer(d)) + "c raw, " +
                          std::to_string(best_offer(*cooked)) + "c cooked)");
        }

        // Buying a forge's bars, smithing them and selling the work turns a
        // profit, as far as the day's stock goes. Anything the forge does not
        // sell is costed at the cheapest shop, or at what it would have fetched
        // if no one sells it (logs, which the player chops).
        for (const auto& kv : shops.All()) {
            const ShopDef& s = kv.second;
            if (s.type != "forge") continue;
            int made = 0;
            for (const ItemDef* r : items.Recipes()) {
                bool uses_bar = false, all_known = true;
                int cost = 0;
                for (const auto& in : r->craft_inputs) {
                    const ItemDef* mat = items.Get(in.first);
                    if (!mat) { all_known = false; break; }
                    int each = 0;
                    for (const ShopStock& line : s.sells)
                        if (line.item == in.first) { each = Trade::BuyPrice(s, *mat); if (mat->piece == "bar") uses_bar = true; }
                    if (each == 0) each = cheapest(in.first);
                    if (each == 0) each = best_offer(*mat);
                    cost += each * in.second;
                }
                const ItemDef* out = items.Get(r->craft_result);
                if (!uses_bar || !all_known || !out) continue;
                ++made;
                const int revenue = best_offer(*out) * r->craft_qty;
                Check(revenue > cost, "smithing " + out->id + " from " + s.id + "'s bars pays: " +
                      std::to_string(cost) + "c in, " + std::to_string(revenue) + "c out");
            }
            Check(made > 0, s.id + " sells bars something can be smithed from");
        }

        // --- trading ---------------------------------------------------------------
        const ShopDef* forge = shops.Get("havenbrook_forge");
        Check(forge != nullptr, "Havenbrook has its forge");
        if (forge) {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            ShopLedger ledger;
            ledger.SetDay(3);
            Inventory bag(&items);
            bag.Add("coins", 5000);
            const ItemDef* bar = items.Get("bronze_bar");
            const int price = Trade::BuyPrice(*forge, *bar);
            const int stock = ledger.Remaining(*forge, "bronze_bar");

            TradeOutcome t = Trade::Buy(*forge, ledger, bag, items, &log, "bronze_bar", 3);
            Check(t.result == TradeResult::Ok && t.qty == 3 && bag.Count("bronze_bar") == 3 &&
                  bag.Coins() == 5000 - 3 * price, "buying three bronze bars takes three prices and gives three bars");
            Check(ledger.Remaining(*forge, "bronze_bar") == stock - 3, "the shelf has three fewer bars");
            t = Trade::Buy(*forge, ledger, bag, items, &log, "bronze_bar", 100);
            Check(t.result == TradeResult::Ok && t.qty == stock - 3, "buying a hundred takes only what is left");
            t = Trade::Buy(*forge, ledger, bag, items, &log, "bronze_bar", 1);
            Check(t.result == TradeResult::SoldOut, "then the bars are sold out");
            ledger.SetDay(3);
            Check(ledger.Remaining(*forge, "bronze_bar") == 0, "and stay sold out the same day");
            ShopLedger saved;
            saved.FromJson(ledger.ToJson());
            Check(saved.Day() == 3 && saved.Remaining(*forge, "bronze_bar") == 0, "a save remembers what was sold today");
            saved.SetDay(4);
            Check(saved.Remaining(*forge, "bronze_bar") == stock, "dawn restocks the shelf");

            // The better stock waits on the smith's own order.
            const auto on_shelf = [&](const string& id) {
                for (const ShopStock* line : Trade::Shelf(*forge, &log)) if (line->item == id) return true;
                return false;
            };
            Check(!on_shelf("iron_bar") && on_shelf("bronze_bar"), "iron bars are not on Halda's shelf before her ore arrives");
            t = Trade::Buy(*forge, ledger, bag, items, &log, "iron_bar", 1);
            Check(t.result == TradeResult::Locked && bag.Count("iron_bar") == 0, "and cannot be bought");
            log.FromJson({{"q_ore_for_the_forge", {{"status", 2}}}});
            Check(on_shelf("iron_bar"), "they are once the forge has its copper");
            for (const ShopStock& line : forge->sells)
                if (line.item == "iron_bar")
                    Check(!Trade::OnShelf(line, nullptr), "with no quest log to ask, gated stock stays hidden");

            // Money, room.
            Inventory broke(&items);
            t = Trade::Buy(*forge, ledger, broke, items, &log, "iron_ore", 1);
            Check(t.result == TradeResult::NoCoins && broke.Count("iron_ore") == 0, "no coins, no ore");
            Inventory full(&items);
            full.Add("coins", 900);
            while (full.FreeSlots() > 0) full.Add("bronze_sword", 1);
            const int coins_before = full.Coins();
            t = Trade::Buy(*forge, ledger, full, items, &log, "iron_ore", 1);
            Check(t.result == TradeResult::BagFull && full.Coins() == coins_before, "a full pack is not charged for ore it cannot hold");
            for (int i = 0; i < full.SlotCount(); ++i)
                if (full.Slot(i).id == "bronze_sword") { full.RemoveSlot(i, 1); break; }
            full.Add("iron_ore", 1);
            Check(full.FreeSlots() == 0, "the pack is full again, with an ore stack in it");
            t = Trade::Buy(*forge, ledger, full, items, &log, "iron_ore", 2);
            Check(t.result == TradeResult::Ok && full.Count("iron_ore") == 3, "and bought ore joins that stack");

            // Selling.
            Inventory seller(&items);
            seller.Add("copper_ore", 5);
            seller.Add("raw_trout", 2);
            seller.Add("bronze_sword", 1);
            const int ore_price = Trade::SellPrice(*forge, items, *items.Get("copper_ore"));
            t = Trade::Sell(*forge, seller, items, "copper_ore", 5);
            Check(t.result == TradeResult::Ok && seller.Count("copper_ore") == 0 && seller.Coins() == 5 * ore_price,
                  "selling five copper ore pays five times " + std::to_string(ore_price) + "c");
            t = Trade::Sell(*forge, seller, items, "raw_trout", 1);
            Check(t.result == TradeResult::WontBuy && seller.Count("raw_trout") == 2, "the forge turns down a trout and it stays in the pack");
            t = Trade::Sell(*shops.Get("fernhollow_tackle"), seller, items, "raw_trout", 2);
            Check(t.result == TradeResult::Ok && seller.Count("raw_trout") == 0, "Wendel buys it");
            t = Trade::Sell(*forge, seller, items, "iron_ore", 1);
            Check(t.result == TradeResult::NotHeld, "nothing sells that is not carried");
            const int sword = Trade::SellPrice(*forge, items, *items.Get("bronze_sword"));
            const int general = Trade::SellPrice(*shops.Get("havenbrook_general"), items, *items.Get("bronze_sword"));
            Check(sword > general, "the forge pays more for a sword than the general store (" +
                  std::to_string(sword) + "c against " + std::to_string(general) + "c)");
        }

        // Asking to trade opens the shop.
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            for (const auto& kv : shops.All()) {
                auto k = keepers.find(kv.second.keeper);
                if (k == keepers.end()) continue;
                DialogueRunner r;
                r.Begin(&dialogue, k->second.root, k->first, "Keeper", dc);
                r.TakeActions();
                int index = -1;
                for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                    if (r.VisibleOptions()[i]->action.open_shop == kv.first) index = static_cast<int>(i);
                Check(index >= 0, kv.second.keeper + " shows a new character the way into " + kv.first);
                if (index < 0) continue;
                r.MoveSelection(index - r.Selected());
                r.Choose(dc);
                bool opens = false;
                for (const DialogueAction& a : r.TakeActions()) if (a.open_shop == kv.first) opens = true;
                Check(opens && !r.Active(), "choosing it closes the conversation and opens " + kv.first);
            }
        }
    }

    Section("monsters and the new places");
    {
        // --- the monsters -----------------------------------------------------------------
        const char* kNew[] = {"rat", "spider", "broodmother", "lizardman", "lizardman_chief", "ice_troll",
                              "wyvern", "wyvern_matriarch", "imp", "demon", "pit_lord"};
        for (const char* id : kNew) {
            const EnemyDef* d = enemy_db.Get(id);
            Check(d != nullptr, string(id) + " is a monster");
            if (!d) continue;
            const SpriteDef* sd = sprites.Get(d->sprite);
            Check(sd != nullptr, string(id) + " has its sprite '" + d->sprite + "'");
            if (sd)
                for (const char* clip : {"idle", "walk", "attack", "hurt", "death"})
                    Check(sd->Find(clip) != nullptr, string(id) + " has a " + clip + " clip");
            Check(!d->loot_table.empty() && loot.Has(d->loot_table), string(id) + " drops from table '" + d->loot_table + "'");
        }
        for (const char* art : {"rat", "spider", "lizardman", "ice_troll", "wyvern", "demon", "imp"})
            for (const char* clip : {"idle", "walk", "attack", "hurt", "death"})
                Check(fs::exists(string("assets/characters/") + art + "/" + clip + ".png"),
                      string(art) + " " + clip + " sheet is drawn");

        // Where each is found, and at what level: every place is harder than the one before it.
        const auto effective = [&](const string& type, int level) {
            const EnemyDef* d = enemy_db.Get(type);
            return d ? d->attack_level + level - 1 : 0;
        };
        std::map<string, std::pair<int, int>> range;   // map -> strongest, weakest effective level
        std::map<string, std::set<string>> types;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const EnemySpawnDef& e : m.Enemies()) {
                types[id].insert(e.type);
                if (string(id) == "overworld" && e.x < (34 + 20) * 32 && e.type.rfind("lizardman", 0) == 0) types["mire"].insert(e.type);
                const int lv = effective(e.type, e.level);
                // The dragon is not part of the peak's spread: it is the thing
                // at the top of it, and it is checked on its own below.
                if (e.type == "frost_dragon") continue;
                auto& rg = range[id];
                rg.first = std::max(rg.first, lv);
                rg.second = rg.second == 0 ? lv : std::min(rg.second, lv);
                if (std::find_if(std::begin(kNew), std::end(kNew), [&](const char* n) { return e.type == n; }) != std::end(kNew))
                    Check(!m.Blocked({e.x - 6.0f, e.y - 6.0f, 12.0f, 6.0f}), string(id) + " " + e.type + " is not spawned inside a wall");
            }
        }
        int lizardmen = 0;
        {
            Map ow;
            ow.Load("maps/overworld.mx");
            for (const EnemySpawnDef& e : ow.Enemies()) if (e.type == "lizardman" || e.type == "lizardman_chief") ++lizardmen;
        }
        Check(lizardmen >= 10 && types["mire"].count("lizardman_chief"), "lizardmen and their chief hold the Mire (" + std::to_string(lizardmen) + ")");
        Check(types["house_inn_cellar"].count("rat") && types["house_inn_cellar"].count("spider") &&
              types["house_inn_cellar"].count("broodmother"), "rats, spiders and a broodmother are in the inn's cellar");
        Check(types["ice_spire_peak"].count("ice_troll") && types["ice_spire_peak"].count("wyvern") &&
              types["ice_spire_peak"].count("wyvern_matriarch"), "ice trolls and wyverns are on the Ice Spire");
        Check(types["ashen_path"].count("imp") && types["dungeon_infernal"].count("demon") &&
              types["dungeon_infernal"].count("pit_lord"), "imps on the Ashen Path, demons and the Pit Lord in the pit");
        Check(range["house_inn_cellar"].first <= 8, "the cellar is a beginner's fight (up to " + std::to_string(range["house_inn_cellar"].first) + ")");
        Check(range["ice_spire_peak"].second >= 24 && range["ice_spire_peak"].first <= 42,
              "the Ice Spire's monsters stand between 24 and 42 (" + std::to_string(range["ice_spire_peak"].second) + "-" +
              std::to_string(range["ice_spire_peak"].first) + ")");
        Check(range["dungeon_infernal"].second > range["ice_spire_peak"].second, "the pit is harder than the peak");

        // --- the dragon ----------------------------------------------------------------------
        {
            const EnemyDef* drake = enemy_db.Get("frost_dragon");
            Check(drake && drake->is_boss && drake->hp > 600, "Hoarfang is a boss with a boss's hit points");
            Check(drake && effective("frost_dragon", 1) > range["ice_spire_peak"].first,
                  "and stands above everything else on the Ice Spire");
            Check(types["ice_spire_peak"].count("frost_dragon"), "it holds the ground above the summit");
            int dragons = 0;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const EnemySpawnDef& e : m.Enemies()) if (e.type == "frost_dragon") ++dragons;
            }
            Check(dragons == 1, "there is one dragon in the world");
            for (const char* clip : {"idle", "walk", "attack", "hurt", "death"})
                Check(fs::exists(string("assets/characters/frost_dragon/") + clip + ".png"),
                      string("the dragon's ") + clip + " sheet is drawn");

            // The quest, and the old man who only speaks to someone who could
            // survive it.
            const QuestDef* hunt = quests.Definition("q_ice_spire_dragon");
            Check(hunt && hunt->major && hunt->giver == "npc_elder" && hunt->combat_level >= 35,
                  "the dragon hunt is a story quest from Elder Vask, closed below Combat 35");
            if (hunt) {
                Skills fresh, veteran;
                LevelUp lu;
                for (int sk : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_HITPOINTS})
                    veteran.AddXp(sk, XpForLevel(40), lu);
                QuestLog log;
                log.LoadDefinitions("data/quests.json");
                Check(!log.CanStart("q_ice_spire_dragon", fresh), "a new character cannot take it");
                Check(log.CanStart("q_ice_spire_dragon", veteran) && veteran.CombatLevel() >= 35,
                      "a Combat " + std::to_string(veteran.CombatLevel()) + " character can");
                Check(hunt->stages.size() == 3 && hunt->stages[1].type == ObjectiveType::Kill &&
                      hunt->stages[1].target == "frost_dragon" &&
                      hunt->stages[2].type == ObjectiveType::Deliver && hunt->stages[2].target == "dragon_fang",
                      "it is climb, kill, and carry the fang back");
            }
            {
                Map hall;
                Check(hall.Load("maps/guild_hall.mx"), "the guild hall loads");
                const NpcDef* vask = nullptr;
                for (const NpcDef& n : hall.Npcs()) if (n.id == "npc_elder") vask = &n;
                Check(vask && vask->name == "Elder Vask" && vask->dialogue == "elder_root",
                      "Elder Vask sits in the guild hall");
                // Art of his own: an old man in a rocking chair, not a townsfolk sheet.
                Check(vask && vask->sprite == "vask" && sprites.Get("vask"),
                      "and has art of his own rather than a townsfolk sheet");
                for (const char* clip : {"idle", "walk"})
                    Check(fs::exists(string("assets/characters/vask/") + clip + ".png"),
                          string("his ") + clip + " sheet is drawn");
                Check(vask && hall.Blocked({vask->x - 4.0f, vask->y - 6.0f, 8.0f, 6.0f}),
                      "and the floor his chair stands on is blocked");
            }
            {
                std::ifstream in("data/dialogue.json");
                json dj;
                in >> dj;
                bool gated = false, grunts = false;
                for (const auto& opt : dj["elder_root"].value("options", json::array())) {
                    const json& cond = opt.value("if", json::object());
                    if (opt.value("next", string("")) == "elder_offer" && cond.value("combat", 0) >= 35)
                        gated = true;
                    if (opt.value("next", string("")) == "elder_grunt" && cond.value("combat", 0) >= 35 &&
                        cond.value("not", false)) grunts = true;
                }
                Check(gated, "only a Combat 35 fighter is offered the hunt");
                Check(grunts && dj.contains("elder_grunt") &&
                      dj["elder_grunt"].value("text", string("")).find("*grunt*") != string::npos,
                      "anyone else gets a grunt");
            }
        }

        // --- the well under Havenbrook ----------------------------------------------------------
        // Two dark floors, four chambers to a floor, one kind of thing to a
        // chamber, and a lantern the only way to see any of it.
        {
            const ItemDef* lit = items.Get("lantern");
            const ItemDef* unlit = items.Get("lantern_unlit");
            const ItemDef* tinder = items.Get("tinderbox");
            Check(lit && lit->slot == SLOT_SHIELD && lit->light_radius > 150.0f,
                  "a lit lantern is worn in the off hand and throws a long light");
            Check(unlit && unlit->lights == "lantern" && unlit->needs_recipe,
                  "an unlit one becomes it when lit, and has to be shown to you first");
            Check(tinder && tinder->use == "light", "a tinderbox is what lights it");
            // Made at an anvil, out of a couple of bars.
            const ItemDef* recipe = nullptr;
            for (const ItemDef* r : items.Recipes())
                if (r->craft_result == "lantern_unlit") recipe = r;
            Check(recipe && items.StationFor(*recipe) == CraftStation::Anvil,
                  "the lantern is smithed at an anvil");
            int bars = 0;
            if (recipe)
                for (const auto& in : recipe->craft_inputs) {
                    const ItemDef* mat = items.Get(in.first);
                    if (mat && mat->piece == "bar") bars += in.second;
                }
            Check(bars >= 1 && bars <= 3, "out of a couple of ingots (" + std::to_string(bars) + ")");
            // Sold somewhere a new character can reach.
            bool sold = false;
            ShopDatabase well_shops;
            well_shops.Load("data/shops.json");
            for (const auto& kv : well_shops.All())
                for (const ShopStock& st : kv.second.sells)
                    if (st.item == "tinderbox") sold = true;
            Check(sold, "and a tinderbox can be bought");

            for (const char* id : {"well_shallow", "well_deep"}) {
                std::ifstream in(string("maps/") + id + ".mx");
                json mx;
                in >> mx;
                Check(mx["dreamquest"].value("dark", false), string(id) + " is pitch dark");
                Check(mx["dreamquest"]["bounds"][0].get<int>() >= 2900 &&
                      mx["dreamquest"]["bounds"][1].get<int>() >= 2200,
                      string(id) + " is a big floor (" + std::to_string(mx["dreamquest"]["bounds"][0].get<int>()) +
                      "x" + std::to_string(mx["dreamquest"]["bounds"][1].get<int>()) + ")");
            }

            // Four chambers of monsters on each floor: the quadrants hold what
            // they should, and nothing stands in the same room as its neighbour.
            const auto quadrants = [&](const string& id, std::map<string, std::set<string>>& out) {
                Map m;
                if (!m.Load("maps/" + id + ".mx")) return;
                for (const EnemySpawnDef& e : m.Enemies()) {
                    const string q = string(e.y < m.Height() / 2 ? "n" : "s") +
                                     (e.x < m.Width() / 2 ? "w" : "e");
                    out[q].insert(e.type);
                }
            };
            std::map<string, std::set<string>> shallow, deep;
            quadrants("well_shallow", shallow);
            quadrants("well_deep", deep);
            Check(shallow.size() == 4 && deep.size() == 4, "each floor has four quadrants with something in them");
            std::set<string> up_kinds, down_kinds;
            for (const auto& kv : shallow) up_kinds.insert(kv.second.begin(), kv.second.end());
            for (const auto& kv : deep) down_kinds.insert(kv.second.begin(), kv.second.end());
            for (const char* k : {"slime", "rat", "bat"})
                Check(up_kinds.count(k) > 0, string("the upper workings have ") + k + "s");
            for (const char* k : {"hound", "ankou", "banshee"})
                Check(down_kinds.count(k) > 0, string("the deep cut has ") + k + "s");
            Check(!up_kinds.count("ankou") && !down_kinds.count("slime"),
                  "and neither floor is holding the other's monsters");
            for (const char* k : {"slime", "bat", "hound", "ankou", "banshee", "well_warden"}) {
                const EnemyDef* d = enemy_db.Get(k);
                Check(d && d->aggro_range <= 175.0f, string(k) + " notices you late (" +
                      std::to_string(static_cast<int>(d ? d->aggro_range : 0)) + ")");
                Check(d && loot.Has(d->loot_table), string(k) + " has a loot table");
                for (const char* clip : {"idle", "walk", "attack", "hurt", "death"})
                    Check(fs::exists(string("assets/characters/") + (d ? d->sprite : "") + "/" + clip + ".png"),
                          string(k) + " has its " + clip + " drawn");
            }
            {
                Map deep_map;
                deep_map.Load("maps/well_deep.mx");
                int warden = 0;
                bool spring = false;
                for (const EnemySpawnDef& e : deep_map.Enemies()) if (e.type == "well_warden") ++warden;
                for (const MapObject& o : deep_map.Objects())
                    if (o.id == "spring_well" && o.type == "lever") spring = true;
                Check(warden == 1 && spring, "the spring is at the bottom, with the thing in it");
            }

            // The quest: the lantern first, then down, then the spring, then Bess.
            const QuestDef* q = quests.Definition("q_dry_well");
            Check(q && q->giver == "npc_cook" && q->major, "Bess gives the well quest");
            Check(q && q->stages.size() == 5 && q->stages[0].target == "lantern" &&
                  q->stages[1].target == "well_deep" && q->stages[2].target == "well_warden" &&
                  q->stages[3].target == "spring_well" && q->stages[4].target == "npc_cook",
                  "it is a lantern, a climb, a fight, the spring, and word back");
            // And the recipe comes with the work.
            {
                std::ifstream din("data/dialogue.json");
                json dj;
                din >> dj;
                bool teaches = false;
                for (auto it = dj.begin(); it != dj.end(); ++it)
                    for (const auto& opt : it.value().value("options", json::array())) {
                        const json& a = opt.value("action", json::object());
                        if (a.value("start_quest", string("")) == "q_dry_well" &&
                            a.value("learn", string("")) == "lantern_unlit") teaches = true;
                    }
                Check(teaches, "and taking it on is what teaches the lantern");
            }
            // The way down is in the town square.
            {
                Map town;
                town.Load("maps/town_havenbrook.mx");
                const Portal* down = nullptr;
                for (const Portal& p : town.Portals()) if (p.target_map == "well_shallow") down = &p;
                Check(down != nullptr, "the well in the square is the way in");
            }
        }

        // --- the world map ---------------------------------------------------------------------
        // What the map screen marks comes from data/worldmap.json, written by
        // genmaps as it places things, and the trades in a town come from the
        // shop database, so neither can drift from the world it describes.
        {
            ShopDatabase shop_db;
            shop_db.Load("data/shops.json");
            std::ifstream in("data/worldmap.json");
            Check(in.good(), "data/worldmap.json is written beside the maps");
            json wm;
            if (in.good()) in >> wm;
            Map ow;
            ow.Load("maps/overworld.mx");
            Check(wm.value("width", 0) == static_cast<int>(ow.Width()) &&
                  wm.value("height", 0) == static_cast<int>(ow.Height()),
                  "it is drawn to the size of the overworld");
            std::map<string, int> kinds;
            int off_map = 0;
            for (const auto& mk : wm.value("marks", json::array())) {
                ++kinds[mk.value("kind", string(""))];
                const float x = mk.value("x", 0.0f), y = mk.value("y", 0.0f);
                if (x < 0.0f || y < 0.0f || x > ow.Width() || y > ow.Height()) ++off_map;
                Check(!mk.value("label", string("")).empty(),
                      "every mark on the world map is named (" + mk.value("kind", string("")) + ")");
            }
            Check(off_map == 0, "and every one of them is somewhere on it");
            Check(kinds["dungeon"] >= 2, "the dungeons are marked (" + std::to_string(kinds["dungeon"]) + ")");
            Check(kinds["path"] >= 3, "so is every way out to another land (" + std::to_string(kinds["path"]) + ")");
            Check(kinds["town"] >= 1 && kinds["grave"] >= 1 && kinds["camp"] >= 1,
                  "and the town, the graveyard and the camp");
            // Each marked way out is a portal that is really there, and each
            // dungeon mark stands at a door into a dungeon.
            int matched_paths = 0, matched_dungeons = 0;
            for (const auto& mk : wm.value("marks", json::array())) {
                const string kind = mk.value("kind", string(""));
                if (kind != "path" && kind != "dungeon") continue;
                const float x = mk.value("x", 0.0f), y = mk.value("y", 0.0f);
                for (const Portal& p : ow.Portals()) {
                    const float px = p.rect.x + p.rect.w / 2.0f, py = p.rect.y + p.rect.h / 2.0f;
                    if (fabsf(px - x) > 120.0f || fabsf(py - y) > 120.0f) continue;
                    if (kind == "dungeon" && p.target_map.rfind("dungeon_", 0) == 0) ++matched_dungeons;
                    if (kind == "path" && p.target_map.rfind("dungeon_", 0) != 0 &&
                        p.target_map.rfind("town_", 0) != 0) ++matched_paths;
                }
            }
            Check(matched_dungeons >= 2, "each dungeon mark stands at a dungeon door");
            Check(matched_paths >= 3, "each path mark stands at a way out of the Hollowmarch");

            // A town's trades: every shop in the database that belongs to a
            // marked town has a glyph and a name in the legend.
            for (const auto& mk : wm.value("marks", json::array())) {
                const string town = mk.value("town", string(""));
                if (town.empty()) continue;
                int found = 0;
                for (const auto& kv : shop_db.All())
                    if (kv.second.town == town) {
                        ++found;
                        Check(string(WorldMapPanel::ShopGlyph(kv.second.type)) != "S" ||
                              kv.second.type == "shop",
                              town + "'s " + kv.second.type + " has an icon on the map");
                        Check(string(WorldMapPanel::ShopName(kv.second.type)) != "Trader" ||
                              kv.second.type == "shop",
                              "and a name for the legend");
                    }
                Check(found >= 3, town + " shows the trades it keeps (" + std::to_string(found) + ")");
            }
            for (const char* kind : {"town", "dungeon", "path", "grave", "camp", "landmark", "door", "trader", "craft"}) {
                Check(string(WorldMapPanel::KindGlyph(kind)).size() == 1, string(kind) + " has a glyph");
                Check(string(WorldMapPanel::KindName(kind)) != "", string(kind) + " has a legend line");
            }

            // --- a page for wherever you are ---------------------------------------------------
            // The map screen opens on the map of the place the player is in. What
            // is on a page is read off the map itself; which page a room belongs
            // to, and which road leads where, come from the list genmaps writes.
            WorldMapPanel pages;
            pages.Load("data/worldmap.json", shop_db);
            bool listed = true;
            string unlisted;
            for (const char* id : kMaps) {
                const auto it = pages.Areas().find(id);
                const bool ok = it != pages.Areas().end() && !it->second.name.empty() &&
                                (it->second.kind == "land" || it->second.kind == "dungeon" || it->second.kind == "interior");
                if (!ok) { listed = false; unlisted = id; }
            }
            Check(listed, "every map says what it is called and whether it is country, a dungeon or a room" +
                  (listed ? string() : ": " + unlisted));
            string door;
            Check(pages.PageFor("fernhollow", &door) == "fernhollow" && door.empty() &&
                  pages.PageFor("ashen_path") == "ashen_path" && pages.PageFor("westwold") == "westwold" &&
                  pages.PageFor("dungeon_emberfell_2") == "dungeon_emberfell_2" && pages.PageFor("well_deep") == "well_deep",
                  "open country and dungeons each have a page of their own");
            Check(pages.PageFor("guild_hall", &door) == "town_havenbrook" && door == "guild_hall",
                  "a room's page is the place the room is in, with the dot on its door");
            Check(pages.PageFor("house_inn_upper", &door) == "town_havenbrook" && door == "house_inn",
                  "and upstairs at the inn is still the inn's door");
            Check(pages.PageFor("mossvale_herbalist", &door) == "mossvale" && pages.PageFor("mossvale_weavers") == "mossvale" &&
                  pages.PageFor("fernhollow_cottage") == "fernhollow",
                  "in Mossvale and Fernhollow as in Havenbrook");
            // The college is a place of its own: its court has a page, and its rooms are rooms of the court.
            Check(pages.PageFor("college_grounds") == "college_grounds" && pages.PageFor("fernhollow_college", &door) == "college_grounds" &&
                  door == "fernhollow_college" && pages.PageFor("college_training") == "college_grounds" &&
                  pages.PageFor("college_classroom") == "college_grounds",
                  "and the college's court is a page, with its three chambers on it");
            Check(!pages.HasOverview("overworld") && pages.HasOverview("fernhollow") && pages.HasOverview("house_smith"),
                  "the Hollowmarch is the other side of every page but its own");
            Check(pages.WayFrom("fernhollow") == "whisperwood_trail" && pages.WayFrom("mossvale") == "whisperwood_trail" &&
                  pages.WayFrom("brackenwood") == "town_havenbrook" && pages.WayFrom("dungeon_infernal") == "ashen_path" &&
                  pages.WayFrom("well_deep") == "town_havenbrook",
                  "from the Hollowmarch, the way to anywhere is the road that starts towards it");
            Check(pages.WayFrom("dreamworld").empty(), "and no road leads to the Reverie");

            const auto count = [](const vector<WorldMark>& marks, const string& kind) {
                int n = 0;
                for (const WorldMark& m : marks) n += m.kind == kind ? 1 : 0;
                return n;
            };
            bool inside = true;
            string outside;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const WorldMark& mk : pages.MarksOf(m))
                    if (mk.label.empty() || mk.x < 0.0f || mk.y < 0.0f || mk.x > m.Width() || mk.y > m.Height()) {
                        inside = false;
                        outside = string(id) + ": " + mk.label;
                    }
            }
            Check(inside, "everything marked on a page has a name and is on the page" + (inside ? string() : ": " + outside));
            {
                Map m;
                m.Load("maps/fernhollow.mx");
                const vector<WorldMark> marks = pages.MarksOf(m);
                bool fishmonger = false;
                for (const WorldMark& mk : marks)
                    fishmonger |= mk.kind == "trader" && !mk.shops.empty() && mk.shops[0] == "fishmonger";
                // The cottage is a door. The college was one too, when it was a room in
                // the hamlet; it is a place now, and its gatehouse is a way to it.
                Check(count(marks, "trader") == 2 && fishmonger && count(marks, "door") + count(marks, "path") == 3 &&
                      count(marks, "door") >= 1 && count(marks, "path") >= 1,
                      "Fernhollow's page: two traders by their trades, the cottage's door, the college's gate, and the way back to the trail (" +
                          std::to_string(count(marks, "door")) + " doors, " + std::to_string(count(marks, "path")) + " ways)");
            }
            {
                Map m;
                m.Load("maps/town_havenbrook.mx");
                const vector<WorldMark> marks = pages.MarksOf(m);
                bool west = false;
                for (const WorldMark& mk : marks)
                    west |= mk.kind == "path" && mk.label.find("Westwold") != string::npos && mk.label.find("Combat 5") != string::npos;
                Check(count(marks, "door") == 4 && count(marks, "dungeon") == 1 && count(marks, "path") == 2 && west &&
                      count(marks, "craft") >= 2 && count(marks, "trader") >= 2,
                      "Havenbrook's: four doors, the well, both gates -- the west one with its warning -- the benches and the traders");
            }
            {
                Map m;
                m.Load("maps/ashen_path.mx");
                const vector<WorldMark> marks = pages.MarksOf(m);
                Check(count(marks, "dungeon") == 1 && count(marks, "path") == 1,
                      "the Ashen Path's: the way back, and the pit at the end of it");
            }
        }

        // --- Hollowrest and its dead -----------------------------------------------------------
        // A graveyard is its own ground with its own fence and its own dead,
        // and each kind of dead leaves a different kind of thing: a zombie its
        // flesh and its pockets, a skeleton nothing but bones, a wraith the
        // oddments it was buried in.
        {
            std::ifstream in("maps/overworld.mx");
            json mx;
            in >> mx;
            int grave_ground = 0;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it)
                if (it.key().rfind("grave_grass", 0) == 0 || it.key().rfind("grave_earth", 0) == 0)
                    grave_ground += static_cast<int>(it.value()["locations"].size());
            Check(grave_ground > 100, "Hollowrest is ground of its own, not a patch of meadow (" +
                  std::to_string(grave_ground) + " cells)");
            for (const char* prop : {"grave_fence", "lych_gate", "crypt", "gravestone", "gravestone_cross",
                                     "grave_mound"})
                Check(mx["tiles"].contains(prop), string("the graveyard has its ") + prop);
            int stones = 0;
            for (const char* prop : {"gravestone", "gravestone_cross"})
                if (mx["tiles"].contains(prop)) stones += static_cast<int>(mx["tiles"][prop]["locations"].size());
            Check(stones >= 80, "with a field full of markers (" + std::to_string(stones) + ")");

            Map ow;
            Check(ow.Load("maps/overworld.mx"), "the overworld loads for its graveyard");
            std::map<string, int> dead;
            int outside = 0;
            for (const EnemySpawnDef& e : ow.Enemies()) {
                if (e.type != "zombie" && e.type != "skeleton" && e.type != "wraith" &&
                    e.type != "barrow_wight") continue;
                // What walks the Mire and the greenwood after dark is another
                // matter, and is meant to: see "what comes out at night".
                if (e.night) continue;
                ++dead[e.type];
                // Every one of them stands inside the fence: the graveyard is
                // an ellipse about (40, 99) in cells, and the map is shifted
                // twenty cells east of its old origin.
                const float cx = (e.x - 20.0f * 32.0f) / 32.0f, cy = e.y / 32.0f;
                const float dx = (cx - 38.0f) / 18.0f, dy = (cy - 110.0f) / 11.0f;
                if (dx * dx + dy * dy > 1.6f) ++outside;
            }
            Check(dead["zombie"] >= 4 && dead["skeleton"] >= 5 && dead["wraith"] >= 4,
                  "zombies, skeletons and wraiths walk in it (" + std::to_string(dead["zombie"]) + ", " +
                  std::to_string(dead["skeleton"]) + ", " + std::to_string(dead["wraith"]) + ")");
            Check(dead["barrow_wight"] == 1, "and the wight holds the crypt");
            Check(outside == 0, "none of the dead has wandered outside the fence by day");
            for (const char* id : {"zombie", "skeleton", "wraith", "barrow_wight"}) {
                const EnemyDef* d = enemy_db.Get(id);
                Check(d && loot.Has(d->loot_table), string(id) + " drops from a table of its own");
                for (const char* clip : {"idle", "walk", "attack", "hurt", "death"})
                    Check(fs::exists(string("assets/characters/") + (d ? d->sprite : "") + "/" + clip + ".png"),
                          string(id) + " has its " + clip + " drawn");
            }

            // What each of them leaves, read straight out of the tables.
            std::ifstream lin("data/loot_tables.json");
            json tables;
            lin >> tables;
            const auto drops = [&](const char* table) {
                std::set<string> out;
                for (const auto& a : tables[table].value("always", json::array()))
                    out.insert(a.value("item", string("")));
                for (const auto& row : tables[table].value("table", json::array()))
                    out.insert(row.value("item", string("")));
                out.erase("nothing");
                return out;
            };
            const std::set<string> zom = drops("zombie"), skel = drops("skeleton"), wr = drops("wraith");
            Check(zom.count("rotten_flesh") && zom.count("coins") && zom.size() == 2,
                  "a zombie leaves its flesh and its money, and nothing else");
            Check(skel.size() == 1 && skel.count("bones"), "a skeleton leaves bones, and nothing else");
            Check(!wr.count("coins") && !wr.count("rotten_flesh") && wr.size() >= 3,
                  "a wraith leaves oddments rather than meat or money");
            for (const string& id : wr) {
                const ItemDef* d = items.Get(id);
                Check(d && d->value >= 10 && d->slot == SLOT_NONE,
                      "a wraith's " + id + " is worth carrying to a trader (" +
                      std::to_string(d ? d->value : 0) + ")");
            }
            // The graveyard's own chest, inside the fence.
            bool chest = false;
            for (const MapObject& o : ow.Objects())
                if (o.id == "chest_hollowrest" && o.loot_table == "chest_hollowrest") chest = true;
            Check(chest && loot.Has("chest_hollowrest"), "there is a chest in the yard worth opening");
        }

        // --- the drowned king's boots --------------------------------------------------------
        // One item, one chest, one quest: the boots are in no loot table, no
        // shop and no reward list, and the chest that holds them is only in
        // the world while the quest that sends you for it is running.
        {
            const ItemDef* boots = items.Get("drowned_king_boots");
            Check(boots && boots->slot == SLOT_FEET && boots->passive == "marshstride" &&
                  !boots->passive_text.empty(),
                  "the Boots of the Drowned King are worn on the feet and carry a passive");
            Check(boots && boots->defence_bonus > 20 && boots->requirements.count(SkillFromName("Defence")),
                  "they are worth wearing, and ask for the Defence to wear them");

            // Nowhere in any table, on any shelf, or in any quest's rewards.
            int in_tables = 0;
            for (const char* file : {"data/loot_tables.json", "data/loot_tables_armour.json"}) {
                std::ifstream in(file);
                if (!in) continue;
                json root;
                in >> root;
                const string text = root.dump();
                if (text.find("drowned_king_boots") != string::npos) ++in_tables;
            }
            Check(in_tables == 0, "no loot table can drop them");
            {
                std::ifstream in("data/shops.json");
                json shops;
                in >> shops;
                Check(shops.dump().find("drowned_king_boots") == string::npos, "no trader sells them");
            }
            int as_reward = 0;
            for (const auto& kv : quests.Definitions())
                for (const auto& it : kv.second.rewards.items)
                    if (it.first == "drowned_king_boots") ++as_reward;
            Check(as_reward == 0, "and no quest hands them over as a reward");

            // The chest: in the barrow, holding them by name, gated on the quest.
            Map barrow;
            Check(barrow.Load("maps/dungeon_barrow.mx"), "the barrow loads for its hoard");
            const MapObject* hoard = nullptr;
            for (const MapObject& o : barrow.Objects()) if (o.id == "chest_barrow_hoard") hoard = &o;
            Check(hoard && hoard->loot_item == "drowned_king_boots" && hoard->loot_table.empty(),
                  "the drowned king's chest holds the boots themselves, not a table roll");
            Check(hoard && hoard->needs_quest == "q_drowned_hoard",
                  "and stands in the world only while the quest is being done");
            int gated_chests = 0;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const MapObject& o : m.Objects())
                    if (o.loot_item == "drowned_king_boots") ++gated_chests;
            }
            Check(gated_chests == 1, "and it is the only place in the world they are");

            // The quest itself: Orlend, after the barrow, opening that chest.
            const QuestDef* hunt = quests.Definition("q_drowned_hoard");
            Check(hunt && hunt->giver == "npc_guildmaster" && hunt->major, "Orlend gives the hunt for it");
            Check(hunt && hunt->prerequisites.size() == 1 && hunt->prerequisites[0] == "q_barrow_seal",
                  "and only once the barrow has already been opened");
            Check(hunt && hunt->stages.size() == 3 && hunt->stages[1].type == ObjectiveType::Interact &&
                  hunt->stages[1].target == "chest_barrow_hoard",
                  "its middle stage is opening that chest");
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            LevelUp lu;
            for (int s2 : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_HITPOINTS})
                sk.AddXp(s2, XpForLevel(20), lu);
            Check(!log.CanStart("q_drowned_hoard", sk), "it cannot be taken before the barrow quest is done");
        }

        // --- the journal's two tabs ----------------------------------------------------------
        // The story is one line; the board, the orders and the favours are not
        // part of it, or the main quest would be buried under errands.
        {
            int major = 0, side = 0;
            for (const auto& kv : quests.Definitions()) {
                const QuestDef& d = kv.second;
                if (d.major) {
                    ++major;
                    Check(d.source != QuestSource::Board && !d.daily,
                          kv.first + " is a story quest, so it is not off a board and not repeatable");
                } else {
                    ++side;
                }
            }
            Check(major >= 8 && side > major, "the journal has a story line (" + std::to_string(major) +
                  ") and a side list (" + std::to_string(side) + ")");
            for (const char* id : {"q_marens_letter", "q_the_sunken_road", "q_emberfell_depths",
                                   "q_barrow_seal", "q_ice_spire_dragon"}) {
                const QuestDef* d = quests.Definition(id);
                Check(d && d->major, string(id) + " is on the story tab");
            }
            for (const char* id : {"q_thin_the_herd", "q_daily_boar", "q_order_iron_ore"}) {
                const QuestDef* d = quests.Definition(id);
                Check(d && !d->major && !d->tutorial, string(id) + " is a side quest");
            }
            // And a third tab for the trades, named for what each one teaches.
            const std::pair<const char*, const char*> taught[] = {
                {"q_learn_woodcutting", "Woodcutting"}, {"q_learn_mining", "Mining"},
                {"q_learn_fishing", "Fishing"}};
            for (const auto& t : taught) {
                const QuestDef* d = quests.Definition(t.first);
                Check(d && d->tutorial && !d->major, string(t.first) + " is on the tutorial tab");
                Check(d && d->name.rfind(t.second, 0) == 0,
                      string(t.first) + " is named for the skill it teaches (" + (d ? d->name : "") + ")");
            }
            for (const auto& kv : quests.Definitions())
                if (kv.second.source == QuestSource::Board || kv.second.daily)
                    Check(!kv.second.major && !kv.second.tutorial,
                          kv.first + " stays out of the story and tutorial tabs");
        }

        // --- the swamp ----------------------------------------------------------------------
        {
            std::ifstream in("maps/overworld.mx");
            json mx;
            in >> mx;
            Check(!mx["tiles"].contains("marsh_dark") && !mx["tiles"].contains("marsh_stone"),
                  "the black cliff tile cut from the cursed-land pack is gone from the Mire");
            int bog = 0, sedge = 0;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it) {
                if (it.key().rfind("bog_water", 0) == 0) bog += static_cast<int>(it.value()["locations"].size());
                if (it.key().rfind("swamp_", 0) == 0 || it.key().rfind("peat", 0) == 0) sedge += static_cast<int>(it.value()["locations"].size());
            }
            Check(bog > 40 && sedge > 400, "the Mire is sedge, peat and mud with pools of bog water (" + std::to_string(bog) +
                  " pool cells)");
            for (const char* prop : {"reeds", "lily_pads", "swamp_tree", "lizard_hut", "lizard_totem"})
                Check(mx["tiles"].contains(prop), string("the swamp has ") + prop);

            // The Hollowmarch grew twenty cells west and twelve south.
            const json& dq = mx["dreamquest"];
            Check(dq["bounds"][0] == 4736 && dq["bounds"][1] == 3968, "the Hollowmarch is 4736 by 3968 now");
            Check(dq["elevation"]["cols"].get<int>() * dq["elevation"]["cell"].get<int>() == 4736 &&
                  dq["elevation"]["rows"].get<int>() * dq["elevation"]["cell"].get<int>() == 3968,
                  "its height grid covers the whole of it");
            int west_swamp = 0, south_ground = 0;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it)
                for (const auto& l : it.value()["locations"]) {
                    if (l[0].get<int>() < 640 && (it.key().rfind("swamp_", 0) == 0 || it.key().rfind("peat", 0) == 0 || it.key().rfind("bog_water", 0) == 0)) ++west_swamp;
                    if (l[1].get<int>() > 3072 && l[2].get<int>() == 32) ++south_ground;
                }
            Check(west_swamp > 300, "the Mire carries on into the new land to the west (" + std::to_string(west_swamp) + " cells)");
            Check(south_ground > 1500, "and the land carries on south (" + std::to_string(south_ground) + " cells)");
            int west_lizards = 0;
            for (const auto& e : dq["enemies"]) if (e["x"].get<float>() < 640.0f && e["type"] == "lizardman") ++west_lizards;
            Check(west_lizards >= 2, "lizardmen range into the new western Mire");
            for (auto it = dq["spawns"].begin(); it != dq["spawns"].end(); ++it)
                Check(it.value()[0].get<int>() > 0 && it.value()[0].get<int>() < 4736 && it.value()[1].get<int>() > 0 && it.value()[1].get<int>() < 3968,
                      "the overworld's " + it.key() + " arrival is on the map");

            // The ground decals are things lying on the ground, not the road
            // pack's blending stencils.
            bool stencils = false;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it)
                if (it.key().find("patch_") != string::npos) stencils = true;
            Check(!stencils, "none of the road pack's grass stencils are scattered on the overworld");
            for (const char* d : {"~tuft_", "~flowers_", "~leaves_", "~pebbles_", "~dry_tuft_", "~crack_", "~sedge_"}) {
                bool any = false;
                for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it) if (it.key().rfind(d, 0) == 0) any = true;
                Check(any, string("the overworld has ") + (d + 1) + "* lying on its ground");
            }

            // The barrow is a mound with a doorway in it, the portal in the door.
            Check(mx["tiles"].contains("barrow_mound") && !mx["tiles"].contains("door") && !mx["tiles"].contains("door_open"),
                  "the barrow is entered through a grave mound, not a door sprite on the grass");
            if (mx["tiles"].contains("barrow_mound")) {
                const auto& l = mx["tiles"]["barrow_mound"]["locations"][0];
                for (const auto& p : dq["portals"])
                    if (p["target"] == "dungeon_barrow") {
                        const int px = p["rect"][0].get<int>() + p["rect"][2].get<int>() / 2;
                        const int py = p["rect"][1].get<int>() + p["rect"][3].get<int>();
                        const int bottom = l[1].get<int>() + l[3].get<int>() / 2;
                        Check(abs(px - l[0].get<int>()) <= 4 && bottom - py >= 30 && bottom - py <= 60,
                              "the barrow's portal is in the mound's doorway");
                    }
            }
        }
        // Inside, the way out is a stone flight up and the way down a stairwell.
        for (const char* id : {"dungeon_emberfell_1", "dungeon_emberfell_2", "dungeon_barrow", "dungeon_infernal"}) {
            std::ifstream in(string("maps/") + id + ".mx");
            json mx;
            in >> mx;
            Check(mx["tiles"].contains("dungeon_stairs_up") && !mx["tiles"].contains("door") && !mx["tiles"].contains("door_open"),
                  string(id) + " is left by a flight of stone stairs, not a door in the middle of a room");
            bool deeper = false;
            for (const auto& p : mx["dreamquest"]["portals"]) if (p["label"] == "Descend") deeper = true;
            if (deeper) Check(mx["tiles"].contains("dungeon_stairs_down"), string(id) + " goes deeper down a stairwell");
        }

        // --- the ways in ----------------------------------------------------------------------
        // Copies, so they outlive the map they were read from.
        std::map<string, Portal> found;
        const auto portal_to = [&](const string& from, const string& to) -> const Portal* {
            Map m;
            m.Load("maps/" + from + ".mx");
            for (const Portal& p : m.Portals())
                if (p.target_map == to) { found[from + ">" + to] = p; return &found[from + ">" + to]; }
            return nullptr;
        };
        {
            const Portal* up = portal_to("overworld", "ice_spire_peak");
            Check(up && up->min_combat >= 30, "the way to the Ice Spire is closed below Combat 30");
            const Portal* east = portal_to("overworld", "ashen_path");
            Check(east && east->min_combat >= 40, "the Ashen Path is closed below Combat 40");
            const Portal* gate = portal_to("ashen_path", "dungeon_infernal");
            Check(gate && gate->min_combat >= 40 && gate->requires_interact, "the pit is entered through the hellgate at the Ashen Path's end");
            Check(portal_to("house_inn", "house_inn_cellar") != nullptr && portal_to("house_inn_cellar", "house_inn") != nullptr,
                  "the inn has a hatch down to its cellar, and steps back up");
            Check(portal_to("dungeon_infernal", "ashen_path") != nullptr, "the pit leads back out to the Ashen Path");
        }

        Input input;
        std::mt19937 rng(66);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };

        // A gated edge turns a beginner back and lets a veteran through.
        {
            World w;
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            w.LoadMap("overworld", "from_peak", ctx);
            w.enemies.clear();
            frames(w, 90);
            const Portal* up = nullptr;
            for (const Portal& p : w.CurrentMap().Portals()) if (p.target_map == "ice_spire_peak") up = &p;
            if (up) {
                w.player.x = up->rect.x + up->rect.w / 2.0f;
                w.player.y = up->rect.y + up->rect.h - 2.0f;
                frames(w, 120);
                Check(w.MapId() == "overworld", "a new character walking into the Ice Spire's path stays in the Hollowmarch");
                World v;
                v.player = Player();
                v.player.Init(ctx, "player_hero");
                LevelUp lu;
                for (int sk : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_HITPOINTS})
                    v.player.skills.AddXp(sk, XpForLevel(40), lu);
                v.LoadMap("overworld", "from_peak", ctx);
                v.enemies.clear();
                frames(v, 90);
                v.player.x = up->rect.x + up->rect.w / 2.0f;
                v.player.y = up->rect.y + up->rect.h - 2.0f;
                frames(v, 240);
                Check(v.MapId() == "ice_spire_peak", "a Combat " + std::to_string(v.player.skills.CombatLevel()) +
                      " character walks on up to the Ice Spire (" + v.MapId() + ")");
            }
        }

        // Lava burns.
        {
            World w;
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            LevelUp lu;
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(40), lu);
            w.player.Rest();
            Check(w.LoadMap("dungeon_infernal", "", ctx), "the Infernal Pit loads");
            w.enemies.clear();
            const auto& hz = w.CurrentMap().Hazards();
            Check(hz.size() >= 12, "the pit's floors have lava vents in them (" + std::to_string(hz.size()) + ")");
            if (!hz.empty()) {
                const int before = w.player.hp;
                w.player.x = hz[0].rect.x + hz[0].rect.w / 2.0f;
                w.player.y = hz[0].rect.y + hz[0].rect.h / 2.0f + 4.0f;
                frames(w, 70);
                Check(w.player.hp < before, "standing in a vent burns (" + std::to_string(before) + " to " + std::to_string(w.player.hp) + ")");
                const int burnt = w.player.hp;
                w.player.x = hz[0].rect.x - 80.0f;
                bool moved = !w.CurrentMap().Blocked(w.player.Bounds());
                if (moved) {
                    frames(w, 70);
                    Check(w.player.hp >= burnt, "and stepping off it stops the burning");
                }
            }
            Map path;
            path.Load("maps/ashen_path.mx");
            Check(path.Hazards().size() >= 6, "the Ashen Path's lava fords burn to cross");
        }

        // --- the cellar quest, played through the world --------------------------------------
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Check(log.CanStart("q_cellar_vermin", sk), "a new character can take the cellar quest from Bess");
            log.Start("q_cellar_vermin");
            ctx.quests = &log;
            World w;
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            LevelUp lu;
            for (int s2 : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_HITPOINTS})
                w.player.skills.AddXp(s2, XpForLevel(30), lu);
            w.player.Rest();
            Check(w.LoadMap("house_inn_cellar", "from_inn", ctx), "the cellar loads");
            // Kill everything down there the way the world does it: through the quest events it raises.
            int rats = 0, spiders = 0, brood = 0;
            for (auto& e : w.enemies) {
                const string t = e->Def() ? e->Def()->kill_target : "";
                QuestEvent k;
                k.type = ObjectiveType::Kill;
                k.target = t;
                k.map_id = "house_inn_cellar";
                log.Notify(k, w.player.inventory);
                rats += t == "rat"; spiders += t == "spider"; brood += t == "broodmother";
            }
            Check(rats >= 6 && spiders >= 4 && brood == 1, "the cellar holds enough to finish it (" + std::to_string(rats) +
                  " rats, " + std::to_string(spiders) + " spiders)");
            // Rats counted before spiders are due do not count twice; kill the rest in order.
            for (int round = 0; round < 3; ++round)
                for (auto& e : w.enemies) {
                    QuestEvent k;
                    k.type = ObjectiveType::Kill;
                    k.target = e->Def() ? e->Def()->kill_target : "";
                    k.map_id = "house_inn_cellar";
                    log.Notify(k, w.player.inventory);
                }
            Check(log.IsActive("q_cellar_vermin") && log.Stage("q_cellar_vermin") == 3, "clearing the cellar leaves Bess to tell");
            QuestEvent rat_elsewhere;
            rat_elsewhere.type = ObjectiveType::Kill;
            rat_elsewhere.target = "rat";
            rat_elsewhere.map_id = "overworld";
            QuestLog fresh;
            fresh.LoadDefinitions("data/quests.json");
            fresh.Start("q_cellar_vermin");
            for (int i = 0; i < 10; ++i) fresh.Notify(rat_elsewhere, w.player.inventory);
            Check(fresh.Counter("q_cellar_vermin") == 0, "rats anywhere else do not count");
            QuestEvent talk;
            talk.type = ObjectiveType::Talk;
            talk.target = "npc_cook";
            log.Notify(talk, w.player.inventory);
            Check(log.IsComplete("q_cellar_vermin"), "and telling her finishes it");
            ctx.quests = &quests;
        }

        // --- the three trades taught in Havenbrook -------------------------------------------
        // A new character starts with no tools, so the town has somewhere to
        // learn each gathering skill and someone to lend the tool for it.
        {
            Map town;
            Check(town.Load("maps/town_havenbrook.mx"), "Havenbrook loads for its working camps");
            int oaks = 0, seams = 0, casts = 0;
            for (const MapObject& o : town.Objects()) {
                if (o.skill == "Woodcutting" && o.yield == "logs" && o.skill_level <= 1) ++oaks;
                if (o.skill == "Mining" && o.yield == "copper_ore" && o.skill_level <= 1) ++seams;
                if (o.skill == "Fishing" && o.skill_level <= 1) ++casts;
            }
            Check(oaks >= 8, "a stand of timber behind the sawpit, cut at Woodcutting 1 (" + std::to_string(oaks) + ")");
            Check(seams >= 5, "copper in the gravel pit's face, at Mining 1 (" + std::to_string(seams) + ")");
            Check(casts >= 2, "somewhere to cast on the mill pond (" + std::to_string(casts) + ")");
            bool water = false;
            {
                std::ifstream in("maps/town_havenbrook.mx");
                json mx;
                in >> mx;
                water = mx["tiles"].contains("water");
                for (const char* prop : {"sawmill", "ore_cart", "rowboat"})
                    Check(mx["tiles"].contains(prop), string("the camps have their ") + prop);
            }
            Check(water, "and the pond is water, not a patch of grass");

            struct Trade { const char* npc; const char* name; const char* quest; const char* tool;
                           const char* item; int count; const char* skill; };
            const Trade trades[] = {
                {"npc_sawyer",    "Sawyer Jessa",    "q_learn_woodcutting", "bronze_axe",     "logs",       10, "Woodcutting"},
                {"npc_pitmaster", "Pitmaster Dorn",  "q_learn_mining",      "bronze_pickaxe", "copper_ore",  8, "Mining"},
                {"npc_angler",    "Angler Sula",     "q_learn_fishing",     "fishing_rod",    "raw_minnow",  6, "Fishing"},
            };
            std::ifstream din("data/dialogue.json");
            json dj;
            din >> dj;
            for (const Trade& t : trades) {
                const NpcDef* who = nullptr;
                for (const NpcDef& n : town.Npcs()) if (n.id == t.npc) who = &n;
                Check(who && who->name == t.name, string(t.name) + " works in Havenbrook");

                const QuestDef* d = quests.Definition(t.quest);
                Check(d && d->giver == t.npc && d->recommended_level <= 1 && d->prerequisites.empty(),
                      string(t.quest) + " is given by " + t.npc + " and needs nothing first");
                Skills fresh_skills;
                QuestLog log;
                log.LoadDefinitions("data/quests.json");
                Check(log.CanStart(t.quest, fresh_skills), string("a brand new character can take ") + t.quest);
                if (!d) continue;
                Check(d->stages.size() == 2 && d->stages[0].type == ObjectiveType::Collect &&
                      d->stages[0].target == t.item && d->stages[0].count == t.count,
                      string(t.quest) + " asks for " + std::to_string(t.count) + " " + t.item);
                Check(d->stages[1].type == ObjectiveType::Deliver && d->stages[1].deliver_to == t.npc,
                      "and for them to be carried back to " + string(t.npc));

                // The lesson hands the tool over when the work is taken on.
                bool lends = false;
                for (auto it = dj.begin(); it != dj.end(); ++it)
                    for (const auto& opt : it.value().value("options", json::array())) {
                        if (!opt.contains("action")) continue;
                        const json& a = opt["action"];
                        if (a.value("start_quest", string("")) == t.quest && a.value("give", string("")) == t.tool)
                            lends = true;
                    }
                Check(lends, string(t.npc) + " lends a " + t.tool + " with the work");

                // And the tool lent is one a character of no level at all may use.
                Inventory bag;
                bag.SetDatabase(&items);
                bag.Add(t.tool, 1);
                Equipment worn;
                worn.SetDatabase(&items);
                const ItemDef* usable = Gathering::BestTool(bag, worn, items, fresh_skills,
                                                            Gathering::ToolFor(t.skill));
                Check(usable && usable->id == t.tool, string("a beginner can work with the ") + t.tool);

                // Played through: gather what was asked for, carry it back.
                log.Start(t.quest);
                Inventory carried;
                carried.SetDatabase(&items);
                for (int i = 0; i < t.count; ++i) {
                    carried.Add(t.item, 1);
                    log.RefreshCollectObjectives(carried);
                }
                Check(log.Stage(t.quest) == 1, string("gathering ") + std::to_string(t.count) + " " + t.item +
                      " sends you back to " + t.npc);
                // The line that hands them over: offered only with the whole
                // load in the bag, and it takes them.
                bool hands_in = false;
                for (auto it = dj.begin(); it != dj.end(); ++it)
                    for (const auto& opt : it.value().value("options", json::array())) {
                        if (!opt.contains("action") || !opt.contains("if")) continue;
                        if (opt["action"].value("take", string("")) != t.item) continue;
                        if (opt["action"].value("take_qty", 1) != t.count) continue;
                        if (opt["if"].value("has_item", string("")) == t.item &&
                            opt["if"].value("qty", 1) >= t.count) hands_in = true;
                    }
                Check(hands_in, string("the load is handed to ") + t.npc + " only once it is all carried");
                QuestEvent hand;
                hand.type      = ObjectiveType::Deliver;
                hand.target    = t.item;
                hand.secondary = t.npc;
                hand.amount    = t.count;
                carried.Remove(t.item, t.count);
                log.Notify(hand, carried);
                Check(log.IsComplete(t.quest), string("handing them over finishes ") + t.quest +
                      ", and the " + t.tool + " is kept");
            }
        }
    }

    Section("smithing, foraging and brewing");
    {
        // --- the skills -----------------------------------------------------------------------
        Check(string(SkillName(SKILL_SMITHING)) == "Smithing" && string(SkillName(SKILL_FORAGING)) == "Foraging" &&
              string(SkillName(SKILL_BREWING)) == "Brewing", "Smithing, Foraging and Brewing are skills");
        {
            // A save from before Smithing: bars and blades trained Crafting.
            Skills old;
            old.FromJson(json{{"xp", {{"Crafting", XpForLevel(34)}, {"Mining", XpForLevel(20)}}}});
            Check(old.Level(SKILL_SMITHING) == 34, "an old save's Smithing starts where its Crafting stood");
            Skills fresh;
            fresh.FromJson(json{{"xp", {{"Crafting", XpForLevel(34)}, {"Smithing", 0}}}});
            Check(fresh.Level(SKILL_SMITHING) == 1, "a new save's Smithing is its own");
        }

        // --- herbs ------------------------------------------------------------------------------
        vector<const ItemDef*> herbs;
        for (const auto& kv : items.All()) if (kv.second.forage_level > 0) herbs.push_back(&kv.second);
        Check(herbs.size() >= 8, "there are herbs to forage (" + std::to_string(herbs.size()) + ")");
        std::set<int> levels;
        for (const ItemDef* h : herbs) {
            levels.insert(h->forage_level);
            Check(!h->grows.empty() && h->forage_xp > 0, h->id + " says where it grows and what it is worth");
            const bool brewed = std::find(h->tags.begin(), h->tags.end(), "brewing") != h->tags.end();
            const bool woven = std::find(h->tags.begin(), h->tags.end(), "fibre") != h->tags.end();
            Check(brewed != woven, h->id + (woven ? " goes on a loom, and not in a brew" : " goes in a brew"));
            for (const char* art : {"assets/props/herb_%s.png", "assets/props/herb_%s_picked.png"}) {
                char path[128];
                SDL_snprintf(path, sizeof(path), art, h->id.c_str());
                Check(fs::exists(path), h->id + " is drawn in the world: " + path);
            }
        }
        Check(levels.size() == herbs.size(), "every herb opens at its own Foraging level");

        // Where they are, on every map.
        std::map<string, int> spawned;
        std::map<string, std::map<string, int>> by_map;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "herb") continue;
                const ItemDef* h = items.Get(o.yield);
                Check(h && h->forage_level > 0, string(id) + " " + o.id + " grows a real herb");
                if (!h) continue;
                Check(o.skill == "Foraging" && o.skill_level == h->forage_level && o.yield_xp == h->forage_xp,
                      string(id) + " " + o.id + " asks the herb's own level and pays its XP");
                Check(!o.sprite.empty() && !o.sprite_open.empty() && o.regrow_hours > 0.0f,
                      string(id) + " " + o.id + " is drawn growing and picked, and grows back");
                ++spawned[o.yield];
                ++by_map[id][o.yield];
            }
        }
        for (const ItemDef* h : herbs)
            Check(spawned[h->id] >= 12, h->id + " grows in at least a dozen places (" + std::to_string(spawned[h->id]) + ")");
        Check(by_map["dreamworld"]["starlily"] > 0 && by_map["dreamworld"]["moonpetal"] > 0 &&
              !by_map["overworld"]["starlily"] && !by_map["overworld"]["moonpetal"],
              "moonpetal and starlily grow only in the Reverie");
        Check(by_map["whisperwood_trail"]["glowcap"] > by_map["overworld"]["glowcap"] &&
              by_map["whisperwood_trail"]["glowcap"] >= 20, "the Whisperwood is where glowcaps grow");
        Check(by_map["mossvale"]["marigold"] > 0 && by_map["mossvale"]["brookmint"] > 0 && by_map["mossvale"]["nettle"] > 0,
              "Oona's garden grows the three beginner herbs");

        // On the overworld, each grows on its own ground, thickest in one patch.
        {
            std::ifstream in("maps/overworld.mx");
            json mx;
            in >> mx;
            std::map<std::pair<int, int>, vector<string>> ground;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it)
                for (const auto& loc : it.value()["locations"])
                    // Ground is laid in 32px tiles, and the Mire's dark mud and
                    // stones in quarters of one.
                    if (loc[2].get<int>() == loc[3].get<int>() && loc[2].get<int>() <= 32)
                        ground[{loc[0].get<int>() / 32, loc[1].get<int>() / 32}].push_back(it.key());
            // Whether any ground laid in a cell is of a family: decals are
            // drawn over the tiles, so a cell can hold several names.
            const auto has = [&](int cx, int cy, const char* prefix) {
                auto g = ground.find({cx, cy});
                if (g == ground.end()) return false;
                for (const string& n : g->second) if (n.rfind(prefix, 0) == 0) return true;
                return false;
            };
            const auto near_water = [&](int cx, int cy) {
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                        if (has(cx + dx, cy + dy, "water")) return true;
                return false;
            };
            Map ow;
            ow.Load("maps/overworld.mx");
            std::map<string, int> total, right;
            std::map<string, vector<std::pair<int, int>>> cells;
            for (const MapObject& o : ow.Objects()) {
                if (o.type != "herb") continue;
                const int cx = static_cast<int>(o.x) / 32, cy = static_cast<int>(o.y - 12) / 32;
                bool ok = false;
                if (o.yield == "marigold")      ok = has(cx, cy, "grass") || has(cx, cy, "moss");
                if (o.yield == "nettle")        ok = has(cx, cy, "grass") || has(cx, cy, "moss");
                if (o.yield == "bogbean")       ok = has(cx, cy, "swamp") || has(cx, cy, "peat");
                if (o.yield == "mountain_sage") ok = has(cx, cy, "dirt") || has(cx, cy, "sand");
                if (o.yield == "emberbloom")    ok = has(cx, cy, "cursed");
                if (o.yield == "brookmint")     ok = near_water(cx, cy);
                ++total[o.yield];
                right[o.yield] += ok;
                cells[o.yield].push_back({cx, cy});
            }
            for (const char* herb : {"marigold", "nettle", "bogbean", "mountain_sage", "emberbloom", "brookmint"}) {
                Check(total[herb] > 0 && right[herb] * 10 >= total[herb] * 9,
                      string(herb) + " grows on its own ground on the overworld (" + std::to_string(right[herb]) +
                      " of " + std::to_string(total[herb]) + ")");
                int best = 0;
                for (const auto& c : cells[herb]) {
                    int near = 0;
                    for (const auto& d : cells[herb])
                        if (abs(d.first - c.first) <= 3 && abs(d.second - c.second) <= 3) ++near;
                    best = std::max(best, near);
                }
                Check(best >= 6, string(herb) + " has a patch where it grows thick (" + std::to_string(best) +
                      " within a few steps)");
            }
            int west_bogbean = 0;
            for (const MapObject& o : ow.Objects())
                if (o.type == "herb" && o.yield == "bogbean") west_bogbean += (o.x < 2240.0f);
            Check(west_bogbean * 10 >= total["bogbean"] * 9, "bogbean keeps to the Mire in the west");
        }

        // --- picking one ----------------------------------------------------------------------
        Input input;
        std::mt19937 rng(55);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        const auto stand_by = [&](World& w, const string& map_id, const string& herb, int level) -> const MapObject* {
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            w.SetPickedHerbs({});
            w.clock.Set(2, 10.0f);
            if (!w.LoadMap(map_id, "", ctx)) return nullptr;
            w.enemies.clear();
            LevelUp lu;
            if (level > 1) w.player.skills.AddXp(SKILL_FORAGING, XpForLevel(level), lu);
            for (const MapObject& o : w.CurrentMap().Objects()) {
                if (o.type != "herb" || o.yield != herb) continue;
                for (float a = 1.57f; a < 1.57f + 6.28f; a += 0.3f)
                    for (float d = 14.0f; d <= 40.0f; d += 6.0f) {
                        const float px = o.x + cosf(a) * d, py = o.y + sinf(a) * d;
                        if (w.CurrentMap().Blocked({px - 8.0f, py - 10.0f, 16.0f, 10.0f})) continue;
                        w.player.x = px;
                        w.player.y = py;
                        frames(w, 2);
                        if (w.player.interact.kind == InteractTarget::Object &&
                            &w.CurrentMap().Objects()[w.player.interact.index] == &o) return &o;
                    }
            }
            return nullptr;
        };
        {
            World w;
            const MapObject* herb = stand_by(w, "mossvale", "marigold", 1);
            Check(herb != nullptr, "a new character can stand at a marigold in Oona's garden");
            if (herb) {
                Check(w.player.interact.label.rfind("Pick marigold", 0) == 0, "the prompt says Pick marigold (" +
                      w.player.interact.label + ")");
                w.TryInteract(ctx);
                frames(w, 1);
                Check(w.player.GatherClip() == "gather", "picking plays the gather animation, hands empty");
                int f = 0;
                while (w.player.inventory.Count("marigold") == 0 && f++ < 600) frames(w, 1);
                Check(w.player.inventory.Count("marigold") >= 1 && w.player.skills.Xp(SKILL_FORAGING) >= 10,
                      "and gives a marigold and Foraging XP, in " + std::to_string(f / 60.0f).substr(0, 4) + "s");
                Check(!w.Gathering() && w.Picked(*herb), "then it is picked, and the work stops");
                frames(w, 2);
                Check(!(w.player.interact.kind == InteractTarget::Object &&
                        &w.CurrentMap().Objects()[w.player.interact.index] == herb),
                      "a picked plant offers nothing");
                w.clock.Set(2, 10.0f + herb->regrow_hours * 0.5f);
                Check(w.Picked(*herb), "half way through its regrowth it is still bare");
                w.clock.Set(2, 10.0f + herb->regrow_hours + 0.1f);
                Check(!w.Picked(*herb), "and it grows back");
                // A save carries it.
                w.clock.Set(2, 10.0f);
                w.Pick(*herb);
                World back;
                back.SetPickedHerbs(w.PickedHerbs());
                back.clock.Set(2, 10.0f);
                back.LoadMap("mossvale", "", ctx);
                Check(back.Picked(back.CurrentMap().Objects()[herb - &w.CurrentMap().Objects()[0]]),
                      "what was picked is remembered across a save");
            }
        }
        {
            World w;
            const MapObject* cap = stand_by(w, "whisperwood_trail", "glowcap", 1);
            Check(cap != nullptr && w.player.interact.label.find("Needs Foraging 36") != string::npos,
                  "a glowcap tells a beginner it needs Foraging 36");
            if (cap) {
                w.TryInteract(ctx);
                frames(w, 120);
                Check(w.player.inventory.Count("glowcap") == 0 && !w.Picked(*cap), "and gives them nothing");
            }
            World w2;
            if (stand_by(w2, "whisperwood_trail", "glowcap", 99)) {
                int twos = 0;
                for (int k = 0; k < 40; ++k) {
                    const int before = w2.player.inventory.Count("glowcap");
                    w2.SetPickedHerbs({});
                    w2.TryInteract(ctx);
                    for (int f = 0; f < 400 && w2.player.inventory.Count("glowcap") == before; ++f) frames(w2, 1);
                    if (w2.player.inventory.Count("glowcap") - before == 2) ++twos;
                    frames(w2, 2);
                }
                Check(twos >= 10 && twos <= 30, "a master forager often picks two (" + std::to_string(twos) + " of 40)");
            }
        }
        Check(Gathering::ForageExtraChance(10, 10) == 0.0f && Gathering::ForageExtraChance(99, 1) == 0.5f,
              "two herbs from one plant: never at its level, at most half the time");

        // --- brewing ----------------------------------------------------------------------------
        const auto brews = items.Recipes(CraftStation::Cauldron);
        Check(brews.size() >= 8, "there are brews (" + std::to_string(brews.size()) + ")");
        std::set<string> scrolls, sold;
        for (const auto& kv : items.All()) if (!kv.second.learn.empty()) scrolls.insert(kv.second.learn);
        ShopDatabase shopdb;
        shopdb.Load("data/shops.json");
        for (const auto& kv : shopdb.All())
            for (const ShopStock& line : kv.second.sells)
                if (const ItemDef* d = items.Get(line.item)) if (!d->learn.empty()) sold.insert(d->learn);
        for (const auto& kv : shopdb.All())
            for (const ShopStock& line : kv.second.sells)
                if (line.item == "vial" && kv.second.General()) sold.insert("vial@" + kv.second.town);
        for (const char* town : {"havenbrook", "mossvale", "fernhollow", "whisperwood", "reverie"})
            Check(sold.count(string("vial@") + town), string("the general store in ") + town + " sells vials");
        int dyes = 0;
        for (const ItemDef* r : brews) {
            const ItemDef* potion = items.Get(r->craft_result);
            // A dye is boiled in the same pot and is nobody's secret: no scroll,
            // no teacher, nothing to drink. It is still brewed at the Foraging
            // level of its rarest herb, and into one vial.
            if (potion && potion->untaught) {
                ++dyes;
                int herb = 1;
                for (const auto& in : r->craft_inputs)
                    if (const ItemDef* mat = items.Get(in.first)) herb = std::max(herb, mat->forage_level);
                Check(!potion->consumable && r->craft_level == herb && r->craft_inputs.count("vial") &&
                      fs::exists(potion->icon),
                      potion->id + " is a dye: brewed at Brewing " + std::to_string(r->craft_level) + " with no teaching, and not for drinking");
                continue;
            }
            Check(potion && potion->consumable && !potion->icon.empty(), r->craft_result + " is a potion you can drink");
            if (!potion) continue;
            Check(potion->heal > 0 || potion->mana > 0 || potion->stamina || !potion->boosts.empty(),
                  potion->id + " does something");
            Check(r->craft_inputs.count("vial") && r->craft_inputs.at("vial") == 1, potion->id + " is brewed into one vial");
            int herb_level = 0;
            for (const auto& in : r->craft_inputs)
                if (const ItemDef* mat = items.Get(in.first)) herb_level = std::max(herb_level, mat->forage_level);
            Check(r->craft_level == herb_level, potion->id + " is brewed at Brewing " + std::to_string(r->craft_level) +
                  ", the Foraging level of its rarest herb");
            Check(!potion->recipe_from.empty(), potion->id + " says where its recipe is learned");
            if (potion->id != "healing_draught") {
                Check(scrolls.count(potion->id), potion->id + " has a recipe scroll");
                Check(sold.count(potion->id), "someone sells the recipe for " + potion->id);
            }
        }
        Check(dyes == 12, "there is a dye for every tier of robe (" + std::to_string(dyes) + ")");
        {
            // Oona teaches the first one, once.
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            DialogueRunner r;
            r.Begin(&dialogue, "oona_root", "npc_oona", "Oona", dc);
            int teach = -1;
            for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                if (r.VisibleOptions()[i]->next == "oona_brew_teach") teach = static_cast<int>(i);
            Check(teach >= 0, "Oona offers to teach a new character to brew");
            if (teach >= 0) {
                r.MoveSelection(teach - r.Selected());
                r.Choose(dc);
                bool learns = false;
                for (const auto& o : r.VisibleOptions()) if (o->action.learn_recipe == "healing_draught") learns = true;
                Check(learns, "and her lesson teaches the healing draught");
            }
            flags.insert("recipe:healing_draught");
            DialogueRunner again;
            again.Begin(&dialogue, "oona_root", "npc_oona", "Oona", dc);
            bool offers = false;
            for (const auto& o : again.VisibleOptions()) if (o->next == "oona_brew_teach") offers = true;
            Check(!offers, "and does not offer the lesson twice");
        }

        // --- drinking -----------------------------------------------------------------------------
        {
            World w;
            w.player = Player();
            w.player.Init(ctx, "player_hero");
            w.LoadMap("mossvale", "", ctx);
            w.enemies.clear();
            Player& p = w.player;
            LevelUp lu;
            p.skills.AddXp(SKILL_STRENGTH, XpForLevel(40), lu);
            p.skills.ResetCurrent();
            const auto slot = [&](const string& id) {
                for (int i = 0; i < p.inventory.SlotCount(); ++i) if (p.inventory.Slot(i).id == id) return i;
                return -1;
            };
            string why;
            p.inventory.Add("nettle_brew", 2);
            Check(p.Consume(slot("nettle_brew"), why) && p.skills.Current(SKILL_STRENGTH) == 40 + 3 + 4,
                  "a nettle brew lifts Strength 40 to 47");
            Check(!p.Consume(slot("nettle_brew"), why) && p.inventory.Count("nettle_brew") == 1 && !why.empty(),
                  "a second one does nothing while the first holds, and is not wasted");
            frames(w, static_cast<int>(Player::BOOST_DECAY * 60.0f) + 30);
            Check(p.skills.Current(SKILL_STRENGTH) == 46, "the boost wears off a point at a time");
            p.inventory.Add("healing_draught", 1);
            Check(!p.Consume(slot("healing_draught"), why), "a healing draught at full health is kept");
            p.hp = 2;
            p.skills.SetCurrent(SKILL_HITPOINTS, 2);
            Check(p.Consume(slot("healing_draught"), why) && p.hp > 2, "and heals when it is needed");
            p.inventory.Add("mana_tonic", 1);
            p.SyncMana();
            p.SpendMana(p.Mana());
            Check(p.Consume(slot("mana_tonic"), why) && p.Mana() > 0, "a mana tonic restores mana");
        }
    }

    // --- nodes that run out --------------------------------------------------------------
    Section("trees come down and seams give out");
    {
        Check(!Gathering::Depletes(0.0f, 0.0f) && Gathering::Depletes(0.125f, 0.10f) && !Gathering::Depletes(0.125f, 0.20f),
              "a node runs out on a roll under its chance, and never with no chance");

        // Every tree and seam on every map can run out, says how long for,
        // and a tree has a stump to be drawn as.
        int tree_count = 0, seam_count = 0, never = 0, greedy = 0;
        std::set<string> stumps;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if ((o.type != "tree" && o.type != "rock") || o.skill.empty()) continue;
                if (o.deplete <= 0.0f || o.regrow_hours <= 0.0f) { ++never; continue; }
                if (o.deplete > 0.5f) ++greedy;
                if (o.type == "tree") { ++tree_count; stumps.insert(o.sprite_open); }
                else ++seam_count;
            }
        }
        Check(tree_count >= 50 && seam_count >= 20 && never == 0,
              "every tree and seam can run out (" + std::to_string(tree_count) + " trees, " + std::to_string(seam_count) + " seams)");
        Check(greedy == 0, "and none of them on most strokes");
        Check(stumps.size() == 2 && !stumps.count(""), "a felled tree is drawn as a stump, one for each size of tree");
        for (const string& s : stumps) if (!s.empty()) Check(fs::exists(s), "the stump art exists: " + s);

        Input input;
        std::mt19937 rng(11);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
        };
        // Stands the player on clear ground beside the first node `pick`
        // likes, close enough that the prompt names it.
        const auto stand_by = [&](World& w, const std::function<bool(const MapObject&)>& pick) -> const MapObject* {
            const auto& objects = w.CurrentMap().Objects();
            for (size_t i = 0; i < objects.size(); ++i) {
                const MapObject& o = objects[i];
                if (!pick(o)) continue;
                for (float a = 1.57f; a < 1.57f + 6.28f; a += 0.3f)
                    for (float d = 14.0f; d <= 44.0f; d += 6.0f) {
                        const float px = o.x + cosf(a) * d, py = o.y + sinf(a) * d;
                        if (w.CurrentMap().Blocked({px - 8.0f, py - 10.0f, 16.0f, 10.0f})) continue;
                        w.player.x = px;
                        w.player.y = py;
                        frames(w, 2);
                        if (w.player.interact.kind == InteractTarget::Object &&
                            w.player.interact.index == static_cast<int>(i)) return &o;
                    }
            }
            return nullptr;
        };

        // Played through: an oak chopped until it comes down, then the count
        // of how long one stands.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the woodcutter");
            w.enemies.clear();
            w.clock.Set(1, 12.0f);
            LevelUp lu;
            w.player.skills.AddXp(SKILL_WOODCUTTING, XpForLevel(99), lu);
            w.player.inventory.Add("platinum_axe", 1);
            const MapObject* oak = stand_by(w, [](const MapObject& o) {
                return o.type == "tree" && o.skill_level <= 1 && o.deplete > 0.0f && o.title == "oak";
            });
            Check(oak != nullptr, "there is an oak to chop");
            if (oak) {
                const string key = "overworld:" + oak->id;
                int logs = 0, falls = 0, run = 0;
                bool stopped = true, silent = true, written = true;
                while (falls < 25 && run < 60 * 60 * 6) {
                    if (!w.Gathering() && !w.Spent(*oak)) w.TryInteract(ctx);
                    const int before = w.player.inventory.Count("logs");
                    frames(w, 1);
                    ++run;
                    logs += w.player.inventory.Count("logs") - before;
                    if (!w.Spent(*oak)) continue;
                    ++falls;
                    stopped &= !w.Gathering() && w.player.GatherClip().empty();
                    written &= w.PickedHerbs().count(key) > 0;
                    frames(w, 2);
                    silent &= !(w.player.interact.kind == InteractTarget::Object &&
                                &w.CurrentMap().Objects()[w.player.interact.index] == oak);
                    if (falls == 1) {
                        // Its time passes, and it is back.
                        const double now = w.GameHours();
                        w.clock.Set(1, static_cast<float>(now - 24.0 + oak->regrow_hours - 0.1));
                        Check(w.Spent(*oak), "a felled oak is still down just short of its time");
                        w.clock.Set(1, static_cast<float>(now - 24.0 + oak->regrow_hours + 0.1));
                        Check(!w.Spent(*oak), "and back once it has passed");
                    } else {
                        // Straight back, so the count does not wait on the clock.
                        w.SetPickedHerbs({});
                    }
                }
                Check(falls == 25, "an oak comes down (" + std::to_string(falls) + " times in " +
                      std::to_string(run / 60) + "s of chopping)");
                Check(stopped, "when it comes down the work stops");
                Check(silent, "and the stump offers nothing to chop");
                Check(written, "a felled tree is written down beside the picked herbs, so it is saved");
                Check(falls > 0 && logs / std::max(1, falls) >= 4 && logs / std::max(1, falls) <= 16,
                      "an oak gives about eight logs before it comes down (" +
                      std::to_string(logs) + " logs over " + std::to_string(falls) + " falls)");
            }
        }

        // And a copper outcrop, which gives out sooner and is drawn dull
        // rather than replaced.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            w.LoadMap("overworld", "start", ctx);
            w.enemies.clear();
            w.clock.Set(1, 12.0f);
            LevelUp lu;
            w.player.skills.AddXp(SKILL_MINING, XpForLevel(99), lu);
            w.player.inventory.Add("platinum_pickaxe", 1);
            const MapObject* rock = stand_by(w, [](const MapObject& o) {
                return o.type == "rock" && o.yield == "copper_ore" && o.skill_level <= 1 && o.deplete > 0.0f &&
                       o.title.find("outcrop") != string::npos;
            });
            Check(rock != nullptr, "there is a copper outcrop to mine");
            if (rock) {
                Check(rock->sprite_open.empty(), "a worked-out seam is the same rock drawn dull, not a picture of its own");
                Check(rock->deplete > 0.2f && rock->regrow_hours < 2.0f, "an outcrop gives out sooner than an oak and is back within the hour");
                int ores = 0, run = 0;
                w.TryInteract(ctx);
                while (!w.Spent(*rock) && run < 60 * 60 * 2) {
                    if (!w.Gathering()) w.TryInteract(ctx);
                    const int before = w.player.inventory.Count("copper_ore");
                    frames(w, 1);
                    ++run;
                    ores += w.player.inventory.Count("copper_ore") - before;
                }
                Check(w.Spent(*rock) && !w.Gathering(), "the outcrop gives out and the work stops (" +
                      std::to_string(ores) + " ore)");
            }
        }
        input.Update(1.0f / 60.0f);
    }

    // --- dropping things --------------------------------------------------------------------
    Section("dropping things from the bag");
    {
        Input input;
        {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = SDLK_G;
            input.HandleEvent(e);
            Check(input.Pressed(Action::Drop) && input.PromptFor(Action::Drop) == "G", "G is the drop key, and the bag says so");
            e.type = SDL_EVENT_KEY_UP;
            input.HandleEvent(e);
            input.Update(1.0f / 60.0f);
            Check(!input.Pressed(Action::Drop) && !input.Down(Action::Drop), "and lets go");
            SDL_Event b{};
            b.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            b.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            input.HandleEvent(b);
            Check(input.Pressed(Action::Drop) && input.Pressed(Action::StrongAttack), "on a pad, Y drops in the bag as well as swinging in a fight");
            b.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            input.HandleEvent(b);
            input.Update(1.0f / 60.0f);
        }

        // What cannot be dropped: handed over once, by someone who could not
        // hand it over again.
        bool kept = true;
        for (const char* id : {"mossvale_house_key", "rusted_key", "elder_letter", "barrow_seal", "torn_page", "warchief_totem"})
            kept &= items.Get(id) && items.Get(id)->keep;
        Check(kept, "keys, letters and seals cannot be dropped");
        Check(items.Get("logs") && !items.Get("logs")->keep && items.Get("iron_sword") && !items.Get("iron_sword")->keep &&
              items.Get("coins") && !items.Get("coins")->keep, "ordinary things can be");

        std::mt19937 rng(5);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
        };

        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the drop");
        w.enemies.clear();
        w.player.y -= 200.0f;
        const float px = w.player.x, py = w.player.y;

        // On the ground at the feet, and not straight back into the bag.
        w.player.inventory.Add("logs", 5);
        w.player.inventory.Remove("logs", 5);
        w.DropItem("logs", 5, px, py + 4.0f, ctx, true);
        frames(w, 90);
        Check(w.player.inventory.Count("logs") == 0 && w.pickups.size() == 1,
              "a dropped stack lies at the feet and is not scooped straight back up by the feet that dropped it");
        w.player.x = px + 70.0f;
        frames(w, 5);
        w.player.x = px;
        w.player.y = py;
        frames(w, 30);
        Check(w.player.inventory.Count("logs") == 5 && w.pickups.empty(), "step away and back, and it is picked up again");

        // It does not lie there for ever.
        w.player.inventory.Remove("logs", 5);
        w.DropItem("logs", 5, px, py + 4.0f, ctx, true);
        w.player.x = px + 70.0f;
        frames(w, static_cast<int>(World::DROP_LIFE * 60.0f) + 120);
        Check(w.pickups.empty() && w.player.inventory.Count("logs") == 0, "and is gone after three minutes");

        // What a monster leaves is not on a clock, and is taken at once.
        w.DropItem("bones", 1, px + 70.0f, py + 4.0f, ctx);
        frames(w, static_cast<int>(World::DROP_LIFE * 60.0f) + 120);
        Check(w.player.inventory.Count("bones") == 1 && w.pickups.empty(), "what a monster drops is taken at once, and would have waited");
        input.Update(1.0f / 60.0f);
    }

    // --- hide boots ----------------------------------------------------------------------------
    Section("hide boots");
    {
        const ItemDef* boots = items.Get("hide_boots");
        Check(boots && boots->slot == SLOT_FEET && boots->move_speed > 0.0f && boots->move_speed <= 0.1f,
              "hide boots are worn on the feet and quicken the step a little");
        Check(boots && boots->defence_bonus > 0 && boots->defence_bonus < items.Get("leather_legs")->defence_bonus,
              "and turn a little aside, less than the chaps");
        Check(boots && !boots->icon.empty() && fs::exists(boots->icon), "with a picture of their own");

        const ItemDef* recipe = nullptr;
        bool jerkin = false;
        for (const ItemDef* r : items.Recipes()) {
            if (r->craft_result == "hide_boots") recipe = r;
            if (r->craft_result == "leather_body") jerkin = true;
        }
        Check(recipe && items.StationFor(*recipe) == CraftStation::Rack && recipe->craft_inputs.count("hide") &&
              recipe->craft_inputs.count("thread") && recipe->craft_level <= 8,
              "made on a tanning rack from hide and thread, early in Crafting");
        Check(jerkin, "and hide still makes a jerkin as well");
        Check(recipe && boots && boots->value >= items.InputValue(*recipe) * ItemDatabase::CRAFT_VALUE_ADD - 1,
              "worth more than the hide that went into them");
        {
            ShopDatabase shopdb;
            shopdb.Load("data/shops.json");
            const ShopDef* ivo = shopdb.Get("havenbrook_bowyer");
            bool sells = false;
            if (ivo) for (const ShopStock& line : ivo->sells) sells |= line.item == "hide_boots";
            Check(sells, "Hunter Ivo sells a pair");
        }
        {
            Equipment eq(&items);
            Check(eq.MoveSpeed() == 0.0f, "nothing worn, nothing quicker");
            eq.Equip(SLOT_FEET, "hide_boots");
            Check(fabsf(eq.MoveSpeed() - 0.05f) < 1e-4f, "hide boots are a twentieth quicker");
            eq.Equip(SLOT_FEET, "drowned_king_boots");
            Check(eq.MoveSpeed() == 0.0f && eq.HasPassive(Player::PASSIVE_MARSHSTRIDE),
                  "the Drowned King's boots keep their own stride instead");
        }

        // Worn, the player covers more ground in the same time.
        Input input;
        std::mt19937 rng(9);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for a walk");
        w.enemies.clear();
        w.player.y -= 200.0f;
        const float x0 = w.player.x, y0 = w.player.y;
        const auto walk = [&](int n) {
            w.player.x = x0;
            w.player.y = y0;
            key(SDLK_D, true);
            for (int f = 0; f < n; ++f) {
                input.Update(1.0f / 60.0f);
                w.Update(1.0f / 60.0f, ctx);
            }
            key(SDLK_D, false);
            input.Update(1.0f / 60.0f);
            w.Update(1.0f / 60.0f, ctx);
            return w.player.x - x0;
        };
        const float plain = walk(60);
        w.player.equipment.Equip(SLOT_FEET, "hide_boots");
        const float shod = walk(60);
        Check(plain > 40.0f && shod > plain * 1.03f && shod < plain * 1.08f,
              "in hide boots the player covers a twentieth more ground in a second (" +
              std::to_string(static_cast<int>(plain)) + " to " + std::to_string(static_cast<int>(shod)) + " px)");
        input.Update(1.0f / 60.0f);
    }

    // --- enchanting -----------------------------------------------------------------------------
    Section("enchanting");
    {
        const auto all = items.Enchantments();
        Check(all.size() >= 8, "there are enchantments to learn (" + std::to_string(all.size()) + ")");
        bool levels_rise = true, fits = true, mats = true, said = true, paid = true;
        int last = 0;
        std::set<int> covered;
        for (const EnchantDef* e : all) {
            levels_rise &= e->level >= last;
            last = e->level;
            fits &= !e->slots.empty();
            for (EquipSlot s : e->slots) { covered.insert(s); fits &= s != SLOT_WEAPON; }
            mats &= !e->inputs.empty() && e->inputs.count("dream_shard") > 0;
            for (const auto& in : e->inputs) mats &= items.Has(in.first) && in.second > 0;
            said &= !e->from.empty() && !e->text.empty();
            paid &= e->xp > 0 && e->value > 0;
        }
        Check(levels_rise, "listed cheapest first");
        Check(fits, "each fits at least one slot, and never a weapon");
        Check(covered.count(SLOT_RING) && covered.count(SLOT_AMULET) && covered.count(SLOT_FEET) &&
              covered.count(SLOT_BODY) && covered.count(SLOT_HEAD) && covered.count(SLOT_SHIELD),
              "between them they cover rings, amulets, boots and armour");
        Check(mats, "every one costs real materials, a shard of dream among them");
        Check(said && paid, "every one says what it does and where it is learned, pays Magic XP and adds to the piece's worth");

        // Every charm but the first is a scroll someone sells; Mira teaches the first.
        ShopDatabase shopdb;
        shopdb.Load("data/shops.json");
        std::set<string> sold;
        bool no_twin_sold = true;
        for (const auto& kv : shopdb.All())
            for (const ShopStock& line : kv.second.sells) {
                if (const ItemDef* d = items.Get(line.item))
                    if (d->learn.rfind("enchant:", 0) == 0) sold.insert(d->learn.substr(8));
                no_twin_sold &= line.item.find('+') == string::npos;
            }
        for (const EnchantDef* e : all) {
            if (e->id == "swiftness") Check(!sold.count(e->id), "Swiftness is taught, not sold");
            else Check(sold.count(e->id), "someone sells the scroll for " + e->name);
        }
        Check(no_twin_sold, "no shop sells an enchanted piece ready made");
        bool scrolls_ok = true;
        int scrolls = 0;
        for (const auto& kv : items.All())
            if (kv.second.learn.rfind("enchant:", 0) == 0) {
                ++scrolls;
                scrolls_ok &= items.Enchantment(kv.second.learn.substr(8)) != nullptr && fs::exists(kv.second.icon) &&
                              kv.second.icon != items.Get("scroll_nettle_brew")->icon;
            }
        Check(scrolls >= 7 && scrolls_ok, "every charm scroll names a real enchantment and is told from a brew's at a glance");

        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            DialogueRunner r;
            r.Begin(&dialogue, "mira_root", "npc_mira", "Mira", dc);
            int teach = -1;
            for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                if (r.VisibleOptions()[i]->next == "mira_enchant_teach") teach = static_cast<int>(i);
            Check(teach >= 0, "Mira offers to teach a new character the shrine's craft");
            if (teach >= 0) {
                r.MoveSelection(teach - r.Selected());
                r.Choose(dc);
                bool learns = false;
                for (const auto& o : r.VisibleOptions()) if (o->action.learn_recipe == "enchant:swiftness") learns = true;
                Check(learns, "and her lesson teaches Swiftness");
            }
            flags.insert("recipe:enchant:swiftness");
            DialogueRunner again;
            again.Begin(&dialogue, "mira_root", "npc_mira", "Mira", dc);
            bool offers = false, more = false;
            for (const auto& o : again.VisibleOptions()) {
                offers |= o->next == "mira_enchant_teach";
                more   |= o->next == "mira_enchant_more";
            }
            Check(!offers && more, "and does not offer the lesson twice, only to talk about it");
        }

        // The twins: an enchanted piece is an item like any other.
        const ItemDef* ring = items.Get("copper_ring");
        const ItemDef* keen = items.Get("copper_ring+keenness");
        const EnchantDef* keenness = items.Enchantment("keenness");
        Check(ring && keen && keenness, "the Copper Ring has a twin of Keenness");
        if (ring && keen && keenness) {
            Check(keen->name == "Copper Ring of Keenness", "named for it (" + keen->name + ")");
            Check(keen->slot == SLOT_RING && keen->icon == ring->icon && !keen->stackable &&
                  keen->attack_bonus == ring->attack_bonus + keenness->attack_bonus &&
                  keen->value == ring->value + keenness->value &&
                  keen->enchant == "keenness" && keen->base_item == "copper_ring",
                  "worn in the same slot with the same picture, the ring's bonuses plus the charm's, and worth both");
            Check(keen->passive_text.find("Keenness") != string::npos, "and the bag says what it does");
            Check(!items.Takes(*keen, *keenness) && !items.Takes(*keen, *items.Enchantment("insight")),
                  "and takes no second charm");
            Check(items.EnchantedId("copper_ring", "keenness") == "copper_ring+keenness" &&
                  items.EnchantedId("copper_ring", "swiftness").empty(), "a ring is not worked with Swiftness");
        }
        {
            const ItemDef* helm = items.Get(items.TierPiece("iron", "helm"));
            const ItemDef* warded = helm ? items.Get(helm->id + "+warding") : nullptr;
            Check(helm && warded && warded->defence_bonus == helm->defence_bonus + 8 &&
                  warded->armour_layer == helm->armour_layer && warded->tint.r == helm->tint.r,
                  "an iron helm takes Warding, for eight more Defence, and is drawn as the same helm");
            const ItemDef* shield = items.Get(items.TierPiece("wood", "shield"));
            const ItemDef* lantern = items.Get("lantern");
            const EnchantDef* fortitude = items.Enchantment("fortitude");
            Check(shield && lantern && fortitude && items.Takes(*shield, *fortitude) && !items.Takes(*lantern, *fortitude),
                  "a shield takes Fortitude and a lantern, worn in the same hand, does not");
            const ItemDef* sword = items.Get(items.TierPiece("iron", "sword"));
            bool sword_takes = false;
            for (const EnchantDef* e : all) sword_takes |= sword && items.Takes(*sword, *e);
            Check(sword && !sword_takes, "a sword takes nothing: weapons are not enchanted");
        }
        {
            int twins = 0;
            bool clean = true;
            for (const auto& kv : items.All())
                if (!kv.second.enchant.empty()) {
                    ++twins;
                    clean &= kv.second.craft_result.empty() && kv.second.learn.empty() &&
                             kv.second.id == kv.second.base_item + "+" + kv.second.enchant;
                }
            for (const ItemDef* r : items.Recipes()) clean &= r->craft_result.find('+') == string::npos;
            Check(twins >= 150 && clean, std::to_string(twins) + " enchanted pieces exist, and none is a recipe or a scroll");
        }

        // Working one.
        {
            Inventory bag(&items);
            bag.Add("copper_ring", 1);
            bag.Add("dream_shard", 2);
            bag.Add("glowcap", 1);
            const auto targets = ::Enchanting::Targets(items, *keenness, bag);
            Check(targets.size() == 1 && bag.Slot(targets[0]).id == "copper_ring", "the table finds the ring in the bag");
            string why;
            Check(!targets.empty() && ::Enchanting::Work(items, *keenness, bag, targets[0], why) &&
                  bag.Count("copper_ring+keenness") == 1 && bag.Count("copper_ring") == 0 &&
                  bag.Count("dream_shard") == 0 && bag.Count("glowcap") == 0,
                  "and works Keenness into it, for the shards and the cap");
            Check(::Enchanting::Targets(items, *keenness, bag).empty(), "the enchanted ring is not offered again");
            bag.Add("copper_ring", 1);
            const auto again = ::Enchanting::Targets(items, *keenness, bag);
            Check(!again.empty() && !::Enchanting::Work(items, *keenness, bag, again[0], why) &&
                  why == "You are missing materials." && bag.Count("copper_ring") == 1,
                  "without materials nothing is taken, and it says why");
            Check(!::Enchanting::Work(items, *keenness, bag, -1, why) && !why.empty(), "nor with nothing to work into");

            Equipment eq(&items);
            eq.Equip(SLOT_RING, "copper_ring+keenness");
            Check(eq.AttackBonus() == ring->attack_bonus + 8, "worn, the charm's bonus counts");
            Equipment shod(&items);
            shod.Equip(SLOT_FEET, "hide_boots+swiftness");
            Check(fabsf(shod.MoveSpeed() - 0.175f) < 1e-4f, "hide boots of Swiftness quicken the step by both");
            Equipment back(&items);
            back.FromJson(shod.ToJson());
            Check(back.InSlot(SLOT_FEET) == "hide_boots+swiftness" && back.MoveSpeed() == shod.MoveSpeed(),
                  "and an enchanted piece survives a save");
        }

        // The tables in the world, and standing at one.
        {
            std::set<string> where;
            bool drawn = true;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const MapObject& o : m.Objects())
                    if (o.type == "altar") { where.insert(id); drawn &= fs::exists(o.sprite); }
            }
            Check(where.count("fernhollow") && where.count("dreamworld") && drawn,
                  "there is a table by Mira's stones and one in the Reverie, and both are drawn");

            Input input;
            std::mt19937 rng(3);
            GameContext ctx;
            ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
            ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
            ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
            ctx.input = &input;       ctx.rng = &rng;
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("fernhollow", "", ctx), "Fernhollow loads");
            w.enemies.clear();
            const auto& objects = w.CurrentMap().Objects();
            bool stood = false;
            for (size_t i = 0; i < objects.size() && !stood; ++i) {
                const MapObject& o = objects[i];
                if (o.type != "altar") continue;
                for (float a = 0.0f; a < 6.28f && !stood; a += 0.3f)
                    for (float d = 20.0f; d <= 50.0f && !stood; d += 6.0f) {
                        const float px = o.x + cosf(a) * d, py = o.y + sinf(a) * d;
                        if (w.CurrentMap().Blocked({px - 8.0f, py - 10.0f, 16.0f, 10.0f})) continue;
                        w.player.x = px;
                        w.player.y = py;
                        for (int f = 0; f < 2; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                        stood = w.player.interact.kind == InteractTarget::Object &&
                                w.player.interact.index == static_cast<int>(i);
                    }
            }
            Check(stood && w.player.interact.label == "Use the enchanting table",
                  "standing at the table, the prompt offers it (" + w.player.interact.label + ")");
            w.TryInteract(ctx);
            const auto reqs = w.TakeRequests();
            Check(reqs.size() == 1 && reqs[0].type == WorldRequest::Type::Enchant,
                  "and pressing the button asks for the enchanting panel");
        }
    }

    // --- combos ---------------------------------------------------------------------------------
    Section("combos: a heavy in the chain, a light after a heavy, and both at once");
    {
        // The shapes.
        const AttackProfile& l0 = ProfileFor(AttackType::Light, 0);
        const AttackProfile& l2 = ProfileFor(AttackType::Light, 2);
        const AttackProfile& strong = ProfileFor(AttackType::Strong);
        const AttackProfile& crush = ProfileForCombo(ComboMove::Crush);
        const AttackProfile& cleave = ProfileForCombo(ComboMove::Cleave);
        const AttackProfile& backhand = ProfileForCombo(ComboMove::Backhand);
        const AttackProfile& cross = ProfileForCombo(ComboMove::CrossCut);
        Check(crush.damage_mult > strong.damage_mult && crush.windup < strong.windup,
              "a Crushing Blow hits harder than a strong attack and comes out sooner");
        Check(cleave.damage_mult > crush.damage_mult && cleave.width > 2.0f * l2.width &&
              cleave.knockback > l2.knockback && cleave.cooldown > l2.cooldown,
              "a Cleave hits harder still, twice as wide as the finisher, throws, and ends the chain");
        Check(backhand.windup < l0.windup && backhand.damage_mult > l0.damage_mult && backhand.cooldown <= l0.cooldown,
              "a Backhand is quicker than an opening light, hits harder, and leaves a light's gap");
        Check(cross.damage_mult > l2.damage_mult && cross.cooldown > strong.cooldown && CROSS_CUT_STAMINA > 0.0f,
              "a Cross Cut hits harder than the finisher, costs stamina, and leaves the longest gap");
        bool named = true;
        for (ComboMove m : {ComboMove::Crush, ComboMove::Cleave, ComboMove::Backhand, ComboMove::CrossCut})
            named &= ProfileForCombo(m).cooldown > 0.0f && string(ComboName(m)).size() > 3;
        Check(named && string(ComboName(ComboMove::None)).empty(),
              "each combo has a gap and a name, and a plain swing has no name");

        // The art: each has a clip of its own, with every tier's sword and
        // spear drawn in the hand for it.
        {
            const SpriteDef* hero = sprites.Get("player_hero");
            bool clips = true;
            int layers = 0;
            for (const char* clip : {"crush", "cleave", "backhand", "spin"}) {
                clips &= hero && hero->Find(clip) != nullptr;
                for (const TierDef& t : items.Tiers())
                    for (const char* w : {"sword", "spear"})
                        if (fs::exists("assets/characters/player_hero/layers/" + string(clip) + "_4_weapon_" +
                                       w + "_" + t.id + ".png")) ++layers;
            }
            Check(clips, "the hero has a clip for each combo");
            Check(layers == 4 * 2 * static_cast<int>(items.Tiers().size()),
                  "and every tier's sword and spear are drawn in the hand for each (" + std::to_string(layers) + ")");
        }

        Input input;
        std::mt19937 rng(31);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        // Monsters held where they were put, so a swing's arc is measured
        // against something that stands still for it.
        vector<std::pair<Enemy*, SDL_FPoint>> pins;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) {
                for (auto& pin : pins) { pin.first->x = pin.second.x; pin.first->y = pin.second.y; pin.first->knock_x = pin.first->knock_y = 0.0f; }
                input.Update(dt);
                w.Update(dt, ctx);
            }
        };
        const auto tap = [&](World& w, SDL_Keycode k) {
            for (auto& pin : pins) { pin.first->x = pin.second.x; pin.first->y = pin.second.y; }
            input.Update(dt); key(k, true);  w.Update(dt, ctx);
            input.Update(dt); key(k, false); w.Update(dt, ctx);
        };
        // The overworld start, up the road on open ground, facing right with
        // a bronze sword: sure of hitting and too weak to kill.
        const auto arena = [&](World& w, const string& weapon) {
            pins.clear();
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap("overworld", "start", ctx)) return false;
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.sprite.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, weapon);
            LevelUp lu;
            w.player.skills.AddXp(SKILL_ATTACK, XpForLevel(70), lu);
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(60), lu);
            w.player.Rest();
            return true;
        };
        // A monster whose body's middle is dx, dy from the player's chest,
        // pinned there.
        const auto spawn = [&](World& w, const string& type, float dx, float dy) -> Enemy* {
            const EnemyDef* stats = enemy_db.Get(type);
            if (!stats) return nullptr;
            EnemySpawnDef def;
            def.type = type; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            const SDL_FPoint aim = Targeting::AimPoint(*e);
            const SDL_FPoint muzzle = Targeting::Muzzle(w.player);
            e->x += (muzzle.x + dx) - aim.x;
            e->y += (muzzle.y + dy) - aim.y;
            e->home_x = e->x; e->home_y = e->y;
            Enemy* raw = e.get();
            pins.push_back({raw, SDL_FPoint{raw->x, raw->y}});
            w.enemies.push_back(std::move(e));
            return raw;
        };
        // Waits for the swing in flight to end and its gap to pass.
        const auto settle = [&](World& w) {
            for (int f = 0; f < 120 && !w.player.CanAttack(); ++f) frames(w, 1);
        };

        // --- the chain ends at three ----------------------------------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                tap(w, SDLK_J); settle(w);
                Check(w.player.ComboOpen() && w.player.NextCombo(false) == ComboMove::Crush &&
                      w.player.NextCombo(true) == ComboMove::None,
                      "after one light the window is open and a heavy would be a Crushing Blow");
                tap(w, SDLK_J);
                Check(w.player.Attack().combo == 1 && w.player.Attack().move == ComboMove::None, "a second light is the second link");
                settle(w);
                Check(w.player.NextCombo(false) == ComboMove::Cleave, "and after two a heavy would be a Cleave");
                tap(w, SDLK_J);
                Check(w.player.Attack().combo == 2, "a third is the finisher");
                settle(w);
                Check(!w.player.ComboOpen() && w.player.NextCombo(false) == ComboMove::None,
                      "and after the finisher the window is closed");
                tap(w, SDLK_J);
                Check(w.player.Attack().combo == 0, "so a fourth light opens a new chain rather than repeating the finisher");
            }
        }

        // --- Light, Heavy: the Crushing Blow, and the reel it leaves ------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                Enemy* orc = spawn(w, "orc1", 26.0f, 0.0f);
                frames(w, 2);
                tap(w, SDLK_J); settle(w);
                tap(w, SDLK_K);
                Check(w.player.Attack().move == ComboMove::Crush && w.player.Attack().type == AttackType::Strong &&
                      !w.player.IsCharging(), "a heavy after one light is a Crushing Blow, out on the press with no hold");
                Check(fabsf(w.player.Attack().damage_mult - crush.damage_mult) < 1e-4f, "at the blow's own damage");
                const int hp_before = orc ? orc->hp : 0;
                for (int f = 0; f < 60 && orc && !orc->Staggered(); ++f) frames(w, 1);
                Check(orc && orc->Staggered() && orc->hp < hp_before, "it lands, and the orc reels");
                int reeling = 0;
                const int player_hp = w.player.hp;
                for (int f = 0; f < 120 && orc && orc->Staggered(); ++f) { frames(w, 1); ++reeling; }
                Check(reeling >= 45 && reeling <= 75, "for about a second (" + std::to_string(reeling) + " frames)");
                Check(w.player.hp == player_hp, "during which it does not swing back");
                Check(orc && !orc->Staggered(), "and then it is back on its feet");
            }
        }

        // --- Light, Light, Heavy: the Cleave, wide enough for a second monster ----------
        {
            const auto side_struck = [&](bool cleave) {
                World w;
                if (!arena(w, "bronze_sword")) return false;
                Enemy* front = spawn(w, "orc1", 26.0f, 0.0f);
                Enemy* side  = spawn(w, "orc1", 24.0f, 44.0f);
                frames(w, 2);
                tap(w, SDLK_J); settle(w);
                tap(w, SDLK_J); settle(w);
                tap(w, cleave ? SDLK_K : SDLK_J);
                if (cleave && w.player.Attack().move != ComboMove::Cleave) return false;
                settle(w);
                return front && side && front->HealthBarVisible() && side->HealthBarVisible();
            };
            Check(!side_struck(false), "the finisher's arc does not reach a monster standing off to the side");
            Check(side_struck(true), "the Cleave's does: a heavy after two lights sweeps wide");
        }

        // --- Heavy, Light: the Backhand, and the chain goes on from it --------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                tap(w, SDLK_K);
                Check(w.player.Attack().type == AttackType::Strong && w.player.Attack().move == ComboMove::None,
                      "a heavy on its own is a plain strong attack");
                settle(w);
                Check(w.player.ComboOpen() && w.player.NextCombo(true) == ComboMove::Backhand &&
                      w.player.NextCombo(false) == ComboMove::None,
                      "after it a light would be a Backhand, and another heavy nothing special");
                tap(w, SDLK_J);
                Check(w.player.Attack().move == ComboMove::Backhand && w.player.Attack().type == AttackType::Light,
                      "and a light on its heels is the Backhand");
                settle(w);
                tap(w, SDLK_J);
                Check(w.player.Attack().combo == 2, "which stands in for two links: the next light is the finisher");
            }
            World w2;
            if (arena(w2, "bronze_sword")) {
                tap(w2, SDLK_K); settle(w2);
                tap(w2, SDLK_J); settle(w2);
                tap(w2, SDLK_K);
                Check(w2.player.Attack().move == ComboMove::Cleave, "or the next heavy the Cleave");
            }
        }

        // --- both at once: the Cross Cut ---------------------------------------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                Enemy* front  = spawn(w, "orc1", 26.0f, 0.0f);
                Enemy* behind = spawn(w, "orc1", -26.0f, 0.0f);
                frames(w, 2);
                const float stamina = w.player.Stamina();
                for (auto& pin : pins) { pin.first->x = pin.second.x; pin.first->y = pin.second.y; }
                input.Update(dt); key(SDLK_J, true); key(SDLK_K, true); w.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); w.Update(dt, ctx);
                Check(w.player.Attack().move == ComboMove::CrossCut, "both buttons on one frame are a Cross Cut");
                Check(fabsf(stamina - w.player.Stamina() - CROSS_CUT_STAMINA) < 0.5f, "which costs its stamina");
                settle(w);
                Check(front && behind && front->HealthBarVisible() && behind->HealthBarVisible(),
                      "and strikes the monster behind the player as well as the one in front");
            }
            World plain;
            if (arena(plain, "bronze_sword")) {
                Enemy* front  = spawn(plain, "orc1", 26.0f, 0.0f);
                Enemy* behind = spawn(plain, "orc1", -26.0f, 0.0f);
                frames(plain, 2);
                tap(plain, SDLK_J); settle(plain);
                Check(front && behind && front->HealthBarVisible() && !behind->HealthBarVisible(),
                      "where a plain light reaches only the one in front");
            }
            // A few frames apart still counts, whichever came first.
            World jk;
            if (arena(jk, "bronze_sword")) {
                input.Update(dt); key(SDLK_J, true); jk.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); jk.Update(dt, ctx);
                Check(jk.player.Attack().type == AttackType::Light && jk.player.Attack().move == ComboMove::None,
                      "a light pressed first comes out as a light");
                input.Update(dt); key(SDLK_K, true); jk.Update(dt, ctx);
                input.Update(dt); key(SDLK_K, false); jk.Update(dt, ctx);
                Check(jk.player.Attack().move == ComboMove::CrossCut, "and a heavy two frames later turns it into the Cross Cut");
            }
            World kj;
            if (arena(kj, "bronze_sword")) {
                input.Update(dt); key(SDLK_K, true); kj.Update(dt, ctx);
                input.Update(dt); kj.Update(dt, ctx);
                Check(!kj.player.Attacking(), "a heavy pressed first is a hold, with nothing out yet");
                input.Update(dt); key(SDLK_J, true); kj.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); kj.Update(dt, ctx);
                Check(kj.player.Attack().move == ComboMove::CrossCut, "and a light two frames into it is the Cross Cut");
            }
            // Winded, the presses mean what they mean alone.
            World tired;
            if (arena(tired, "bronze_sword")) {
                key(SDLK_LSHIFT, true); key(SDLK_D, true);
                for (int f = 0; f < 60 * 8 && !tired.player.Winded(); ++f) frames(tired, 1);
                key(SDLK_LSHIFT, false); key(SDLK_D, false);
                frames(tired, 3);
                input.Update(dt); key(SDLK_J, true); key(SDLK_K, true); tired.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); tired.Update(dt, ctx);
                Check(tired.player.Winded() && tired.player.Attack().move != ComboMove::CrossCut,
                      "winded, there is no Cross Cut to be had");
            }
        }

        // --- a bow reads the same grammar, with its own moves at the end of it --------------
        {
            World w;
            if (arena(w, "oak_shortbow")) {
                input.Update(dt); key(SDLK_J, true); key(SDLK_K, true); w.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); w.Update(dt, ctx);
                Check(w.player.Attack().move == ComboMove::CrossCut &&
                      string(ComboNameFor(ComboMove::CrossCut, AttackStyle::Ranged)) == "Twin Shot",
                      "a bow makes a Twin Shot of the two buttons");
                settle(w);
                tap(w, SDLK_J); settle(w);
                Check(w.player.NextCombo(false) == ComboMove::Crush, "and offers a Split Shot after a shot");
            }
        }

        // --- a press inside a swing is kept -----------------------------------------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                tap(w, SDLK_J);
                frames(w, 6);
                Check(w.player.Attacking() && !w.player.CanAttack(), "six frames into a light the hands are busy");
                tap(w, SDLK_J);
                Check(w.player.Attack().combo == 0, "a second press then starts nothing yet");
                int waited = 0;
                while (waited < 60 && !(w.player.Attacking() && w.player.Attack().combo == 1)) { frames(w, 1); ++waited; }
                Check(w.player.Attacking() && w.player.Attack().combo == 1,
                      "but the second link comes out by itself the moment the first is over (" + std::to_string(waited) + " frames)");
            }
            World w2;
            if (arena(w2, "bronze_sword")) {
                tap(w2, SDLK_J);
                frames(w2, 6);
                tap(w2, SDLK_K);
                int waited = 0;
                while (waited < 60 && !(w2.player.Attacking() && w2.player.Attack().move == ComboMove::Crush)) { frames(w2, 1); ++waited; }
                Check(w2.player.Attack().move == ComboMove::Crush, "and a heavy pressed inside the light is the Crushing Blow when it ends");
            }
        }

        // --- a hold is a hold ------------------------------------------------------------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                frames(w, 24);
                Check(w.player.IsCharging() && !w.player.Attacking(), "the heavy button held past its window is a charge");
                tap(w, SDLK_J);
                Check(!w.player.Attacking() && w.player.IsCharging(), "and a light pressed during it does nothing");
                input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                Check(w.player.Attack().type == AttackType::Charged, "the release is the charged attack");
            }
        }

        // --- a braced leader shrugs a stagger off -------------------------------------------------
        {
            World w;
            if (arena(w, "bronze_sword")) {
                Enemy* orc = spawn(w, "orc1", 26.0f, 0.0f);
                EnemyDef chief_def = *enemy_db.Get("orc3");
                chief_def.heavy.opening = 0.0f;
                EnemySpawnDef def;
                def.type = "orc3"; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
                def.x = w.player.x + 40.0f; def.y = w.player.y + 60.0f;
                auto e = std::make_unique<Enemy>();
                e->Init(&chief_def, def, ctx);
                Enemy* chief = e.get();
                w.enemies.push_back(std::move(e));
                for (int f = 0; f < 120 && !chief->ChargingHeavy(); ++f) frames(w, 1);
                Check(chief->ChargingHeavy(), "a Warchief winds up");
                chief->Stagger(CRUSH_STAGGER);
                Check(!chief->Staggered() && chief->ChargingHeavy(), "and braced in it, a stagger does nothing to him");
                if (orc) {
                    const float ox = orc->x;
                    pins.clear();
                    orc->Stagger(CRUSH_STAGGER);
                    frames(w, 40);
                    Check(orc->Staggered() && fabsf(orc->x - ox) < 2.0f, "where a plain orc reels on the spot");
                }
            }
        }
        input.Update(dt);
    }

    // --- the chain counter, and the highwaymen -------------------------------------------------
    Section("the chain counter, and highwaymen on the forest paths");
    {
        Input input;
        std::mt19937 rng(37);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        vector<std::pair<Enemy*, SDL_FPoint>> pins;
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) {
                for (auto& pin : pins) { pin.first->x = pin.second.x; pin.first->y = pin.second.y; pin.first->knock_x = pin.first->knock_y = 0.0f; }
                input.Update(dt);
                w.Update(dt, ctx);
            }
        };
        const auto tap = [&](World& w, SDL_Keycode k) {
            for (auto& pin : pins) { pin.first->x = pin.second.x; pin.first->y = pin.second.y; }
            input.Update(dt); key(k, true);  w.Update(dt, ctx);
            input.Update(dt); key(k, false); w.Update(dt, ctx);
        };
        const auto arena = [&](World& w, int attack_level) {
            pins.clear();
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap("overworld", "start", ctx)) return false;
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.sprite.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, "bronze_sword");
            LevelUp lu;
            w.player.skills.AddXp(SKILL_ATTACK, XpForLevel(attack_level), lu);
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(60), lu);
            w.player.Rest();
            return true;
        };
        // A target that stands there and takes it: an orc grunt that never
        // notices the player, so nothing it does can break the chain.
        EnemyDef dummy_def = *enemy_db.Get("orc1");
        dummy_def.aggro_range = 0.0f;
        dummy_def.attack_range = 0.0f;
        dummy_def.hp = 500;      // it has to outlast a long run
        const auto spawn = [&](World& w, const string& type, float dx, float dy, int level, bool pin) -> Enemy* {
            const EnemyDef* stats = type == "dummy" ? &dummy_def : enemy_db.Get(type);
            if (!stats) return nullptr;
            EnemySpawnDef def;
            def.type = type; def.level = level; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            const SDL_FPoint aim = Targeting::AimPoint(*e);
            const SDL_FPoint muzzle = Targeting::Muzzle(w.player);
            e->x += (muzzle.x + dx) - aim.x;
            e->y += (muzzle.y + dy) - aim.y;
            e->home_x = e->x; e->home_y = e->y;
            Enemy* raw = e.get();
            if (pin) pins.push_back({raw, SDL_FPoint{raw->x, raw->y}});
            w.enemies.push_back(std::move(e));
            return raw;
        };
        const auto settle = [&](World& w) {
            for (int f = 0; f < 120 && !w.player.CanAttack(); ++f) frames(w, 1);
        };
        const auto trail_is = [&](const World& w, std::initializer_list<const char*> want) {
            const auto& t = w.player.ChainTrail();
            if (t.size() != want.size()) return false;
            size_t i = 0;
            for (const char* s : want) if (t[i++] != s) return false;
            return true;
        };

        // --- the counter ---------------------------------------------------------------
        {
            World w;
            if (arena(w, 70)) {
                Enemy* orc = spawn(w, "dummy", 26.0f, 0.0f, 1, true);
                frames(w, 2);
                Check(orc && w.player.ChainHits() == 0 && w.player.ChainTrail().empty(), "a fresh fight has no chain");
                tap(w, SDLK_J); settle(w);
                Check(w.player.ChainHits() == 1 && trail_is(w, {"Light"}), "one light that lands is a chain of one");
                tap(w, SDLK_J); settle(w);
                tap(w, SDLK_J); settle(w);
                Check(w.player.ChainHits() == 3 && trail_is(w, {"Light", "Light", "Light"}),
                      "three that land are three, and the trail says what they were");
                tap(w, SDLK_J); settle(w);
                tap(w, SDLK_K); settle(w);
                Check(w.player.ChainHits() == 5 && trail_is(w, {"Light", "Light", "Light", "Light", "Crushing Blow"}),
                      "a new chain carries the count on, and a combo is named in the trail");
                Check(w.player.ChainFade() == 1.0f, "and it is fully shown");
                for (int f = 0; f < 60 * 3 && w.player.ChainHits() > 0; ++f) frames(w, 1);
                Check(w.player.ChainHits() == 0 && w.player.ChainTrail().empty() && w.player.ChainFade() == 0.0f,
                      "left alone, the run ends and the counter clears");

                // Ten of them, to see the tail.
                for (int i = 0; i < 10; ++i) { tap(w, SDLK_J); settle(w); }
                Check(w.player.ChainHits() == 10 && w.player.ChainTrail().size() == 6,
                      "a long run counts every hit and keeps the last six for the trail");

                // A blow taken ends it.
                w.HitPlayer(3, orc->Profile(), orc->x, orc->y);
                Check(w.player.ChainHits() == 0, "a blow taken breaks the chain");
                // And so does a swing at nothing.
                tap(w, SDLK_J); settle(w);
                Check(w.player.ChainHits() == 1, "one more lands");
                pins.clear();
                orc->x += 400.0f;
                orc->y += 400.0f;
                tap(w, SDLK_J); settle(w);
                Check(w.player.ChainHits() == 0, "and a swing that meets nothing breaks it");
            }
            World w2;
            if (arena(w2, 70)) {
                spawn(w2, "dummy", 26.0f, 0.0f, 1, true);
                spawn(w2, "dummy", -26.0f, 0.0f, 1, true);
                frames(w2, 2);
                input.Update(dt); key(SDLK_J, true); key(SDLK_K, true); w2.Update(dt, ctx);
                input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); w2.Update(dt, ctx);
                settle(w2);
                Check(w2.player.ChainHits() == 1 && trail_is(w2, {"Cross Cut"}),
                      "a Cross Cut that strikes two counts once, by name");
            }
        }

        // --- the highwaymen -----------------------------------------------------------------
        {
            const EnemyDef* d = enemy_db.Get("highwayman");
            Check(d && d->name == "Highwayman" && d->sprite == "highwayman", "highwaymen are a monster with art of their own");
            if (d) {
                const SpriteDef* sd = sprites.Get(d->sprite);
                bool clips = sd != nullptr, sheets = true;
                for (const char* clip : {"idle", "walk", "attack", "hurt", "death"}) {
                    clips &= sd && sd->Find(clip) != nullptr;
                    sheets &= fs::exists(string("assets/characters/highwayman/") + clip + ".png");
                }
                Check(clips && sheets, "drawn on the hero's rig in a bandit's clothes, with every clip a monster plays");
                Check(loot.Has(d->loot_table), "they carry loot (" + d->loot_table + ")");
                Check(d->body_box.h <= 32.0f && d->foot_box.w <= 16.0f, "and are the size of a person");
                // No tougher than the trail's other company.
                const EnemyDef* orc1 = enemy_db.Get("orc1");
                const EnemyDef* orc2 = enemy_db.Get("orc2");
                const EnemyDef* boar = enemy_db.Get("boar");
                Check(orc1 && orc2 && boar && d->hp <= 24 && d->hp >= boar->hp && d->attack_level > orc1->attack_level &&
                      d->attack_level + 4 < orc2->attack_level && d->speed > orc1->speed,
                      "quicker than an orc grunt, a little tougher, and well short of a raider");
            }

            // Where they stand: in twos at the trailside, and under the trees
            // along the Sunken Road.
            {
                Map trail;
                Check(trail.Load("maps/whisperwood_trail.mx"), "the trail loads");
                int on_trail = 0, by_path = 0, levels_ok = 0, clear = 0;
                for (const EnemySpawnDef& e : trail.Enemies()) {
                    if (e.type != "highwayman") continue;
                    ++on_trail;
                    if (e.level >= 2 && e.level <= 4) ++levels_ok;
                    if (!trail.Blocked({e.x - 6.0f, e.y - 6.0f, 12.0f, 6.0f})) ++clear;
                    bool near_dirt = false;
                    for (const TileInstance& t : trail.Tiles()) {
                        if (trail.TexturePath(t).find("dirt") == string::npos) continue;
                        if (Length(t.rect.x + t.rect.w / 2.0f - e.x, t.rect.y + t.rect.h / 2.0f - e.y) < 100.0f) { near_dirt = true; break; }
                    }
                    if (near_dirt) ++by_path;
                }
                Check(on_trail >= 6, "highwaymen loiter on the Whisperwood Trail (" + std::to_string(on_trail) + ")");
                Check(by_path == on_trail && clear == on_trail, "every one of them by the path, on open ground");
                Check(levels_ok == on_trail, "at levels two to four");
                Map ow;
                int on_road = 0;
                if (ow.Load("maps/overworld.mx"))
                    for (const EnemySpawnDef& e : ow.Enemies()) if (e.type == "highwayman") ++on_road;
                Check(on_road >= 4, "and along the trail east of the road, under the trees (" + std::to_string(on_road) + ")");
            }

            // The daily that sends the player after them.
            {
                const QuestDef* q = quests.Definition("q_daily_highwaymen");
                Check(q && q->giver == "board_mossvale" && q->daily && q->pool == "mossvale",
                      "the Mossvale board posts a daily against them");
                Check(q && q->stages.size() == 1 && q->stages[0].type == ObjectiveType::Kill &&
                      q->stages[0].target == "highwayman" && q->stages[0].map_id == "whisperwood_trail",
                      "which is to drive them off the trail");
            }

            // Played through: a traveller who can manage the trail's foxes
            // can manage one, sword in hand, without a shield.
            {
                World w;
                if (arena(w, 12)) {
                    LevelUp lu;
                    w.player.skills.AddXp(SKILL_STRENGTH, XpForLevel(12), lu);
                    w.player.skills.AddXp(SKILL_DEFENCE, XpForLevel(10), lu);
                    w.player.skills.SetXp(SKILL_HITPOINTS, XpForLevel(16));
                    w.player.SyncHitpoints();
                    w.player.Rest();
                    Enemy* bandit = spawn(w, "highwayman", 40.0f, 0.0f, 3, false);
                    const int start_hp = w.player.hp;
                    int f = 0;
                    for (; f < 60 * 30 && bandit && !bandit->Dead(); ++f) {
                        if (w.player.CanAttack()) { input.Update(dt); key(SDLK_J, true); w.Update(dt, ctx); input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx); f += 2; }
                        else frames(w, 1);
                    }
                    Check(bandit && bandit->Dead() && !w.player.IsDead(),
                          "a level 12 fighter with a bronze sword beats a highwayman (" + std::to_string(f / 60) + "s)");
                    Check(w.player.hp > start_hp / 3, "and walks away with most of their health (" +
                          std::to_string(w.player.hp) + " of " + std::to_string(start_hp) + ")");
                }
            }
        }
        input.Update(dt);
    }

    // --- the quest tracker's counters, the welcome, affinities, the college --------------------
    Section("counters that count, affinities, ranged combos, and the college");
    {
        // --- the tracker --------------------------------------------------------------
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Inventory inv(&items);
            log.Start("q_learn_woodcutting");
            inv.Add("logs", 10);
            log.RefreshCollectObjectives(inv);
            const string text = log.CurrentObjectiveText("q_learn_woodcutting");
            Check(log.Stage("q_learn_woodcutting") == 1, "ten logs in the bag finish the gathering stage");
            Check(text.find("Bring the 10 logs") != string::npos && text.find("(") == string::npos,
                  "and the tracker says to bring them back, with no count (" + text + ")");
            QuestLog fire;
            fire.LoadDefinitions("data/quests.json");
            Inventory bag(&items);
            fire.Start("q_firewood");
            bag.Add("logs", 5);
            fire.RefreshCollectObjectives(bag);
            Check(fire.CurrentObjectiveText("q_firewood").find("(5/12)") != string::npos,
                  "a gathering stage still counts what is carried");
            bag.Add("logs", 7);
            fire.RefreshCollectObjectives(bag);
            Check(fire.CurrentObjectiveText("q_firewood") == "Complete", "and says Complete once it is");
        }

        // --- affinities --------------------------------------------------------------------
        {
            Check(Player::AffinityFor("player_hero") == AttackStyle::Melee &&
                  Player::AffinityFor("player_warden") == AttackStyle::Ranged &&
                  Player::AffinityFor("player_wayfarer") == AttackStyle::Magic,
                  "the hero favours the blade, the warden the bow, the wayfarer the staff");
            Check(string(Player::AffinityName(AttackStyle::Melee)) == "the blade" &&
                  string(Player::AffinityName(AttackStyle::Magic)) == "the staff", "and each is named");
            Input input;
            std::mt19937 rng(41);
            GameContext ctx;
            ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
            ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
            ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
            ctx.input = &input;       ctx.rng = &rng;
            Player hero, wayfarer;
            hero.Init(ctx, "player_hero");
            wayfarer.Init(ctx, "player_wayfarer");
            Check(hero.TalentDamage(AttackStyle::Melee, AttackType::Light) >
                  wayfarer.TalentDamage(AttackStyle::Melee, AttackType::Light) &&
                  fabsf(hero.TalentDamage(AttackStyle::Melee, AttackType::Light) - 1.0f - Player::AFFINITY_DAMAGE) < 1e-4f,
                  "the hero's swings hit a tenth harder than the wayfarer's");
            Check(wayfarer.TalentDamage(AttackStyle::Magic, AttackType::Light) >
                  hero.TalentDamage(AttackStyle::Magic, AttackType::Light), "and the wayfarer's casts than the hero's");
            Check(hero.Profile().attack_bonus == wayfarer.Profile().attack_bonus + Player::AFFINITY_BONUS &&
                  wayfarer.Profile().magic_bonus == hero.Profile().magic_bonus + Player::AFFINITY_BONUS,
                  "each carries a little more accuracy with their own style");

            // And each sets out with the weapon of it, in the wood tier, and
            // armour it can actually wear alongside.
            const auto kit_of = [&](const char* who, WeaponKind kind, const char* weapon) {
                const vector<string> kit = Player::StartingKit(who);
                const ItemDef* w = kit.empty() ? nullptr : items.Get(kit.front());
                bool wearable = !kit.empty();
                int defence = 0;
                for (const string& id : kit) {
                    const ItemDef* d = items.Get(id);
                    wearable &= d && d->slot != SLOT_NONE && d->requirements.empty();
                    if (d) defence += d->defence_bonus;
                }
                Check(w && w->id == weapon && w->slot == SLOT_WEAPON && w->kind == kind && w->tier == "wood",
                      string(who) + " sets out with the " + (w ? w->name : string("?")));
                Check(wearable, string(who) + "'s kit is all wearable at level 1");
                return defence;
            };
            const int hero_def = kit_of("player_hero", WeaponKind::Melee, "wood_sword");
            const int warden_def = kit_of("player_warden", WeaponKind::Bow, "oak_shortbow");
            const int wayfarer_def = kit_of("player_wayfarer", WeaponKind::Staff, "wood_staff");
            Check(hero_def == 26, "the hero wears the cuirass and the shield, 26 points");

            // The other two set out in the wooden tier's armour of their own
            // kind, the whole set of it, and not in the hero's plate.
            const auto dressed_in = [&](const char* who, const char* cut) {
                std::set<int> slots;
                bool own = true, plate = false;
                for (const string& id : Player::StartingKit(who)) {
                    const ItemDef* d = items.Get(id);
                    if (!d) { own = false; continue; }
                    plate |= id == "wood_body" || id == "wood_helm" || id == "wood_legs";
                    if (d->slot != SLOT_HEAD && d->slot != SLOT_BODY && d->slot != SLOT_LEGS) continue;
                    slots.insert(static_cast<int>(d->slot));
                    own &= d->armour_cut == cut && d->tier == "wood";
                }
                return own && !plate && slots.size() == 3;
            };
            Check(dressed_in("player_warden", "hide"), "the warden sets out in rawhide: coif, jerkin and chaps, and no cuirass");
            Check(dressed_in("player_wayfarer", "robe"), "the wayfarer in homespun: hat, robe and skirt, and no cuirass");
            {
                int ranged = 0, magic = 0, hero_style = 0;
                for (const string& id : Player::StartingKit("player_warden"))   if (const ItemDef* d = items.Get(id)) if (d->slot != SLOT_WEAPON) ranged += d->ranged_bonus;
                for (const string& id : Player::StartingKit("player_wayfarer")) if (const ItemDef* d = items.Get(id)) if (d->slot != SLOT_WEAPON) magic += d->magic_bonus;
                for (const string& id : Player::StartingKit("player_hero"))     if (const ItemDef* d = items.Get(id)) if (d->slot != SLOT_WEAPON) hero_style += d->ranged_bonus + d->magic_bonus;
                Check(ranged > 0 && magic > 0 && hero_style == 0,
                      "and what they wear helps the way they fight, which the cuirass never did (+" +
                          std::to_string(ranged) + " Ranged, +" + std::to_string(magic) + " Magic)");
            }
            // Hide and cloth turn less than wood; neither of them is sent out
            // much worse protected than the hero for it.
            Check(warden_def >= 18 && warden_def <= hero_def + 4, "the warden, whose bow takes both hands, wears boots and no shield (" +
                  std::to_string(warden_def) + ")");
            Check(wayfarer_def >= 18 && wayfarer_def <= hero_def + 4, "the wayfarer keeps the shield, a staff being held in one hand (" +
                  std::to_string(wayfarer_def) + ")");
            {
                Inventory bag(&items);
                Equipment worn(&items);
                bool fits = true;
                for (const string& id : Player::StartingKit("player_warden")) {
                    const ItemDef* d = items.Get(id);
                    if (!d) { fits = false; break; }
                    fits &= worn.Equip(d->slot, id).empty();
                }
                Check(fits && worn.InSlot(SLOT_SHIELD).empty(), "and nothing in the warden's kit fights the bow for a hand");
            }
        }

        // --- the ancient magic: data ------------------------------------------------------------
        const auto arcane = spells.Arcane();
        Check(arcane.size() >= 6, "there are ancient spells to learn (" + std::to_string(arcane.size()) + ")");
        {
            bool ok = true;
            int last = 0;
            std::set<string> shapes;
            for (const SpellDef* s : arcane) {
                ok &= s->arcane && s->element == Element::Arcane && s->level >= last && !s->taught_by.empty() &&
                      projectiles.Has(s->projectile) && projectiles.Get(s->projectile)->element == Element::Arcane;
                last = s->level;
                shapes.insert(s->shape);
            }
            Check(ok, "each is arcane, throws something arcane, says where it is learned, and they come in order");
            Check(shapes.count("bolt") && shapes.count("darts") && shapes.count("rays") && shapes.count("rain") &&
                  shapes.count("ring"), "and they take every shape there is");
            Check(spells.Get("eldritch_blast") && spells.Get("hail_of_blades") && spells.Get("magic_missile"),
                  "Eldritch Blast, Magic Missile and Hail of Blades are among them");
            Check(ElementMultiplier(Element::Arcane, Element::Fire) == 1.0f &&
                  ElementMultiplier(Element::Water, Element::Arcane) == 1.0f &&
                  ElementBeats(Element::Arcane) == Element::None, "the arcane stands outside the elements' cycle");
            // Tomes for all but the first, sold at the college.
            ShopDatabase shopdb;
            shopdb.Load("data/shops.json");
            const ShopDef* college = shopdb.Get("fernhollow_college");
            Check(college && college->keeper == "npc_magister" && college->town == "fernhollow",
                  "the college's copying room is a shop in Fernhollow, kept by the magister");
            std::set<string> sold;
            if (college)
                for (const ShopStock& line : college->sells)
                    if (const ItemDef* d = items.Get(line.item))
                        if (d->learn.rfind("spell:", 0) == 0) sold.insert(d->learn.substr(6));
            for (const SpellDef* s : arcane) {
                if (s->id == "eldritch_blast") Check(!sold.count(s->id), "Eldritch Blast is taught, not sold");
                else Check(sold.count(s->id), "the copying room sells the tome of " + s->name);
            }
            bool tomes_ok = true;
            for (const auto& kv : items.All())
                if (kv.second.learn.rfind("spell:", 0) == 0)
                    tomes_ok &= spells.Get(kv.second.learn.substr(6)) != nullptr && fs::exists(kv.second.icon);
            Check(tomes_ok, "every tome names a real spell and has its picture");
        }

        // --- the magister's lesson --------------------------------------------------------------
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            DialogueRunner r;
            r.Begin(&dialogue, "magister_root", "npc_magister", "Magister Orrin", dc);
            int teach = -1;
            for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                if (r.VisibleOptions()[i]->next == "magister_teach") teach = static_cast<int>(i);
            Check(teach >= 0, "the magister offers to teach the old magic");
            if (teach >= 0) {
                r.MoveSelection(teach - r.Selected());
                r.Choose(dc);
                bool learns = false;
                for (const auto& o : r.VisibleOptions()) if (o->action.learn_recipe == "spell:eldritch_blast") learns = true;
                Check(learns, "and his lesson is the Eldritch Blast");
            }
            flags.insert("recipe:spell:eldritch_blast");
            DialogueRunner again;
            again.Begin(&dialogue, "magister_root", "npc_magister", "Magister Orrin", dc);
            bool offers = false;
            for (const auto& o : again.VisibleOptions()) offers |= o->next == "magister_teach";
            Check(!offers, "once");
        }

        // --- the college, and casting in it ---------------------------------------------------------
        Input input;
        std::mt19937 rng(43);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        const auto tap = [&](World& w, SDL_Keycode k) {
            input.Update(dt); key(k, true);  w.Update(dt, ctx);
            input.Update(dt); key(k, false); w.Update(dt, ctx);
        };
        const auto settle = [&](World& w) {
            for (int f = 0; f < 120 && !w.player.CanAttack(); ++f) frames(w, 1);
        };
        {
            Map college;
            Check(college.Load("maps/fernhollow_college.mx") && college.IsInterior(), "the college's hall loads, indoors");
            bool magister = false, circle = false;
            for (const NpcDef& n : college.Npcs()) magister |= n.id == "npc_magister" && n.sprite == "magister";
            std::ifstream in("maps/fernhollow_college.mx");
            json mx;
            in >> mx;
            for (auto it = mx["tiles"].begin(); it != mx["tiles"].end(); ++it)
                circle |= it.key().find("spell_circle") != string::npos;
            Check(magister, "Magister Orrin is in it, drawn in his own robes");
            Check(circle && fs::exists("assets/props/spell_circle.png") && fs::exists("assets/props/college_hall.png") &&
                  fs::exists("assets/characters/magister/idle.png"),
                  "the circle is cut into its floor, and the hall, the circle and the magister are drawn");
            // It was one room under a tower in the hamlet. It is the north side of the
            // college's court now, and the hamlet has the gatehouse.
            Map fern, court;
            bool gate = false, door = false;
            if (fern.Load("maps/fernhollow.mx"))
                for (const Portal& p : fern.Portals()) gate |= p.target_map == "college_grounds";
            if (court.Load("maps/college_grounds.mx"))
                for (const Portal& p : court.Portals()) door |= p.target_map == "fernhollow_college";
            Check(gate && door, "and Fernhollow has a gate into the college, whose court has a door into the hall");
        }

        // A wayfarer with a staff: 5 does nothing until the magic is known,
        // then chooses it, and the Eldritch Blast leaves the staff as a bolt
        // that passes through what it hits.
        {
            World w;
            w.player.Init(ctx, "player_wayfarer");
            Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the caster");
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.sprite.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, "novice_staff");
            LevelUp lu;
            w.player.skills.AddXp(SKILL_MAGIC, XpForLevel(20), lu);
            w.player.SyncMana();
            w.player.RestoreMana();
            const auto press5 = [&]() { w.player.SelectArcane(w.KnownArcane(spells)); };
            press5();
            Check(w.player.SelectedElement() != Element::Arcane, "with no ancient magic known, 5 chooses nothing");
            Check(w.KnownArcane(spells).empty(), "and none is known");
            w.SetFlag("recipe:spell:eldritch_blast");
            Check(w.KnownArcane(spells) == vector<string>{"eldritch_blast"}, "learned, the Eldritch Blast is known");
            press5();
            Check(w.player.SelectedElement() == Element::Arcane && w.player.ArcaneSpell() == "eldritch_blast",
                  "and 5 chooses it");
            const int mana = w.player.Mana();
            w.projectiles.clear();
            tap(w, SDLK_J);
            for (int f = 0; f < 60 && w.projectiles.empty(); ++f) frames(w, 1);
            Check(w.projectiles.size() == 1 && w.projectiles.front().def == projectiles.Get("bolt_eldritch") &&
                  w.projectiles.front().pierce_left >= 3, "a cast is one bolt of force that passes through three bodies");
            Check(w.player.Mana() < mana, "for mana");
            settle(w);
            // Magic Missile too, and 5 steps between them.
            w.SetFlag("recipe:spell:magic_missile");
            press5();
            Check(w.player.ArcaneSpell() == "magic_missile", "with a second spell learned, 5 again turns the page");
            w.projectiles.clear();
            tap(w, SDLK_J);
            for (int f = 0; f < 60 && w.projectiles.empty(); ++f) frames(w, 1);
            Check(w.projectiles.size() == 3, "and Magic Missile is three darts (" + std::to_string(w.projectiles.size()) + ")");
            settle(w);
            w.SetFlag("recipe:spell:thunderwave");
            press5();
            Check(w.player.ArcaneSpell() == "thunderwave", "a third turns to the third");
            w.projectiles.clear();
            tap(w, SDLK_J);
            frames(w, 20);
            Check(w.projectiles.empty(), "which is refused below its Magic level");
            w.player.CycleElement(1);
            Check(w.player.SelectedElement() == Element::Fire, "and R cycles round to fire again");
            // A save keeps the page.
            Player back;
            back.FromJson(w.player.ToJson(), ctx);
            Check(back.ArcaneSpell() == "thunderwave", "the page chosen survives a save");
        }

        // --- the combos, at range ---------------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            w.LoadMap("overworld", "start", ctx);
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.sprite.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, "oak_shortbow");
            tap(w, SDLK_J); settle(w);
            Check(w.player.NextCombo(false) == ComboMove::Crush &&
                  string(ComboNameFor(ComboMove::Crush, AttackStyle::Ranged)) == "Split Shot",
                  "with a bow, a heavy after a shot would be a Split Shot");
            w.projectiles.clear();
            tap(w, SDLK_K);
            Check(w.player.Attack().move == ComboMove::Crush, "and it is");
            for (int f = 0; f < 60 && w.projectiles.size() < 3; ++f) frames(w, 1);
            Check(w.projectiles.size() == 3, "three arrows in a fan (" + std::to_string(w.projectiles.size()) + ")");
            settle(w);
            w.projectiles.clear();
            input.Update(dt); key(SDLK_J, true); key(SDLK_K, true); w.Update(dt, ctx);
            input.Update(dt); key(SDLK_J, false); key(SDLK_K, false); w.Update(dt, ctx);
            Check(w.player.Attack().move == ComboMove::CrossCut, "both buttons are a Twin Shot");
            for (int f = 0; f < 60 && w.projectiles.size() < 2; ++f) frames(w, 1);
            Check(w.projectiles.size() == 2, "two arrows at once");
            settle(w);
            World s;
            s.player.Init(ctx, "player_wayfarer");
            s.LoadMap("overworld", "start", ctx);
            s.enemies.clear();
            s.player.y -= 200.0f;
            s.player.facing = FACE_RIGHT;
            s.player.equipment.Equip(SLOT_WEAPON, "novice_staff");
            s.player.SyncMana();
            s.player.RestoreMana();
            tap(s, SDLK_J); settle(s);
            tap(s, SDLK_J); settle(s);
            const int mana = s.player.Mana();
            s.projectiles.clear();
            tap(s, SDLK_K);
            Check(s.player.Attack().move == ComboMove::Cleave &&
                  string(ComboNameFor(ComboMove::Cleave, AttackStyle::Magic)) == "Cascade",
                  "with a staff, a heavy after two casts is a Cascade");
            for (int f = 0; f < 60 && s.projectiles.size() < 3; ++f) frames(s, 1);
            Check(s.projectiles.size() == 3 && s.player.Mana() < mana, "three bolts in a fan, for more mana");
        }
        input.Update(dt);
    }

    Section("order books");
    {
        ShopDatabase shops;
        shops.Load("data/shops.json");
        QuestLog log;
        log.LoadDefinitions("data/quests.json");

        struct Book { const char* npc; const char* pool; int skill; vector<string> kinds; };
        const Book books[] = {
            {"npc_smith", "halda_orders", SKILL_MINING, {"ore", "bar", "weapon", "hide", "armour"}},
            {"npc_wendel", "wendel_orders", SKILL_FISHING, {"fish"}},
        };
        for (const Book& b : books) {
            std::set<string> kinds;
            int count = 0;
            for (const auto& kv : log.Definitions()) {
                const QuestDef& q = kv.second;
                if (q.pool != b.pool) continue;
                ++count;
                const string qid = "order " + kv.first;
                Check(q.daily && q.giver == b.npc && q.source == QuestSource::Npc,
                      qid + " is a daily given by " + b.npc);
                Check(q.stages.size() == 1 && q.stages[0].type == ObjectiveType::Deliver &&
                      q.stages[0].deliver_to == b.npc, qid + " is one delivery, to " + string(b.npc));
                Check(q.rewards.coins > 0 && q.rewards.xp.count(b.skill) && q.rewards.xp.at(b.skill) > 0,
                      qid + " pays coins and " + SkillName(b.skill) + " XP");
                const ItemDef* want = items.Get(q.stages[0].target);
                Check(want != nullptr, qid + " asks for an item that exists");
                if (!want) continue;
                const QuestStage& st = q.stages[0];
                for (const string& tag : Trade::Tags(items, *want)) kinds.insert(tag);
                if (want->id == "hide") kinds.insert("hide");
                // Filling it beats selling the goods to anyone.
                int best = 0;
                for (const auto& sk : shops.All()) best = std::max(best, Trade::SellPrice(sk.second, items, *want));
                Check(q.rewards.coins > best * st.count, qid + " pays more than selling the goods (" +
                      std::to_string(q.rewards.coins) + "c against " + std::to_string(best * st.count) + "c)");
                // And whatever it needs to be made or caught, it says so.
                for (const ItemDef* r : items.Recipes())
                    if (r->craft_result == want->id && r->craft_level > 1)
                        {
                            const int sk = CraftSkill(items.StationFor(*r));
                            Check(q.requirements.count(sk) && q.requirements.at(sk) >= r->craft_level,
                                  qid + " needs the " + SkillName(sk) + " level its " + want->id + " is made at");
                        }
                if (want->fish_level > 1)
                    Check(q.requirements.count(SKILL_FISHING) && q.requirements.at(SKILL_FISHING) >= want->fish_level,
                          qid + " needs the Fishing level " + want->id + " bites at");
            }
            Check(count >= 6, string(b.pool) + " has a book of orders (" + std::to_string(count) + ")");
            for (const string& k : b.kinds)
                Check(kinds.count(k) > 0, string(b.pool) + " has an order for " + k);

            // A new character always has orders they can fill, every day.
            Skills fresh;
            int days_short = 0, beyond = 0;
            for (int day = 1; day <= 40; ++day) {
                log.SetDay(day);
                const auto posted = log.PoolToday(b.pool, &fresh);
                if (static_cast<int>(posted.size()) < 2) ++days_short;
                for (const string& id : posted) if (!log.CanStart(id, fresh)) ++beyond;
            }
            Check(days_short == 0, string(b.npc) + " posts at least two orders a new character can take, every day");
            Check(beyond == 0, string(b.npc) + " never posts an order the player cannot take yet");
        }
        Check(log.PostsPerDay("halda_orders") == 3, "Halda takes three orders a day");

        // Taking an order, filling it through Halda's conversation, and not again today.
        {
            QuestLog ql;
            ql.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &ql; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            const auto lines = [&](const string& npc, const string& root_node) {
                DialogueRunner r;
                r.Begin(&dialogue, root_node, npc, "Keeper", dc);
                vector<const DialogueOption*> out = r.VisibleOptions();
                return out;
            };
            const auto has = [&](const vector<const DialogueOption*>& opts, bool hand_in, const string& orders) {
                for (const DialogueOption* o : opts)
                    if ((hand_in && o->action.hand_in) || (!orders.empty() && o->action.open_orders == orders)) return true;
                return false;
            };
            Check(has(lines("npc_smith", "smith_root"), false, "npc_smith"), "Halda offers her order book from her first line");
            Check(has(lines("npc_wendel", "wendel_root"), false, "npc_wendel"), "and Wendel his");
            Check(!has(lines("npc_smith", "smith_root"), true, ""), "with no order taken, there is nothing to hand in");

            int day = 1;
            const auto posted = [&](const string& id) {
                ql.SetDay(day);
                const auto t = ql.PoolToday("halda_orders", &sk);
                return std::find(t.begin(), t.end(), id) != t.end();
            };
            while (!posted("q_order_hides") && day < 90) ++day;
            Check(ql.CanStart("q_order_hides", sk), "an order for hides is posted on some day, and can be taken");
            ql.Start("q_order_hides");
            ql.TakeJustStarted();
            inv.Add("hide", 5);
            Check(!has(lines("npc_smith", "smith_root"), true, ""), "five hides are not enough to hand in eight");
            Check(ql.ReadyToDeliver("npc_smith", inv).empty(), "and the order is not ready");
            inv.Add("hide", 4);
            Check(has(lines("npc_smith", "smith_root"), true, ""), "nine hides are: Halda asks for the order");
            Check(!has(lines("npc_wendel", "wendel_root"), true, ""), "Wendel does not");

            // Choose the hand-in line and apply what it does, the way the game does.
            DialogueRunner r;
            r.Begin(&dialogue, "smith_root", "npc_smith", "Halda", dc);
            int index = -1;
            for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                if (r.VisibleOptions()[i]->action.hand_in) index = static_cast<int>(i);
            r.MoveSelection(index - r.Selected());
            r.Choose(dc);
            bool handed = false;
            for (const DialogueAction& a : r.TakeActions())
                if (a.hand_in)
                    for (const string& id : ql.ReadyToDeliver(r.NpcId(), inv)) {
                        const QuestStage& st = ql.Definition(id)->stages[ql.Stage(id)];
                        const int need = st.count - ql.Counter(id);
                        if (!inv.Remove(st.target, need)) continue;
                        QuestEvent e;
                        e.type = ObjectiveType::Deliver; e.target = st.target;
                        e.secondary = r.NpcId(); e.amount = need;
                        ql.Notify(e, inv);
                        handed = true;
                    }
            Check(handed && ql.IsComplete("q_order_hides") && inv.Count("hide") == 1,
                  "handing it in takes eight hides, leaves the ninth, and fills the order");
            const auto done = ql.TakeJustCompleted();
            Check(done.size() == 1 && done[0] == "q_order_hides", "the order pays out once");
            Check(!ql.CanStart("q_order_hides", sk), "the same order cannot be taken twice in a day");

            // A daily for someone else is never handed in by an order line.
            ql.FromJson(json::object());
            ql.SetDay(1);
            ql.Start("q_word_to_fernhollow");
            inv.Add("herbal_tonic", 1);
            Check(ql.ReadyToDeliver("npc_wendel", inv).empty(), "Oona's remedy is not one of Wendel's orders");
        }

        // Levels open more of the book without closing what is already posted.
        {
            QuestLog ql;
            ql.LoadDefinitions("data/quests.json");
            Skills weak, strong;
            LevelUp up;
            strong.AddXp(SKILL_MINING, 200000, up);
            strong.AddXp(SKILL_CRAFTING, 200000, up);
            strong.AddXp(SKILL_SMITHING, 200000, up);
            strong.AddXp(SKILL_FISHING, 2000000, up);
            strong.AddXp(SKILL_COOKING, 200000, up);
            std::set<string> weak_seen, strong_seen;
            for (int day = 1; day <= 60; ++day) {
                ql.SetDay(day);
                for (const string& id : ql.PoolToday("halda_orders", &weak)) weak_seen.insert(id);
                for (const string& id : ql.PoolToday("halda_orders", &strong)) strong_seen.insert(id);
            }
            Check(strong_seen.count("q_order_steel_greaves") && !weak_seen.count("q_order_steel_greaves"),
                  "steel greaves are ordered only from someone who can make them");
            Check(strong_seen.size() > weak_seen.size(), "a skilled smith sees more of the book (" +
                  std::to_string(strong_seen.size()) + " orders against " + std::to_string(weak_seen.size()) + ")");
        }
    }

    Section("a house of your own, and somewhere to put things");
    {
        // The empty house at the bottom of Mossvale. Its door wants a key, the
        // key is under a stone beside it, and inside is the only container in
        // the world that keeps what is put in it.
        Map village;
        Check(village.Load("maps/mossvale.mx"), "Mossvale loads");

        const Portal* door = nullptr;
        for (const Portal& p : village.Portals())
            if (p.target_map == "mossvale_cottage") door = &p;
        Check(door != nullptr, "there is a way into the house from the village");
        if (door) {
            Check(door->locked_by == "mossvale_house_key", "and the door is locked");
            Check(door->requires_interact, "you have to try the door rather than walk through it");
        }
        Check(items.Get("mossvale_house_key") != nullptr, "the key is an item");

        const MapObject* stone = nullptr;
        for (const MapObject& o : village.Objects())
            if (o.id == "rock_mossvale_key") stone = &o;
        Check(stone != nullptr, "there is a stone beside the house");
        if (stone) {
            Check(stone->type == "search", "it is something to look under, not a chest to loot");
            Check(stone->loot_item == "mossvale_house_key", "and the key is under it");
            // The point of the stone is that it is not on the doorstep: a
            // player who walks up to the door should have to go round.
            if (door) {
                const float dx = stone->x - (door->rect.x + door->rect.w / 2.0f);
                Check(std::fabs(dx) > 48.0f, "the stone is round the side, not on the step");
            }
        }

        Map house;
        Check(house.Load("maps/mossvale_cottage.mx"), "the house loads");
        Check(house.IsInterior(), "and it is an inside");
        bool way_out = false;
        for (const Portal& p : house.Portals()) way_out |= p.target_map == "mossvale";
        Check(way_out, "there is a way back out of it");

        const MapObject* chest = nullptr;
        for (const MapObject& o : house.Objects())
            if (o.type == "storage") chest = &o;
        Check(chest != nullptr, "there is a storage chest in it");
        if (chest) {
            Check(chest->capacity == 100, "ten by ten, a hundred slots");
            Check(!chest->sprite.empty() && fs::exists(chest->sprite), "and art to draw it with");
        }

        // The quest that points at all of it: Bess sends you, the stone is the
        // first stage, being inside is the last.
        const QuestDef* q = quests.Definition("q_a_place_of_your_own");
        Check(q != nullptr, "Bess has a quest for the house");
        if (q) {
            Check(q->giver == "npc_cook", "and she is the one who gives it");
            Check(q->stages.size() == 2, "it is two stages long");
            if (q->stages.size() == 2) {
                Check(q->stages[0].type == ObjectiveType::Interact &&
                      q->stages[0].target == "rock_mossvale_key", "find the key");
                Check(q->stages[1].type == ObjectiveType::Reach &&
                      q->stages[1].target == "mossvale_cottage", "then get inside");
            }
        }
    }

    Section("moving in: the whole thing, played through");
    {
        // Driving the world rather than reading the files: walk to the stone,
        // look under it, try the door with and without the key, and put
        // something in the chest.
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        World world;
        GameContext ctx;
        std::mt19937 rng(7);
        ctx.sprites = &sprites; ctx.items = &items; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng;
        world.player.Init(ctx, "player_hero");
        world.player.inventory.SetDatabase(&items);
        constexpr float kFrame = 1.0f / 60.0f;
        log.Start("q_a_place_of_your_own");
        Check(log.IsActive("q_a_place_of_your_own"), "Bess sends you to Mossvale");

        Check(world.LoadMap("mossvale", "from_trail", ctx), "arrive in Mossvale");

        const auto find = [&](const string& id) -> const MapObject* {
            for (const MapObject& o : world.CurrentMap().Objects())
                if (o.id == id) return &o;
            return nullptr;
        };
        const MapObject* stone = find("rock_mossvale_key");
        Check(stone != nullptr, "the stone is where the map says");
        if (!stone) stone = &world.CurrentMap().Objects().front();

        // Stand on the stone and run a frame: what the button offers is worked
        // out during the update, so going through one is the only way to prove
        // the prompt reads right.
        world.player.x = stone->x;
        world.player.y = stone->y + 12.0f;
        world.Update(kFrame, ctx);
        Check(world.player.interact.kind == InteractTarget::Object,
              "standing at the stone offers the stone");
        Check(world.player.interact.label.find("stone") != string::npos,
              "and says it is something to look under");
        Check(!world.player.inventory.Has("mossvale_house_key"), "the key is not in the bag yet");

        world.TryInteract(ctx);
        Check(log.Stage("q_a_place_of_your_own") == 1, "finding the key advances the quest");
        // The key is dropped at the stone's feet the way a chest's contents
        // are, so it has to be picked up before the door will take it.
        world.player.inventory.Add("mossvale_house_key", 1);

        // A second look at the same stone offers nothing.
        world.Update(kFrame, ctx);
        Check(world.player.interact.label.empty() ||
              world.player.interact.kind != InteractTarget::Object,
              "a stone that has been looked under stays looked under");

        // The door.
        const Portal* door = nullptr;
        for (const Portal& p : world.CurrentMap().Portals())
            if (p.target_map == "mossvale_cottage") door = &p;
        Check(door != nullptr, "the door is on the map");
        if (door) {
            Inventory keyless(&items);
            Check(!keyless.Has(door->locked_by), "a character with no key has no key");
            Check(world.player.inventory.Has(door->locked_by), "and this one does");
        }

        Check(world.LoadMap("mossvale_cottage", "entrance", ctx), "let yourself in");
        Check(log.IsComplete("q_a_place_of_your_own"), "being inside finishes the quest");

        const MapObject* chest = nullptr;
        for (const MapObject& o : world.CurrentMap().Objects())
            if (o.type == "storage") chest = &o;
        Check(chest != nullptr, "the chest is in the house");
        if (!chest) chest = &world.CurrentMap().Objects().front();

        world.player.x = chest->x;
        world.player.y = chest->y + 12.0f;
        world.Update(kFrame, ctx);
        Check(world.player.interact.kind == InteractTarget::Object &&
              world.player.interact.label.find("chest") != string::npos,
              "standing at the chest offers to open it");

        world.TakeRequests();            // clear anything the walk queued
        world.TryInteract(ctx);
        vector<WorldRequest> reqs = world.TakeRequests();
        Check(reqs.size() == 1 && reqs[0].type == WorldRequest::Type::Storage,
              "opening it asks the game for the storage panel");
        Check(!reqs.empty() && reqs[0].count == 100, "a hundred slots of it");

        // What the panel does with that: move a stack across and back. The
        // id and position are copied out because `chest` points into the map's
        // own object list, and stepping outside throws that list away.
        const string chest_id = chest->id;
        const float chest_x = chest->x, chest_y = chest->y;
        world.player.inventory.Add("logs", 25);
        Inventory& box = world.Storage(chest_id, 100, &items);
        const int put = box.Add("logs", 25);
        world.player.inventory.Remove("logs", put);
        Check(put == 25 && box.Count("logs") == 25 && world.player.inventory.Count("logs") == 0,
              "twenty-five logs go into the chest");

        // And it is still there after walking out and back in, because the
        // contents belong to the character and not to the map.
        Check(world.LoadMap("mossvale", "from_mossvale_cottage", ctx), "step outside");
        Check(world.LoadMap("mossvale_cottage", "entrance", ctx), "and come back in");
        Check(world.Storage(chest_id, 100, &items).Count("logs") == 25,
              "the logs are still in the chest");

        // A chest that has never been opened is empty, not shared with this one.
        Check(world.Storage("storage_somewhere_else", 100, &items).Count("logs") == 0,
              "a different chest is a different chest");

        // Unlike a looted chest, this one keeps offering itself forever.
        world.player.x = chest_x;
        world.player.y = chest_y + 12.0f;
        world.Update(kFrame, ctx);
        Check(!world.player.interact.label.empty(), "a storage chest never goes quiet");
    }

    Section("the storage chest holds what is put in it");
    {
        Inventory chest(&items, 100);
        Check(chest.SlotCount() == 100, "a chest is a hundred slots");

        Inventory bag(&items);
        bag.Add("logs", 40);
        bag.Add("iron_sword", 1);

        // Moving a stack across is the whole of what the panel does.
        const int moved = chest.Add("logs", bag.Count("logs"));
        Check(moved == 40, "a stack goes in whole");
        bag.Remove("logs", moved);
        Check(bag.Count("logs") == 0 && chest.Count("logs") == 40, "and it is in one place, not two");
        chest.Add("iron_sword", 1);
        bag.Remove("iron_sword", 1);
        Check(chest.Count("iron_sword") == 1, "so does a sword");

        // A chest with no room takes nothing, and says so by taking nothing:
        // the panel leans on the return value to decide whether to remove the
        // stack it was moving, so a lie here would destroy items.
        Inventory tiny(&items, 2);
        Check(tiny.Add("iron_sword", 1) == 1 && tiny.Add("iron_sword", 1) == 1,
              "two swords fill two slots");
        Check(tiny.Add("iron_sword", 1) == 0, "and a third does not go in");
        Check(tiny.Count("iron_sword") == 2, "nothing was swallowed");
        // A partial move reports what actually fit.
        Inventory three(&items, 3);
        Check(three.Add("iron_sword", 5) == 3, "five swords into three slots is three swords");

        // A chest that is made smaller gives back what was in the slots that
        // went away rather than eating them.
        Inventory shrunk(&items, 4);
        shrunk.Add("coins", 100);
        shrunk.Add("iron_sword", 1);
        shrunk.Add("bronze_sword", 1);
        shrunk.Add("steel_sword", 1);
        shrunk.Resize(2);
        Check(shrunk.SlotCount() == 2, "the chest is smaller");
        Check(shrunk.Count("coins") == 100, "and what fits is still there");
        Check(shrunk.Count("iron_sword") + shrunk.Count("bronze_sword") +
              shrunk.Count("steel_sword") == 1, "with room for one of the three swords");
        shrunk.Resize(8);
        Check(shrunk.SlotCount() == 8, "and it grows again");

        // What is in it survives being written out and read back, which is how
        // it gets from one session to the next.
        Inventory saved(&items, 100);
        saved.Add("logs", 40);
        saved.Add("demonite_bar", 7);
        Inventory loaded(nullptr, static_cast<int>(saved.ToJson().size()));
        loaded.FromJson(saved.ToJson());
        loaded.SetDatabase(&items);
        Check(loaded.SlotCount() == 100, "a saved chest comes back the same size");
        Check(loaded.Count("logs") == 40 && loaded.Count("demonite_bar") == 7,
              "with everything that was in it");
    }

    Section("bags: a bigger pack, made dear or found by luck");
    {
        const char* kBags[] = {"bag_satchel", "bag_pack", "bag_rucksack", "bag_haversack"};
        const char* kChests[] = {"chest_common", "chest_dungeon", "chest_barrow", "chest_dream", "chest_peak",
                                 "chest_infernal", "chest_hollowrest", "chest_well"};
        Check(MAX_INVENTORY_SLOTS == INVENTORY_SLOTS + 4 * BAG_ROW && MAX_INVENTORY_SLOTS == 56,
              "four bags, a row of seven each: twenty-eight slots can become fifty-six");
        int last_level = 0, last_value = 0, last_cost = 0;
        for (const char* id : kBags) {
            const ItemDef* d = items.Get(id);
            Check(d && d->use == "bag" && d->bag_slots == BAG_ROW, string(id) + " is a bag, and a row's worth of one");
            if (!d) continue;
            Check(!d->icon.empty() && fs::exists(d->icon), d->name + " has a picture");
            Check(d->slot == SLOT_NONE && !d->consumable && !d->stackable,
                  d->name + " is not worn in a slot, eaten, or stacked");
            Check(d->craft_result == id && items.StationFor(*d) == CraftStation::Rack,
                  d->name + " is hide, and is made on a tanning rack");
            int pieces = 0, cost = 0;
            bool known = true;
            for (const auto& in : d->craft_inputs) {
                const ItemDef* mat = items.Get(in.first);
                known &= mat != nullptr;
                pieces += in.second;
                if (mat) cost += mat->value * in.second;
            }
            Check(known && pieces >= 25, d->name + " takes a great deal of material, all of it real");
            Check(d->craft_level > last_level && d->value > last_value && cost > last_cost,
                  d->name + " asks more of the maker than the one before it, and is worth more");
            last_level = d->craft_level; last_value = d->value; last_cost = cost;

            float best = 0.0f;
            int where = 0;
            for (const char* chest : kChests) {
                const float c = loot.ChanceOf(chest, id);
                if (c > 0.0f) ++where;
                best = std::max(best, c);
            }
            Check(where > 0, d->name + " can be found in a chest");
            Check(best > 0.0f && best <= 0.06f, d->name + " is a rare thing to find in one");
        }
        Check(loot.ChanceOf("chest_common", "bag_haversack") == 0.0f && loot.ChanceOf("chest_dream", "bag_satchel") == 0.0f,
              "and the best of them is not in a barrel by the road, nor the least of them at the end of the world");

        // --- putting one on --------------------------------------------------------------------------
        GameContext ctx;
        ctx.sprites = &sprites;  ctx.items = &items;  ctx.trees = &trees;
        Player p;
        p.Init(ctx, "player_hero");
        Check(p.inventory.SlotCount() == INVENTORY_SLOTS && p.Bags().empty(), "a new character has the bag they always had");
        p.inventory.Add("bag_satchel", 1);
        p.inventory.Add("bag_satchel", 1);
        p.inventory.Add("iron_sword", 1);
        string why;
        Check(!p.WearBag(2, why) && !why.empty() && p.inventory.SlotCount() == INVENTORY_SLOTS,
              "a sword is not a bag, and says so");
        Check(p.WearBag(0, why) && why.empty(), "a satchel is put on from the bag it is in");
        Check(p.inventory.SlotCount() == INVENTORY_SLOTS + BAG_ROW && p.Bags().size() == 1 &&
              p.inventory.Count("bag_satchel") == 1, "which is a row bigger for it, and one satchel lighter");
        Check(!p.WearBag(1, why) && !why.empty() && p.inventory.Count("bag_satchel") == 1 &&
              p.inventory.SlotCount() == INVENTORY_SLOTS + BAG_ROW,
              "a second satchel is only a satchel: it stays where it is, and the reason is given");
        Check(p.inventory.Count("iron_sword") == 1, "and nothing else in the bag was disturbed");

        // The new row is real room.
        p.inventory.Clear();
        for (int i = 0; i < INVENTORY_SLOTS + BAG_ROW; ++i) p.inventory.Add("iron_sword", 1);
        Check(p.inventory.Count("iron_sword") == INVENTORY_SLOTS + BAG_ROW && p.inventory.Full() &&
              p.inventory.Add("iron_sword", 1) == 0, "thirty-five swords go in, and the thirty-sixth does not");

        // --- it is the character's, and goes where they go ----------------------------------------------
        const json saved = p.ToJson();
        Check(saved.contains("bags") && saved["bags"].size() == 1 && saved["inventory"].size() == 35,
              "the save says which bags, and holds every slot");
        Player back;
        back.Init(ctx, "player_hero");
        back.FromJson(saved, ctx);
        Check(back.inventory.SlotCount() == 35 && back.inventory.Count("iron_sword") == 35 && back.Bags() == p.Bags(),
              "loaded, the bag is as big as it was and nothing in the last row is lost");
        Player friend_copy;
        friend_copy.Init(ctx, "player_warden");
        friend_copy.ApplySheet(saved, ctx);
        Check(friend_copy.inventory.SlotCount() == 35 && friend_copy.inventory.Count("iron_sword") == 35,
              "and a friend's host keeps a copy of them with the same room in it");

        // A smaller character read into the same Player is a smaller bag again.
        Player fresh;
        fresh.Init(ctx, "player_hero");
        back.FromJson(fresh.ToJson(), ctx);
        Check(back.inventory.SlotCount() == INVENTORY_SLOTS && back.Bags().empty(),
              "another character loaded over them does not inherit the satchel");
        // A save from before there were bags has none.
        json old = saved;
        old.erase("bags");
        back.FromJson(old, ctx);
        Check(back.inventory.SlotCount() == INVENTORY_SLOTS && back.inventory.Count("iron_sword") == INVENTORY_SLOTS,
              "and a save from before bags loads as it always did");

        // --- all four, in any order, and no further ----------------------------------------------------
        Player rich;
        rich.Init(ctx, "player_wayfarer");
        for (const char* id : {"bag_haversack", "bag_satchel", "bag_rucksack", "bag_pack"}) {
            rich.inventory.Add(id, 1);
            bool worn = false;
            for (int i = 0; i < rich.inventory.SlotCount() && !worn; ++i)
                if (rich.inventory.Slot(i).id == id) worn = rich.WearBag(i, why);
            Check(worn, string(id) + " goes on, whatever was put on before it");
        }
        Check(rich.inventory.SlotCount() == MAX_INVENTORY_SLOTS && rich.BagSlots() == MAX_INVENTORY_SLOTS,
              "with all four, the bag is eight rows");
        Player rich_back;
        rich_back.Init(ctx, "player_wayfarer");
        rich_back.FromJson(rich.ToJson(), ctx);
        Check(rich_back.inventory.SlotCount() == MAX_INVENTORY_SLOTS && rich_back.Bags().size() == 4, "and stays eight rows");
        // A save that names a bag twice, or a thing that is not one, gets no room for it.
        json odd = rich.ToJson();
        odd["bags"] = json::array({"bag_satchel", "bag_satchel", 7, "bag_pack"});
        rich_back.FromJson(odd, ctx);
        Check(rich_back.Bags().size() == 2 && rich_back.inventory.SlotCount() == INVENTORY_SLOTS + 2 * BAG_ROW,
              "a bag named twice in a save counts once");
    }

    Section("a town entrance is a gate, with someone at it");
    {
        struct Way { const char* town; const char* to; bool side; };
        const Way kWays[] = {
            {"town_havenbrook", "overworld",         false},
            {"town_havenbrook", "westwold",          true},
            {"mossvale",        "whisperwood_trail", true},
            {"fernhollow",      "whisperwood_trail", false},
        };
        const Player walker;
        auto feet = [&](float x, float y) {
            return SDL_FRect{x + walker.foot_box.x, y + walker.foot_box.y, walker.foot_box.w, walker.foot_box.h};
        };
        for (const Way& way : kWays) {
            Map town;
            const string what = string(way.town) + " to " + way.to;
            if (!town.Load(string("maps/") + way.town + ".mx")) { Check(false, what + ": the town loads"); continue; }

            // Every road out of a town to open country is on this list: a new
            // one has to come with a gate.
            int roads = 0;
            for (const Portal& o : town.Portals())
                if (!o.requires_interact) {
                    bool listed = false;
                    for (const Way& w : kWays) listed |= string(w.town) == way.town && o.target_map == w.to;
                    Check(listed, string(way.town) + ": the road to " + o.target_map + " is a gate this test knows about");
                    ++roads;
                }
            Check(roads > 0, string(way.town) + " has roads out");

            const Portal* road = nullptr;
            for (const Portal& o : town.Portals()) if (o.target_map == way.to && !o.requires_interact) road = &o;
            Check(road != nullptr, what + ": there is a road");
            if (!road) continue;
            const float cx = road->rect.x + road->rect.w * 0.5f, cy = road->rect.y + road->rect.h * 0.5f;

            // The gate: a gatehouse across a road seen from the front, a tower
            // either side of one seen from the side.
            int houses = 0, north = 0, south = 0;
            for (const TileInstance& t : town.Tiles()) {
                const string& tex = town.TexturePath(t);
                const float bx = t.rect.x + t.rect.w * 0.5f, by = t.rect.y + t.rect.h;
                if (Length(bx - cx, by - cy) > 170.0f) continue;
                if (tex.find("town_gate") != string::npos) ++houses;
                if (tex.find("gate_tower") != string::npos) (by < cy ? north : south)++;
            }
            if (way.side) Check(north == 1 && south == 1 && houses == 0, what + ": a gate tower stands either side of the road");
            else          Check(houses == 1 && north + south == 0, what + ": a gatehouse stands across the road");

            // Someone keeps it, close enough to be at it and not in the way of it.
            const NpcDef* keeper = nullptr;
            for (const NpcDef& n : town.Npcs())
                if (Length(n.x - cx, n.y - cy) <= 130.0f && n.path.empty() &&
                    (n.name.rfind("Warden", 0) == 0 || n.name.rfind("Watchman", 0) == 0)) keeper = &n;
            Check(keeper != nullptr, what + ": a warden stands at the gate, and stays there");
            if (keeper) Check(!town.Blocked(feet(keeper->x, keeper->y)), what + ": " + keeper->name + " is not stood inside a tower");

            // And the way through is a way through: straight in off the road,
            // down the middle, a hundred and sixty pixels without a bump; and
            // wide enough that the keeper is not a cork in it.
            const bool west = road->rect.x <= 1.0f, south_edge = road->rect.y + road->rect.h >= town.Height() - 1.0f;
            Check(west || south_edge, what + ": the road leaves by the west or the south");
            bool clear = true;
            float narrowest = 1.0e9f;
            for (float d = 12.0f; d <= 160.0f; d += 4.0f) {
                const float x = west ? road->rect.x + d : cx;
                const float y = west ? cy : road->rect.y + road->rect.h - d;
                clear &= !town.Blocked(feet(x, y + (west ? 0.0f : walker.foot_box.h)));
                // How much room there is across the road at this step.
                float room_here = 0.0f;
                for (float off = -80.0f; off <= 80.0f; off += 2.0f)
                    if (!town.Blocked(SDL_FRect{west ? x : x + off, west ? y + off : y, 2.0f, 2.0f})) room_here += 2.0f;
                narrowest = std::min(narrowest, room_here);
            }
            Check(clear, what + ": the middle of the road is open all the way through the gate");
            Check(narrowest >= 64.0f, what + ": and the gateway is never narrower than two people");
        }
    }

    Section("the Reverie goes down, and is never the same twice");
    {
        Input input;
        std::mt19937 rng(20260919);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto until = [&](World& w, float seconds, const std::function<bool()>& done) {
            for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) {
                if (done()) return true;
                input.Update(dt); w.Update(dt, ctx);
            }
            return done();
        };

        const char* kDepths[] = {"dreamworld", "dreamworld_2", "dreamworld_3"};
        Map depth[3];
        bool loaded = true;
        for (int i = 0; i < 3; ++i) loaded &= depth[i].Load(string("maps/") + kDepths[i] + ".mx");
        Check(loaded, "all three depths of the dream load");

        // --- three depths, a ladder between each --------------------------------------------------------
        for (int i = 0; i < 3 && loaded; ++i) {
            const Map& m = depth[i];
            const string name = kDepths[i];
            Check(m.Ambient() == "dream" && m.DreamDepth() == i + 1, name + " is a dream, and knows how deep it is");
            int down = 0, up = 0, stones = 0, crystals = 0, chests = 0;
            for (const Portal& o : m.Portals()) {
                Check(o.requires_interact, name + ": a ladder is climbed on purpose, not walked into");
                if (i < 2 && o.target_map == kDepths[i + 1] && o.target_spawn == "from_above") {
                    ++down;
                    Check(o.danger_level > (i == 0 ? 15 : 35), name + ": the ladder down says what it is a ladder down to");
                }
                if (i > 0 && o.target_map == kDepths[i - 1] && o.target_spawn == "from_below") ++up;
            }
            Check(down == (i < 2 ? 1 : 0) && up == (i > 0 ? 1 : 0),
                  name + (i == 0 ? " has a ladder down and none up" : i == 1 ? " has a ladder each way" : " has a ladder up, and is the bottom"));
            SDL_FPoint at;
            if (i > 0) Check(m.Spawn("from_above", at), name + ": there is somewhere to arrive from above");
            if (i < 2) Check(m.Spawn("from_below", at), name + ": there is somewhere to arrive from below");
            for (const MapObject& o : m.Objects()) {
                if (o.type == "dream_wake") ++stones;
                if (o.type == "chest") ++chests;
                if (o.yield == "dream_shard" && o.skill == "Mining") ++crystals;
            }
            Check(stones == 1, name + " can be woken from, without climbing back up");
            Check(chests == 1 && crystals >= 5, name + " has a chest and crystals of its own");
            Check(m.Width() * m.Height() > (i == 0 ? 0.0f : depth[i - 1].Width() * depth[i - 1].Height()),
                  name + (i == 0 ? " is somewhere" : " is bigger than the one above it"));
        }

        // More platforms and more bridges the further down. A platform is a
        // patch of ground; count them by flooding the walkable cells with the
        // planks taken out.
        auto platforms = [&](const Map& m) {
            constexpr float C = 32.0f;
            const int cols = static_cast<int>(m.Width() / C), rows = static_cast<int>(m.Height() / C);
            vector<char> ground(static_cast<size_t>(cols) * rows, 0);
            for (const TileInstance& t : m.Tiles()) {
                if (t.layer != LAYER_GROUND || m.TexturePath(t).find("plank") != string::npos) continue;
                const int cx = static_cast<int>(t.rect.x / C), cy = static_cast<int>(t.rect.y / C);
                if (cx >= 0 && cy >= 0 && cx < cols && cy < rows) ground[static_cast<size_t>(cy) * cols + cx] = 1;
            }
            int count = 0;
            for (int start = 0; start < cols * rows; ++start) {
                if (ground[start] != 1) continue;
                ++count;
                vector<int> todo{start};
                ground[start] = 2;
                while (!todo.empty()) {
                    const int c = todo.back(); todo.pop_back();
                    const int cx = c % cols, cy = c / cols;
                    const int next[4][2] = {{cx + 1, cy}, {cx - 1, cy}, {cx, cy + 1}, {cx, cy - 1}};
                    for (const auto& n : next) {
                        if (n[0] < 0 || n[1] < 0 || n[0] >= cols || n[1] >= rows) continue;
                        char& g = ground[static_cast<size_t>(n[1]) * cols + n[0]];
                        if (g == 1) { g = 2; todo.push_back(n[1] * cols + n[0]); }
                    }
                }
            }
            return count;
        };
        if (loaded) {
            const int p1 = platforms(depth[0]), p2 = platforms(depth[1]), p3 = platforms(depth[2]);
            Check(p1 == 9, "the Reverie is nine platforms now, not five (" + std::to_string(p1) + ")");
            Check(p2 > p1 && p3 > p2, "and there are more of them at every depth (" + std::to_string(p2) + ", " + std::to_string(p3) + ")");
        }

        // --- harder, the further down -----------------------------------------------------------------------
        auto weight = [&](const string& type, int level) {
            const EnemyDef* d = enemy_db.Get(type);
            return d ? (d->attack_level + d->strength_level + d->defence_level + 3 * (level - 1)) : 0;
        };
        int hardest_regular[3] = {0, 0, 0}, easiest_regular[3] = {9999, 9999, 9999}, guardian[3] = {0, 0, 0};
        std::set<string> seen_types[3];
        for (int i = 0; i < 3 && loaded; ++i) {
            for (const EnemySpawnDef& e : depth[i].Enemies()) {
                if (e.pool.empty()) {
                    const EnemyDef* d = enemy_db.Get(e.type);
                    Check(d && d->is_boss == (i > 0) && d->tint.r != 255, string(kDepths[i]) + ": " + e.type + " keeps its post every night, and is a nightmare");
                    guardian[i] = std::max(guardian[i], weight(e.type, e.level));
                    continue;
                }
                Check(e.pool.size() >= 2 && !e.group.empty() && e.type == e.pool.front(),
                      string(kDepths[i]) + ": a post with a pool has a choice, a group, and a fallback");
                for (const string& type : e.pool) {
                    const EnemyDef* d = enemy_db.Get(type);
                    Check(d != nullptr, string(kDepths[i]) + ": " + type + " is a real monster");
                    if (!d) continue;
                    seen_types[i].insert(type);
                    Check(d->tint.r != 255 && d->kill_target == "nightmare" && d->aggro_range > 0.0f,
                          type + " is a dream's version of something: tinted, hostile, and a nightmare to the slate");
                    const LootTable* t = loot.Get(d->loot_table);
                    Check(t && !t->always.empty() && t->always.front().item == "dream_shard", type + " always leaves a shard");
                    hardest_regular[i] = std::max(hardest_regular[i], weight(type, e.level + e.spread));
                    easiest_regular[i] = std::min(easiest_regular[i], weight(type, e.level));
                }
            }
            Check(seen_types[i].size() >= 5, string(kDepths[i]) + " has five kinds of thing or more to find in it");
            Check(guardian[i] > hardest_regular[i], string(kDepths[i]) + ": what guards the way on is worse than anything on the way to it");
        }
        if (loaded) {
            Check(easiest_regular[1] > hardest_regular[0] && easiest_regular[2] > hardest_regular[1],
                  "the easiest thing at each depth is harder than the hardest thing above it");
            Check(guardian[1] > guardian[0] && guardian[2] > guardian[1], "and so are the three that do not move");
            for (const string& t : seen_types[1]) Check(!seen_types[0].count(t) && !seen_types[2].count(t), t + " belongs to the second depth only");
            Check(enemy_db.Get("nightmare_troll") && enemy_db.Get("nightmare_troll")->name == "The Sleepless" &&
                  enemy_db.Get("nightmare_dragon") && enemy_db.Get("nightmare_dragon")->name == "The Unwaking",
                  "the Sleepless has the second ladder behind it, and the Unwaking has nothing behind it at all");
        }

        // --- never the same twice ------------------------------------------------------------------------------
        if (loaded) {
            for (int i = 0; i < 3; ++i) {
                const Map& m = depth[i];
                const string name = kDepths[i];
                std::map<string, std::set<string>> kinds;       // group -> what has kept it, over a month
                int nights_changed = 0;
                bool agreed = true, steady = true, in_pool = true, in_range = true;
                vector<string> last;
                for (int day = 1; day <= 30; ++day) {
                    vector<string> tonight;
                    std::map<string, string> of_group;
                    int post = 0;
                    for (const EnemySpawnDef& e : m.Enemies()) {
                        const EnemySpawnDef a = World::ResolveSpawn(e, name, day, post);
                        const EnemySpawnDef b = World::ResolveSpawn(e, name, day, post);
                        steady &= a.type == b.type && a.level == b.level;
                        ++post;
                        tonight.push_back(a.type + ":" + std::to_string(a.level));
                        in_range &= a.level >= e.level && a.level <= e.level + e.spread;
                        if (e.pool.empty()) { in_pool &= a.type == e.type; continue; }
                        in_pool &= std::find(e.pool.begin(), e.pool.end(), a.type) != e.pool.end();
                        kinds[e.group].insert(a.type);
                        if (!of_group.count(e.group)) of_group[e.group] = a.type;
                        agreed &= of_group[e.group] == a.type;
                    }
                    if (day > 1 && tonight != last) ++nights_changed;
                    last = tonight;
                }
                Check(steady, name + ": asked twice on one night, a post gives one answer");
                Check(in_pool && in_range, name + ": what comes is from the post's pool, at a level the post allows");
                Check(agreed, name + ": the posts on a platform agree, so it holds a pack and not one of each");
                Check(nights_changed >= 27, name + ": it is a different dream nearly every night of a month (" +
                                            std::to_string(nights_changed) + " of 29)");
                bool varied = !kinds.empty();
                for (const auto& kv : kinds) varied &= kv.second.size() >= 2;
                Check(varied, name + ": every platform has been kept by more than one kind of thing in that month");
                // The map is not part of the answer by accident: the same group
                // name at another depth is another roll.
                EnemySpawnDef probe;
                probe.pool = {"a", "b", "c", "d", "e", "f", "g"};
                probe.group = "plateau";
                int differs = 0;
                for (int day = 1; day <= 30; ++day)
                    if (World::ResolveSpawn(probe, name, day, 0).type != World::ResolveSpawn(probe, "somewhere_else", day, 0).type) ++differs;
                Check(differs >= 15, name + ": and its rolls are its own");
            }
            // A post with no pool and no spread is exactly what is written.
            EnemySpawnDef plain;
            plain.type = "boar"; plain.level = 4;
            const EnemySpawnDef same = World::ResolveSpawn(plain, "overworld", 12, 3);
            Check(same.type == "boar" && same.level == 4, "a post that was never given a pool is what the map says it is, any day");
        }

        // A world walked into is kept by what that night says, and two machines
        // that agree what day it is agree who is there -- a guest builds its own
        // monsters from the map file and is only told where they stand.
        {
            auto roster = [&](int day, float hours, const char* map_id) {
                World w;
                w.clock.Set(day, hours);
                w.player.Init(ctx, "player_hero");
                vector<string> out;
                if (w.LoadMap(map_id, "", ctx)) for (const auto& e : w.enemies) out.push_back(e->TypeId() + ":" + std::to_string(e->max_hp));
                return out;
            };
            const vector<string> host = roster(7, 22.0f, "dreamworld_2"), guest = roster(7, 22.0f, "dreamworld_2");
            Check(!host.empty() && host == guest, "the host's Deep Reverie and a guest's are kept by the same things");
            Check(roster(7, 3.5f, "dreamworld_2") == roster(6, 23.0f, "dreamworld_2"),
                  "and it is the same dream after midnight that it was before: the night is one night");
            Check(roster(7, 22.0f, "dreamworld_2") != roster(8, 22.0f, "dreamworld_2"), "and another dream the night after");
            Check(roster(7, 22.0f, "dreamworld") != roster(8, 22.0f, "dreamworld") &&
                  roster(7, 22.0f, "dreamworld_3") != roster(8, 22.0f, "dreamworld_3"), "at every depth");
        }

        // --- one more shard for every ladder down -------------------------------------------------------------
        for (int i = 0; i < 3; ++i) {
            World w;
            w.clock.Set(3, 22.0f);
            w.player.Init(ctx, "player_hero");
            if (!w.LoadMap(kDepths[i], "", ctx)) { Check(false, string(kDepths[i]) + " can be walked into"); continue; }
            const string name = kDepths[i];
            Check(w.InDream() && w.DreamBonus("dream_shard") == i && w.DreamBonus("coins") == 0,
                  name + ": " + std::to_string(i) + " more of every shard, and of nothing else");

            // A kill: the shade's table always has one shard in it, and sometimes a second stack.
            int least = 9999, stacks_lifted = 0, kills = 0;
            for (int k = 0; k < 40; ++k) {
                w.pickups.clear();
                w.SpawnLoot("nightmare_shade", 400.0f, 400.0f, ctx);
                int shards = 0, big = 0;
                for (const Pickup& pk : w.pickups) if (pk.item_id == "dream_shard") { shards += pk.qty; if (pk.qty > 2) ++big; }
                least = std::min(least, shards);
                stacks_lifted += big;
                ++kills;
            }
            Check(least == 1 + i, name + ": the least a nightmare leaves is " + std::to_string(1 + i));
            (void)stacks_lifted; (void)kills;
            // Once for the kill, not once a stack: a table that drops two stacks lifts one of them.
            w.pickups.clear();
            for (int k = 0; k < 200; ++k) w.SpawnLoot("nightmare_shade", 400.0f, 400.0f, ctx);
            int total = 0;
            for (const Pickup& pk : w.pickups) if (pk.item_id == "dream_shard") total += pk.qty;
            w.pickups.clear();
            Check(total >= 200 * (1 + i) && total <= 200 * (1 + i) + 200 * 2,
                  name + ": and it is one bonus a kill, however many stacks the kill drops");

            // A crystal.
            int crystal = -1;
            for (size_t k = 0; k < w.CurrentMap().Objects().size(); ++k) {
                const MapObject& o = w.CurrentMap().Objects()[k];
                if (o.yield == "dream_shard" && o.skill == "Mining") { crystal = static_cast<int>(k); break; }
            }
            Check(crystal >= 0, name + " has a crystal to try");
            if (crystal >= 0) {
                const MapObject& o = w.CurrentMap().Objects()[static_cast<size_t>(crystal)];
                Check(o.skill_level == (i == 0 ? 1 : i == 1 ? 20 : 45), name + ": its crystals ask Mining " + std::to_string(o.skill_level));
            }
        }
        {
            // Awake, a shard is a shard.
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("overworld", "start", ctx) && w.DreamBonus("dream_shard") == 0, "and awake there is no bonus to have");
        }

        // The ladder is a way between depths of one night: the dream a sleeper
        // is having, and where they will wake, come down it with them.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("house_inn_upper", "default", ctx), "somebody is upstairs at the inn");
            for (const MapObject& o : w.CurrentMap().Objects())
                if (o.id == "bed_inn_1") { w.player.x = o.x; w.player.y = o.y + 18.0f; }
            w.clock.Set(2, 21.0f);
            until(w, 0.5f, [] { return false; });
            const float bx = w.player.x, by = w.player.y;
            Check(w.Sleep(World::SleepChoice::Reverie, ctx), "and goes to sleep, into the Reverie");
            Check(until(w, 5.0f, [&] { return w.MapId() == "dreamworld" && !w.TransitionPending(); }) && w.InDream(), "they are dreaming");
            Check(w.RequestTransition("dreamworld_2", "from_above"), "they climb down");
            Check(until(w, 5.0f, [&] { return w.MapId() == "dreamworld_2" && !w.TransitionPending(); }) && w.InDream() &&
                  w.Dream().active && w.Dream().map == "house_inn_upper",
                  "and are still asleep upstairs at the inn, one ladder down");
            Check(w.AmbientLight().b > w.AmbientLight().g, "where the light is still a dream's");
            const int second_light = w.AmbientLight().r + w.AmbientLight().g + w.AmbientLight().b;
            Check(w.RequestTransition("dreamworld_3", "from_above"), "and down again");
            Check(until(w, 5.0f, [&] { return w.MapId() == "dreamworld_3" && !w.TransitionPending(); }) &&
                  w.DreamBonus("dream_shard") == 2, "to the bottom, where everything leaves two more");
            Check(w.AmbientLight().r + w.AmbientLight().g + w.AmbientLight().b < second_light, "and it is darker than it was a ladder up");
            // Dawn finds them there as it would anywhere in the dream.
            w.clock.Set(2, WorldClock::NIGHT_END - 0.01f);
            Check(until(w, 6.0f, [&] { return !w.InDream() && !w.TransitionPending(); }) && w.MapId() == "house_inn_upper" &&
                  fabsf(w.player.x - bx) < 1.0f && fabsf(w.player.y - by) < 1.0f,
                  "dawn, at the bottom of the dream, wakes them in the bed they lay down in");

            // And the stone at the bottom does what the one at the top does.
            w.clock.Set(3, 21.0f);
            until(w, 0.2f, [] { return false; });
            Check(w.Sleep(World::SleepChoice::Reverie, ctx) &&
                  until(w, 5.0f, [&] { return w.MapId() == "dreamworld" && !w.TransitionPending(); }), "another night");
            w.RequestTransition("dreamworld_3", "from_above");
            until(w, 5.0f, [&] { return w.MapId() == "dreamworld_3" && !w.TransitionPending(); });
            for (const MapObject& o : w.CurrentMap().Objects())
                if (o.type == "dream_wake") { w.player.x = o.x; w.player.y = o.y + 20.0f; }
            until(w, 0.1f, [] { return false; });
            Check(w.player.interact.kind == InteractTarget::Object, "the waking stone at the bottom can be reached");
            w.TryInteract(ctx);
            Check(until(w, 6.0f, [&] { return !w.InDream() && !w.TransitionPending(); }) && w.clock.IsNight() &&
                  w.MapId() == "house_inn_upper", "and wakes them, with the night still going");
        }

        // The bags test knows which chests there are.
        Check(loot.ChanceOf("chest_dream_deep", "bag_rucksack") > 0.0f && loot.ChanceOf("chest_dream_deep", "bag_rucksack") <= 0.06f &&
              loot.ChanceOf("chest_dream_dark", "bag_haversack") > 0.0f && loot.ChanceOf("chest_dream_dark", "bag_haversack") <= 0.06f,
              "the deeper chests have the better bags in them, and as rarely as anywhere");
    }

    Section("keys and buttons can be moved, and cannot be lost");
    {
        const float dt = 1.0f / 60.0f;
        const auto key = [](Input& in, SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            return in.HandleEvent(e);
        };
        const auto button = [](Input& in, int b, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
            e.gbutton.button = static_cast<Uint8>(b);
            return in.HandleEvent(e);
        };
        const auto trigger = [](Input& in, bool left, float amount) {
            SDL_Event e{};
            e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            e.gaxis.axis = left ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            e.gaxis.value = static_cast<Sint16>(amount * 32767.0f);
            return in.HandleEvent(e);
        };
        // Pressed for one frame: what came down while it was.
        const auto tap = [&](Input& in, SDL_Keycode k) {
            in.Update(dt);
            key(in, k, true);
            std::set<Action> down;
            for (int a = 0; a < ACTION_COUNT; ++a) if (in.Pressed(static_cast<Action>(a))) down.insert(static_cast<Action>(a));
            key(in, k, false);
            in.Update(dt);
            return down;
        };

        // --- as it ships -----------------------------------------------------------------------------
        const Bindings shipped;
        std::set<SDL_Keycode> keys_seen;
        std::set<int> buttons_seen;
        std::set<string> ids, names;
        for (Action a : Bindings::Rebindable()) {
            Check(shipped.Key(a) != SDLK_UNKNOWN && Bindings::KeyFree(shipped.Key(a)), string(Bindings::Name(a)) + " ships on a key, and one that may be moved");
            Check(keys_seen.insert(shipped.Key(a)).second, string(Bindings::Name(a)) + " ships on a key of its own");
            Check(*Bindings::Name(a) && *Bindings::Id(a) && ids.insert(Bindings::Id(a)).second && names.insert(Bindings::Name(a)).second,
                  string(Bindings::Id(a)) + " has a name for the menu and another for the file, and nothing else has either");
            if (Bindings::OnPad(a))
                Check(Bindings::ButtonFree(shipped.Button(a)) && buttons_seen.insert(shipped.Button(a)).second &&
                      !Bindings::ButtonLabel(shipped.Button(a)).empty(),
                      string(Bindings::Name(a)) + " ships on a button of its own, with a label");
        }
        Check(Bindings::Rebindable().size() >= 20 && !Bindings::OnPad(Action::Drop) && !Bindings::OnPad(Action::SelectFire) &&
              Bindings::OnPad(Action::Sprint) && shipped.Button(Action::Sprint) == PAD_LEFT_TRIGGER && shipped.Button(Action::Target) == PAD_RIGHT_TRIGGER,
              "everything a player does is on the list; a trigger is a button like any other, and the pad has none for the bag's drop or for each element");
        for (SDL_Keycode k : {SDLK_ESCAPE, SDLK_RETURN, SDLK_BACKSPACE, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT})
            Check(!Bindings::KeyFree(k), Bindings::KeyLabel(k) + " is kept for the menus");
        for (int b : {SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_LEFT})
            Check(!Bindings::ButtonFree(b), Bindings::ButtonLabel(b) + " is kept for the menus");

        {
            Input in;
            Check(in.PromptFor(Action::LightAttack) == "J" && in.PromptFor(Action::StrongAttack) == "K" && in.PromptFor(Action::Sprint) == "Shift" &&
                  in.PromptFor(Action::Jump) == "Space" && in.PromptFor(Action::Pause) == "Esc" && in.PromptFor(Action::Confirm) == "J" &&
                  in.PromptFor(Action::Back) == "K" && in.PromptFor(Action::Drop) == "G" && in.PromptFor(Action::SelectArcane) == "5",
                  "the prompts read as they always did, and the ancient magic's key has one at last");
            Check(tap(in, SDLK_J).count(Action::LightAttack) && tap(in, SDLK_J).count(Action::Confirm) && tap(in, SDLK_K).count(Action::Back) &&
                  tap(in, SDLK_TAB).count(Action::Inventory) && tap(in, SDLK_Q).count(Action::QuestLog) && tap(in, SDLK_RSHIFT).count(Action::Sprint) &&
                  tap(in, SDLK_UP).count(Action::MoveUp) && tap(in, SDLK_W).count(Action::MoveUp),
                  "and the keys do what they always did: J swings and confirms, K backs out, Tab and Q and the right Shift are spares, W and Up both go up");
        }

        // --- a swap, never a loss ------------------------------------------------------------------------
        {
            Bindings b;
            Check(b.BindKey(Action::LightAttack, SDLK_K) == Action::StrongAttack && b.Key(Action::LightAttack) == SDLK_K &&
                  b.Key(Action::StrongAttack) == SDLK_J, "giving the light attack the heavy attack's key gives the heavy attack the light attack's");
            Check(b.BindKey(Action::LightAttack, SDLK_K) == Action::COUNT, "asking for the key it has is nothing");
            Check(b.BindKey(Action::Jump, SDLK_F) == Action::COUNT && b.Key(Action::Jump) == SDLK_F, "a key nobody had is just taken");
            const Bindings before = b;
            Check(b.BindKey(Action::Jump, SDLK_ESCAPE) == Action::COUNT && b.BindKey(Action::Jump, SDLK_RETURN) == Action::COUNT &&
                  b.BindKey(Action::Jump, SDLK_UP) == Action::COUNT && b == before, "Esc, Enter and the arrows cannot be given away");
            Check(b.BindButton(Action::Jump, SDL_GAMEPAD_BUTTON_START) == Action::COUNT && b.BindButton(Action::Drop, SDL_GAMEPAD_BUTTON_SOUTH) == Action::COUNT &&
                  b == before, "nor Start, and the bag's drop has no button to move");
            Check(b.BindButton(Action::Jump, PAD_LEFT_TRIGGER) == Action::Sprint && b.Button(Action::Sprint) == SDL_GAMEPAD_BUTTON_LEFT_STICK,
                  "a jump on the left trigger puts the sprint on the stick it came off");

            // Any amount of it: every action still has a key and a button of its own.
            std::mt19937 dice(77);
            const SDL_Keycode pool[] = {SDLK_A, SDLK_B, SDLK_C, SDLK_F, SDLK_G, SDLK_J, SDLK_K, SDLK_Z, SDLK_X, SDLK_1, SDLK_9, SDLK_TAB,
                                        SDLK_LCTRL, SDLK_SPACE, SDLK_ESCAPE, SDLK_UP};
            const int pad_pool[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                                    SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_GUIDE, PAD_LEFT_TRIGGER, PAD_RIGHT_TRIGGER,
                                    SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_DOWN};
            const auto& list = Bindings::Rebindable();
            for (int i = 0; i < 400; ++i) {
                b.BindKey(list[dice() % list.size()], pool[dice() % std::size(pool)]);
                b.BindButton(list[dice() % list.size()], pad_pool[dice() % std::size(pad_pool)]);
            }
            std::set<SDL_Keycode> k2;
            std::set<int> b2;
            bool sound = b.keys.size() == shipped.keys.size() && b.buttons.size() == shipped.buttons.size();
            for (const auto& kv : b.keys) sound &= Bindings::KeyFree(kv.second) && k2.insert(kv.second).second;
            for (const auto& kv : b.buttons) sound &= Bindings::ButtonFree(kv.second) && b2.insert(kv.second).second;
            Check(sound, "after four hundred changes at random every action still has a key and a button, its own, and none of them is Esc");

            // And it comes back out of the file as it went in.
            Bindings back;
            back.FromJson(b.ToJson());
            Check(back == b, "a set of bindings survives being written down");
            Bindings junk;
            junk.FromJson(json::parse(R"({"keys": {"jump": "Escape", "light_attack": "No Such Key", "sprint": 7, "nonsense": "Q"}, "buttons": "no"})"));
            Check(junk == shipped, "and a file of nonsense is the defaults");
            Bindings twice;
            twice.FromJson(json::parse(R"({"keys": {"jump": "F", "sprint": "F", "block": "F"}})"));
            std::set<SDL_Keycode> k3;
            bool own = true;
            for (const auto& kv : twice.keys) own &= k3.insert(kv.second).second;
            Check(own && twice.keys.size() == shipped.keys.size(), "a file that gives three things one key still ends with a key each");
            Bindings none;
            none.FromJson(json());
            Check(none == shipped, "and no file at all is the defaults");
        }

        // --- moved, a key does the new thing and not the old ---------------------------------------------
        {
            Input in;
            Bindings b;
            b.BindKey(Action::LightAttack, SDLK_F);       // nobody's
            b.BindKey(Action::Inventory, SDLK_TAB);       // the spare, claimed
            b.BindKey(Action::Interact, SDLK_Q);          // and the journal's spare, by something else
            in.SetBindings(b);
            Check(tap(in, SDLK_F).count(Action::LightAttack) && tap(in, SDLK_F).count(Action::Confirm) && !tap(in, SDLK_J).count(Action::LightAttack) &&
                  !tap(in, SDLK_J).count(Action::Confirm), "the light attack on F swings and confirms on F, and J does neither");
            Check(in.PromptFor(Action::LightAttack) == "F" && in.PromptFor(Action::Confirm) == "F" && in.PromptFor(Action::Interact) == "Q",
                  "and every prompt that named J names F");
            Check(tap(in, SDLK_Q).count(Action::Interact) && !tap(in, SDLK_Q).count(Action::QuestLog) && tap(in, SDLK_P).count(Action::QuestLog),
                  "Q given to Interact stops being the journal's spare, and P still opens it");
            Check(tap(in, SDLK_TAB).count(Action::Inventory) && !tap(in, SDLK_I).count(Action::Inventory), "Tab given to the bag is the bag's key, and I is nobody's");
            Check(tap(in, SDLK_RETURN).count(Action::Confirm) && tap(in, SDLK_BACKSPACE).count(Action::Back) && tap(in, SDLK_ESCAPE).count(Action::Pause) &&
                  tap(in, SDLK_DOWN).count(Action::MoveDown), "Enter, Backspace, Esc and the arrows are where they were");

            // Held across a change, a key is let go: what was holding it may mean something else now.
            in.Update(dt);
            key(in, SDLK_H, true);
            Check(in.Down(Action::Block), "the guard is up");
            Bindings c = b;
            c.BindKey(Action::Block, SDLK_C);
            in.SetBindings(c);
            Check(!in.Down(Action::Block), "and comes down when the keys are changed under it");
            key(in, SDLK_H, false);
        }

        // --- the pad -------------------------------------------------------------------------------------------
        {
            Input in;
            const auto press = [&](int b) {
                in.Update(dt);
                button(in, b, true);
                std::set<Action> down;
                for (int a = 0; a < ACTION_COUNT; ++a) if (in.Pressed(static_cast<Action>(a))) down.insert(static_cast<Action>(a));
                button(in, b, false);
                in.Update(dt);
                return down;
            };
            Check(press(SDL_GAMEPAD_BUTTON_WEST).count(Action::LightAttack) && press(SDL_GAMEPAD_BUTTON_SOUTH).count(Action::Interact) &&
                  press(SDL_GAMEPAD_BUTTON_SOUTH).count(Action::Confirm) && press(SDL_GAMEPAD_BUTTON_EAST).count(Action::Block) &&
                  press(SDL_GAMEPAD_BUTTON_EAST).count(Action::Back) && press(SDL_GAMEPAD_BUTTON_NORTH).count(Action::Drop) &&
                  press(SDL_GAMEPAD_BUTTON_START).count(Action::Pause) && press(SDL_GAMEPAD_BUTTON_DPAD_UP).count(Action::MoveUp),
                  "as it ships: X swings, A interacts and confirms, B guards and backs out, Y drops in the bag, Start pauses, the d-pad steers");
            in.Update(dt);
            trigger(in, true, 0.9f);
            Check(in.Down(Action::Sprint), "and the left trigger sprints");
            trigger(in, true, 0.0f);
            Check(!in.Down(Action::Sprint), "until it is let up");

            Bindings b;
            b.BindButton(Action::Interact, SDL_GAMEPAD_BUTTON_WEST);      // swaps with the light attack
            b.BindButton(Action::WorldMap, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
            b.BindButton(Action::Jump, PAD_RIGHT_TRIGGER);                // swaps with the lock
            in.SetBindings(b);
            Check(press(SDL_GAMEPAD_BUTTON_WEST).count(Action::Interact) && press(SDL_GAMEPAD_BUTTON_WEST).count(Action::Confirm) &&
                  press(SDL_GAMEPAD_BUTTON_SOUTH).count(Action::LightAttack) && !press(SDL_GAMEPAD_BUTTON_SOUTH).count(Action::Confirm),
                  "with Interact moved to X, X confirms and A swings: the menus follow the action and not the button");
            Check(press(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1).count(Action::WorldMap) && !press(SDL_GAMEPAD_BUTTON_GUIDE).count(Action::WorldMap),
                  "the map can be put on a back paddle, which is how a Steam Deck gets one: its Guide button is Steam's");
            in.Update(dt);
            trigger(in, false, 0.9f);
            Check(in.Pressed(Action::Jump) && !in.Down(Action::Target), "a jump on the right trigger jumps, and does not lock on");
            trigger(in, false, 0.0f);
            Check(press(SDL_GAMEPAD_BUTTON_LEFT_STICK).count(Action::Target), "and the lock is on the stick the jump came off");
        }

        // --- being told what a key is -----------------------------------------------------------------------
        {
            Input in;
            in.Update(dt);
            key(in, SDLK_J, true);
            Check(in.Down(Action::Confirm), "Confirm is held: it is what asked");
            in.Listen(Input::ListenFor::Key);
            Check(in.Listening() && !in.Down(Action::Confirm) && !in.Down(Action::LightAttack), "listening lets go of everything first");
            key(in, SDLK_J, false);
            in.Update(dt);
            Check(!in.TakeHeard().any && in.Listening(), "a key coming up is not an answer");
            key(in, SDLK_G, true);
            Check(!in.Pressed(Action::Drop) && !in.Down(Action::Drop), "what is pressed while listening does nothing in the game");
            const Input::Heard heard = in.TakeHeard();
            Check(heard.any && !heard.cancelled && heard.key == SDLK_G && heard.button < 0 && !in.Listening(), "it is the answer, once, and the listening is over");
            key(in, SDLK_G, false);

            in.Listen(Input::ListenFor::Key);
            key(in, SDLK_ESCAPE, true);
            const Input::Heard off = in.TakeHeard();
            Check(off.cancelled && !off.any && !in.Pressed(Action::Pause) && !in.Listening(), "Esc calls it off, and does not pause the game doing it");
            key(in, SDLK_ESCAPE, false);

            in.Listen(Input::ListenFor::Button);
            key(in, SDLK_F, true);
            Check(!in.TakeHeard().any && in.Listening(), "a key is not a button");
            key(in, SDLK_F, false);
            trigger(in, true, 0.9f);
            const Input::Heard lt = in.TakeHeard();
            Check(lt.any && lt.button == PAD_LEFT_TRIGGER && !in.Down(Action::Sprint), "a trigger pulled while listening for a button is one");
            trigger(in, true, 0.0f);
            in.Listen(Input::ListenFor::Button);
            button(in, SDL_GAMEPAD_BUTTON_START, true);
            Check(in.TakeHeard().cancelled && !in.Pressed(Action::Pause), "and Start calls that off");
        }

        // --- kept -------------------------------------------------------------------------------------------------
        {
            fs::create_directories("bin/selftest_net");
            const string path = "bin/selftest_net/settings_controls.json";
            Settings out;
            Bindings b;
            b.BindKey(Action::LightAttack, SDLK_F);
            b.BindButton(Action::WorldMap, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
            out.controls = b.ToJson();
            out.quest_waypoints = false;
            Check(out.Save(path), "settings with bindings in them are written");
            Settings in_again;
            Check(in_again.Load(path) && !in_again.quest_waypoints, "and read");
            Bindings back;
            back.FromJson(in_again.controls);
            Check(back == b && back.Key(Action::LightAttack) == SDLK_F && back.Button(Action::WorldMap) == SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
                  "with the light attack still on F and the map still on the paddle");
            Settings old;
            { std::ofstream f(path, std::ios::trunc); f << R"({"zoom": 2.0})"; }
            Bindings from_old;
            Check(old.Load(path) && old.quest_waypoints, "settings from before there were bindings load, with waypoints on");
            from_old.FromJson(old.controls);
            Check(from_old == shipped, "and are the keys as they shipped");
            fs::remove(path);
        }
    }

    Section("quest waypoints");
    {
        WaypointIndex ways;
        Check(ways.Load("data/waypoints.json"), "data/waypoints.json loads");
        for (const char* id : kMaps) Check(ways.Areas().count(id) > 0, string(id) + " is in the waypoint index");
        bool exits_known = true;
        size_t people = 0, things = 0, posts = 0;
        for (const auto& kv : ways.Areas()) {
            for (const auto& e : kv.second.exits) exits_known &= ways.Areas().count(e.to) > 0;
            people += kv.second.people.size(); things += kv.second.things.size(); posts += kv.second.posts.size();
        }
        Check(exits_known, "every way out in it leads to a map in it");
        Check(people >= 40 && things >= 1000 && posts >= 300, "and it knows who stands where, what is where, and what lives where");

        // --- roads -------------------------------------------------------------------------------------------------
        const auto road = [&](const string& a, const string& b) { return ways.Route(a, b); };
        Check(road("overworld", "overworld") == vector<string>{"overworld"}, "the road from somewhere to itself is that place");
        Check(road("overworld", "town_havenbrook") == (vector<string>{"overworld", "town_havenbrook"}), "Havenbrook is one door from the Hollowmarch");
        Check(road("town_havenbrook", "mossvale") == (vector<string>{"town_havenbrook", "overworld", "whisperwood_trail", "mossvale"}),
              "Mossvale is out of the gate, up the road and down the trail");
        Check(road("house_inn_upper", "fernhollow_college").size() == 8, "from upstairs at the inn to the college's hall is seven doors");
        Check(road("town_havenbrook", "brackenwood") == (vector<string>{"town_havenbrook", "westwold", "brackenwood"}), "the Brackenwood is out of the west gate");
        Check(road("overworld", "dreamworld").empty() && road("dreamworld_3", "overworld").empty(), "no road leads into a dream, and none out");
        Check(road("dreamworld", "dreamworld_3").size() == 3, "but the ladders are roads");
        Check(road("overworld", "nowhere").empty(), "and nowhere is not on any road");

        // --- every stage of every quest can be found -----------------------------------------------------------
        int stages = 0, located = 0, gathered = 0;
        for (const auto& kv : quests.Definitions()) {
            for (size_t i = 0; i < kv.second.stages.size(); ++i) {
                const QuestStage& st = kv.second.stages[i];
                ++stages;
                const string what = kv.first + " stage " + std::to_string(i + 1);
                if (st.type == ObjectiveType::Reach) {
                    Check(ways.Areas().count(st.target) > 0, what + ": the place to reach is a place");
                    ++located;
                    continue;
                }
                // With enough in the bag, and with none.
                const auto full = ways.SpotsFor(st, st.count, &enemy_db, &loot, &items);
                const auto empty = ways.SpotsFor(st, 0, &enemy_db, &loot, &items);
                if (st.type == ObjectiveType::Collect) {
                    // What is only bought or made has nowhere to point at, and says nothing.
                    if (!empty.empty()) ++gathered;
                    continue;
                }
                Check(!full.empty() && !empty.empty(), what + " (" + st.target + "): there is somewhere to point");
                if (!full.empty()) ++located;
                if (st.type == ObjectiveType::Deliver && !full.empty()) {
                    bool to_them = true;
                    for (const auto& spot : full) to_them &= spot.label.find("somewhere") == string::npos;
                    Check(to_them, what + ": with it in the bag, it points at who wants it");
                }
                for (const auto& spot : full) Check(ways.Areas().count(spot.map) > 0, what + ": on a map that exists");
            }
        }
        Check(stages >= 80 && located >= 70 && gathered >= 5, "which is most of what the journal ever asks (" + std::to_string(located) + " of " +
              std::to_string(stages) + " stages, and " + std::to_string(gathered) + " things to gather)");

        // --- where, for somebody standing somewhere ---------------------------------------------------------------
        Input input;
        std::mt19937 rng(20260920);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.quests = &log;
        World w;
        w.player.Init(ctx, "player_hero");
        w.clock.Set(1, 12.0f);
        Check(w.LoadMap("overworld", "start", ctx), "somebody is standing at Havenbrook's gate");
        const auto where = [&](const string& quest) { return ways.Resolve(log, quest, w, &enemy_db, &loot, &items); };

        Check(!where("q_marens_letter").found, "a quest not in hand has no waypoint");
        Check(log.Start("q_marens_letter") && log.Followed() == "q_marens_letter", "taking a quest follows it");
        {
            // Stage one: speak to the guild master, who is in the guild hall, in Havenbrook, which is through the gate.
            const Waypoint wp = where("q_marens_letter");
            Check(wp.found && !wp.here && wp.map == "guild_hall" && wp.maps_away == 2 && wp.what == "Guild Master Orlend",
                  "the guild master is two doors away, by name");
            bool a_door = false;
            for (const Portal& o : w.CurrentMap().Portals())
                a_door |= o.target_map == "town_havenbrook" && fabsf(o.rect.x + o.rect.w / 2.0f - wp.local_x) < 1.0f &&
                          fabsf(o.rect.y + o.rect.h / 2.0f - wp.local_y) < 1.0f;
            Check(a_door && wp.via == "Enter Havenbrook", "and from the Hollowmarch the thing to walk to is the gate: " + wp.via);
        }
        Check(w.LoadMap("town_havenbrook", "from_field", ctx), "through the gate");
        {
            const Waypoint wp = where("q_marens_letter");
            bool guild_door = false;
            for (const Portal& o : w.CurrentMap().Portals())
                guild_door |= o.target_map == "guild_hall" && fabsf(o.rect.x + o.rect.w / 2.0f - wp.local_x) < 1.0f;
            Check(wp.found && !wp.here && wp.maps_away == 1 && guild_door, "in Havenbrook it is the guild hall's door");
        }
        Check(w.LoadMap("guild_hall", "entrance", ctx), "and through that");
        {
            const Waypoint wp = where("q_marens_letter");
            const Npc* orlend = nullptr;
            for (const auto& n : w.npcs) if (n->Id() == "npc_guildmaster") orlend = n.get();
            Check(wp.found && wp.here && wp.maps_away == 0 && orlend && fabsf(wp.local_x - orlend->x) < 1.0f && fabsf(wp.local_y - orlend->y) < 1.0f,
                  "inside, it is the man himself, where he is standing");
        }

        // Somebody on a round is where they are now, not where the map file first put them.
        {
            Check(w.LoadMap("town_havenbrook", "default", ctx), "back in the square");
            const Npc* walker = nullptr;
            for (const auto& n : w.npcs) if (n->Id() == "npc_brask") walker = n.get();
            QuestDef find_brask;
            Check(walker != nullptr, "Brask is on his round");
            if (walker) {
                QuestStage st;
                st.type = ObjectiveType::Talk;
                st.target = "npc_brask";
                const auto spots = ways.SpotsFor(st, 0, &enemy_db, &loot, &items);
                Check(spots.size() == 1 && spots[0].map == "town_havenbrook", "the index knows which town he walks");
            }
        }

        // --- a kill: the nearest that is still standing --------------------------------------------------------------
        Check(w.LoadMap("overworld", "start", ctx) && log.Start("q_thin_the_herd") && log.Followed() == "q_thin_the_herd",
              "a second quest, taken later, is the one followed");
        {
            Waypoint wp = where("q_thin_the_herd");
            Enemy* nearest = nullptr;
            float best = 1.0e18f;
            for (const auto& e : w.enemies) {
                if (!e->Def() || (e->TypeId() != "boar" && e->Def()->kill_target != "boar")) continue;
                const float d = Length(e->x - w.player.x, e->y - w.player.y);
                if (d < best) { best = d; nearest = e.get(); }
            }
            Check(wp.found && wp.here && nearest && fabsf(wp.local_x - nearest->x) < 1.0f && fabsf(wp.local_y - nearest->y) < 1.0f,
                  "thin the herd points at the nearest boar");
            if (nearest) {
                nearest->Damage(99999);
                for (int f = 0; f < 5; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                wp = where("q_thin_the_herd");
                Check(wp.found && wp.here && (fabsf(wp.local_x - nearest->x) > 1.0f || fabsf(wp.local_y - nearest->y) > 1.0f),
                      "and, that one dead, at the next");
            }
        }
        // A kill that names its map is only ever there.
        {
            QuestStage st;
            st.type = ObjectiveType::Kill; st.target = "boar"; st.map_id = "whisperwood_trail";
            bool only_there = true;
            const auto spots = ways.SpotsFor(st, 0, &enemy_db, &loot, &items);
            for (const auto& spot : spots) only_there &= spot.map == "whisperwood_trail";
            Check(!spots.empty() && only_there, "boars on the trail are boars on the trail, not the nearer ones");
        }

        // --- a delivery: where it comes from, then who wants it ---------------------------------------------------------
        Check(log.Start("q_daily_logs") || log.IsActive("q_daily_logs") || log.Start("q_firewood"), "an order for logs");
        {
            QuestStage st;
            st.type = ObjectiveType::Deliver; st.target = "logs"; st.deliver_to = "npc_sawyer"; st.count = 10;
            const auto empty_handed = ways.SpotsFor(st, 0, &enemy_db, &loot, &items);
            const auto laden = ways.SpotsFor(st, 10, &enemy_db, &loot, &items);
            Check(empty_handed.size() > 20, "with no logs, it is trees: every one of them");
            Check(laden.size() == 1 && laden[0].label == "Sawyer Jessa" && laden[0].map == "town_havenbrook", "with ten, it is Jessa");
            st.target = "raw_minnow";
            const auto water = ways.SpotsFor(st, 0, &enemy_db, &loot, &items);
            Check(!water.empty() && water.front().label == "somewhere to fish", "and a fish is wherever there is water to cast at");
            st.target = "hide";
            bool a_beast = false;
            for (const auto& spot : ways.SpotsFor(st, 0, &enemy_db, &loot, &items)) a_beast |= spot.label == "Wild Boar";
            Check(a_beast, "and a hide is whatever wears one");
        }

        // --- a dream is not down any road --------------------------------------------------------------------------------
        {
            QuestLog dreamer;
            dreamer.LoadDefinitions("data/quests.json");
            // Take it the way the game would not let you, to ask about its first stage.
            Check(dreamer.Start("q_the_water_remembers") || true, "");
            if (dreamer.IsActive("q_the_water_remembers")) {
                const Waypoint wp = ways.Resolve(dreamer, "q_the_water_remembers", w, &enemy_db, &loot, &items);
                Check(!wp.found && wp.hint.find("Reverie") != string::npos && wp.hint.find("bed") != string::npos,
                      "reach the Reverie: there is nothing to point at, and it says to go to bed");
            }
        }

        // --- which quest is followed --------------------------------------------------------------------------------------
        {
            QuestLog j;
            j.LoadDefinitions("data/quests.json");
            Check(j.Followed().empty() && !j.Chosen(), "with nothing in hand, nothing is followed");
            j.Start("q_thin_the_herd");
            j.Start("q_firewood");
            Check(j.Followed() == "q_firewood" && !j.Chosen(), "the newest is followed");
            j.Follow("q_thin_the_herd");
            Check(j.Followed() == "q_thin_the_herd" && j.Chosen(), "until one is chosen");
            j.Start("q_orc_trouble");
            Check(j.Followed() == "q_thin_the_herd", "and a quest taken after that does not take it away");
            j.Follow("q_not_a_quest");
            j.Follow("q_marens_letter");
            Check(j.Followed() == "q_thin_the_herd", "nor does asking to follow what is not in hand");

            QuestLog back;
            back.LoadDefinitions("data/quests.json");
            back.FromJson(j.ToJson());
            Check(back.Followed() == "q_thin_the_herd" && back.Chosen() && back.Active().size() == 3 && back.Status("_following") == QuestStatus::NotStarted,
                  "the choice survives a save, and is not mistaken for a quest");

            j.Follow("q_thin_the_herd");
            Check(!j.Chosen() && j.Followed() == "q_orc_trouble", "asked again of the chosen one, it lets go, and the newest leads");
            j.Follow("q_firewood");
            Inventory bag(&items);
            bag.Add("logs", 50);
            j.RefreshCollectObjectives(bag);
            Check(j.Status("q_firewood") == QuestStatus::Complete && j.Followed() == "q_orc_trouble" && !j.Chosen(),
                  "and a followed quest that is finished hands over to the newest still in hand");

            QuestLog old;
            old.LoadDefinitions("data/quests.json");
            json before = j.ToJson();
            before.erase("_following");
            old.FromJson(before);
            Check(!old.Followed().empty() && old.IsActive(old.Followed()), "a journal saved before any of this follows something that is in hand");
        }
    }

    Section("a monster's level is what it fights like");
    {
        // The number over a monster's head used to be the spawn's own -- a
        // nudge on a stat block, 1 to 8 -- so a dire bear that hits like
        // Combat 62 said "Lv 1" and the Brackenwood, advised at Combat 20, was
        // full of things calling themselves level 1. Now it is worked out from
        // the stats it actually fights with, and nothing about the fight
        // changed with it.
        // Every kind in data/enemies.json, for the checks that hold for all of them.
        static const char* kEveryEnemy[] = {
            "orc1", "orc2", "orc3", "highwayman", "boar", "deer", "fox", "hare", "rat", "spider", "broodmother",
            "lizardman", "lizardman_chief", "ice_troll", "wyvern", "wyvern_matriarch", "imp", "demon", "pit_lord",
            "frost_dragon", "zombie", "skeleton", "wraith", "barrow_wight", "slime", "bat", "hound", "ankou",
            "banshee", "well_warden", "wolf", "bear", "den_mother", "greatwolf", "dire_bear",
            "nightmare_shade", "dread_boar", "nightmare_brute", "gloom_spider", "pale_stag", "dusk_wolf",
            "dream_wolf", "dream_lizardman", "dream_wraith", "dream_bat", "dream_skeleton", "nightmare_troll",
            "dream_bear", "dream_hound", "dream_demon", "dream_banshee", "dream_wyvern", "dream_ankou",
            "nightmare_dragon",
        };
        struct Expect { const char* type; int low, high; };
        const Expect kExpect[] = {
            {"hare", 1, 3}, {"deer", 1, 4}, {"boar", 3, 6}, {"orc1", 4, 8}, {"wolf", 8, 13},
            {"highwayman", 6, 11}, {"orc2", 12, 17}, {"lizardman", 10, 15}, {"bear", 20, 28},
            {"ice_troll", 28, 35}, {"wyvern", 33, 42}, {"demon", 40, 50}, {"greatwolf", 52, 64},
            {"dire_bear", 66, 80}, {"frost_dragon", 72, 86}, {"pit_lord", 60, 72},
        };
        for (const Expect& x : kExpect) {
            const EnemyDef* d = enemy_db.Get(x.type);
            Check(d != nullptr, string(x.type) + " is a monster");
            if (!d) continue;
            const int shown = Enemy::ShownLevelOf(*d, 1);
            Check(shown >= x.low && shown <= x.high,
                  string(d->name) + " reads as Combat " + std::to_string(shown) + ", which is what it fights like");
        }

        // Stronger stats, higher number; a stronger spawn of the same thing, higher again.
        const EnemyDef* boar = enemy_db.Get("boar");
        const EnemyDef* bear = enemy_db.Get("bear");
        const EnemyDef* dire = enemy_db.Get("dire_bear");
        Check(boar && bear && dire &&
              Enemy::ShownLevelOf(*boar, 1) < Enemy::ShownLevelOf(*bear, 1) &&
              Enemy::ShownLevelOf(*bear, 1) < Enemy::ShownLevelOf(*dire, 1),
              "a bear outranks a boar, and a dire bear outranks a bear");
        Check(bear && Enemy::ShownLevelOf(*bear, 1) < Enemy::ShownLevelOf(*bear, 3) &&
              Enemy::ShownLevelOf(*bear, 3) < Enemy::ShownLevelOf(*bear, 6),
              "and a stronger one of the same kind reads higher again");
        // Every monster there is, not only the ones named above.
        for (const char* id : kEveryEnemy) {
            const EnemyDef* d = enemy_db.Get(id);
            if (!d) continue;
            const int shown = Enemy::ShownLevelOf(*d, 1);
            Check(shown >= 1 && shown <= 99, d->name + " reads as a level a character could be");
            Check(Enemy::ShownLevelOf(*d, 8) > shown, d->name + " reads higher when it is a stronger one");
        }

        // Nothing about the fight moved: the stats, the hit points and the
        // damage are the stat block's, whatever number is over its head.
        {
            GameContext ctx;
            std::mt19937 rng(11);
            ctx.sprites = &sprites; ctx.items = &items; ctx.enemies = &enemy_db; ctx.rng = &rng;
            EnemySpawnDef def;
            def.type = "dire_bear"; def.level = 1; def.x = 100.0f; def.y = 100.0f;
            Enemy beast;
            beast.Init(enemy_db.Get("dire_bear"), def, ctx);
            const EnemyDef* d = enemy_db.Get("dire_bear");
            Check(d && beast.max_hp == d->hp && beast.level == 1 && beast.ShownLevel() > 60,
                  "a dire bear has its own hit points, is spawn level 1, and says Combat " + std::to_string(beast.ShownLevel()));
            const CombatProfile p = beast.Profile();
            Check(d && p.attack_level == d->attack_level && p.strength_level == d->strength_level &&
                  p.defence_level == d->defence_level, "and fights with exactly the numbers it always did");
        }

        // Every area's advice matches what is actually in it: the level the
        // portal warns about is somewhere near the middle of what lives beyond.
        struct Area { const char* map; int advised; };
        const Area kAreas[] = {
            {"westwold", 5}, {"brackenwood", 20}, {"dungeon_emberfell_1", 10}, {"dungeon_barrow", 20},
            {"ice_spire_peak", 34}, {"dreamworld_2", 25}, {"dreamworld_3", 50},
        };
        for (const Area& a : kAreas) {
            Map m;
            if (!m.Load(string("maps/") + a.map + ".mx")) { Check(false, string(a.map) + " loads"); continue; }
            vector<int> levels;
            for (const EnemySpawnDef& e : m.Enemies())
                for (const string& type : e.pool.empty() ? vector<string>{e.type} : e.pool) {
                    const EnemyDef* d = enemy_db.Get(type);
                    if (d && !d->is_boss) levels.push_back(Enemy::ShownLevelOf(*d, e.level));
                }
            Check(!levels.empty(), string(a.map) + " has something living in it");
            if (levels.empty()) continue;
            std::sort(levels.begin(), levels.end());
            const int middle = levels[levels.size() / 2];
            // Within a dozen levels either way: an area is a spread, not a number.
            Check(std::abs(middle - a.advised) <= 12,
                  string(a.map) + " is advised at Combat " + std::to_string(a.advised) +
                      " and the middle of what lives there is " + std::to_string(middle));
        }

        // And somewhere advised for a beginner holds nothing that reads like a
        // boss: the first fields are the first fights.
        {
            Map field;
            Check(field.Load("maps/overworld.mx"), "the Hollowmarch loads");
            int over_twenty = 0, total = 0;
            for (const EnemySpawnDef& e : field.Enemies())
                for (const string& type : e.pool.empty() ? vector<string>{e.type} : e.pool) {
                    const EnemyDef* d = enemy_db.Get(type);
                    if (!d || d->is_boss) continue;
                    ++total;
                    if (Enemy::ShownLevelOf(*d, e.level) > 20) ++over_twenty;
                }
            Check(total > 50 && over_twenty * 10 < total, "the Hollowmarch is mostly things a new character can fight");
        }
    }

    Section("a tannery, and a way to train Crafting");
    {
        // Havenbrook had nowhere inside its walls to learn a trade with: the
        // Westwold's tannery is out of the west gate and past the wolves.
        Map town;
        Check(town.Load("maps/town_havenbrook.mx"), "Havenbrook loads");
        const NpcDef* nessa = nullptr;
        for (const NpcDef& n : town.Npcs()) if (n.id == "npc_nessa") nessa = &n;
        Check(nessa != nullptr, "Nessa the Tanner keeps a yard in Havenbrook");
        Check(nessa && nessa->shop == "havenbrook_tannery", "and a shop");
        int frames = 0;
        bool bench = false, sign = false;
        for (const MapObject& o : town.Objects()) {
            frames += o.type == "workbench" && o.station == "rack" && o.id.rfind("rack_tannery_", 0) == 0;
            bench |= o.id == "bench_tannery";
            sign |= o.id == "sign_tannery";
        }
        Check(frames == 3, "with three frames in it to work at, which is the point of a tannery you can reach");
        Check(!bench, "and no carpenter's bench: a tanner's station is the tanner's own");
        Check(sign, "and a sign saying what it is");

        ShopDatabase tannery_shops;
        Check(tannery_shops.Load("data/shops.json"), "the shops load");
        const ShopDef* shop = tannery_shops.Get("havenbrook_tannery");
        Check(shop && shop->town == "havenbrook" && shop->keeper == "npc_nessa", "the tannery is Havenbrook's");
        if (shop) {
            Check(shop->buys.count("leather") && shop->buys.count("cloth"), "she buys hide and cloth");
            for (const ShopStock& row : shop->sells) {
                const ItemDef* d = items.Get(row.item);
                Check(d != nullptr, "she sells " + row.item + ", which exists");
            }
        }

        // The order book: every order asks for something that can be made, at
        // the Crafting the recipe itself asks for, and pays in Crafting.
        vector<const QuestDef*> orders;
        for (const auto& kv : quests.Definitions())
            if (kv.second.giver == "npc_nessa") orders.push_back(&kv.second);
        Check(orders.size() >= 10, "her book has a dozen orders in it (" + std::to_string(orders.size()) + ")");
        int lowest = 99, highest = 0;
        for (const QuestDef* d : orders) {
            Check(d->daily && d->pool == "nessa_orders" && d->posts >= 3,
                  d->name + " is a daily order, three of them posted a day");
            Check(d->stages.size() == 1 && d->stages[0].type == ObjectiveType::Deliver &&
                  d->stages[0].deliver_to == "npc_nessa", d->name + " is a delivery to her");
            if (d->stages.empty()) continue;
            const string& what = d->stages[0].target;
            // It has to be makeable: an order for something nobody can craft is
            // an order nobody can fill. Where a thing can be made more than one
            // way, the order asks for the easiest of them.
            const ItemDef* recipe = nullptr;
            for (const ItemDef* r : items.Recipes())
                if (r->craft_result == what && (!recipe || r->craft_level < recipe->craft_level)) recipe = r;
            Check(recipe != nullptr, d->name + " asks for " + what + ", which somebody can make");
            if (!recipe) continue;
            // All of it is a tanner's work: hers is the leather trade, and the
            // cloth went to Wynn's loom with the rest of the weaving.
            Check(items.StationFor(*recipe) == CraftStation::Rack,
                  what + " is made on a tanning rack, like the ones in her yard");
            const auto needs = d->requirements.find(SKILL_CRAFTING);
            const int asked = needs == d->requirements.end() ? 1 : needs->second;
            Check(asked == recipe->craft_level,
                  d->name + " asks for Crafting " + std::to_string(asked) + ", which is what the recipe asks for");
            Check(d->rewards.xp.count(SKILL_CRAFTING) && d->rewards.xp.at(SKILL_CRAFTING) >= 200,
                  d->name + " pays in Crafting");
            lowest = std::min(lowest, asked);
            highest = std::max(highest, asked);
        }
        Check(lowest == 1, "there is work in it for somebody who has never made anything");
        Check(highest >= 40, "and work in it at Crafting " + std::to_string(highest));

        // A day's posting is three, and they are ones the crafter could do.
        Skills green;
        const vector<string> today = quests.PoolToday("nessa_orders", &green);
        Check(today.size() == 3, "three are posted (" + std::to_string(today.size()) + ")");
        for (const string& id : today) {
            const QuestDef* d = quests.Definition(id);
            const bool easy = d && (!d->requirements.count(SKILL_CRAFTING) || d->requirements.at(SKILL_CRAFTING) <= 1);
            Check(easy,
                  "a crafter who has never made anything is posted work they can do: " + id);
        }
        LevelUp up;
        Skills master;
        master.AddXp(SKILL_CRAFTING, XpForLevel(50), up);
        const vector<string> later = quests.PoolToday("nessa_orders", &master);
        Check(later.size() == 3 && later != today, "and a master of the trade is posted different work");

        // She takes what she orders, and talks about it.
        const DialogueNode* root = dialogue.Get("nessa_root");
        Check(root != nullptr, "she has something to say");
        bool has_orders = false, has_shop = false, has_hand_in = false, has_teach = false;
        if (root)
            for (const DialogueOption& o : root->options) {
                has_orders |= o.action.open_orders == "npc_nessa";
                has_shop   |= o.action.open_shop == "havenbrook_tannery";
                has_hand_in|= o.action.hand_in;
                has_teach  |= o.next == "nessa_teach";
            }
        Check(has_orders && has_shop && has_hand_in && has_teach,
              "and will show the book, the shelf, take an order in, and say how the trade is learned");
    }

    Section("a monster on a map the host is not on");
    {
        // Player One walks into the inn and Player Two stays outside, or a
        // friend takes a road the host has not taken: the map they are left on
        // is a world of its own, and the host's own Player is a stand-in on it
        // -- standing at the arrival point with `absent` set, so that every
        // line written for "the player" has something to point at.
        //
        // Two things about that stand-in used to make the monsters there
        // ignore whoever was actually on the map. Seats are handed out from
        // zero and the host is not one of them, so the first friend to sit
        // down at the host's machine is seat 0 -- and so was the stand-in, by
        // default. And the loop that decides who each monster is after thinks
        // as each player in turn: once as the friend, who it chases, and once
        // as the stand-in, who is a hundred yards away, on which it gives up.
        // The upshot was a boar that changed its mind on every frame of every
        // second and never once landed a blow.
        Input hin;
        std::mt19937 rng(606);
        QuestLog one_quests, two_quests;
        one_quests.LoadDefinitions("data/quests.json");
        two_quests.LoadDefinitions("data/quests.json");
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &one_quests; ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &hin;         ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;

        net::Server offline{net::Server::Config{}};
        World home;
        home.player.Init(ctx, "player_hero");
        Check(home.LoadMap("town_havenbrook", "", ctx), "Player One is in Havenbrook");
        coop::Host realm;
        realm.kept_dir.clear();
        const uint8_t seat = static_cast<uint8_t>(offline.ReserveSeat("Player Two", "player_warden"));
        realm.AddLocal(seat, "Player Two", "player_warden", &two_quests, json());
        PlayerInput hands;
        const auto frame = [&] {
            hin.Update(dt);
            home.Update(dt, ctx);
            realm.FeedLocal(seat, hands);
            realm.Update(dt, offline, home, ctx, true);
        };
        const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
        frames(2);
        Player* two = realm.PlayerOf(seat);
        Check(two != nullptr && realm.WorldOf(seat) == &home, "Player Two is beside them");
        Check(seat == 0, "and holds seat 0, which is the seat a stand-in used to hold too");

        // Player One goes inside; Player Two is left on a map of their own.
        Check(home.LoadMap("house_inn", "default", ctx), "Player One goes into the inn");
        frames(3);
        World* theirs = realm.WorldOf(seat);
        Check(theirs && theirs != &home && theirs->MapId() == "town_havenbrook",
              "Player Two is on a world of their own, still in Havenbrook");
        if (theirs && two) {
            Check(theirs->player.absent && theirs->company, "the host is a stand-in there, and it is company");
            Check(theirs->player.seat == Player::NO_SEAT,
                  "and the stand-in holds a seat no real seat can have, so no monster can mistake it for one");

            theirs->enemies.clear();
            // Out of their barkwood, so a boar's swing is worth something: this
            // is about whether it swings at them at all, not about the armour.
            two->equipment.Clear();
            EnemySpawnDef def;
            def.type = "boar"; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = two->x + 70.0f; def.y = two->y;
            auto e = std::make_unique<Enemy>();
            e->Init(enemy_db.Get("boar"), def, ctx);
            Enemy* boar = e.get();
            theirs->enemies.push_back(std::move(e));

            const int one_hp0 = home.player.hp;
            int swings = 0, changes = 0, taken = 0;
            float closest = 1.0e9f;
            bool chased = false, after_them = false;
            const int frames_run = 60 * 30;
            for (int f = 0; f < frames_run; ++f) {
                const Enemy::State was = boar->CurrentState();
                const int hp_was = two->hp;
                frame();
                const Enemy::State now = boar->CurrentState();
                if (now != was) ++changes;
                if (now == Enemy::State::Attack && was != Enemy::State::Attack) ++swings;
                chased |= now == Enemy::State::Chase;
                after_them |= boar->target_seat == static_cast<int>(seat);
                closest = std::min(closest, Length(boar->x - two->x, boar->y - two->y));
                // Kept on their feet, so the fight can be watched for half a
                // minute rather than ending with them on the floor.
                if (two->hp < hp_was) { taken += hp_was - two->hp; two->hp = two->max_hp; }
            }
            Check(after_them, "the boar is after Player Two, by seat");
            Check(chased && closest <= 26.0f,
                  "it comes for them and gets within reach (" + std::to_string(static_cast<int>(closest)) + "px)");
            Check(swings >= 6, "it swings at them, over and over (" + std::to_string(swings) + " swings in thirty seconds)");
            Check(taken > 0, "and it gets them: " + std::to_string(taken) + " hit points off Player Two");
            Check(home.player.hp == one_hp0, "while Player One, indoors, is not touched by it");
            // The signature of the bug: a monster that is thought about as two
            // different people in one frame changes its mind on every one of
            // them. It used to be one change a frame, eighteen hundred of them.
            Check(changes < frames_run / 8,
                  "and it does not change its mind every frame (" + std::to_string(changes) + " changes in " +
                      std::to_string(frames_run) + " frames)");

            // Two of them on that map: whoever is nearer is who it is after,
            // and the one further off is not forgotten about either.
            Player* third = theirs->AddGuest(2, "Player Three", "player_wayfarer", ctx);
            if (third) {
                third->x = two->x - 400.0f;
                third->y = two->y;
                third->equipment.Clear();
                third->hp = third->max_hp;
                boar->x = third->x + 40.0f;
                boar->y = third->y;
                int third_taken = 0;
                bool after_third = false;
                for (int f = 0; f < 60 * 20; ++f) {
                    const int hp_was = third->hp;
                    frame();
                    after_third |= boar->target_seat == 2;
                    if (third->hp < hp_was) { third_taken += hp_was - third->hp; third->hp = third->max_hp; }
                }
                Check(after_third, "a boar beside the further one turns to them");
                Check(third_taken > 0, "and gets them too");
            }
        }
    }

    Section("cooking, and what a dish is worth");
    {
        // A fire used to cook whatever was nearest the top of the bag when the
        // button was pressed. It opens a menu now, with every raw thing the
        // cook can turn into food on it and, past those, the dishes -- which
        // are worth more than the hit points in them.
        const auto fire = items.Recipes(CraftStation::Range);
        Check(fire.size() >= 15, "the fire has a menu (" + std::to_string(fire.size()) + " things to cook)");
        int plain = 0, dishes = 0;
        for (const ItemDef* r : fire) {
            Check(items.StationFor(*r) == CraftStation::Range, r->craft_result + " is cooked at a fire");
            Check(CraftSkill(CraftStation::Range) == SKILL_COOKING, "and cooking is Cooking");
            const ItemDef* made = items.Get(r->craft_result);
            Check(made && made->consumable, r->craft_result + " is something you can eat");
            if (made && made->IsDish()) ++dishes; else ++plain;
            for (const auto& in : r->craft_inputs)
                Check(items.Get(in.first) != nullptr, r->craft_result + " is made of real things");
        }
        Check(plain >= 8, "every raw thing that could be cooked is on it (" + std::to_string(plain) + ")");
        Check(dishes >= 6, "and the dishes are too (" + std::to_string(dishes) + ")");
        // The plain ones are the same conversions the fire always did.
        const ItemDef* raw = items.Get("raw_meat");
        bool meat_on_the_menu = false;
        for (const ItemDef* r : fire)
            if (r->craft_result == "cooked_meat")
                meat_on_the_menu = r->craft_level == raw->cook_level && r->craft_xp == raw->cook_xp &&
                                   r->craft_inputs.size() == 1 && r->craft_inputs.count("raw_meat");
        Check(meat_on_the_menu, "raw meat still cooks into cooked meat, at the level it always did");

        // What a dish does, and that it says so.
        int lifts_hp = 0, lifts_mana = 0, lifts_breath = 0, lifts_levels = 0;
        for (const ItemDef* r : fire) {
            const ItemDef* d = items.Get(r->craft_result);
            if (!d || !d->IsDish()) continue;
            Check(d->dish_minutes >= 10.0f && d->dish_minutes <= 40.0f,
                  d->name + " sits with you for a few minutes, not a second and not an hour");
            const bool does_something = d->dish_max_hp > 0.0f || d->dish_max_mana > 0.0f ||
                                        d->dish_max_stamina > 0.0f || !d->dish_levels.empty();
            Check(does_something, d->name + " is worth eating for something");
            Check(d->dish_max_hp <= 0.25f && d->dish_max_mana <= 0.25f && d->dish_max_stamina <= 0.30f,
                  d->name + " is a dinner and not a potion");
            lifts_hp += d->dish_max_hp > 0.0f;
            lifts_mana += d->dish_max_mana > 0.0f;
            lifts_breath += d->dish_max_stamina > 0.0f;
            for (const auto& b : d->dish_levels) {
                Check(b.first == SKILL_ATTACK || b.first == SKILL_RANGED || b.first == SKILL_MAGIC ||
                      b.first == SKILL_STRENGTH || b.first == SKILL_DEFENCE,
                      d->name + " lifts a way of fighting");
                Check(b.second >= 1 && b.second <= 6, d->name + " lifts it by a few levels");
                ++lifts_levels;
            }
        }
        Check(lifts_hp >= 2 && lifts_mana >= 2 && lifts_breath >= 2 && lifts_levels >= 3,
              "between them the dishes lift health, mana, breath and the three ways of fighting");

        // Eating one.
        Input input;
        std::mt19937 rng(808);
        GameContext ctx;
        ctx.sprites = &sprites; ctx.items = &items; ctx.trees = &trees; ctx.rng = &rng;
        ctx.loot = &loot; ctx.enemies = &enemy_db; ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;
        // A world to stand in, so the clock that wears a meal off is running.
        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("overworld", "start", ctx), "somewhere to eat it");
        w.enemies.clear();
        Player& p = w.player;
        LevelUp up;
        p.skills.AddXp(SKILL_HITPOINTS, XpForLevel(40), up);
        p.skills.AddXp(SKILL_MAGIC, XpForLevel(40), up);
        p.SyncHitpoints(); p.hp = p.max_hp;
        p.SyncMana(); p.RestoreMana();
        const int hp_before = p.max_hp, mana_before = p.MaxMana();
        const float breath_before = p.MaxStamina();

        const ItemDef* stew = items.Get("hearty_stew");
        Check(stew && stew->IsDish() && stew->dish_max_hp > 0.0f, "there is a stew, and it is a dish");
        p.inventory.Add("hearty_stew", 2);
        string why;
        Check(p.Consume(0, why), "it can be eaten");
        Check(p.Meal() == stew && p.MealLeft() > 0.0f, "and it is what you are on");
        Check(p.max_hp > hp_before, "your health pool is bigger for it (" + std::to_string(hp_before) + " -> " +
                                    std::to_string(p.max_hp) + ")");
        Check(p.MaxMana() == mana_before && p.MaxStamina() == breath_before, "and nothing else is");

        // A second dish is the one you are on; the first is gone.
        const ItemDef* tea = items.Get("moonpetal_tea");
        p.inventory.Add("moonpetal_tea", 1);
        int tea_slot = -1;
        for (int k = 0; k < p.inventory.SlotCount(); ++k) if (p.inventory.Slot(k).id == "moonpetal_tea") tea_slot = k;
        // One mouthful at a time: the stew has to go down first.
        Check(tea_slot >= 0 && !p.Consume(tea_slot, why) && !why.empty(),
              "tea straight after a stew is refused: the stew is still going down");
        for (int f = 0; f < 60 * 2; ++f) w.Update(1.0f / 60.0f, ctx);
        Check(p.EatCooldown() <= 0.0f && p.Consume(tea_slot, why), "and a moment later it is not");
        Check(p.Meal() == tea && p.max_hp == hp_before && p.MaxMana() > mana_before,
              "one dish at a time: the stew is gone and the mana is up");
        for (const auto& b : tea->dish_levels)
            Check(p.skills.Current(b.first) >= p.skills.Level(b.first) + b.second,
                  "and it lifts what it says it lifts");

        // It holds its levels up against the ordinary boost decay, and then
        // wears off.
        const float dt = 1.0f / 60.0f;
        for (int f = 0; f < 60 * 30; ++f) { input.Update(dt); w.Update(dt, ctx); }
        Check(p.Meal() == tea && p.MaxMana() > mana_before, "half a minute in, it is still with you");
        for (const auto& b : tea->dish_levels)
            Check(p.skills.Current(b.first) >= p.skills.Level(b.first) + b.second,
                  "and what it lifted has not drained away");
        p.SetMeal(tea, 0.4f);
        for (int f = 0; f < 60 * 120; ++f) { input.Update(dt); w.Update(dt, ctx); }
        Check(p.Meal() == nullptr && p.MaxMana() == mana_before, "when it wears off, the pool is what it was");
        bool back_down = true;
        for (const auto& b : tea->dish_levels) back_down &= p.skills.Current(b.first) <= p.skills.Level(b.first);
        Check(back_down, "and so are the levels");

        // A dish is worth eating on a full stomach; plain food is not.
        Player full;
        full.Init(ctx, "player_hero");
        full.inventory.Add("cooked_meat", 1);
        full.inventory.Add("hearty_stew", 1);
        Check(!full.Consume(0, why) && !why.empty(), "plain food at full health does nothing, and says so");
        Check(full.Consume(1, why), "a dish at full health is still worth eating");
    }

    Section("ducks and geese, and the one pond they can get into");
    {
        // --- the pond is water, and water is a wall to everything that walks ---
        Map pond;
        Check(pond.Load("maps/fernhollow.mx"), "Fernhollow loads");
        Check(pond.HasWater(), "its pond is marked as water and not as plain collision");
        // The middle of the pond: cell (32, 15) at 32 to the cell.
        const float wet_x = 32 * 32 + 16, wet_y = 15 * 32 + 16;
        Check(pond.InWater(wet_x, wet_y), "the middle of it is water");
        const SDL_FRect boots{wet_x - 8, wet_y - 10, 16, 10};
        Check(pond.Blocked(boots), "and a walker cannot stand there");
        Check(!pond.Blocked(boots, true), "though a swimmer can");
        // The jetty is not water: it is what you fish from.
        Check(!pond.InWater(26 * 32 + 16, 16 * 32 + 8), "the jetty is not water");
        // Every other map's water is a plain wall, so nothing can swim anywhere
        // it was not meant to.
        int watery = 0;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            if (m.HasWater()) ++watery;
        }
        Check(watery == 1, "and it is the only water in the world anything can swim in (" +
              std::to_string(watery) + ")");

        // --- who is on it -------------------------------------------------------
        int ducks = 0, geese = 0;
        bool dry_start = true, roomy = true;
        for (const EnemySpawnDef& e : pond.Enemies()) {
            if (e.type == "duck") ++ducks;
            else if (e.type == "goose") ++geese;
            else continue;
            // Posted on the bank: getting in is something they decide to do.
            if (pond.InWater(e.x, e.y)) dry_start = false;
            // And with a leash long enough that there is water inside it.
            if (e.leash < 120.0f) roomy = false;
        }
        Check(ducks >= 4 && geese >= 2, "there are ducks and geese on it (" +
              std::to_string(ducks) + " and " + std::to_string(geese) + ")");
        Check(dry_start, "each of them starts on dry land");
        Check(roomy, "and is on a long enough leash to reach open water");

        const EnemyDef* drake = enemy_db.Get("duck");
        const EnemyDef* gander = enemy_db.Get("goose");
        Check(drake && drake->swims && gander && gander->swims, "both of them swim");
        Check(drake && drake->aggro_range <= 0.0f && gander && gander->aggro_range <= 0.0f,
              "and neither of them starts anything");
        // Nothing else does. A boar that could cross the pond would cross the
        // sea at the edge of the overworld too.
        int swimmers = 0;
        for (const char* other : {"boar", "hare", "deer", "fox", "lizardman", "orc1",
                                  "chicken", "sheep", "pig", "cow", "frog", "wolf"})
            if (const EnemyDef* d = enemy_db.Get(other)) swimmers += d->swims ? 1 : 0;
        Check(swimmers == 0, "and nothing else in the world does");

        // --- what they actually do ----------------------------------------------
        // The whole of the feature is that they get in of their own accord and
        // come out again. Run the hamlet for five minutes with nobody in it and
        // count what the birds do.
        Input input;
        std::mt19937 rng(4646);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;

        World w;
        w.player.Init(ctx, "player_hero");
        Check(w.LoadMap("fernhollow", "from_trail", ctx), "Fernhollow loads as a world");
        // Out of the way: at aggro 0 they would ignore the player anyway, but a
        // player stood in the middle of them is not what is being measured.
        w.player.x = 2 * 32.0f;
        w.player.y = 34 * 32.0f;

        int in_water = 0, on_land = 0, ever_wet = 0, ever_dry = 0, wettest = 0, wet_total = 0;
        std::set<const Enemy*> swum, walked;
        for (int frame = 0; frame < 60 * 300; ++frame) {
            w.Update(1.0f / 60.0f, ctx);
            if (frame % 30) continue;
            int wet = 0;
            for (const auto& e : w.enemies) {
                if (!e || e->CurrentState() == Enemy::State::Dead) continue;
                if (!e->Def() || !e->Def()->swims) continue;
                if (w.map.InWater(e->x, e->y)) { ++wet; swum.insert(e.get()); }
                else                           { walked.insert(e.get()); }
            }
            wettest = std::max(wettest, wet);
            wet_total += wet;
            if (wet > 0) ++in_water; else ++on_land;
        }
        ever_wet = static_cast<int>(swum.size());
        ever_dry = static_cast<int>(walked.size());
        Check(ever_wet >= 7, "over five minutes most of them go swimming (" +
              std::to_string(ever_wet) + " of " + std::to_string(ducks + geese) + ")");
        Check(ever_dry >= 7, "and most of them are seen out of the water too (" +
              std::to_string(ever_dry) + ")");
        Check(in_water > 400, "there is nearly always somebody on the water (" +
              std::to_string(in_water) + " of " + std::to_string(in_water + on_land) + " moments)");
        Check(wettest >= 4, "and more than one of them at once (" + std::to_string(wettest) + ")");
        // The balance is the thing, not either extreme: a flock that never
        // gets in is scenery, and one that gets in and never comes out is a
        // flock of decoys. Out of nine birds, somewhere between two and seven
        // of them are afloat at any given moment.
        const int afloat_x100 = wet_total * 100 / std::max(1, in_water + on_land);
        Check(afloat_x100 > 200 && afloat_x100 < 700,
              "and about half the flock is on the water at any moment (" +
              std::to_string(afloat_x100 / 100) + "." + std::to_string((afloat_x100 / 10) % 10) +
              " of " + std::to_string(ducks + geese) + ")");

        // Nobody drowned in the scenery: every bird is somewhere it could be,
        // and none of them left the pond's end of the hamlet.
        bool placed = true, homely = true;
        for (const auto& e : w.enemies) {
            if (!e || !e->Def() || !e->Def()->swims) continue;
            SDL_FRect box = e->Bounds();
            if (w.map.Blocked(box, true)) placed = false;
            if (Length(e->x - e->home_x, e->y - e->home_y) > 320.0f) homely = false;
        }
        Check(placed, "none of them ends up inside anything");
        Check(homely, "and none of them wanders off out of the hamlet");

        // A bird that has been swung at comes out after whoever did it, and is
        // drawn walking while it does: the swim clip belongs to the water and
        // not to the bird.
        {
            Enemy* afloat_bird = nullptr;
            for (const auto& e : w.enemies)
                if (e && e->Def() && e->Def()->swims && w.map.InWater(e->x, e->y)) afloat_bird = e.get();
            Check(afloat_bird != nullptr, "one of them is on the water to be bothered");
            if (afloat_bird) {
                Check(afloat_bird->Told().clip == "swim", "and is drawn sitting on it");
                w.player.x = afloat_bird->home_x;
                w.player.y = afloat_bird->home_y;
                afloat_bird->Provoke(0);
                bool came_out = false, walked_dry = true;
                for (int frame = 0; frame < 60 * 25; ++frame) {
                    w.Update(1.0f / 60.0f, ctx);
                    if (!w.map.InWater(afloat_bird->x, afloat_bird->y)) came_out = true;
                    if (came_out && !w.map.InWater(afloat_bird->x, afloat_bird->y) &&
                        afloat_bird->Told().clip == "swim") walked_dry = false;
                }
                Check(came_out, "a duck that has been provoked leaves the water");
                Check(walked_dry, "and is never drawn swimming once it is out of it");
            }
        }

        // A walker still cannot. Drop a hare where the ducks are and it stays
        // on the bank however long it drifts.
        {
            const EnemyDef* hare = enemy_db.Get("hare");
            EnemySpawnDef def;
            def.type = "hare"; def.level = 1; def.leash = 220.0f; def.respawn = 0.0f;
            def.x = 24 * 32 + 16; def.y = 15 * 32 + 16;
            auto e = std::make_unique<Enemy>();
            e->Init(hare, def, ctx);
            Enemy* raw = e.get();
            w.enemies.push_back(std::move(e));
            bool dry = true;
            for (int frame = 0; frame < 60 * 90; ++frame) {
                w.Update(1.0f / 60.0f, ctx);
                if (w.map.InWater(raw->x, raw->y)) { dry = false; break; }
            }
            Check(dry, "a hare beside the same water never gets into it");
        }

        // --- and what comes off them ----------------------------------------------
        for (const char* raw : {"raw_duck", "raw_goose"}) {
            const ItemDef* d = items.Get(raw);
            Check(d && !d->cook_result.empty() && items.Get(d->cook_result),
                  string(raw) + " cooks into something");
        }
        for (const char* who : {"duck", "goose"}) {
            const LootTable* t = loot.Get(who);
            Check(t && !t->always.empty(), string(who) + " leaves supper");
        }
        // The swim clip is a real sheet, and only these two have one.
        int swim_sheets = 0;
        for (const string& id : sprites.Ids())
            if (const SpriteDef* d = sprites.Get(id))
                if (d->Find("swim")) ++swim_sheets;
        Check(swim_sheets == 2, "the drake and the goose are drawn sitting on the water, and nothing else is (" +
              std::to_string(swim_sheets) + ")");
    }

    Section("what each blow trains, and other things that were quietly broken");
    {
        Input input;
        std::mt19937 rng(9090);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        // --- Attack is whether it lands; Strength is how hard ----------------------
        // The two numbers, held apart: raising one must move only its own half of
        // a blow.
        {
            CombatProfile dummy;
            dummy.defence_level = 30; dummy.defence_bonus = 40;
            CombatProfile plain;
            plain.attack_level = 20; plain.strength_level = 20; plain.attack_bonus = 30; plain.strength_bonus = 30;
            CombatProfile accurate = plain;  accurate.attack_level = 70;
            CombatProfile strong = plain;    strong.strength_level = 70;

            Check(HitChance(accurate, dummy) > HitChance(plain, dummy) + 0.15f,
                  "Attack makes a blow likelier to land");
            Check(MaxHit(accurate, 1.0f) == MaxHit(plain, 1.0f),
                  "and does nothing to how hard it can land");
            Check(MaxHit(strong, 1.0f) > MaxHit(plain, 1.0f) * 2,
                  "Strength raises the top of the damage roll");
            Check(std::fabs(HitChance(strong, dummy) - HitChance(plain, dummy)) < 0.0001f,
                  "and does nothing to whether it lands");
        }

        // --- and each is trained by the swing that uses it -------------------------
        {
            Player p;
            p.Init(ctx, "player_hero");
            const auto gained = [&](AttackType type, int skill) {
                const int before = p.skills.Xp(skill);
                p.AwardCombatXp(10, type);
                return p.skills.Xp(skill) - before;
            };
            Check(gained(AttackType::Light, SKILL_ATTACK) == 40, "a light swing trains Attack");
            Check(gained(AttackType::Light, SKILL_STRENGTH) == 0, "and not Strength");
            Check(gained(AttackType::Strong, SKILL_STRENGTH) == 40, "a heavy swing trains Strength");
            Check(gained(AttackType::Strong, SKILL_ATTACK) == 0, "and not Attack");
            const int att0 = p.skills.Xp(SKILL_ATTACK), str0 = p.skills.Xp(SKILL_STRENGTH);
            p.AwardCombatXp(10, AttackType::Charged);
            Check(p.skills.Xp(SKILL_ATTACK) - att0 == 20 && p.skills.Xp(SKILL_STRENGTH) - str0 == 20,
                  "a charged one trains both, half each");
            Check(gained(AttackType::None, SKILL_ATTACK) == 40,
                  "a blow with no swing behind it still teaches something");
            const int hp0 = p.skills.Xp(SKILL_HITPOINTS);
            p.AwardCombatXp(30, AttackType::Strong);
            Check(p.skills.Xp(SKILL_HITPOINTS) > hp0, "and every blow trains Hitpoints");
            // A monster's own worth, which was read from the file and then by nothing.
            const int s0 = p.skills.Xp(SKILL_STRENGTH);
            p.AwardCombatXp(10, AttackType::Strong, 2.0f);
            Check(p.skills.Xp(SKILL_STRENGTH) - s0 == 80, "a monster worth double pays double");
        }

        // --- in a real fight: the whole way from the button to the skill -----------
        // This is the one that was broken. The award was right; the call that
        // made it said "light" whatever had been swung.
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for a fight");
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.facing = FACE_RIGHT;
            w.player.equipment.Equip(SLOT_WEAPON, "bronze_sword");
            const auto frames = [&](int n) {
                for (int f = 0; f < n; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
            };
            // Something to hit that will stand there and take it.
            EnemyDef post = *enemy_db.Get("boar");
            post.hp = 100000; post.aggro_range = 0.0f; post.speed = 0.0f;
            post.defence_level = 1; post.defence_bonus = 0;
            EnemySpawnDef def;
            def.type = "boar"; def.level = 1; def.leash = 10.0f; def.respawn = 0.0f;
            def.x = w.player.x + 26.0f; def.y = w.player.y;
            auto e = std::make_unique<Enemy>();
            e->Init(&post, def, ctx);
            w.enemies.push_back(std::move(e));

            // Pressed and let go the way a real key arrives: after the input's
            // frame has begun and before the world's. A heavy swing goes on the
            // release, and a release the world never sees is a button still held.
            const auto swing = [&](SDL_Keycode k, int times) {
                for (int n = 0; n < times; ++n) {
                    input.Update(1.0f / 60.0f); key(k, true);  w.Update(1.0f / 60.0f, ctx);
                    frames(3);
                    input.Update(1.0f / 60.0f); key(k, false); w.Update(1.0f / 60.0f, ctx);
                    frames(75);                 // let it land and the chain lapse
                }
            };
            int att0 = w.player.skills.Xp(SKILL_ATTACK), str0 = w.player.skills.Xp(SKILL_STRENGTH);
            swing(SDLK_K, 8);
            const int str_from_heavy = w.player.skills.Xp(SKILL_STRENGTH) - str0;
            const int att_from_heavy = w.player.skills.Xp(SKILL_ATTACK) - att0;
            Check(str_from_heavy > 0, "heavy swings that land train Strength (" + std::to_string(str_from_heavy) + " xp)");
            Check(att_from_heavy == 0, "and leave Attack alone (" + std::to_string(att_from_heavy) + ")");

            att0 = w.player.skills.Xp(SKILL_ATTACK); str0 = w.player.skills.Xp(SKILL_STRENGTH);
            // Eight heavy blows have shoved it back out of a light swing's reach,
            // and it has been told to stand still: walk up to it again.
            w.player.x = w.enemies.front()->x - 26.0f;
            w.player.y = w.enemies.front()->y;
            w.player.facing = FACE_RIGHT;
            frames(30);
            const int hp_before_lights = w.enemies.front()->hp;
            swing(SDLK_J, 8);
            Check(w.player.skills.Xp(SKILL_ATTACK) - att0 > 0, "light swings that land train Attack (" +
                  std::to_string(w.player.skills.Xp(SKILL_ATTACK) - att0) + " xp, " +
                  std::to_string(hp_before_lights - w.enemies.front()->hp) + " damage dealt)");
            Check(w.player.skills.Xp(SKILL_STRENGTH) - str0 == 0, "and leave Strength alone (" +
                  std::to_string(w.player.skills.Xp(SKILL_STRENGTH) - str0) + ")");
        }

        // --- a full bag gets nothing, and the tree still falls --------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the axe");
            w.enemies.clear();
            Player& p = w.player;
            p.inventory.Add("bronze_axe", 1);
            // Find a tree anybody can cut, and stand at it.
            const MapObject* tree = nullptr;
            for (const MapObject& o : w.map.Objects())
                if (o.type == "tree" && o.skill_level <= 1 && o.yield == "logs") { tree = &o; break; }
            Check(tree != nullptr, "there is a tree to cut");
            if (tree) {
                // Fill every slot with things that do not stack with a log.
                const char* junk[] = {"bones", "hide", "thread", "raw_meat", "cooked_meat", "marigold", "flax",
                                      "copper_ore", "iron_ore", "coal", "vial", "brookmint", "nettle", "egg",
                                      "milk", "wool", "raw_minnow", "cooked_minnow", "raw_chicken", "feather",
                                      "bolt_cloth", "bronze_bar", "iron_bar", "oak_logs", "raw_boar", "bogbean",
                                      "tinderbox", "bedroll", "lantern", "rope", "steel_bar", "raw_trout"};
                for (const char* j : junk) if (p.inventory.FreeSlots() > 0 && items.Get(j)) p.inventory.Add(j, 1);
                Check(p.inventory.FreeSlots() == 0 && !p.inventory.Has("logs"), "the pack is full, with no logs in it");

                const auto frames = [&](int n) {
                    for (int f = 0; f < n; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
                };
                // Stand where the tree is what the button would act on.
                bool at_it = false;
                for (int dy = 8; dy <= 40 && !at_it; dy += 4)
                    for (int dx = -24; dx <= 24 && !at_it; dx += 8) {
                        p.x = tree->x + dx; p.y = tree->y + dy;
                        frames(2);
                        at_it = p.interact.kind == InteractTarget::Object;
                    }
                Check(at_it, "the tree is in reach");

                const int xp0 = p.skills.Xp(SKILL_WOODCUTTING);
                // Ask to cut it, over and over, for a minute. The swing has to
                // be seen to start, or "it taught nothing" proves nothing.
                int started = 0;
                for (int round = 0; round < 12; ++round) {
                    w.TryInteract(ctx);
                    frames(2);
                    if (w.Gathering()) ++started;
                    frames(60 * 5);
                }
                Check(started >= 6, "the axe is swung at it (" + std::to_string(started) + " times)");
                Check(p.skills.Xp(SKILL_WOODCUTTING) == xp0,
                      "a minute's chopping with a full pack teaches nothing (" +
                      std::to_string(p.skills.Xp(SKILL_WOODCUTTING) - xp0) + " xp)");

                // With room, it is paid for as it always was.
                p.inventory.Remove("bones", 1);
                for (int round = 0; round < 4 && !p.inventory.Has("logs"); ++round) {
                    if (!w.Gathering()) w.TryInteract(ctx);
                    frames(60 * 5);
                }
                Check(p.inventory.Has("logs") && p.skills.Xp(SKILL_WOODCUTTING) > xp0,
                      "and with room for the log, the log and the experience both come");
            }
        }

        // --- the Cross Cut costs what it costs ---------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("overworld", "start", ctx), "the overworld loads for the Cross Cut");
            w.enemies.clear();
            w.player.y -= 200.0f;
            w.player.equipment.Equip(SLOT_WEAPON, "bronze_sword");
            const auto frames = [&](int n) {
                for (int f = 0; f < n; ++f) { input.Update(1.0f / 60.0f); w.Update(1.0f / 60.0f, ctx); }
            };
            const auto both = [&]() {
                input.Update(1.0f / 60.0f);
                key(SDLK_J, true); key(SDLK_K, true);
                w.Update(1.0f / 60.0f, ctx);
                frames(2);
                const bool cut = w.player.Attack().move == ComboMove::CrossCut;
                key(SDLK_J, false); key(SDLK_K, false);
                return cut;
            };
            Check(both(), "both buttons together are a Cross Cut, on a full bar");
            frames(120);
            w.player.SetStamina(10.0f);
            Check(!both(), "but not on ten points of breath: it costs twenty-five");
            frames(120);
            w.player.SetStamina(CROSS_CUT_STAMINA);
            Check(both(), "and exactly enough is enough");
        }

        // --- a healer mends you and leaves your potions alone ------------------------
        {
            Skills s;
            LevelUp up;
            s.AddXp(SKILL_STRENGTH, XpForLevel(40), up);
            s.AddXp(SKILL_ATTACK, XpForLevel(40), up);
            s.SetCurrent(SKILL_STRENGTH, 48);     // an elixir
            s.SetCurrent(SKILL_ATTACK, 31);       // something that drained it
            s.RestoreDrained();
            Check(s.Current(SKILL_STRENGTH) == 48, "being healed leaves a potion's boost where it was");
            Check(s.Current(SKILL_ATTACK) == 40, "and puts back what had been drained");
            s.ResetCurrent();
            Check(s.Current(SKILL_STRENGTH) == 40, "dying still costs the boost, as it always did");
        }

        // --- saves: backups, damage, deleting -- nowhere near a real one -------------
        {
            namespace fs = std::filesystem;
            const string was = SaveSystem::Directory();
            const fs::path dir = fs::temp_directory_path() / "dreamquest_selftest_saves";
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
            SaveSystem::SetDirectory(dir.string());
            Check(SaveSystem::SlotPath(1).find("dreamquest_selftest_saves") != string::npos,
                  "the save tests are pointed away from the real saves");

            World w;
            w.player.Init(ctx, "player_hero");
            w.LoadMap("town_havenbrook", "default", ctx);
            QuestLog log;
            log.LoadDefinitions("data/quests.json");

            Check(!SaveSystem::Occupied(2) && !SaveSystem::Peek(2).exists && !SaveSystem::Peek(2).damaged,
                  "an empty slot is empty");
            Check(SaveSystem::Save(2, w, log, 60.0f), "a save is written");
            Check(SaveSystem::Peek(2).exists && !fs::exists(SaveSystem::BackupPath(2)),
                  "the first save has nothing to back up");
            w.player.inventory.Add("coins", 500);
            Check(SaveSystem::Save(2, w, log, 120.0f), "and a second");
            Check(fs::exists(SaveSystem::BackupPath(2)), "which keeps the first beside it as a backup");

            // The slot's file goes bad.
            { std::ofstream bad(SaveSystem::SlotPath(2), std::ios::trunc); bad << "{ \"version\": 2, \"player\": {"; }
            SaveSlotInfo info = SaveSystem::Peek(2);
            Check(info.exists && info.from_backup && !info.damaged,
                  "a slot whose file cannot be read is shown from its backup");
            {
                World back; back.player.Init(ctx, "player_hero");
                QuestLog log2; log2.LoadDefinitions("data/quests.json");
                float played = 0.0f; bool from_backup = false;
                Check(SaveSystem::Load(2, back, log2, ctx, played, &from_backup) && from_backup,
                      "and loads from it, and says so");
                Check(std::fabs(played - 60.0f) < 0.5f, "it is the save before the last one");
            }
            // Saving over a bad file must not make the bad file the backup.
            Check(SaveSystem::Save(2, w, log, 180.0f), "saving over the damaged file works");
            {
                std::ifstream in(SaveSystem::BackupPath(2));
                json j; bool ok = true;
                try { in >> j; } catch (...) { ok = false; }
                Check(ok && j.is_object(), "and the good backup was not replaced with the bad file");
            }

            // Both gone bad: damaged, and treated as occupied.
            { std::ofstream bad(SaveSystem::SlotPath(2), std::ios::trunc); bad << "not a save"; }
            { std::ofstream bad(SaveSystem::BackupPath(2), std::ios::trunc); bad << "nor this"; }
            info = SaveSystem::Peek(2);
            Check(info.damaged && !info.exists, "a slot nothing can read is damaged, not empty");
            Check(SaveSystem::Occupied(2), "and counts as occupied, so a new game there has to ask first");

            // Deleting puts it aside rather than destroying it.
            Check(SaveSystem::Save(3, w, log, 30.0f) && SaveSystem::Save(3, w, log, 40.0f), "a slot to delete");
            Check(SaveSystem::Delete(3), "a slot can be deleted");
            Check(!SaveSystem::Occupied(3) && !SaveSystem::Peek(3).exists, "and is then empty");
            Check(fs::exists(SaveSystem::DeletedPath(3)), "with what was in it put aside, not destroyed");
            Check(!SaveSystem::Delete(3), "and there is nothing to delete twice");

            SaveSystem::SetDirectory(was);
            fs::remove_all(dir, ec);
            Check(SaveSystem::Directory() == "saves", "and the real saves are where they were");
        }

        // --- the interface at a size ------------------------------------------------
        // Text is measured in the layout's units whatever size it is set at, so a
        // panel that fits at 100% is laid out the same at 150%.
        {
            UI probe;
            // No renderer: nothing can be drawn, but the arithmetic can be asked for.
            Check(std::fabs(probe.Scale() - 1.0f) < 0.001f, "the interface starts at its own size");
            probe.SetScale(1.25f);
            Check(std::fabs(probe.Scale() - 1.25f) < 0.001f, "and can be made larger");
            probe.SetScale(9.0f);
            Check(probe.Scale() <= 2.0f, "within reason");
            probe.SetScale(0.2f);
            Check(probe.Scale() >= 1.0f, "and never smaller than it was designed at");
        }
    }

    Section("a lesson is a quest, not a button");
    {
        // Three people used to hand out a couple of hundred experience every
        // time they were asked how something was done, and could be asked for
        // as long as anybody cared to keep asking. Each is a tutorial quest
        // now: asked for once, done with your hands, paid for on the way back.

        // --- no conversation anywhere gives experience ---------------------------
        // Checked in the file itself, because the engine no longer reads the
        // field at all and a test of the engine would pass whatever was in it.
        {
            std::ifstream in("data/dialogue.json");
            json root;
            in >> root;
            int xp_lines = 0;
            string where;
            for (auto n = root.begin(); n != root.end(); ++n)
                if (n.value().contains("options"))
                    for (const json& o : n.value()["options"])
                        if (o.contains("action") && (o["action"].contains("xp") || o["action"].contains("xp_skill"))) {
                            ++xp_lines;
                            where = n.key();
                        }
            Check(xp_lines == 0, "no line of dialogue hands out experience" +
                  (where.empty() ? string() : ": " + where));
        }

        struct Lesson {
            const char* quest; const char* npc; const char* root; int skill;
            ObjectiveType doing; const char* target; int count;
        };
        const Lesson lessons[] = {
            {"q_learn_fighting", "npc_guard",  "guard_root",  SKILL_ATTACK,   ObjectiveType::Kill,  "boar",        3},
            {"q_learn_crafting", "npc_smith",  "smith_root",  SKILL_CRAFTING, ObjectiveType::Craft, "wood_helm",   1},
            {"q_learn_cooking",  "npc_hunter", "hunter_root", SKILL_COOKING,  ObjectiveType::Craft, "cooked_meat", 3},
        };

        for (const Lesson& L : lessons) {
            const QuestDef* def = quests.Definition(L.quest);
            Check(def != nullptr, string(L.quest) + " exists");
            if (!def) continue;
            const string name = def->name;
            Check(def->tutorial, name + " is filed with the tutorials");
            Check(def->giver == L.npc && def->source == QuestSource::Npc, name + " is theirs to give");
            Check(!def->daily, name + " is not a daily: it is done once");
            Check(def->stages.size() == 2, name + " is the doing and the coming back");
            if (def->stages.size() != 2) continue;
            Check(def->stages[0].type == L.doing && def->stages[0].target == L.target &&
                  def->stages[0].count == L.count, name + " asks for the skill to be used");
            Check(def->stages[1].type == ObjectiveType::Talk && def->stages[1].target == L.npc,
                  name + " ends back where it started");
            Check(def->rewards.xp.count(L.skill) && def->rewards.xp.at(L.skill) >= 150,
                  name + " pays in the skill it teaches");

            // ---- played through, with every attempt to get paid twice ----------
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            std::set<string> flags;
            DialogueContext dc;
            dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags;
            dc.npc = L.npc;

            // Picks the first visible option that leads to `next`, applies what
            // it does through the same function the game uses, and says
            // whether there was such an option.
            const auto pick = [&](DialogueRunner& r, const string& next) {
                for (size_t i = 0; i < r.VisibleOptions().size(); ++i)
                    if (r.VisibleOptions()[i]->next == next) {
                        r.MoveSelection(static_cast<int>(i) - r.Selected());
                        r.Choose(dc);
                        for (const DialogueAction& a : r.TakeActions())
                            ApplyDialogueAction(a, log, inv, sk, L.npc);
                        return true;
                    }
                return false;
            };
            // The option on the root that starts the lesson: the one whose
            // condition is "this quest is available".
            const auto offer_of = [&](DialogueRunner& r) -> string {
                for (const DialogueOption* o : r.VisibleOptions())
                    if (o->condition.quest == L.quest && o->condition.quest_state == "available") return o->next;
                return string();
            };
            const auto line_for = [&](DialogueRunner& r, const string& state) -> string {
                for (const DialogueOption* o : r.VisibleOptions())
                    if (o->condition.quest == L.quest && o->condition.quest_state == state) return o->next;
                return string();
            };

            DialogueRunner first;
            first.Begin(&dialogue, L.root, L.npc, "them", dc);
            const string offer = offer_of(first);
            Check(!offer.empty(), name + ": they offer the lesson to somebody who has not had it");
            const int xp_before = sk.Xp(L.skill);
            Check(pick(first, offer), name + ": the lesson can be asked for");
            Check(sk.Xp(L.skill) == xp_before, name + ": asking teaches nothing by itself");

            // Accept: whichever option on the lesson's node starts the quest.
            string accept;
            for (const DialogueOption* o : first.VisibleOptions())
                if (o->action.start_quest == L.quest) accept = o->next;
            Check(!accept.empty(), name + ": the lesson ends in a quest");
            const int held_before = inv.SlotCount() - inv.FreeSlots();
            pick(first, accept);
            Check(log.IsActive(L.quest), name + ": and taking it starts it");
            const int held_after = inv.SlotCount() - inv.FreeSlots();

            // Asked again, straight away: no offer, nothing handed over, no XP.
            for (int again = 0; again < 5; ++again) {
                DialogueRunner r;
                r.Begin(&dialogue, L.root, L.npc, "them", dc);
                Check(offer_of(r).empty(), name + ": the lesson is not offered twice");
                Check(!line_for(r, "active").empty(), name + ": they ask how it is going instead");
                // Walk every line they will say while it is under way.
                pick(r, line_for(r, "active"));
                for (int deeper = 0; deeper < 4 && r.Active(); ++deeper) {
                    if (r.VisibleOptions().empty()) break;
                    r.Choose(dc);
                    for (const DialogueAction& a : r.TakeActions()) ApplyDialogueAction(a, log, inv, sk, L.npc);
                }
            }
            Check(sk.Xp(L.skill) == xp_before, name + ": asking five more times is still worth nothing");
            Check(inv.SlotCount() - inv.FreeSlots() == held_after,
                  name + ": and nothing more is handed over for asking");
            (void)held_before;

            // Coming back early does not finish it.
            {
                DialogueRunner r;
                r.Begin(&dialogue, L.root, L.npc, "them", dc);
                bool can_hand_in = false;
                for (const DialogueOption* o : r.VisibleOptions()) can_hand_in |= o->action.advance_quest == L.npc;
                Check(!can_hand_in, name + ": it cannot be handed in before it is done");
            }

            // Do the thing.
            QuestEvent did;
            did.type = L.doing; did.target = L.target; did.amount = 1;
            for (int n = 0; n < L.count; ++n) log.Notify(did, inv);
            Check(log.IsActive(L.quest) && log.Stage(L.quest) == 1, name + ": doing it moves it on to the walk back");

            // And hand it in.
            {
                DialogueRunner r;
                r.Begin(&dialogue, L.root, L.npc, "them", dc);
                string done;
                for (const DialogueOption* o : r.VisibleOptions()) if (o->action.advance_quest == L.npc) done = o->next;
                Check(!done.empty(), name + ": they will hear about it now");
                pick(r, done);
            }
            Check(log.IsComplete(L.quest), name + ": and telling them finishes it");

            // Finished, the lesson is still there to be read -- and is only words.
            for (int again = 0; again < 3; ++again) {
                DialogueRunner r;
                r.Begin(&dialogue, L.root, L.npc, "them", dc);
                Check(offer_of(r).empty(), name + ": it is never offered again");
                bool any_action = false;
                const string after = line_for(r, "complete");
                Check(!after.empty(), name + ": the lesson can still be asked about");
                pick(r, after);
                for (const DialogueOption* o : r.VisibleOptions())
                    any_action |= !o->action.start_quest.empty() || !o->action.gives.empty() ||
                                  !o->action.advance_quest.empty();
                Check(!any_action, name + ": and asking about it afterwards does nothing but talk");
            }
            Check(log.Completions(L.quest) == 1, name + ": it was completed exactly once");
        }

        // --- the rule about gifts, at the place that keeps it ----------------------
        {
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Skills sk;
            Inventory inv(&items);
            DialogueAction gift;
            gift.start_quest = "q_learn_cooking";
            gift.gives = {{"raw_meat", 4}};
            DialogueOutcome out = ApplyDialogueAction(gift, log, inv, sk, "npc_hunter");
            Check(out.quest_started && inv.Count("raw_meat") == 4, "a lesson hands over what it is learnt on");
            for (int again = 0; again < 10; ++again) out = ApplyDialogueAction(gift, log, inv, sk, "npc_hunter");
            Check(!out.quest_started && inv.Count("raw_meat") == 4,
                  "and the same action ten more times hands over nothing: a gift, not a tap");

            // Several things in one hand.
            DialogueAction both;
            both.start_quest = "q_learn_crafting";
            both.gives = {{"logs", 2}, {"hide", 1}};
            ApplyDialogueAction(both, log, inv, sk, "npc_smith");
            Check(inv.Count("logs") == 2 && inv.Count("hide") == 1, "Halda hands over the logs and the hide together");

            // A gift with no quest behind it is still a gift.
            DialogueAction plain;
            plain.gives = {{"herbal_tonic", 1}};
            ApplyDialogueAction(plain, log, inv, sk, "npc_oona");
            Check(inv.Count("herbal_tonic") == 1, "something given with no quest attached is simply given");
        }

        // --- "I lost it" is said once ----------------------------------------------
        // The lent axe, the lent pick, the lent rod and Oona's tonic were each
        // replaced whenever they were missing -- and a forge pays forty coins
        // for a bronze axe. Sell it, ask, sell it, ask. One replacement now,
        // remembered by the world, and after that the line is gone.
        {
            struct Lent { const char* quest; const char* npc; const char* waiting; const char* item; };
            const Lent lent[] = {
                {"q_learn_woodcutting", "npc_sawyer",     "sawyer_waiting", "bronze_axe"},
                {"q_learn_mining",      "npc_pitmaster",  "pit_waiting",    "bronze_pickaxe"},
                {"q_learn_fishing",     "npc_angler",     "angler_waiting", "fishing_rod"},
                {"q_word_to_fernhollow","npc_oona",       "oona_waiting",   "herbal_tonic"},
            };
            for (const Lent& L : lent) {
                const DialogueNode* node = dialogue.Get(L.waiting);
                Check(node != nullptr, string(L.waiting) + " exists");
                if (!node) continue;
                QuestLog log;
                log.LoadDefinitions("data/quests.json");
                Skills sk;
                Inventory inv(&items);
                std::set<string> flags;
                DialogueContext dc;
                dc.quests = &log; dc.inventory = &inv; dc.skills = &sk; dc.flags = &flags; dc.npc = L.npc;

                // Sell it and ask again, twenty times over.
                int handed = 0;
                for (int round = 0; round < 20; ++round) {
                    inv.Remove(L.item, inv.Count(L.item));          // "sold"
                    DialogueRunner r;
                    r.Begin(&dialogue, L.waiting, L.npc, "them", dc);
                    for (size_t i = 0; i < r.VisibleOptions().size(); ++i) {
                        const DialogueOption* o = r.VisibleOptions()[i];
                        bool gives_it = false;
                        for (const auto& g : o->action.gives) gives_it |= g.first == L.item;
                        if (!gives_it) continue;
                        r.MoveSelection(static_cast<int>(i) - r.Selected());
                        r.Choose(dc);
                        for (const DialogueAction& a : r.TakeActions()) {
                            const DialogueOutcome out = ApplyDialogueAction(a, log, inv, sk, L.npc);
                            for (const string& f : out.flags) flags.insert(f);
                            for (const auto& got : out.received) if (got.first == L.item) handed += got.second;
                        }
                        break;
                    }
                }
                Check(handed == 1, string(L.item) + " is replaced once, however often it goes missing (" +
                      std::to_string(handed) + ")");

                // And it is not offered at all to somebody who still has theirs.
                inv.Add(L.item, 1);
                std::set<string> fresh;
                dc.flags = &fresh;
                DialogueRunner r;
                r.Begin(&dialogue, L.waiting, L.npc, "them", dc);
                bool offered = false;
                for (const DialogueOption* o : r.VisibleOptions())
                    for (const auto& g : o->action.gives) offered |= g.first == L.item;
                Check(!offered, string(L.item) + " is not replaced while it is still in the bag");
            }

            // Nothing anywhere hands something over with neither a quest nor a
            // memory behind it: every give is a quest's, or is said once.
            std::ifstream in("data/dialogue.json");
            json root;
            in >> root;
            string tap;
            for (auto n = root.begin(); n != root.end(); ++n)
                if (n.value().contains("options"))
                    for (const json& o : n.value()["options"]) {
                        if (!o.contains("action")) continue;
                        const json& a = o["action"];
                        if (!a.contains("give") && !a.contains("gives")) continue;
                        const bool with_quest = a.contains("start_quest");
                        const bool remembered = a.contains("set_flag") && o.contains("if") &&
                                                o["if"].value("no_flag", string()) == a.value("set_flag", string());
                        // Oona's tonic at the start of her errand is the quest's.
                        if (!with_quest && !remembered && tap.empty()) tap = n.key();
                    }
            Check(tap.empty(), "every line that hands something over is a quest's to give, or is given once" +
                  (tap.empty() ? string() : ": " + tap));
        }

        // --- what the lessons ask for can be done ----------------------------------
        {
            const ItemDef* helm = nullptr;
            for (const ItemDef* r : items.Recipes()) if (r->craft_result == "wood_helm") helm = r;
            Check(helm && items.StationFor(*helm) == CraftStation::Workbench && helm->craft_level <= 1,
                  "a Barkwood Helm is made at a workbench by somebody who has never made anything");
            Check(helm && helm->craft_inputs.size() == 2 && helm->craft_inputs.count("logs") &&
                  helm->craft_inputs.at("logs") == 2 && helm->craft_inputs.count("hide") &&
                  helm->craft_inputs.at("hide") == 1, "out of exactly what Halda hands over");
            const ItemDef* cooked = nullptr;
            for (const ItemDef* r : items.Recipes(CraftStation::Range))
                if (r->craft_result == "cooked_meat") cooked = r;
            Check(cooked && cooked->craft_level <= 1 && cooked->craft_inputs.count("raw_meat"),
                  "meat is cooked at a fire by somebody who has never cooked, out of what Ivo hands over");
            const EnemyDef* boar = enemy_db.Get("boar");
            Check(boar && boar->kill_target == "boar", "and a boar counts as a boar");

            // A new character's three cooked meat are not the lesson.
            QuestLog log;
            log.LoadDefinitions("data/quests.json");
            Inventory inv(&items);
            inv.Add("cooked_meat", 3);
            log.Start("q_learn_cooking");
            log.RefreshCollectObjectives(inv);
            Check(log.Stage("q_learn_cooking") == 0 && log.Counter("q_learn_cooking") == 0,
                  "meat already in the bag is not meat cooked: the lesson counts the cooking");

            // Somewhere to do each of them is on the way.
            WaypointIndex index;
            Check(index.Load("data/waypoints.json"), "the waypoint index loads");
            for (const char* id : {"q_learn_crafting", "q_learn_cooking"}) {
                const QuestDef* d = quests.Definition(id);
                if (!d) continue;
                const auto spots = index.SpotsFor(d->stages[0], 0, &enemy_db, &loot, &items);
                bool in_town = false;
                for (const auto& s : spots) in_town |= s.map == "town_havenbrook" || s.map == "house_inn";
                Check(!spots.empty() && in_town, d->name + " points at somewhere in Havenbrook to do it (" +
                      std::to_string(spots.size()) + " places)");
            }
        }
    }

    Section("what the menus say a thing is worth");
    {
        // The panels at a shop and an anvil now print an item's bonuses and the
        // change against what is worn. They read the item's own fields, so what
        // is worth testing is the two places that could lie: a boost, whose
        // printed number is worked out rather than stored, and the promise that
        // an equippable thing has something to print at all.

        // --- a potion says what it will actually do -----------------------------
        // The panel prints ItemDef::BoostGain; drinking it applies the same.
        // If those ever drift, a potion sold as "+10 Strength" gives eight.
        {
            Input input;
            std::mt19937 rng(77);
            GameContext ctx;
            ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
            ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
            ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
            ctx.input = &input;       ctx.rng = &rng;

            int tested = 0;
            for (const auto& kv : items.All()) {
                const ItemDef& d = kv.second;
                if (d.boosts.empty()) continue;
                World w;
                w.player.Init(ctx, "player_hero");
                Player& p = w.player;
                LevelUp up;
                for (const auto& b : d.boosts) p.skills.AddXp(b.first, XpForLevel(60), up);
                p.SyncHitpoints(); p.hp = p.max_hp;
                p.inventory.Add(d.id, 1);
                int slot = -1;
                for (int k = 0; k < p.inventory.SlotCount(); ++k)
                    if (p.inventory.Slot(k).id == d.id) { slot = k; break; }
                if (slot < 0) continue;
                // What the panel would print, before the cork comes out.
                std::map<int, int> promised;
                for (const auto& b : d.boosts)
                    promised[b.first] = ItemDef::BoostGain(b.second, p.skills.Level(b.first));
                std::map<int, int> before;
                for (const auto& b : d.boosts) before[b.first] = p.skills.Current(b.first);
                string why;
                Check(p.Consume(slot, why), d.name + " can be drunk" + (why.empty() ? string() : ": " + why));
                for (const auto& b : d.boosts) {
                    const int got = p.skills.Current(b.first) - before[b.first];
                    Check(got == promised[b.first],
                          d.name + " gives the " + std::to_string(promised[b.first]) + " " +
                          SkillName(b.first) + " the panel promises (" + std::to_string(got) + ")");
                }
                ++tested;
            }
            Check(tested >= 5, "every potion that boosts was checked (" + std::to_string(tested) + ")");
        }

        // --- which rows a stat block has -------------------------------------
        // The rules the panels follow, which are worth holding still: a stat
        // that is nothing on both pieces is left off, one the worn piece has
        // and this one does not is kept, and an empty slot shows the whole
        // bonus as the gain it is.
        {
            const auto row = [](const vector<ItemStat>& rows, const string& label) -> const ItemStat* {
                for (const ItemStat& r : rows) if (r.label == label) return &r;
                return nullptr;
            };
            ItemDef plain;
            plain.id = "test_helm";
            plain.slot = SLOT_HEAD;
            plain.defence_bonus = 10;

            // Nothing worn there: the whole of it is the gain.
            vector<ItemStat> rows = ItemStatLines(plain, nullptr, true);
            Check(rows.size() == 1 && row(rows, "Defence"), "a plain helm is one row, not five zeroes");
            Check(row(rows, "Defence") && row(rows, "Defence")->value == "+10" &&
                  row(rows, "Defence")->delta == "(+10)" && row(rows, "Defence")->verdict > 0,
                  "against an empty slot the whole bonus reads as the gain");
            Check(!row(rows, "Attack") && !row(rows, "Magic"),
                  "and the stats it does not have are left off");

            // Against itself: every change is nothing.
            rows = ItemStatLines(plain, &plain, true);
            Check(row(rows, "Defence") && row(rows, "Defence")->delta == "( -- )" &&
                  row(rows, "Defence")->verdict == 0, "a piece weighed against itself changes nothing");

            // Against something better, and something it does not have.
            ItemDef better;
            better.id = "test_better";
            better.slot = SLOT_HEAD;
            better.defence_bonus = 22;
            better.magic_bonus = 4;
            rows = ItemStatLines(plain, &better, true);
            Check(row(rows, "Defence") && row(rows, "Defence")->delta == "(-12)" &&
                  row(rows, "Defence")->verdict < 0, "a worse piece says so, in the minus");
            Check(row(rows, "Magic") && row(rows, "Magic")->value == "+0" &&
                  row(rows, "Magic")->delta == "(-4)" && row(rows, "Magic")->verdict < 0,
                  "and a stat you would lose is shown even though this piece has none of it");

            // With no comparison at all -- a potion is not instead of anything.
            rows = ItemStatLines(better, nullptr, false);
            Check(!rows.empty() && rows[0].delta.empty(),
                  "with nothing to compare against there is no change column");

            // A faster weapon reads as faster, though its stored number is smaller.
            ItemDef quick, slow;
            quick.id = "test_dagger"; quick.slot = SLOT_WEAPON; quick.attack_speed = 0.75f;
            slow.id = "test_maul";    slow.slot = SLOT_WEAPON;  slow.attack_speed = 1.20f;
            rows = ItemStatLines(quick, &slow, true);
            Check(row(rows, "Swing speed") && row(rows, "Swing speed")->verdict > 0,
                  "a quicker weapon is better, although the number stored for it is smaller");
            rows = ItemStatLines(slow, &quick, true);
            Check(row(rows, "Swing speed") && row(rows, "Swing speed")->verdict < 0,
                  "and a slower one is worse");

            // Every real piece in the game produces at least one row, so no
            // card is ever drawn empty.
            int mute = 0;
            string first;
            for (const auto& kv : items.All()) {
                if (kv.second.slot == SLOT_NONE) continue;
                if (ItemStatLines(kv.second, nullptr, true).empty()) {
                    ++mute;
                    if (first.empty()) first = kv.first;
                }
            }
            Check(mute == 0, "every piece that can be worn makes at least one row" +
                  (first.empty() ? string() : ": " + first));
        }

        // --- everything worn has something to say for itself ----------------------
        // A piece with no bonus, no block, no speed and no light draws an empty
        // stat block, which reads as a bug rather than as a plain item.
        {
            int worn = 0;
            string mute;
            for (const auto& kv : items.All()) {
                const ItemDef& d = kv.second;
                if (d.slot == SLOT_NONE) continue;
                ++worn;
                const bool says_something =
                    d.attack_bonus || d.strength_bonus || d.defence_bonus ||
                    d.ranged_bonus || d.magic_bonus ||
                    d.block > 0.0f || d.move_speed != 0.0f || d.light_radius > 0.0f ||
                    fabsf(d.attack_speed - 1.0f) > 0.005f || !d.passive.empty();
                if (!says_something && mute.empty()) mute = d.id;
            }
            Check(worn > 100, "there is a wardrobe to check (" + std::to_string(worn) + ")");
            Check(mute.empty(), "every piece that can be worn has a number to show for it" +
                  (mute.empty() ? string() : ": " + mute));
        }
    }

    Section("a clothier, a farm, and frogs in the mire");
    {
        // --- Wynn's, at Mossvale -----------------------------------------------------------
        // A house of her own now, up the lane from the square, and not a stall on it.
        Map moss;
        Check(moss.Load("maps/mossvale_weavers.mx"), "Wynn's house in Mossvale loads");
        const NpcDef* wynn = nullptr;
        for (const NpcDef& n : moss.Npcs()) if (n.id == "npc_wynn") wynn = &n;
        Check(wynn && wynn->shop == "mossvale_clothier", "Wynn the Clothier keeps a shop in Mossvale");
        bool has_loom = false;
        for (const MapObject& o : moss.Objects())
            has_loom |= o.id == "loom_weaver" && o.station == "loom" &&
                        o.sprite == "assets/props/loom.png";
        Check(has_loom, "with her loom in it, and no bench standing in for one");

        ShopDatabase shed;
        Check(shed.Load("data/shops.json"), "the shops load");
        const ShopDef* shop = shed.Get("mossvale_clothier");
        Check(shop && shop->town == "mossvale" && shop->buys.count("cloth") && shop->buys.count("fibre"),
              "she buys cloth and what cloth is made of");

        vector<const QuestDef*> orders;
        for (const auto& kv : quests.Definitions()) if (kv.second.giver == "npc_wynn") orders.push_back(&kv.second);
        Check(orders.size() >= 8, "her book has orders in it (" + std::to_string(orders.size()) + ")");
        int robes = 0, hats = 0, skirts = 0, lowest = 99, highest = 0;
        for (const QuestDef* d : orders) {
            Check(d->daily && d->pool == "wynn_orders", d->name + " is a daily order of hers");
            if (d->stages.empty()) continue;
            const string& what = d->stages[0].target;
            robes += what.find("_robe_body") != string::npos;
            hats += what.find("_robe_head") != string::npos;
            skirts += what.find("_robe_legs") != string::npos;
            const ItemDef* recipe = nullptr;
            for (const ItemDef* r : items.Recipes())
                if (r->craft_result == what && (!recipe || r->craft_level < recipe->craft_level)) recipe = r;
            Check(recipe != nullptr, d->name + " asks for " + what + ", which somebody can make");
            if (!recipe) continue;
            const auto needs = d->requirements.find(SKILL_CRAFTING);
            const int asked = needs == d->requirements.end() ? 1 : needs->second;
            Check(asked == recipe->craft_level, d->name + " asks the Crafting its recipe asks");
            Check(d->rewards.xp.count(SKILL_CRAFTING), d->name + " pays in Crafting");
            lowest = std::min(lowest, asked);
            highest = std::max(highest, asked);
            // What she orders is a robe, a hat, a skirt or the cloth they are
            // made of: she is the mage's tailor, and the tannery has the hides.
            const ItemDef* made = items.Get(what);
            Check(what == "bolt_cloth" || (made && made->armour_cut == "robe"),
                  d->name + " is the mage's, not the ranger's");
        }
        Check(robes >= 4 && hats >= 2 && skirts >= 1, "robes, hats and skirts, which is what a mage wears");
        Check(lowest == 1 && highest >= 20, "work in it from the first bolt to the upper sets");

        const DialogueNode* root = dialogue.Get("wynn_root");
        bool book = false, sells = false, hands_in = false;
        if (root)
            for (const DialogueOption& o : root->options) {
                book |= o.action.open_orders == "npc_wynn";
                sells |= o.action.open_shop == "mossvale_clothier";
                hands_in |= o.action.hand_in;
            }
        Check(root && book && sells && hands_in, "and she will show the book, the shelf and take an order in");

        // --- the farm at Havenbrook ------------------------------------------------------
        Map town;
        Check(town.Load("maps/town_havenbrook.mx"), "Havenbrook loads");
        Check(town.Width() > 56 * 32, "the town is bigger than it was (" + std::to_string(static_cast<int>(town.Width() / 32)) + " cells across)");
        std::map<string, int> pens;
        for (const EnemySpawnDef& e : town.Enemies()) pens[e.type] += 1;
        for (const char* beast : {"chicken", "sheep", "pig", "cow"}) {
            Check(pens[beast] >= 3, string("there are ") + beast + "s in the farm's pens (" + std::to_string(pens[beast]) + ")");
            const EnemyDef* d = enemy_db.Get(beast);
            Check(d != nullptr, string(beast) + " is a real animal");
            if (!d) continue;
            // Nothing on a farm fights: they are stock, not monsters.
            Check(d->aggro_range <= 0.0f, d->name + " does not come for anybody");
            Check(!d->is_boss && d->hp >= 4, d->name + " is worth killing on purpose and not by accident");
            const LootTable* t = loot.Get(d->loot_table);
            Check(t && !t->always.empty(), d->name + " leaves something");
            bool food = false;
            if (t) for (const LootEntry& row : t->always) {
                const ItemDef* item = items.Get(row.item);
                food |= item && (item->consumable || !item->cook_result.empty() || row.item == "wool" || row.item == "hide");
            }
            Check(food, d->name + " leaves supper, a fleece or a hide");
        }
        const NpcDef* marrow = nullptr;
        for (const NpcDef& n : town.Npcs()) if (n.id == "npc_marrow") marrow = &n;
        Check(marrow != nullptr, "and a farmer stands in the yard");
        Check(dialogue.Get("marrow_root") != nullptr, "with something to say");

        // The fleece is the loom's: what the farm gives, the clothier uses.
        const ItemDef* fleece = items.Get("wool");
        bool fleece_to_cloth = false;
        for (const ItemDef* r : items.Recipes())
            fleece_to_cloth |= r->craft_result == "bolt_cloth" && r->craft_inputs.count("wool");
        Check(fleece && fleece_to_cloth, "a fleece off the farm spins into cloth for the shed");

        // And what the farm gives, the fire cooks.
        for (const char* raw : {"raw_chicken", "raw_mutton", "raw_pork", "raw_beef", "raw_frog_legs"}) {
            const ItemDef* d = items.Get(raw);
            Check(d && !d->cook_result.empty() && items.Get(d->cook_result), string(raw) + " cooks into something");
        }

        // --- frogs in the mire -----------------------------------------------------------
        Map field;
        Check(field.Load("maps/overworld.mx"), "the Hollowmarch loads");
        int frogs = 0;
        for (const EnemySpawnDef& e : field.Enemies()) frogs += e.type == "frog";
        Check(frogs >= 5, "there are frogs in the mire (" + std::to_string(frogs) + ")");
        const EnemyDef* frog = enemy_db.Get("frog");
        Check(frog && frog->aggro_range <= 0.0f, "and they sit there: a frog does not come for anybody");
        Check(sprites.Has("frog") && sprites.Has("cow") && sprites.Has("sheep") && sprites.Has("pig") &&
              sprites.Has("chicken"), "every one of them is drawn");
    }

    Section("what the same review changed about playing it");
    {
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        GameContext rctx;
        std::mt19937 rng(23);
        rctx.sprites = &sprites; rctx.items = &items; rctx.enemies = &enemy_db;
        rctx.quests = &log; rctx.rng = &rng;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- the quick item, and the wait between mouthfuls -------------------------------
        {
            World w;
            w.player.Init(rctx, "player_hero");
            w.player.inventory.SetDatabase(&items);
            Check(w.LoadMap("town_havenbrook", "default", rctx), "somewhere quiet to eat");
            Player& p = w.player;
            p.inventory.Clear();
            p.SetQuickItem("");
            string why;
            Check(!p.UseQuickItem(why) && !why.empty(), "with nothing to eat, it says so");

            p.inventory.Add("logs", 3);
            p.inventory.Add("cooked_meat", 3);
            p.inventory.Add("mana_tonic", 2);
            p.inventory.Add("cooked_trout", 2);
            Check(p.QuickChoices() == vector<string>({"cooked_meat", "mana_tonic", "cooked_trout"}),
                  "what could be the quick item is what can be eaten or drunk, in the order carried, and not the logs");

            p.Damage(std::max(1, p.hp - 2));
            const int hurt = p.hp;
            Check(p.UseQuickItem(why) && p.QuickItem() == "cooked_meat" && p.hp > hurt &&
                  p.inventory.Count("cooked_meat") == 2,
                  "with nothing chosen it is the first food in the pack, and it is eaten from the pack");
            Check(p.EatCooldown() > 0.0f, "a mouthful takes a moment to get down");

            const int after_one = p.hp;
            Check(!p.UseQuickItem(why) && !why.empty() && p.hp == after_one && p.inventory.Count("cooked_meat") == 2,
                  "a second straight after it is refused, with a reason, and costs nothing");
            p.SetQuickItem("cooked_trout");
            Check(!p.UseQuickItem(why) && p.inventory.Count("cooked_trout") == 2,
                  "and so is a different thing that heals: it is the healing that waits, not the dish");
            p.SetQuickItem("mana_tonic");
            p.SetMana(0);
            Check(p.UseQuickItem(why) && p.inventory.Count("mana_tonic") == 1,
                  "a tonic that mends nothing is not held up by it");

            for (int f = 0; f < static_cast<int>(Player::EAT_COOLDOWN * 60.0f) + 6; ++f) w.Update(kFrame, rctx);
            Check(p.EatCooldown() <= 0.0f, "the wait is a second and a half");
            p.SetQuickItem("cooked_trout");
            if (p.hp >= p.max_hp) p.Damage(3);
            Check(p.UseQuickItem(why) && p.inventory.Count("cooked_trout") == 1, "and then the next goes down");

            Check(p.CycleQuickItem() == "cooked_meat" && p.CycleQuickItem() == "mana_tonic" &&
                  p.CycleQuickItem() == "cooked_trout", "stepping goes round what is carried, and comes back to the start");

            // Out of it: it says which, and does not quietly eat something else.
            p.SetQuickItem("cooked_meat");
            p.inventory.Remove("cooked_meat", 2);
            for (int f = 0; f < 120; ++f) w.Update(kFrame, rctx);
            Check(!p.UseQuickItem(why) && why.find("no ") != string::npos && p.inventory.Count("cooked_trout") == 1,
                  "out of the chosen thing, it says so rather than eating something else");

            // And it is the character's, so it is in the save.
            p.SetQuickItem("cooked_trout");
            Player back;
            back.Init(rctx, "player_hero");
            back.FromJson(p.ToJson(), rctx);
            Check(back.QuickItem() == "cooked_trout", "the quick item is saved with the character");
        }

        // --- the inn's beds have a price, and nobody else's does ----------------------------
        {
            int paid = 0, lent = 0;
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                for (const MapObject& o : m.Objects()) {
                    if (o.type != "bed" && o.type != "campsite") continue;
                    if (string(id) == "house_inn_upper") {
                        Check(o.fee > 0 && o.title.find(std::to_string(o.fee)) != string::npos,
                              o.id + " at the inn is paid for, and says what it costs before it is asked for");
                        ++paid;
                    } else {
                        Check(o.fee == 0, string(id) + "/" + o.id + " is somebody's own, and is free");
                        ++lent;
                    }
                }
            }
            Check(paid == 3 && lent >= 3, "three rooms at the Barley and Bell, and beds elsewhere besides");

            World w;
            w.player.Init(rctx, "player_hero");
            if (w.LoadMap("house_inn_upper", "entrance", rctx) || w.LoadMap("house_inn_upper", "default", rctx)) {
                w.enemies.clear();
                w.clock.Set(1, 22.0f);
                w.TakeRequests();
                Check(w.AskToSleep("A bed at the inn", 15), "the inn's bed asks");
                const vector<WorldRequest> reqs = w.TakeRequests();
                Check(reqs.size() == 1 && reqs[0].type == WorldRequest::Type::Sleep && reqs[0].count == 15,
                      "and what it costs goes to the panel with the question");
                Check(w.AskToSleep("Your own"), "a bed with no price asks too");
                const vector<WorldRequest> own = w.TakeRequests();
                Check(own.size() == 1 && own[0].count == 0, "for nothing");
            }
            Check(Talents::RESPEC_FEE > 0, "and unlearning a tree has a price as well");
        }

        // --- a boss is killed once a day ------------------------------------------------------
        {
            World w;
            w.player.Init(rctx, "player_hero");
            Check(w.LoadMap("house_inn_cellar", "entrance", rctx) || w.LoadMap("house_inn_cellar", "default", rctx),
                  "down into Bess's cellar");
            w.clock.Set(3, 12.0f);
            int boss_at = -1, rat_at = -1;
            for (size_t i = 0; i < w.enemies.size(); ++i) {
                const EnemyDef* d = w.enemies[i]->Def();
                if (!d) continue;
                if (d->is_boss && boss_at < 0) boss_at = static_cast<int>(i);
                if (!d->is_boss && rat_at < 0) rat_at = static_cast<int>(i);
            }
            Check(boss_at >= 0 && rat_at >= 0, "the Broodmother is there, and her brood");
            if (boss_at >= 0 && rat_at >= 0) {
                const size_t before = w.enemies.size();
                const int boss_post = w.enemies[boss_at]->post;
                Check(boss_post >= 0 && !w.SlainToday("house_inn_cellar", boss_post), "she has not been killed today");
                w.enemies[boss_at]->Damage(99999);
                w.enemies[rat_at]->Damage(99999);
                // Stood well away, so nothing is kept from coming back by being watched.
                for (int f = 0; f < 5; ++f) w.Update(kFrame, rctx);
                Check(w.enemies[boss_at]->Dead() && w.SlainToday("house_inn_cellar", boss_post),
                      "killed, she is remembered by where she stood and the day");
                Check(!w.SlainToday("house_inn_cellar", w.enemies[rat_at]->post),
                      "a rat is not: only a boss is kept count of");

                // Out of the door and in again.
                Check(w.LoadMap("house_inn", "default", rctx) || w.LoadMap("house_inn", "entrance", rctx), "up the stairs");
                Check(w.LoadMap("house_inn_cellar", "entrance", rctx) || w.LoadMap("house_inn_cellar", "default", rctx),
                      "and down again");
                Check(w.enemies.size() == before,
                      "everything keeps its place in the list, which is what friends count monsters by");
                Check(w.enemies[boss_at]->Def() && w.enemies[boss_at]->Def()->is_boss && w.enemies[boss_at]->Dead(),
                      "and she is lying dead in hers");
                Check(!w.enemies[rat_at]->Dead(), "the rat is back, as rats always were");
                for (int f = 0; f < 600; ++f) w.Update(kFrame, rctx);
                Check(w.enemies[boss_at]->Dead(), "ten seconds on she has not got up");

                // It is in the save, and it is only today's that is.
                const string was = SaveSystem::Directory();
                const fs::path dir = fs::temp_directory_path() / "dreamquest_selftest_slain";
                std::error_code ec;
                fs::remove_all(dir, ec);
                fs::create_directories(dir, ec);
                SaveSystem::SetDirectory(dir.string());
                Check(SaveSystem::Save(1, w, log, 10.0f), "a save with her dead writes");
                {
                    World back;
                    back.player.Init(rctx, "player_hero");
                    QuestLog log2;
                    log2.LoadDefinitions("data/quests.json");
                    float played = 0.0f;
                    Check(SaveSystem::Load(1, back, log2, rctx, played), "and loads");
                    Check(back.SlainToday("house_inn_cellar", boss_post) && back.MapId() == "house_inn_cellar" &&
                          static_cast<int>(back.enemies.size()) > boss_at && back.enemies[boss_at]->Dead(),
                          "loading does not stand her back up");
                }
                SaveSystem::SetDirectory(was);
                fs::remove_all(dir, ec);

                // The day after, she is back.
                w.clock.Set(4, 12.0f);
                Check(!w.SlainToday("house_inn_cellar", boss_post), "the next day is another day");
                Check(w.LoadMap("house_inn", "default", rctx) || w.LoadMap("house_inn", "entrance", rctx), "up");
                Check(w.LoadMap("house_inn_cellar", "entrance", rctx) || w.LoadMap("house_inn_cellar", "default", rctx), "and down");
                Check(!w.enemies[boss_at]->Dead(), "and she is on her feet for it");
            }
        }
    }

    Section("a spell is paid for when it lands, and a wall teaches nothing");
    {
        Input input;
        std::mt19937 rng(417);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };
        const auto frames = [&](World& w, int n) {
            for (int f = 0; f < n; ++f) { input.Update(dt); w.Update(dt, ctx); }
        };
        const auto mage = [&](World& w, const string& map, const string& at, int level,
                              std::initializer_list<const char*> nodes, const char* technique) {
            w.player.Init(ctx, "player_wayfarer");
            if (!w.LoadMap(map, at, ctx)) return false;
            w.enemies.clear();
            w.clock.Set(1, 12.0f);
            LevelUp lu;
            if (level > 1) w.player.skills.AddXp(SKILL_MAGIC, XpForLevel(level), lu);
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(40), lu);
            w.player.SyncHitpoints();
            w.player.hp = w.player.max_hp;
            w.player.SyncMana();
            w.player.RestoreMana();
            w.player.equipment.Equip(SLOT_WEAPON, "novice_staff");
            for (const char* n : nodes) w.player.talents.Learn(n, w.player.skills);
            if (technique) w.player.talents.ToggleTechnique(technique);
            w.player.facing = FACE_RIGHT;
            return true;
        };
        const auto spawn = [&](World& w, const string& type, float dx, float dy) -> Enemy* {
            const EnemyDef* stats = enemy_db.Get(type);
            if (!stats) return nullptr;
            EnemySpawnDef def;
            def.type = type; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
            def.x = w.player.x + dx; def.y = w.player.y + dy;
            auto e = std::make_unique<Enemy>();
            e->Init(stats, def, ctx);
            Enemy* raw = e.get();
            w.enemies.push_back(std::move(e));
            return raw;
        };
        // Something to cast at that does not fight back and does not fall over:
        // a mage at thirty kills a cow with one bolt, and then what is being
        // measured is how much of the blow was wasted on a dead cow.
        const auto sturdy = [&](World& w, float dx, float dy) -> Enemy* {
            Enemy* cow = spawn(w, "cow", dx, dy);
            if (cow) { cow->max_hp = 4000; cow->hp = cow->max_hp; }
            return cow;
        };
        // One press of the light button: a bolt.
        const auto bolt = [&](World& w) {
            input.Update(dt); key(SDLK_J, true);  w.Update(dt, ctx);
            input.Update(dt); key(SDLK_J, false); w.Update(dt, ctx);
        };

        // --- the exploit, as it was played -------------------------------------------------
        // In the middle of Havenbrook, a pace from something solid, with the
        // mana put back before every cast so that nothing stops it but the rule.
        {
            World w;
            if (mage(w, "town_havenbrook", "waystone", 1, {}, nullptr)) {
                // The stone is north. A few paces back from it, because a bolt
                // leaves the staff a hand's width ahead of the caster, and let go
                // with the staff against the stone it begins inside it.
                w.player.y += 40.0f;
                w.player.facing = FACE_UP;
                const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                int casts = 0;
                bool struck_stone = false, flew = false;
                for (int i = 0; i < 40; ++i) {
                    w.player.RestoreMana();
                    const int mana = w.player.Mana();
                    int lowest = mana;
                    bolt(w);
                    for (int f = 0; f < 50; ++f) {
                        frames(w, 1);
                        lowest = std::min(lowest, w.player.Mana());
                        flew |= !w.projectiles.empty();
                        struck_stone |= !w.impacts.empty();
                    }
                    casts += lowest < mana;               // the mana went: it was cast
                }
                Check(flew && struck_stone, "the bolts are cast, and break on the stone");
                Check(casts >= 30, "forty presses is a good many casts (" + std::to_string(casts) + ")");
                Check(w.player.skills.Xp(SKILL_MAGIC) == xp0,
                      "and a minute of casting at a wall teaches no Magic at all (" +
                          std::to_string(w.player.skills.Xp(SKILL_MAGIC) - xp0) + " gained)");
                Check(w.player.skills.Level(SKILL_MAGIC) == 1, "not a level of it");
                frames(w, 200);
                Check(w.OwedCasts() == 0, "nothing is left owed for bolts that are gone");
            }
        }

        // --- nor does the open air ----------------------------------------------------------
        {
            World w;
            if (mage(w, "overworld", "start", 30, {"potency", "focus", "nova"}, "nova")) {
                const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                for (int i = 0; i < 6; ++i) { w.player.RestoreMana(); bolt(w); frames(w, 50); }
                // And the dear ones: a ring of eight at twice the mana.
                for (int i = 0; i < 3; ++i) {
                    w.player.RestoreMana();
                    input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                    frames(w, 80);
                    input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                    frames(w, 60);
                }
                Check(w.player.skills.Xp(SKILL_MAGIC) == xp0, "bolts and novas into an empty field teach nothing either");
                // A fire bolt leaves the ground burning for three seconds and
                // more, and the cast is owed for as long as something could
                // still walk into it.
                Check(w.OwedCasts() > 0 && !w.ground_effects.empty(),
                      "while what they left is still burning, something could yet walk into it");
                frames(w, 360);
                Check(w.OwedCasts() == 0 && w.projectiles.empty() && w.ground_effects.empty(),
                      "and are forgotten when they have burnt out");
            }
        }

        // --- a spell that lands is paid, in full, and a miss is not ---------------------------
        {
            World w;
            if (mage(w, "overworld", "start", 30, {}, nullptr)) {
                // Water, for the sums: it leaves nothing burning, so what one
                // press earns is all earned by that press.
                w.player.SelectElement(Element::Water);
                const SpellDef* spell = spells.BestFor(w.player.SelectedElement(), 30);
                Enemy* cow = sturdy(w, 80, 0);
                Check(spell && spell->xp > 0 && cow, "a spell worth something, and a cow to cast it at");
                int landed = 0, missed = 0;
                bool paid_right = true, miss_free = true, owed_in_flight = false;
                for (int i = 0; i < 16 && spell && cow; ++i) {
                    w.player.RestoreMana();
                    cow->hp = cow->max_hp;
                    cow->x = w.player.x + 80.0f; cow->y = w.player.y;
                    const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                    bolt(w);
                    for (int f = 0; f < 60; ++f) {
                        frames(w, 1);
                        if (!w.projectiles.empty() && w.OwedCasts() == 1 && w.projectiles.front().cast_id != 0)
                            owed_in_flight = true;
                    }
                    const int hurt = cow->max_hp - cow->hp;
                    const int gain = w.player.skills.Xp(SKILL_MAGIC) - xp0;
                    if (hurt > 0) { ++landed; paid_right &= gain == spell->xp + 4 * hurt; }
                    else          { ++missed; miss_free &= gain == 0; }
                }
                Check(owed_in_flight, "while the bolt is in the air the cast is owed, and the bolt says which cast it is");
                Check(landed >= 3, "most of sixteen bolts land on a cow (" + std::to_string(landed) + ")");
                Check(paid_right, "and each that does pays the spell's own experience, once, on top of what the damage pays");
                Check(miss_free, "one that reaches the cow and does nothing pays nothing (" + std::to_string(missed) +
                                     " missed): a monster that cannot be hit is not a wall with a name");
                frames(w, 360);
                Check(w.OwedCasts() == 0, "and nothing is owed afterwards");
            }
        }

        // --- the fire a bolt leaves is the same cast, and is not a second one -------------------------
        {
            World w;
            if (mage(w, "overworld", "start", 30, {}, nullptr)) {
                const SpellDef* spell = spells.BestFor(Element::Fire, 30);
                Enemy* cow = sturdy(w, 80, 0);
                bool proved = false;
                for (int attempt = 0; attempt < 6 && !proved && spell && cow; ++attempt) {
                    w.player.RestoreMana();
                    cow->hp = cow->max_hp;
                    cow->x = w.player.x + 80.0f; cow->y = w.player.y;
                    const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                    bolt(w);
                    int ticks = 0, last = cow->hp;
                    for (int f = 0; f < 330; ++f) {             // the bolt, and all of the burning after it
                        frames(w, 1);
                        cow->x = w.player.x + 80.0f; cow->y = w.player.y;   // stood in it
                        if (cow->hp < last) { ++ticks; last = cow->hp; }
                    }
                    const int hurt = cow->max_hp - cow->hp;
                    if (ticks < 2) continue;                    // the dice; go again
                    const int gain = w.player.skills.Xp(SKILL_MAGIC) - xp0;
                    Check(gain == spell->xp + 4 * hurt,
                          "a fire bolt and the ground it leaves burning hurt the cow " + std::to_string(ticks) +
                              " times and pay for the spell once (" + std::to_string(gain - 4 * hurt) + " of " +
                              std::to_string(spell->xp) + ")");
                    Check(w.OwedCasts() == 0, "and with the fire out there is nothing owed");
                    proved = true;
                }
                Check(proved, "a cow stood in a fire bolt's burning ground is burnt by it");
            }
        }

        // --- once a cast, however many things it hits ---------------------------------------------
        {
            World w;
            if (mage(w, "overworld", "start", 30, {"potency", "focus", "nova"}, "nova")) {
                w.player.SelectElement(Element::Water);
                const SpellDef* spell = spells.BestFor(w.player.SelectedElement(), 30);
                const float marks[4][2] = {{60, 0}, {-60, 0}, {0, 60}, {0, -60}};
                vector<Enemy*> herd;
                for (const auto& m : marks) herd.push_back(sturdy(w, m[0], m[1]));
                bool proved = false;
                for (int attempt = 0; attempt < 8 && !proved && spell; ++attempt) {
                    w.player.RestoreMana();
                    input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                    frames(w, 80);
                    for (size_t i = 0; i < herd.size(); ++i)
                        if (herd[i]) { herd[i]->hp = herd[i]->max_hp; herd[i]->x = w.player.x + marks[i][0]; herd[i]->y = w.player.y + marks[i][1]; }
                    const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                    input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                    frames(w, 70);
                    int hurt = 0, cows_hurt = 0;
                    for (Enemy* c : herd) if (c && c->hp < c->max_hp) { hurt += c->max_hp - c->hp; ++cows_hurt; }
                    if (cows_hurt < 2) continue;                       // the dice; go again
                    const int gain = w.player.skills.Xp(SKILL_MAGIC) - xp0;
                    Check(gain == spell->xp + 4 * hurt,
                          "a nova that hurts " + std::to_string(cows_hurt) + " cows is one cast: the spell's experience once (" +
                              std::to_string(gain - 4 * hurt) + " of " + std::to_string(spell->xp) + ")");
                    proved = true;
                }
                Check(proved, "a nova in the middle of four cows hurts at least two of them");
            }
        }

        // --- what comes down from above is the cast's as well ----------------------------------------
        {
            World w;
            if (mage(w, "overworld", "start", 30, {"ward", "seeker", "meteor"}, "meteor")) {
                const SpellDef* spell = spells.BestFor(w.player.SelectedElement(), 30);
                // Into an empty field first: a meteor is three bolts' mana, and was three bolts' worth of nothing.
                const int idle0 = w.player.skills.Xp(SKILL_MAGIC);
                input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                frames(w, 80);
                input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                bool carried = false;
                for (int f = 0; f < 90; ++f) {
                    frames(w, 1);
                    for (const GroundEffect& g : w.ground_effects) carried |= g.cast_id != 0;
                }
                Check(carried, "a meteor carries its cast down with it");
                Check(w.player.skills.Xp(SKILL_MAGIC) == idle0 && w.OwedCasts() == 0,
                      "and one that lands on nothing pays nothing and is forgotten");

                Enemy* cow = sturdy(w, 110, 0);
                bool proved = false;
                for (int attempt = 0; attempt < 8 && !proved && spell && cow; ++attempt) {
                    w.player.RestoreMana();
                    input.Update(dt); key(SDLK_K, true); w.Update(dt, ctx);
                    frames(w, 80);
                    cow->hp = cow->max_hp;
                    cow->x = w.player.x + 110.0f; cow->y = w.player.y;
                    const int xp0 = w.player.skills.Xp(SKILL_MAGIC);
                    input.Update(dt); key(SDLK_K, false); w.Update(dt, ctx);
                    for (int f = 0; f < 90; ++f) {
                        frames(w, 1);
                        // Held under it: a cow that ambles off is a test of cows.
                        if (cow->hp == cow->max_hp) { cow->x = w.player.x + 110.0f; cow->y = w.player.y; }
                    }
                    const int hurt = cow->max_hp - cow->hp;
                    if (hurt <= 0) continue;
                    const int gain = w.player.skills.Xp(SKILL_MAGIC) - xp0;
                    Check(gain == spell->xp + 4 * hurt, "a meteor that lands on a cow pays the spell's experience, once (" +
                                                           std::to_string(gain - 4 * hurt) + " of " + std::to_string(spell->xp) + ")");
                    proved = true;
                }
                Check(proved, "a meteor lands on a cow that is stood under it");
            }
        }

        // --- an arrow is not a cast ------------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_warden");
            if (w.LoadMap("overworld", "start", ctx)) {
                w.enemies.clear();
                w.player.equipment.Equip(SLOT_WEAPON, "oak_shortbow");
                w.player.facing = FACE_RIGHT;
                const int xp0 = w.player.skills.Xp(SKILL_RANGED);
                bool loosed = false, plain = true;
                bolt(w);
                for (int f = 0; f < 60; ++f) {
                    frames(w, 1);
                    for (const Projectile& p : w.projectiles) { loosed = true; plain &= p.cast_id == 0; }
                }
                Check(loosed && plain && w.OwedCasts() == 0, "a bow owes nothing: it never paid for an arrow that hit nothing");
                Check(w.player.skills.Xp(SKILL_RANGED) == xp0, "and an arrow into a field still teaches nothing");
            }
        }
    }

    Section("what comes out at night");
    {
        GameContext ctx;
        std::mt19937 rng(2020);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- where, and what ---------------------------------------------------------------
        const std::set<string> wilds = {"overworld", "whisperwood_trail", "westwold", "brackenwood"};
        int posts_all = 0;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            vector<const EnemySpawnDef*> night, day;
            for (const EnemySpawnDef& e : m.Enemies()) (e.night ? night : day).push_back(&e);
            if (!wilds.count(id)) {
                Check(night.empty(), string(id) + " is a town, a room, a hole in the ground or a dream: nothing new comes to it at night");
                continue;
            }
            posts_all += static_cast<int>(night.size());
            Check(night.size() >= 6, string(id) + " has visitors after dark (" + std::to_string(night.size()) + " posts)");
            // "Don't overdo it": with half of them kept on any one night, what
            // is abroad is a small share of what lives there.
            Check(night.size() * 0.5f <= day.size() * 0.2f,
                  string(id) + ": and few of them -- " + std::to_string(night.size()) + " posts, half kept, to " +
                      std::to_string(day.size()) + " by day");
            // They are written last, so that every post there was before keeps
            // its number: the day's hash and a slain boss both go by it.
            bool seen_night = false, night_last = true;
            for (const EnemySpawnDef& e : m.Enemies()) { if (e.night) seen_night = true; else if (seen_night) night_last = false; }
            Check(night_last, string(id) + ": the night posts come after everything else in the list");

            bool sound = true, strangers = true, stronger = true, measured = true, clear = true;
            for (const EnemySpawnDef* n : night) {
                sound &= n->respawn <= 0.0f && !n->pool.empty() && !n->group.empty() && n->chance > 0.0f && n->chance <= 0.75f;
                int low = 99, high = 0;
                for (const string& t : n->pool) {
                    const EnemyDef* d = enemy_db.Get(t);
                    sound &= d != nullptr && !d->is_boss && d->aggro_range > 0.0f;
                    if (!d) continue;
                    low  = std::min(low,  Enemy::ShownLevelOf(*d, n->level));
                    high = std::max(high, Enemy::ShownLevelOf(*d, n->level + n->spread));
                }
                // What lives round about by day, bosses apart.
                float sum = 0.0f; int count = 0;
                for (const EnemySpawnDef* d : day) {
                    const EnemyDef* def = enemy_db.Get(d->type);
                    if (!def || def->is_boss) continue;
                    const float far = std::hypot(d->x - n->x, d->y - n->y);
                    if (far < 640.0f && std::find(n->pool.begin(), n->pool.end(), d->type) != n->pool.end()) strangers = false;
                    if (far < 800.0f) { sum += Enemy::ShownLevelOf(*def, d->level); ++count; }
                }
                if (count > 0) {
                    const float usual = sum / count;
                    stronger &= low > usual;
                    measured &= high <= usual + 24.0f;
                }
                clear &= !m.Blocked({n->x - 6.0f, n->y - 6.0f, 12.0f, 6.0f});
                // Nowhere near a way in, somewhere to arrive, or anybody.
                for (const Portal& p : m.Portals()) {
                    const float px = std::clamp(n->x, p.rect.x, p.rect.x + p.rect.w), py = std::clamp(n->y, p.rect.y, p.rect.y + p.rect.h);
                    clear &= std::hypot(px - n->x, py - n->y) >= 340.0f;
                }
                for (const NpcDef& who : m.Npcs()) clear &= std::hypot(who.x - n->x, who.y - n->y) >= 340.0f;
                for (const MapObject& o : m.Objects())
                    if (o.type == "bed" || o.type == "campsite") clear &= std::hypot(o.x - n->x, o.y - n->y) >= 340.0f;
            }
            Check(sound, string(id) + ": each is a pack's post, of real monsters that fight, kept some nights and never respawning");
            Check(strangers, string(id) + ": and what comes is not what lives there -- nothing of its kind stands within twenty cells by day");
            Check(stronger, string(id) + ": it is stronger than the run of what does");
            Check(measured, string(id) + ": but by a step or two, not by a dragon");
            Check(clear, string(id) + ": and is posted on open ground, away from every gate, camp and person");
        }
        Check(posts_all >= 40, "about eighty posts across the wilds (" + std::to_string(posts_all) + ")");

        // The road out of Havenbrook is the way to travel after dark.
        {
            Map ow;
            ow.Load("maps/overworld.mx");
            SDL_FPoint gate{};
            ow.Spawn("start", gate);
            float nearest_to_gate = 1.0e9f;
            for (const EnemySpawnDef& e : ow.Enemies())
                if (e.night) nearest_to_gate = std::min(nearest_to_gate, std::hypot(e.x - gate.x, e.y - gate.y));
            Check(nearest_to_gate > 500.0f, "nothing comes within a screen of Havenbrook's gate (" +
                                                std::to_string(static_cast<int>(nearest_to_gate)) + " px)");
        }

        // --- which nights -----------------------------------------------------------------------
        {
            int kept = 0, asked = 0, changed = 0;
            for (int day = 1; day <= 60; ++day)
                for (int post = 0; post < 40; ++post) {
                    const bool tonight = World::KeptTonight("overworld", day, post, "", 0.5f);
                    kept += tonight; ++asked;
                    changed += tonight != World::KeptTonight("overworld", day + 1, post, "", 0.5f);
                }
            const float share = static_cast<float>(kept) / asked;
            Check(share > 0.42f && share < 0.58f, "half the posts are kept on a night (" + std::to_string(static_cast<int>(share * 100)) + "%)");
            Check(changed > asked / 3, "and not the same half two nights running");
            Check(World::KeptTonight("overworld", 3, 7, "", 1.0f) && !World::KeptTonight("overworld", 3, 7, "", 0.0f),
                  "always is always and never is never");
            bool together = true;
            for (int day = 1; day <= 40; ++day)
                together &= World::KeptTonight("westwold", day, 4, "night_90_33", 0.5f) ==
                            World::KeptTonight("westwold", day, 5, "night_90_33", 0.5f);
            Check(together, "a pack comes or stays away together");
        }

        // --- a night, played through -------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            w.clock.Set(2, 12.0f);
            Check(w.LoadMap("overworld", "start", ctx), "the Hollowmarch at noon");
            const size_t listed = w.enemies.size();
            const auto up = [&]() {
                int n = 0;
                for (const auto& e : w.enemies) n += e->night && !e->Dead();
                return n;
            };
            const auto due = [&]() {
                int n = 0;
                for (const auto& e : w.enemies) n += w.Abroad(*e);
                return n;
            };
            Check(w.HasNightPosts() && up() == 0 && due() == 0, "by day every one of them is in the list and none of them is there");
            for (int f = 0; f < 120; ++f) w.Update(kFrame, ctx);
            Check(up() == 0, "and stays away while the sun is up");

            // Nightfall, watched from the town gate, which is nowhere near any of them.
            w.clock.Set(2, 19.99f);
            w.TakeRequests();
            for (int f = 0; f < 90; ++f) w.Update(kFrame, ctx);
            Check(w.clock.IsNight(), "the clock goes round to eight");
            const int tonight = due();
            Check(tonight >= 8 && tonight <= 30, "about half of them are due tonight (" + std::to_string(tonight) + ")");
            Check(up() == tonight, "and every one that is due is up, at its post (" + std::to_string(up()) + ")");
            Check(w.enemies.size() == listed, "with the list as long as it was: friends count monsters by their place in it");
            bool warned = false;
            for (const WorldRequest& r : w.TakeRequests())
                warned |= r.type == WorldRequest::Type::Toast && r.text.find("road") != string::npos;
            Check(warned, "nightfall says so, and says to keep to the road");

            // One of them, killed.
            Enemy* visitor = nullptr;
            for (const auto& e : w.enemies) if (e->night && !e->Dead()) { visitor = e.get(); break; }
            Check(visitor != nullptr, "there is one to go and find");
            if (visitor) {
                const int post = visitor->post;
                const size_t index = static_cast<size_t>(post);
                w.player.x = visitor->x - 300.0f; w.player.y = visitor->y;
                visitor->Damage(99999);
                for (int f = 0; f < 5; ++f) w.Update(kFrame, ctx);
                Check(visitor->Dead() && w.SlainToday("overworld", post), "killed, it is remembered for the night");
                // It leaves what its kind leaves: it is a real one.
                for (int f = 0; f < 60 * 40; ++f) w.Update(kFrame, ctx);
                Check(visitor->Dead(), "forty seconds on it has not come back: there is no respawn in the dark");
                // Out of the door and in again.
                Check(w.LoadMap("town_havenbrook", "default", ctx) && w.LoadMap("overworld", "start", ctx), "into town and out again");
                Check(index < w.enemies.size() && w.enemies[index]->night && w.enemies[index]->Dead(),
                      "and it is still dead, the same night");
                Check(up() == tonight - 1, "with the rest of them up, as a map walked into at night finds them");

                // Dawn.
                const size_t lying = w.pickups.size();
                w.clock.Set(3, 4.99f);
                w.TakeRequests();
                for (int f = 0; f < 120; ++f) w.Update(kFrame, ctx);
                Check(!w.clock.IsNight() && up() == 0, "at dawn what is left goes to ground");
                Check(w.pickups.size() == lying, "and leaves nothing: it was not killed");
                bool said = false;
                for (const WorldRequest& r : w.TakeRequests()) said |= r.type == WorldRequest::Type::Toast && r.text.find("Dawn") != string::npos;
                Check(said, "and the morning says so");

                // The next night it may be back. Find one on which it is.
                int back_day = 0;
                for (int day = 3; day < 40 && !back_day; ++day)
                    if (World::KeptTonight("overworld", day, post, w.enemies[index]->night_group, w.enemies[index]->night_chance)) back_day = day;
                w.clock.Set(back_day, 19.99f);
                for (int f = 0; f < 90; ++f) w.Update(kFrame, ctx);
                Check(back_day > 0 && !w.enemies[index]->Dead(), "and on another night that is one of its own, it is back");
            }
        }

        // --- one in the middle of a fight at dawn finishes it -----------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            LevelUp lu;
            w.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(60), lu);
            w.player.skills.AddXp(SKILL_DEFENCE, XpForLevel(60), lu);
            w.player.SyncHitpoints();
            w.player.hp = w.player.max_hp;
            w.clock.Set(5, 23.0f);
            if (w.LoadMap("overworld", "start", ctx)) {
                Enemy* visitor = nullptr;
                for (const auto& e : w.enemies) if (e->night && !e->Dead()) { visitor = e.get(); break; }
                if (visitor) {
                    w.player.x = visitor->x + 40.0f; w.player.y = visitor->y;
                    for (int f = 0; f < 30; ++f) w.Update(kFrame, ctx);
                    Check(visitor->Engaged(), "walked up to, it comes for you");
                    w.clock.Set(6, 4.999f);
                    for (int f = 0; f < 20; ++f) { w.player.x = visitor->x + 30.0f; w.player.y = visitor->y; w.Update(kFrame, ctx); }
                    Check(!w.clock.IsNight() && !visitor->Dead(), "and dawn does not pull it out of a fight it is in");
                }
            }
        }

        // --- a town's nightfall is as it was, and a quest does not point into the dark -----------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            w.clock.Set(2, 19.99f);
            if (w.LoadMap("town_havenbrook", "default", ctx)) {
                w.TakeRequests();
                for (int f = 0; f < 90; ++f) w.Update(kFrame, ctx);
                bool plain = false;
                for (const WorldRequest& r : w.TakeRequests())
                    plain |= r.type == WorldRequest::Type::Toast && r.text.find("Night falls") != string::npos &&
                             r.text.find("road") == string::npos;
                Check(!w.HasNightPosts() && plain, "in town night falls the way it always did");
            }
            std::ifstream in("data/waypoints.json");
            json ways_json;
            if (in) in >> ways_json;
            bool pointed = false;
            if (ways_json.contains("maps") && ways_json["maps"].contains("overworld"))
                for (const auto& post : ways_json["maps"]["overworld"].value("posts", json::array()))
                    for (const auto& t : post.value("types", json::array()))
                        pointed |= t == "hound" || t == "imp" || t == "bat" || t == "wolf";
            Check(!pointed, "no contract is pointed at a post that is only kept after dark");
        }
    }

    Section("what a boss leaves, the first time");
    {
        GameContext ctx;
        std::mt19937 rng(1111);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng; ctx.trees = &trees;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- the boons ----------------------------------------------------------------------------
        vector<string> bosses;
        {
            std::ifstream in("data/enemies.json");
            json ej;
            in >> ej;
            const json& rows = ej.contains("enemies") ? ej["enemies"] : ej;
            for (auto it = rows.begin(); it != rows.end(); ++it) {
                const string id = rows.is_object() ? it.key() : it.value().value("id", string(""));
                const EnemyDef* d = enemy_db.Get(id);
                if (d && d->is_boss) bosses.push_back(id);
            }
        }
        Check(bosses.size() >= 10, "there are bosses to kill (" + std::to_string(bosses.size()) + ")");
        // Every effect a boon names has to be one the game reads, or the boon is a name and nothing else.
        const std::set<string> read = {"max_health", "defence", "stamina", "stamina_regen", "move_speed", "crit", "damage",
                                       "lifesteal", "evade", "speed", "charged_damage", "block_cost", "projectile_speed",
                                       "max_mana", "mana_regen"};
        Check(trees.Boons().size() >= 12, "and boons to leave (" + std::to_string(trees.Boons().size()) + ")");
        std::set<string> ids;
        for (const BoonDef& b : trees.Boons()) {
            Check(ids.insert(b.id).second && !b.name.empty() && !b.text.empty(), b.id + " has a name, and says what it does");
            for (const auto& fx : b.effects)
                Check(read.count(fx.first) > 0 && fx.second > 0.0f, b.id + ": '" + fx.first + "' is something the game reads");
        }
        for (const AttackStyle path : {AttackStyle::Melee, AttackStyle::Ranged, AttackStyle::Magic}) {
            size_t usable = 0;
            for (const BoonDef& b : trees.Boons()) usable += b.For(path);
            Check(usable >= bosses.size(), "every path has a boon for every boss, so no first kill has to repeat one (" +
                                               std::to_string(usable) + ")");
        }

        // --- the first time, and once -----------------------------------------------------------------
        {
            Talents t;
            t.SetDatabase(&trees);
            t.SetPath(AttackStyle::Magic);
            Skills s;
            LevelUp lu;
            s.AddXp(SKILL_MAGIC, XpForLevel(6), lu);
            Check(t.PointsEarned(AttackStyle::Magic, s) == 2 && t.BonusPoints() == 0, "level six is two points, and no boss is none besides");
            const Talents::Trophy first = t.SlayBoss("broodmother", rng);
            Check(first.first && first.boon && t.HasSlain("broodmother") && t.BonusPoints() == 1 && t.Boons().size() == 1,
                  "the first Broodmother leaves a point and a boon");
            Check(t.PointsEarned(AttackStyle::Magic, s) == 3 && t.PointsFree(AttackStyle::Magic, s) == 3, "and the point is one to spend");
            const Talents::Trophy again = t.SlayBoss("broodmother", rng);
            Check(!again.first && !again.boon && t.BonusPoints() == 1 && t.Boons().size() == 1, "the second leaves her loot and nothing else");
            Check(!t.SlayBoss("", rng).first, "and nobody is nobody");

            // It buys a rank like any other.
            const TalentTree& tree = trees.Tree(AttackStyle::Magic);
            const TalentNode* top = tree.At(0, 0);
            if (top) {
                int bought = 0;
                while (t.Learn(top->id, s)) ++bought;
                Check(bought == std::min(3, top->ranks), "three points buy three ranks where level six alone bought two");
                t.Reset(AttackStyle::Magic);
                Check(t.PointsFree(AttackStyle::Magic, s) == 3 && t.Boons().size() == 1 && t.HasSlain("broodmother"),
                      "unlearning the tree gives the point back with the rest, and keeps the boon");
            }

            // All of them: a different boon from each, and none a mage has no use for.
            for (const string& b : bosses) t.SlayBoss(b, rng);
            std::set<string> got(t.Boons().begin(), t.Boons().end());
            Check(t.Boons().size() == bosses.size() && got.size() == bosses.size(),
                  "every boss leaves a boon, and no two the same (" + std::to_string(got.size()) + ")");
            bool fitting = true;
            for (const string& id : t.Boons()) { const BoonDef* b = trees.Boon(id); fitting &= b && b->For(AttackStyle::Magic); }
            Check(fitting && !t.HasBoon("boon_shieldarm") && !t.HasBoon("boon_true_flight"), "and none is for a path that is not theirs");
            Check(t.PointsEarned(AttackStyle::Magic, s) == 2 + static_cast<int>(bosses.size()), "a point for each");

            // It is the character's, so it is wherever the character is kept.
            Talents back;
            back.SetDatabase(&trees);
            back.SetPath(AttackStyle::Magic);
            back.FromJson(t.ToJson());
            Check(back.Boons() == t.Boons() && back.BossesSlain() == t.BossesSlain(), "bosses and boons survive a save");
            Check(!back.SlayBoss("pit_lord", rng).first, "and a boss killed before the save is not a first kill after it");
            // A save that has been meddled with cannot have more boons than bosses, or boons nobody made.
            json forged = t.ToJson();
            forged["bosses"] = json::array({"broodmother"});
            forged["boss_kills"] = json{{"broodmother", 3}};
            forged["boons"] = json::array({"boon_might", "boon_vigour", "boon_of_being_a_god"});
            back.FromJson(forged);
            Check(back.Boons().size() == 1 && back.BonusPoints() == 1, "a boon for each boss and no more, whatever the file says");

            // A hero is never handed mana.
            for (int seed = 0; seed < 30; ++seed) {
                std::mt19937 dice(seed);
                Talents hero;
                hero.SetDatabase(&trees);
                hero.SetPath(AttackStyle::Melee);
                for (const string& b : bosses) hero.SlayBoss(b, dice);
                fitting &= !hero.HasBoon("boon_deep_reserves") && !hero.HasBoon("boon_wellspring") && !hero.HasBoon("boon_true_flight");
            }
            Check(fitting, "thirty heroes kill everything and not one is given mana or arrows");
        }

        // --- what each one does -----------------------------------------------------------------------
        {
            const auto with = [&](const char* boon) {
                auto p = std::make_unique<Player>();
                p->Init(ctx, "player_wayfarer");
                LevelUp lu;
                p->skills.AddXp(SKILL_HITPOINTS, XpForLevel(50), lu);
                p->skills.AddXp(SKILL_MAGIC, XpForLevel(40), lu);
                if (boon) p->talents.FromJson(json{{"bosses", json::array({"x"})}, {"boons", json::array({boon})}});
                p->SyncHitpoints();
                p->SyncMana();
                return p;
            };
            const auto plain = with(nullptr);
            Check(with("boon_vigour")->max_hp == static_cast<int>(std::lround(plain->max_hp * 1.08f)) && plain->max_hp == 50,
                  "Vigour is eight parts in a hundred more health");
            Check(with("boon_stoneblood")->Profile().defence_bonus == plain->Profile().defence_bonus + 5, "Stoneblood is five Defence");
            Check(std::fabs(with("boon_long_wind")->MaxStamina() - plain->MaxStamina() * 1.10f) < 0.01f, "Long Wind is a tenth more breath");
            Check(with("boon_deep_reserves")->MaxMana() == static_cast<int>(std::lround(plain->MaxMana() * 1.10f)), "Deep Reserves is a tenth more mana");
            Check(std::fabs(with("boon_might")->TalentDamage(AttackStyle::Magic, AttackType::Light) -
                            plain->TalentDamage(AttackStyle::Magic, AttackType::Light) - 0.03f) < 1e-4f &&
                  std::fabs(with("boon_might")->TalentDamage(AttackStyle::Melee, AttackType::Light) -
                            plain->TalentDamage(AttackStyle::Melee, AttackType::Light) - 0.03f) < 1e-4f,
                  "Might is three in a hundred, with whatever is in hand");
            Check(std::fabs(with("boon_keen_eye")->talents.Effect("crit", AttackStyle::Ranged) - 0.02f) < 1e-5f &&
                  std::fabs(with("boon_sure_feet")->talents.Global("evade") - 0.03f) < 1e-5f &&
                  std::fabs(with("boon_leech")->talents.Effect("lifesteal", AttackStyle::Melee) - 0.02f) < 1e-5f,
                  "and the rest are read where the tree's own are");
        }

        // --- in the world: down into the cellar ------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("house_inn_cellar", "entrance", ctx) || w.LoadMap("house_inn_cellar", "default", ctx), "Bess's cellar");
            w.clock.Set(3, 12.0f);
            Enemy* mother = nullptr;
            Enemy* rat = nullptr;
            for (const auto& e : w.enemies) {
                if (e->Def() && e->Def()->is_boss) mother = e.get();
                else if (!rat) rat = e.get();
            }
            Check(mother && rat, "the Broodmother, and a rat");
            if (mother && rat) {
                w.TakeRequests();
                rat->Damage(99999);
                for (int f = 0; f < 5; ++f) w.Update(kFrame, ctx);
                Check(w.player.talents.BonusPoints() == 0 && w.player.talents.Boons().empty(), "a rat leaves nothing of the kind");
                w.TakeRequests();

                const int points = w.player.talents.PointsEarned(AttackStyle::Melee, w.player.skills);
                mother->Damage(99999);
                for (int f = 0; f < 5; ++f) w.Update(kFrame, ctx);
                Check(w.player.talents.HasSlain("broodmother") && w.player.talents.Boons().size() == 1 &&
                      w.player.talents.PointsEarned(AttackStyle::Melee, w.player.skills) == points + 1,
                      "the Broodmother, killed, leaves a skill point and a boon");
                bool named = false, pointed = false, booned = false, short_enough = true;
                const BoonDef* left = w.player.talents.Boons().empty() ? nullptr : trees.Boon(w.player.talents.Boons().back());
                for (const WorldRequest& r : w.TakeRequests()) {
                    if (r.type != WorldRequest::Type::Toast) continue;
                    named   |= r.text.find("The Broodmother") != string::npos;
                    pointed |= r.text.find("skill point") != string::npos;
                    booned  |= left && r.text.find(left->name) != string::npos && r.text.find(left->text) != string::npos;
                    short_enough &= r.text.size() <= 90;
                }
                Check(named && pointed && booned, "and the game says who, and that there is a point, and which boon and what it does");
                Check(short_enough, "in lines short enough for a narrow window");

                // The next day she is back, and is only a fight.
                w.clock.Set(4, 12.0f);
                Check((w.LoadMap("house_inn", "default", ctx) || w.LoadMap("house_inn", "entrance", ctx)) &&
                      (w.LoadMap("house_inn_cellar", "entrance", ctx) || w.LoadMap("house_inn_cellar", "default", ctx)), "up, and down again the day after");
                Enemy* again = nullptr;
                for (const auto& e : w.enemies) if (e->Def() && e->Def()->is_boss) again = e.get();
                Check(again && !again->Dead(), "she is back");
                if (again) {
                    w.TakeRequests();
                    again->Damage(99999);
                    for (int f = 0; f < 5; ++f) w.Update(kFrame, ctx);
                    bool told_again = false;
                    for (const WorldRequest& r : w.TakeRequests()) told_again |= r.type == WorldRequest::Type::Toast && r.text.find("boon") != string::npos;
                    Check(w.player.talents.Boons().size() == 1 && w.player.talents.BonusPoints() == 1 && !told_again,
                          "and the second time leaves what she drops and nothing more");
                }
            }
        }

        // --- a fight shared: everyone who was there, each once ------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            Check(w.LoadMap("house_inn_cellar", "entrance", ctx) || w.LoadMap("house_inn_cellar", "default", ctx), "the cellar, in company");
            w.clock.Set(3, 12.0f);
            QuestLog couch_journal;
            couch_journal.LoadDefinitions("data/quests.json");
            Player* couch = w.AddGuest(1, "Player Two", "player_warden", ctx);
            Player* wire  = w.AddGuest(2, "Oona", "player_wayfarer", ctx);
            Check(couch && wire, "two friends: one on the couch, one down the wire");
            if (couch && wire) {
                w.SeatOf(1).own_journal = &couch_journal;      // here in person: their character is this one
                w.SeatOf(2).journal.relay = true;              // theirs is on their own machine
                w.player.talents.SlayBoss("broodmother", rng); // the host has killed her before
                const size_t host_boons = w.player.talents.Boons().size();
                Enemy* mother = nullptr;
                for (const auto& e : w.enemies) if (e->Def() && e->Def()->is_boss) mother = e.get();
                if (mother) {
                    mother->Damage(99999);
                    for (int f = 0; f < 5; ++f) w.Update(kFrame, ctx);
                }
                Check(w.player.talents.Boons().size() == host_boons, "the host, who had, gets nothing new");
                Check(couch->talents.HasSlain("broodmother") && couch->talents.Boons().size() == 1, "the friend on the couch, who had not, gets theirs");
                bool couch_told = false;
                for (const WorldRequest& r : w.SeatOf(1).requests) couch_told |= r.type == WorldRequest::Type::Toast && r.text.find("boon") != string::npos;
                Check(couch_told, "and is the one told");
                // The friend down the wire: not here, where their character is a copy...
                Check(!wire->talents.HasSlain("broodmother") && wire->talents.Boons().empty(),
                      "the friend down the wire is not given it here, on a copy of their character");
                // ...but their machine is sent the kill, with which boss it was.
                bool relayed = false;
                for (const QuestEvent& e : w.SeatOf(2).journal.relayed)
                    relayed |= e.type == ObjectiveType::Kill && e.secondary == "broodmother";
                Check(relayed, "their machine is sent the kill, and which boss it was");
                // What it does with that.
                World theirs;
                theirs.player.Init(ctx, "player_wayfarer");
                theirs.AwardBoss("broodmother", ctx);
                Check(theirs.player.talents.HasSlain("broodmother") && theirs.player.talents.Boons().size() == 1 &&
                      !theirs.TakeRequests().empty(), "where it is given to the real one, and said");
                // And what goes back to the host is the character, boon and all.
                wire->ApplySheet(theirs.player.ToJson(), ctx);
                Check(wire->talents.Boons() == theirs.player.talents.Boons() && wire->talents.BonusPoints() == 1,
                      "and the next sheet tells the host, so its rolls are the right ones");
            }
            // A name with its own article is not given a second.
            {
                World w2;
                w2.player.Init(ctx, "player_hero");
                w2.AwardBoss("barrow_wight", ctx);
                bool doubled = false, said = false;
                for (const WorldRequest& r : w2.TakeRequests()) {
                    doubled |= r.text.find("The The") != string::npos;
                    said |= r.text.find("Wight") != string::npos;
                }
                Check(said && !doubled, "the Hollowrest Wight is not The The Hollowrest Wight");
            }
            // A chief still counts for the contract on lizardmen.
            const EnemyDef* chief = enemy_db.Get("lizardman_chief");
            Check(chief && chief->is_boss && chief->kill_target == "lizardman", "a chief is a boss and still a lizardman to the board");
        }
    }

    Section("a totem for the fifteenth, and the ring it stands in");
    {
        GameContext ctx;
        std::mt19937 rng(1515);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng; ctx.trees = &trees;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- one for every boss, and each a real thing ----------------------------------------------
        const std::set<string> read = {"max_health", "defence", "stamina", "stamina_regen", "move_speed", "crit", "crit_damage",
                                       "damage", "lifesteal", "evade", "speed", "charged_damage"};
        int bosses = 0;
        {
            std::ifstream in("data/enemies.json");
            json ej;
            in >> ej;
            const json& rows = ej.contains("enemies") ? ej["enemies"] : ej;
            for (auto it = rows.begin(); it != rows.end(); ++it) {
                const string id = rows.is_object() ? it.key() : it.value().value("id", string(""));
                const EnemyDef* d = enemy_db.Get(id);
                if (!d || !d->is_boss) continue;
                ++bosses;
                const TotemDef* t = trees.TotemOf(id);
                Check(t != nullptr, d->name + " has a totem to leave");
                if (!t) continue;
                const ItemDef* thing = items.Get(t->item);
                Check(thing && thing->keep && thing->value == 0 && !thing->stackable && !thing->consumable,
                      t->item + " is a thing in the bag that cannot be sold, dropped or eaten");
                Check(thing && fs::exists(thing->icon), t->item + " has a picture, which is also what stands in the ring");
                Check(thing && thing->description.find("Mossvale") != string::npos, t->item + " says where it goes");
                Check(!t->name.empty() && !t->text.empty() && trees.Totem(t->item) == t, t->item + " says what it gives");
                for (const auto& fx : t->effects) {
                    Check(read.count(fx.first) > 0 && fx.second > 0.0f, t->item + ": '" + fx.first + "' is something the game reads");
                    // A day's blessing, one at a time and gone home for, is more than what a first kill leaves for good.
                    for (const BoonDef& b : trees.Boons()) {
                        const auto same = b.effects.find(fx.first);
                        if (same != b.effects.end())
                            Check(fx.second > same->second, t->item + ": " + fx.first + " is more than " + b.name + " gives for good");
                    }
                }
            }
        }
        Check(bosses >= 10 && static_cast<int>(trees.Totems().size()) == bosses, "a totem for each boss and no others");
        Check(Talents::TOTEM_KILLS == 15, "and the fifteenth kill is the one");

        // --- counting to fifteen ----------------------------------------------------------------------
        {
            Talents t;
            t.SetDatabase(&trees);
            t.SetPath(AttackStyle::Melee);
            int totems = 0, boons = 0, which = 0;
            for (int n = 1; n <= 20; ++n) {
                const Talents::Trophy won = t.SlayBoss("orc3", rng);
                Check(won.kills == n, "kill " + std::to_string(n) + " is counted as " + std::to_string(n));
                if (won.totem) { ++totems; which = n; }
                boons += won.boon != nullptr;
            }
            Check(totems == 1 && which == 15, "twenty Warchiefs leave one totem, on the fifteenth");
            Check(boons == 1 && t.BonusPoints() == 1 && t.Kills("orc3") == 20, "and one boon and one point, on the first");
            Check(t.Kills("pit_lord") == 0 && !t.SlayBoss("pit_lord", rng).totem, "one boss's count is not another's");

            Talents back;
            back.SetDatabase(&trees);
            back.SetPath(AttackStyle::Melee);
            back.FromJson(t.ToJson());
            Check(back.Kills("orc3") == 20 && back.Kills("pit_lord") == 1, "the count is in the save");
            Check(!back.SlayBoss("orc3", rng).totem, "and a totem already earned is not earned again after it");
            // A save from before kills were counted: each boss in it, once.
            back.FromJson(json{{"bosses", json::array({"den_mother"})}, {"boons", json::array({"boon_might"})}});
            Check(back.Kills("den_mother") == 1 && back.HasSlain("den_mother") && back.Boons().size() == 1,
                  "an older save's bosses count as killed once, and keep their boons");
            int until = 0;
            while (!back.SlayBoss("den_mother", rng).totem && until < 40) ++until;
            Check(until == 13, "so fourteen more leave the totem");
        }

        // --- the ring: one at a time, for the day ---------------------------------------------------------
        {
            Talents t;
            t.SetDatabase(&trees);
            t.SetToday(3);
            Check(t.PlacedTotem().empty() && !t.TotemAwake() && !t.ActiveTotem(), "an empty ring gives nothing");
            Check(t.PlaceTotem("not_a_totem", 3).empty() && t.PlacedTotem().empty(), "and only a totem will stand in it");
            Check(t.PlaceTotem("totem_orc3", 3).empty() && t.TotemAwake() && t.ActiveTotem() &&
                  std::fabs(t.Effect("damage", AttackStyle::Ranged) - 0.10f) < 1e-5f,
                  "the Warchief's, stood in it and touched, is a tenth more damage with anything");
            // The same day, all day.
            Check(t.TotemAwake(), "and still is that evening");
            t.SetToday(4);
            Check(!t.TotemAwake() && t.PlacedTotem() == "totem_orc3" && t.Effect("damage", AttackStyle::Ranged) == 0.0f,
                  "at dawn it is a carving in a ring: still there, and giving nothing");
            Check(t.PlaceTotem("totem_orc3", 4).empty() && t.TotemAwake(), "a hand on it wakes it for the new day, and nothing comes out of the ring");
            // Another in its place.
            const string was = t.PlaceTotem("totem_den_mother", 4);
            Check(was == "totem_orc3" && t.PlacedTotem() == "totem_den_mother", "another stood in its place sends the first back to the bag");
            Check(t.Effect("damage", AttackStyle::Melee) == 0.0f && std::fabs(t.Global("defence") - 15.0f) < 1e-4f,
                  "and there is one blessing, the new one: never two");
            // With what the first kills left, which is for good.
            t.FromJson(json{{"bosses", json::array({"a", "b"})}, {"boons", json::array({"boon_stoneblood", "boon_might"})},
                            {"totem", "totem_den_mother"}, {"totem_day", 4}});
            t.SetToday(4);
            Check(std::fabs(t.Global("defence") - 20.0f) < 1e-4f && std::fabs(t.Effect("damage", AttackStyle::Melee) - 0.03f) < 1e-5f,
                  "a totem is on top of the boons, not instead of them, and is in the save");
            Check(t.TakeTotem() == "totem_den_mother" && t.PlacedTotem().empty() && std::fabs(t.Global("defence") - 5.0f) < 1e-4f,
                  "lifted out, its blessing goes with it and the boons stay");
            t.FromJson(json{{"totem", "totem_of_nobody"}, {"totem_day", 4}});
            Check(t.PlacedTotem().empty(), "a totem nobody made does not stand in the ring, whatever the file says");
        }

        // --- what it is to the character ---------------------------------------------------------------------
        {
            Player p;
            p.Init(ctx, "player_hero");
            LevelUp lu;
            p.skills.AddXp(SKILL_HITPOINTS, XpForLevel(50), lu);
            p.SyncHitpoints();
            const int plain = p.max_hp;
            const int armour = p.Profile().defence_bonus;
            p.talents.SetToday(7);
            p.talents.PlaceTotem("totem_nightmare_troll", 7);
            p.SyncHitpoints();
            Check(plain == 50 && p.max_hp == 60, "the Nightmare's is a fifth more health: fifty is sixty");
            p.talents.PlaceTotem("totem_frost_dragon", 7);
            p.SyncHitpoints();
            Check(p.max_hp == 56 && p.Profile().defence_bonus == armour + 12, "Hoarfang's is twelve Defence and twelve in a hundred health");
            p.talents.SetToday(8);
            p.SyncHitpoints();
            Check(p.max_hp == 50 && p.Profile().defence_bonus == armour, "and the day after, nothing, until it is touched");
        }

        // --- the fifteenth, in the world ------------------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_hero");
            w.player.inventory.SetDatabase(&items);
            Check(w.LoadMap("town_havenbrook", "default", ctx), "somewhere to stand");
            for (int n = 0; n < 14; ++n) w.AwardBoss("broodmother", ctx);
            Check(!w.player.inventory.Has("totem_broodmother"), "fourteen Broodmothers leave no totem");
            w.TakeRequests();
            w.AwardBoss("broodmother", ctx);
            Check(w.player.inventory.Count("totem_broodmother") == 1, "the fifteenth leaves one, in the bag");
            bool said = false, where = false, short_enough = true;
            for (const WorldRequest& r : w.TakeRequests()) {
                said  |= r.text.find("totem") != string::npos && r.text.find("15") != string::npos;
                where |= r.text.find("Mossvale") != string::npos;
                short_enough &= r.text.size() <= 90;
            }
            Check(said && where && short_enough, "and the game says so, and where it goes, in lines short enough for a narrow window");
            for (int n = 0; n < 10; ++n) w.AwardBoss("broodmother", ctx);
            Check(w.player.inventory.Count("totem_broodmother") == 1, "and ten more leave no second");

            // A full pack does not lose it.
            for (int i = 0; i < w.player.inventory.SlotCount(); ++i) w.player.inventory.Add("bronze_sword", 1);
            Check(w.player.inventory.Full(), "a pack with no room in it");
            const size_t lying = w.pickups.size();
            for (int n = 0; n < 15; ++n) w.AwardBoss("orc3", ctx);
            Check(!w.player.inventory.Has("totem_orc3") && w.pickups.size() == lying + 1, "has the totem put at its owner's feet instead");
        }

        // --- the ring itself -----------------------------------------------------------------------------------------
        {
            for (const char* id : kMaps) {
                Map m;
                if (!m.Load(string("maps/") + id + ".mx")) continue;
                int rings = 0;
                for (const MapObject& o : m.Objects()) rings += o.type == "totem_circle";
                Check(rings == (string(id) == "mossvale_cottage" ? 1 : 0),
                      string(id) + (string(id) == "mossvale_cottage" ? " has the ring" : " has no ring: there is one, and it is at home"));
            }
            Check(fs::exists("assets/props/totem_circle.png"), "the ring is drawn");

            World w;
            w.player.Init(ctx, "player_hero");
            w.player.inventory.SetDatabase(&items);
            Check(w.LoadMap("mossvale_cottage", "entrance", ctx), "the house in Mossvale");
            const MapObject* ring = nullptr;
            for (const MapObject& o : w.CurrentMap().Objects()) if (o.type == "totem_circle") ring = &o;
            Check(ring != nullptr, "the ring is in it");
            if (ring) {
                const float rx = ring->x, ry = ring->y;
                Check(std::fabs(rx - w.CurrentMap().Width() / 2.0f) <= 8.0f && std::fabs(ry - w.CurrentMap().Height() / 2.0f) <= 8.0f,
                      "in the middle of the room");
                bool open = true;
                for (int k = 0; k < 8; ++k) {
                    const float a = k * 0.7853982f;
                    open &= !w.CurrentMap().Blocked({rx + cosf(a) * 30.0f - 8.0f, ry + sinf(a) * 30.0f - 10.0f, 16.0f, 10.0f});
                }
                Check(open && !w.CurrentMap().Blocked({rx - 8.0f, ry - 10.0f, 16.0f, 10.0f}),
                      "with clear floor all round it, and over it: a ring is walked across");

                w.player.x = rx; w.player.y = ry + 14.0f;
                w.Update(kFrame, ctx);
                Check(w.player.interact.kind == InteractTarget::Object && w.player.interact.label == "Touch the ring",
                      "stood at it, empty, it asks to be touched");
                w.TakeRequests();
                w.TryInteract(ctx);
                const vector<WorldRequest> reqs = w.TakeRequests();
                Check(reqs.size() == 1 && reqs[0].type == WorldRequest::Type::Totem, "and touching it asks the game for the ring's panel");

                const int day = w.clock.QuestDay();
                w.player.talents.PlaceTotem("totem_nightmare_troll", day);
                w.TellTheDay();
                w.player.SyncHitpoints();
                const int blessed = w.player.max_hp;
                w.Update(kFrame, ctx);
                Check(w.player.interact.label == "Touch the totem", "with a totem in it, it is the totem that is touched");

                // The blessing goes where they go, and lasts the day.
                Check(w.LoadMap("mossvale", "from_mossvale_cottage", ctx), "out of the door");
                for (int f = 0; f < 30; ++f) w.Update(kFrame, ctx);
                Check(w.player.talents.TotemAwake() && w.player.max_hp == blessed, "the blessing goes out of the door with them");
                w.clock.Set(w.clock.Day(), 23.5f);
                for (int f = 0; f < 30; ++f) w.Update(kFrame, ctx);
                Check(w.player.talents.TotemAwake(), "and is still there at midnight: the day turns at dawn");
                w.clock.Set(w.clock.Day() + 1, 4.995f);
                for (int f = 0; f < 60; ++f) w.Update(kFrame, ctx);
                Check(!w.clock.IsNight() && !w.player.talents.TotemAwake() && w.player.max_hp < blessed,
                      "at dawn it is over, and the health it lent is given back");
                Check(w.player.talents.PlacedTotem() == "totem_nightmare_troll", "though the totem is still standing at home");
                Check(w.LoadMap("mossvale_cottage", "entrance", ctx), "home again");
                w.player.x = rx; w.player.y = ry + 14.0f;
                w.Update(kFrame, ctx);
                Check(w.player.interact.label.find("asleep") != string::npos, "where it says it is asleep, and wants a hand on it");

                // And it is the character's, so it is in the save with them.
                const string was = SaveSystem::Directory();
                const fs::path dir = fs::temp_directory_path() / "dreamquest_selftest_totem";
                std::error_code ec;
                fs::remove_all(dir, ec);
                fs::create_directories(dir, ec);
                SaveSystem::SetDirectory(dir.string());
                w.player.talents.PlaceTotem("totem_nightmare_troll", w.clock.QuestDay());
                Check(SaveSystem::Save(1, w, log, 10.0f), "a save with a totem awake writes");
                World back;
                back.player.Init(ctx, "player_hero");
                QuestLog log2;
                log2.LoadDefinitions("data/quests.json");
                float played = 0.0f;
                Check(SaveSystem::Load(1, back, log2, ctx, played), "and loads");
                back.Update(kFrame, ctx);
                Check(back.player.talents.PlacedTotem() == "totem_nightmare_troll" && back.player.talents.TotemAwake(),
                      "with the totem in the ring, and awake for the rest of the same day");
                SaveSystem::SetDirectory(was);
                fs::remove_all(dir, ec);
            }
        }
    }

    Section("a new game starts with a new world");
    {
        // Someone played one save, then started another, and found the first
        // character's things in the second character's storage chest -- and the
        // new save wrote them down as its own. A new game cleared what the
        // world remembers a line at a time, and the two newest things a save
        // had learned to keep were not on the list.
        GameContext ctx;
        std::mt19937 rng(303);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng; ctx.trees = &trees;

        World w;
        w.player.Init(ctx, "player_hero");
        w.player.inventory.SetDatabase(&items);
        Check(w.LoadMap("mossvale_cottage", "entrance", ctx), "the first character's house");
        w.Storage("storage_mossvale", 100, &items).Add("iron_bar", 40);
        w.SetFlag("chest_mine_01");
        w.clock.Set(9, 14.0f);
        w.NoteSlain(3);
        w.SetCamp({true, "overworld", 900.0f, 900.0f});
        Check(w.Storage("storage_mossvale", 100, &items).Count("iron_bar") == 40 && !w.Slain().empty() && w.Flagged("chest_mine_01"),
              "with forty bars in the chest, a chest looted, a boss dead and nine days gone");

        w.StartAfresh();
        Check(w.Storages().empty() && w.Storage("storage_mossvale", 100, &items).Count("iron_bar") == 0,
              "a new game finds the storage chest empty");
        Check(w.Slain().empty(), "no boss dead before anybody has met one");
        Check(w.Flags().empty() && !w.PlayerCamp().pitched && !w.Dream().active, "nothing opened, no camp pitched, nobody dreaming");
        Check(w.clock.Day() == 1 && std::fabs(w.clock.Hours() - 9.0f) < 0.01f, "and nine in the morning of the first day");

        // And the two saves are two saves: each chest is in its own file, and
        // loading one after the other shows each its own.
        const string was = SaveSystem::Directory();
        const fs::path dir = fs::temp_directory_path() / "dreamquest_selftest_two_saves";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        SaveSystem::SetDirectory(dir.string());
        {
            World a;
            a.player.Init(ctx, "player_hero");
            a.player.inventory.SetDatabase(&items);
            a.LoadMap("mossvale_cottage", "entrance", ctx);
            a.Storage("storage_mossvale", 100, &items).Add("iron_bar", 40);
            Check(SaveSystem::Save(1, a, log, 10.0f), "the first save, with bars in the chest");
            // The same session goes on to a new game, in the same world object, as the game does.
            a.StartAfresh();
            a.player = Player();
            a.player.Init(ctx, "player_wayfarer");
            a.player.inventory.SetDatabase(&items);
            a.LoadMap("mossvale_cottage", "entrance", ctx);
            a.Storage("storage_mossvale", 100, &items).Add("logs", 5);
            Check(SaveSystem::Save(2, a, log, 5.0f), "and a second, started after it, with logs in its own");
        }
        for (int slot : {1, 2, 1}) {
            World back;
            back.player.Init(ctx, "player_hero");
            QuestLog log2;
            log2.LoadDefinitions("data/quests.json");
            float played = 0.0f;
            Check(SaveSystem::Load(slot, back, log2, ctx, played), "slot " + std::to_string(slot) + " loads");
            Inventory& chest = back.Storage("storage_mossvale", 100, &items);
            Check(slot == 1 ? (chest.Count("iron_bar") == 40 && chest.Count("logs") == 0)
                            : (chest.Count("logs") == 5 && chest.Count("iron_bar") == 0),
                  "slot " + std::to_string(slot) + " has its own chest and none of the other's");
        }
        // One world loading one slot and then the other: the way the load menu does it.
        {
            World one;
            one.player.Init(ctx, "player_hero");
            QuestLog log2;
            log2.LoadDefinitions("data/quests.json");
            float played = 0.0f;
            SaveSystem::Load(1, one, log2, ctx, played);
            SaveSystem::Load(2, one, log2, ctx, played);
            Check(one.Storage("storage_mossvale", 100, &items).Count("iron_bar") == 0 &&
                  one.Storage("storage_mossvale", 100, &items).Count("logs") == 5,
                  "and loading the second over the first leaves nothing of the first behind");
        }
        SaveSystem::SetDirectory(was);
        fs::remove_all(dir, ec);
    }

    Section("the College at Fernhollow, and Wynn's at Mossvale");
    {
        GameContext ctx;
        std::mt19937 rng(606);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng; ctx.trees = &trees; ctx.projectiles = &projectiles; ctx.spells = &spells;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- the way in, and the court -----------------------------------------------------------
        Map fern, court;
        Check(fern.Load("maps/fernhollow.mx") && court.Load("maps/college_grounds.mx"), "the hamlet and the court load");
        const Portal* gate = nullptr;
        bool tower = false;
        for (const Portal& p : fern.Portals()) if (p.target_map == "college_grounds") gate = &p;
        for (const TileInstance& t : fern.Tiles()) tower |= fern.TexturePath(t).find("mage_college") != string::npos;
        Check(gate && gate->requires_interact, "the college is through a gatehouse in Fernhollow, walked up to like a door");
        Check(gate && gate->rect.y < fern.Height() * 0.3f, "on the hamlet's north side");
        Check(!tower, "and the tower in the south-east corner is gone");
        bool porter = false;
        for (const NpcDef& n : fern.Npcs()) porter |= n.id == "npc_college_porter" && gate &&
                                                       std::hypot(n.x - gate->rect.x, n.y - gate->rect.y) < 160.0f;
        Check(porter, "with a porter at it");

        Check(!court.IsInterior(), "the court is out of doors");
        Check(court.Width() >= 1800.0f && court.Height() >= 1400.0f &&
              court.Width() * court.Height() > fern.Width() * fern.Height(),
              "and huge: bigger than the hamlet it stands beside");
        // Its own tiles, and nothing of the Hollowmarch's brown.
        {
            std::map<string, int> ground;
            for (const TileInstance& t : court.Tiles()) {
                const string& tex = court.TexturePath(t);
                if (tex.find("assets/tiles/") != string::npos) ground[tex.substr(tex.find("assets/tiles/") + 13)] += 1;
            }
            int own = 0, all = 0;
            bool dirt = false;
            for (const auto& kv : ground) {
                all += kv.second;
                if (kv.first.rfind("college_", 0) == 0) own += kv.second;
                dirt |= kv.first.rfind("dirt", 0) == 0 || kv.first.rfind("plank", 0) == 0;
            }
            Check(all > 0 && own * 10 >= all * 6 && !dirt, "paved in the college's own stone (" + std::to_string(own) + " of " +
                                                              std::to_string(all) + " tiles), with no dirt and no planks");
            for (const char* t : {"college_paving", "college_inlay", "college_floor", "college_wall", "college_walltop",
                                  "college_wallface", "college_carpet"})
                Check(fs::exists(string("assets/tiles/") + t + ".png"), string(t) + " is drawn");
        }
        // A chamber off the west wall, the north and the east -- and each somewhere different.
        const Portal* west = nullptr;
        const Portal* north = nullptr;
        const Portal* east = nullptr;
        const Portal* south = nullptr;
        for (const Portal& p : court.Portals()) {
            const float cx = p.rect.x + p.rect.w / 2.0f, cy = p.rect.y + p.rect.h / 2.0f;
            if (cx < 64.0f) west = &p;
            else if (cx > court.Width() - 64.0f) east = &p;
            else if (cy > court.Height() - 64.0f) south = &p;
            else if (cy < court.Height() * 0.4f) north = &p;
        }
        Check(west && north && east && south, "there is a door in the west wall, the north and the east, and the gate to the south");
        if (west && north && east && south) {
            Check(west->target_map == "college_training" && north->target_map == "fernhollow_college" &&
                  east->target_map == "college_classroom" && south->target_map == "fernhollow",
                  "the practice hall west, the great hall north, the lecture room east, the hamlet south");
            std::set<string> rooms = {west->target_map, north->target_map, east->target_map};
            Check(rooms.size() == 3, "three rooms, not one room three times");
            Check(std::fabs((west->rect.y + west->rect.h / 2.0f) - (east->rect.y + east->rect.h / 2.0f)) < 2.0f,
                  "the west door and the east face each other across the court");
        }
        int statues = 0, fountains = 0, halls = 0, wings = 0, lamps = 0;
        for (const TileInstance& t : court.Tiles()) {
            const string& tex = court.TexturePath(t);
            statues += tex.find("college_statue") != string::npos;
            fountains += tex.find("college_fountain") != string::npos;
            halls += tex.find("college_hall") != string::npos;
            wings += tex.find("college_wing") != string::npos;
        }
        for (const MapObject& o : court.Objects()) lamps += o.type == "lamp";
        Check(halls == 1 && wings == 2 && fountains == 1 && statues == 4,
              "the hall with a wing either side of it, a fountain, and four founders");
        Check(lamps >= 8, "and lamps, which are lit after dark (" + std::to_string(lamps) + ")");

        // --- the three rooms -----------------------------------------------------------------------
        struct Room { const char* id; const char* must[3]; };
        const Room rooms[] = {
            {"college_training",   {"training_dummy", "crystal_pylon", "spell_circle"}},
            {"college_classroom",  {"college_blackboard", "college_desk", "college_orrery"}},
            {"fernhollow_college", {"council_table", "high_chair", "spell_circle"}},
        };
        for (const Room& r : rooms) {
            Map m;
            Check(m.Load(string("maps/") + r.id + ".mx") && m.IsInterior(), string(r.id) + " loads, indoors");
            for (const char* art : r.must) {
                bool has = false;
                for (const TileInstance& t : m.Tiles()) has |= m.TexturePath(t).find(art) != string::npos;
                Check(has && fs::exists(string("assets/props/") + art + ".png"), string(r.id) + " has its " + art);
            }
            bool floor = false, dummies_elsewhere = false;
            for (const TileInstance& t : m.Tiles()) {
                floor |= m.TexturePath(t).find("college_floor") != string::npos;
                if (string(r.id) != "college_training") dummies_elsewhere |= m.TexturePath(t).find("training_dummy") != string::npos;
            }
            Check(floor && !dummies_elsewhere, string(r.id) + " is floored in the college's chequer, and is its own room");
            bool back = false;
            for (const Portal& p : m.Portals()) back |= p.target_map == "college_grounds";
            Check(back, string(r.id) + " lets back out onto the court");
        }
        {
            Map cls, hall;
            cls.Load("maps/college_classroom.mx");
            hall.Load("maps/fernhollow_college.mx");
            int pupils = 0, desks = 0, lector = 0, council = 0;
            for (const NpcDef& n : cls.Npcs()) { pupils += n.id.rfind("npc_college_pupil_", 0) == 0; lector += n.id == "npc_college_lector"; }
            for (const TileInstance& t : cls.Tiles()) desks += cls.TexturePath(t).find("college_desk") != string::npos;
            for (const NpcDef& n : hall.Npcs()) council += n.id == "npc_magister" || n.id.rfind("npc_councillor_", 0) == 0;
            Check(lector == 1 && pupils >= 4 && desks >= 8 && desks > pupils,
                  "a lector, a class of " + std::to_string(pupils) + " and " + std::to_string(desks) + " desks: somewhere to sit");
            Check(council == 3, "and a council of three in the hall, the Magister among them");
        }

        // --- the practice hall, watched --------------------------------------------------------------
        {
            World w;
            w.player.Init(ctx, "player_wayfarer");
            Check(w.LoadMap("college_training", "entrance", ctx), "into the practice hall");
            int casters = 0;
            for (const auto& n : w.npcs) casters += n->Practises();
            Check(casters == 4, "four of them at the head of four lanes");
            const int hp0 = w.player.hp;
            const int magic0 = w.player.skills.Xp(SKILL_MAGIC);
            size_t most = 0, shown = 0, real = 0;
            bool cast_seen = false;
            // Stood in the middle of a lane, in the line of fire, for twenty seconds.
            const float lane_y = 6.0f * 32.0f + 20.0f;
            for (int f = 0; f < 60 * 20; ++f) {
                w.player.x = 9.0f * 32.0f;
                w.player.y = lane_y;
                w.Update(kFrame, ctx);
                most = std::max(most, w.projectiles.size());
                for (const Projectile& p : w.projectiles) (p.show ? shown : real) += 1;
                for (const auto& n : w.npcs) cast_seen |= n->Casting();
            }
            Check(cast_seen && most >= 1 && shown > 0, "they cast, and the bolts fly down the lanes");
            Check(real == 0, "every bolt in the room is a practice bolt");
            Check(w.player.hp == hp0, "twenty seconds stood in a lane costs nobody anything");
            Check(w.ground_effects.empty(), "and a practice fire bolt leaves nothing burning on the floor");
            Check(w.player.skills.Xp(SKILL_MAGIC) == magic0, "watching teaches no Magic, which would have been a trick worth knowing");
            // The player's own spells find nothing to land on either: straw is scenery.
            Check(w.enemies.empty(), "and the straw men are straw: nothing in here can be fought for experience");
        }

        // --- Wynn's ---------------------------------------------------------------------------------
        {
            Map moss, shop;
            Check(moss.Load("maps/mossvale.mx") && shop.Load("maps/mossvale_weavers.mx") && shop.IsInterior(),
                  "Mossvale, and Wynn's house in it, load");
            const Portal* door = nullptr;
            for (const Portal& p : moss.Portals()) if (p.target_map == "mossvale_weavers") door = &p;
            Check(door != nullptr, "her door is on the village");
            bool outside = false, loom_outside = false;
            for (const NpcDef& n : moss.Npcs()) outside |= n.id == "npc_wynn";
            for (const MapObject& o : moss.Objects()) loom_outside |= o.station == "loom";
            Check(!outside && !loom_outside, "she and her loom are not out on the square any more");
            // Away from the anvil: across the village from it.
            const MapObject* anvil = nullptr;
            for (const MapObject& o : moss.Objects()) if (o.station == "anvil") anvil = &o;
            Check(anvil != nullptr, "the village anvil is where it was");
            if (anvil && door) {
                const float far = std::hypot(anvil->x - door->rect.x, anvil->y - door->rect.y);
                Check(far > 800.0f, "and her door is a long way from it (" + std::to_string(static_cast<int>(far)) + " px; the stall was about 400)");
            }
            bool wynn = false, loom = false;
            for (const NpcDef& n : shop.Npcs()) wynn |= n.id == "npc_wynn" && n.shop == "mossvale_clothier";
            for (const MapObject& o : shop.Objects()) loom |= o.id == "loom_weaver" && o.station == "loom";
            Check(wynn && loom, "she is inside, selling, with her loom");
            std::map<string, int> has;
            for (const TileInstance& t : shop.Tiles()) {
                const string& tex = shop.TexturePath(t);
                for (const char* art : {"mannequin_", "tapestry_", "fabric_shelf", "fabric_rolls", "cutting_table", "spinning_wheel", "shop_counter"})
                    if (tex.find(art) != string::npos) has[art] += 1;
            }
            Check(has["mannequin_"] >= 3 && has["tapestry_"] >= 3, "with forms dressed in her work (" + std::to_string(has["mannequin_"]) +
                                                                       ") and hangings on the walls (" + std::to_string(has["tapestry_"]) + ")");
            Check(has["fabric_shelf"] >= 2 && has["fabric_rolls"] >= 1 && has["cutting_table"] == 1 && has["spinning_wheel"] == 1 &&
                  has["shop_counter"] == 1, "bolts on the shelves, a cutting table, her wheel, and a counter to sell over");
            for (const char* art : {"clothier_shop", "mannequin_robe", "mannequin_dress", "mannequin_cloak", "tapestry_blue",
                                    "tapestry_red", "tapestry_green", "fabric_shelf", "fabric_rolls", "cutting_table"})
                Check(fs::exists(string("assets/props/") + art + ".png"), string(art) + " is drawn");
        }
    }

    Section("what a spell looks like in the air");
    {
        GameContext ctx;
        std::mt19937 rng(717);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        ctx.sprites = &sprites; ctx.items = &items; ctx.loot = &loot; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng; ctx.trees = &trees; ctx.projectiles = &projectiles; ctx.spells = &spells;
        constexpr float kFrame = 1.0f / 60.0f;

        // --- the pictures ---------------------------------------------------------------------
        // Each element is thrown as the thing it is, drawn for the purpose, and
        // not as whatever icon was nearest.
        struct Look { const char* bolt; const char* art; Element element; bool upright; };
        for (const Look& l : {Look{"bolt_fire", "fireball", Element::Fire, false},
                              Look{"bolt_fire_greater", "fireball_greater", Element::Fire, false},
                              Look{"bolt_water", "water_orb", Element::Water, true},
                              Look{"bolt_water_greater", "water_orb_greater", Element::Water, true},
                              Look{"bolt_earth", "rock_shard", Element::Earth, true},
                              Look{"bolt_earth_greater", "rock_shard_greater", Element::Earth, true},
                              Look{"bolt_air", "gust", Element::Air, false},
                              Look{"bolt_air_greater", "gust_greater", Element::Air, false}}) {
            const ProjectileDef* d = projectiles.Get(l.bolt);
            Check(d != nullptr, string(l.bolt) + " is a projectile");
            if (!d) continue;
            Check(d->sprite == string("assets/effects/") + l.art + ".png", string(l.bolt) + " is drawn as a " + l.art);
            Check(d->frames >= 4 && d->fps > 0.0f, string(l.bolt) + " moves as it flies: a strip, not a still");
            Check(!d->spin, string(l.bolt) + " is not a picture spun round");
            Check(d->upright == l.upright, string(l.bolt) + (l.upright ? " keeps its light top-left whichever way it goes"
                                                                       : " is turned to the way it is going"));
            Check(d->shed == l.element, string(l.bolt) + " sheds what it is made of");
            Check(d->tint.r == 255 && d->tint.g == 255 && d->tint.b == 255,
                  string(l.bolt) + " is its own colours: an element's tint over it would put out the white of a fire");
            Check(d->scale == 1.0f, string(l.bolt) + " is drawn pixel for pixel");
        }
        // Water is upright, so the way it is going has to be said by something
        // else: what streams off the back of it.
        for (const char* id : {"bolt_water", "bolt_water_greater"})
            if (const ProjectileDef* d = projectiles.Get(id))
                Check(!d->tail.empty() && fs::exists(d->tail) && d->tail_frames >= 4, string(id) + " has a wake behind it");
        if (const ProjectileDef* d = projectiles.Get("bolt_fire")) Check(d->glow > 0.0f, "a fireball lights the ground under it");
        if (const ProjectileDef* d = projectiles.Get("arrow"))
            Check(d->shed == Element::None && d->frames == 1, "an arrow is a still, and sheds nothing");
        Check(fs::exists("assets/effects/glow.png"), "the glow's picture exists");

        // Every strip is whole frames, held at a point inside one, and the
        // greater of a pair is the bigger.
        const auto frame_of = [&](const string& art, int frames, float& w, float& h) {
            SDL_Surface* img = IMG_Load(art.c_str());
            if (!img) return false;
            const bool whole = frames > 0 && img->w % frames == 0;
            w = static_cast<float>(img->w) / std::max(1, frames);
            h = static_cast<float>(img->h);
            SDL_DestroySurface(img);
            return whole;
        };
        for (const auto& kv : projectiles.All()) {
            const ProjectileDef& d = kv.second;
            float w = 0, h = 0;
            Check(frame_of(d.sprite, d.frames, w, h), kv.first + "'s strip is a whole number of frames");
            Check(d.pivot_x < w && d.pivot_y < h, kv.first + " is held at a point inside its frame");
            if (!d.tail.empty()) {
                Check(frame_of(d.tail, d.tail_frames, w, h), kv.first + "'s tail is a whole number of frames");
                Check(d.tail_pivot_x < w && d.tail_pivot_y < h, kv.first + "'s tail is held inside its frame");
            }
            // What is seen should be about what strikes: a picture three times
            // the circle sails through things it looks to have hit.
            if (d.frames > 1 && d.upright) Check(h <= d.radius * 4.0f, kv.first + " is no bigger than about what it hits");
        }
        for (const char* el : {"fire", "water", "earth", "air"}) {
            const ProjectileDef* small = projectiles.Get(string("bolt_") + el);
            const ProjectileDef* great = projectiles.Get(string("bolt_") + el + "_greater");
            float sw = 0, sh = 0, gw = 0, gh = 0;
            if (small && great && frame_of(small->sprite, small->frames, sw, sh) && frame_of(great->sprite, great->frames, gw, gh))
                Check(gw > sw && gh > sh, string("the greater ") + el + " is the bigger");
        }

        // --- in the air -----------------------------------------------------------------------
        const auto field = [&](World& w) {
            w.player.Init(ctx, "player_wayfarer");
            if (!w.LoadMap("overworld", "start", ctx)) return false;
            w.enemies.clear();
            w.clock.Set(1, 12.0f);
            return true;
        };
        const auto loose = [&](World& w, const char* bolt, float dx, float dy) {
            w.SpawnProjectile(bolt, w.player.x + dx * 24.0f, w.player.y - 20.0f + dy * 24.0f, dx, dy,
                              w.player.Profile(), AttackStyle::Magic, 1.0f, true, ctx);
        };
        for (const char* bolt : {"bolt_fire", "bolt_water", "bolt_earth", "bolt_air", "bolt_eldritch"}) {
            World w;
            if (!field(w)) { Check(false, "the overworld loads for a bolt"); continue; }
            loose(w, bolt, 1.0f, 0.0f);
            for (int i = 0; i < 10; ++i) w.Update(kFrame, ctx);
            Check(!w.projectiles.empty(), string(bolt) + " is still in the air a sixth of a second on");
            Check(w.motes.size() >= 3, string(bolt) + " sheds as it flies (" + std::to_string(w.motes.size()) + ")");
            // Behind it, not ahead: it is a trail.
            bool ahead = false;
            if (!w.projectiles.empty())
                for (const Mote& m : w.motes) ahead |= m.x > w.projectiles.front().x + 6.0f;
            Check(!ahead, string(bolt) + " leaves it behind it");
        }
        {
            World w;
            if (field(w)) {
                loose(w, "arrow", 1.0f, 0.0f);
                for (int i = 0; i < 10; ++i) w.Update(kFrame, ctx);
                Check(w.motes.empty(), "an arrow leaves nothing in the air behind it");
            }
        }

        // --- where it lands --------------------------------------------------------------------
        // Against the waystone at Havenbrook, as the wall test does.
        for (const char* bolt : {"bolt_fire", "bolt_water", "bolt_earth", "bolt_air"}) {
            World w;
            w.player.Init(ctx, "player_wayfarer");
            if (!w.LoadMap("town_havenbrook", "waystone", ctx)) { Check(false, "Havenbrook loads for a bolt"); continue; }
            w.enemies.clear();
            w.player.y += 40.0f;
            loose(w, bolt, 0.0f, -1.0f);
            // The most that appears in any one frame: a trail is one or two,
            // and breaking on something is a dozen at once. (Air comes back off
            // the stone and flies on, so it is the frame and not the end.)
            size_t jump = 0;
            bool struck = false;
            for (int i = 0; i < 90 && !struck; ++i) {
                const size_t before = w.motes.size();
                w.Update(kFrame, ctx);
                if (w.motes.size() > before) jump = std::max(jump, w.motes.size() - before);
                struck = !w.impacts.empty();
            }
            Check(struck, string(bolt) + " meets the stone");
            Check(jump >= 7, string(bolt) + " breaks on it: what it was made of is thrown back (" +
                                 std::to_string(jump) + " at once)");
        }
        // What a fireball leaves burning stands in flames for as long as it burns.
        {
            World w;
            w.player.Init(ctx, "player_wayfarer");
            if (w.LoadMap("town_havenbrook", "waystone", ctx)) {
                w.enemies.clear();
                w.player.y += 40.0f;
                loose(w, "bolt_fire", 0.0f, -1.0f);
                for (int i = 0; i < 90; ++i) w.Update(kFrame, ctx);       // landed, and the burst of it long gone
                bool burning = false;
                for (const GroundEffect& g : w.ground_effects) burning |= g.element == Element::Fire && g.Active();
                int tongues = 0;
                for (const Mote& m : w.motes) tongues += m.tall > 0.0f;
                Check(burning && tongues >= 4, "burning ground stands in tongues of flame (" + std::to_string(tongues) + ")");
            }
        }

        // --- and none of it is the game's business ------------------------------------------------
        // Two fields, the same dice: a fireball over one and an arrow over the
        // other. Embers are shed over the first and nothing over the second,
        // and the game's dice are where they were in both.
        {
            std::mt19937 dice_a(99), dice_b(99);
            GameContext ca = ctx, cb = ctx;
            ca.rng = &dice_a; cb.rng = &dice_b;
            World a, b;
            a.player.Init(ca, "player_wayfarer"); b.player.Init(cb, "player_wayfarer");
            if (a.LoadMap("overworld", "start", ca) && b.LoadMap("overworld", "start", cb)) {
                a.enemies.clear(); b.enemies.clear();
                a.SpawnProjectile("bolt_fire", a.player.x + 24.0f, a.player.y - 20.0f, 1, 0, a.player.Profile(),
                                  AttackStyle::Magic, 1.0f, true, ca);
                b.SpawnProjectile("arrow", b.player.x + 24.0f, b.player.y - 20.0f, 1, 0, b.player.Profile(),
                                  AttackStyle::Ranged, 1.0f, true, cb);
                for (int i = 0; i < 30; ++i) { a.Update(kFrame, ca); b.Update(kFrame, cb); }
                Check(!a.motes.empty() && b.motes.empty(), "embers over one field and nothing over the other");
                Check(dice_a() == dice_b(), "and the game's dice have not been touched by the embers");
            }
        }
        // A room full of mages is a great many embers, and no more than that.
        {
            World w;
            if (field(w)) {
                size_t most = 0;
                for (int i = 0; i < 120; ++i) {
                    for (int k = 0; k < 6; ++k) {
                        const float a = 6.2831853f * (i * 6 + k) / 97.0f;
                        loose(w, "bolt_fire_greater", cosf(a), sinf(a));
                    }
                    w.Update(kFrame, ctx);
                    most = std::max(most, w.motes.size());
                }
                Check(most > 300 && most <= 700, "seven hundred bolts' worth of embers is capped (" + std::to_string(most) + ")");
            }
        }

        // --- on a friend's machine ------------------------------------------------------------------
        // A guest is told where the shots are and nothing else. It makes its
        // own trail from that, and a shot it stops hearing about has met
        // something: it breaks where it last was.
        {
            World w;
            if (field(w)) {
                w.visiting = true;
                const ProjectileDef* def = projectiles.Get("bolt_water");
                float x = w.player.x + 30.0f;
                const float y = w.player.y - 20.0f;
                for (int i = 0; i < 12 && def; ++i) {
                    // As coop::Guest does: the list made again from the host's word.
                    w.projectiles.clear();
                    Projectile p;
                    p.def = def; p.net_id = 4242; p.x = x; p.y = y; p.vx = def->speed; p.vy = 0.0f;
                    p.element = def->element;
                    w.projectiles.push_back(p);
                    w.Update(kFrame, ctx);
                    x += def->speed * kFrame;
                }
                Check(w.motes.size() >= 3, "a friend's machine sheds a trail from the shots it is told of (" +
                                               std::to_string(w.motes.size()) + ")");
                const size_t before = w.motes.size();
                w.projectiles.clear();
                w.Update(kFrame, ctx);
                bool ring = false;
                for (const Mote& m : w.motes) ring |= m.kind == Mote::Kind::Ring;
                Check(w.motes.size() >= before + 8 && ring, "and one it stops hearing of breaks where it last was");
                // Leaving takes it all along: nothing bursts on the far side of a door.
                w.visiting = false;
            }
        }
    }

    Section("waystones: three towns, woken by hand");
    {
        // --- where they stand ------------------------------------------------------------
        // One in each town and none anywhere else: not the wilds, not a dungeon,
        // not the Reverie. The road to a town is walked once; everything that
        // is not a town is always walked.
        const std::map<string, string> towns = {
            {"town_havenbrook", "waystone_havenbrook"},
            {"mossvale",        "waystone_mossvale"},
            {"fernhollow",      "waystone_fernhollow"},
        };
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            vector<const MapObject*> stones;
            for (const MapObject& o : m.Objects()) if (o.type == "waystone") stones.push_back(&o);
            const auto town = towns.find(id);
            if (town == towns.end()) {
                Check(stones.empty(), string(id) + " is not a town and has no waystone");
                continue;
            }
            Check(stones.size() == 1, string(id) + " has one waystone, and only one");
            if (stones.empty()) continue;
            const MapObject& stone = *stones.front();
            Check(stone.id == town->second, string(id) + "'s stone is " + town->second);
            Check(stone.sprite == "assets/props/waystone.png" && fs::exists(stone.sprite),
                  string(id) + ": asleep, it is drawn dark");
            Check(stone.sprite_open == "assets/props/waystone_lit.png" && fs::exists(stone.sprite_open),
                  string(id) + ": awake, it is drawn lit");
            SDL_FPoint arrive{};
            const bool has = m.Spawn("waystone", arrive);
            Check(has, string(id) + " has somewhere to arrive by it");
            if (!has) continue;
            const SDL_FRect feet = {arrive.x - 8.0f, arrive.y - 10.0f, 16.0f, 10.0f};
            Check(!m.Blocked(feet), string(id) + ": and it is not inside the stone, or anything else");
            Check(std::hypot(arrive.x - stone.x, arrive.y - stone.y) < 64.0f,
                  string(id) + ": you arrive at the stone, not across the square from it");
            bool on_a_door = false;
            for (const Portal& p : m.Portals()) on_a_door |= !p.requires_interact && RectsOverlap(feet, p.rect);
            Check(!on_a_door, string(id) + ": and not on a way out of town");
        }

        // --- the first touch wakes it, and only the second goes anywhere ------------------
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        World w;
        GameContext wctx;
        std::mt19937 rng(11);
        wctx.sprites = &sprites; wctx.items = &items; wctx.enemies = &enemy_db;
        wctx.quests = &log; wctx.rng = &rng;
        w.player.Init(wctx, "player_hero");
        w.player.inventory.SetDatabase(&items);
        constexpr float kFrame = 1.0f / 60.0f;
        Check(w.LoadMap("town_havenbrook", "default", wctx), "arrive in Havenbrook on foot");
        Check(!w.Flagged("waystone_havenbrook") && !w.Flagged("waystone_mossvale") &&
              !w.Flagged("waystone_fernhollow"), "a new character has woken nothing");

        const auto stone_here = [&]() -> const MapObject* {
            for (const MapObject& o : w.CurrentMap().Objects()) if (o.type == "waystone") return &o;
            return nullptr;
        };
        // Stood where the stone's own spawn puts you, which is also the proof
        // that whoever arrives by it can reach it without taking a step.
        const auto stand_by_it = [&]() {
            SDL_FPoint at{};
            w.CurrentMap().Spawn("waystone", at);
            w.player.x = at.x; w.player.y = at.y;
            w.Update(kFrame, wctx);
        };
        const auto count = [](const vector<WorldRequest>& reqs, WorldRequest::Type t) {
            int n = 0;
            for (const WorldRequest& r : reqs) n += r.type == t;
            return n;
        };

        const MapObject* stone = stone_here();
        Check(stone != nullptr, "the stone is in the square");
        if (stone) {
            stand_by_it();
            Check(w.player.interact.kind == InteractTarget::Object && w.player.interact.label == "Wake the waystone",
                  "where you would arrive by it is where you can reach it, and asleep it asks to be woken");
            w.TakeRequests();
            w.TryInteract(wctx);
            vector<WorldRequest> reqs = w.TakeRequests();
            Check(w.Flagged("waystone_havenbrook"), "a hand on it wakes it");
            Check(count(reqs, WorldRequest::Type::Travel) == 0, "and that is all the first touch does");
            Check(count(reqs, WorldRequest::Type::Toast) == 1, "but it says what it is for");

            w.Update(kFrame, wctx);
            Check(w.player.interact.label == "Touch the waystone", "awake, it asks to be touched");
            w.TryInteract(wctx);
            reqs = w.TakeRequests();
            Check(count(reqs, WorldRequest::Type::Travel) == 1, "the second touch opens the way");
            for (const WorldRequest& r : reqs)
                if (r.type == WorldRequest::Type::Travel)
                    Check(r.id == "waystone_havenbrook", "and says which stone you are standing at");
            Check(!w.Flagged("waystone_mossvale") && !w.Flagged("waystone_fernhollow"),
                  "waking one wakes that one: the others are still to be walked to");
        }

        // --- one stone is one stone: Mossvale's has to be walked to and woken too ----------
        Check(w.LoadMap("mossvale", "from_trail", wctx), "walk to Mossvale");
        Check(w.Flagged("waystone_havenbrook"), "Havenbrook's stays awake behind you");
        stone = stone_here();
        if (stone) {
            stand_by_it();
            Check(w.player.interact.label == "Wake the waystone", "Mossvale's is asleep until it is touched");
            w.TakeRequests();
            w.TryInteract(wctx);
            Check(w.Flagged("waystone_mossvale") && count(w.TakeRequests(), WorldRequest::Type::Travel) == 0,
                  "and wakes the same way");
        }

        // --- the going itself ------------------------------------------------------------
        // What the panel does once a woken stone is chosen: an ordinary
        // transition, to the spawn by the far stone.
        Check(w.RequestTransition("town_havenbrook", "waystone"), "choosing Havenbrook asks for the way there");
        for (int i = 0; i < 240 && w.MapId() != "town_havenbrook"; ++i) w.Update(kFrame, wctx);
        Check(w.MapId() == "town_havenbrook", "and the fade takes you");
        stone = stone_here();
        if (stone) {
            Check(std::hypot(w.player.x - stone->x, w.player.y - stone->y) < 64.0f,
                  "you come out standing at Havenbrook's stone");
            Check(!w.CurrentMap().Blocked(w.player.Bounds()), "on open ground");
            for (int i = 0; i < 90; ++i) w.Update(kFrame, wctx);      // let the fade finish
            Check(w.player.interact.label == "Touch the waystone", "with the stone in reach to go on again");
        }

        // --- a woken stone is part of the save ---------------------------------------------
        {
            const string was = SaveSystem::Directory();
            const fs::path dir = fs::temp_directory_path() / "dreamquest_selftest_waystones";
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
            SaveSystem::SetDirectory(dir.string());
            Check(SaveSystem::Save(1, w, log, 30.0f), "a save with two stones woken writes");
            World back;
            back.player.Init(wctx, "player_hero");
            QuestLog log2;
            log2.LoadDefinitions("data/quests.json");
            float played = 0.0f;
            Check(SaveSystem::Load(1, back, log2, wctx, played), "and loads");
            Check(back.Flagged("waystone_havenbrook") && back.Flagged("waystone_mossvale") &&
                  !back.Flagged("waystone_fernhollow"), "with the same two awake and the third still asleep");
            SaveSystem::SetDirectory(was);
            fs::remove_all(dir, ec);
        }
    }

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

    Section("woodland journeys and delivery quests");
    {
        Inventory inv(&items);
        QuestLog log;
        log.LoadDefinitions("data/quests.json");
        auto event = [&](ObjectiveType type, const string& target,
                         const string& secondary = "", const string& where = "", int amount = 1) {
            QuestEvent e;
            e.type = type; e.target = target; e.secondary = secondary;
            e.map_id = where; e.amount = amount;
            log.Notify(e, inv);
        };
        log.Start("q_clear_the_trail");
        log.Start("q_thin_the_herd");
        event(ObjectiveType::Kill, "boar", "", "overworld", 6);
        Check(log.Counter("q_clear_the_trail") == 0, "boar outside the Whisperwood do not clear its trail");
        event(ObjectiveType::Kill, "boar", "", "whisperwood_trail", 6);
        Check(log.IsComplete("q_clear_the_trail"), "six Whisperwood boar clear the trail");
        Skills skills;
        Check(log.CanStart("q_trail_wardens", skills), "clearing the trail unlocks the wardens' notice");
        log.Start("q_trail_wardens");
        event(ObjectiveType::Kill, "fox", "", "fernhollow", 8);
        Check(log.Counter("q_trail_wardens") == 0, "Fernhollow foxes do not count as trail wardens' targets");
        event(ObjectiveType::Kill, "fox", "", "whisperwood_trail", 8);
        Check(log.IsComplete("q_trail_wardens"), "the trail wardens' hunt completes in its own area");

        log.Start("q_word_to_fernhollow");
        event(ObjectiveType::Talk, "npc_wendel");
        Check(log.Stage("q_word_to_fernhollow") == 0, "talking to Wendel cannot substitute for his remedy");
        DialogueContext dc;
        dc.quests = &log; dc.inventory = &inv; dc.skills = &skills;
        const DialogueOption& remedy = dialogue.Get("wendel_root")->options.front();
        Check(!EvaluateCondition(remedy.condition, dc), "the remedy option is hidden without a bottle");
        inv.Add("herbal_tonic", 1);
        Check(EvaluateCondition(remedy.condition, dc), "carrying the remedy enables the handover");
        event(ObjectiveType::Deliver, "herbal_tonic", "npc_mira");
        Check(log.Stage("q_word_to_fernhollow") == 0, "the remedy must go to Wendel, not another NPC");
        Check(inv.Remove("herbal_tonic", 1), "the remedy is handed over");
        event(ObjectiveType::Deliver, "herbal_tonic", "npc_wendel");
        Check(log.Stage("q_word_to_fernhollow") == 1, "handing over the remedy starts the return journey");
        event(ObjectiveType::Talk, "npc_oona");
        Check(log.IsComplete("q_word_to_fernhollow"), "returning to Oona completes the errand");
        bool oona_thanks = false;
        for (const auto& option : dialogue.Get("oona_root")->options)
            if (option.next == "oona_done" && EvaluateCondition(option.condition, dc)) oona_thanks = true;
        Check(oona_thanks, "Oona's closing conversation remains available once the errand is done");

        log.Start("q_old_offering");
        inv.Add("raw_boar", 2);
        log.RefreshCollectObjectives(inv);
        Check(log.IsActive("q_old_offering"), "carrying the offering does not hand it to Mira remotely");
        Check(inv.Remove("raw_boar", 2), "Mira receives both haunches");
        event(ObjectiveType::Deliver, "raw_boar", "npc_mira", "", 2);
        Check(log.IsComplete("q_old_offering"), "giving Mira the offering completes her quest");

        World world;
        GameContext ctx;
        std::mt19937 rng(42);
        ctx.sprites = &sprites; ctx.items = &items; ctx.enemies = &enemy_db;
        ctx.quests = &log; ctx.rng = &rng;
        world.player.Init(ctx, "player_hero");
        log.Start("q_road_beneath_leaves");
        Check(world.LoadMap("overworld", "start", ctx), "the journey begins in the Hollowmarch");
        const size_t tiles = world.CurrentMap().Tiles().size();
        const size_t enemies = world.enemies.size();
        const float px = world.player.x, py = world.player.y;
        Check(!world.LoadMap("__selftest_missing_destination__", "", ctx), "a missing destination is refused");
        Check(world.MapId() == "overworld" && world.CurrentMap().Loaded() &&
              world.CurrentMap().Tiles().size() == tiles && world.enemies.size() == enemies &&
              world.player.x == px && world.player.y == py,
              "a failed map load preserves the area, entities, and player position");
        Check(world.LoadMap("whisperwood_trail", "from_hollowmarch", ctx), "enter the forest");
        Check(log.Stage("q_road_beneath_leaves") == 1, "entering the forest records the first journey stage");
        Check(world.LoadMap("mossvale", "from_trail", ctx), "reach Mossvale");
        Check(log.Stage("q_road_beneath_leaves") == 2, "Mossvale advances the itinerary to Fernhollow");
        QuestLog restored;
        restored.LoadDefinitions("data/quests.json");
        restored.FromJson(log.ToJson());
        Check(restored.Stage("q_road_beneath_leaves") == 2, "the woodland itinerary survives a save");
        log.TakeJustCompleted();
        Check(world.LoadMap("whisperwood_trail", "from_mossvale", ctx), "return to the forest fork");
        Check(world.LoadMap("fernhollow", "from_trail", ctx), "reach Fernhollow");
        Check(log.IsComplete("q_road_beneath_leaves"), "the whole woodland itinerary completes");
        Check(log.TakeJustCompleted().size() == 1, "the journey rewards are queued once");
        Check(world.LoadMap("fernhollow", "default", ctx), "revisit Fernhollow");
        Check(log.TakeJustCompleted().empty(), "revisiting cannot duplicate the journey reward");
    }


    Section("every character holds the weapon they are holding");
    {
        // The three playable characters are one rig in three sets of clothes,
        // and every tier's weapon is rendered once, in the hero's hand. The
        // warden and the wayfarer used to be asked for sheets of their own,
        // which do not exist: a bow and a staff were both drawn as the plain
        // tinted blade, and the log said so once for every weapon and clip.
        const SpriteDef* hero = sprites.Get("player_hero");
        Check(hero && hero->weapon_dir.empty() && hero->dir == "assets/characters/player_hero/", "the hero's weapons are the hero's own");
        for (const char* who : {"player_hero", "player_warden", "player_wayfarer"}) {
            const SpriteDef* def = sprites.Get(who);
            Check(def && (string(who) == "player_hero" || def->weapon_dir == "assets/characters/player_hero/"),
                  string(who) + " takes the weapon in hand from the hero's renders");
            if (!def) continue;
            // What each sets out with, and what they might pick up, through
            // every clip they play with it in hand.
            int asked = 0, missing = 0;
            string first;
            for (const char* model : {"sword_wood", "bow_wood", "staff_wood", "sword_bronze", "spear_iron", "bow_enchanted", "staff_dracon"})
                for (const char* played : {"idle", "walk", "run", "sprint", "jump", "attack", "block", "hurt", "death"}) {
                    // A spear is thrust, not swung: it has no sheet for a clip it never plays.
                    const string clip = (string(played) == "attack" && string(model).rfind("spear", 0) == 0) ? "thrust" : played;
                    const AnimClip* c = def->Find(clip);
                    if (!c) continue;
                    for (const AnimLayer& layer : c->layers) {
                        const string sheet = def->WeaponSheet(layer.sheet, model);
                        if (layer.slot != LayerSlot::WeaponFront || sheet.empty()) continue;
                        ++asked;
                        if (!fs::exists(sheet)) { ++missing; if (first.empty()) first = sheet; }
                    }
                }
            Check(asked >= 60 && missing == 0, string(who) + ": every weapon sheet the game will ask for is on disk (" +
                  std::to_string(asked) + " asked" + (first.empty() ? string() : ", first missing " + first) + ")");
        }
        const SpriteDef* wayfarer = sprites.Get("player_wayfarer");
        if (wayfarer)
            Check(wayfarer->WeaponSheet("assets/characters/player_wayfarer/layers/idle_4_weapon_front.png", "staff_wood") ==
                      "assets/characters/player_hero/layers/idle_4_weapon_staff_wood.png" &&
                  wayfarer->WeaponSheet("assets/characters/player_wayfarer/layers/idle_3_body.png", "staff_wood").empty() &&
                  wayfarer->WeaponSheet("assets/characters/player_wayfarer/layers/idle_4_weapon_front.png", "").empty(),
                  "the wayfarer's staff is the hero's render of it; a layer that is not a weapon, or no weapon, asks for nothing");
    }

    // =========================================================================
    //  Co-op, milestone 0: the wire, the door and the chat line
    // =========================================================================
    //
    // No world is shared yet. What is checked here is everything the later
    // milestones stand on: bytes that mean the same on two machines, a
    // transport that keeps its promises, a door that turns away the wrong
    // build by name, seats, a roster everyone agrees on, and a typed line
    // arriving on the other screen. A server and its clients run in this one
    // process over the loopback transport, and then once more over real UDP
    // on 127.0.0.1.
    Section("co-op M0: bytes on the wire");
    {
        using namespace net;
        ByteWriter w;
        w.U8(0xAB); w.U16(0x1234); w.U32(0x11223344u); w.U64(0x0102030405060708ull);
        w.I16(-2); w.I8(-3); w.Bool(true); w.Str("oak", 8);
        const Bytes b = w.Take();
        Check(b.size() == 1 + 2 + 4 + 8 + 2 + 1 + 1 + 2 + 3, "a writer writes exactly the widths asked for");
        Check(b[1] == 0x34 && b[2] == 0x12 && b[3] == 0x44 && b[6] == 0x11 && b[7] == 0x08 && b[14] == 0x01,
              "little-endian, whatever the machine");
        ByteReader r(b);
        Check(r.U8() == 0xAB && r.U16() == 0x1234 && r.U32() == 0x11223344u &&
              r.U64() == 0x0102030405060708ull && r.I16() == -2 && r.I8() == -3 && r.Bool() &&
              r.Str(8) == "oak" && r.Done(), "and a reader reads them back");
        Check(r.U8() == 0 && !r.Ok() && !r.Done(), "reading past the end fails and yields zero");

        ByteWriter longer;
        longer.Str("a very long name indeed", 64);
        const Bytes lb = longer.Take();
        ByteReader strict(lb);
        Check(strict.Str(8).empty() && !strict.Ok(), "a string past the reader's limit is refused, not truncated");
        ByteWriter cut;
        cut.Str("a very long name indeed", 6);
        const Bytes cb = cut.Take();
        ByteReader cr(cb);
        Check(cr.Str(6) == "a very" && cr.Done(), "a writer cuts to the limit so it never writes what would be refused");
        Bytes lying = {5, 0, 'a', 'b'};     // says five, carries two
        ByteReader lr(lying);
        Check(lr.Str(16).empty() && !lr.Ok(), "a length that lies about what follows is caught");

        // Every message, there and back, and every truncation of it refused.
        const auto survives = [&](const char* name, const Bytes& bytes, const std::function<bool(const Bytes&)>& decode) {
            Check(decode(bytes), string(name) + " round-trips");
            bool any_short = false;
            for (size_t n = 0; n < bytes.size(); ++n)
                any_short |= decode(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)));
            Check(!any_short, string(name) + ": no truncation of it decodes");
            Bytes padded = bytes; padded.push_back(0);
            Check(!decode(padded), string(name) + ": nor does it with a byte left over");
        };
        Hello hello; hello.data_hash = 0xDEADBEEFCAFEF00Dull; hello.maps_hash = 7; hello.name = "Oona"; hello.look = "player_warden";
        survives("Hello", Encode(hello), [&](const Bytes& x) {
            Hello o; return Decode(x, o) && o.magic == PROTOCOL_MAGIC && o.version == PROTOCOL_VERSION &&
                            o.data_hash == hello.data_hash && o.maps_hash == 7 && o.name == "Oona" && o.look == "player_warden"; });
        Welcome welcome; welcome.seat = 2; welcome.world_name = "Hollowmarch";
        welcome.roster = {{0, "Dada", "player_hero", true}, {2, "Oona", "player_warden", false}};
        survives("Welcome", Encode(welcome), [&](const Bytes& x) {
            Welcome o; return Decode(x, o) && o.seat == 2 && o.max_seats == MAX_SEATS && o.tick_rate == 60 &&
                              o.world_name == "Hollowmarch" && o.roster.size() == 2 && o.roster[0].host &&
                              o.roster[1].name == "Oona" && o.roster[1].seat == 2 && !o.roster[1].host; });
        survives("Refuse", Encode(Refuse{RefuseReason::Data, "differs"}), [&](const Bytes& x) {
            Refuse o; return Decode(x, o) && o.reason == RefuseReason::Data && o.text == "differs"; });
        survives("Roster", Encode(Roster{welcome.roster}), [&](const Bytes& x) {
            Roster o; return Decode(x, o) && o.seats.size() == 2 && o.seats[1].look == "player_warden"; });
        survives("Say", Encode(Say{"to the mine?"}), [&](const Bytes& x) {
            Say o; return Decode(x, o) && o.text == "to the mine?"; });
        survives("Chat", Encode(Chat{1, "aye"}), [&](const Bytes& x) {
            Chat o; return Decode(x, o) && o.seat == 1 && o.text == "aye"; });
        {
            Say wrong;
            Check(!Decode(Encode(Chat{1, "aye"}), wrong), "a message is not decoded as another");
            Hello other = hello; other.version = PROTOCOL_VERSION + 1;
            Bytes future = Encode(other);
            future.resize(7);           // a later version may lay the rest out differently
            Hello seen;
            Check(Decode(future, seen) && seen.version == PROTOCOL_VERSION + 1,
                  "a Hello from another version still reads far enough to be refused by number");
        }

        Check(CleanLine("  to the\tmine?\n", 64) == "to the mine?", "a line is trimmed and its control characters dropped");
        Check(CleanLine("\x1b[31m\r\n", 64) == "[31m" && CleanLine(" \n\t ", 64).empty(), "and an empty one is empty");
        const string accented = "caf\xC3\xA9";      // five bytes, four characters
        Check(CleanLine(accented, 4) == "caf" && CleanLine(accented, 5) == accented,
              "a cut never leaves half a UTF-8 character");

        string host; uint16_t port = 0;
        Check(SplitAddress("dada-pc", host, port) && host == "dada-pc" && port == DEFAULT_PORT, "a bare name dials the default port");
        Check(SplitAddress(" dada-pc.tail1234.ts.net:7800 ", host, port) && host == "dada-pc.tail1234.ts.net" && port == 7800,
              "a MagicDNS name with a port splits");
        Check(SplitAddress("100.101.102.103", host, port) && host == "100.101.102.103" && port == DEFAULT_PORT, "a tailnet address");
        Check(SplitAddress("[fd7a:115c::1]:7801", host, port) && host == "fd7a:115c::1" && port == 7801, "a bracketed IPv6 literal");
        Check(!SplitAddress("", host, port) && !SplitAddress("   ", host, port) && !SplitAddress("pc:", host, port) &&
              !SplitAddress("pc:70000", host, port) && !SplitAddress("pc:0", host, port) && !SplitAddress("pc:77x7", host, port) &&
              !SplitAddress(":7777", host, port), "and nonsense is refused");
    }

    Section("co-op M0: both ends read the same data");
    {
        using namespace net;
        const DataHashes here = ComputeDataHashes(".");
        Check(here.data_files >= 10 && here.map_files >= 20, "data/ and maps/ are found (" +
              std::to_string(here.data_files) + " and " + std::to_string(here.map_files) + " files)");
        const DataHashes again = ComputeDataHashes(".");
        Check(here.data == again.data && here.maps == again.maps && here.data != here.maps, "hashing twice gives the same answer");
        Check(ShortHash(0x9F3A61C200000000ull) == "9f3a61c2", "a hash reads aloud as eight hex digits");

        // A clone with autocrlf on and a zip from one with it off must agree,
        // and any real difference must not.
        const fs::path scratch = fs::path("bin") / "selftest_net";
        std::error_code ec;
        fs::remove_all(scratch, ec);
        const auto write = [&](const char* folder, const char* file, const string& text) {
            fs::create_directories(scratch / folder, ec);
            std::ofstream out(scratch / folder / file, std::ios::binary);
            out << text;
        };
        write("lf",   "a.json", "{\n \"hp\": 40\n}\n");   write("lf",   "b.json", "[1,\n2]\n");
        write("crlf", "a.json", "{\r\n \"hp\": 40\r\n}\r\n"); write("crlf", "b.json", "[1,\r\n2]\r\n");
        write("hp",   "a.json", "{\n \"hp\": 41\n}\n");   write("hp",   "b.json", "[1,\n2]\n");
        write("name", "a.json", "{\n \"hp\": 40\n}\n");   write("name", "c.json", "[1,\n2]\n");
        write("more", "a.json", "{\n \"hp\": 40\n}\n");   write("more", "b.json", "[1,\n2]\n"); write("more", "z.json", "");
        write("more", "notes.txt", "not data");
        const auto hash = [&](const char* folder, int* n = nullptr) { return HashFolder((scratch / folder).string(), ".json", n); };
        int lf_files = 0, more_files = 0;
        const uint64_t lf = hash("lf", &lf_files);
        Check(lf_files == 2 && lf == hash("crlf"), "line endings do not change the hash");
        Check(lf != hash("hp"), "one hit point does");
        Check(lf != hash("name"), "a renamed file does");
        Check(lf != hash("more", &more_files) && more_files == 3, "an extra file does, and only files of the kind are counted");
        int none = -1;
        HashFolder((scratch / "nowhere").string(), ".json", &none);
        Check(none == 0, "a missing folder is no files, not a crash");
        fs::remove_all(scratch, ec);
    }

    Section("co-op M0: the loopback transport keeps ENet's promises");
    {
        using namespace net;
        const auto kinds = [](const vector<Packet>& ps) {
            string s;
            for (const Packet& p : ps) s += p.type == Packet::Type::Connected ? 'C' : p.type == Packet::Type::Disconnected ? 'D' : 'm';
            return s;
        };
        {
            LoopbackHub hub;
            auto lonely = hub.Client();
            Check(lonely->Connect("anyone", 7777) && kinds(lonely->Poll()) == "D" && lonely->Peers().empty(),
                  "dialling a hub nobody listens on is answered with a disconnect");
        }
        LoopbackHub hub;
        auto server = hub.Server();
        Check(server && !hub.Server(), "a hub has one listening end");
        Check(!server->Connect("x", 1), "which does not dial");
        auto a = hub.Client();
        auto b = hub.Client();
        Check(a->Connect("ignored", 0) && b->Connect("ignored", 0), "two clients dial it");
        const vector<Packet> arrivals = server->Poll();
        Check(kinds(arrivals) == "CC" && arrivals[0].peer != arrivals[1].peer && server->Peers().size() == 2,
              "and the server sees two different peers arrive");
        Check(kinds(a->Poll()) == "C" && a->Peers() == vector<PeerId>{LOOPBACK_SERVER}, "each client sees the server");
        b->Poll();
        const PeerId pa = arrivals[0].peer, pb = arrivals[1].peer;

        for (uint8_t i = 1; i <= 5; ++i) a->Send(LOOPBACK_SERVER, Channel::Reliable, Bytes{i});
        b->Send(LOOPBACK_SERVER, Channel::Unreliable, Bytes{9});
        const vector<Packet> got = server->Poll();
        bool ordered = got.size() == 6;
        for (size_t i = 0; ordered && i < 5; ++i) ordered = got[i].peer == pa && got[i].data == Bytes{static_cast<uint8_t>(i + 1)};
        Check(ordered && got[5].peer == pb && got[5].channel == Channel::Unreliable, "packets arrive in order, from who sent them, on their channel");
        server->Send(pb, Channel::Reliable, Bytes{42});
        Check(a->Poll().empty() && b->Poll().size() == 1, "and a reply goes to the one it was sent to");

        // A refusal has to be able to say why before the line drops.
        server->Send(pa, Channel::Reliable, Bytes{7});
        server->Disconnect(pa);
        Check(kinds(a->Poll()) == "mD" && a->Peers().empty(), "a disconnect arrives after what was sent before it");
        Check(kinds(server->Poll()) == "D" && server->Peers() == vector<PeerId>{pb}, "and the end that hung up is told too");
        a->Send(LOOPBACK_SERVER, Channel::Reliable, Bytes{1});
        Check(server->Poll().empty(), "nothing is heard from a closed line");

        hub.SetDelay(3);
        b->Send(LOOPBACK_SERVER, Channel::Reliable, Bytes{1});
        int polls = 0;
        while (polls < 10 && server->Poll().empty()) ++polls;
        Check(polls == 3, "a delayed hub holds a packet back that many polls (" + std::to_string(polls) + ")");
        hub.SetDelay(0);
        hub.DropUnreliable(3);
        for (int i = 0; i < 9; ++i) b->Send(LOOPBACK_SERVER, Channel::Unreliable, Bytes{1});
        for (int i = 0; i < 9; ++i) b->Send(LOOPBACK_SERVER, Channel::Reliable, Bytes{2});
        size_t lost = 0, kept = 0;
        for (const Packet& p : server->Poll()) (p.channel == Channel::Unreliable ? lost : kept)++;
        Check(lost == 6 && kept == 9, "a lossy hub loses unreliable packets and never reliable ones");
        hub.DropUnreliable(0);

        b.reset();
        Check(kinds(server->Poll()) == "D" && server->Peers().empty(), "a client that goes away is a disconnect");
        auto c = hub.Client();
        c->Connect("x", 0); c->Poll();
        server.reset();
        Check(kinds(c->Poll()) == "D", "and so is a server that does");
    }

    Section("co-op M0: the door, the seats and the chat line");
    {
        using namespace net;
        Server::Config config;
        config.world_name = "Hollowmarch";
        config.data_hash = 0xAAAA; config.maps_hash = 0xBBBB;
        const auto greeting = [&](const string& name, const string& look = "player_hero") {
            Hello h; h.data_hash = config.data_hash; h.maps_hash = config.maps_hash; h.name = name; h.look = look;
            return h;
        };
        const float dt = 1.0f / 60.0f;

        LoopbackHub local, tailnet;          // the host's own way in, and everyone else's
        Server server(config);
        server.Attach(local.Server(), true);
        server.Attach(tailnet.Server());
        vector<Client*> everyone;
        const auto tick = [&](int frames = 4) {
            for (int f = 0; f < frames; ++f) {
                server.Update(dt);
                for (Client* c : everyone) c->Update(dt);
            }
        };
        const auto same_roster = [](const vector<SeatInfo>& l, const vector<SeatInfo>& r) {
            if (l.size() != r.size()) return false;
            for (size_t i = 0; i < l.size(); ++i)
                if (l[i].seat != r[i].seat || l[i].name != r[i].name || l[i].look != r[i].look || l[i].host != r[i].host) return false;
            return true;
        };

        Client dada, oona;
        everyone = {&dada, &oona};
        Check(dada.Start(local.Client(), "loopback", 0, greeting("Dada")) && dada.Busy(), "the host's own client knocks like anyone else");
        tick();
        Check(dada.Seated() && dada.Seat() == 0 && dada.WorldName() == "Hollowmarch", "and is given the first seat");
        Check(server.Roster().size() == 1 && server.Roster()[0].host && !server.ReachedFromOutside(),
              "marked as the host; nobody has reached the door from outside yet");

        Check(oona.Start(tailnet.Client(), "dada-pc", 7777, greeting("  Oona\n", "player_warden")), "a friend dials");
        tick();
        Check(oona.Seated() && oona.Seat() == 1 && server.ReachedFromOutside(), "and is seated beside them; the door has been reached");
        Check(server.Roster().size() == 2 && server.Roster()[1].name == "Oona" && server.Roster()[1].look == "player_warden" &&
              !server.Roster()[1].host, "the server knows them by a cleaned name and their look");
        Check(same_roster(dada.Roster(), server.Roster()) && same_roster(oona.Roster(), server.Roster()),
              "and both screens say who is connected, the same as the server does");
        {
            const auto lines = dada.TakeNewLines();
            Check(lines.size() == 2 && lines[0].seat == SERVER_SEAT && lines[0].text == "Dada joined." &&
                  lines[1].text == "Oona joined." && dada.TakeNewLines().empty(),
                  "the host is told who came in, once");
            const auto theirs = oona.TakeNewLines();
            Check(theirs.size() == 1 && theirs[0].text == "Oona joined.", "a newcomer hears of their own arrival, not of earlier ones");
        }

        // The M0 gate: a typed line appears on the other screen.
        oona.Say("  to the mine?\t");
        tick();
        {
            const auto at_host = dada.TakeNewLines(), at_oona = oona.TakeNewLines();
            Check(at_host.size() == 1 && at_host[0].seat == 1 && at_host[0].name == "Oona" && at_host[0].text == "to the mine?",
                  "a line typed by the friend appears on the host's screen, with her name");
            Check(at_oona.size() == 1 && at_oona[0].text == "to the mine?" && at_oona[0].name == "Oona",
                  "and comes back to her own as the server saw it");
        }
        dada.Say("aye");
        oona.Say(" \n ");
        oona.Say(string(400, 'x'));
        tick();
        {
            const auto at_oona = oona.TakeNewLines();
            Check(at_oona.size() == 2 && at_oona[0].name == "Dada" && at_oona[0].text == "aye" &&
                  at_oona[1].text.size() == MAX_CHAT, "and the other way; an empty line is not sent and a long one is cut");
            Check(dada.Log().size() == 5 && dada.Log().back().text.size() == MAX_CHAT, "the log keeps what was said");
            dada.TakeNewLines();
        }

        // --- the door ----------------------------------------------------------------
        const auto knock = [&](Hello h, RefuseReason want, const char* must_say, const char* what) {
            Client c;
            everyone.push_back(&c);
            c.Start(tailnet.Client(), "dada-pc", 7777, h);
            tick(6);
            everyone.pop_back();
            Check(c.Where() == Client::State::Refused && c.WhyRefused() == want &&
                  c.Reason().find(must_say) != string::npos && c.Roster().empty(),
                  string(what) + " (" + RefuseReasonName(c.WhyRefused()) + ": " + c.Reason() + ")");
        };
        { Hello h = greeting("Old"); h.version = PROTOCOL_VERSION + 1;
          knock(h, RefuseReason::Version, "protocol", "another protocol version is refused, with both numbers"); }
        { Hello h = greeting("Modder"); h.data_hash ^= 1;
          knock(h, RefuseReason::Data, "data/", "a different data/ is refused by name"); }
        { Hello h = greeting("Mapper"); h.maps_hash ^= 1;
          knock(h, RefuseReason::Maps, "maps/", "a different maps/ is refused by name"); }
        { Hello h = greeting("Stranger"); h.magic = 0x12345678;
          knock(h, RefuseReason::NotDreamQuest, "DreamQuest", "something that is not DreamQuest is refused"); }
        Check(server.Roster().size() == 2 && same_roster(dada.Roster(), server.Roster()) && dada.TakeNewLines().empty(),
              "none of which was seated, announced, or seen by anyone inside");

        // Someone who talks before saying hello, and someone who never talks.
        {
            auto rude = tailnet.Client();
            rude->Connect("dada-pc", 7777);
            tick(2);
            rude->Send(LOOPBACK_SERVER, Channel::Reliable, Encode(Say{"let me in"}));
            tick(3);
            bool refused = false, dropped = false;
            for (const Packet& p : rude->Poll()) {
                Refuse r;
                if (p.type == Packet::Type::Data && Decode(p.data, r)) refused = r.reason == RefuseReason::Malformed;
                if (p.type == Packet::Type::Disconnected) dropped = refused;      // in that order
            }
            Check(refused && dropped, "speaking before the greeting is refused and then dropped");

            auto silent = tailnet.Client();
            silent->Connect("dada-pc", 7777);
            tick(2);
            silent->Poll();
            for (int f = 0; f < static_cast<int>(Server::HELLO_TIMEOUT * 60.0f) - 30; ++f) server.Update(dt);
            bool early = false;
            for (const Packet& p : silent->Poll()) early |= p.type == Packet::Type::Disconnected;
            for (int f = 0; f < 60; ++f) server.Update(dt);
            bool late = false;
            for (const Packet& p : silent->Poll()) late |= p.type == Packet::Type::Disconnected;
            Check(!early && late, "a connection that never says hello is dropped after five seconds, not before");
            dada.TakeNewLines(); oona.TakeNewLines();
        }

        // --- seats ----------------------------------------------------------------------
        Client sam, sam2, fifth;
        everyone = {&dada, &oona, &sam, &sam2};
        sam.Start(tailnet.Client(), "dada-pc", 7777, greeting("Sam", "player_wayfarer"));
        tick();
        sam2.Start(tailnet.Client(), "dada-pc", 7777, greeting("Sam"));
        tick();
        Check(sam.Seated() && sam.Seat() == 2 && sam2.Seated() && sam2.Seat() == 3, "four friends, four seats");
        Check(server.Roster().size() == 4 && server.Roster()[2].name == "Sam" && server.Roster()[3].name == "Sam 2",
              "two friends called Sam are Sam and Sam 2");
        Check(same_roster(sam2.Roster(), server.Roster()) && same_roster(dada.Roster(), server.Roster()),
              "and all four screens agree");
        everyone.push_back(&fifth);
        fifth.Start(tailnet.Client(), "dada-pc", 7777, greeting("Late"));
        tick(6);
        Check(fifth.Where() == Client::State::Refused && fifth.WhyRefused() == RefuseReason::Full &&
              fifth.Reason().find("4 seats") != string::npos, "a fifth is told the world is full");

        dada.TakeNewLines();
        oona.Leave();
        tick();
        Check(!oona.Seated() && oona.Where() == Client::State::Idle && server.Roster().size() == 3 &&
              same_roster(dada.Roster(), server.Roster()), "a friend who leaves is gone from every roster");
        {
            const auto lines = dada.TakeNewLines();
            Check(lines.size() == 1 && lines[0].seat == SERVER_SEAT && lines[0].text == "Oona left.", "and the others are told");
        }
        fifth.Start(tailnet.Client(), "dada-pc", 7777, greeting("Late"));
        tick();
        Check(fifth.Seated() && fifth.Seat() == 1, "the seat they left is the next one given");

        // A slow, lossy line changes when things arrive, not what arrives.
        tailnet.SetDelay(5);
        tailnet.DropUnreliable(2);
        sam.Say("still here");
        tick(20);
        bool heard = false;
        for (const auto& line : fifth.TakeNewLines()) heard |= line.name == "Sam" && line.text == "still here";
        Check(heard, "chat survives a delayed, lossy line: it is sent reliably");
        tailnet.SetDelay(0);
        tailnet.DropUnreliable(0);

        // The host goes. Everyone is told, and nobody is left waiting.
        everyone = {&dada, &sam, &sam2, &fifth};
        server.Shutdown();
        for (int f = 0; f < 4; ++f) for (Client* c : everyone) c->Update(dt);
        Check(sam.Where() == Client::State::Lost && sam2.Where() == Client::State::Lost && !sam.Reason().empty() &&
              sam.Roster().empty(), "when the host stops, every friend is told the line was lost");

        // Nobody home at all.
        {
            LoopbackHub empty;
            Client c;
            Check(c.Start(empty.Client(), "nobody", 7777, greeting("Hopeful")), "dialling an empty hub is an attempt");
            c.Update(dt); c.Update(dt);
            Check(c.Where() == Client::State::Lost && !c.Reason().empty(), "that ends in 'nobody answered'");
            // And a line that connects but is never answered gives up on its own.
            LoopbackHub mute;
            auto deaf = mute.Server();          // listening, but nothing reads it
            Client waiting;
            waiting.Start(mute.Client(), "mute", 7777, greeting("Patient"));
            for (int f = 0; f < static_cast<int>(Client::CONNECT_TIMEOUT * 60.0f) - 30; ++f) waiting.Update(dt);
            const bool still = waiting.Busy();
            for (int f = 0; f < 60; ++f) waiting.Update(dt);
            Check(still && waiting.Where() == Client::State::Lost, "a door that never answers is given up on after eight seconds");
        }
    }

    Section("co-op M0: a session over real UDP, on this machine");
    {
        using namespace net;
        const uint16_t port = 47813;          // not 7777: a game may be hosting while this runs
        const auto spin = [&](vector<Session*> all, const std::function<bool()>& done, int ms = 3000) {
            for (int i = 0; i < ms; ++i) {
                for (Session* s : all) s->Update(0.001f);
                if (done()) return true;
                SDL_Delay(1);
            }
            return done();
        };
        Session host, guest;
        string error;
        const DataHashes& hashes = host.Hashes(".");
        Check(hashes.data_files > 0 && hashes.map_files > 0, "a session hashes the game's own data");
        const bool hosting = host.Host(port, {"Dada", "player_hero"}, "Hollowmarch", error, "127.0.0.1");
        Check(hosting && host.Hosting() && host.Port() == port, "hosting listens on the port" + (error.empty() ? string() : " (" + error + ")"));
        if (hosting) {
            Check(spin({&host}, [&] { return host.Me().Seated(); }, 200) && host.Me().Seat() == 0 &&
                  host.Hosted() && host.Hosted()->Roster().size() == 1 && host.Hosted()->Roster()[0].host,
                  "and the host is seated in their own world, over the loopback");
            {
                Session second;
                string why;
                Check(!second.Host(port, {"Twin", "player_hero"}, "Other", why, "127.0.0.1") && !why.empty() && !second.Active(),
                      "a second host on the same port is refused, and says why (" + why + ")");
            }
            Check(guest.Join("127.0.0.1:" + std::to_string(port), {"Oona", "player_warden"}, error) && guest.Me().Busy(),
                  "a guest dials it");
            Check(spin({&host, &guest}, [&] { return guest.Me().Seated(); }) && guest.Me().Seat() == 1 &&
                  guest.As() == Session::Role::Guest, "and is seated, over UDP");
            Check(spin({&host, &guest}, [&] { return host.Me().Roster().size() == 2; }) &&
                  host.Me().Roster()[1].name == "Oona" && guest.Me().Roster().size() == 2 && guest.Me().Roster()[0].host &&
                  host.Hosted()->ReachedFromOutside(), "both screens say who is connected, and the host's light is on");
            host.Me().TakeNewLines(); guest.Me().TakeNewLines();
            guest.Me().Say("can you hear me?");
            Check(spin({&host, &guest}, [&] { return !host.Me().Log().empty() && host.Me().Log().back().text == "can you hear me?"; }) &&
                  host.Me().Log().back().name == "Oona", "a typed line crosses the wire to the host");
            host.Me().Say("loud and clear");
            Check(spin({&host, &guest}, [&] { return !guest.Me().Log().empty() && guest.Me().Log().back().text == "loud and clear"; }) &&
                  guest.Me().Log().back().name == "Dada", "and one comes back");

            // A guest with another build's data is turned away over the real wire too.
            {
                // Reached the only way a test can: a client of our own making.
                Client c;
                Hello h; h.data_hash = hashes.data ^ 1; h.maps_hash = hashes.maps; h.name = "Modder";
                string why;
                auto wire = DialEnet(why);
                Check(wire != nullptr, "a second socket opens");
                if (wire) {
                    c.Start(std::move(wire), "127.0.0.1", port, h);
                    for (int i = 0; i < 3000 && c.Busy(); ++i) { host.Update(0.001f); c.Update(0.001f); SDL_Delay(1); }
                    Check(c.Where() == Client::State::Refused && c.WhyRefused() == RefuseReason::Data,
                          "different data is refused over UDP, with the reason delivered before the line drops");
                }
            }

            guest.Leave();
            Check(!guest.Active() && spin({&host}, [&] { return host.Me().Roster().size() == 1; }),
                  "a guest who leaves is off the host's roster at once, not after a timeout");
            host.Leave();
            Check(!host.Active() && host.Hosted() == nullptr && !host.Me().Seated(), "and a host who stops is offline");
            Session again;
            Check(again.Host(port, {"Dada", "player_hero"}, "Hollowmarch", error, "127.0.0.1"), "the port is free to host on again");
        }
        {
            Session nowhere;
            string why;
            Check(!nowhere.Join("pc:notaport", {"Oona", "player_warden"}, why) && !why.empty() && !nowhere.Active(),
                  "an address that is not one is refused before anything is dialled");
        }
        const vector<LocalAddress> mine = LocalAddresses();
        bool sorted = true, no_loopback = true;
        for (size_t i = 0; i < mine.size(); ++i) {
            no_loopback &= mine[i].ip.rfind("127.", 0) != 0;
            if (i > 0) sorted &= mine[i - 1].tailnet || !mine[i].tailnet;
        }
        Check(sorted && no_loopback, "this machine's addresses list the tailnet's first and never loopback (" +
              std::to_string(mine.size()) + " found)");
    }


    // =========================================================================
    //  Co-op, milestone 1: two bodies
    // =========================================================================
    Section("co-op M1: hands, not the keyboard");
    {
        Input input;
        std::mt19937 rng(7);
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &quests;     ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &input;       ctx.rng = &rng;
        const float dt = 1.0f / 60.0f;
        const auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            input.HandleEvent(e);
        };

        input.Update(dt);
        key(SDLK_D, true); key(SDLK_J, true); key(SDLK_SPACE, true); key(SDLK_SPACE, false);
        const PlayerInput hands = PlayerInput::FromDevice(input);
        Check(hands.move.x > 0.9f && fabsf(hands.move.y) < 0.01f && hands.Down(PlayerInput::Light) &&
              hands.Pressed(PlayerInput::Light) && !hands.Released(PlayerInput::Light),
              "a player's hands are read off the device: the axis, what is held, what was just pressed");
        Check(hands.Pressed(PlayerInput::Jump) && hands.Released(PlayerInput::Jump) && !hands.Down(PlayerInput::Jump),
              "a tap shorter than a frame is pressed and released with nothing held, and is not lost");
        key(SDLK_D, false); key(SDLK_J, false);
        input.Update(dt);

        PlayerInput diagonal; diagonal.move = {0.70710678f, -0.70710678f};
        const PlayerInput q = diagonal.Quantised();
        Check(fabsf(q.move.x - diagonal.move.x) < 0.005f && q.move.x == PlayerInput::FromWire(PlayerInput::ToWire(q.move.x)) &&
              q.Quantised().move.x == q.move.x, "quantising an axis is within half a percent, and quantising twice changes nothing");
        const float qdt = coop::QuantiseDt(1.0f / 72.0f);
        const net::InputStep step = coop::ToStep(q, qdt);
        Check(coop::StepSeconds(step) == qdt && coop::ToHands(step).move.x == q.move.x && coop::ToHands(step).move.y == q.move.y,
              "a step crosses the wire as exactly the numbers it was taken with");
        Check(coop::QuantiseDt(0.2f) == 0.05f && coop::QuantiseDt(-1.0f) == 0.0f, "and no step is longer than the frame clamp");

        // The foundation of prediction: the same hands and the same clock give
        // the same walk, whether the character is the seat at this machine or
        // a guest stepped by the host. Two worlds, one walk.
        World mine, theirs;
        mine.player.Init(ctx, "player_warden");
        theirs.player.Init(ctx, "player_hero");
        Check(mine.LoadMap("town_havenbrook", "", ctx) && theirs.LoadMap("town_havenbrook", "", ctx), "Havenbrook loads twice");
        mine.player.hands_external = true;
        Player* guest = theirs.AddGuest(1, "Oona", "player_warden", ctx);
        Check(guest && !guest->local && guest->seat == 1 && theirs.guests.size() == 1 && theirs.Guest(1) == guest &&
              guest->x == theirs.player.x, "a guest joins the world beside the host");
        const float host_x = theirs.player.x, host_y = theirs.player.y;
        bool same = true;
        float worst = 0.0f;
        std::mt19937 script(99);
        PlayerInput h;
        for (int f = 0; f < 600; ++f) {
            // A wandering, swinging, jumping, sprinting script at an uneven frame rate.
            if (f % 23 == 0) {
                const float a = (script() % 628) / 100.0f;
                h.move = (script() % 5 == 0) ? Vec2{0.0f, 0.0f} : Vec2{cosf(a), sinf(a)};
                h.down = (script() % 3 == 0) ? PlayerInput::Sprint : 0;
            }
            h.pressed = h.released = 0;
            if (f % 41 == 7)  h.pressed |= PlayerInput::Light;
            if (f % 97 == 50) h.pressed |= PlayerInput::Jump;
            if (f % 131 == 60) { h.pressed |= PlayerInput::Strong; h.released |= PlayerInput::Strong; }
            const PlayerInput sent = h.Quantised();
            const float step_dt = coop::QuantiseDt(1.0f / (55.0f + static_cast<float>(script() % 40)));
            mine.player.hands = sent;
            mine.Update(step_dt, ctx);
            theirs.StepGuest(*guest, sent, step_dt, ctx);
            const float d = Length(mine.player.x - guest->x, mine.player.y - guest->y);
            worst = std::max(worst, d);
            same &= d == 0.0f && mine.player.facing == guest->facing && mine.player.Clip() == guest->Clip() &&
                    mine.player.ClipFrame() == guest->ClipFrame();
        }
        Check(Length(mine.player.x - host_x, mine.player.y - host_y) > 40.0f, "the walk went somewhere");
        Check(same, "six hundred uneven steps, and the guest's walk is the local one to the last bit: place, facing, clip and frame (worst " +
              std::to_string(worst) + " px)");
        Check(theirs.player.x == host_x && theirs.player.y == host_y, "and the host, whose hands were empty, has not moved");
        Check(&theirs.NearestPlayer(guest->x, guest->y) == guest && &theirs.NearestPlayer(host_x, host_y) == &theirs.player &&
              theirs.Players().size() == 2 && &mine.NearestPlayer(0, 0) == &mine.player,
              "the world can say who is nearest a point, and alone that is always the player");

        // Everyone goes through the door together, until M4.
        Check(theirs.LoadMap("house_inn", "default", ctx) && theirs.guests.size() == 1 &&
              guest->x == theirs.player.x && guest->y == theirs.player.y, "a guest is kept across a map change and arrives where the host does");
        theirs.RemoveGuest(1);
        Check(theirs.guests.empty() && theirs.Guest(1) == nullptr, "and leaves when told");

        World window;
        window.visiting = true;
        window.player.Init(ctx, "player_hero");
        World full;
        full.player.Init(ctx, "player_hero");
        bool unseen = window.LoadMap("overworld", "start", ctx) && full.LoadMap("overworld", "start", ctx);
        unseen = unseen && window.enemies.size() == full.enemies.size() && !full.enemies.empty();
        for (const auto& e : window.enemies) unseen = unseen && e->puppet && e->CorpseGone() && !Targeting::Targetable(*e);
        Check(unseen, "a world a guest looks through has every monster the map has, as puppets nobody has spoken of yet: out of sight");
        {
            const float ex = window.enemies.front()->x;
            for (int f = 0; f < 120; ++f) window.Update(dt, ctx);
            Check(window.enemies.front()->x == ex && window.enemies.front()->CorpseGone(), "and they think nothing and go nowhere on their own");
        }
        Check(window.LoadMap("town_havenbrook", "", ctx) && !window.npcs.empty() && window.CurrentMap().Loaded(),
              "but it has the map, and the people who live on it");
        window.clock.Set(1, 22.0f);
        Check(!window.RequestTransition("overworld", "start") && !window.TransitionPending() && window.MapId() == "town_havenbrook",
              "a guest's window does not go through doors on its own: the host leads, until M4");
        window.visitor_acts.clear();
        Check(window.Sleep(World::SleepChoice::Reverie, ctx) && window.visitor_acts.size() == 1 && window.visitor_acts[0].kind == 5 &&
              window.visitor_acts[0].n == 1 && !window.TransitionPending() && window.clock.IsNight(),
              "and a bed chosen in it is written down for the host, not slept in");
        window.TryInteract(ctx);
        window.DropItem("logs", 3, 0, 0, ctx, true);
        Check(window.visitor_acts.size() == 3 && window.visitor_acts[1].kind == 1 && window.visitor_acts[2].kind == 2 &&
              window.visitor_acts[2].a == "logs" && window.visitor_acts[2].n == 3 && window.pickups.empty(),
              "as is E, and a thing dropped: the window decides nothing");
        Check(full.RequestTransition("town_havenbrook", "") && full.TransitionPending(), "a world of one's own still does");
    }

    Section("co-op M1: messages");
    {
        using namespace net;
        const auto survives = [&](const char* name, const Bytes& bytes, const std::function<bool(const Bytes&)>& decode) {
            Check(decode(bytes), string(name) + " round-trips");
            bool any_short = false;
            for (size_t n = 0; n < bytes.size(); ++n)
                any_short |= decode(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)));
            Bytes padded = bytes; padded.push_back(0);
            Check(!any_short && !decode(padded), string(name) + ": no truncation of it decodes, nor one with a byte left over");
        };
        InputFrames frames; frames.first_seq = 1000;
        frames.steps = {{13889, 127, -90, 9, 1, 0}, {16667, 0, 0, 0, 0, 1}};
        survives("InputFrames", Encode(frames), [&](const Bytes& x) {
            InputFrames o; return Decode(x, o) && o.first_seq == 1000 && o.steps.size() == 2 && o.steps[0].dt_us == 13889 &&
                                  o.steps[0].move_x == 127 && o.steps[0].move_y == -90 && o.steps[0].down == 9 &&
                                  o.steps[0].pressed == 1 && o.steps[1].released == 1; });
        Check(Encode(frames).size() == 1 + 4 + 2 + 1 + 2 + 1 + 2 * 7, "a step is seven bytes on the wire");
        { InputFrames slow; slow.steps = {{65000, 0, 0, 0, 0, 0}}; InputFrames o;
          Check(Decode(Encode(slow), o) && o.steps[0].dt_us == MAX_STEP_US, "a step longer than the frame clamp is clamped on arrival"); }
        Snapshot snap; snap.time_ms = 123456; snap.ack_seq = 77; snap.day = 3; snap.hours = 21.25f;
        PlayerState ps; ps.seat = 1; ps.x = 1234.5678f; ps.y = -0.125f; ps.lift = 6.5f; ps.facing = 2; ps.flags = PlayerState::Jumping;
        ps.frame = 5; ps.hp = 31; ps.max_hp = 40; ps.clip = "attack_light_2";
        snap.players = {ps, ps};
        survives("Snapshot", Encode(snap), [&](const Bytes& x) {
            Snapshot o; return Decode(x, o) && o.time_ms == 123456 && o.ack_seq == 77 && o.day == 3 && o.hours == 21.25f &&
                               o.players.size() == 2 && o.players[1].x == 1234.5678f && o.players[1].y == -0.125f &&
                               o.players[1].lift == 6.5f && o.players[1].clip == "attack_light_2" && o.players[1].frame == 5 &&
                               o.players[1].hp == 31 && o.players[1].flags == PlayerState::Jumping; });
        Enter enter; enter.map = "town_havenbrook"; enter.x = 640.25f; enter.y = 512.0f; enter.day = 9; enter.hours = 6.5f;
        survives("Enter", Encode(enter), [&](const Bytes& x) {
            Enter o; return Decode(x, o) && o.map == "town_havenbrook" && o.x == 640.25f && o.y == 512.0f && o.day == 9 && o.hours == 6.5f; });
        Outfit outfit; outfit.seat = 2; outfit.look = "player_wayfarer"; outfit.worn = {"wood_staff", "", "wood_body"};
        survives("Outfit", Encode(outfit), [&](const Bytes& x) {
            Outfit o; return Decode(x, o) && o.seat == 2 && o.look == "player_wayfarer" && o.worn.size() == 3 &&
                             o.worn[0] == "wood_staff" && o.worn[1].empty() && o.worn[2] == "wood_body"; });
        Check(IsGameMessage(PeekType(Encode(snap))) && !IsGameMessage(PeekType(Encode(Say{"hi"}))),
              "the game's messages are told from the door's by their number");
    }

    Section("co-op M1: a host, a guest, and the line between them");
    {
        using namespace net;
        Input hin, gin;
        std::mt19937 hrng(1), grng(2);
        QuestLog guest_quests;
        GameContext hctx;
        hctx.sprites = &sprites;   hctx.items = &items;       hctx.loot = &loot;
        hctx.quests = &quests;     hctx.dialogue = &dialogue; hctx.enemies = &enemy_db;
        hctx.projectiles = &projectiles; hctx.spells = &spells;
        hctx.input = &hin;         hctx.rng = &hrng;
        GameContext gctx = hctx;
        gctx.quests = &guest_quests; gctx.input = &gin; gctx.rng = &grng;
        const auto press = [](Input& in, SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            in.HandleEvent(e);
        };

        Server::Config config;
        config.world_name = "Dada's Hollowmarch"; config.data_hash = 1; config.maps_hash = 2;
        LoopbackHub local, wire;
        Server server(config);
        server.Attach(local.Server(), true);
        server.Attach(wire.Server());
        Client dada, oona;
        Hello hd; hd.data_hash = 1; hd.maps_hash = 2; hd.name = "Dada"; hd.look = "player_hero";
        Hello ho = hd; ho.name = "Oona"; ho.look = "player_warden";

        World hw, gw;
        gw.visiting = true;
        hw.player.Init(hctx, "player_hero");
        for (const string& id : Player::StartingKit("player_hero"))
            if (const ItemDef* d = items.Get(id)) hw.player.equipment.Equip(d->slot, id);
        Check(hw.LoadMap("town_havenbrook", "", hctx), "the host is in Havenbrook");
        coop::Host host;
        host.kept_dir = "bin/selftest_net/kept";
        coop::Guest guest;
        bool in_world = true, guest_in = false;
        int enters = 0;
        // Keys held on each side, applied every frame after the input's own update.
        std::set<SDL_Keycode> hkeys, gkeys, gtaps;
        const float hdt = 1.0f / 60.0f;           // the host's display
        const float gdt = coop::QuantiseDt(1.0f / 72.0f);   // the guest's
        const auto frame = [&] {
            // --- the host's machine
            hin.Update(hdt);
            for (SDL_Keycode k : {SDLK_W, SDLK_A, SDLK_S, SDLK_D}) press(hin, k, hkeys.count(k) > 0);
            if (in_world) hw.Update(hdt, hctx);
            server.Update(hdt);
            dada.Update(hdt);
            host.Update(hdt, server, hw, hctx, in_world);
            // --- the guest's
            oona.Update(gdt);
            guest.Update(gdt, oona, gw, gctx);
            if (guest.HasEnter()) {
                const Enter e = guest.PendingEnter();
                ++enters;
                if (e.map.empty()) { guest_in = false; guest.Reset(gw); }
                else {
                    if (!guest_in) {
                        gw.player = Player();
                        gw.player.Init(gctx, "player_warden");
                        for (const string& id : Player::StartingKit("player_warden"))
                            if (const ItemDef* d = items.Get(id)) gw.player.equipment.Equip(d->slot, id);
                    }
                    guest.SetTheDay(gw);
                    guest_in = gw.LoadMap(e.map, "", gctx);
                    gw.player.x = e.x; gw.player.y = e.y;
                    gw.clock.Set(e.day, e.hours);
                    guest.Arrived(gw);
                }
            }
            if (guest_in) {
                gin.Update(gdt);
                for (SDL_Keycode k : {SDLK_W, SDLK_A, SDLK_S, SDLK_D, SDLK_LSHIFT}) press(gin, k, gkeys.count(k) > 0);
                for (SDL_Keycode k : gtaps) { press(gin, k, true); press(gin, k, false); }
                gtaps.clear();
                guest.BeforeStep(gw, &gin);
                gw.Update(gdt, gctx);
                guest.AfterStep(gw, oona, gdt);
            }
        };
        const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };

        dada.Start(local.Client(), "loopback", 0, hd);
        frames(4);
        Check(dada.Seated() && hw.guests.empty(), "the host hosts, alone in the world");
        hw.clock.Set(4, 15.5f);
        oona.Start(wire.Client(), "dada-pc", 7777, ho);
        frames(8);
        Check(oona.Seated() && guest_in && gw.MapId() == "town_havenbrook" && enters == 1,
              "a friend who is seated is told which map to load, and loads it");
        Check(hw.guests.size() == 1 && hw.Guest(1) && hw.Guest(1)->sprite_id == "player_warden" && hw.Guest(1)->name == "Oona" &&
              !hw.Guest(1)->puppet && !hw.Guest(1)->local, "and the host's world has her character in it, to be stepped");
        // Her world is a window: it builds the map's own monsters -- Havenbrook
        // has a farm in it now, so the town has fifteen -- but every one of them
        // is a puppet, out of sight until the host says where it is. The number
        // is what matters: the nth monster on her screen is the nth on his.
        bool all_puppets = !gw.enemies.empty();
        for (const auto& e : gw.enemies) all_puppets &= e->puppet;
        Check(gw.enemies.size() == hw.enemies.size() && all_puppets &&
              gw.clock.Day() == 4 && fabsf(gw.clock.Hours() - 15.5f) < 0.1f,
              "her world is a window: the map's monsters as puppets, posed by the host, and the host's clock");
        Check(fabsf(gw.player.x - hw.Guest(1)->x) < 0.01f && fabsf(gw.player.y - hw.Guest(1)->y) < 0.01f,
              "she stands where the host's copy of her stands");
        frames(20);
        Check(gw.guests.size() == 1 && gw.Guest(0) && gw.Guest(0)->puppet && gw.Guest(0)->name == "Dada" &&
              gw.Guest(0)->sprite_id == "player_hero" && gw.Guest(0)->equipment.InSlot(SLOT_WEAPON) == "wood_sword",
              "and the host is there on her screen, as a puppet, with the right look and the right sword in hand");
        Check(hw.Guest(1)->equipment.InSlot(SLOT_WEAPON) == "oak_shortbow", "as she is on his, with her bow");

        // --- she walks ---------------------------------------------------------------------
        const float start_x = gw.player.x;
        gkeys = {SDLK_D};
        frames(90);
        gkeys.clear();
        frames(30);
        const Player& copy = *hw.Guest(1);
        Check(gw.player.x > start_x + 60.0f, "the guest walks on her own screen the frame she presses the key");
        Check(host.LastApplied(1) > 100 && guest.Acked() > 100 && guest.Sent() >= guest.Acked(),
              "every step she took was sent, taken by the host, and acknowledged (" + std::to_string(guest.Sent()) + " sent, " +
              std::to_string(guest.Acked()) + " acked)");
        Check(fabsf(copy.x - gw.player.x) < 0.01f && fabsf(copy.y - gw.player.y) < 0.01f && copy.facing == gw.player.facing,
              "and the host's copy of her ended exactly where she did: the prediction was right");
        Check(guest.Corrections() == 0 && guest.LastError() <= coop::RECONCILE_THRESHOLD,
              "so nothing had to be put right (last error " + std::to_string(guest.LastError()) + " px)");

        // --- he walks, and she sees it, a tenth of a second ago ------------------------------
        const float hx0 = hw.player.x;
        hkeys = {SDLK_A};
        frames(60);
        const float mid_lag = gw.Guest(0)->x - hw.player.x;      // he is moving left, so she sees him to the right of where he is
        const bool walking_clip = gw.Guest(0)->Clip() == hw.player.Clip() && gw.Guest(0)->facing == FACE_LEFT;
        hkeys.clear();
        frames(40);
        Check(hw.player.x < hx0 - 40.0f && mid_lag > 1.0f && mid_lag < 40.0f,
              "while the host walks, his puppet trails him by about a tenth of a second (" + std::to_string(mid_lag) + " px)");
        Check(walking_clip, "in the clip he is playing, facing the way he faces");
        Check(fabsf(gw.Guest(0)->x - hw.player.x) < 0.5f && fabsf(gw.Guest(0)->y - hw.player.y) < 0.5f,
              "and once he stops, it comes to rest where he is");

        // --- a swing and a jump cross the wire -------------------------------------------------
        gtaps = {SDLK_J};
        frames(6);
        Check(gw.player.Attacking() && copy.Attacking() && copy.Clip() == gw.player.Clip(), "she swings, and the host's copy of her swings");
        frames(60);
        gtaps = {SDLK_SPACE};
        frames(5);
        Check(gw.player.IsJumping() && copy.IsJumping(), "a jump tapped inside one frame -- pressed and released together -- is still a jump on the host");
        frames(60);

        // --- a bad line ----------------------------------------------------------------------
        // Every third unreliable packet lost, and everything five polls late.
        wire.DropUnreliable(3);
        wire.SetDelay(5);
        const int corrections_before = guest.Corrections();
        const uint32_t applied_before = host.LastApplied(1);
        gkeys = {SDLK_S, SDLK_LSHIFT};
        frames(30);
        gtaps = {SDLK_SPACE};
        frames(90);
        gkeys.clear();
        frames(60);
        Check(host.LastApplied(1) - applied_before >= 170, "on a line that loses a third of its packets, the repeats carry every step across (" +
              std::to_string(host.LastApplied(1) - applied_before) + " of 180 taken)");
        Check(fabsf(copy.x - gw.player.x) < 0.01f && fabsf(copy.y - gw.player.y) < 0.01f && guest.Corrections() == corrections_before,
              "and both ends still agree where she is, with nothing to put right");
        wire.DropUnreliable(0);
        wire.SetDelay(0);
        frames(20);

        // --- being put right --------------------------------------------------------------------
        // The host's copy is shoved: a knock from something her window cannot see.
        hw.Guest(1)->x += 5.0f;
        frames(12);
        Check(guest.Corrections() > corrections_before && fabsf(gw.player.x - copy.x) < 0.01f,
              "a small disagreement is put right at once, to the host's answer");
        hw.Guest(1)->x -= 40.0f;
        frames(3);
        const float half_way = fabsf(gw.player.x - copy.x);
        frames(30);
        Check(half_way > 1.0f && half_way < 39.0f && fabsf(gw.player.x - copy.x) < 0.01f,
              "a larger one is closed over a few snapshots rather than in a jump (" + std::to_string(half_way) + " px left after the first)");

        // --- what she wears, and what she carries --------------------------------------------------
        // Her character is hers, on her machine; the host keeps a copy from the
        // sheet she sends when it changes.
        gw.player.equipment.Unequip(SLOT_WEAPON);
        gw.player.equipment.Equip(SLOT_WEAPON, "wood_staff");
        gw.player.inventory.Add("cooked_meat", 3);
        gw.player.inventory.Add("bronze_axe", 1);
        frames(30);
        Check(hw.Guest(1)->equipment.InSlot(SLOT_WEAPON) == "wood_staff", "changing what she holds changes what the host's copy holds");
        Check(hw.Guest(1)->inventory.Count("cooked_meat") == 3 && hw.Guest(1)->inventory.Count("bronze_axe") == 1 &&
              gw.player.inventory.Count("cooked_meat") == 3, "and what is in her bag is in the copy's, with nothing sent back as a gift");
        hw.player.equipment.Equip(SLOT_WEAPON, "iron_sword");
        frames(8);
        Check(gw.Guest(0)->equipment.InSlot(SLOT_WEAPON) == "iron_sword", "and the host's new sword is in his puppet's hand");
        gw.player.equipment.Equip(SLOT_WEAPON, "oak_shortbow");
        frames(30);

        // --- a thing dropped changes hands ----------------------------------------------------------
        gw.player.inventory.Remove("cooked_meat", 1);
        gw.DropItem("cooked_meat", 1, gw.player.x, gw.player.y, gctx, true);
        frames(12);
        Check(hw.pickups.size() == 1 && hw.pickups[0].item_id == "cooked_meat" && hw.pickups[0].dropped &&
              gw.pickups.size() == 1 && gw.pickups[0].item_id == "cooked_meat", "what she drops lies on the ground in the host's world, and in her window");
        frames(60);
        Check(gw.player.inventory.Count("cooked_meat") == 2 && hw.pickups.size() == 1, "standing on it, she does not scoop it straight back up");
        {
            const int had = hw.player.inventory.Count("cooked_meat");
            const float hx = hw.player.x, hy = hw.player.y;
            hw.player.x = hw.pickups[0].x; hw.player.y = hw.pickups[0].y;
            frames(10);
            Check(hw.player.inventory.Count("cooked_meat") == had + 1 && hw.pickups.empty() && gw.pickups.empty(),
                  "but the host, walking over it, has it: that is how things change hands");
            hw.player.x = hx; hw.player.y = hy;
        }

        // --- E, pressed on someone --------------------------------------------------------------------
        {
            // Whoever is first in the file, which is Corrin, at the foot of the
            // south gate's tower: from the road side of him, since north of him
            // is the tower and a step south of him is the way out.
            const Npc& maren = *hw.npcs.front();
            hw.Guest(1)->x = gw.player.x = maren.x - 22.0f;
            hw.Guest(1)->y = gw.player.y = maren.y;
            frames(4);
            hw.TakeRequests(); gw.TakeRequests();
            Check(gw.player.interact.kind == InteractTarget::Npc, "in her window the prompt finds who she is standing by");
            gw.TryInteract(gctx);
            frames(8);
            bool hers = false, his = false;
            for (const WorldRequest& r : gw.TakeRequests()) hers |= r.type == WorldRequest::Type::Dialogue && r.id == maren.Id();
            for (const WorldRequest& r : hw.TakeRequests()) his |= r.type == WorldRequest::Type::Dialogue;
            Check(hers && !his, "E on someone opens the conversation on her screen, and not on the host's");
        }

        // --- the host goes through a door, and she does not ---------------------------------------------
        const int enters_before = enters;
        Check(hw.LoadMap("house_inn", "default", hctx), "the host goes into the inn");
        frames(10);
        Check(host.Worlds() == 2 && host.WorldOf(1) && host.WorldOf(1) != &hw && host.WorldOf(1)->MapId() == "town_havenbrook" &&
              hw.guests.empty() && enters == enters_before && gw.MapId() == "town_havenbrook",
              "she stays in Havenbrook, in a world of her own, and is not told to go anywhere");
        {
            const uint32_t acked = guest.Acked();
            const float gx = gw.player.x;
            gkeys = {SDLK_A};
            frames(60);
            gkeys.clear();
            frames(20);
            const Player* there = host.WorldOf(1)->Guest(1);
            Check(guest.Acked() > acked + 50 && gw.player.x < gx - 30.0f && there && fabsf(there->x - gw.player.x) < 0.01f,
                  "where she goes on walking, stepped by the host as before");
            frames(70);
            Check(gw.Guest(0) == nullptr, "and the host, who is somewhere else, is no longer in her window");
        }

        // --- she goes through one herself -----------------------------------------------------------------
        {
            World& havenbrook = *host.WorldOf(1);
            const Portal* gate = nullptr;
            for (const Portal& p : havenbrook.CurrentMap().Portals())
                if (!p.requires_interact && p.locked_by.empty() && p.min_combat == 0 && !gate) gate = &p;
            Check(gate != nullptr, "Havenbrook has a way out that is walked through");
            if (gate) {
                const string to = gate->target_map;
                havenbrook.Guest(1)->x = gw.player.x = gate->rect.x + gate->rect.w / 2.0f;
                havenbrook.Guest(1)->y = gw.player.y = gate->rect.y + gate->rect.h / 2.0f;
                frames(20);
                Check(gw.MapId() == to && host.WorldOf(1) && host.WorldOf(1)->MapId() == to && enters == enters_before + 1 &&
                      host.Worlds() == 3, "walking into it takes her through, to a third map, and her window follows");
                frames(30);
                const Player* there = host.WorldOf(1)->Guest(1);
                Check(there && fabsf(there->x - gw.player.x) < 0.01f && fabsf(there->y - gw.player.y) < 0.01f &&
                      gw.RequestTransition("town_havenbrook", "") == false, "she is where the host has her, and her window still opens no doors of its own");

                // The host follows her there, and finds the place as she has it.
                const float gx = there->x, gy = there->y;
                Check(hw.LoadMap(to, "", hctx), "the host comes out to the same map");
                frames(30);
                Check(host.WorldOf(1) == &hw && hw.guests.size() == 1 && fabsf(hw.Guest(1)->x - gx) < 0.01f && fabsf(hw.Guest(1)->y - gy) < 0.01f &&
                      host.Worlds() == 2 && gw.Guest(0) != nullptr,
                      "and they are in one world again, she where she stood, each on the other's screen");
            }
        }

        // --- the host leaves the world, and comes back ------------------------------------------------
        in_world = false;
        frames(10);
        Check(!guest_in && hw.guests.empty() && gw.guests.empty() && oona.Seated() && host.Worlds() == 1,
              "when the host leaves the world she is sent back to the lobby, still seated");
        in_world = true;
        frames(10);
        Check(guest_in && hw.guests.size() == 1, "and brought back in when he returns");

        // --- she drops out, and comes back ------------------------------------------------------------
        const string where_map = host.WorldOf(1)->MapId();
        const float where_x = hw.Guest(1)->x, where_y = hw.Guest(1)->y;
        oona.Leave();
        guest.Reset(gw);
        guest_in = false;
        frames(6);
        Check(hw.guests.size() == 1 && hw.guests[0]->away && hw.guests[0]->IsDead() && !hw.guests[0]->Fallen() &&
              hw.PlayerTouching(hw.guests[0]->BodyBox()) != hw.guests[0].get(),
              "a friend whose line drops stands where they were, out of the fight, for a while");
        Check(fs::exists("bin/selftest_net/kept/Oona.json"), "and the host keeps a copy of her character, and where she stood");
        Check(hw.LoadMap("house_inn", "default", hctx), "the host wanders off meanwhile");
        frames(4);
        oona.Start(wire.Client(), "dada-pc", 7777, ho);
        frames(12);
        {
            World* back = host.WorldOf(1);
            const Player* her = back ? back->Guest(1) : nullptr;
            Check(oona.Seated() && guest_in && back && back->MapId() == where_map && her && !her->away &&
                  fabsf(her->x - where_x) < 0.5f && fabsf(her->y - where_y) < 0.5f && back->guests.size() == 1 && gw.MapId() == where_map,
                  "coming back, she is put where she left off -- not beside the host, who is elsewhere -- and the stand-in is gone");
        }
        oona.Leave();
        guest.Reset(gw);
        guest_in = false;
        frames(4);
        for (int i = 0; i < 32; ++i) host.Update(1.0f, server, hw, hctx, true);
        {
            bool anyone = !hw.guests.empty();
            for (int seat = 0; seat < 4; ++seat) anyone |= host.WorldOf(static_cast<uint8_t>(seat)) != nullptr;
            Check(!anyone, "and if she does not come back, the stand-in is let go when the grace is up");
        }
        for (int i = 0; i < 70; ++i) host.Update(1.0f, server, hw, hctx, true);
        Check(host.Worlds() == 1, "as is a map nobody is on, after a minute");

        Client sam;
        Hello hs = hd; hs.name = "Sam"; hs.look = "player_wayfarer";
        sam.Start(wire.Client(), "dada-pc", 7777, hs);
        for (int i = 0; i < 8; ++i) { frame(); sam.Update(hdt); }
        Check(hw.guests.size() == 1 && hw.Guest(1) && hw.Guest(1)->name == "Sam" && hw.Guest(1)->sprite_id == "player_wayfarer" &&
              hw.Guest(1)->equipment.InSlot(SLOT_WEAPON) == "wood_staff",
              "and whoever takes the seat next is a new character, dressed as one");
    }


    // =========================================================================
    //  Co-op, milestones 2 to 5: one fight, one world, one night
    // =========================================================================
    Section("co-op M2-M5: a fight shared, the world shared, the night shared");
    {
        using namespace net;
        std::error_code ec;
        fs::remove_all("bin/selftest_net", ec);
        Input hin, gin;
        std::mt19937 hrng(11), grng(12);
        QuestLog host_quests, guest_quests;
        host_quests.LoadDefinitions("data/quests.json");
        guest_quests.LoadDefinitions("data/quests.json");
        GameContext hctx;
        hctx.sprites = &sprites;   hctx.items = &items;       hctx.loot = &loot;
        hctx.quests = &host_quests; hctx.dialogue = &dialogue; hctx.enemies = &enemy_db;
        hctx.projectiles = &projectiles; hctx.spells = &spells;
        hctx.input = &hin;         hctx.rng = &hrng;
        GameContext gctx = hctx;
        gctx.quests = &guest_quests; gctx.input = &gin; gctx.rng = &grng;
        const auto press = [](Input& in, SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k;
            in.HandleEvent(e);
        };

        Server::Config config;
        config.world_name = "Dada's Hollowmarch"; config.data_hash = 1; config.maps_hash = 2;
        config.password = "barley";
        LoopbackHub local, wire;
        Server server(config);
        server.Attach(local.Server(), true);
        server.Attach(wire.Server());
        Client dada, oona;
        Hello hd; hd.data_hash = 1; hd.maps_hash = 2; hd.name = "Dada"; hd.look = "player_hero"; hd.password = "barley";
        Hello ho = hd; ho.name = "Oona";

        World hw, gw;
        gw.visiting = true;
        hw.player.Init(hctx, "player_hero");
        for (const string& id : Player::StartingKit("player_hero"))
            if (const ItemDef* d = items.Get(id)) hw.player.equipment.Equip(d->slot, id);
        Check(hw.LoadMap("overworld", "start", hctx), "the host is out on the road");
        coop::Host host;
        host.kept_dir = "bin/selftest_net/kept";
        coop::Guest guest;
        bool guest_in = false;
        int enters = 0, woke = 0;
        vector<string> toasts;
        size_t most_texts = 0;
        std::set<SDL_Keycode> gkeys, gtaps;
        const float hdt = 1.0f / 60.0f;
        const float gdt = coop::QuantiseDt(1.0f / 72.0f);
        const auto frame = [&] {
            hin.Update(hdt);
            hw.Update(hdt, hctx);
            server.Update(hdt);
            dada.Update(hdt);
            host.Update(hdt, server, hw, hctx, true);
            oona.Update(gdt);
            guest.Update(gdt, oona, gw, gctx);
            if (guest.HasEnter()) {
                const Enter e = guest.PendingEnter();
                ++enters;
                woke += e.woke ? 1 : 0;
                if (e.map.empty()) { guest_in = false; guest.Reset(gw); }
                else {
                    if (!guest_in) {
                        gw.player = Player();
                        gw.player.Init(gctx, "player_hero");
                        for (const string& id : Player::StartingKit("player_hero"))
                            if (const ItemDef* d = items.Get(id)) gw.player.equipment.Equip(d->slot, id);
                    }
                    guest.SetTheDay(gw);
                    guest_in = gw.LoadMap(e.map, "", gctx);
                    gw.player.x = e.x; gw.player.y = e.y;
                    guest.Arrived(gw);
                }
            }
            if (guest_in) {
                gin.Update(gdt);
                for (SDL_Keycode k : {SDLK_W, SDLK_A, SDLK_S, SDLK_D}) press(gin, k, gkeys.count(k) > 0);
                for (SDL_Keycode k : gtaps) { press(gin, k, true); press(gin, k, false); }
                gtaps.clear();
                if (gw.player.Fallen()) {}
                guest.BeforeStep(gw, &gin);
                gw.Update(gdt, gctx);
                guest.AfterStep(gw, oona, gdt);
            }
            most_texts = std::max(most_texts, gw.texts.size());
            for (const WorldRequest& r : gw.TakeRequests()) if (r.type == WorldRequest::Type::Toast) toasts.push_back(r.text);
        };
        const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
        const auto her = [&]() -> Player* { World* w = host.WorldOf(1); return w ? w->Guest(1) : nullptr; };

        // --- the door has a word ----------------------------------------------------------------------
        dada.Start(local.Client(), "loopback", 0, hd);
        frames(4);
        {
            Client stranger;
            Hello wrong = ho; wrong.password = "oats";
            stranger.Start(wire.Client(), "dada-pc", 7777, wrong);
            for (int i = 0; i < 8; ++i) { frame(); stranger.Update(hdt); }
            Check(stranger.Where() == Client::State::Refused && stranger.WhyRefused() == RefuseReason::Password && hw.guests.empty(),
                  "a world with a password turns away whoever does not have it (" + stranger.Reason() + ")");
        }
        oona.Start(wire.Client(), "dada-pc", 7777, ho);
        frames(10);
        Check(oona.Seated() && guest_in && her() && gw.MapId() == "overworld", "and lets in whoever does");

        // --- a boar, put between them ----------------------------------------------------------------------
        int bi = -1;
        for (size_t i = 0; i < hw.enemies.size() && bi < 0; ++i)
            if (hw.enemies[i]->TypeId().find("boar") != string::npos) bi = static_cast<int>(i);
        Check(bi >= 0, "the meadow has a boar");
        if (bi >= 0 && her()) {
            // She is strong, so the fight is short; her machine says so in her sheet.
            for (int skill : {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_HITPOINTS}) gw.player.GrantXp(skill, 400000);
            gw.player.TakeLevelUps(); gw.player.TakeXpDrops();
            gw.player.Rest();
            host_quests.Start("q_thin_the_herd");
            guest_quests.Start("q_thin_the_herd");
            frames(30);
            Check(her()->skills.Level(SKILL_ATTACK) == gw.player.skills.Level(SKILL_ATTACK) && her()->skills.Level(SKILL_ATTACK) > 50 &&
                  her()->max_hp == gw.player.max_hp, "the host's copy of her has her levels: its rolls are hers");

            Enemy& boar = *hw.enemies[bi];
            hw.player.x = her()->x - 320.0f;                 // the host stands well off
            boar.x = boar.home_x = her()->x + 34.0f;
            boar.y = boar.home_y = her()->y;
            frames(20);
            const Enemy& seen = *gw.enemies[bi];
            Check(seen.puppet && !seen.CorpseGone() && fabsf(seen.x - boar.x) < 6.0f && fabsf(seen.y - boar.y) < 6.0f && seen.hp == boar.hp,
                  "the host's boar is in her window, where the host has it, with its health");
            Check(boar.target_seat == 1, "and it goes for her, who is nearest, not for the host");
            int others = 0;
            for (const auto& e : gw.enemies) others += (!e->CorpseGone() && Length(e->x - gw.player.x, e->y - gw.player.y) > coop::RELEVANCE_RADIUS + 64.0f) ? 1 : 0;
            Check(others == 0, "nothing beyond the relevance radius is told to her");

            // She fights it. Every blow is rolled by the host, for her.
            const int xp_before = gw.player.skills.Xp(SKILL_STRENGTH) + gw.player.skills.Xp(SKILL_ATTACK) + gw.player.skills.Xp(SKILL_DEFENCE);
            const int hp_full = boar.hp;
            gkeys = {SDLK_D};
            frames(3);
            gkeys.clear();
            bool aimed = false;
            for (int f = 0; f < 900 && boar.CurrentState() != Enemy::State::Dead; ++f) {
                if (f % 22 == 0) gtaps = {SDLK_J};
                frame();
                aimed |= gw.targeting.Current() == gw.enemies[bi].get();
            }
            Check(boar.CurrentState() == Enemy::State::Dead && boar.hp <= 0 && hp_full > 0, "her swings, made on her machine, land on the host's boar and kill it");
            Check(aimed, "her window's targeting found it, and her swings were aimed at it");
            frames(25);        // her window is a tenth of a second behind
            Check(gw.enemies[bi]->CurrentState() == Enemy::State::Dead, "in her window it is dead too");
            Check(most_texts > 0, "the numbers over its head were on her screen");
            const int xp_after = gw.player.skills.Xp(SKILL_STRENGTH) + gw.player.skills.Xp(SKILL_ATTACK) + gw.player.skills.Xp(SKILL_DEFENCE);
            Check(xp_after > xp_before, "the experience is hers, on her own character (" + std::to_string(xp_after - xp_before) + ")");
            Check(guest_quests.Counter("q_thin_the_herd") == 1 && host_quests.Counter("q_thin_the_herd") == 1,
                  "and the kill counts in her journal and in the host's: a fight shared is a kill shared");
            Check(gw.player.hp == her()->hp && gw.player.hp > 0, "how hurt she is is what the host says (" + std::to_string(gw.player.hp) + ")");

            // What it dropped is the host's pickup, and hers for the taking.
            Check(!hw.pickups.empty() && gw.pickups.size() == hw.pickups.size(), "what it dropped lies in the host's world and in her window");
            if (!hw.pickups.empty()) {
                const string what = hw.pickups[0].item_id;
                const int had = gw.player.inventory.Count(what);
                her()->x = gw.player.x = hw.pickups[0].x;
                her()->y = gw.player.y = hw.pickups[0].y;
                frames(40);
                Check(gw.player.inventory.Count(what) > had && her()->inventory.Count(what) == gw.player.inventory.Count(what),
                      "walking over it puts it in her real bag, and the copy's agrees (" + what + ")");
            }

            // Food is eaten on her machine; the host hears of it.
            const int whole = gw.player.hp;
            her()->Damage(4);
            frames(8);
            const int hurt = gw.player.hp;
            Check(hurt == her()->hp && hurt == whole - 4, "a wound the host deals is a wound on her screen");
            gw.player.Heal(3);
            frames(12);
            Check(her()->hp == hurt + 3 && gw.player.hp == hurt + 3, "and what she eats heals the host's copy of her too");

            // A shot of hers flies in both worlds.
            gw.player.equipment.Equip(SLOT_WEAPON, "oak_shortbow");
            frames(30);
            gtaps = {SDLK_J};
            bool flew_there = false, flew_here = false, hers = false;
            for (int f = 0; f < 90; ++f) {
                frame();
                for (const Projectile& p : hw.projectiles) { flew_there = true; hers |= !p.owner_local && p.owner_seat == 1; }
                flew_here |= !gw.projectiles.empty();
            }
            Check(flew_there && hers && flew_here, "an arrow she looses is the host's arrow, marked as hers, and is seen in her window");
        }

        // --- a tree, felled for everyone ---------------------------------------------------------------------
        {
            const MapObject* tree = nullptr;
            int tree_index = -1;
            const auto& objects = hw.CurrentMap().Objects();
            for (size_t i = 0; i < objects.size() && !tree; ++i)
                if (objects[i].skill == "Woodcutting" && objects[i].skill_level <= 1 && !hw.Spent(objects[i]) &&
                    Length(objects[i].x - hw.player.x, objects[i].y - hw.player.y) < 900.0f) { tree = &objects[i]; tree_index = static_cast<int>(i); }
            Check(tree != nullptr && her() != nullptr, "there is a tree near the road");
            if (tree && her()) {
                gw.player.equipment.Equip(SLOT_WEAPON, "wood_sword");
                gw.player.inventory.Add("bronze_axe", 1);
                her()->x = gw.player.x = tree->x;
                her()->y = gw.player.y = tree->y + 18.0f;
                frames(30);
                const int logs = gw.player.inventory.Count("logs");
                const int wc = gw.player.skills.Xp(SKILL_WOODCUTTING);
                Check(gw.player.interact.kind == InteractTarget::Object && gw.player.interact.index == tree_index, "her window's prompt finds the tree");
                gw.TryInteract(gctx);
                bool chopping = false, bar = false;
                for (int f = 0; f < 60 * 14 && gw.player.inventory.Count("logs") == logs; ++f) {
                    frame();
                    chopping |= gw.player.GatherClip() == "chop";
                    bar |= gw.Gathering() && gw.GatherProgress() > 0.0f;
                }
                Check(chopping && bar, "she is seen to chop on her own screen, with the bar filling");
                Check(gw.player.inventory.Count("logs") > logs && gw.player.skills.Xp(SKILL_WOODCUTTING) > wc,
                      "and the logs and the Woodcutting are hers");
                gw.TryInteract(gctx);
                frames(10);
            }
        }

        // --- a chest, opened for everyone ---------------------------------------------------------------------
        {
            const auto& objects = hw.CurrentMap().Objects();
            int chest = -1;
            for (size_t i = 0; i < objects.size() && chest < 0; ++i)
                if (objects[i].type == "chest" && objects[i].needs_quest.empty() && !hw.Flagged(objects[i].id)) chest = static_cast<int>(i);
            Check(chest >= 0 && her() != nullptr, "the overworld has a chest nobody has opened");
            if (chest >= 0 && her()) {
                const MapObject& o = objects[chest];
                her()->x = gw.player.x = o.x;
                her()->y = gw.player.y = o.y + 20.0f;
                frames(30);
                Check(!gw.Flagged(o.id) && gw.player.interact.kind == InteractTarget::Object, "she stands at it, and it is shut on her screen too");
                gw.TryInteract(gctx);
                frames(12);
                Check(hw.Flagged(o.id) && gw.Flagged(o.id), "opened by her, it is open in the host's world and in her window: first come, first served");
            }
        }

        // --- she falls, and gets up in Havenbrook ----------------------------------------------------------------
        if (her()) {
            her()->Damage(99999);
            frames(8);
            Check(gw.player.Fallen() && her()->Fallen(), "if the host's copy of her falls, she falls");
            const int enters_before = enters, woke_before = woke;
            gw.visitor_acts.push_back({net::Action::Respawn, "", "", 0});
            frames(12);
            Check(enters == enters_before + 1 && woke == woke_before + 1 && gw.MapId() == "town_havenbrook" && !gw.player.Fallen() &&
                  gw.player.hp == gw.player.max_hp && her() && !her()->Fallen() && host.WorldOf(1) != &hw && host.Worlds() == 2,
                  "and having read the screen she is got up in Havenbrook, whole, while the host is still out on the road");
        }

        // --- the night --------------------------------------------------------------------------------------------
        if (her()) {
            hw.clock.Set(3, 21.0f);
            hw.enemies.clear();
            frames(4);
            Check(hw.company && hw.Sleep(World::SleepChoice::Through, hctx) && hw.player.resting && !hw.TransitionPending() && hw.clock.IsNight(),
                  "in company, sleeping the night through is lying down: the clock keeps its pace");
            frames(30);
            Check(hw.clock.Day() == 3 && hw.player.resting, "and dawn does not come while a friend is up");
            toasts.clear();
            gw.Sleep(World::SleepChoice::Through, gctx);
            frames(12);
            Check(hw.clock.Day() == 4 && !hw.clock.IsNight() && !hw.player.resting && !her()->resting && !guest.Resting(),
                  "when she lies down too, it is dawn for both at once");
            bool told = false;
            for (const string& t : toasts) told |= t.find("Dawn breaks") != string::npos;
            Check(told && gw.clock.Day() == 4, "and she is told so, on her own clock");

            // One dreams while the other sleeps.
            hw.clock.Set(5, 22.0f);
            frames(4);
            const float bed_x = her()->x, bed_y = her()->y;
            const int enters_before = enters;
            gw.Sleep(World::SleepChoice::Reverie, gctx);
            frames(12);
            Check(gw.MapId() == "dreamworld" && host.WorldOf(1) && host.WorldOf(1)->InDream() && enters == enters_before + 1 && hw.clock.IsNight(),
                  "choosing the Reverie takes her to the dream, which is a map like any other, while the host's evening goes on");
            {
                // Her machine built its own nightmares out of the map file, and is
                // only ever told where they stand: they have to be the same ones.
                const World* hers = host.WorldOf(1);
                bool same = hers && hers->enemies.size() == gw.enemies.size() && !gw.enemies.empty();
                std::set<string> kinds;
                for (size_t k = 0; same && k < gw.enemies.size(); ++k) {
                    same = hers->enemies[k]->TypeId() == gw.enemies[k]->TypeId() && hers->enemies[k]->max_hp == gw.enemies[k]->max_hp;
                    kinds.insert(gw.enemies[k]->TypeId());
                }
                Check(same, "what keeps the dream's platforms tonight is the same on her screen as in the host's world");
                Check(kinds.size() >= 3, "and it is a night's pick of them, not the first of every pool");
            }
            Check(hw.Sleep(World::SleepChoice::Through, hctx) && hw.player.resting, "the host lies down");
            frames(30);
            Check(hw.clock.Day() == 6 && !hw.clock.IsNight() && !hw.player.resting, "and with one abed and one dreaming, the night is over");
            Check(gw.MapId() == "town_havenbrook" && her() && fabsf(her()->x - bed_x) < 1.0f && fabsf(her()->y - bed_y) < 1.0f && woke >= 2,
                  "dawn wakes the dreamer where she lay down");
            // Up, the host's getting up is one press.
            hw.clock.Set(7, 22.0f);
            hw.Sleep(World::SleepChoice::Through, hctx);
            frames(2);
            press(hin, SDLK_D, true);
            frames(2);
            press(hin, SDLK_D, false);
            Check(!hw.player.resting && hw.clock.Day() == 7, "and a sleeper who is bored gets up with one press, the night still to come");
        }

        // --- a character, kept ------------------------------------------------------------------------------------
        {
            coop::Character c;
            c.player = gw.player.ToJson();
            c.quests = guest_quests.ToJson();
            c.flags = {"recipe:nettle_brew", "chest_someone_elses"};
            c.storage = json::object();
            c.playtime = 321.0f;
            const string own = coop::CharacterPath("bin/selftest_net/characters", "Oona", "Dada's Hollowmarch", true);
            const string here = coop::CharacterPath("bin/selftest_net/characters", "Oona", "Dada's Hollowmarch", false);
            Check(own != here && own.find("Oona.json") != string::npos && here.find("Oona@Dada_s_Hollowmarch.json") != string::npos,
                  "a character that travels is one file; one a world keeps is a file for that world");
            Check(coop::SaveCharacter(own, c), "her character is written to her own machine");
            coop::Character back;
            Check(coop::LoadCharacter(own, back) && back.playtime == 321.0f && back.flags.size() == 2, "and read back");
            Player again;
            again.Init(gctx, back.player.value("sprite", string("player_hero")));
            again.FromJson(back.player, gctx);
            QuestLog journal;
            journal.LoadDefinitions("data/quests.json");
            journal.FromJson(back.quests);
            Check(again.skills.Level(SKILL_ATTACK) == gw.player.skills.Level(SKILL_ATTACK) &&
                  again.inventory.Count("logs") == gw.player.inventory.Count("logs") &&
                  again.equipment.InSlot(SLOT_WEAPON) == gw.player.equipment.InSlot(SLOT_WEAPON) &&
                  journal.Counter("q_thin_the_herd") == 1,
                  "with her levels, her bag, what she wears and her journal: she picks up where she left off");
            coop::Character none;
            Check(!coop::LoadCharacter("bin/selftest_net/characters/Nobody.json", none), "someone who has never been out has no file");
            Check(coop::PrivateFlag("recipe:nettle_brew") && coop::PrivateFlag("visited:overworld") && !coop::PrivateFlag("chest_hollowrest"),
                  "what she has learned and seen is hers; what is opened is everyone's");
            const string sheet = coop::Guest::MakeSheet(gw, &guest_quests);
            Check(sheet.find("\"x\"") == string::npos && sheet.find("\"hp\"") == string::npos && sheet.find("q_thin_the_herd") != string::npos,
                  "the sheet she sends leaves out where she stands and how hurt she is, which the host knows better");
        }
        fs::remove_all("bin/selftest_net", ec);
    }


    // =========================================================================
    //  Split screen: two players at one machine
    // =========================================================================
    Section("split screen: two sets of hands, one realm");
    {
        using namespace net;
        const float dt = 1.0f / 60.0f;

        // --- two devices, no cross-talk -------------------------------------------------------------
        {
            Input one, two;
            one.SetDevices(true, -1, true);
            two.SetDevices(false, -1, true);
            const auto key = [](Input& in, SDL_Keycode k, bool down) {
                SDL_Event e{};
                e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                e.key.key = k;
                return in.HandleEvent(e);
            };
            one.Update(dt); two.Update(dt);
            key(one, SDLK_D, true); key(two, SDLK_D, true);
            Check(one.Down(::Action::MoveRight) && !two.Down(::Action::MoveRight) && !two.UsesKeyboard(),
                  "the keyboard is Player One's: Player Two's input does not hear it");
            two.Inject(::Action::LightAttack, true);
            Check(two.Pressed(::Action::LightAttack) && two.Down(::Action::LightAttack) && !one.Down(::Action::LightAttack) &&
                  two.ActiveDevice() == InputMode::Controller && two.PromptFor(::Action::LightAttack) == "(X)",
                  "a press on Player Two's controller is theirs alone, and their prompts are a controller's");
            // A panel written for "the input" is handed Player Two's.
            one.Borrow(&two);
            Check(one.Down(::Action::LightAttack) && !one.Down(::Action::MoveRight) && one.PromptFor(::Action::Interact) == "(A)",
                  "borrowing answers every question from the other player's hands");
            one.Borrow(nullptr);
            Check(one.Down(::Action::MoveRight) && !one.Down(::Action::LightAttack), "and giving them back, from its own again");
            key(one, SDLK_D, false);
            two.Inject(::Action::LightAttack, false);
            SDL_Event pad{};
            pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            pad.gbutton.which = 4242;                   // nobody's controller
            pad.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
            one.HandleEvent(pad); two.HandleEvent(pad);
            Check(!one.Down(::Action::LightAttack) && !two.Down(::Action::LightAttack), "a controller that is neither player's is heard by neither");
        }

        // --- a seat at the door with no line -----------------------------------------------------------
        {
            Server::Config config; config.data_hash = 1; config.maps_hash = 2;
            LoopbackHub wire;
            Server server(config);
            server.Attach(wire.Server());
            const int couch = server.ReserveSeat("Player Two", "player_warden");
            Client oona;
            Hello ho; ho.data_hash = 1; ho.maps_hash = 2; ho.name = "Player Two"; ho.look = "player_hero";
            oona.Start(wire.Client(), "x", 1, ho);
            for (int i = 0; i < 6; ++i) { server.Update(dt); oona.Update(dt); }
            Check(couch == 0 && oona.Seated() && oona.Seat() == 1 && oona.Roster().size() == 2 && oona.Roster()[0].name == "Player Two" &&
                  oona.Roster()[1].name == "Player Two 2" && !oona.Roster()[0].host,
                  "someone on the host's couch has a seat in the roster with no line, and a friend across the wire sees who they are");
            server.ReleaseSeat(static_cast<uint8_t>(couch));
            for (int i = 0; i < 4; ++i) { server.Update(dt); oona.Update(dt); }
            Check(oona.Roster().size() == 1 && server.ReserveSeat("Again", "player_hero") == 0, "and when they get up the seat is free again");
        }

        // --- Player Two in the realm ---------------------------------------------------------------------
        Input hin;
        std::mt19937 rng(21);
        QuestLog one_quests, two_quests;
        one_quests.LoadDefinitions("data/quests.json");
        two_quests.LoadDefinitions("data/quests.json");
        GameContext ctx;
        ctx.sprites = &sprites;   ctx.items = &items;       ctx.loot = &loot;
        ctx.quests = &one_quests; ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
        ctx.projectiles = &projectiles; ctx.spells = &spells;
        ctx.input = &hin;         ctx.rng = &rng;

        Server offline{Server::Config{}};
        World home;
        home.player.Init(ctx, "player_hero");
        Check(home.LoadMap("town_havenbrook", "", ctx), "Player One is in Havenbrook");
        coop::Host realm;
        realm.kept_dir.clear();
        const uint8_t seat = static_cast<uint8_t>(offline.ReserveSeat("Player Two", "player_warden"));
        realm.AddLocal(seat, "Player Two", "player_warden", &two_quests, json());
        PlayerInput hands;
        const auto frame = [&] {
            hin.Update(dt);
            home.Update(dt, ctx);
            realm.FeedLocal(seat, hands);
            realm.Update(dt, offline, home, ctx, true);
        };
        const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
        frames(2);
        Player* two = realm.PlayerOf(seat);
        Check(two && realm.IsLocal(seat) && realm.WorldOf(seat) == &home && two->sprite_id == "player_warden" &&
              two->equipment.InSlot(SLOT_WEAPON) == "oak_shortbow" && two->inventory.Count("cooked_meat") == 3 && !two->local,
              "Player Two arrives beside Player One: a new character, dressed and provisioned as one");
        Check(home.company && home.SeatOf(seat).viewed && home.SeatOf(seat).own_journal == &two_quests,
              "their seat is looked through at this machine, and their journal is a real one");

        if (two) {
            // Their hands move them, and nobody else.
            const float x0 = two->x, px = home.player.x;
            hands.move = {1.0f, 0.0f};
            frames(45);
            hands = PlayerInput{};
            frames(5);
            Check(two->x > x0 + 30.0f && home.player.x == px, "Player Two's hands move Player Two, and not Player One");
            const SDL_FPoint on_screen = home.SeatOf(seat).camera.ToScreen(two->x, two->y);
            Check(on_screen.x > 40.0f && on_screen.x < 600.0f && on_screen.y > 40.0f && on_screen.y < 680.0f,
                  "and their own camera follows them, within their half of the screen");

            // The game serves one seat at a time.
            two->inventory.Add("logs", 4);
            const int one_logs = home.player.inventory.Count("logs");
            home.BeginActing(*two);
            Check(home.Acting() && home.player.sprite_id == "player_warden" && home.player.inventory.Count("logs") == 4 &&
                  home.player.seat == seat, "serving Player Two, `player` is Player Two: their bag, their look");
            home.SetFlag("recipe:nettle_brew");
            home.SetFlag("chest_opened_by_two");
            Check(home.KnowsRecipe("nettle_brew"), "what they learn while served, they know");
            home.EndActing();
            Check(!home.Acting() && home.player.sprite_id == "player_hero" && home.player.inventory.Count("logs") == one_logs &&
                  two->inventory.Count("logs") == 4, "and handing back, everyone is themselves again");
            Check(!home.KnowsRecipe("nettle_brew") && home.SeatOf(seat).private_flags.count("recipe:nettle_brew") == 1 &&
                  home.Flagged("chest_opened_by_two"),
                  "a recipe Player Two learned is theirs and not Player One's; a chest they opened is open for both");

            // Each to their own journal; a fight shared is a kill shared.
            one_quests.Start("q_thin_the_herd");
            two_quests.Start("q_thin_the_herd");
            QuestEvent kill;
            kill.type = ObjectiveType::Kill;
            kill.target = "boar";
            kill.map_id = home.MapId();
            home.CreditKill(kill);
            frames(1);
            Check(one_quests.Counter("q_thin_the_herd") == 1 && two_quests.Counter("q_thin_the_herd") == 1,
                  "a kill counts in both journals, each a real one");

            // Going their separate ways: Player One into the inn, Player Two stays.
            Check(home.LoadMap("house_inn", "default", ctx), "Player One goes into the inn");
            frames(3);
            World* theirs = realm.WorldOf(seat);

            Check(theirs && theirs != &home && theirs->MapId() == "town_havenbrook" && realm.Worlds() == 2 &&
                  realm.PlayerOf(seat) == two && home.guests.empty(),
                  "and Player Two stays in Havenbrook, on a map of their own: the halves show different places");
            const float y0 = two->y;
            hands.move = {0.0f, 1.0f};
            frames(30);
            hands = PlayerInput{};
            Check(two->y > y0 + 15.0f && theirs->SeatOf(seat).viewed, "where they go on walking, with their camera");

            // The night is one night for both.
            home.clock.Set(2, 22.0f);
            frames(2);
            Check(home.Sleep(World::SleepChoice::Through, ctx) && home.player.resting && home.clock.Day() == 2,
                  "Player One lies down, and the clock keeps its pace while Player Two is up");
            net::Action bed;
            bed.kind = net::Action::Sleep;
            bed.n = 0;
            realm.LocalAct(seat, bed, ctx);
            frames(3);
            Check(home.clock.Day() == 3 && !home.clock.IsNight() && !home.player.resting && !two->resting,
                  "when Player Two lies down too, it is dawn for both");

            // Falling, and getting up.
            two->Damage(99999);
            frames(2);
            net::Action up;
            up.kind = net::Action::Respawn;
            realm.LocalAct(seat, up, ctx);
            frames(3);
            two = realm.PlayerOf(seat);
            Check(two && !two->Fallen() && two->hp == two->max_hp && realm.WorldOf(seat) && realm.WorldOf(seat)->MapId() == "town_havenbrook",
                  "Player Two, fallen, is got up in Havenbrook, whole");

            // A kept character comes back as it was.
            const json kept = two->ToJson();
            realm.RemoveLocal(seat);
            offline.ReleaseSeat(seat);
            frames(2);
            Check(realm.PlayerOf(seat) == nullptr && !realm.IsLocal(seat) && offline.Roster().empty(),
                  "Player Two gets up from the couch, and is gone from the world and the roster");
            Check(offline.ReserveSeat("Player Two", "player_warden") == seat, "the seat is theirs to take again");
            realm.AddLocal(seat, "Player Two", "player_hero", &two_quests, kept);
            frames(2);
            two = realm.PlayerOf(seat);
            Check(two && two->sprite_id == "player_warden" && two->inventory.Count("logs") == 4 && realm.WorldOf(seat) == &home,
                  "and sitting down again with their kept character, they are who they were, beside Player One");
        }
    }

    // Optional render smoke test. Uses the real world renderer and SDL image
    // loading without creating a window, reading saves, or opening a game.
    if (argc > 1 && string(argv[1]) == "--render-previews") {
        Section("off-screen woodland rendering");
        const bool initialized = SDL_Init(0);
        Check(initialized, "SDL initializes for off-screen rendering");
        SDL_Surface* surface = initialized ? SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32) : nullptr;
        SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        Check(renderer != nullptr, "software renderer is available");
        if (renderer) {
            {
                TextureCache cache(renderer);
                Input input;
                std::mt19937 rng(42);
                GameContext ctx;
                ctx.renderer = renderer; ctx.textures = &cache; ctx.sprites = &sprites;
                ctx.items = &items; ctx.enemies = &enemy_db; ctx.input = &input; ctx.rng = &rng;
                fs::create_directories("bin/previews");
                struct View { const char* map; const char* name; float x, y, zoom; };
                const View views[] = {
                    {"whisperwood_trail", "whisperwood_bridge", 1232, 848, 2},
                    {"whisperwood_trail", "woodcutter_camp", 1744, 460, 2},
                    {"whisperwood_trail", "forest_fork", 2256, 704, 2},
                    {"mossvale", "mossvale", 976, 650, 1.5f},
                    {"fernhollow", "fernhollow", 880, 600, 1.5f},
                    {"mossvale_lodge_hall", "mossvale_lodge", 352, 250, 1.5f},
                    {"mossvale_herbalist", "oonas_cottage", 256, 235, 1.5f},
                    {"fernhollow_cottage", "ferry_cottage", 256, 235, 1.5f},
                    {"town_havenbrook", "havenbrook_store", 1090, 870, 2},
                    {"town_havenbrook", "havenbrook_sawpit", 300, 300, 1.5f},
                    {"town_havenbrook", "havenbrook_pit", 1460, 240, 1.5f},
                    {"town_havenbrook", "havenbrook_pond", 1450, 1190, 1.5f},
                    {"town_havenbrook", "havenbrook_well", 768, 832, 2},
                    {"overworld", "emberfell_entrance", 2768, 300, 2},
                    {"overworld", "mire_bogs", 1060, 1000, 1.5f},
                    {"overworld", "lizard_camp", 1070, 2540, 1.5f},
                    {"overworld", "barrow_mound", 1048, 1440, 2},
                    {"overworld", "far_west_mire", 420, 2200, 1.5f},
                    {"overworld", "southern_meadow", 2600, 3200, 1.5f},
                    {"overworld", "meadow_decals", 2900, 2300, 2},
                    {"overworld", "hollowrest_gate", 1872, 3230, 1},
                    {"overworld", "hollowrest_crypt", 1872, 3700, 1},
                    {"dungeon_emberfell_1", "mine_stairs_up", 592, 440, 2},
                    {"dungeon_emberfell_1", "mine_stairs_down", 1616, 950, 3},
                    {"dungeon_barrow", "barrow_stairs_up", 720, 640, 2},
                    {"house_inn_cellar", "inn_cellar", 330, 230, 1.5f},
                    {"ice_spire_peak", "peak_camp", 1028, 2480, 1.5f},
                    {"ice_spire_peak", "peak_slopes", 650, 1600, 1.5f},
                    {"ice_spire_peak", "peak_summit", 1190, 300, 1.5f},
                    {"ice_spire_peak", "dragon_ground", 1160, 140, 1.5f},
                    {"ashen_path", "ashen_ford", 976, 860, 1.5f},
                    {"ashen_path", "ashen_gate", 2896, 540, 1.5f},
                    {"dungeon_infernal", "infernal_pit", 592, 1040, 1.5f},
                    {"house_smith", "halda_forge", 288, 200, 1.5f},
                    {"mossvale", "mossvale_pell", 700, 930, 2},
                    {"overworld", "havenbrook_gate", 2672, 2650, 1.5f},
                    {"mossvale", "mossvale_house", 1472, 1216, 2},
                    {"mossvale_cottage", "your_house", 256, 200, 1.5f},
                    {"mossvale", "mossvale_smith", 1540, 840, 2},
                    {"fernhollow", "nell_cart", 560, 640, 2},
                    {"dreamworld", "night_market", 1168, 930, 1.5f},
                };
                for (const View& view : views) {
                    World world;
                    world.player.Init(ctx, "player_hero");
                    const bool loaded = world.LoadMap(view.map, "", ctx);
                    Check(loaded, string(view.map) + " loads for rendering");
                    if (!loaded) continue;
                    world.player.x = view.x; world.player.y = view.y;
                    world.camera.SetViewport(1280, 720);
                    world.camera.SetZoom(view.zoom);
                    world.camera.SnapTo(view.x, view.y);
                    world.ambience.Update(1.0f / 60.0f, world.camera);
                    world.Render(renderer, cache);
                    SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
                    Check(pixels != nullptr, string(view.name) + " produces pixels");
                    if (pixels) {
                        Check(IMG_SavePNG(pixels, (string("bin/previews/") + view.name + ".png").c_str()),
                              string(view.name) + " preview saves");
                        SDL_DestroySurface(pixels);
                    }
                }

                // A Warchief caught at three points of his heavy's wind-up, side
                // by side: the bar over his head filling and the red glow
                // growing, which is the whole warning the player gets.
                {
                    std::mt19937 prng(3);
                    GameContext pctx = ctx;
                    pctx.rng = &prng;
                    EnemyDef chief = *enemy_db.Get("orc3");
                    chief.heavy.opening = 0.0f;
                    const float stops[] = {0.25f, 0.6f, 0.95f};
                    for (int i = 0; i < 3; ++i) {
                        World world;
                        world.player.Init(pctx, "player_hero");
                        if (!world.LoadMap("overworld", "start", pctx)) break;
                        world.enemies.clear();
                        world.clock.Set(1, 12.0f);
                        world.player.y -= 200.0f;
                        {
                            LevelUp up;
                            world.player.skills.AddXp(SKILL_HITPOINTS, XpForLevel(90), up);
                            world.player.Rest();
                        }
                        EnemySpawnDef def;
                        def.type = "orc3"; def.level = 1; def.leash = 400.0f; def.respawn = 0.0f;
                        def.x = world.player.x + 40.0f; def.y = world.player.y;
                        auto e = std::make_unique<Enemy>();
                        e->Init(&chief, def, pctx);
                        Enemy* orc = e.get();
                        world.enemies.push_back(std::move(e));
                        orc->RevealHealthBar();
                        for (int f = 0; f < 400 && orc->HeavyCharge() < stops[i]; ++f)
                            world.Update(1.0f / 60.0f, pctx);
                        world.camera.SetViewport(1280, 720);
                        world.camera.SetZoom(3.0f);
                        world.camera.SnapTo(world.player.x + 20.0f, world.player.y - 20.0f);
                        world.Render(renderer, cache);
                        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
                        if (pixels) {
                            const string name = "bin/previews/heavy_charge_" + std::to_string(i) + ".png";
                            Check(IMG_SavePNG(pixels, name.c_str()), "the heavy wind-up preview saves");
                            SDL_DestroySurface(pixels);
                        }
                    }
                }

                // The enchanting table by Mira's stones, and a felled oak's
                // stump on the overworld, drawn in the world.
                {
                    std::mt19937 prng(5);
                    GameContext pctx = ctx;
                    pctx.rng = &prng;
                    struct View { const char* name; const char* map; const char* type; };
                    for (const View& v : {View{"enchanting_table", "fernhollow", "altar"},
                                          View{"felled_oak", "overworld", "tree"}}) {
                        World world;
                        world.player.Init(pctx, "player_hero");
                        if (!world.LoadMap(v.map, "", pctx)) continue;
                        world.enemies.clear();
                        world.clock.Set(1, 12.0f);
                        const MapObject* at = nullptr;
                        for (const MapObject& o : world.CurrentMap().Objects())
                            if (o.type == v.type && (o.type != "tree" || o.title == "oak")) { at = &o; break; }
                        if (!at) continue;
                        if (at->type == "tree") world.Pick(*at);
                        world.player.x = at->x + 34.0f;
                        world.player.y = at->y + 22.0f;
                        world.Update(1.0f / 60.0f, pctx);
                        world.camera.SetViewport(1280, 720);
                        world.camera.SetZoom(3.0f);
                        world.camera.SnapTo(at->x, at->y - 10.0f);
                        world.Render(renderer, cache);
                        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
                        if (pixels) {
                            const string name = string("bin/previews/") + v.name + ".png";
                            Check(IMG_SavePNG(pixels, name.c_str()), string(v.name) + " preview saves");
                            SDL_DestroySurface(pixels);
                        }
                    }
                }

                // The swing, drawn: a light attack and a Cleave caught on their
                // active frames, the crescent swept through what they cover.
                {
                    std::mt19937 prng(8);
                    Input pin;
                    GameContext pctx = ctx;
                    pctx.rng = &prng;
                    pctx.input = &pin;
                    const auto press = [&](World& w, SDL_Keycode k) {
                        SDL_Event e{};
                        pin.Update(1.0f / 60.0f);
                        e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; pin.HandleEvent(e);
                        w.Update(1.0f / 60.0f, pctx);
                        pin.Update(1.0f / 60.0f);
                        e.type = SDL_EVENT_KEY_UP; pin.HandleEvent(e);
                        w.Update(1.0f / 60.0f, pctx);
                    };
                    for (int which = 0; which < 3; ++which) {
                        World world;
                        world.player.Init(pctx, "player_hero");
                        // The third is the same Cleave on the mine's dark floor.
                        if (!world.LoadMap(which == 2 ? "dungeon_emberfell_1" : "overworld",
                                           which == 2 ? "entrance" : "start", pctx)) break;
                        world.enemies.clear();
                        world.clock.Set(1, 12.0f);
                        if (which == 2) world.player.y += 48.0f; else world.player.y -= 200.0f;
                        world.player.facing = FACE_RIGHT;
                        world.player.sprite.facing = FACE_RIGHT;
                        world.player.equipment.Equip(SLOT_WEAPON, "iron_sword");
                        const auto settle = [&]() {
                            for (int f = 0; f < 120 && !world.player.CanAttack(); ++f) { pin.Update(1.0f / 60.0f); world.Update(1.0f / 60.0f, pctx); }
                        };
                        if (which >= 1) { press(world, SDLK_J); settle(); press(world, SDLK_J); settle(); }
                        press(world, which == 0 ? SDLK_J : SDLK_K);
                        // To the middle of the active frames.
                        const AttackProfile& pr = world.player.Attack().profile;
                        const int to = static_cast<int>((pr.windup + pr.active * 0.8f) * 60.0f);
                        for (int f = 2; f < to; ++f) { pin.Update(1.0f / 60.0f); world.Update(1.0f / 60.0f, pctx); }
                        world.camera.SetViewport(1280, 720);
                        world.camera.SetZoom(4.0f);
                        world.camera.SnapTo(world.player.x + 10.0f, world.player.y - 16.0f);
                        world.Render(renderer, cache);
                        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
                        if (pixels) {
                            const string name = string("bin/previews/") +
                                (which == 0 ? "swing_light" : which == 1 ? "swing_cleave" : "swing_dark") + ".png";
                            Check(IMG_SavePNG(pixels, name.c_str()), "the swing preview saves");
                            SDL_DestroySurface(pixels);
                        }
                    }
                }

                // The college at Fernhollow from its doorstep, and its hall.
                {
                    std::mt19937 prng(6);
                    GameContext pctx = ctx;
                    pctx.rng = &prng;
                    struct Spot { const char* name; const char* map; const char* spawn; float dx, dy; };
                    for (const Spot& v : {Spot{"college_gate", "fernhollow", "from_college_grounds", 0.0f, -70.0f},
                                          Spot{"college_court", "college_grounds", "from_fernhollow_college", 0.0f, 40.0f},
                                          Spot{"college_practice", "college_training", "entrance", -300.0f, -140.0f},
                                          Spot{"college_lecture", "college_classroom", "entrance", 300.0f, -140.0f},
                                          Spot{"college_hall", "fernhollow_college", "entrance", 0.0f, -110.0f},
                                          Spot{"wynns", "mossvale_weavers", "entrance", 0.0f, -110.0f}}) {
                        World world;
                        world.player.Init(pctx, "player_wayfarer");
                        if (!world.LoadMap(v.map, v.spawn, pctx)) continue;
                        world.enemies.clear();
                        world.clock.Set(1, 12.0f);
                        world.Update(1.0f / 60.0f, pctx);
                        world.camera.SetViewport(1280, 720);
                        world.camera.SetZoom(3.0f);
                        world.camera.SnapTo(world.player.x + v.dx, world.player.y + v.dy);
                        world.Render(renderer, cache);
                        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
                        if (pixels) {
                            const string name = string("bin/previews/") + v.name + ".png";
                            Check(IMG_SavePNG(pixels, name.c_str()), string(v.name) + " preview saves");
                            SDL_DestroySurface(pixels);
                        }
                    }
                }
            }
            SDL_DestroyRenderer(renderer);
        }
        if (surface) SDL_DestroySurface(surface);
        if (initialized) SDL_Quit();
    }

    // --- summary --------------------------------------------------------------
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) printf("all good\n");
    return g_failures;
}
