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
#include "../src/systems/audio.h"
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
    "dreamworld",
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
        p.Init(ctx, "player_male");
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
        full.Init(ctx, "player_male");
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
        tight.Init(ctx, "player_male");
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
        Check(bench.size() + anvil.size() == all.size(), "every recipe belongs to exactly one station");

        for (const ItemDef* r : all) {
            bool metal = false;
            for (const auto& in : r->craft_inputs)
                if (const ItemDef* mat = items.Get(in.first)) metal |= mat->metal;
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
        int anvils = 0, benches = 0;
        for (const char* id : kMaps) {
            Map m;
            if (!m.Load(string("maps/") + id + ".mx")) continue;
            for (const MapObject& o : m.Objects()) {
                if (o.type != "workbench") continue;
                Check(o.station == "workbench" || o.station == "anvil",
                      o.id + " is a known crafting station");
                const bool drawn_as_anvil = o.sprite.find("anvil") != string::npos;
                Check(drawn_as_anvil == (o.station == "anvil"),
                      o.id + " works as the station it looks like");
                (o.station == "anvil" ? anvils : benches)++;
            }
        }
        Check(anvils >= 1, "there is an anvil somewhere to smith at");
        Check(benches >= 1, "there is a workbench somewhere to make simple things");
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

        // A rig with no sprint clip still speeds up, and runs rather than
        // freezing on a missing animation.
        const Run fallback = run_for("player_male", true, 1.0f);
        Check(fallback.sprinted && fallback.clip == "run", "a rig without a sprint clip runs faster instead");
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
