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
#include "../src/systems/dialogue.h"
#include "../src/systems/skills.h"
#include "../src/systems/combat.h"
#include "../src/systems/save.h"
#include "../src/systems/projectile.h"
#include "../src/systems/spell.h"
#include "../src/systems/audio.h"
#include "../src/systems/shop.h"
#include "../src/entity/player.h"
#include "../src/ui/minimap.h"
#include "../src/ui/worldmap.h"
#include "../src/ui/titlescreen.h"

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
    "mossvale_lodge_hall", "mossvale_herbalist", "fernhollow_cottage", "fernhollow_college",
    "dreamworld",
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
        Check(!cauldron.empty(), "and the cauldron has brews");
        Check(bench.size() + anvil.size() + cauldron.size() == all.size(), "every recipe belongs to exactly one station");
        for (const ItemDef* r : all) {
            bool metal = false, brewed = false;
            for (const auto& in : r->craft_inputs)
                if (const ItemDef* mat = items.Get(in.first)) {
                    metal |= mat->metal;
                    brewed |= std::find(mat->tags.begin(), mat->tags.end(), "brewing") != mat->tags.end();
                }
            const bool at_cauldron = std::find(cauldron.begin(), cauldron.end(), r) != cauldron.end();
            Check(brewed == at_cauldron, r->craft_result + (brewed ? " is brewed at a cauldron" : " is not brewed"));
            if (brewed) continue;
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
        for (const char* simple : {"wooden_shield", "leather_body", "oak_shortbow"})
            Check(made_at(simple, CraftStation::Workbench) && !made_at(simple, CraftStation::Anvil),
                  string(simple) + " is made at a workbench, not the anvil");
        for (const char* ore : {"copper_ore", "iron_ore"})
            Check(items.Get(ore) && items.Get(ore)->metal, string(ore) + " counts as metal");
        for (const char* soft : {"logs", "oak_logs", "hide", "thread"})
            Check(items.Get(soft) && !items.Get(soft)->metal, string(soft) + " is not metal");

        // What stands in the world agrees with what it is called and drawn as.
        int anvils = 0, benches = 0, cauldrons = 0;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "workbench") continue;
                Check(o.station == "workbench" || o.station == "anvil" || o.station == "cauldron",
                      o.id + " is a known crafting station");
                const bool drawn_as_anvil = o.sprite.find("anvil") != string::npos;
                const bool drawn_as_cauldron = o.sprite.find("cauldron") != string::npos;
                Check(drawn_as_anvil == (o.station == "anvil") && drawn_as_cauldron == (o.station == "cauldron"),
                      o.id + " works as the station it looks like");
                if (o.station == "anvil") ++anvils;
                else if (o.station == "workbench") ++benches;
                else ++cauldrons;
            }
        }
        Check(anvils >= 1, "there is an anvil somewhere to smith at");
        Check(benches >= 1, "there is a workbench somewhere to make simple things");
        Check(cauldrons >= 3, "there are cauldrons to brew at (" + std::to_string(cauldrons) + ")");

        // Each station trains its own skill, and smithing a tier asks for the
        // same level as its tier: the level its gear needs to be worn.
        Check(CraftSkill(CraftStation::Workbench) == SKILL_CRAFTING && CraftSkill(CraftStation::Anvil) == SKILL_SMITHING &&
              CraftSkill(CraftStation::Cauldron) == SKILL_BREWING, "workbench, anvil and cauldron train Crafting, Smithing and Brewing");
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
        Check(taken >= static_cast<int>(least * World::HEAVY_BLOCK_PUNISH) - 1,
              "the best shield there is stops none of it, and it lands half as hard again (" +
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
        // Running right and pressing the light attack.
        const auto running_light = [&]() {
            w.player.Rest();
            key(SDLK_D, true);
            frames(12);
            tap(SDLK_J);
        };
        const auto stop = [&]() {
            key(SDLK_D, false);
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
        // The rule on its own. A blow of 10 from a level 5 attacker costs
        // 10 x 5 = 50 stamina through a shield with a multiplier of one, and a
        // shield that turns aside half of it stops 5.
        BlockOutcome b = ResolveBlock(10, 5, 0.5f, 1.0f, 100.0f);
        Check(b.blocked == 5 && b.taken == 5 && std::fabs(b.stamina - 50.0f) < 0.01f && !b.broke,
              "a blow of 10 from a level 5 costs 50 stamina and half of it is stopped");
        b = ResolveBlock(6, 12, 0.5f, 1.0f, 1000.0f);
        Check(std::fabs(b.stamina - 72.0f) < 0.01f, "stamina is the damage times the attacker's level");
        b = ResolveBlock(6, 12, 0.5f, 0.5f, 1000.0f);
        Check(std::fabs(b.stamina - 36.0f) < 0.01f, "and a shield's multiplier takes its share off that");
        // Short of stamina: the block holds for what could be paid, all the
        // stamina goes, and the guard breaks.
        b = ResolveBlock(10, 20, 0.5f, 1.0f, 50.0f);
        Check(b.broke && std::fabs(b.stamina - 50.0f) < 0.01f,
              "a blow costing more than is left empties the bar and breaks the guard");
        Check(b.blocked == 1 && b.taken == 9, "and only the share that was paid for is stopped");
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
        Check(std::fabs(st0 - w.player.Stamina() - 30.0f) < 0.01f, "for 6 x 5 = 30 stamina");
        Check(w.player.skills.Xp(SKILL_DEFENCE) - xp0 >= 12 + 3,
              "stopping it trains Defence, on top of what the blow that got through does");

        const int hp1 = w.player.hp;
        taken = w.HitPlayer(4, grunt, w.player.x - 30.0f, w.player.y);
        Check(taken == 4 && w.player.hp == hp1 - 4, "a blow from behind is not blocked");

        CombatProfile dragon;
        dragon.attack_level = 60; dragon.strength_level = 64;
        const float before_break = w.player.Stamina();
        taken = w.HitPlayer(6, dragon, w.player.x + 30.0f, w.player.y);
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
        for (int st = 0; st < 3; ++st) {
            const TalentTree& t = trees.Tree(static_cast<AttackStyle>(st));
            // Three full branches five deep. The melee tree also has Footwork,
            // a fourth branch for moves made on the run, which is not counted.
            size_t core = 0;
            for (const TalentNode& n : t.nodes) core += n.branch < 3 ? 1 : 0;
            if (core != 15 || t.branches.size() < 3) shape = false;
            int tech = 0;
            for (int b = 0; b < 3; ++b)
                for (int r = 0; r < 5; ++r) {
                    const TalentNode* n = t.At(b, r);
                    if (!n) { shape = false; continue; }
                    const TalentNode* row0 = t.At(0, r);
                    if (row0 && row0->level != n->level) milestones = false;
                    if (r > 0 && t.At(b, r - 1) && t.At(b, r - 1)->level >= n->level) milestones = false;
                    if (!n->technique.empty()) { ++tech; if (r != 2) techniques = false; }
                    if (n->technique.empty() && n->effects.empty()) techniques = false;
                }
            if (tech != 3) techniques = false;
        }
        Check(shape, "each style has a tree of three branches five nodes deep");
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
        Check(techniques, "each tree teaches three techniques, and every other node does something");
        Check(trees.Tree(AttackStyle::Melee).skill == SKILL_ATTACK && trees.Tree(AttackStyle::Ranged).skill == SKILL_RANGED &&
              trees.Tree(AttackStyle::Magic).skill == SKILL_MAGIC, "melee, ranged and magic are earned by Attack, Ranged and Magic");

        Skills sk;
        Talents t;
        t.SetDatabase(&trees);
        Check(t.PointsEarned(AttackStyle::Melee, sk) == 0, "a level 1 character has no points");
        LevelUp up;
        sk.AddXp(SKILL_ATTACK, XpForLevel(17), up);
        Check(t.PointsEarned(AttackStyle::Melee, sk) == 3 && t.PointsEarned(AttackStyle::Ranged, sk) == 0,
              "Attack 17 earns three melee points and no ranged ones");
        Check(t.CanLearn("flurry", sk) == Talents::Why::Prerequisite, "a node needs the one above it");
        Check(t.CanLearn("whirlwind", sk) == Talents::Why::Level, "and its milestone level");
        Check(t.Learn("keen_edge", sk) && t.Learn("flurry", sk) && t.Learn("heavy_hand", sk),
              "three points learn three nodes");
        Check(t.CanLearn("thick_skin", sk) == Talents::Why::NoPoints, "and then there are none left");
        Check(fabsf(t.Effect("damage", AttackStyle::Melee) - 0.06f) < 1e-4f && t.Effect("damage", AttackStyle::Ranged) == 0.0f,
              "a melee damage node helps melee and not the bow");
        Check(fabsf(t.Global("charge") - 0.15f) < 1e-4f, "a global node applies whatever is held");
        Check(!t.ToggleTechnique("whirlwind"), "an unlearned technique cannot be chosen");
        t.Reset(AttackStyle::Melee);
        Check(t.PointsSpent(AttackStyle::Melee) == 0 && t.PointsFree(AttackStyle::Melee, sk) == 3,
              "unlearning a tree gives every point back");

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
            w.player.Init(ctx, "player_hero");
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
                Check(w.player.Profile().defence_bonus >= 6, "thick skin adds defence");
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
                Check(w3.player.MaxStamina() > Player::MAX_STAMINA * 1.1f, "trail legs adds stamina");
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
                const int expect = spell ? static_cast<int>(std::lround(spell->mana * 2 * 0.9f)) : -1;
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
        Check(Gathering::WorkTime(3.0f, 99, 10.0f) >= 0.6f, "however good, work takes a moment");
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
                    Check(m.Portals().empty(), "there is no walking out of a dream");
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
            for (const ItemDef* r : items.Recipes(CraftStation::Workbench)) {
                if (r->craft_result == "bedroll") bedroll_recipe = true;
                if (r->craft_result == "dreamcatcher") catcher_recipe = true;
            }
            Check(bedroll_recipe, "a bedroll is made at a workbench");
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
                w.TryInteract(ctx); frames(w, 1);
                frames(w, 90);
                Check(!w.InDream() && !w.TransitionPending(), "and by day it will not let you sleep");

                w.clock.Set(1, 21.0f);
                w.player.Damage(4);
                frames(w, 1);
                Check(w.player.interact.label == "Sleep until dawn", "at night the bed offers sleep");
                const float bx = w.player.x, by = w.player.y;
                w.TryInteract(ctx); frames(w, 1);
                Check(w.TransitionPending() && !w.FadeCaption().empty(), "pressing E at night starts to fall asleep");
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
                w.TryInteract(ctx); frames(w, 1);
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
                w.TryInteract(ctx); frames(w, 1);
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
                Check(!w.TrySleep(ctx) && !w.TransitionPending(), "you cannot sleep with a monster nearby");
                w.enemies.clear();
                Check(w.TrySleep(ctx), "and once it is gone, you can");
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
            {"fernhollow", "fernhollow"}, {"fernhollow_cottage", "fernhollow"}, {"fernhollow_college", "fernhollow"},
            {"whisperwood_trail", "whisperwood"}, {"dreamworld", "reverie"},
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
            for (const char* kind : {"town", "dungeon", "path", "grave", "camp", "landmark"}) {
                Check(string(WorldMapPanel::KindGlyph(kind)).size() == 1, string(kind) + " has a glyph");
                Check(string(WorldMapPanel::KindName(kind)) != "", string(kind) + " has a legend line");
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
            Check(outside == 0, "none of the dead has wandered outside the fence");
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
            Check(std::find(h->tags.begin(), h->tags.end(), "brewing") != h->tags.end(), h->id + " goes in a brew");
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
        for (const ItemDef* r : brews) {
            const ItemDef* potion = items.Get(r->craft_result);
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
        Check(recipe && items.StationFor(*recipe) == CraftStation::Workbench && recipe->craft_inputs.count("hide") &&
              recipe->craft_inputs.count("thread") && recipe->craft_level <= 8,
              "made at a workbench from hide and thread, early in Crafting");
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
            Check(circle && fs::exists("assets/props/spell_circle.png") && fs::exists("assets/props/mage_college.png") &&
                  fs::exists("assets/characters/magister/idle.png"),
                  "the circle is cut into its floor, and the tower, the circle and the magister are drawn");
            Map fern;
            bool door = false;
            if (fern.Load("maps/fernhollow.mx"))
                for (const Portal& p : fern.Portals()) door |= p.target_map == "fernhollow_college";
            Check(door, "and Fernhollow has a door into it");
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
                    for (const Spot& v : {Spot{"mage_college", "fernhollow", "from_fernhollow_college", 0.0f, -70.0f},
                                          Spot{"college_hall", "fernhollow_college", "entrance", 0.0f, -110.0f}}) {
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
