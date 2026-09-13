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
#include "../src/ui/minimap.h"

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
    "whisperwood_trail", "mossvale", "fernhollow",
    "mossvale_lodge_hall", "mossvale_herbalist", "fernhollow_cottage",
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
                           "fernhollow_cottage", "mossvale", "fernhollow", "whisperwood_trail"}) {
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
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx)
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
            if (string(id) == "overworld" && o.id != "sign_trailhead") continue;
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

    // --- the HUD --------------------------------------------------------------
    Section("hud fittings");
    {
        for (const char* art : {"assets/ui/minimap_ring.png",
                                "assets/icons/hud_heart.png",
                                "assets/icons/hud_drop.png"})
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
                        held = SDLK_Z;
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
        Check(oona_thanks, "Oona's closing conversation remains available after the automatic talk event");

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
        world.player.Init(ctx, "player_male");
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
                };
                for (const View& view : views) {
                    World world;
                    world.player.Init(ctx, "player_male");
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
