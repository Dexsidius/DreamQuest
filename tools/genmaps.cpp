// -----------------------------------------------------------------------------
//  genmaps - builds the DreamQuest world into maps/*.mx
//
//  Output is the LevelEdit-Plus export format: a dictionary of tile name ->
//  image + centre-anchored placements. Everything the game needs on top of
//  that (draw layers, collision, portals, spawns, enemies, NPCs, objects) goes
//  under a separate "dreamquest" key, which the editor ignores. That means a
//  map generated here can be opened in LevelEdit-Plus, edited by hand, saved,
//  and still run.
//
//  The generator is deterministic: the same seed always produces the same
//  world, so a map can be regenerated after tweaking the layout without
//  invalidating anything else.
//
//  Build:   see build.ps1 -Tools, or compile.sh
//  Run:     ./genmaps            (writes into maps/ relative to the cwd)
// -----------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <filesystem>
#include <random>

#include "../src/json.hpp"

using json = nlohmann::json;
using std::string;
using std::vector;

namespace fs = std::filesystem;

// --- deterministic noise -----------------------------------------------------

static float Hash2(int x, int y, int seed) {
    unsigned int h = static_cast<unsigned int>(x) * 374761393u +
                     static_cast<unsigned int>(y) * 668265263u +
                     static_cast<unsigned int>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise: smooth, cheap, and good enough to keep biome edges from looking
// ruled with a straight edge.
static float Noise(float x, float y, int seed) {
    const int xi = static_cast<int>(floorf(x)), yi = static_cast<int>(floorf(y));
    const float xf = Smooth(x - xi), yf = Smooth(y - yi);
    const float a = Hash2(xi,     yi,     seed);
    const float b = Hash2(xi + 1, yi,     seed);
    const float c = Hash2(xi,     yi + 1, seed);
    const float d = Hash2(xi + 1, yi + 1, seed);
    return (a * (1 - xf) + b * xf) * (1 - yf) + (c * (1 - xf) + d * xf) * yf;
}

static float Fbm(float x, float y, int seed, int octaves = 3) {
    float sum = 0, amp = 0.5f, freq = 1.0f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum  += Noise(x * freq, y * freq, seed + i * 77) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

// --- asset manifest ----------------------------------------------------------
//
// tools/make_manifest.ps1 records the pixel size of every imported image and
// sorts the loose ground decals by colour family. Reading it here means art is
// placed at the size it was drawn at, and a decal only ever lands on terrain
// whose palette it belongs to.

// genmaps links no SDL, so it carries its own four floats rather than
// borrowing SDL_FRect from the game.
struct Rect4 { float x, y, w, h; };

struct Manifest {
    std::map<string, std::pair<int, int>> size;      // "decor/patch_00" -> w,h
    std::map<string, vector<string>> families;       // "grass" -> decal names

    bool Load(const string& path) {
        std::ifstream in(path);
        if (!in) {
            std::printf("genmaps: no %s - run tools/make_manifest.ps1\n", path.c_str());
            return false;
        }
        json root;
        try {
            in >> root;
        } catch (const std::exception& e) {
            std::printf("genmaps: bad manifest: %s\n", e.what());
            return false;
        }

        if (root.contains("assets"))
            for (auto it = root["assets"].begin(); it != root["assets"].end(); ++it)
                size[it.key()] = {it.value().value("w", 32), it.value().value("h", 32)};

        if (root.contains("families"))
            for (auto it = root["families"].begin(); it != root["families"].end(); ++it) {
                vector<string> names;
                if (it.value().is_array())
                    for (const auto& n : it.value()) names.push_back(n.get<string>());
                families[it.key()] = names;
            }
        return true;
    }

    bool Has(const string& key) const { return size.count(key) > 0; }

    // A picture the manifest has never heard of comes back as the fallback,
    // and is remembered: a prop placed at 32x32 because nobody ran
    // make_manifest.ps1 after rendering it looks like a fence post, and did, for
    // everything new in the Westwold, until the gate towers did it too and were
    // noticed. main() refuses to finish with any of these outstanding.
    std::pair<int, int> Size(const string& key, int fallback = 32) const {
        auto it = size.find(key);
        if (it == size.end()) { unsized.insert(key); return {fallback, fallback}; }
        return it->second;
    }
    mutable std::set<string> unsized;

    const vector<string>& Family(const string& name) const {
        static const vector<string> empty;
        auto it = families.find(name);
        return it == families.end() ? empty : it->second;
    }
};

static Manifest g_manifest;

// --- the world map ---------------------------------------------------------------
//
// What the overworld holds, written out beside the maps as data/worldmap.json.
// The game bakes the terrain picture itself from maps/overworld.mx; what it
// cannot work out on its own is which of the portals is a dungeon and which is
// the road to another zone, or what a place is called before you have been
// there. That is decided here, where the things are placed.
struct WorldMark {
    string kind;      // dungeon | path | town | camp | grave | landmark
    string label;
    int x = 0, y = 0;
    string town;      // for a town: which one, so its shops can be listed
};
static vector<WorldMark> g_world_marks;

static void MarkWorld(const string& kind, const string& label, int x, int y,
                      const string& town = "") {
    g_world_marks.push_back({kind, label, x, y, town});
}

// And what every other map is, for the map screen to draw the one the player is
// standing in: its name; whether it is open country, a dungeon, or a room in a
// building; and where its ways out lead, which is how the screen works out that
// a room is a room *of* somewhere and which road off the Hollowmarch leads to a
// place three maps away. Gathered as each map is written, so a new map is on
// the list by being built.
struct AreaEntry {
    string id, name, kind;
    vector<string> exits;
};
static vector<AreaEntry> g_areas;

// For quest waypoints: who stands where, what is where and what it yields, what
// lives where, and which way out leads to which map, for every map there is.
// The game works out where a quest's next stage is from this and nothing else,
// so it can point at a map the player has never loaded. Written once at the end
// as data/waypoints.json; genmaps is the only thing that ever has all of it in
// one place.
static json g_waypoint_maps = json::object();

// The overworld is the first thing built and the area list is only complete at
// the end, so what it asked to have written is kept until then.
struct WorldMapRequest { string dir; int px_w = 0, px_h = 0, offset_x = 0; bool asked = false; };
static WorldMapRequest g_world_map;

static void WriteWorldMap(const string& dir, int px_w, int px_h, int offset_x) {
    g_world_map = {dir, px_w, px_h, offset_x, true};
}

static void FlushWorldMap() {
    if (!g_world_map.asked) return;
    const string& dir = g_world_map.dir;
    const int px_w = g_world_map.px_w, px_h = g_world_map.px_h, offset_x = g_world_map.offset_x;
    json root;
    root["width"]  = px_w;
    root["height"] = px_h;
    json areas = json::object();
    for (const AreaEntry& a : g_areas) {
        json j;
        j["name"] = a.name;
        j["kind"] = a.kind;
        j["exits"] = a.exits;
        areas[a.id] = j;
    }
    root["areas"] = areas;
    json marks = json::array();
    for (const WorldMark& m : g_world_marks) {
        json j;
        j["kind"]  = m.kind;
        j["label"] = m.label;
        // In the map's own pixels: the overworld is shifted east of its origin.
        j["x"] = m.x + offset_x;
        j["y"] = m.y;
        if (!m.town.empty()) j["town"] = m.town;
        marks.push_back(j);
    }
    root["marks"] = marks;
    fs::create_directories(dir);
    {
        std::ofstream ways(dir + "/waypoints.json", std::ios::trunc);
        ways << json{{"maps", g_waypoint_maps}}.dump();
        std::printf("  %-24s %5zu maps\n", "waypoints.json", g_waypoint_maps.size());
    }
    std::ofstream out(dir + "/worldmap.json", std::ios::trunc);
    out << root.dump(2);
    std::printf("  %-24s %6zu marks, %zu areas\n", "worldmap.json", g_world_marks.size(), g_areas.size());
}

// --- map builder -------------------------------------------------------------

struct TileGroup {
    string filepath;
    int    layer = 0;
    vector<std::array<int, 4>> locations;   // cx, cy, w, h
};

class MapBuilder {
public:
    MapBuilder(const string& id, const string& display, int w, int h)
        : id(id), display(display), width(w), height(h) {
        dq["version"]   = 1;
        dq["tile_size"] = 16;
        dq["bounds"]    = json::array({w, h});
        dq["layers"]    = json::object();
        dq["solid"]     = json::array();
        dq["collision"] = json::array();
        dq["water"]     = json::array();
        dq["portals"]   = json::array();
        dq["spawns"]    = json::object();
        dq["enemies"]   = json::array();
        dq["npcs"]      = json::array();
        dq["objects"]   = json::array();
    }

    // Places art by top-left corner; the file stores centres, as the editor does.
    void Place(const string& name, const string& path, int layer,
               int x, int y, int w, int h) {
        TileGroup& g = groups[name];
        if (g.filepath.empty()) {
            g.filepath = path;
            g.layer = layer;
            dq["layers"][name] = layer;
        }
        g.locations.push_back({x + ox + w / 2, y + h / 2, w, h});
    }

    // True for tiles whose art has visible detail rather than being one colour.
    static bool Textured(const string& name) {
        return name == "dungeon_wall" ||
               name == "marsh_stone" || name == "marsh_dark";
    }

    // Lays the base layer over a cell. Flat fills cover the whole cell with a
    // single quad; textured tiles are tiled at their authored 16px so their
    // detail stays the same scale as the sprites standing on them.
    void Ground(const string& name, int x, int y, int size) {
        const string path = "assets/tiles/" + name + ".png";
        if (!Textured(name)) {
            Place(name, path, 0, x, y, size, size);
            return;
        }
        for (int oy = 0; oy < size; oy += 16)
            for (int ox = 0; ox < size; ox += 16)
                Place(name, path, 0, x + ox, y + oy, 16, 16);
    }

    void Decor(const string& file, int x, int y, int w, int h, int layer = 1) {
        // Decorations hang above their footprint, so anchor them by the base.
        Place(fs::path(file).stem().string(), file, layer, x - w / 2, y - h, w, h);
    }

    // Standing scenery at the size it was drawn, anchored on its base so it
    // sorts against the player correctly.
    void Prop(const string& group, const string& name, int x, int y, int layer = 1) {
        const auto wh = g_manifest.Size(group + "/" + name);
        const string path = "assets/" + group + "/" + name + ".png";
        Place(name, path, layer, x - wh.first / 2, y - wh.second, wh.first, wh.second);
    }

    // Makes a piece of scenery sort against people this far above its base.
    void SortLift(const string& name, int px) { dq["sort_lift"][name] = px; }

    // A decal lying flat on the ground: centred, and on the base layer so
    // nothing sorts against it.
    void Flat(const string& group, const string& name, int x, int y) {
        const auto wh = g_manifest.Size(group + "/" + name);
        const string path = "assets/" + group + "/" + name + ".png";
        Place(name, path, 0, x - wh.first / 2, y - wh.second / 2, wh.first, wh.second);
    }

    // Something that lies flat on the floor over the floor tiles -- a rug.
    // The ground layer is drawn in the order its tiles appear in the file, and
    // the file groups tiles by name, alphabetically, so "inn_rug" would be
    // written ahead of "plank_floor" and then painted over by it. The key is
    // prefixed with '~', which sorts after every lower-case letter, so an
    // overlay always lands on top of the floor it is laid on.
    void Overlay(const string& group, const string& name, int x, int y) {
        const auto wh = g_manifest.Size(group + "/" + name);
        const string path = "assets/" + group + "/" + name + ".png";
        Place("~" + name, path, 0, x - wh.first / 2, y - wh.second / 2, wh.first, wh.second);
    }

    void Collision(int x, int y, int w, int h) {
        dq["collision"].push_back(json::array({x + ox, y, w, h}));
    }

    // Collision that a swimmer may pass. Everything that walks is stopped by
    // it exactly as before; the ducks and the geese at Fernhollow are not.
    // Only water somebody is meant to be in wants marking -- the sea along the
    // overworld's edge is still a plain wall, and nothing can swim there.
    void Water(int x, int y, int w, int h) {
        dq["water"].push_back(json::array({x + ox, y, w, h}));
    }

    void Spawn(const string& name, int x, int y) {
        dq["spawns"][name] = json::array({x + ox, y});
    }

    // Ice that bears a walker and not a runner: see World::UpdateThinIce.
    // `weak` is a darker patch that strains under a walker too.
    void ThinIce(int x, int y, int w, int h, float weak = 0.0f) {
        json t;
        t["rect"] = json::array({x + ox, y, w, h});
        if (weak > 0.0f) t["weak"] = weak;
        dq["thin_ice"].push_back(t);
    }

    // True when something small at (x, y) -- a bug a few pixels off the
    // ground -- would be drawn behind a picture standing there: a bush, a
    // tree, a hut whose frame takes in the point and whose foot is in front
    // of it. Most scenery has no collision, so Clear() cannot say.
    bool Covered(int x, int y) const {
        x += ox;
        for (const auto& [name, g] : groups) {
            if (g.layer == 0) continue;
            for (const auto& l : g.locations) {
                const int left = l[0] - l[2] / 2, top = l[1] - l[3] / 2;
                if (x >= left - 14 && x <= left + l[2] + 14 && y - 8 >= top - 14 && top + l[3] >= y - 14) return true;
            }
        }
        return false;
    }

    // True when a player standing with their feet at (x, y) would be on
    // burning ground placed so far: a ford, a pool, a vent in the embers.
    bool OnHazard(int x, int y) const {
        if (!dq.contains("hazards")) return false;
        x += ox;
        for (const auto& h : dq["hazards"]) {
            const int hx = h["rect"][0], hy = h["rect"][1], hw = h["rect"][2], hh = h["rect"][3];
            if (x - 8 < hx + hw && hx < x + 8 && y - 10 < hy + hh && hy < y) return true;
        }
        return false;
    }

    // True when a player standing with their feet at (x, y) touches none of
    // the collision placed so far. The foot box matches the self-test's.
    bool Clear(int x, int y) const {
        x += ox;
        const auto hits = [&](const json& list) {
            for (const auto& c : list) {
                const int cx = c[0], cy = c[1], cw = c[2], ch = c[3];
                if (x - 8 < cx + cw && cx < x + 8 && y - 10 < cy + ch && cy < y) return true;
            }
            return false;
        };
        // Water counts: a walker cannot stand in it either, so a prop or an
        // NPC placed by Clear() must not end up in the pond.
        return !hits(dq["collision"]) && !(dq.contains("water") && hits(dq["water"]));
    }

    void Portal(int x, int y, int w, int h, const string& target,
                const string& spawn, const string& label,
                bool interact = true, const string& locked_by = "") {
        json p;
        p["rect"]     = json::array({x + ox, y, w, h});
        p["target"]   = target;
        p["spawn"]    = spawn;
        p["label"]    = label;
        p["interact"] = interact;
        if (!locked_by.empty()) p["locked_by"] = locked_by;
        dq["portals"].push_back(p);
    }

    // Marks the last portal as leading somewhere a new character should not
    // wander into unwarned.
    void Danger(int combat_level) { dq["portals"].back()["level"] = combat_level; }
    // And one that is closed outright below a Combat level.
    void Requires(int combat_level) { dq["portals"].back()["min_combat"] = combat_level; }
    // And one that wants a key. PlaceBuilding makes the portal itself, so the
    // lock is put on afterwards rather than threaded through its arguments.
    void Lock(const string& item) { dq["portals"].back()["locked_by"] = item; }

    // Ground that burns while it is stood on.
    void Hazard(int x, int y, int w, int h, float dps, const string& kind = "fire") {
        json hz;
        hz["rect"] = json::array({x + ox, y, w, h});
        hz["dps"]  = dps;
        hz["kind"] = kind;
        dq["hazards"].push_back(hz);
    }

    void Enemy(const string& type, int x, int y, int level,
               float respawn = 28.0f, float leash = 260.0f) {
        json e;
        e["type"]    = type;
        e["x"]       = x + ox;
        e["y"]       = y;
        e["level"]   = level;
        e["respawn"] = respawn;
        e["leash"]   = leash;
        dq["enemies"].push_back(e);
    }

    // The same, kept under the water until somebody comes too close: see
    // EnemySpawnDef::lurk. For a monster that swims, at a post in water.
    void LurkingEnemy(const string& type, int x, int y, int level,
                      float respawn = 40.0f, float leash = 240.0f) {
        Enemy(type, x, y, level, respawn, leash);
        dq["enemies"].back()["lurk"] = true;
    }

    // A post that is not kept by the same thing every day: one of `pool`, the
    // same one for every post in `group`, at `level` to `level + spread`. Which,
    // is the game's to say as the map is walked into -- World::ResolveSpawn.
    void EnemyPool(const vector<string>& pool, const string& group, int x, int y, int level, int spread,
                   float respawn = 28.0f, float leash = 260.0f) {
        Enemy(pool.front(), x, y, level, respawn, leash);
        json& e = dq["enemies"].back();
        e["pool"]   = pool;
        e["group"]  = group;
        e["spread"] = spread;
    }

    // A post kept only after dark, by something that does not live here: see
    // EnemySpawnDef::night and World::Abroad. One of `pool` by the day, the
    // same one for a `group`, on `chance` of the nights; and it does not come
    // back the same night, so there is no respawn to give it.
    //
    // Always written after everything else a map has. A post is known by its
    // place in the list -- the day's hash is taken over it, and a boss killed
    // today is remembered by it -- so what was there before keeps its number.
    void NightEnemy(const vector<string>& pool, const string& group, int x, int y, int level, int spread,
                    float chance, float leash = 300.0f) {
        EnemyPool(pool, group, x, y, level, spread, 0.0f, leash);
        json& e = dq["enemies"].back();
        e["night"]  = true;
        e["chance"] = chance;
        ++night_posts;
    }
    int night_posts = 0;

    // How far, in pixels, to the nearest place somebody has a right to feel
    // safe: a way in or out, somewhere to arrive, a bed or a camp, a person, a
    // chest, a sign. Nothing that comes out at night is posted near one -- a
    // bed will not take you with a monster in sight, and a gate is not where
    // to meet a wolf.
    float NearestHaven(int x, int y) const {
        x += ox;
        float best = 1.0e9f;
        const auto near = [&](float hx, float hy) {
            best = std::min(best, std::sqrt((hx - x) * (hx - x) + (hy - y) * (hy - y)));
        };
        for (const auto& p : dq["portals"]) {
            const float px = p["rect"][0].get<float>(), py = p["rect"][1].get<float>(),
                        pw = p["rect"][2].get<float>(), ph = p["rect"][3].get<float>();
            // The nearest point of it, not its middle: a gate is wide.
            near(std::clamp(static_cast<float>(x), px, px + pw), std::clamp(static_cast<float>(y), py, py + ph));
        }
        for (auto it = dq["spawns"].begin(); it != dq["spawns"].end(); ++it) near(it.value()[0].get<float>(), it.value()[1].get<float>());
        for (const auto& n : dq["npcs"]) near(n["x"].get<float>(), n["y"].get<float>());
        for (const auto& o : dq["objects"]) {
            const string type = o.value("type", string(""));
            if (type == "bed" || type == "campsite" || type == "camp" || type == "chest" || type == "sign" ||
                type == "storage" || type == "range" || type == "note")
                near(o["x"].get<float>(), o["y"].get<float>());
        }
        return best;
    }

    // How far down the Reverie a map is: see World::DreamBonus.
    void DreamDepth(int depth) { dq["dream_depth"] = depth; }

    // Returns the NPC so a trader can be given its shop: m.Npc(...)["shop"] = id.
    // The height of the ground at (x, y), from the elevation grid if the map
    // has one, and whether it is on a ramp -- the only places the height may
    // change underfoot.
    int LevelAt(int x, int y) const {
        if (!dq.contains("elevation")) return 0;
        const json& e = dq["elevation"];
        const int cell = e.value("cell", 32), cols = e.value("cols", 0), rows = e.value("rows", 0);
        const int cx = (x + ox) / cell, cy = y / cell;
        if (cx < 0 || cy < 0 || cx >= cols || cy >= rows) return 0;
        return e["levels"][static_cast<size_t>(cy) * cols + cx].get<int>();
    }
    bool OnRamp(int x, int y) const {
        if (!dq.contains("elevation")) return false;
        for (const auto& r : dq["elevation"]["ramps"]) {
            const int rx = r[0], ry = r[1], rw = r[2], rh = r[3];
            if (x + ox >= rx && x + ox < rx + rw && y >= ry && y < ry + rh) return true;
        }
        return false;
    }
    // True when something walking straight from one point to the other never
    // stands anywhere a player could not, and never changes height except on
    // a ramp. What a roamer's loop is made of: it walks in straight lines.
    // `side` is how far either side of the line it must be clear as well: a
    // monster is wider than a line, but a bridge two planks wide is only just
    // wider than a monster, and there it is the line or nothing.
    bool Walkable(int x0, int y0, int x1, int y1, int side = 6) const {
        const float len = std::sqrt(static_cast<float>((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
        const int steps = std::max(1, static_cast<int>(len / 8.0f));
        int level = LevelAt(x0, y0);
        for (int i = 0; i <= steps; ++i) {
            const int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
            if (!Clear(x, y)) return false;
            if (side > 0 && (!Clear(x - side, y) || !Clear(x + side, y))) return false;
            const int here = LevelAt(x, y);
            if (here != level) {
                if (!OnRamp(x, y)) return false;
                level = here;
            }
        }
        return true;
    }
    // A post that walks the map: see EnemySpawnDef::route. `route` is in map
    // pixels, as everything placed is; `shown` > 0 has the game scale whatever
    // the day picks from `pool` to look that strong, and `chance` is the share
    // of days it is out at all. Put before the night posts, which keep the end
    // of the list to themselves.
    void RoamingEnemy(const vector<string>& pool, int shown, int spread, float chance,
                      const vector<std::array<int, 2>>& route, float respawn = 0.0f, float leash = 320.0f) {
        json e;
        e["type"]    = pool.front();
        e["x"]       = route.front()[0] + ox;
        e["y"]       = route.front()[1];
        e["level"]   = 1;
        e["respawn"] = respawn;
        e["leash"]   = leash;
        if (pool.size() > 1) e["pool"] = pool;
        if (shown > 0) e["shown"] = shown;
        if (spread > 0) e["spread"] = spread;
        if (chance < 1.0f) e["chance"] = chance;
        json r = json::array();
        for (const auto& p : route) r.push_back(json::array({p[0] + ox, p[1]}));
        e["route"] = r;
        json& list = dq["enemies"];
        size_t at = 0;
        while (at < list.size() && !list[at].value("night", false)) ++at;
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), e);
    }

    json& Npc(const string& npc_id, const string& name, const string& sprite,
              int x, int y, const string& dialogue, int facing = 0,
              bool wanders = false) {
        json n;
        n["id"]       = npc_id;
        n["name"]     = name;
        n["sprite"]   = sprite;
        n["x"]        = x + ox;
        n["y"]        = y;
        n["dialogue"] = dialogue;
        n["facing"]   = facing;
        n["wanders"]  = wanders;
        dq["npcs"].push_back(n);
        return dq["npcs"].back();
    }

    json& Object(const string& obj_id, const string& type, int x, int y) {
        json o;
        o["id"]   = obj_id;
        o["type"] = type;
        o["x"]    = x + ox;
        o["y"]    = y;
        dq["objects"].push_back(o);
        return dq["objects"].back();
    }

    // The height grid, written under "dreamquest" so LevelEdit-Plus keeps
    // ignoring it. levels is row-major, one small integer per cell; ramps are
    // rectangles where walking between levels is allowed, which is the only
    // thing that stops a raised map becoming a set of islands.
    // `face` is the tile an exposed edge is drawn in and `lip` the colour
    // along its top: a bank of soil with grass over it, unless a map says
    // otherwise -- the Bayou's raised ground is decking.
    void Elevation(int cell, int cols, int rows,
                   const vector<int>& levels, const vector<Rect4>& ramps,
                   const string& face = "", std::array<int, 3> lip = {-1, -1, -1}) {
        json e;
        if (!face.empty()) e["face"] = face;
        if (lip[0] >= 0) e["lip"] = json::array({lip[0], lip[1], lip[2]});
        e["cell"] = cell;
        e["cols"] = cols;
        e["rows"] = rows;
        e["levels"] = levels;
        json rr = json::array();
        for (const Rect4& r : ramps)
            rr.push_back(json::array({r.x + ox, r.y, r.w, r.h}));
        e["ramps"] = rr;
        dq["elevation"] = e;
    }

    // A room dressed for whatever stands in its ring: see World::HouseDress and
    // docs/MAP_FORMAT.md. A "floor" or "wall" tile group is drawn from its
    // `undyed` picture instead and tinted with the totem's colour; "cloth" and
    // "trim" groups are there only while something stands, tinted; "plain"
    // ones only while nothing does. The game knows none of the names: this is
    // where they are said.
    void Dress(const string& role, const string& group, const string& undyed = "") {
        json& t = dq["themed"];
        if (role == "floor" || role == "wall") t[role][group] = undyed;
        else t[role].push_back(group);
    }
    // And where the totem's own light is.
    void DressLight(int x, int y) { dq["themed"]["light"] = json::array({x + ox, y}); }

    void Interior(bool v) { dq["interior"] = v; }
    // Ground fog, drawn by the fog shader over the floor and under everyone:
    // how thick, what colour, how much thicker over and beside water, and --
    // when `region` has a width -- only in there, fading in at its edges.
    void Fog(float density, float water, std::array<int, 3> colour, Rect4 region = {0, 0, 0, 0}) {
        json f;
        f["density"] = density;
        f["water"] = water;
        f["colour"] = json::array({colour[0], colour[1], colour[2]});
        if (region.w > 0.0f) f["region"] = json::array({region.x + ox, region.y, region.w, region.h});
        dq["fog"] = f;
    }
    void Ambient(const string& v) { dq["ambient"] = v; }
    // The line under the zone name on the banner shown when a player walks in.
    void Subtitle(const string& v) { dq["subtitle"] = v; }
    void Background(int r, int g, int b) {
        dq["background"] = json::array({r, g, b, 255});
    }

    void Write(const string& dir) const {
        json root;
        root["name"] = display;

        json tiles = json::object();
        for (const auto& kv : groups) {
            json entry;
            entry["filepath"] = kv.second.filepath;
            json locs = json::array();
            for (const auto& l : kv.second.locations)
                locs.push_back(json::array({l[0], l[1], l[2], l[3]}));
            entry["locations"] = locs;
            tiles[kv.first] = entry;
        }
        root["tiles"] = tiles;
        root["dreamquest"] = dq;

        // For the map screen: see AreaEntry.
        {
            AreaEntry area;
            area.id = id;
            area.name = display;
            const string ambient = dq.value("ambient", string(""));
            area.kind = ambient == "dungeon" ? "dungeon" : dq.value("interior", false) ? "interior" : "land";
            if (dq.contains("portals"))
                for (const auto& portal : dq["portals"]) {
                    const string to = portal.value("target", string(""));
                    if (!to.empty() && std::find(area.exits.begin(), area.exits.end(), to) == area.exits.end())
                        area.exits.push_back(to);
                }
            g_areas.push_back(area);
        }
        {
            json w;
            w["name"]  = display;
            w["dream"] = dq.value("ambient", string("")) == "dream";
            json exits = json::array(), people = json::array(), things = json::array(), posts = json::array();
            if (dq.contains("portals"))
                for (const auto& portal : dq["portals"]) {
                    if (!portal.contains("rect") || portal.value("target", string("")).empty()) continue;
                    const auto& r = portal["rect"];
                    exits.push_back({{"x", r[0].get<float>() + r[2].get<float>() / 2.0f},
                                     {"y", r[1].get<float>() + r[3].get<float>() / 2.0f},
                                     {"to", portal["target"]}, {"label", portal.value("label", string(""))}});
                }
            for (const auto& n : dq["npcs"])
                people.push_back({{"id", n["id"]}, {"name", n["name"]}, {"x", n["x"]}, {"y", n["y"]}});
            for (const auto& o : dq["objects"]) {
                // Light on a floor is nothing anybody is sent to.
                if (o.value("type", string("")) == "glass_light") continue;
                json t = {{"id", o["id"]}, {"kind", o["type"]}, {"x", o["x"]}, {"y", o["y"]}};
                if (o.contains("yield")) t["yield"] = o["yield"];
                // What a bench works as, so a quest that asks for something to
                // be made can be pointed at somewhere it can be.
                if (o.contains("station")) t["station"] = o["station"];
                if (o.contains("title")) t["title"] = o["title"];
                things.push_back(t);
            }
            for (const auto& e : dq["enemies"]) {
                // Not what comes out at night: a contract for wolves is filled
                // where wolves live, not where two might be after dark.
                if (e.value("night", false) || e.contains("route")) continue;
                json types = e.contains("pool") ? e["pool"] : json::array({e["type"]});
                posts.push_back({{"types", types}, {"x", e["x"]}, {"y", e["y"]}});
            }
            w["exits"] = exits; w["people"] = people; w["things"] = things; w["posts"] = posts;
            g_waypoint_maps[id] = w;
        }

        fs::create_directories(dir);
        const string path = dir + "/" + id + ".mx";
        std::ofstream out(path, std::ios::trunc);
        // Compact: these are generated files, and the base layer alone runs to
        // thousands of placements.
        out << root.dump();

        size_t placements = 0;
        for (const auto& kv : groups) placements += kv.second.locations.size();
        std::printf("  %-24s %6zu placements  %5zu enemies  %3zu objects  %dx%d\n",
                    (id + ".mx").c_str(), placements,
                    dq["enemies"].size(), dq["objects"].size(), width, height);
    }

    const string& Id() const { return id; }
    // For a map made from another -- Havenbrook's dream is Havenbrook's copy.
    void Rename(const string& new_id, const string& new_display) { id = new_id; display = new_display; }
    int Width() const { return width; }
    int Height() const { return height; }

    json dq;
    // Added to every x placed, so a map can grow west without renumbering
    // what is already laid out on it: the overworld keeps its cell (0, 0)
    // where it always was, and the new land lies at negative cells.
    int ox = 0;

private:
    string id, display;
    int width, height;
    std::map<string, TileGroup> groups;
};

// --- shared helpers ----------------------------------------------------------

static const char* kTrees[] = {
    "tree_00", "tree_01", "tree_02", "tree_03", "tree_04",
    "tree_05", "tree_06", "tree_07", "tree_08", "tree_09"
};
static const char* kSmallTrees[] = {
    "treesmall_00", "treesmall_02", "treesmall_04", "treesmall_06", "treesmall_08"
};
static const char* kRocks[] = {
    "rock_00", "rock_01", "rock_02", "rock_03", "rock_04",
    "rock_05", "rock_06", "rock_07"
};
static const char* kSmallRocks[] = {
    "rocksmall_00", "rocksmall_02", "rocksmall_04", "rocksmall_06"
};
static const char* kBushes[] = {
    "bush_00", "bush_01", "bush_02", "bush_03", "bush_04",
    "bush_05", "bush_06", "bush_07"
};
static const char* kSmallBushes[] = {
    "bushsmall_00", "bushsmall_02", "bushsmall_04", "bushsmall_06"
};
static const char* kFungus[] = {
    "mushroom_00", "mushroom_02", "mushroom_04", "fungus_00", "fungus_02"
};

template <size_t N>
static const char* Pick(const char* (&arr)[N], std::mt19937& rng) {
    return arr[rng() % N];
}

static string ObjPath(const string& name) { return "assets/objects/" + name + ".png"; }

// A tree the player can chop. Its collision is only the trunk, so the canopy
// overlaps the player instead of blocking them.
static void PlaceTree(MapBuilder& m, std::mt19937& rng, int index,
                      int x, int y, bool big, int level, const string& yield) {
    const string art = big ? Pick(kTrees, rng) : Pick(kSmallTrees, rng);

    json& o = m.Object("tree_" + std::to_string(index), "tree", x, y);
    o["sprite"]      = ObjPath(art);
    o["skill"]       = "Woodcutting";
    o["skill_level"] = level;
    o["yield"]       = yield;
    // What a tree is worth follows what it asks for, on the slope the seams
    // are on: 65 for a log anybody can cut, 135 for an oak at fifteen, 210 for
    // the old growth at thirty -- and each a little longer in the cutting.
    // They were all 65, so the hundred oldest trees in the Brackenwood taught
    // exactly what the ones by the sawpit did.
    o["yield_xp"]    = big ? static_cast<int>(65.0f * (1.0f + level / 13.0f) + 0.5f) : 25;
    o["gather_time"] = big ? 3.0f + level * 0.027f : 2.2f;
    o["title"]       = big ? (level >= 30 ? "old oak" : "oak") : "sapling";
    // Not for ever: on each log there is a chance the tree comes down, and
    // then a stump stands there for a while. A sapling goes sooner than an
    // oak and is back sooner.
    o["deplete"]     = big ? 0.125f : 0.25f;
    o["regrow"]      = big ? 1.5f : 1.0f;
    o["sprite_open"] = ObjPath(big ? "stump" : "stumpsmall");

    m.Collision(x - 9, y - 9, 18, 9);
}

// Which ore is in a rock, by what it yields: the name shown over it, and the
// art it is drawn with. Every ore used to be the same grey boulder with a
// different word over it, so a new miner walked up to iron they could not touch
// with nothing on screen to say which rock was the copper.
static const std::map<string, string> kOre = {
    {"copper_ore", "copper"}, {"iron_ore", "iron"}, {"coal", "coal"},
    {"azuryte_ore", "azuryte"}, {"damascus_ore", "damascus"},
    {"orichalcum_ore", "orichalcum"},
    {"diamond_ore", "diamond"}, {"platinum_ore", "platinum"}, {"demonite_ore", "demonite"}};

static void PlaceRock(MapBuilder& m, std::mt19937& rng, int index,
                      int x, int y, bool big, int level, const string& yield) {
    const auto name = kOre.find(yield);
    // Two of each, so a hillside of copper is not one rock stamped out; and a
    // yield with no rock of its own falls back to the plain boulders.
    const string art = name == kOre.end()
        ? (big ? Pick(kRocks, rng) : Pick(kSmallRocks, rng))
        : (big ? "ore_" : "oresmall_") + name->second + "_" +
              std::to_string(std::uniform_int_distribution<int>(0, 1)(rng));

    json& o = m.Object("rock_" + std::to_string(index), "rock", x, y);
    o["sprite"]      = ObjPath(art);
    o["skill"]       = "Mining";
    o["skill_level"] = level;
    o["yield"]       = yield;
    // Deeper ore is slower to work and worth more for it.
    o["yield_xp"]    = static_cast<int>((big ? 60 : 24) * (1.0f + level / 12.0f));
    o["gather_time"] = (big ? 3.2f : 2.4f) + level * 0.02f;
    o["title"]       = (name == kOre.end() ? string("ore") : name->second) + (big ? " seam" : " outcrop");
    // And on each ore a chance the seam gives out. A seam holds more than an
    // outcrop, and deeper ore takes longer to show again.
    o["deplete"]     = big ? 0.167f : 0.333f;
    o["regrow"]      = (big ? 1.0f : 0.75f) + level * 0.02f;

    m.Collision(x - 14, y - 12, 28, 12);
}

static void PlaceChest(MapBuilder& m, const string& chest_id, int x, int y,
                       const string& table) {
    json& o = m.Object(chest_id, "chest", x, y);
    o["sprite"]      = ObjPath("chest");
    o["sprite_open"] = ObjPath("chest_open");
    o["loot"]        = table;
    m.Collision(x - 14, y - 10, 28, 10);
}

// A chest with one named thing in it rather than a table roll, standing in the
// world only while its quest is being done. It is how an item can exist in
// exactly one place: no loot table can roll what is not in one, and the chest
// is not there to be opened before the quest is taken or after it is over.
static void PlaceRelicChest(MapBuilder& m, const string& chest_id, int x, int y,
                            const string& item, const string& quest) {
    json& o = m.Object(chest_id, "chest", x, y);
    o["sprite"]      = ObjPath("chest");
    o["sprite_open"] = ObjPath("chest_open");
    o["item"]        = item;
    o["needs_quest"] = quest;
    m.Collision(x - 14, y - 10, 28, 10);
}

// A waystone: the old stones that stand in the three towns and nowhere else.
// Asleep until somebody puts a hand on it; after that, a door to every other
// one that has been woken. The world remembers a woken stone as a flag with
// the stone's own id, which is also what draws it lit -- an object whose id is
// flagged is drawn as its `sprite_open`, the way an opened chest is.
//
// Towns only, and that is the whole of the design: the road to a place has to
// be walked once, and the wilds and the dungeons are always walked.
static void PlaceWaystone(MapBuilder& m, const string& town, int x, int y) {
    json& o = m.Object("waystone_" + town, "waystone", x, y);
    o["sprite"]      = "assets/props/waystone.png";
    o["sprite_open"] = "assets/props/waystone_lit.png";
    o["title"]       = "Waystone";
    m.Collision(x - 28, y - 20, 56, 20);
    // Where somebody arriving by it stands: in front of it, facing the town.
    m.Spawn("waystone", x, y + 30);
}

// A bed anyone may sleep in after dusk. Drawn from the same prop art as the
// rest of the furniture, but placed as an object so it can be used.
static void PlaceBed(MapBuilder& m, const string& bed_id, const string& art,
                     int x, int y, int cw, int ch, int fee = 0) {
    json& o = m.Object(bed_id, "bed", x, y);
    o["sprite"] = "assets/props/" + art + ".png";
    o["title"]  = (art == "bed_double") ? "Double bed" : "Bed";
    // An inn's bed is paid for by the night; anybody else's is theirs to lend.
    if (fee > 0) {
        o["fee"]   = fee;
        o["title"] = string(art == "bed_double" ? "The inn's double bed" : "A bed at the inn") +
                     "  -  " + std::to_string(fee) + " coins";
    }
    m.Collision(x - cw / 2, y - ch, cw, ch);
}

// A tent by a fire that a traveller may bed down in, the same way.
static void PlaceCampsite(MapBuilder& m, const string& camp_id, int x, int y) {
    json& o = m.Object(camp_id, "campsite", x, y);
    o["sprite"] = "assets/props/tent.png";
    o["title"]  = "Campsite";
}

// A place worth casting a line: an object on the water at the edge of a bank,
// drawn by the game as rings spreading on the surface. What bites there is the
// list of fish; the lowest of them sets the level the spot asks for.
static void PlaceFishingSpot(MapBuilder& m, const string& spot_id, int x, int y,
                             const string& title, const vector<string>& fish, int level) {
    json& o = m.Object(spot_id, "fishing_spot", x, y);
    o["skill"]       = "Fishing";
    o["skill_level"] = level;
    o["gather_time"] = 3.6f;
    o["title"]       = title;
    o["fish"]        = fish;
}

// --- herbs ----------------------------------------------------------------------
// What each plant is -- its name, the Foraging level it needs and the XP it is
// worth -- comes from data/items.json, so the maps and the items can never
// disagree about it.
struct HerbInfo { string name; int level = 1, xp = 10; };
static std::map<string, HerbInfo> g_herbs;

static void LoadHerbs() {
    std::ifstream in("data/items.json");
    json root;
    try { in >> root; } catch (const std::exception&) { return; }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_object() || !it.value().contains("forage")) continue;
        HerbInfo h;
        h.name  = it.value().value("name", it.key());
        h.level = it.value()["forage"].value("level", 1);
        h.xp    = it.value()["forage"].value("xp", 10);
        g_herbs[it.key()] = h;
    }
}

// A plant to pick. Drawn growing, and picked while it grows back; no collision,
// so a patch of them never walls anything off.
static void PlaceHerb(MapBuilder& m, const string& herb, int x, int y, int& index) {
    auto it = g_herbs.find(herb);
    if (it == g_herbs.end()) return;
    json& o = m.Object("herb_" + std::to_string(index++), "herb", x, y);
    o["sprite"]      = "assets/props/herb_" + herb + ".png";
    o["sprite_open"] = "assets/props/herb_" + herb + "_picked.png";
    o["skill"]       = "Foraging";
    o["skill_level"] = it->second.level;
    o["yield"]       = herb;
    o["yield_xp"]    = it->second.xp;
    o["gather_time"] = 1.4f + it->second.level * 0.012f;
    // Game hours to grow back: a marigold in under two, a starlily in five.
    // It was three and nearly nine, which with one pick a plant and a couple
    // of dozen of the rare ones in the whole world left a forager standing in
    // a picked field nineteen minutes in twenty -- five hundred hours to 99,
    // against eight for a miner.
    o["regrow"]      = 1.8f + it->second.level / 21.0f;
    string title = it->second.name;
    for (char& c : title) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    o["title"]       = title;
}

// --- bugs ----------------------------------------------------------------------------
// What each bug is -- its name, the Foraging level it is caught at and the XP it
// is worth -- comes from its "catch" block in data/items.json, as a herb's comes
// from its "forage" block. A bug has no "forage" block, so none of the herb
// rules touch it, and PlaceHerb would never place one.
struct BugInfo { string name; int level = 1, xp = 10; };
static std::map<string, BugInfo> g_bugs;
static std::set<string> g_bugs_missing;

static void LoadBugs() {
    std::ifstream in("data/items.json");
    json root;
    try { in >> root; } catch (const std::exception&) { return; }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_object() || !it.value().contains("catch")) continue;
        BugInfo b;
        b.name  = it.value().value("name", it.key());
        b.level = it.value()["catch"].value("level", 1);
        b.xp    = it.value()["catch"].value("xp", 10);
        g_bugs[it.key()] = b;
    }
}

// A bug to catch: an object with no picture of its own, which the game draws
// flying -- or, a beetle, walking -- about its spot (World::BugFlight), caught by
// hand the way a herb is picked, with Foraging's level and XP from the bug's
// own catch block, and another along to the spot a few hours later. Its id is
// its own ("bug_"), so catching one never marks a herb with the same number
// picked.
static void PlaceBug(MapBuilder& m, const string& bug, int x, int y, int& index) {
    auto it = g_bugs.find(bug);
    if (it == g_bugs.end()) { g_bugs_missing.insert(bug); return; }
    json& o = m.Object("bug_" + std::to_string(index++), "bug", x, y);
    o["skill"]       = "Foraging";
    o["skill_level"] = it->second.level;
    o["yield"]       = bug;
    o["yield_xp"]    = it->second.xp;
    o["gather_time"] = 1.2f + it->second.level * 0.006f;
    // As long to come back as a herb of its level takes to grow: a
    // swallowtail in a little over two game hours, a rime beetle nearly five.
    o["regrow"]      = 1.8f + it->second.level / 21.0f;
    string title = it->second.name;
    for (char& c : title) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    o["title"]       = title;
}

// Nobody's prompt is taken by a bug put here: no one to talk to within three
// cells, nothing else to use within two, no way out under it. A catch next to
// a signpost would argue with it over what E does.
static bool QuietAround(const MapBuilder& m, int x, int y) {
    const float fx = static_cast<float>(x + m.ox), fy = static_cast<float>(y);
    if (m.dq.contains("npcs"))
        for (const auto& n : m.dq["npcs"])
            if (std::hypot(n["x"].get<float>() - fx, n["y"].get<float>() - fy) < 96.0f) return false;
    if (m.dq.contains("objects"))
        for (const auto& o : m.dq["objects"])
            if (std::hypot(o["x"].get<float>() - fx, o["y"].get<float>() - fy) < 56.0f) return false;
    if (m.dq.contains("portals"))
        for (const auto& p : m.dq["portals"]) {
            const float px = p["rect"][0].get<float>(), py = p["rect"][1].get<float>(),
                        pw = p["rect"][2].get<float>(), ph = p["rect"][3].get<float>();
            if (std::hypot(std::clamp(fx, px, px + pw) - fx, std::clamp(fy, py, py + ph) - fy) < 64.0f) return false;
        }
    return true;
}

// Where a map's bugs go: every cell in [cx0, cx1) x [cy0, cy1) that `fits`
// allows, ranked by a hash of the cell -- never a builder's rng, which would
// reshuffle every bush and tree drawn from it after -- and taken in that order
// so long as each is `apart` cells from the others already `taken`, on ground
// that can be stood on, off burning ground, quiet round it, and out from
// behind the scenery (a swallowtail was put down behind a bush on the
// Whisperwood's verge, and all that showed of it was its feelers). The spot is
// a little off the cell's middle, by the same hash, so they do not line up.
// Returns how many went down.
static int PlaceBugs(MapBuilder& m, const string& bug, int want, int CELL, int cx0, int cy0, int cx1, int cy1,
                     uint32_t salt, float apart, const std::function<bool(int, int)>& fits, int& index,
                     vector<std::pair<int, int>>* taken = nullptr) {
    struct Spot { float rank; int cx, cy; };
    vector<Spot> spots;
    for (int cy = cy0; cy < cy1; ++cy)
        for (int cx = cx0; cx < cx1; ++cx)
            if (fits(cx, cy)) spots.push_back({Hash2(cx, cy, static_cast<int>(salt)), cx, cy});
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.cy != b.cy ? a.cy < b.cy : a.cx < b.cx;
    });
    vector<std::pair<int, int>> own;
    vector<std::pair<int, int>>& mine = taken ? *taken : own;
    int placed = 0;
    for (const Spot& s : spots) {
        if (placed >= want) break;
        bool room = true;
        for (const auto& p : mine)
            if (std::hypot(static_cast<float>(p.first - s.cx), static_cast<float>(p.second - s.cy)) < apart) room = false;
        if (!room) continue;
        const int x = s.cx * CELL + 8 + static_cast<int>(Hash2(s.cx, s.cy, static_cast<int>(salt) + 1) * 16.0f);
        const int y = s.cy * CELL + 12 + static_cast<int>(Hash2(s.cx, s.cy, static_cast<int>(salt) + 2) * 14.0f);
        if (!m.Clear(x, y) || m.OnHazard(x, y) || !QuietAround(m, x, y) || m.Covered(x, y)) continue;
        PlaceBug(m, bug, x, y, index);
        mine.push_back({s.cx, s.cy});
        ++placed;
    }
    return placed;
}

// A hive to take honey from: its picture, a band of collision at its foot, and
// what it gives -- honey, a little Foraging XP to anybody, and more honey a few
// hours after. The bees round it are the game's (World::DrawBees).
static void PlaceHive(MapBuilder& m, const string& obj_id, const string& art, const string& title, int x, int y,
                      int solid_w) {
    json& o = m.Object(obj_id, "hive", x, y);
    o["sprite"]      = "assets/props/" + art + ".png";
    o["skill"]       = "Foraging";
    o["skill_level"] = 1;
    o["yield"]       = "honey";
    o["yield_xp"]    = 12;
    o["gather_time"] = 1.6f;
    o["regrow"]      = 5.0f;
    o["title"]       = title;
    m.Collision(x - solid_w / 2, y - 10, solid_w, 10);
}

// A cauldron to brew at.
// A tanner's frame that can be worked at: a hide laced into poles to dry, and
// the beam beside it that hides are scraped and cut on. It is the tanning rack
// that always stood about a tannery as scenery, and is now the tannery's
// station -- everything of leather is made on one. See CraftStation::Rack.
static void PlaceTanningRack(MapBuilder& m, const string& obj_id, int x, int y) {
    json& o = m.Object(obj_id, "workbench", x, y);
    o["sprite"]  = "assets/props/tanning_rack.png";
    o["title"]   = "Tanning rack";
    o["station"] = "rack";
    m.Collision(x - 24, y - 10, 48, 10);
}

static void PlaceCauldron(MapBuilder& m, const string& obj_id, int x, int y) {
    json& o = m.Object(obj_id, "workbench", x, y);
    o["sprite"]  = "assets/props/cauldron.png";
    o["title"]   = "Cauldron";
    o["station"] = "cauldron";
    m.Collision(x - 16, y - 12, 32, 12);
}

// An enchanting table: where charms are worked into worn pieces, for Magic.
// Its own object type rather than a crafting station, because what it makes
// is not on any list -- it is one of the player's own pieces.
static void PlaceEnchantingTable(MapBuilder& m, const string& obj_id, int x, int y) {
    json& o = m.Object(obj_id, "altar", x, y);
    o["sprite"] = "assets/props/enchanting_table.png";
    o["title"]  = "Enchanting table";
    m.Collision(x - 22, y - 14, 44, 14);
}

// Buildings are drawn bottom-centre. Collision runs along the lower wall but
// leaves the doorway open, and a portal sits in the gap. exit_spawn names the
// spot on the doorstep that the interior's way out should arrive at.
static void PlaceBuilding(MapBuilder& m, const string& art, int x, int y,
                          int w, int h, const string& target,
                          const string& spawn, const string& label,
                          const string& exit_spawn,
                          const string& group = "objects") {
    m.Prop(group, art, x, y);

    const int wall_h = 30;
    const int door_w = 30;
    const int half = w / 2;

    m.Collision(x - half + 4, y - wall_h, half - door_w / 2 - 4, wall_h);
    m.Collision(x + door_w / 2, y - wall_h, half - door_w / 2 - 4, wall_h);
    // Keep the upper storey solid too, so you cannot walk through the roof.
    m.Collision(x - half + 4, y - h + 6, w - 8, h - wall_h - 6);

    m.Portal(x - door_w / 2, y - wall_h + 8, door_w, wall_h, target, spawn, label);

    // On the step outside, clear of the door, facing the street.
    m.Spawn(exit_spawn, x, y + 22);
}

// A length of split-rail fence along a line of cells, with a gap left for a gate.
static void Fence(MapBuilder& m, int CELL, int cx0, int cx1, int cy, int gate_cx = -999) {
    for (int cx = cx0; cx < cx1; cx += 2) {
        if (abs(cx - gate_cx) <= 1) continue;
        const int x = cx * CELL + CELL, y = cy * CELL + 20;
        m.Prop("props", "rail_fence", x, y);
        m.Collision(x - 30, y - 8, 60, 8);
    }
}

// --- town gates ------------------------------------------------------------------
//
// If it is the way into a town, it is a gate, with somebody standing at it. A
// warden posted in the middle of a field beside a gap in an invisible fence is
// not guarding anything.
//
// There are two kinds, because there are two ways a road can meet a wall. One
// that runs north or south goes through a gate seen from the front: the
// gatehouse, prop_town_gate, one picture with the road under its lintel. One
// that runs east or west goes through a gate seen from the side, and that
// cannot be one picture -- whoever is on the road is in front of the tower north
// of it and behind the tower south of it, and a picture is sorted once -- so it
// is one tower, stood twice.

// A gatehouse across a road running north-south. (x, base_y) is the middle of
// the threshold, at the bottom of the picture. The towers are solid and the way
// between them is eighty pixels; `lengths` of palisade run off either side,
// starting hard against the towers -- they used to start a stride clear of
// them, with a wall nobody could see in the gap -- and stopping at the edge of
// the map if `map_w` says where that is.
//
// Inside a town the gatehouse stands two cells in from the edge the road leaves
// by, not on it. Seen from the town side everything just north of it is behind
// it: a warden in the gateway showed as a pair of boots under the lintel. Set
// in, there is ground south of it, which is in front of it, and that is where
// the warden stands: see FrontGateKeeper.
static void PlaceFrontGate(MapBuilder& m, int x, int base_y, int lengths, int map_w = 0) {
    m.Prop("props", "town_gate", x, base_y);
    for (int side = -1; side <= 1; side += 2) {
        m.Collision(x + (side < 0 ? -72 : 40), base_y - 38, 32, 38);
        for (int k = 0; k < lengths; ++k) {
            const int px = x + side * (97 + k * 54);
            if (map_w > 0 && (px < -20 || px > map_w + 20)) break;
            m.Prop("props", "palisade", px, base_y - 4);
            m.Collision(px - 27, base_y - 16, 54, 14);
        }
    }
}

// Where whoever keeps a front gate stands: outside it, at the foot of the
// lamp-side tower, clear of the way through.
static int FrontGateKeeperX(int x)      { return x + 56; }
static int FrontGateKeeperY(int base_y) { return base_y + 14; }

// A gate across a road running east-west, in a wall at x: a tower at the
// road's north edge and another south of it. The southern one stands a little
// way off the road, because it is drawn up over whatever is north of its foot.
static void PlaceSideGate(MapBuilder& m, int x, int road_top, int road_bottom) {
    m.Prop("props", "gate_tower", x, road_top - 2);
    m.Collision(x - 20, road_top - 26, 40, 24);
    m.Prop("props", "gate_tower", x, road_bottom + 44);
    m.Collision(x - 20, road_bottom + 20, 40, 24);
}

// A palisade down the east or west side of a village, seen along its length,
// leaving out the rows a gate stands in.
static void PalisadeSide(MapBuilder& m, int x, int from_y, int to_y, int gap_top = -1, int gap_bottom = -1) {
    for (int y = from_y; y <= to_y; y += 22) {
        if (gap_top >= 0 && y > gap_top - 34 && y < gap_bottom + 58) continue;
        m.Prop("props", "palisade_side", x, y);
    }
}

// --- overworld ---------------------------------------------------------------

enum Biome { MEADOW, GREENWOOD, FOOTHILLS, MIRE, CURSED, GRAVEYARD, WATER, ROAD, TRAIL };

static const int OW_CELL = 32;
// The Hollowmarch was 128 by 96 cells, and the Mire in its south-west corner
// ran into the edge of the world. It has grown twenty cells west and twelve
// south, out of the same noise, so the swamp, the river, the foothills and the
// meadow simply carry on. The old cells keep their numbers: the new western
// land is at negative cells, OW_X0 to -1, and MapBuilder::ox shifts it all
// onto the map. The north and east edges, where the ways out are, stay put.
static const int OW_X0 = -20;                           // westmost cell
static const int OW_W = 128, OW_H = 124;                // east edge, south edge (cells)
static const int OW_PX_W = OW_W * OW_CELL;              // the east edge, in the old x
static const int OW_PX_H = OW_H * OW_CELL;              // 3968

// Which variant of a ground family a cell gets.
//
// tools/make_ground.ps1 writes "grass", "grass_1", "grass_2" and so on. Rather
// than hard-code how many exist, this asks the asset manifest, so adding a
// variant to the generator is enough to start seeing it on the map.
//
// The choice is a hash of the coordinates rather than the drift noise used for
// the family: the family should move in broad bands, the variant should not
// move in bands at all, and reusing the same noise for both would line the
// variants up with the colour drifts and make the seams worse, not better.
static string VariantOf(const string& family, int cx, int cy) {
    int count = 1;
    while (count < 8 && g_manifest.Has("tiles/" + family + "_" + std::to_string(count)))
        ++count;
    if (count <= 1) return family;

    unsigned h = static_cast<unsigned>(cx) * 73856093u
               ^ static_cast<unsigned>(cy) * 19349663u;
    h ^= h >> 13;
    const int pick = static_cast<int>(h % static_cast<unsigned>(count));
    return pick == 0 ? family : family + "_" + std::to_string(pick);
}

// The road runs south to north; this is its centre line at a given row.
static float RoadX(int cy) {
    return 62.0f + sinf(cy * 0.075f) * 7.0f;
}

// The Whisperwood Trail leaves the Sunken Road at this row and winds east
// through the greenwood to the edge of the map, where it becomes its own zone.
static const int TRAIL_JUNCTION_CY = 60;
static float TrailY(int cx) {
    return 60.0f + sinf((cx - 64) * 0.11f) * 3.2f;
}
static bool OnTrail(int cx, int cy, float half = 1.2f) {
    return cx >= RoadX(TRAIL_JUNCTION_CY) + 1.0f && fabsf(cy - TrailY(cx)) < half;
}

// Hollowrest, the burying ground south-west of Havenbrook: its own patch of
// dead grass and turned earth inside an iron fence, out on the flat where the
// meadow runs into the Mire. An ellipse with the edge roughened by noise, so
// the ground it stands on does not look drawn with a compass.
// Big enough to walk about in: the first one was a plot the size of a room,
// and every grave in it woke at once when you opened the gate. This is a field
// -- thirty-six cells across and twenty-two deep, better than a thousand feet
// of ground -- with aisles through it and the dead spread thin.
static const int GRAVE_CX = 38, GRAVE_CY = 110;
static const float GRAVE_RX = 18.0f, GRAVE_RY = 11.0f;
static float GraveField(int cx, int cy) {
    const float dx = (cx - GRAVE_CX) / GRAVE_RX, dy = (cy - GRAVE_CY) / GRAVE_RY;
    return dx * dx + dy * dy;
}
static bool InGraveyard(int cx, int cy) {
    return GraveField(cx, cy) <= 1.0f + (Fbm(cx * 0.16f, cy * 0.16f, 2727) - 0.5f) * 0.30f;
}
// The two gaps in the fence: the lych gate at the north, and the old gap in
// the east wall where the wall has come down.
static bool GraveGate(int cx, int cy) {
    if (abs(cx - GRAVE_CX) <= 1 && cy <= GRAVE_CY) return true;
    return abs(cy - GRAVE_CY) <= 1 && cx >= GRAVE_CX;
}
// The aisles: a walk from the gate to the crypt, and one across it.
static bool GraveAisle(int cx, int cy) {
    return abs(cx - GRAVE_CX) <= 1 || abs(cy - GRAVE_CY) <= 1;
}

static Biome BiomeAt(int cx, int cy) {
    const float n = Fbm(cx * 0.045f, cy * 0.045f, 1337);

    // The river cuts across the south-west before feeding the mire.
    const float river = fabsf((cy - 66.0f) - sinf(cx * 0.09f) * 5.0f);
    if (cx < 44 && river < 2.2f + n * 1.4f) return WATER;

    // The road runs all the way to Havenbrook's gate rather than stopping a
    // cell short of it: the cobbles used to give out in the grass and the gate
    // stood on a lawn behind them.
    if (fabsf(cx - RoadX(cy)) < 1.6f && cy > 10 && cy < 90) return ROAD;
    if (OnTrail(cx, cy)) return TRAIL;
    // The causeway into the Bayou: three cells of packed earth from beside the
    // lizardmen's camp to the west edge. A trail as far as everything else is
    // concerned, so no bog forms across it and nothing is put on it.
    if (cx <= 5 && abs(cy - 80) <= 1) return TRAIL;

    if (InGraveyard(cx, cy)) return GRAVEYARD;
    if (cx > 96 && cy < 34 && n > 0.42f) return CURSED;
    if (cy < 20 + n * 8.0f) return FOOTHILLS;
    if (cx < 24 + n * 10.0f) return MIRE;
    if (cx > 86 - n * 10.0f) return GREENWOOD;
    return MEADOW;
}

// The lizardmen's camp, deep in the south of the Mire.
static const int MIRE_CAMP_X = 13, MIRE_CAMP_Y = 80;

// Open bog water in the Mire: impassable, with reeds round its edges and lily
// pads on it. Never round anything in the Mire that has to be walked to.
static bool BogAt(int cx, int cy) {
    if (BiomeAt(cx, cy) != MIRE) return false;
    const auto near = [&](int x, int y, int r) { return abs(cx - x) <= r && abs(cy - y) <= r; };
    if (near(12, 44, 5) || near(10, 60, 4) || near(MIRE_CAMP_X, MIRE_CAMP_Y, 7) || near(10, 30, 5)) return false;
    if (cx < OW_X0 + 3 || cy < 3 || cy >= OW_H - 3) return false;
    return Fbm(cx * 0.16f, cy * 0.16f, 4545) > 0.64f;
}

// How high the ground stands, in levels, at a given cell.
//
// The shape is deliberate rather than pure noise: the land climbs steadily
// toward the northern foothills, and a couple of broad shelves lift out of the
// greenwood in the east. Noise alone gives a rash of one-cell buttes, which
// reads as damage rather than as terrain.
static int ElevationAt(int cx, int cy) {
    if (BiomeAt(cx, cy) == WATER) return 0;

    // The march tilts up toward the north. Nothing in the southern half rises
    // at all, so the approach to Havenbrook stays open ground.
    //
    // The slope is broken up by noise stretched along the east-west axis. A
    // clean function of cy alone terraces the whole map into straight bands
    // from edge to edge, which reads as a flight of stairs rather than as
    // rising ground; the noise lets each contour wander a dozen cells either
    // side of where it would otherwise sit.
    float h = 0.0f;
    if (cy < 44) {
        const float ridge = Fbm(cx * 0.026f, cy * 0.055f, 7717);
        h += (44 - cy) / 15.0f + (ridge - 0.5f) * 2.4f;
    }

    // Two shelves in the greenwood, from low-frequency noise so their edges
    // wander instead of following the grid.
    const float shelf = Fbm(cx * 0.035f, cy * 0.035f, 5150);
    if (cx > 78 && shelf > 0.58f) h += 2.6f;
    if (cx > 96 && shelf > 0.70f) h += 2.2f;

    // A dip where the mire lies, so the west reads as low, wet ground.
    if (cx < 26) h -= 0.6f;

    // Rounded to whole levels with a wide flat top to each band, so the map
    // is a handful of broad terraces rather than a continuous ramp. A smooth
    // height field steps down one level at a time and every face is a single
    // ten-pixel bar, which reads as a stripe painted on the grass; plateaus
    // with two- and three-level edges read as ground.
    const int level = static_cast<int>(std::floor(h * 0.62f + 0.2f));
    return std::clamp(level, 0, 3);
}

// A strange thing lying where somebody will come across it: an item that
// starts a quest as it goes into the bag (its `starts_quest` in items.json),
// drawn on the ground as its own icon with a glint to it. Nothing guards it
// and nothing asks for a level first -- whoever finds it has found it, and what
// they can make of it is theirs to find out.
//
// Put near a monster's post, since what it starts is usually the thing that
// lives there: `near` is the post's kind (a pool counts when it can be one),
// `nth` which of them, and (dx, dy) the step from it. Or near a fixed point,
// with `near` empty. Either way on the nearest ground a player can stand on.
struct Curio {
    const char* map;
    const char* item;
    const char* quest;
    const char* title;       // "Pick up <title>"
    const char* near;
    int nth, x, y;           // with `near`, (x, y) is the step from the post
};
static const Curio kCurios[] = {
    {"bayou",          "reed_doll",         "q_doll_in_the_reeds", "the reed doll",        "bog_lurker",     2, -46, 30},
    {"bayou",          "drowned_locket",    "q_drowned_locket",    "the drowned locket",   "rot_shambler",   0, 40, -36},
    {"overworld",      "bell_clapper",      "q_tongueless_bell",   "the bell clapper",     "",               0,
                       (GRAVE_CX + 3) * OW_CELL + 12, (GRAVE_CY - 8) * OW_CELL + 20},
    {"crypt_2",        "ashcroft_letter",   "q_ashcroft_letter",   "the sealed letter",    "bone_knight",    0, 44, 28},
    {"ashen_path",     "cinder_invitation", "q_cinder_invitation", "the invitation",       "",               0, 2208, 1180},
    {"palace_dungeon", "rime_key",          "q_rime_key",          "the key of ice",       "rime_revenant",  0, 52, 20},
    {"westwold",       "bloodied_collar",   "q_bloodied_collar",   "the bloodied collar",  "wolf",           1, 48, 26},
    {"brackenwood",    "antler_circlet",    "q_antler_circlet",    "the antler circlet",   "wolf",           0, 44, 30},
};

// --- how strong a post looks -------------------------------------------------
// The game's own Enemy::ShownLevelOf, over data/enemies.json, so that a map can
// post a monster by the level it will be shown at rather than by a nudge to its
// stat block -- and so the night visitors can be chosen here by the very rules
// the self-test holds them to, instead of by guessing and seeing what fails.
static const json& EnemyData() {
    static json data;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in("data/enemies.json");
        if (!in) {
            std::fprintf(stderr, "genmaps: cannot read data/enemies.json\n");
            std::exit(1);
        }
        in >> data;
    }
    return data;
}
static const json& EnemyOf(const string& type) {
    const json& all = EnemyData();
    if (!all.contains(type)) {
        std::fprintf(stderr, "genmaps: no enemy '%s' in data/enemies.json\n", type.c_str());
        std::exit(1);
    }
    return all[type];
}
static int ShownOf(const string& type, int spawn_level) {
    const json& d = EnemyOf(type);
    const int bump = std::max(0, spawn_level - 1);
    const float hp = std::max(1, d.value("hp", 10)) * (1.0f + 0.12f * bump);
    const float hp_level = std::clamp(sqrtf(std::max(0.0f, hp - 6.0f)) * 3.4f, 1.0f, 99.0f);
    const float att = static_cast<float>(d.value("attack", 1) + bump);
    const float str = static_cast<float>(d.value("strength", 1) + bump);
    const float dfn = static_cast<float>(d.value("defence", 1) + bump);
    const float base = 0.25f * (dfn + hp_level);
    const float melee = 0.325f * (att + str);
    return std::clamp(static_cast<int>(floorf(base + melee)), 1, 99);
}
static bool BossType(const string& type) { return EnemyOf(type).value("boss", false); }
// The least spawn level at which `type` shows `shown` or more: Enemy::LevelToShow.
static int SpawnToShow(const string& type, int shown) {
    int lv = 1;
    while (lv < 250 && ShownOf(type, lv) < shown) ++lv;
    return lv;
}

// A roaming post laid on a loop of cells the caller has chosen -- a track up a
// mountain, the bridges between islands, the ground round a fort -- rather than
// round an ellipse. Each point is taken as given if it is open ground in a
// walkable line from the last, and otherwise moved a cell or three until it
// is; a point that cannot be had is left out. The loop has to close.
static void RoamOn(MapBuilder& m, const vector<string>& pool, int shown, int spread, float chance,
                   const vector<std::array<int, 2>>& cells, int cell = 32, int offset = 16,
                   bool narrow = false) {
    // On a narrow way -- a bridge between two islands -- the loop runs along
    // its middle with the monster's feet a little below the line, so that
    // the box they stand in straddles it, and nothing either side is asked of.
    const int side = narrow ? 0 : 6, lift = narrow ? 5 : 0;
    static const int kNudge[][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {-2, 0}, {0, 2}, {0, -2},
                                    {1, 1}, {-1, -1}, {1, -1}, {-1, 1}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}};
    vector<std::array<int, 2>> route;
    for (const auto& c : cells)
        for (const auto& d : kNudge) {
            const int x = (c[0] + d[0]) * cell + offset, y = (c[1] + d[1]) * cell + offset + lift;
            if (!m.Clear(x, y)) continue;
            if (!route.empty() && !m.Walkable(route.back()[0], route.back()[1], x, y, side)) continue;
            route.push_back({x, y});
            break;
        }
    while (route.size() >= 4 && !m.Walkable(route.back()[0], route.back()[1], route.front()[0], route.front()[1], side))
        route.pop_back();
    if (route.size() < 4) {
        std::fprintf(stderr, "genmaps: %s: no loop to roam on the cells given\n", m.Id().c_str());
        std::exit(1);
    }
    m.RoamingEnemy(pool, shown, spread, chance, route);
}

// --- what comes out at night, chosen ------------------------------------------------
// For every point of a lattice over the map that is open ground well away from
// anywhere safe, the first of `options` that keeps the self-test's rules there:
// nothing of its kind lives within twenty cells by day; it is stronger than the
// run of what does live within twenty-five, and by no more than twenty-four
// levels. Half kept on any night, never respawning, a pack's post each. As many
// as the map can hold without overdoing it -- a fifth of the day's posts abroad
// on a night, with half kept -- and never fewer than six.
struct NightOption { vector<string> pool; int level, spread; };
static void PlaceNightVisitors(MapBuilder& m, const vector<NightOption>& options, int step,
                               const std::function<bool(int, int)>& open_ground, int most = 14) {
    struct DayPost { string type; float x, y; int shown; };
    vector<DayPost> day;
    for (const auto& e : m.dq["enemies"]) {
        if (e.value("night", false)) continue;
        const string type = e.value("type", string(""));
        day.push_back({type, e["x"].get<float>(), e["y"].get<float>(),
                       BossType(type) ? -1 : ShownOf(type, e.value("level", 1))});
    }
    const int cap = std::min(most, static_cast<int>(day.size() * 0.4f));
    const int cols = m.Width() / 32, rows = m.Height() / 32;
    int placed = 0;
    for (int gy = step / 2 + 1; gy < rows - 2 && placed < cap; gy += step)
        for (int gx = step / 2 + 1; gx < cols - 2 && placed < cap; gx += step) {
            const int cx = gx + static_cast<int>(Hash2(gx, gy, 3031) * 5.0f) - 2;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, 3032) * 5.0f) - 2;
            if (cx < 2 || cy < 2 || cx >= cols - 2 || cy >= rows - 2 || !open_ground(cx, cy)) continue;
            const int x = cx * 32 + 16, y = cy * 32 + 16;
            if (!m.Clear(x, y) || !m.Clear(x - 12, y) || !m.Clear(x + 12, y) || m.NearestHaven(x, y) < 360.0f) continue;
            const float fx = static_cast<float>(x + m.ox), fy = static_cast<float>(y);
            float sum = 0.0f;
            int count = 0;
            for (const DayPost& d : day) {
                if (d.shown < 0) continue;
                if (std::hypot(d.x - fx, d.y - fy) < 800.0f) { sum += static_cast<float>(d.shown); ++count; }
            }
            for (const NightOption& o : options) {
                bool strangers = true;
                for (const DayPost& d : day)
                    if (d.shown >= 0 && std::hypot(d.x - fx, d.y - fy) < 640.0f &&
                        std::find(o.pool.begin(), o.pool.end(), d.type) != o.pool.end())
                        strangers = false;
                if (!strangers) continue;
                int low = 99, high = 0;
                for (const string& t : o.pool) {
                    low = std::min(low, ShownOf(t, o.level));
                    high = std::max(high, ShownOf(t, o.level + o.spread));
                }
                if (count > 0) {
                    const float usual = sum / count;
                    if (!(low > usual) || high > usual + 24.0f) continue;
                }
                m.NightEnemy(o.pool, "night_" + std::to_string(cx) + "_" + std::to_string(cy), x, y, o.level, o.spread,
                             0.5f);
                ++placed;
                break;
            }
        }
    if (placed < 6) {
        std::fprintf(stderr, "genmaps: %s: only %d places for night visitors (the self-test wants six)\n",
                     m.Id().c_str(), placed);
        std::exit(1);
    }
    std::printf("  %s by night: %d posts\n", m.Id().c_str(), placed);
}

// A loop for something that walks the map: `n` points round an ellipse about
// (cx, cy) with radii (rx, ry), each moved in toward the middle until it is on
// open ground and in a straight, walkable line from the one before -- a roamer
// walks straight, and a tree between two of its points is a tree it walks into
// all day. Points that cannot be had are left out; the loop has to close.
static vector<std::array<int, 2>> RoamRoute(const MapBuilder& m, int cx, int cy, int rx, int ry, int n) {
    vector<std::array<int, 2>> route;
    for (int i = 0; i < n; ++i) {
        const float a0 = 6.2831853f * i / n;
        bool placed = false;
        for (int s = 0; s < 14 && !placed; ++s) {
            const float scale = 1.0f - 0.05f * s;
            for (float jitter : {0.0f, 0.12f, -0.12f, 0.24f, -0.24f}) {
                const float a = a0 + jitter;
                const int x = cx + static_cast<int>(lroundf(cosf(a) * rx * scale));
                const int y = cy + static_cast<int>(lroundf(sinf(a) * ry * scale));
                if (!m.Clear(x, y)) continue;
                if (!route.empty() && !m.Walkable(route.back()[0], route.back()[1], x, y)) continue;
                route.push_back({x, y});
                placed = true;
                break;
            }
        }
    }
    // Closed: the last point walks back to the first.
    while (route.size() >= 4 && !m.Walkable(route.back()[0], route.back()[1], route.front()[0], route.front()[1]))
        route.pop_back();
    if (route.size() < 4) route.clear();
    return route;
}

// Where no ring will go -- a swamp that is more water than land, a camp of
// stakes in every clearing -- a way made of the map's own posts instead: from
// the one nearest (cx, cy), on to the nearest not yet walked to that can be
// walked to in a line, and so on, and then back the same way. Out and back
// always closes, and every point of it is ground something already stands on.
static vector<std::array<int, 2>> RoamChain(const MapBuilder& m, int cx, int cy, int points) {
    vector<std::array<int, 2>> spots;
    for (const auto& e : m.dq["enemies"]) {
        if (e.value("night", false) || e.contains("route") || e.value("lurk", false)) continue;
        const int x = e["x"].get<int>() - m.ox, y = e["y"].get<int>();
        if (m.Clear(x, y) && m.Clear(x - 8, y) && m.Clear(x + 8, y)) spots.push_back({x, y});
    }
    vector<std::array<int, 2>> chain;
    if (spots.empty()) return chain;
    size_t at = 0;
    for (size_t i = 1; i < spots.size(); ++i)
        if (std::hypot(spots[i][0] - cx, spots[i][1] - cy) < std::hypot(spots[at][0] - cx, spots[at][1] - cy)) at = i;
    vector<bool> used(spots.size(), false);
    chain.push_back(spots[at]);
    used[at] = true;
    while (static_cast<int>(chain.size()) < points) {
        int best = -1;
        float best_d = 900.0f;
        for (size_t i = 0; i < spots.size(); ++i) {
            if (used[i]) continue;
            const float d = std::hypot(static_cast<float>(spots[i][0] - chain.back()[0]),
                                       static_cast<float>(spots[i][1] - chain.back()[1]));
            if (d < 120.0f || d >= best_d) continue;
            if (!m.Walkable(chain.back()[0], chain.back()[1], spots[i][0], spots[i][1])) continue;
            best = static_cast<int>(i);
            best_d = d;
        }
        if (best < 0) break;
        used[best] = true;
        chain.push_back(spots[best]);
    }
    if (chain.size() < 3) return {};
    for (int i = static_cast<int>(chain.size()) - 2; i >= 1; --i) chain.push_back(chain[i]);
    return chain;
}

// What walks each map that has something walking it. The bosses come out at
// `shown` whatever their own strength, one of the pool a day, and each day
// somewhere else on the loop -- which is laid round the map at (cx, cy) with
// radii (rx, ry), all as fractions of its size.
struct Roamer {
    const char* map;
    vector<string> pool;
    int shown, spread;
    float chance;
    float cx, cy, rx, ry;
    int points;
};
static const vector<Roamer>& Roamers() {
    static const vector<Roamer> list = {
        // The waking world's high country: one of a pool of bosses a day,
        // somewhere different on the loop each day, some days none.
        {"bayou",            {"lizardman_chief", "broodmother", "den_mother", "well_warden"}, 52, 2, 0.6f,
         0.50f, 0.50f, 0.30f, 0.30f, 12},
        {"plateau_ascent",   {"orc3", "lizardman_chief", "den_mother", "broodmother"}, 58, 2, 0.5f,
         0.50f, 0.50f, 0.34f, 0.30f, 12},
        {"plateau_flats",    {"pit_lord", "vampire_lord", "wyvern_matriarch"}, 64, 2, 0.5f,
         0.50f, 0.50f, 0.34f, 0.30f, 12},
        {"plateau_terraces", {"bayou_matriarch", "well_warden", "nightmare_troll"}, 64, 2, 0.5f,
         0.50f, 0.50f, 0.19f, 0.20f, 12},
        // The Hexmire, a band on from the Bayou's (its temple's is round the
        // stockade: see BuildHexTemple).
        {"hex_drowns",       {"bayou_matriarch", "lizardman_chief", "well_warden", "den_mother"}, 58, 2, 0.5f,
         0.50f, 0.50f, 0.30f, 0.30f, 12},
        {"hex_strand",       {"wyvern_matriarch", "bayou_matriarch", "broodmother"}, 60, 2, 0.5f,
         0.36f, 0.62f, 0.18f, 0.22f, 12},
        {"hex_fens",         {"vampire_lord", "barrow_wight", "nightmare_troll"}, 62, 2, 0.5f,
         0.50f, 0.52f, 0.30f, 0.28f, 12},
        // The Frostreach: bosses of the waking world on the heath and at the
        // Howe. (The Abominable Snowman's walks are laid in BuildFrostGlacier
        // and BuildFrostMere.)
        {"frost_barrows",    {"barrow_wight", "vampire_lord", "den_mother"}, 66, 2, 0.5f,
         0.50f, 0.50f, 0.30f, 0.30f, 12},
        {"frost_howe",       {"pit_lord", "vampire_lord", "wyvern_matriarch", "nightmare_troll"}, 72, 2, 0.5f,
         0.50f, 0.64f, 0.32f, 0.20f, 12},
        // Havenbrook, dreaming: one walking the town every night, and on half
        // of them a second.
        {"dream_havenbrook", {"vampire_lord", "pit_lord", "den_mother", "lizardman_chief", "well_warden"}, 62, 2, 1.0f,
         0.46f, 0.52f, 0.34f, 0.30f, 14},
        {"dream_havenbrook", {"wyvern_matriarch", "bayou_matriarch", "nightmare_troll"}, 58, 2, 0.5f,
         0.50f, 0.50f, 0.24f, 0.22f, 12},
    };
    return list;
}

static void PlaceRoamers(MapBuilder& m) {
    for (const Roamer& r : Roamers()) {
        if (m.Id() != r.map) continue;
        const int w = m.Width(), h = m.Height();
        const int cx = static_cast<int>(w * r.cx) - m.ox, cy = static_cast<int>(h * r.cy);
        auto route = RoamRoute(m, cx, cy, static_cast<int>(w * r.rx), static_cast<int>(h * r.ry), r.points);
        if (route.empty()) route = RoamChain(m, cx, cy, r.points);
        if (route.size() < 4) {
            std::fprintf(stderr, "genmaps: no way to roam in %s about (%d, %d)\n", m.Id().c_str(), cx, cy);
            std::exit(1);
        }
        m.RoamingEnemy(r.pool, r.shown, r.spread, r.chance, route);
    }
}

static void PlaceCurios(MapBuilder& m) {
    for (const Curio& c : kCurios) {
        if (m.Id() != c.map) continue;
        int x = c.x, y = c.y;
        if (c.near[0]) {
            int seen = 0;
            bool found = false;
            for (const auto& e : m.dq["enemies"]) {
                if (e.value("night", false) || e.contains("route")) continue;
                bool kind = e.value("type", string()) == c.near;
                if (e.contains("pool"))
                    for (const auto& p : e["pool"]) kind |= p.get<string>() == c.near;
                if (!kind || seen++ != c.nth) continue;
                x = e["x"].get<int>() - m.ox + c.x;
                y = e["y"].get<int>() + c.y;
                found = true;
                break;
            }
            if (!found) {
                std::fprintf(stderr, "genmaps: %s has no %s post #%d for the %s\n", c.map, c.near, c.nth, c.item);
                std::exit(1);
            }
        }
        // Out from there in widening rings to the first open ground.
        int px = x, py = y;
        bool open = m.Clear(px, py);
        for (int r = 8; !open && r <= 192; r += 8)
            for (int a = 0; a < 24 && !open; ++a) {
                const float t = a * 6.2831853f / 24.0f;
                px = x + static_cast<int>(lroundf(cosf(t) * r));
                py = y + static_cast<int>(lroundf(sinf(t) * r));
                open = m.Clear(px, py);
            }
        if (!open) {
            std::fprintf(stderr, "genmaps: nowhere to put the %s in %s\n", c.item, c.map);
            std::exit(1);
        }
        json& o = m.Object(string("curio_") + c.item, "curio", px, py);
        o["sprite"]       = string("assets/icons/") + c.item + ".png";
        o["item"]         = c.item;
        o["item_qty"]     = 1;
        o["starts_quest"] = c.quest;
        o["title"]        = c.title;
    }
}

static void BuildOverworld() {
    MapBuilder m("overworld", "The Hollowmarch", (OW_W - OW_X0) * OW_CELL, OW_PX_H);
    m.ox = -OW_X0 * OW_CELL;
    m.Ambient("overworld");
    m.Subtitle("Open country between the foothills and the mire");
    m.Background(38, 52, 40);
    std::mt19937 rng(20260909u);

    // --- ground ---------------------------------------------------------------
    for (int cy = 0; cy < OW_H; ++cy) {
        for (int cx = OW_X0; cx < OW_W; ++cx) {
            const Biome b = BiomeAt(cx, cy);
            // Low frequency: broad drifts of colour rather than noise per cell.
            //
            // Plus a small per-cell jitter. The drift is smooth, so thresholding
            // it draws a clean contour -- and a clean contour on a 32px grid is
            // a staircase of squares, which is exactly what made the old map
            // look like coloured paper. Jittering the value by a fraction of
            // the gap between thresholds dissolves that edge into a scatter of
            // cells from both families, which is how the transition should
            // read anyway.
            unsigned hj = static_cast<unsigned>(cx) * 2654435761u
                        ^ static_cast<unsigned>(cy) * 40503u;
            hj ^= hj >> 15;
            const float jitter = (hj % 1000u) / 1000.0f - 0.5f;
            const float v = Fbm(cx * 0.085f, cy * 0.085f, 909) + jitter * 0.13f;

            string tile;
            switch (b) {
                case WATER:     tile = "water"; break;
                case ROAD:      tile = VariantOf("road", cx, cy); break;
                case TRAIL:     tile = (v > 0.55f) ? "dirt_dark" : "dirt"; break;
                case FOOTHILLS: tile = (v > 0.62f) ? "dirt_dark" : (v > 0.34f ? "dirt" : "sand"); break;
                // Sedge and peat and mud, and pools of bog water. It was laid
                // from a cut of the cursed-land pack that turned out to be
                // black cliff face, so a third of the swamp was a void.
                case MIRE:      tile = BogAt(cx, cy) ? "bog_water"
                                     : (v > 0.62f ? "peat" : (v > 0.34f ? "swamp_grass" : "swamp_mud")); break;
                case CURSED:    tile = (v > 0.5f) ? "cursed_ground" : "cursed_sand"; break;
                // Hollowrest: grass nothing grazes, worn to earth on the paths.
                case GRAVEYARD: tile = (v > 0.58f) ? "grave_earth" : "grave_grass"; break;
                case GREENWOOD: tile = (v > 0.55f) ? "grass_dark" : (v > 0.28f ? "grass" : "moss"); break;
                default:        tile = (v > 0.58f) ? "grass_olive" : (v > 0.3f ? "grass" : "grass_dark"); break;
            }
            // tools/make_ground.ps1 writes several variants of each family.
            // One tile repeated over four thousand pixels is a visible grid
            // however good the tile is, so which variant a cell gets comes
            // from a high-frequency hash -- neighbouring cells differ, and the
            // result is stable between runs.
            tile = VariantOf(tile, cx, cy);
            m.Ground(tile, cx * OW_CELL, cy * OW_CELL, OW_CELL);
        }
    }

    // --- elevation ------------------------------------------------------------
    {
        // The height grid is coarser than the tile grid on purpose. At one
        // level per 32px tile the terraces come out small and their edges
        // fragment into two- and three-tile bars, which reads as damage rather
        // than as landscape. At 64 the plateaus are broad and their edges run
        // far enough to be read as the edge of something.
        const int EL = 64;
        const int ecols = (OW_W - OW_X0) * OW_CELL / EL, erows = OW_PX_H / EL;
        const int per = EL / OW_CELL;          // tile cells per height cell

        vector<int> levels(static_cast<size_t>(ecols) * erows, 0);
        for (int ey = 0; ey < erows; ++ey)
            for (int ex = 0; ex < ecols; ++ex)
                levels[static_cast<size_t>(ey) * ecols + ex] =
                    ElevationAt(OW_X0 + ex * per + per / 2, ey * per + per / 2);

        // Ramps. Without these a raised map is a set of islands, so the rule
        // is simple and generous: the road is walkable end to end whatever it
        // climbs over, and so is a clearing around every place you can enter.
        vector<Rect4> ramps;
        for (int cy = 0; cy < OW_H; ++cy) {
            const float rx = RoadX(cy);
            const float x0 = (rx - 3.0f) * OW_CELL;
            ramps.push_back({x0, static_cast<float>(cy * OW_CELL),
                             6.0f * OW_CELL, static_cast<float>(OW_CELL)});
        }
        auto clearing = [&](int cx, int cy, int r) {
            ramps.push_back({static_cast<float>((cx - r) * OW_CELL),
                             static_cast<float>((cy - r) * OW_CELL),
                             static_cast<float>((2 * r + 1) * OW_CELL),
                             static_cast<float>((2 * r + 1) * OW_CELL)});
        };
        // The trail climbs over the greenwood shelves on its way east, so it
        // is walkable end to end in the same way the road is.
        for (int cx = static_cast<int>(RoadX(TRAIL_JUNCTION_CY)); cx < OW_W; ++cx)
            ramps.push_back({static_cast<float>(cx * OW_CELL),
                             (TrailY(cx) - 3.0f) * OW_CELL,
                             static_cast<float>(OW_CELL), 6.0f * OW_CELL});
        clearing(OW_W - 3, static_cast<int>(TrailY(OW_W - 3)), 3);   // the trail's end
        clearing(static_cast<int>(RoadX(88)), 88, 4);   // the town gate
        clearing(static_cast<int>(RoadX(10)), 9,  4);   // the mine
        clearing(12, 44, 4);                            // the barrow
        clearing(96, 78, 3);                            // meadow chest
        clearing(114, 52, 3);                           // wood chest
        clearing(10, 60, 3);                            // mire chest
        clearing(24, 2, 4);                             // the climb to the Ice Spire
        clearing(OW_W - 3, 50, 4);                      // the Ashen Path

        m.Elevation(EL, ecols, erows, levels, ramps);
    }

    // Water is impassable; walling it off per cell is cheap and exact.
    for (int cy = 0; cy < OW_H; ++cy)
        for (int cx = OW_X0; cx < OW_W; ++cx)
            if (BiomeAt(cx, cy) == WATER || BogAt(cx, cy))
                m.Collision(cx * OW_CELL, cy * OW_CELL, OW_CELL, OW_CELL);

    // --- ground decals --------------------------------------------------------
    // Grass tufts, wildflowers, fallen leaves, pebbles, stones, scorch cracks,
    // sedge and puddles (tools/make_decals.ps1), each on the ground it would lie
    // on. These were once shapes cut from the CraftPix road pack's Ground_grass
    // sheet -- blobs, squares with holes, chevrons -- which are that sheet's
    // stencils for blending grass into a path, and read on a field as stains.
    {
        std::map<string, vector<string>> pools;
        auto pool = [&](const string& prefix) -> const vector<string>& {
            auto it = pools.find(prefix);
            if (it != pools.end()) return it->second;
            vector<string> names;
            for (int i = 0; i < 16 && g_manifest.Has("decor/" + prefix + "_" + std::to_string(i)); ++i)
                names.push_back(prefix + "_" + std::to_string(i));
            return pools[prefix] = names;
        };
        struct Mix { const char* prefix; float upto; };
        static const Mix meadow[]    = {{"tuft", 0.55f}, {"flowers", 0.80f}, {"pebbles", 1.0f}};
        static const Mix greenwood[] = {{"tuft_dark", 0.45f}, {"leaves", 0.80f}, {"flowers", 0.90f}, {"pebbles", 1.0f}};
        static const Mix foothills[] = {{"dry_tuft", 0.35f}, {"pebbles", 0.75f}, {"stone", 1.0f}};
        static const Mix cursed[]    = {{"crack", 0.60f}, {"pebbles", 1.0f}};
        static const Mix mire[]      = {{"sedge", 0.70f}, {"puddle", 1.0f}};
        static const Mix boneyard[]  = {{"dry_tuft", 0.45f}, {"pebbles", 0.80f}, {"stone", 1.0f}};

        for (int cy = 1; cy < OW_H - 1; ++cy) {
            for (int cx = OW_X0 + 1; cx < OW_W - 1; ++cx) {
                const Biome b = BiomeAt(cx, cy);
                if (b == WATER || b == ROAD || b == TRAIL || BogAt(cx, cy)) continue;
                const float density = b == GREENWOOD ? 0.15f : (b == MIRE ? 0.09f : 0.12f);
                if (Hash2(cx, cy, 5150) > density) continue;

                const Mix* mix = meadow;
                size_t kinds = 3;
                if (b == GREENWOOD)      { mix = greenwood; kinds = 4; }
                else if (b == FOOTHILLS) { mix = foothills; kinds = 3; }
                else if (b == CURSED)    { mix = cursed;    kinds = 2; }
                else if (b == MIRE)      { mix = mire;      kinds = 2; }
                else if (b == GRAVEYARD)  { mix = boneyard;  kinds = 3; }
                const float which = Hash2(cx, cy, 6161);
                size_t k = 0;
                while (k + 1 < kinds && which > mix[k].upto) ++k;

                const vector<string>& names = pool(mix[k].prefix);
                if (names.empty()) continue;
                const string& name = names[static_cast<size_t>(Hash2(cx, cy, 6262) * 1000.0f) % names.size()];
                const int jx = static_cast<int>(Hash2(cx, cy, 6363) * 16.0f) - 8;
                const int jy = static_cast<int>(Hash2(cx, cy, 6464) * 16.0f) - 8;
                // An overlay, so it is drawn over the ground tiles whatever
                // its name sorts against.
                m.Overlay("decor", name, cx * OW_CELL + 16 + jx, cy * OW_CELL + 16 + jy);
            }
        }
    }

    // --- scenery and gathering nodes -----------------------------------------
    int tree_index = 0, rock_index = 0;

    for (int cy = 2; cy < OW_H - 2; ++cy) {
        for (int cx = OW_X0 + 2; cx < OW_W - 2; ++cx) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER || b == ROAD || b == TRAIL) continue;
            // Keep a clear verge either side of the road.
            if (fabsf(cx - RoadX(cy)) < 3.2f) continue;
            // And the hillside the mine is cut into.
            if (fabsf(cx - RoadX(10)) < 7.0f && cy < 14) continue;
            // And either side of the Whisperwood Trail, so it reads as a path
            // cut through the trees rather than a stripe of dirt under them.
            if (OnTrail(cx, cy, 3.4f)) continue;
            // And round the two gated ways out, north and east.
            if ((cx >= 20 && cx <= 28 && cy <= 6) || (cx >= OW_W - 8 && cy >= 45 && cy <= 55)) continue;
            // And the barrow mound and the flagstones up to it.
            if (abs(cx - 12) <= 4 && cy >= 40 && cy <= 47) continue;
            // And inside Hollowrest, which is laid out by hand below.
            if (GraveField(cx, cy) < 2.2f) continue;

            const float r = Hash2(cx, cy, 4242);
            const int x = cx * OW_CELL + OW_CELL / 2;
            const int y = cy * OW_CELL + OW_CELL / 2;

            if (b == GREENWOOD) {
                if (r < 0.055f)      PlaceTree(m, rng, tree_index++, x, y, true, 1, "logs");
                else if (r < 0.085f) PlaceTree(m, rng, tree_index++, x, y, false, 1, "logs");
                else if (r < 0.115f) m.Prop("objects", Pick(kBushes, rng), x, y);
                else if (r < 0.135f) m.Prop("objects", Pick(kFungus, rng), x, y);
            } else if (b == MEADOW) {
                if (r < 0.016f)      PlaceTree(m, rng, tree_index++, x, y, true, 1, "logs");
                else if (r < 0.036f) m.Prop("objects", Pick(kSmallBushes, rng), x, y);
                else if (r < 0.05f)  m.Prop("objects", Pick(kSmallRocks, rng), x, y);
            } else if (b == FOOTHILLS) {
                if (r < 0.03f)       PlaceRock(m, rng, rock_index++, x, y, true, 1, "copper_ore");
                else if (r < 0.05f)  PlaceRock(m, rng, rock_index++, x, y, false, 1, "copper_ore");
                else if (r < 0.062f) m.Prop("objects", Pick(kSmallRocks, rng), x, y);
                // Higher up the foothills, the tier after iron: coal for steel,
                // and the first blue of azuryte.
                else if (r < 0.070f && ElevationAt(cx, cy) >= 1) PlaceRock(m, rng, rock_index++, x, y, true, 20, "coal");
                else if (r < 0.075f && ElevationAt(cx, cy) >= 2) PlaceRock(m, rng, rock_index++, x, y, true, 30, "azuryte_ore");
            } else if (b == MIRE) {
                const bool bog = BogAt(cx, cy);
                const bool shore = !bog && (BogAt(cx + 1, cy) || BogAt(cx - 1, cy) || BogAt(cx, cy + 1) || BogAt(cx, cy - 1));
                if (fabsf(cx - MIRE_CAMP_X) <= 6 && fabsf(cy - MIRE_CAMP_Y) <= 6) continue;
                if (bog) {
                    if (r < 0.14f) m.Prop("props", "lily_pads", x, y + 8);
                } else if (shore && Hash2(cx, cy, 4343) < 0.40f) {
                    m.Prop("props", "reeds", x, y + 4);
                } else if (r < 0.024f) {
                    m.Prop("props", "swamp_tree", x, y);
                    m.Collision(x - 8, y - 8, 16, 8);
                } else if (r < 0.042f) {
                    m.Prop("objects", Pick(kFungus, rng), x, y);
                } else if (r < 0.054f) {
                    PlaceRock(m, rng, rock_index++, x, y, false, 5, "iron_ore");
                }
            } else if (b == CURSED) {
                if (r < 0.04f)       m.Prop("objects", Pick(kSmallRocks, rng), x, y);
                else if (r < 0.055f) PlaceRock(m, rng, rock_index++, x, y, true, 10, "iron_ore");
                else if (r < 0.061f) PlaceRock(m, rng, rock_index++, x, y, true, 20, "coal");
            }
        }
    }

    // --- herbs ----------------------------------------------------------------
    // Every plant has the ground it likes, and is scattered thinly over it:
    // marigolds in the meadow, nettles under the greenwood, bogbean in the
    // Mire, sage in the foothills (more of it the higher up), emberbloom on the
    // burnt ground of the Cursed Reach, and brookmint wherever the land meets
    // water. Then each has one patch where it grows thick -- the place a
    // forager learns to go back to.
    {
        int herb_i = 0;
        auto scenery_here = [&](int cx, int cy) { return Hash2(cx, cy, 4242) < 0.14f; };
        auto open_ground = [&](int cx, int cy) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER || b == ROAD || b == TRAIL || BogAt(cx, cy)) return false;
            if (fabsf(cx - MIRE_CAMP_X) <= 6 && fabsf(cy - MIRE_CAMP_Y) <= 6) return false;
            if ((cx >= 20 && cx <= 28 && cy <= 6) || (cx >= OW_W - 8 && cy >= 45 && cy <= 55)) return false;
            if (fabsf(cx - RoadX(10)) < 7.0f && cy < 14) return false;   // the mine
            if (abs(cx - 12) <= 4 && cy >= 40 && cy <= 47) return false;   // the barrow
            if (GraveField(cx, cy) < 2.2f) return false;                   // Hollowrest
            if (fabsf(cx - RoadX(cy)) < 3.2f || OnTrail(cx, cy, 3.4f)) return false;
            return !scenery_here(cx, cy);
        };
        auto near_water = [&](int cx, int cy) {
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (BiomeAt(cx + dx, cy + dy) == WATER) return true;
            return false;
        };
        auto at = [&](int cx, int cy, int seed) {
            return std::pair<int, int>{cx * OW_CELL + 8 + static_cast<int>(Hash2(cx, cy, seed) * 16.0f),
                                       cy * OW_CELL + 12 + static_cast<int>(Hash2(cx, cy, seed + 1) * 12.0f)};
        };
        for (int cy = 3; cy < OW_H - 3; ++cy)
            for (int cx = OW_X0 + 3; cx < OW_W - 3; ++cx) {
                if (!open_ground(cx, cy)) continue;
                const Biome b = BiomeAt(cx, cy);
                const float r = Hash2(cx, cy, 7373);
                string herb;
                if (r < 0.05f && near_water(cx, cy))                      herb = "brookmint";
                else if (b == MEADOW && r < 0.010f)                       herb = "marigold";
                else if (b == GREENWOOD && r < 0.010f)                    herb = "nettle";
                else if (b == GREENWOOD && r < 0.013f)                    herb = "marigold";
                else if (b == MIRE && r < 0.014f)                         herb = "bogbean";
                else if (b == FOOTHILLS && r < (ElevationAt(cx, cy) >= 1 ? 0.016f : 0.007f)) herb = "mountain_sage";
                else if (b == CURSED && r < 0.016f)                       herb = "emberbloom";
                if (herb.empty()) continue;
                const auto [x, y] = at(cx, cy, 7474);
                PlaceHerb(m, herb, x, y, herb_i);
            }

        // The patches. Each looks outward from a rough spot for somewhere its
        // whole round is the right ground, so the patch never spills onto the
        // road or into the next biome.
        struct Patch { const char* herb; Biome biome; int cx, cy; bool water; };
        const Patch patches[] = {
            {"marigold",      MEADOW,    60, 78, false},
            {"nettle",        GREENWOOD, 108, 66, false},
            {"bogbean",       MIRE,      10, 30, false},
            {"mountain_sage", FOOTHILLS, 40, 8,  false},
            {"emberbloom",    CURSED,    114, 14, false},
            {"brookmint",     MEADOW,    30, 62, true},
        };
        for (const Patch& pt : patches) {
            bool found = false;
            for (int ring = 0; ring < 30 && !found; ++ring)
                for (int dy = -ring; dy <= ring && !found; ++dy)
                    for (int dx = -ring; dx <= ring && !found; ++dx) {
                        if (std::max(abs(dx), abs(dy)) != ring) continue;
                        const int ccx = pt.cx + dx, ccy = pt.cy + dy;
                        if (ccx < OW_X0 + 6 || ccy < 6 || ccx >= OW_W - 6 || ccy >= OW_H - 6) continue;
                        bool fits = true;
                        int water = 0;
                        for (int yy = -3; yy <= 3 && fits; ++yy)
                            for (int xx = -3; xx <= 3 && fits; ++xx) {
                                const Biome b = BiomeAt(ccx + xx, ccy + yy);
                                if (b == WATER) { ++water; continue; }
                                if (b == ROAD || b == TRAIL || fabsf(ccx + xx - RoadX(ccy + yy)) < 3.2f) fits = false;
                                else if (!pt.water && b != pt.biome) fits = false;
                            }
                        if (pt.water ? (water < 6 || water > 20) : water > 0) fits = false;
                        if (!fits) continue;
                        found = true;
                        for (int yy = -3; yy <= 3; ++yy)
                            for (int xx = -3; xx <= 3; ++xx) {
                                const int hx = ccx + xx, hy = ccy + yy;
                                if (xx * xx + yy * yy > 10 || !open_ground(hx, hy)) continue;
                                if (pt.water && !near_water(hx, hy)) continue;
                                if (Hash2(hx, hy, 9393) > 0.45f) continue;
                                const auto [x, y] = at(hx, hy, 7575);
                                PlaceHerb(m, pt.herb, x, y, herb_i);
                            }
                    }
        }
    }

    // --- fishing -------------------------------------------------------------
    // Along the lake's east shore, a spot every few rows where dry land
    // meets the water: close enough to the edge to reach from dry land.
    {
        int placed = 0, last_cy = -100;
        for (int cy = 10; cy < OW_H - 10 && placed < 4; ++cy) {
            if (cy - last_cy < 3) continue;
            for (int cx = OW_X0 + 2; cx < OW_W - 3; ++cx) {
                if (BiomeAt(cx, cy) != WATER || BiomeAt(cx + 1, cy) == WATER) continue;
                const Biome bank = BiomeAt(cx + 1, cy);
                if (bank == ROAD || bank == TRAIL) break;
                if (ElevationAt(cx + 1, cy) != 0) break;
                PlaceFishingSpot(m, "fish_lake_" + std::to_string(placed),
                                 cx * OW_CELL + OW_CELL - 8, cy * OW_CELL + OW_CELL / 2, "lake",
                                 {"raw_minnow", "raw_pike", "raw_eel"}, 1);
                ++placed;
                last_cy = cy;
                break;
            }
        }
    }

    // --- wildlife and orcs ----------------------------------------------------
    int spawned = 0;
    for (int cy = 4; cy < OW_H - 4; cy += 3) {
        // On the same lattice as before the map grew, carried on west.
        for (int cx = 4 - 3 * 6; cx < OW_W - 4; cx += 3) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER || BogAt(cx, cy)) continue;
            if (abs(cx - 12) <= 4 && cy >= 40 && cy <= 47) continue;   // the barrow mound
            if (GraveField(cx, cy) < 2.6f) continue;                   // Hollowrest
            // Not in the trunk of a tree or the side of a rock.
            if (!m.Clear(cx * OW_CELL + 16, cy * OW_CELL + 16)) continue;

            const float r = Hash2(cx, cy, 8888);
            const int x = cx * OW_CELL + 16;
            const int y = cy * OW_CELL + 16;
            const float road_gap = fabsf(cx - RoadX(cy));

            if (b == MEADOW) {
                // Small game first: the meadow is where a new character learns
                // to fight, so it is mostly hares and deer with the occasional
                // boar rather than a field of them.
                if (r < 0.026f)      { m.Enemy("boar", x, y, 1); ++spawned; }
                else if (r < 0.085f) { m.Enemy("hare", x, y, 1); ++spawned; }
                else if (r < 0.115f) { m.Enemy("deer", x, y, 1); ++spawned; }
            } else if (b == GREENWOOD) {
                if (r < 0.045f)      { m.Enemy("deer", x, y, 2); ++spawned; }
                else if (r < 0.075f) { m.Enemy("fox", x, y, 2); ++spawned; }
                else if (r < 0.10f)  { m.Enemy("boar", x, y, 3); ++spawned; }
            } else if (b == FOOTHILLS) {
                if (r < 0.06f)       { m.EnemyPool({"orc1", "orc1", "orc_slinger"}, "", x, y, 2, 0); ++spawned; }
                else if (r < 0.08f)  { m.EnemyPool({"orc2", "orc2", "orc_bowman"}, "", x, y, 4, 0); ++spawned; }
            } else if (b == MIRE) {
                // The swamp belongs to the lizardmen now.
                if (fabsf(cx - MIRE_CAMP_X) <= 7 && fabsf(cy - MIRE_CAMP_Y) <= 7) continue;
                // And to the frogs, which were always there in the noise and
                // are something to see now: they sit by the water, they do not
                // fight, and what they leave is the best thing anybody has ever
                // eaten out of a bog.
                // The bands below the frogs are the bands that were always
                // here, moved up by the width of the frogs' one: the lizardmen
                // keep their 0.055 of the noise and the orcs their 0.010, so
                // the swamp has what it had and frogs besides.
                if (r < 0.045f)      { m.Enemy("frog", x, y, 1, 60.0f, 80.0f); ++spawned; }
                else if (r < 0.100f) { m.Enemy("lizardman", x, y, 1 + static_cast<int>(Hash2(cx, cy, 99) * 3)); ++spawned; }
                else if (r < 0.110f) { m.EnemyPool({"orc1", "orc1", "orc_slinger"}, "", x, y, 5, 0); ++spawned; }
            } else if (b == CURSED) {
                if (r < 0.09f)       { m.EnemyPool({"orc2", "orc2", "orc_bowman"}, "", x, y, 8, 0); ++spawned; }
            }

            // The Sunken Road is where the orc contract is actually filled --
            // and, under the trees along it, where the highwaymen wait.
            if (b != WATER && road_gap > 2.0f && road_gap < 6.0f && cy > 24 && cy < 74) {
                if (r > 0.90f) { m.EnemyPool({"orc1", "orc1", "orc_slinger"}, "", x, y, 3, 0, 24.0f, 200.0f); ++spawned; }
                else if (r > 0.84f && b == GREENWOOD) { m.Enemy("highwayman", x, y, 3, 40.0f, 200.0f); ++spawned; }
            }
        }
    }
    (void)spawned;

    // --- landmarks ------------------------------------------------------------
    const int gate_x = static_cast<int>(RoadX(86)) * OW_CELL + 16;
    const int gate_y = 88 * OW_CELL;
    m.Spawn("start", gate_x, gate_y - 40);
    // Named, so loading the map with no spawn lands at the town gate rather
    // than at whichever arrival sorts first alphabetically.
    m.Spawn("default", gate_x, gate_y - 40);
    m.Spawn("from_town", gate_x, gate_y - 44);

    // Havenbrook's gate, standing where the Sunken Road ends. Before this the
    // road simply stopped in a field and the way in was a rectangle of grass:
    // nothing told you that you had arrived anywhere. The palisade runs a few
    // lengths either side and then gives out, the way a village's does -- it
    // is a gate, not a wall around the world.
    PlaceFrontGate(m, gate_x, gate_y + 46, 4);
    m.Portal(gate_x - 36, gate_y + 6, 72, 44, "town_havenbrook", "from_field",
             "Enter Havenbrook", false);
    MarkWorld("town", "Havenbrook", gate_x, gate_y + 16, "havenbrook");
    // The Westwold is out of the town's west gate, not off this map's edge, so
    // it is marked where that gate would be.
    MarkWorld("path", "The Westwold, by Havenbrook's west gate  (Combat 5)", gate_x - 210, gate_y + 96);
    // The well is inside the town, so its mark sits just under the town's.
    MarkWorld("dungeon", "The Dry Well, in Havenbrook", gate_x - 26, gate_y + 92);

    {
        // Clear of the gate's right-hand tower, on the verge where the road
        // still is: it used to stand where the gatehouse now stands.
        json& o = m.Object("sign_gate", "sign", gate_x + 78, gate_y - 74);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Waymarker";
        o["text"]   = "HAVENBROOK, south through the gate.\nEMBERFELL MINE, north along the Sunken Road.\n\nBelow, scratched later and deeper:\nthe road is not safe after the second milestone.";
        m.Collision(gate_x + 62, gate_y - 82, 32, 12);
    }

    // Mine entrance in the northern foothills: the Emberfell adit, a timber
    // mouth cut into a shoulder of rock (tools/blender_props.py,
    // prop_mine_adit). It used to be a door sprite standing on open dirt.
    //
    // The art is 256px, drawn standing on its bottom edge. Measured off it:
    // the tunnel mouth is 42px wide at the centre, its floor 57px above the
    // bottom, and the rails run out of it to the bottom edge. The rock fills
    // the rest from 118px left of centre to 122px right, and up 225px.
    const int mine_x = static_cast<int>(RoadX(10)) * OW_CELL + 16;
    const int mine_y = 9 * OW_CELL;
    {
        const int base = mine_y + 67;               // where the art stands
        const int mouth_floor = base - 57;          // = mine_y + 10
        m.Prop("props", "mine_adit", mine_x, base);
        // Sorted at the back of the tunnel, where the rock behind the mouth
        // stops the player: standing in the doorway draws them in front of
        // the hill, not ghosting the whole hillside out behind them.
        m.SortLift("mine_adit", base - (mine_y - 36));
        m.Spawn("from_mine", mine_x, mine_y + 56);
        // The portal is the dark of the tunnel itself.
        m.Portal(mine_x - 18, mouth_floor - 34, 36, 30, "dungeon_emberfell_1", "entrance",
                 "Enter the Emberfell mine");
        MarkWorld("dungeon", "Emberfell Mine", mine_x, mine_y);
        m.Danger(10);      // the mine: orcs at Combat 8-19 past the first room
        // Rock either side of the mouth and behind it, so the only way in is
        // up the rails.
        m.Collision(mine_x - 118, mine_y - 150, 96, 162);
        m.Collision(mine_x + 22,  mine_y - 150, 100, 162);
        m.Collision(mine_x - 22,  mine_y - 150, 44, 116);
        // The boulders at the foot of the slope, the ore cart and the tailings.
        m.Collision(mine_x - 118, mouth_floor + 2, 56, 22);
        m.Collision(mine_x + 64,  mouth_floor + 2, 58, 22);
        m.Collision(mine_x - 58,  mouth_floor + 12, 32, 16);
        m.Collision(mine_x + 38,  mouth_floor + 18, 30, 12);
        // Loose rock where the hill runs out into the foothills either side,
        // so it grows out of the slope rather than standing on it.
        const int flank[][3] = {{-150, 4, 1}, {-176, -44, 0}, {154, 0, 1}, {182, -50, 0}, {-138, -112, 0}, {142, -118, 1}};
        for (const auto& f : flank) {
            const int fx = mine_x + f[0], fy = mine_y + f[1];
            m.Prop("objects", f[2] ? kRocks[(fx + fy) % 8] : kSmallRocks[(fx + fy) % 4], fx, fy);
            m.Collision(fx - (f[2] ? 18 : 10), fy - 10, f[2] ? 36 : 20, 10);
        }
    }

    // The barrow, out in the Mire: a long grave mound grown over with turf, a
    // dark passage framed by standing stones and a capstone cut into its
    // front, and flagstones up to it (tools/blender_props.py,
    // prop_barrow_mound). It used to be a door sprite on the grass.
    //
    // The art is 208px, standing on its bottom edge. Measured off it: the
    // passage is 40px wide at the centre with its floor 42px above the bottom,
    // the flagstones run from there to the bottom edge, and the mound fills
    // 100px either side of centre from 23px up to 166px.
    const int barrow_x = 12 * OW_CELL + 16;
    const int barrow_y = 44 * OW_CELL;
    {
        const int base = barrow_y + 70;
        const int door_floor = base - 42;
        m.Prop("props", "barrow_mound", barrow_x, base);
        // Sorted at the back of the passage, so someone in the doorway draws
        // in front of the mound rather than vanishing behind it.
        m.SortLift("barrow_mound", 76);
        m.Spawn("from_barrow", barrow_x, base + 18);
        m.Portal(barrow_x - 18, door_floor - 28, 36, 30, "dungeon_barrow", "entrance",
                 "Enter the barrow");
        MarkWorld("dungeon", "The Barrow", barrow_x, barrow_y);
        m.Danger(20);      // the barrow: its dead are Combat 11-22
        // The mound either side of the door and behind it.
        m.Collision(barrow_x - 100, base - 150, 78, 128);
        m.Collision(barrow_x + 24,  base - 150, 76, 128);
        m.Collision(barrow_x - 22,  base - 150, 46, 74);
        // The skull on its stake.
        m.Collision(barrow_x + 34, base - 34, 12, 8);
    }

    // The note that starts the barrow quest, left where someone turned back.
    {
        json& o = m.Object("note_mire", "note", barrow_x + 72, barrow_y + 96);
        o["title"]        = "A water-stained note";
        o["text"]         = "If you are reading this I did not come back out, and you should not go in.\n\nI am going anyway. There is a seal down there with my grandmother's name pressed into it, and the guild has known about it for forty years.\n\nIf you find the seal, do not give it to Orlend. Ask him why he never told anyone first.";
        o["starts_quest"] = "q_barrow_seal";
        // A page on the ground, drawn in tools/icons.txt; it used to be a rock.
        o["sprite"]       = "assets/icons/note_ground.png";
    }

    // The surveyor's page, halfway up the Sunken Road.
    {
        const int px = static_cast<int>(RoadX(48)) * OW_CELL + 76;
        const int py = 48 * OW_CELL + 16;
        json& o = m.Object("note_surveyor", "note", px, py);
        o["title"]  = "Torn survey page";
        o["text"]   = "Third day on the road. Counted twelve of them at the second milestone. Counted thirty at the third.\n\nThey are not raiding. They are walking north, in order, and they are all walking to the same place.\n\nI have drawn it below as best I can from the ridge. It is a mine adit. It is the Emberfell adit.";
        o["loot"]   = "page_surveyor";
        o["sprite"] = "assets/icons/note_ground.png";
    }

    // The Whisperwood trailhead: a signpost where the trail leaves the road,
    // a woodpile beside it, and the way east at the edge of the map.
    {
        const int sx = static_cast<int>(RoadX(TRAIL_JUNCTION_CY) + 5.0f) * OW_CELL + 16;
        const int sy = static_cast<int>(TrailY(static_cast<int>(RoadX(TRAIL_JUNCTION_CY) + 5.0f)) - 2.4f)
                       * OW_CELL + 16;
        MarkWorld("landmark", "Trailhead", sx, sy);
        json& o = m.Object("sign_trailhead", "sign", sx, sy);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Trailhead";
        o["starts_quest"] = "q_road_beneath_leaves";
        o["text"]   = "WHISPERWOOD TRAIL, east.\n"
                      "MOSSVALE, a day's walk under the trees.\n"
                      "FERNHOLLOW, where the trail forks north.\n\n"
                      "Carved smaller: keep to the path after dark.\n\n"
                      "Follow the road east to Mossvale, then return to the fork "
                      "and take the north path to Fernhollow.";
        m.Collision(sx - 16, sy - 10, 32, 10);
        m.Prop("props", "log_pile", sx + 56, sy + 4);
        m.Collision(sx + 56 - 16, sy - 6, 32, 10);

        const int ey = static_cast<int>(TrailY(OW_W - 1) * OW_CELL) + 16;
        m.Portal(OW_PX_W - 24, ey - 72, 24, 144, "whisperwood_trail", "from_hollowmarch",
                 "To the Whisperwood", false);
        MarkWorld("path", "Whisperwood Trail", OW_PX_W - 40, ey);
        m.Spawn("from_whisperwood", OW_PX_W - 96, static_cast<int>(TrailY(OW_W - 4) * OW_CELL) + 16);

        // Highwaymen along the trail east of the road, in twos under the
        // trees, the way they stand on the Whisperwood side of it.
        {
            int n = 0;
            for (int cx = static_cast<int>(RoadX(TRAIL_JUNCTION_CY) + 12.0f); cx < OW_W - 5; cx += 17)
                for (int side : {-1, 1}) {
                    const int tx = cx + (side < 0 ? 0 : 2);
                    const int ty = static_cast<int>(TrailY(static_cast<float>(tx)) + side * 2.4f);
                    if (BiomeAt(tx, ty) == WATER || !m.Clear(tx * OW_CELL + 16, ty * OW_CELL + 16)) continue;
                    m.Enemy("highwayman", tx * OW_CELL + 16, ty * OW_CELL + 16, 2 + (n++ % 2), 40.0f, 200.0f);
                }
        }
    }

    // --- the lizardmen's camp ------------------------------------------------------
    // Three huts up on stilts round a fire, painted totems, and their chief.
    {
        const int cx0 = MIRE_CAMP_X * OW_CELL + 16, cy0 = MIRE_CAMP_Y * OW_CELL + 16;
        for (const auto& h : {std::pair<int, int>{-112, -30}, {100, -48}, {-8, -118}}) {
            m.Prop("props", "lizard_hut", cx0 + h.first, cy0 + h.second);
            m.Collision(cx0 + h.first - 42, cy0 + h.second - 26, 84, 26);
        }
        for (const auto& t : {std::pair<int, int>{-52, 52}, {58, 40}, {140, 30}}) {
            m.Prop("props", "lizard_totem", cx0 + t.first, cy0 + t.second);
            m.Collision(cx0 + t.first - 7, cy0 + t.second - 7, 14, 7);
        }
        json& fire = m.Object("range_lizard_camp", "range", cx0 + 6, cy0 + 8);
        fire["sprite"] = "assets/props/campfire_ring.png";
        fire["title"]  = "Camp fire";
        m.Collision(cx0 + 6 - 16, cy0 - 2, 32, 10);
        MarkWorld("camp", "Lizardmen's Camp", cx0, cy0);
        m.Enemy("lizardman_chief", cx0 - 10, cy0 - 56, 3, 120.0f, 260.0f);
        const int guards[][3] = {{-80, 20, 2}, {70, 10, 3}, {-30, 90, 2}, {110, 90, 4}, {-140, 60, 3}};
        for (const auto& g : guards) m.Enemy("lizardman", cx0 + g[0], cy0 + g[1], g[2], 40.0f, 240.0f);
    }

    // --- the way into the Bayou ----------------------------------------------------------
    // West off the edge of the map along the causeway from the camp: the deep
    // swamp the lizardmen came out of. A post at the edge says what it is.
    {
        const int ey = 80 * OW_CELL + 16;
        m.Portal(OW_X0 * OW_CELL, ey - 72, 24, 144, "bayou", "from_hollowmarch", "To the Bayou", false);
        m.Danger(30);      // the Bayou: Combat 30 at the edge, 56 at the bottom of it
        m.Spawn("from_bayou", OW_X0 * OW_CELL + 88, ey);
        MarkWorld("path", "The Bayou", OW_X0 * OW_CELL + 40, ey);
        json& sign = m.Object("sign_bayou", "sign", OW_X0 * OW_CELL + 220, ey - 62);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "The Bayou";
        sign["text"]   = "WEST: THE BAYOU\n\nPainted on the post in lizardman red, and scratched under it "
                         "by somebody who could write: the water there is not empty. Do not walk the edge.";
        m.Collision(OW_X0 * OW_CELL + 204, ey - 72, 32, 10);
    }

    // --- Hollowrest ------------------------------------------------------------------
    // The burying ground: an iron fence round a field of dead grass, a lych
    // gate at the north with a lantern still lit under it, ranks of headstones
    // and dug graves, drowned trees left where they stood, and the crypt at the
    // south end with the family's wight still in it. The dead walk here: the
    // shamblers among the graves, skeletons along the fence, and wraiths where
    // the old part of the yard has sunk.
    {
        const auto at = [&](int cx, int cy, int ox = 16, int oy = 16) {
            return std::pair<int, int>{cx * OW_CELL + ox, cy * OW_CELL + oy};
        };
        // The fence: one length of railing per cell along the edge of the
        // ground, minus the gaps the gates stand in.
        for (int cy = GRAVE_CY - 14; cy <= GRAVE_CY + 14; ++cy)
            for (int cx = GRAVE_CX - 22; cx <= GRAVE_CX + 22; ++cx) {
                if (!InGraveyard(cx, cy) || GraveGate(cx, cy)) continue;
                const bool edge = !InGraveyard(cx + 1, cy) || !InGraveyard(cx - 1, cy) ||
                                  !InGraveyard(cx, cy + 1) || !InGraveyard(cx, cy - 1);
                if (!edge) continue;
                const auto [x, y] = at(cx, cy, 16, 26);
                m.Prop("props", "grave_fence", x, y);
                m.Collision(x - 16, y - 10, 32, 12);
            }

        // The gate, and the path in from it.
        {
            const auto [gx, gy] = at(GRAVE_CX, GRAVE_CY - 12, 16, 20);
            m.Prop("props", "lych_gate", gx, gy);
            m.SortLift("lych_gate", 60);
            // The piers, not the gateway: you walk through the middle.
            m.Collision(gx - 60, gy - 22, 34, 22);
            m.Collision(gx + 26, gy - 22, 34, 22);
            MarkWorld("grave", "Hollowrest", gx, gy + 40);
            // A ground mist over the burying ground, and only there.
            m.Fog(0.34f, 0.0f, {196, 204, 208},
                  {static_cast<float>((GRAVE_CX - GRAVE_RX - 1) * OW_CELL), static_cast<float>((GRAVE_CY - GRAVE_RY - 1) * OW_CELL),
                   (GRAVE_RX * 2 + 2) * OW_CELL, (GRAVE_RY * 2 + 2) * OW_CELL});
            json& sign = m.Object("sign_hollowrest", "sign", gx + 84, gy - 4);
            sign["sprite"] = "assets/props/signpost.png";
            sign["title"]  = "Hollowrest";
            sign["text"]   = "HOLLOWREST BURYING GROUND\n\n"
                             "Havenbrook's dead, and Emberfell's, and whoever the road left.\n\n"
                             "Under it, cut later and deeper: THEY DO NOT STAY DOWN. "
                             "SHUT THE GATE.";
            m.Collision(gx + 68, gy - 14, 32, 10);
        }

        // Headstones in ranks, with the odd cross and dug grave among them, and
        // a drowned tree left standing where the yard grew round it.
        // Markers, thinner on the ground than they were: a field reads as a
        // field because there is grass between the stones.
        int graves = 0;
        for (int cy = GRAVE_CY - 13; cy <= GRAVE_CY + 13; ++cy)
            for (int cx = GRAVE_CX - 21; cx <= GRAVE_CX + 21; ++cx) {
                if (!InGraveyard(cx, cy) || GraveAisle(cx, cy)) continue;
                const float r = Hash2(cx, cy, 5511);
                const auto [x, y] = at(cx, cy, 8 + static_cast<int>(Hash2(cx, cy, 5512) * 16.0f),
                                       10 + static_cast<int>(Hash2(cx, cy, 5513) * 14.0f));
                if (r < 0.17f) {
                    m.Prop("props", "gravestone", x, y);
                    m.Collision(x - 10, y - 7, 20, 7);
                    ++graves;
                } else if (r < 0.24f) {
                    m.Prop("props", "gravestone_cross", x, y);
                    m.Collision(x - 9, y - 7, 18, 7);
                    ++graves;
                } else if (r < 0.275f) {
                    m.Prop("props", "grave_mound", x, y);
                    ++graves;
                } else if (r < 0.30f) {
                    m.Prop("props", "swamp_tree", x, y);
                    m.Collision(x - 8, y - 8, 16, 8);
                }
            }
        (void)graves;

        // The crypt at the head of the yard, and what is in front of it.
        //
        // The grille is off it. It stood barred for as long as there was
        // nothing under it; there are three floors under it now, and the art
        // says so -- prop crypt_open has the bars gone, the doorway cut dark
        // and the grille itself left leaning against the wall beside it. The
        // sign at the gate was always a warning about this door.
        //
        // The art is 176px on its bottom edge. Measured off it: the doorway is
        // 40px wide about the centre with its floor 22px above the bottom, and
        // the building fills 68px either side of centre.
        {
            const auto [cx0, cy0] = at(GRAVE_CX, GRAVE_CY + 8, 16, 30);
            m.Prop("props", "crypt_open", cx0, cy0);
            m.SortLift("crypt_open", 74);
            // The face either side of the doorway, and the wall behind it: the
            // gap between them is what you walk into.
            m.Collision(cx0 - 78, cy0 - 40, 56, 44);
            m.Collision(cx0 + 22, cy0 - 40, 56, 44);
            m.Collision(cx0 - 22, cy0 - 40, 44, 18);
            m.Spawn("from_crypt", cx0, cy0 + 26);
            m.Portal(cx0 - 20, cy0 - 24, 40, 28, "crypt_1", "entrance",
                     "Go down into the crypt");
            MarkWorld("dungeon", "Hollowrest Crypt", cx0, cy0 - 10);
            m.Danger(34);      // the vaults: its dead are Combat 26-34
            PlaceChest(m, "chest_hollowrest", cx0 - 132, cy0 - 26, "chest_hollowrest");
            // In the aisle in front of its own door, not behind the crypt:
            // south of it is outside the fence. He kept the door shut.
            m.Enemy("barrow_wight", cx0 + 6, cy0 - 56, 1, 0.0f, 200.0f);
        }

        // The dead, spread over the ground rather than in a knot: shamblers in
        // the middle, skeletons out by the fence, wraiths in the sunken corner.
        // On a lattice three cells apart, so no two of them start within reach
        // of each other: walking in wakes one grave, not the whole yard.
        for (int cy = GRAVE_CY - 12; cy <= GRAVE_CY + 12; cy += 3)
            for (int cx = GRAVE_CX - 20; cx <= GRAVE_CX + 20; cx += 3) {
                if (!InGraveyard(cx, cy) || GraveGate(cx, cy)) continue;
                const float r = Hash2(cx, cy, 6611);
                const auto [x, y] = at(cx, cy);
                if (!m.Clear(x, y)) continue;
                const float edge = GraveField(cx, cy);
                const int level = 1 + static_cast<int>(Hash2(cx, cy, 6612) * 3.0f);
                if (edge > 0.55f) {
                    // Out by the fence: the ones that still carry a blade.
                    if (r < 0.46f) m.Enemy("skeleton", x, y, level, 50.0f, 240.0f);
                } else if (cx < GRAVE_CX - 4) {
                    // The old, sunken half of the yard.
                    if (r < 0.50f) m.Enemy("wraith", x, y, level, 60.0f, 240.0f);
                } else if (r < 0.46f) {
                    // Among the newer graves, where the digging still happens.
                    m.Enemy("zombie", x, y, level, 50.0f, 220.0f);
                }
            }
    }

    // --- the way up to the Ice Spire --------------------------------------------------
    // North off the top of the foothills, and closed to anyone below Combat 30.
    {
        const int px = 24 * OW_CELL + 16;
        m.Portal(px - 64, 0, 128, 24, "ice_spire_peak", "from_hollowmarch", "To the Ice Spire", false);
        MarkWorld("path", "Ice Spire Peak  (Combat 30)", px, 16);
        m.Danger(34);
        m.Requires(30);
        m.Spawn("from_peak", px, 76);
        json& o = m.Object("sign_ice_spire", "sign", px + 80, 96);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "The climb north";
        o["text"]   = "ICE SPIRE PEAK, north, up the goat track.\n\n"
                      "Trolls on the high slopes. Wyverns nest at the summit.\n\n"
                      "The warden's mark is cut under it: no one below Combat 30 is let past this stone.";
        m.Collision(px + 64, 86, 32, 10);
    }

    // --- the way east to the Ashen Path -------------------------------------------------
    // Off the east edge below the Cursed Reach, north of the Whisperwood trail,
    // and closed below Combat 40. Not up on the Reach itself: its plateau is
    // climbed to, and an exit has to be walkable to from the road.
    {
        const int ey = 50 * OW_CELL + 16;
        m.Portal(OW_PX_W - 24, ey - 72, 24, 144, "ashen_path", "from_hollowmarch", "To the Ashen Path", false);
        MarkWorld("path", "The Ashen Path  (Combat 40)", OW_PX_W - 40, ey);
        m.Danger(45);
        m.Requires(40);
        m.Spawn("from_ashen", OW_PX_W - 96, ey);
        json& o = m.Object("sign_ashen_path", "sign", OW_PX_W - 150, ey - 70);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A burnt stake";
        o["text"]   = "Somebody has burnt letters into it.\n\n"
                      "THE ASHEN PATH. At its end, the pit.\n\n"
                      "Do not go on unless you have fought for a long time and won: Combat 40.";
        m.Collision(OW_PX_W - 166, ey - 80, 32, 10);
    }

    // A couple of chests off the road for the curious.
    PlaceChest(m, "chest_meadow_01", 96 * OW_CELL, 78 * OW_CELL, "chest_common");
    PlaceChest(m, "chest_wood_01",  114 * OW_CELL, 52 * OW_CELL, "chest_common");
    PlaceChest(m, "chest_mire_01",   10 * OW_CELL, 60 * OW_CELL, "chest_common");

    // --- what comes out at night ----------------------------------------------------
    // Last, so every post above keeps its number. A sparse lattice of its own:
    // well off the road and the trail -- the road is the way to travel after
    // dark, and that is the whole of the advice -- nowhere near the town gate,
    // where a new character is finding out which end of the sword to hold, and
    // nowhere near a way in, a camp or a person. What comes is a step or two
    // up from what the ground has by day, from somewhere nearby that is worse:
    // wolves off the Westwold and bats out of the well on the meadow, the
    // barrow's dead under the trees and up in the hills, what lives at the
    // bottom of the well loose in the Mire, and the hellgate's imps on the
    // Cursed Reach. Half of the posts, on any one night.
    {
        const int gate_cx = static_cast<int>(RoadX(86)), gate_cy = 88;
        for (int gy = 6; gy < OW_H - 7; gy += 8) {
            for (int gx = OW_X0 + 5; gx < OW_W - 7; gx += 8) {
                // Off the lattice by a few cells either way, so that what is
                // abroad is not drawn up in ranks.
                const int cx = gx + static_cast<int>(Hash2(gx, gy, 2022) * 5.0f) - 2;
                const int cy = gy + static_cast<int>(Hash2(gx, gy, 2023) * 5.0f) - 2;
                const Biome b = BiomeAt(cx, cy);
                if (b == WATER || b == ROAD || b == TRAIL || b == GRAVEYARD || BogAt(cx, cy)) continue;
                if (fabsf(cx - RoadX(cy)) < 6.5f && cy > 6 && cy < 94) continue;
                if (cx >= RoadX(TRAIL_JUNCTION_CY) && fabsf(cy - TrailY(cx)) < 5.5f) continue;
                if (GraveField(cx, cy) < 3.2f) continue;                             // Hollowrest has its own dead
                if (abs(cx - 12) <= 7 && cy >= 37 && cy <= 50) continue;             // and so has the barrow
                if (abs(cx - MIRE_CAMP_X) <= 10 && abs(cy - MIRE_CAMP_Y) <= 10) continue;   // the chief keeps his own
                if (std::hypot(static_cast<float>(cx - gate_cx), static_cast<float>(cy - gate_cy)) < 18.0f) continue;
                const int x = cx * OW_CELL + 16, y = cy * OW_CELL + 16;
                if (!m.Clear(x, y) || m.NearestHaven(x, y) < 352.0f) continue;
                if (Hash2(cx, cy, 2020) > 0.21f) continue;      // about one candidate in five

                const string group = "night_" + std::to_string(cx - OW_X0) + "_" + std::to_string(cy);
                if (b == MEADOW)         m.NightEnemy({"wolf", "bat"}, group, x, y, 1, 1, 0.5f);
                else if (b == GREENWOOD) {
                    m.NightEnemy({"wolf", "zombie"}, group, x, y, 1, 2, 0.5f);
                    // Under the trees they come in twos.
                    if (Hash2(cx, cy, 2021) < 0.5f && m.Clear(x + 44, y + 26))
                        m.NightEnemy({"wolf", "zombie"}, group, x + 44, y + 26, 1, 1, 0.5f);
                }
                // Not bats, here: the raiders camped in the hills are worse than a bat.
                else if (b == FOOTHILLS) m.NightEnemy({"wraith", "zombie"}, group, x, y, 1, 2, 0.5f);
                else if (b == MIRE)      m.NightEnemy({"wraith", "hound"}, group, x, y, 1, 1, 0.5f);
                else if (b == CURSED)    m.NightEnemy({"hound", "imp"}, group, x, y, 1, 2, 0.5f);
            }
        }
        std::printf("  the Hollowmarch by night: %d posts\n", m.night_posts);
    }

    // A few swallowtails in the greenwood, well inside it and off the road
    // and the trail -- and a long way from where a new character starts, whose
    // first E should be at a tree, not a butterfly.
    {
        int bug_i = 0;
        float sx = 0.0f, sy = 0.0f;
        if (m.dq.contains("spawns") && m.dq["spawns"].contains("start")) {
            sx = m.dq["spawns"]["start"][0].get<float>();
            sy = m.dq["spawns"]["start"][1].get<float>();
        }
        const int got = PlaceBugs(m, "swallowtail", 7, OW_CELL, OW_X0 + 4, 4, OW_W - 4, OW_H - 4, 7521u, 9.0f,
                                  [&](int cx, int cy) {
            for (int oy = -2; oy <= 2; ++oy)
                for (int ox = -2; ox <= 2; ++ox)
                    if (BiomeAt(cx + ox, cy + oy) != GREENWOOD) return false;
            if (fabsf(cx - RoadX(cy)) < 3.2f || OnTrail(cx, cy, 3.4f)) return false;
            if (Hash2(cx, cy, 4242) < 0.14f) return false;          // where the scenery stands
            return std::hypot(cx * OW_CELL + 16 + m.ox - sx, cy * OW_CELL + 16 - sy) > 1200.0f;
        }, bug_i);
        std::printf("  the Hollowmarch: %d swallowtails in the greenwood\n", got);
    }

    PlaceCurios(m);
    m.Write("maps");
    WriteWorldMap("data", (OW_W - OW_X0) * OW_CELL, OW_PX_H, m.ox);
}

// --- town --------------------------------------------------------------------

static void BuildDreamHavenbrook(const MapBuilder& town);

static void BuildTown() {
    // Sixteen columns wider than it was, for the farm: everything else in the
    // town is placed from the west wall or from the crossroads, and the fence,
    // the gates and the south road are all drawn from W and H, so the town
    // simply has a field on the end of it now. The crossroads is still the
    // middle of the village and the farm is a walk out past the pond, which is
    // what a farm should be.
    const int CELL = 32, W = 72, H = 44;
    MapBuilder m("town_havenbrook", "Havenbrook", W * CELL, H * CELL);
    m.Ambient("town");
    m.Subtitle("A market town on the southern road");
    m.Background(44, 58, 44);
    std::mt19937 rng(4242u);

    // Havenbrook teaches its trades. Three working places stand inside the
    // fence: the sawpit in the north-west with a stand of timber behind it,
    // the gravel pit in the north-east, and the mill pond in the south-east
    // with a jetty out over it. Which ground a cell gets is decided here, so
    // the pit is dirt and the pond is water rather than a patch laid over
    // grass -- the base layer is drawn in name order, and grass would cover
    // dirt whatever order it was placed in.
    const auto in_pit = [](int cx, int cy) {
        return cx >= 42 && cx <= 52 && cy >= 3 && cy <= 10;
    };
    // The pond, as an ellipse, and the planks of the jetty out into it.
    const auto pond = [](int cx, int cy) {
        const float dx = (cx - 47.5f) / 5.6f, dy = (cy - 37.5f) / 3.6f;
        return dx * dx + dy * dy <= 1.0f;
    };
    const auto jetty = [&](int cx, int cy) {
        // From dry land, or it is an island: the bank is only land west of 42.
        return cy == 37 && cx >= 42 && cx <= 46;
    };
    // The farmyard: beaten earth in front of the barn, on the east side.
    const auto farmyard = [](int cx, int cy) {
        return cx >= 57 && cx <= 66 && cy >= 12 && cy <= 19;
    };

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.25f, cy * 0.25f, 77);
            // A crossroads through the middle of the village.
            const bool on_road = (abs(cy - 22) <= 1) || (abs(cx - 28) <= 1);
            string tile = on_road ? VariantOf("road", cx, cy)
                        : (v > 0.6f ? "grass_light" : (v > 0.3f ? "grass" : "grass_olive"));
            if (in_pit(cx, cy))   tile = v > 0.6f ? "sand" : (v > 0.3f ? "dirt" : "dirt_dark");
            else if (farmyard(cx, cy)) tile = v > 0.5f ? "dirt" : "dirt_dark";
            else if (jetty(cx, cy)) tile = "plank_floor";
            else if (pond(cx, cy))  tile = "water";
            m.Ground(VariantOf(tile, cx, cy), cx * CELL, cy * CELL, CELL);
        }

    // The pond is water: it cannot be walked into, but the jetty over it can.
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx)
            if (pond(cx, cy) && !jetty(cx, cy)) m.Collision(cx * CELL, cy * CELL, CELL, CELL);

    // Fence the village in, leaving the south gate open.
    for (int cx = 0; cx < W; ++cx) {
        if (abs(cx - 28) > 2) m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
        m.Collision(cx * CELL, 0, CELL, CELL);
    }
    // And the west gate, where the cross street runs out: the road to the
    // Westwold. It ran into the fence for as long as there was nothing beyond it.
    for (int cy = 0; cy < H; ++cy) {
        if (abs(cy - 22) > 1) m.Collision(0, cy * CELL, CELL, CELL);
        m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }
    m.Portal(0, 21 * CELL - 8, 24, 3 * CELL + 16, "westwold", "from_havenbrook", "To the Westwold", false);
    m.Danger(5);
    // Both ways in are gates: the gatehouse across the south road, the same one
    // that stands on the Hollowmarch side of it, and a pair of towers where the
    // cross street goes out to the west. And the fence the village always had is
    // a fence you can see: a palisade all the way round.
    PlaceFrontGate(m, 28 * CELL + 16, (H - 2) * CELL, 99, W * CELL);
    PlaceSideGate(m, CELL + 8, 21 * CELL, 24 * CELL);
    for (int x = 28; x < W * CELL; x += 54) m.Prop("props", "palisade", x, CELL + 6);
    PalisadeSide(m, 14, 2 * CELL, (H - 2) * CELL - 8, 21 * CELL, 24 * CELL);
    PalisadeSide(m, W * CELL - 14, 2 * CELL, (H - 2) * CELL - 8);
    m.Spawn("from_westwold", 3 * CELL, 22 * CELL + 16);
    {
        json& sign = m.Object("sign_west_gate", "sign", 3 * CELL + 8, 20 * CELL + 20);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "West Gate";
        sign["text"]   = "THE WESTWOLD ROAD\n\nHidewater steading, half a mile. The river Wend, two. "
                         "Beyond the fork: the Brackenwood, north; the Howling Fells, west.\n\n"
                         "Nailed under it, in a tanner's hand: WOLVES ON THE DOWNS. Pelts bought at Hidewater.";
        m.Collision(3 * CELL + 8 - 16, 20 * CELL + 10, 32, 10);
    }

    // Inside the gate, in the middle of the road: the towers' roofs are drawn
    // over whoever stands north of a tower, but between them there is only the
    // lintel, and this is north of that.
    m.Spawn("from_field", 28 * CELL + 16, (H - 4) * CELL - 8);
    m.Spawn("respawn",    28 * CELL + 16, 26 * CELL);
    m.Spawn("default",    28 * CELL + 16, 26 * CELL);
    m.Portal(26 * CELL, H * CELL - 40, 5 * CELL, 40,
             "overworld", "from_town", "Leave Havenbrook", false);

    // Buildings, each with a door that leads somewhere.
    //
    // Each one also gets its own doorstep to come back out onto. The interiors
    // all used to leave to the town's default spawn, which is the crossroads,
    // so stepping out of any building put you in the middle of the square.
    PlaceBuilding(m, "building_guild",   28 * CELL + 16, 14 * CELL, 144, 111,
                  "guild_hall", "entrance", "Enter the guild hall", "from_guild_hall");
    PlaceBuilding(m, "building_house_a", 13 * CELL,      18 * CELL, 136, 149,
                  "house_elder", "entrance", "Enter Maren's house", "from_house_elder");
    // The inn has its own building rather than another cottage: modelled in
    // tools/blender_props.py (prop_inn_building). The door is at the centre of
    // the image, which is where PlaceBuilding cuts the doorway.
    PlaceBuilding(m, "inn_building",     44 * CELL,      18 * CELL, 176, 160,
                  "house_inn", "entrance", "Enter the inn", "from_house_inn", "props");
    PlaceBuilding(m, "building_shop",    14 * CELL,      33 * CELL, 110, 72,
                  "house_smith", "entrance", "Enter the forge", "from_house_smith");

    // The guild hall's plaque, over its own door. This art is the only thing
    // in the project that says GUILD HALL on it, so it belongs on the guild
    // hall and nowhere else.
    {
        // Beside the door rather than over it: an object drawn above the
        // doorway would sort behind the building and be invisible, and one in
        // the doorway would block the way in.
        json& o = m.Object("sign_guild_hall", "sign", 28 * CELL + 16 + 96, 14 * CELL);
        o["sprite"] = ObjPath("sign_guild");
        o["title"]  = "Havenbrook Guild Hall";
        o["text"]   = "THE ADVENTURERS' GUILD OF HAVENBROOK\n\n"
                      "Contracts posted within. Dues payable within. "
                      "Complaints, also within, and briefly.";
    }

    // The mission board, right where you walk in.
    {
        json& o = m.Object("board_havenbrook", "board", 33 * CELL, 24 * CELL);
        o["sprite"] = ObjPath("guild_noticeboard");
        o["title"]  = "Havenbrook Mission Board";
        o["quests"] = json::array({"q_thin_the_herd", "q_firewood",
                                   "q_ore_for_the_forge", "q_orc_trouble"});
        m.Collision(33 * CELL - 36, 24 * CELL - 12, 72, 12);
    }

    // --- the well ----------------------------------------------------------------------
    // Dry these eleven years, and the way down to what stopped it.
    {
        const int wcx = 25, wcy = 25;
        const int wx = wcx * CELL, wy = wcy * CELL;
        // A paved apron, so the well reads as part of the square rather than as
        // something standing in a field: four hundred buckets a day wore the
        // grass off long before the water stopped.
        for (int cy = wcy - 1; cy <= wcy + 1; ++cy)
            for (int cx = wcx - 1; cx <= wcx + 1; ++cx)
                m.Ground(VariantOf("road", cx, cy), cx * CELL, cy * CELL, CELL);
        // The dry variant: a board over the mouth and the bucket left on the
        // rim. A well with water in it would tell the player the opposite of
        // what Bess is about to.
        m.Prop("props", "well_dry", wx, wy);
        m.Collision(wx - 20, wy - 14, 40, 14);
        m.Spawn("from_well", wx, wy + 40);
        m.Portal(wx - 24, wy - 6, 48, 30, "well_shallow", "entrance", "Climb down the well");
        m.Danger(12);
        json& sign = m.Object("sign_well", "sign", wx - 62, wy + 6);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "The town well";
        sign["text"]   = "HAVENBROOK WELL\n\nFour hundred buckets a day, and a queue from dawn.\n\n"
                         "Under that, on a board nailed over the winch: DRY SINCE THE BAD YEAR. "
                         "DO NOT LOWER THE BUCKET. DO NOT GO DOWN.";
        m.Collision(wx - 78, wy - 4, 32, 10);
    }

    // A cooking fire anyone may use.
    {
        json& o = m.Object("range_town", "range", 40 * CELL, 30 * CELL);
        o["sprite"] = "assets/props/campfire_ring.png";
        o["title"]  = "Cooking fire";
        m.Collision(40 * CELL - 16, 30 * CELL - 12, 32, 12);
    }

    // A cauldron beside it, for anyone with a brew to make.
    PlaceCauldron(m, "cauldron_town", 43 * CELL, 30 * CELL);

    // --- the waystone ----------------------------------------------------------------
    // On the grass at the north-east corner of the crossroads, where every road
    // in the town passes it.
    PlaceWaystone(m, "havenbrook", 1010, 652);

    // --- the farm ------------------------------------------------------------------
    // Sixteen columns of new town, east of the pond: a barn on the yard, a
    // farmhouse beside it, and four fenced pens with what is in them. Nothing
    // here fights -- a hen is a hen -- but every one of them is worth a supper,
    // and the sheep are worth a fleece, which is what the loom at Mossvale runs
    // on. The farmer stands in the yard and will say which pen is which.
    {
        const int fx = 60 * CELL, fy = 16 * CELL;      // the yard
        // The farmhouse stands in the yard as a house, with nothing to go into:
        // a door needs a room behind it, and the farm's work is all outside.
        m.Decor("assets/objects/building_house_a.png", fx + 2 * CELL, fy - 2 * CELL, 136, 149);
        m.Collision(fx + 2 * CELL - 58, fy - 2 * CELL - 30, 116, 30);
        // The barn: a stall's worth of roof over the yard, and the hay by it.
        m.Prop("props", "market_stall", fx - 2 * CELL, fy + CELL);
        m.Collision(fx - 2 * CELL - 40, fy + CELL - 16, 80, 16);
        m.Prop("props", "hay_rick", fx - 3 * CELL, fy - CELL);
        m.Collision(fx - 3 * CELL - 22, fy - CELL - 14, 44, 14);
        m.Prop("props", "hay_rick", fx + 4 * CELL, fy + 2 * CELL);
        m.Collision(fx + 4 * CELL - 22, fy + 2 * CELL - 14, 44, 14);
        m.Prop("props", "log_pile", fx + 5 * CELL, fy - 2 * CELL);
        m.Collision(fx + 5 * CELL - 16, fy - 2 * CELL - 10, 32, 10);

        // Four pens, each a run of rail fence with a gap to walk in by, and
        // what lives in it. The fence is drawn a cell at a time so a pen can be
        // any size; the gap is where the farmer walks.
        struct Pen { int x0, y0, x1, y1; const char* beast; int many; int gap; };
        const Pen pens[] = {
            {56, 22, 62, 27, "chicken", 5, 24},     // the hen run, nearest the house
            {64, 22, 70, 27, "pig",     3, 24},
            {56, 29, 62, 35, "sheep",   4, 31},
            {64, 29, 70, 35, "cow",     3, 31},
        };
        int post = 0;
        for (const Pen& pen : pens) {
            // The rails run east and west, two cells to a length, the way the
            // Westwold's do -- one a cell overlaps itself into a hedge. The
            // north and south sides are rails; the east and west sides are a
            // line of posts, which is what a rail fence looks like end-on, and
            // they are what keeps anything in.
            Fence(m, CELL, pen.x0, pen.x1 + 1, pen.y0);
            Fence(m, CELL, pen.x0, pen.x1 + 1, pen.y1);
            for (int cy = pen.y0; cy <= pen.y1; ++cy) {
                for (int cx : {pen.x0, pen.x1}) {
                    if (cx == pen.x0 && cy == pen.gap) continue;        // the way in
                    m.Prop("props", "fence_post", cx * CELL + 16, cy * CELL + 26);
                    m.Collision(cx * CELL + 8, cy * CELL + 16, 16, 10);
                }
            }
            // What is in it, spread about inside the rails. They never leave
            // the pen: a short leash keeps them off the fence and out of the
            // street.
            for (int k = 0; k < pen.many; ++k) {
                const int cx = pen.x0 + 2 + (k * 2) % std::max(1, pen.x1 - pen.x0 - 2);
                const int cy = pen.y0 + 2 + (k * 3) % std::max(1, pen.y1 - pen.y0 - 2);
                m.Enemy(pen.beast, cx * CELL + 16, cy * CELL + 20, 1, 90.0f, 70.0f);
                ++post;
            }
        }
        (void)post;

        // A trough and a water butt, because a pen with nothing in it but
        // animals reads as a paddock.
        m.Prop("props", "well_dry", (58) * CELL + 16, 20 * CELL + 16);
        m.Collision(58 * CELL, 20 * CELL + 6, CELL, 12);

        {
            json& o = m.Object("sign_farm", "sign", 57 * CELL, 13 * CELL + 16);
            o["sprite"] = "assets/props/signpost.png";
            o["title"]  = "Marrow Farm";
            o["text"]   = "MARROW FARM\n\nEGGS. MILK. FLEECES. MUTTON, PORK AND BEEF IN SEASON.\n\n"
                          "Under it: MIND THE GATES. IF YOU LET THE PIGS OUT YOU ARE GETTING THEM BACK IN.";
            m.Collision(57 * CELL - 16, 13 * CELL + 6, 32, 10);
        }
        m.Npc("npc_marrow", "Farmer Marrow", "citizen2", 59 * CELL + 16, 18 * CELL + 10, "marrow_root", 0);
    }

    // --- the tannery ---------------------------------------------------------------
    // Havenbrook had nowhere to learn a trade with: the Westwold's tannery is
    // out of the west gate and past the wolves, which is no use to anyone at
    // Crafting 1. This is the same yard inside the walls -- frames of hide
    // drying, and Nessa, who keeps an order book and buys what comes off it.
    // The frames are what is worked at. There was a carpenter's bench among
    // them, and the frames were scenery round it: a tanner's station is the
    // tanner's own, the way Wynn's is a loom.
    {
        const int tx = 8 * CELL, ty = 27 * CELL;
        for (int k = 0; k < 3; ++k)
            PlaceTanningRack(m, "rack_tannery_" + std::to_string(k), tx - 84 + k * 84, ty - 64);
        m.Prop("props", "log_pile", tx + 120, ty - 60);
        m.Collision(tx + 120 - 16, ty - 70, 32, 10);
        {
            json& o = m.Object("sign_tannery", "sign", tx - 118, ty + 20);
            o["sprite"] = "assets/props/signpost.png";
            o["title"]  = "The Tannery";
            o["text"]   = "NESSA'S TANNERY\n\nHIDES CURED. THREAD WAXED. LEATHER CUT TO ORDER.\n\n"
                          "Under it, in a newer hand: WORK WANTED. I PAY FOR WHAT YOU MAKE, NOT FOR "
                          "WHAT YOU FIND. ASK ME FOR THE BOOK.";
            m.Collision(tx - 118 - 16, ty + 20 - 10, 32, 10);
        }
        m.Npc("npc_nessa", "Nessa the Tanner", "citizen1", tx + 86, ty + 6, "nessa_root", 1)["shop"] = "havenbrook_tannery";
    }

    // A workbench by the forge.
    {
        json& o = m.Object("bench_town", "workbench", 20 * CELL, 34 * CELL);
        o["sprite"]  = "assets/props/workbench.png";
        o["title"]   = "Workbench";
        o["station"] = "workbench";
        m.Collision(20 * CELL - 34, 34 * CELL - 18, 67, 18);
    }

    // Corrin has the south gate, from the foot of its tower on the road side,
    // and Edda the west. He used to stand in the road four cells short of a gap
    // in nothing.
    m.Npc("npc_guard",  "Watchman Corrin", "fighter2", FrontGateKeeperX(28 * CELL + 16),
          FrontGateKeeperY((H - 2) * CELL), "guard_root", 0);
    m.Npc("npc_edda",   "Watchman Edda",   "fighter2", 2 * CELL + 26, 24 * CELL + 6, "edda_root", 1)["tint"] =
        json::array({236, 226, 255});
    m.Npc("npc_hunter", "Hunter Ivo",      "citizen2", 46 * CELL, 30 * CELL, "hunter_root", 0, true)["shop"] = "havenbrook_bowyer";

    // The general store: a stall on the square east of the crossroads, with
    // Tobin beside it and his stock stacked either side.
    {
        const int sx = 35 * CELL, sy = 27 * CELL;
        m.Prop("props", "market_stall", sx, sy);
        m.Collision(sx - 32, sy - 14, 64, 14);
        m.Prop("props", "crates_sacks", sx + 56, sy - 4);
        m.Collision(sx + 56 - 18, sy - 14, 36, 10);
        m.Prop("props", "barrel", sx - 88, sy - 4);
        m.Collision(sx - 88 - 14, sy - 14, 28, 10);
        // Beside the stall, not behind it: the awning would hide him.
        m.Npc("npc_tobin", "Tobin the Grocer", "citizen1", sx - 50, sy + 6, "tobin_root", 0)["shop"] = "havenbrook_general";
    }

    // --- the sawpit ---------------------------------------------------------------------
    // A log up on trestles with the saw still in it, timber stacked round it,
    // and a stand of young oak behind to cut. Where a new character is taught
    // to use an axe.
    int town_tree = 0, town_rock = 0;
    {
        const int sx = 9 * CELL, sy = 9 * CELL;
        m.Prop("props", "sawmill", sx, sy);
        m.Collision(sx - 58, sy - 26, 116, 26);
        m.Prop("props", "log_pile", sx + 84, sy - 30);
        m.Collision(sx + 84 - 22, sy - 40, 44, 12);
        m.Prop("props", "log_pile", sx - 96, sy + 26);
        m.Collision(sx - 96 - 22, sy + 16, 44, 12);
        m.Prop("props", "crates_sacks", sx + 100, sy + 40);
        m.Collision(sx + 100 - 18, sy + 30, 36, 10);
        m.Prop("props", "workbench", sx - 40, sy + 80);
        m.Collision(sx - 40 - 34, sy + 62, 67, 18);

        json& sign = m.Object("sign_sawpit", "sign", sx + 122, sy + 6);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "Havenbrook Sawpit";
        sign["text"]   = "THE SAWPIT\n\nTimber cut and sawn. Firewood by the cord.\n\n"
                         "Under it, in newer paint: apprentices wanted. Axe provided.";
        m.Collision(sx + 122 - 16, sy - 4, 32, 10);

        // The stand of timber: young oak, cut at Woodcutting 1.
        const int trees[][2] = {{3, 4}, {5, 3}, {7, 4}, {4, 7}, {6, 6}, {3, 10}, {5, 11}, {8, 12}, {11, 12}, {2, 7}};
        for (const auto& t : trees)
            PlaceTree(m, rng, 900 + town_tree++, t[0] * CELL + 16, t[1] * CELL + 16, t[0] % 2 == 0, 1, "logs");

        m.Npc("npc_sawyer", "Sawyer Jessa", "citizen1", sx + 52, sy + 34, "sawyer_root", 0);
    }

    // --- the gravel pit -----------------------------------------------------------------
    // Cut into the rise behind the houses: copper in the rock, a tipper cart
    // on a length of rail, and the pitmaster who will lend a pickaxe.
    {
        const int px = 46 * CELL, py = 7 * CELL;
        m.Prop("props", "ore_cart", px, py);
        m.Collision(px - 34, py - 20, 68, 20);
        m.Prop("props", "crates_sacks", px + 76, py + 26);
        m.Collision(px + 76 - 18, py + 16, 36, 10);
        m.Prop("props", "barrel", px - 84, py + 34);
        m.Collision(px - 84 - 14, py + 24, 28, 10);

        json& sign = m.Object("sign_pit", "sign", px - 108, py - 18);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "The Gravel Pit";
        sign["text"]   = "HAVENBROOK PIT\n\nCopper worked here. Mind the loose face.\n\n"
                         "Chalked below: pick lent to anyone willing to swing it.";
        m.Collision(px - 108 - 16, py - 28, 32, 10);

        // Copper in the face, at Mining 1, and loose rock round the edge.
        const int seams[][3] = {{43, 4, 1}, {45, 3, 1}, {49, 4, 1}, {51, 6, 0}, {44, 9, 0}, {50, 9, 1}};
        for (const auto& r : seams)
            PlaceRock(m, rng, 900 + town_rock++, r[0] * CELL + 16, r[1] * CELL + 16, r[2] != 0, 1, "copper_ore");
        for (const auto& b : {std::pair<int, int>{42, 6}, {52, 3}, {48, 10}, {41, 9}}) {
            const int bx = b.first * CELL + 16, by = b.second * CELL + 16;
            m.Prop("objects", kRocks[(bx + by) % 8], bx, by);
            m.Collision(bx - 18, by - 10, 36, 10);
        }

        m.Npc("npc_pitmaster", "Pitmaster Dorn", "citizen2", px - 24, py + 62, "pit_root", 0);
    }

    // --- the mill pond -------------------------------------------------------------------
    // Fed by the brook under the south fence: a jetty of planks out over the
    // water, a boat drawn up on the bank, and the angler who teaches the rod.
    {
        // Reeds along the bank. Nothing under the jetty: a row of boulders
        // there read as rocks dumped in the water.
        for (const auto& r : {std::pair<int, int>{42, 35}, {44, 34}, {49, 34}, {52, 37}, {50, 41}, {45, 41}}) {
            m.Prop("props", "reeds", r.first * CELL + 16, r.second * CELL + 20);
        }
        for (const auto& l : {std::pair<int, int>{48, 36}, {50, 38}, {46, 39}}) {
            m.Prop("props", "lily_pads", l.first * CELL + 16, l.second * CELL + 16);
        }
        m.Prop("props", "rowboat", 42 * CELL, 41 * CELL);
        m.Collision(42 * CELL - 30, 41 * CELL - 24, 60, 24);

        json& sign = m.Object("sign_pond", "sign", 41 * CELL, 34 * CELL);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "The Mill Pond";
        sign["text"]   = "THE MILL POND\n\nMinnow and pike. Keep the jetty clear.\n\n"
                         "A smaller hand has added: rods lent, fish shared.";
        m.Collision(41 * CELL - 16, 34 * CELL - 10, 32, 10);

        // Two places to cast from: the end of the jetty and the bank beside it.
        PlaceFishingSpot(m, "fish_pond_1", 47 * CELL + 16, 37 * CELL + 16, "pond",
                         {"raw_minnow", "raw_pike"}, 1);
        PlaceFishingSpot(m, "fish_pond_2", 44 * CELL + 16, 40 * CELL + 16, "pond",
                         {"raw_minnow"}, 1);

        m.Npc("npc_angler", "Angler Sula", "citizen1", 42 * CELL + 16, 36 * CELL + 20, "angler_root", 0);
    }

    // --- foot traffic -------------------------------------------------------------------
    // People with somewhere to be. Each walks a round by the world's clock --
    // see Npc -- out of a door or in at a gate, along the streets to where
    // their day takes them, a while stood at each place, and back; and all but
    // the watch go in at night. A stop is {x, y, seconds stood, facing}.
    struct Walk { int x, y; float pause; int facing; };
    vector<vector<Walk>> rounds;
    const auto walker = [&](const string& npc_id, const string& name, const string& sprite,
                            const string& dialogue, const vector<Walk>& stops, bool there_and_back,
                            float speed, float phase, float from_hour, float to_hour,
                            std::array<int, 3> tint) {
        json& n = m.Npc(npc_id, name, sprite, stops.front().x, stops.front().y, dialogue, 0);
        json path = json::array();
        for (const Walk& w : stops) path.push_back({w.x, w.y, w.pause, w.facing});
        n["path"] = path;
        n["ping_pong"] = there_and_back;
        n["speed"] = speed;
        n["phase"] = phase;
        if (from_hour != to_hour) n["hours"] = {from_hour, to_hour};
        n["tint"] = {tint[0], tint[1], tint[2]};
        rounds.push_back(stops);
    };
    // Facings: 0 down, 1 left, 2 right, 3 up.
    // Wenna fetches water from the mill pond, the well being what it is.
    walker("npc_wenna", "Wenna", "citizen1", "wenna_root",
           {{13 * CELL, 19 * CELL + 6, 10.0f, 0}, {13 * CELL, 22 * CELL + 12, 0, 0}, {38 * CELL + 14, 22 * CELL + 12, 0, 0},
            {38 * CELL + 14, 37 * CELL, 0, 0}, {41 * CELL, 38 * CELL + 20, 9.0f, 2}},
           true, 30.0f, 0.0f, 6.0f, 19.0f, {255, 236, 224});
    // Old Perrin does the rounds of the square: the stall, the board, the fire.
    walker("npc_perrin", "Old Perrin", "citizen2", "perrin_root",
           // Round the stall rather than through it: down its east side to the
           // front, west past the barrel to the board, and back under it to the fire.
           {{44 * CELL, 19 * CELL + 6, 12.0f, 0}, {44 * CELL, 22 * CELL + 8, 0, 0}, {37 * CELL + 26, 22 * CELL + 8, 0, 0},
            {37 * CELL + 26, 28 * CELL + 6, 0, 0}, {35 * CELL, 28 * CELL + 6, 10.0f, 3}, {31 * CELL + 8, 28 * CELL + 6, 0, 0},
            {31 * CELL + 8, 25 * CELL + 4, 0, 0}, {33 * CELL, 25 * CELL + 4, 7.0f, 3}, {31 * CELL + 8, 25 * CELL + 4, 0, 0},
            {31 * CELL + 8, 29 * CELL + 2, 0, 0}, {38 * CELL + 24, 31 * CELL + 10, 8.0f, 2},
            {38 * CELL + 24, 23 * CELL, 0, 0}, {44 * CELL, 22 * CELL + 8, 0, 0}},
           false, 24.0f, 40.0f, 8.0f, 18.0f, {232, 232, 255});
    // The watch walks the streets, gate to gate, day and night.
    walker("npc_brask", "Watchman Brask", "fighter2", "brask_root",
           {{27 * CELL + 20, 40 * CELL, 8.0f, 0}, {27 * CELL + 20, 23 * CELL, 0, 0}, {3 * CELL + 16, 23 * CELL, 9.0f, 1},
            {27 * CELL + 20, 23 * CELL, 0, 0}, {27 * CELL + 20, 17 * CELL, 6.0f, 3}, {27 * CELL + 20, 21 * CELL + 10, 0, 0},
            {52 * CELL, 21 * CELL + 10, 9.0f, 2}, {29 * CELL + 12, 21 * CELL + 10, 0, 0}, {29 * CELL + 12, 40 * CELL, 0, 0}},
           false, 34.0f, 0.0f, 0.0f, 0.0f, {255, 255, 255});
    // Tam carries cut logs from the sawpit down to the forge.
    walker("npc_tam", "Tam", "citizen2", "tam_root",
           {{12 * CELL + 8, 11 * CELL, 9.0f, 1}, {17 * CELL, 11 * CELL, 0, 0}, {17 * CELL, 23 * CELL, 0, 0},
            {17 * CELL, 35 * CELL, 0, 0}, {15 * CELL, 35 * CELL, 8.0f, 3}},
           true, 32.0f, 25.0f, 7.0f, 17.0f, {255, 244, 214});
    // Dace fishes off the end of the jetty, and drinks at the inn after.
    walker("npc_dace", "Dace", "citizen1", "dace_root",
           {{45 * CELL + 10, 19 * CELL + 6, 8.0f, 0}, {45 * CELL + 10, 23 * CELL + 4, 0, 0}, {38 * CELL + 14, 23 * CELL + 4, 0, 0},
            {38 * CELL + 14, 37 * CELL + 18, 0, 0}, {46 * CELL + 8, 37 * CELL + 18, 24.0f, 2}},
           true, 28.0f, 90.0f, 9.0f, 20.0f, {220, 240, 255});
    // Pip runs the guild's notices: the hall, the board, the south gate.
    walker("npc_pip", "Pip", "citizen2", "pip_root",
           {{28 * CELL + 16, 15 * CELL, 6.0f, 0}, {28 * CELL + 16, 22 * CELL, 0, 0}, {32 * CELL, 24 * CELL + 24, 5.0f, 2},
            {28 * CELL + 16, 24 * CELL + 24, 0, 0}, {28 * CELL + 16, 40 * CELL, 5.0f, 0}, {28 * CELL + 16, 22 * CELL, 0, 0}},
           false, 50.0f, 10.0f, 7.0f, 19.0f, {236, 255, 230});
    // A carter on the new road: in at the south gate, out at the west, and back.
    walker("npc_carter", "Hollis the Carter", "citizen1", "carter_root",
           {{28 * CELL + 16, 42 * CELL, 14.0f, 3}, {28 * CELL + 16, 22 * CELL + 16, 0, 0}, {2 * CELL, 22 * CELL + 16, 14.0f, 1}},
           true, 30.0f, 140.0f, 8.0f, 17.0f, {255, 226, 200});
    // A ranger in off the Westwold, selling pelts to Ivo and gone again.
    walker("npc_ranger", "Sorrel", "player_warden", "sorrel_root",
           {{2 * CELL, 21 * CELL + 16, 10.0f, 2}, {38 * CELL + 14, 21 * CELL + 16, 0, 0}, {38 * CELL + 14, 28 * CELL + 4, 0, 0},
            {45 * CELL, 28 * CELL + 16, 16.0f, 2}},
           true, 36.0f, 60.0f, 10.0f, 16.0f, {255, 255, 255});

    // Whether a point is on somebody's round, so nothing is planted in the way.
    const auto on_a_round = [&](int x, int y) {
        for (const auto& stops : rounds)
            for (size_t i = 0; i + 1 < stops.size(); ++i) {
                const float ax = static_cast<float>(stops[i].x), ay = static_cast<float>(stops[i].y);
                const float bx = static_cast<float>(stops[i + 1].x), by = static_cast<float>(stops[i + 1].y);
                const float dx = bx - ax, dy = by - ay;
                const float len2 = std::max(1.0f, dx * dx + dy * dy);
                const float t = std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f);
                const float px = ax + dx * t - x, py = ay + dy * t - y;
                if (px * px + py * py < 46.0f * 46.0f) return true;
            }
        return false;
    };

    // Greenery so the village is not a bare field.
    for (int i = 0; i < 26; ++i) {
        const int x = 2 * CELL + static_cast<int>(rng() % ((W - 4) * CELL));
        const int y = 3 * CELL + static_cast<int>(rng() % ((H - 6) * CELL));
        if (on_a_round(x, y)) continue;
        if (abs(y - 22 * CELL) < 80 || abs(x - 28 * CELL) < 80) continue;
        if (abs(x - 35 * CELL) < 120 && abs(y - 27 * CELL) < 90) continue;   // the stall
        // And out of the sawpit's stand of timber, the gravel pit and the pond.
        if (x < 15 * CELL && y < 15 * CELL) continue;
        if (x > 39 * CELL && y < 13 * CELL) continue;
        if (x > 38 * CELL && y > 31 * CELL) continue;
        if (rng() % 3 == 0) m.Prop("objects", Pick(kSmallTrees, rng), x, y);
        else                m.Prop("objects", Pick(kSmallBushes, rng), x, y);
    }

    m.Write("maps");
    BuildDreamHavenbrook(m);
}

// --- interiors ---------------------------------------------------------------

// The walls and floor of a room. The back wall is two courses tall so it reads
// as a wall seen from inside rather than a strip along the top; the sides and
// front are one. `door_l`..`door_r` are the front-wall columns left open, or
// -1 for a room with no way out through the front (an upstairs floor).
static void RoomShell(MapBuilder& m, int cols, int rows, int CELL,
                      const string& floor, const string& wall,
                      int door_l = -1, int door_r = -1) {
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool back  = (cy <= 1);
            const bool side  = (cx == 0 || cx == cols - 1);
            const bool front = (cy == rows - 1);
            const bool door  = front && door_l >= 0 && cx >= door_l && cx <= door_r;
            const bool solid = (back || side || front) && !door;
            m.Ground(solid ? wall : VariantOf(floor, cx, cy), cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
}

static void BuildInteriors() {
    // Elder Maren's house.
    //
    // A cottage that someone has lived in for a long time: a bed in the corner,
    // a hearth with a kettle on it, a table laid for one with a second chair for
    // visitors, and -- because she is the village elder and the one who knows
    // things -- a full bookshelf, a desk of scrolls, and a spinning wheel.
    {
        const int CELL = 32, cols = 18, rows = 13;
        MapBuilder m("house_elder", "Maren's House", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 18, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor_dark", "plaster_wall_warm",
                  cols / 2 - 1, cols / 2);

        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "town_havenbrook", "from_house_elder",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        m.Overlay("props", "inn_rug", 288, 272);

        // The hearth is her cooking range.
        {
            json& o = m.Object("range_maren", "range", 13 * CELL, 100);
            o["sprite"] = "assets/props/cottage_hearth.png";
            o["title"]  = "Hearth";
            m.Collision(13 * CELL - 32, 74, 64, 26);
        }

        // Sleeping corner.
        PlaceBed(m, "bed_maren", "bed_single", 70, 150, 30, 36);
        piece("nightstand",  104, 112, 18, 8);
        piece("travel_chest", 80, 370, 28, 12);

        // Where she works.
        piece("cottage_bookshelf", 176, 104, 40, 14);
        piece("writing_desk",      180, 202, 48, 16);
        m.Npc("npc_maren", "Elder Maren", "citizen1", 236, 184, "maren_root", 0);
        piece("spinning_wheel",    112, 300, 32, 12);

        // Where she eats.
        piece("dining_table", 400, 250, 46, 14);
        piece("tavern_chair", 362, 252, 16, 8);
        piece("tavern_chair", 438, 252, 16, 8);

        piece("wardrobe",  520, 120, 34, 14);
        piece("herb_pots", 520, 250, 30, 10);

        m.Write("maps");
    }

    // The guild hall.
    //
    // This was a bare stone box: dungeon floor, dungeon walls and a rock
    // standing in for a workbench. It is the building the game is named around
    // and the first interior most players will walk into, so it is furnished
    // properly -- plank floor, plastered walls, and the guild pack's own
    // interior set arranged into a room somebody could work in.
    //
    // The layout is deliberate rather than scattered. Walking in from the
    // south you face the guild master across the hall with the banners behind
    // him; the long tables run down the middle where members eat and argue;
    // the working walls -- weapons east, records west -- are to either side.
    {
        const int CELL = 32, cols = 24, rows = 17;
        MapBuilder m("guild_hall", "Havenbrook Guild Hall", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 18, 16);

        for (int cy = 0; cy < rows; ++cy)
            for (int cx = 0; cx < cols; ++cx) {
                const bool wall = (cx == 0 || cy == 0 || cx == cols - 1 || cy == rows - 1);
                const bool doorway = (cy == rows - 1 && cx >= cols / 2 - 1 && cx <= cols / 2 + 1);
                m.Ground(wall ? "guild_wall" : "guild_floor", cx * CELL, cy * CELL, CELL);
                if (wall && !doorway) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }

        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "town_havenbrook", "from_guild_hall",
                 "Step outside", false);

        // Furniture is drawn standing on its position and blocks a band along
        // its base rather than its whole footprint, so you can walk behind a
        // bookshelf's upper half the way you can walk behind a tree's canopy.
        auto furnish = [&](const string& id, const string& art,
                           int x, int y, int cw, int ch) {
            m.Prop("objects", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
            return id;
        };

        // --- the north wall: the guild master, his desk and the banners -----
        m.Prop("objects", "guild_banner", 8 * CELL, 2 * CELL);
        m.Prop("objects", "guild_banner", 16 * CELL, 2 * CELL);
        furnish("desk", "guild_desk", dx, 3 * CELL, 52, 14);
        m.Npc("npc_guildmaster", "Guild Master Orlend", "fighter2",
              dx, 2 * CELL - 8, "guildmaster_root", 0);

        // The last of the Spirewatch, sat under the west wall with a stick
        // across his knees. He has nothing to say to anyone who could not
        // survive the climb, and says it.
        // Art of his own (tools/blender_creatures.py, build_vask): the chair is
        // part of the sprite and rocks with him, so the floor under it is
        // blocked rather than the chair being a prop you can walk through.
        m.Npc("npc_elder", "Elder Vask", "vask", 5 * CELL, 11 * CELL, "elder_root", 0);
        m.Collision(5 * CELL - 22, 11 * CELL - 14, 44, 14);

        // The rug sits under him rather than in the middle of the room: it
        // marks where the hall expects you to stand and be spoken to.
        m.Flat("objects", "guild_rug", dx, 5 * CELL);

        // --- the west wall: records ------------------------------------------
        furnish("shelf_a", "guild_bookshelf",   2 * CELL + 16, 2 * CELL, 44, 14);
        furnish("shelf_b", "guild_bookshelf_b", 4 * CELL + 16, 2 * CELL, 44, 14);
        furnish("cabinet",  "guild_cabinet",    2 * CELL + 16, 6 * CELL, 44, 14);
        furnish("plant_w",  "guild_plant",      2 * CELL,      9 * CELL, 14, 8);

        // --- the east wall: arms ---------------------------------------------
        furnish("rack_a", "guild_weapon_rack", 21 * CELL, 3 * CELL, 44, 14);
        furnish("rack_b", "guild_armour_rack", 21 * CELL, 6 * CELL, 44, 14);
        furnish("rack_c", "guild_rack",        21 * CELL, 9 * CELL, 42, 14);
        furnish("plant_e", "guild_plant",      21 * CELL, 11 * CELL, 14, 8);

        // --- the middle: two long tables with benches either side ------------
        for (int i = 0; i < 2; ++i) {
            const int tx = (i == 0 ? 9 : 15) * CELL;
            furnish("table_" + std::to_string(i), "guild_table", tx, 9 * CELL, 52, 16);
            furnish("bench_" + std::to_string(i * 2),     "guild_bench", tx, 8 * CELL - 6, 44, 10);
            furnish("bench_" + std::to_string(i * 2 + 1), "guild_bench", tx, 11 * CELL, 44, 10);
        }

        // --- the south end: where people wait --------------------------------
        furnish("settle", "guild_settle", 6 * CELL, 13 * CELL, 44, 12);
        furnish("couch",  "guild_couch",  18 * CELL, 13 * CELL, 48, 14);
        furnish("chair_a", "guild_chair", 8 * CELL, 13 * CELL, 12, 8);
        furnish("chair_b", "guild_chair", 16 * CELL, 13 * CELL, 12, 8);

        // --- things you can actually use -------------------------------------
        {
            json& o = m.Object("board_guild", "sign", 4 * CELL, 12 * CELL);
            o["sprite"] = ObjPath("guild_noticeboard");
            o["title"]  = "Guild notices";
            o["text"]   = "DUES are payable at the turn of the season. The Guild "
                          "does not accept ore in lieu of coin. It has been asked.\n\n"
                          "THE SUNKEN ROAD is walked at your own risk past the second "
                          "milestone. Two parties have not come back. Neither filed a "
                          "route with the desk, which is the point of the desk.\n\n"
                          "THE BARROW is closed. By order of the Guild Master. "
                          "Enquiries to the Guild Master.";
            m.Collision(4 * CELL - 20, 12 * CELL - 10, 40, 10);
        }
        PlaceChest(m, "chest_guild", 20 * CELL, 13 * CELL, "chest_common");
        {
            json& o = m.Object("range_guild", "range", 3 * CELL, 4 * CELL);
            o["sprite"] = ObjPath("campfire");
            o["title"]  = "Guild hearth";
            m.Collision(3 * CELL - 16, 4 * CELL - 12, 32, 12);
        }

        m.Write("maps");
    }

    // The inn.
    //
    // The Barley and Bell is two floors joined by a flight of stairs up the
    // left-hand wall, the way a coaching inn is built: the taproom downstairs,
    // guest rooms upstairs. Each floor is its own map. Walking up the stairs
    // steps onto a portal at the top of the flight and arrives at the top of
    // the stairwell upstairs; walking back into the stairwell comes back down
    // to the foot of the flight. The two sets of stairs sit in the same place
    // on both floors, so the building stays one building in your head.
    //
    // Both flights are floor overlays rather than standing props. A standing
    // prop sorts by its base, and a player climbing a flight is north of that
    // base for the whole climb -- so they would be drawn behind the stairs they
    // are walking up.
    {
        const int CELL = 32, cols = 22, rows = 14;
        MapBuilder m("house_inn", "The Barley and Bell", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(26, 20, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor", "plaster_wall",
                  cols / 2 - 1, cols / 2);

        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "town_havenbrook", "from_house_inn",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        // --- the stairs up, against the left wall -----------------------------
        // The flight's art is 33px wide inside an 88px image and fills it top to
        // bottom, so centring the image at (47, 164) puts the flight in the
        // column x 32..65 running from y 120 at the top to 208 at the foot.
        m.Overlay("props", "stairs_up", 47, 164);
        m.Portal(34, 116, 30, 16, "house_inn_upper", "from_downstairs", "Go upstairs", false);
        m.Spawn("from_upstairs", 48, 236);
        // The banister, so the flight is climbed from its foot and not stepped
        // onto from the side halfway up; and the dead pocket above its head.
        m.Collision(64, 118, 6, 70);
        m.Collision(32, 64, 34, 50);

        // --- the hearth ----------------------------------------------------------
        m.Overlay("props", "inn_rug", 224, 109);
        {
            json& o = m.Object("range_inn", "range", 7 * CELL, 100);
            o["sprite"] = "assets/props/inn_fireplace.png";
            o["title"]  = "Kitchen fire";
            m.Collision(7 * CELL - 46, 70, 92, 30);
        }

        // --- the bar -------------------------------------------------------------
        piece("bottle_shelf", 470, 100, 60, 14);
        piece("keg_rack",     580, 110, 62, 16);
        piece("crates_sacks", 650, 108, 35, 14);
        m.Npc("npc_cook", "Innkeeper Bess", "citizen1", 512, 168, "cook_root", 0)["shop"] = "havenbrook_inn";
        piece("bar_counter",  512, 230, 108, 30);
        for (int i = 0; i < 3; ++i)
            piece("bar_stool", 478 + i * 34, 264, 14, 8);

        // --- the taproom -----------------------------------------------------------
        auto table_for_two = [&](int x, int y) {
            piece("tavern_table", x, y, 36, 12);
            piece("tavern_chair", x - 40, y + 2, 16, 8);
            piece("tavern_chair", x + 40, y + 2, 16, 8);
        };
        table_for_two(160, 272);
        table_for_two(270, 342);
        table_for_two(448, 344);
        piece("tavern_bench", 140, 404, 46, 12);
        piece("chalk_board",  404, 402, 22, 8);
        // A long table for a party, benches either side, in the far corner.
        piece("tavern_bench", 600, 320, 46, 12);
        piece("dining_table", 600, 352, 46, 14);
        piece("tavern_bench", 600, 398, 46, 12);
        piece("crates_sacks", 640, 250, 35, 14);

        // --- the cellar hatch, behind the bar ------------------------------------------
        m.Overlay("props", "cellar_hatch", 628, 180);
        m.Portal(606, 160, 36, 30, "house_inn_cellar", "from_inn", "Go down to the cellar", true);
        m.Spawn("from_cellar", 628, 222);

        m.Write("maps");
    }

    // The inn's cellar: kegs and grain and something living in them.
    {
        const int CELL = 32, cols = 20, rows = 13;
        MapBuilder m("house_inn_cellar", "The Inn Cellar", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Ambient("dungeon");
        m.Subtitle("Under the Barley and Bell");
        m.Background(16, 14, 12);
        RoomShell(m, cols, rows, CELL, "cellar_floor", "forge_wall");

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        // The steps back up, in the corner the hatch is above.
        m.Overlay("props", "stairs_up", 47, 164);
        m.Portal(34, 116, 30, 16, "house_inn", "from_cellar", "Climb back up", false);
        m.Spawn("from_inn", 48, 236);
        m.Spawn("default", 48, 236);
        m.Collision(64, 118, 6, 70);
        m.Collision(32, 64, 34, 50);

        piece("keg_rack",     260, 100, 62, 16);
        piece("keg_rack",     360, 100, 62, 16);
        piece("bottle_shelf", 470, 100, 60, 14);
        piece("crates_sacks", 560, 106, 35, 14);
        piece("barrel",       600, 180, 26, 10);
        piece("barrel",       600, 220, 26, 10);
        piece("crates_sacks", 180, 380, 35, 14);
        piece("barrel",       300, 390, 26, 10);
        piece("table_long",   330, 250, 88, 14);
        for (const auto& w : {std::pair<int, int>{96, 94}, {cols * CELL - 56, 94}, {cols * CELL - 56, rows * CELL - 64},
                              {400, rows * CELL - 64}})
            m.Prop("props", "cobweb", w.first, w.second);

        const int rats[][2] = {{180, 200}, {260, 320}, {420, 190}, {470, 330}, {150, 300}, {540, 280}};
        for (const auto& r : rats) m.Enemy("rat", r[0], r[1], 1, 30.0f, 220.0f);
        const int spiders[][3] = {{300, 150, 1}, {510, 160, 2}, {420, 370, 1}, {560, 360, 2}};
        for (const auto& sp : spiders) m.Enemy("spider", sp[0], sp[1], sp[2], 40.0f, 220.0f);
        m.Enemy("broodmother", cols * CELL - 96, rows * CELL - 90, 2, 120.0f, 260.0f);
        m.Write("maps");
    }

    // The inn, upstairs.
    //
    // A corridor along the back of the building and three guest rooms off it,
    // each behind its own door: a single room either end and the good room with
    // the double bed in the middle. The stairwell is in the same corner as the
    // stairs below it.
    {
        const int CELL = 32, cols = 22, rows = 14;
        MapBuilder m("house_inn_upper", "The Barley and Bell, upstairs",
                     cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(26, 20, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor", "plaster_wall");

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        // --- the stairwell --------------------------------------------------------
        // Its art spans x 24..64 and y 25..88 of an 88px image; centred at
        // (52, 129) the railing encloses x 32..72, y 110..173, open to the north.
        m.Overlay("props", "stairwell_down", 52, 129);
        m.Portal(36, 108, 26, 18, "house_inn", "from_upstairs", "Go downstairs", false);
        m.Spawn("from_downstairs", 48, 96);
        m.Spawn("default", 48, 96);
        m.Collision(32, 128, 34, 46);           // the well below the top step
        m.Collision(66, 110, 8, 64);            // the railing on its open side

        // --- the rooms -------------------------------------------------------------
        // A partition along row 5 with a doorway into each room, and two walls
        // between the rooms. The doorways sit in the middle column of each.
        const int part_row = 5;
        const int room_cols[3][2] = {{4, 8}, {10, 14}, {16, 20}};
        const int doors[3] = {6, 12, 18};
        for (int cx = 4; cx < cols - 1; ++cx) {
            const bool doorway = (cx == doors[0] || cx == doors[1] || cx == doors[2]);
            if (doorway) continue;
            m.Ground("plaster_wall", cx * CELL, part_row * CELL, CELL);
            m.Collision(cx * CELL, part_row * CELL, CELL, CELL);
        }
        for (int wx : {9, 15})
            for (int cy = part_row + 1; cy < rows - 1; ++cy) {
                m.Ground("plaster_wall", wx * CELL, cy * CELL, CELL);
                m.Collision(wx * CELL, cy * CELL, CELL, CELL);
            }
        for (int d : doors)
            m.Prop("props", "room_door", d * CELL + CELL / 2, (part_row + 1) * CELL);
        (void)room_cols;

        // Room one: a single, with a wardrobe.
        PlaceBed(m, "bed_inn_1", "bed_single", 150, 250, 30, 36, 15);
        piece("nightstand",   178, 214, 18, 8);
        piece("wardrobe",     258, 246, 34, 14);
        piece("washstand",    258, 330, 22, 8);
        piece("travel_chest", 150, 292, 28, 12);

        // Room two: the good room.
        m.Overlay("props", "inn_rug", 400, 326);
        PlaceBed(m, "bed_inn_2", "bed_double", 356, 258, 48, 36, 25);
        piece("nightstand",   448, 214, 18, 8);
        piece("washstand",    452, 322, 22, 8);
        piece("travel_chest", 356, 300, 28, 12);

        // Room three: another single.
        PlaceBed(m, "bed_inn_3", "bed_single", 556, 250, 30, 36, 15);
        piece("nightstand",   522, 214, 18, 8);
        piece("wardrobe",     650, 246, 34, 14);
        piece("washstand",    650, 330, 22, 8);
        piece("travel_chest", 556, 292, 28, 12);

        // --- the corridor ---------------------------------------------------------
        m.Overlay("props", "inn_rug", 400, 92);
        piece("herb_pots",    650, 96, 30, 10);
        piece("nightstand",   300, 90, 18, 8);
        // A linen corner under the stairwell.
        piece("crates_sacks",  80, 392, 35, 14);
        piece("travel_chest",  80, 300, 28, 12);

        m.Write("maps");
    }

    // The forge.
    //
    // Two halves, the way a working smithy that also sells is laid out: the hot
    // end at the back, where the forge stands against the chimney wall with its
    // bellows, anvil and quenching trough around it; and the shop at the front,
    // where the counter faces the door and the finished work is on show. The
    // smith stands between them, behind the counter.
    //
    // Every prop here is original, modelled in tools/blender_props.py and
    // reduced by tools/make_props.ps1.
    {
        const int CELL = 32, cols = 18, rows = 12;
        MapBuilder m("house_smith", "Halda's Forge", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 16, 12);

        const int door_l = cols / 2 - 1, door_r = cols / 2;
        for (int cy = 0; cy < rows; ++cy)
            for (int cx = 0; cx < cols; ++cx) {
                // The back wall is two courses tall, so it reads as a wall seen
                // from inside the room rather than as a strip along the edge.
                const bool back   = (cy <= 1);
                const bool side   = (cx == 0 || cx == cols - 1);
                const bool front  = (cy == rows - 1);
                const bool door   = front && cx >= door_l && cx <= door_r;
                // The chimney breast behind the forge is brick.
                const bool breast = back && cx >= 3 && cx <= 7;

                string tile;
                if (door)                         tile = "forge_floor";
                else if (breast)                  tile = "forge_brick";
                else if (back || side || front)   tile = "forge_wall";
                else tile = ((cx * 7 + cy * 13) % 3 == 0) ? "forge_floor_1"
                          : ((cx * 5 + cy * 3) % 4 == 0) ? "forge_floor_2" : "forge_floor";
                m.Ground(tile, cx * CELL, cy * CELL, CELL);

                if ((back || side || front) && !door)
                    m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }

        const int dx = cols / 2 * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "town_havenbrook", "from_house_smith",
                 "Step outside", false);

        // A prop stands on its position and blocks a band along its base, so
        // the upper part of a tall piece overlaps the player the way a tree's
        // canopy does rather than walling off the space in front of it.
        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        // --- the hot end ----------------------------------------------------
        // The forge itself is also the smelting range, so it is an object
        // rather than scenery; its sprite comes from the same prop art.
        {
            const int fx = 5 * CELL, fy = 5 * CELL - 4;
            json& o = m.Object("range_forge", "range", fx, fy);
            o["sprite"] = "assets/props/forge.png";
            o["title"]  = "Forge";
            m.Collision(fx - 30, fy - 26, 60, 26);
        }
        // Bellows on the left, nozzle toward the fire -- the prop is modelled
        // blowing to its right, and the first layout put it on the forge's
        // right, blowing air at the wall.
        piece("bellows",       2 * CELL + 10, 4 * CELL + 26, 44, 12);
        piece("coal_bin",      8 * CELL - 2, 4 * CELL + 30, 28, 12);
        piece("tool_rack",    11 * CELL,     2 * CELL + 30, 48, 10);
        piece("grindstone",   14 * CELL + 4, 4 * CELL + 28, 26, 12);
        piece("quench_trough", 2 * CELL + 14, 7 * CELL + 24, 42, 14);
        piece("ingot_crate",   8 * CELL + 6, 7 * CELL + 24, 34, 12);

        // The anvil is where things are made, so it is the crafting bench.
        {
            const int ax = 5 * CELL + 10, ay = 7 * CELL + 20;
            json& o = m.Object("bench_forge", "workbench", ax, ay);
            o["sprite"]  = "assets/props/anvil.png";
            o["title"]   = "Anvil";
            o["station"] = "anvil";
            m.Collision(ax - 14, ay - 12, 28, 12);
        }

        // --- the shop -------------------------------------------------------
        piece("shop_counter", 13 * CELL + 8, 8 * CELL + 8, 76, 16);
        m.Npc("npc_smith", "Smith Halda", "citizen2", 13 * CELL + 8, 6 * CELL + 28,
              "smith_root", 0)["shop"] = "havenbrook_forge";
        piece("armour_stand",  16 * CELL + 8, 4 * CELL + 30, 24, 10);
        piece("weapon_barrel", 16 * CELL + 10, 7 * CELL + 10, 18, 10);
        // Kept clear of the doorway, which runs up the middle of the room.
        piece("weapon_barrel",  4 * CELL,     10 * CELL + 12, 18, 10);
        piece("ingot_crate",   15 * CELL + 20, 10 * CELL + 18, 34, 12);

        m.Write("maps");
    }
}

// --- dungeons ----------------------------------------------------------------

struct Room { int x, y, w, h; };   // in cells

// Carves rooms joined by L-shaped corridors, then walls in everything that was
// not carved. Simple, readable, and it always produces a connected floor.
static void BuildDungeon(const string& id, const string& display,
                         unsigned seed, int cols, int rows, int room_count,
                         const string& floor_tile, const string& wall_tile,
                         const string& exit_map, const string& exit_spawn,
                         const vector<std::pair<string, int>>& monsters,
                         const string& chest_table, int chest_count,
                         const string& special_id, const string& special_table,
                         const string& deeper_map, const string& deeper_lock,
                         const string& boss_type = "", int boss_level = 1,
                         const vector<std::pair<string, int>>& ores = {},
                         int lava_vents = 0,
                         const string& relic_id = "", const string& relic_item = "",
                         const string& relic_quest = "") {
    const int CELL = 32;
    MapBuilder m(id, display, cols * CELL, rows * CELL);
    m.Interior(true);
    m.Ambient("dungeon");
    m.Background(12, 12, 18);
    // The crypt's air lies on its floors, grey and cold, thicker the deeper.
    if (id.rfind("crypt_", 0) == 0) m.Fog(id == "crypt_1" ? 0.30f : id == "crypt_2" ? 0.36f : 0.42f, 0.0f, {150, 158, 170});
    std::mt19937 rng(seed);

    vector<vector<bool>> floor(rows, vector<bool>(cols, false));
    vector<Room> rooms;

    for (int attempt = 0; attempt < room_count * 12 &&
                          static_cast<int>(rooms.size()) < room_count; ++attempt) {
        Room r;
        r.w = 5 + static_cast<int>(rng() % 7);
        r.h = 4 + static_cast<int>(rng() % 6);
        r.x = 2 + static_cast<int>(rng() % std::max(1, cols - r.w - 4));
        r.y = 2 + static_cast<int>(rng() % std::max(1, rows - r.h - 4));

        bool clash = false;
        for (const Room& o : rooms)
            if (r.x < o.x + o.w + 2 && o.x < r.x + r.w + 2 &&
                r.y < o.y + o.h + 2 && o.y < r.y + r.h + 2) { clash = true; break; }
        if (clash) continue;

        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) floor[y][x] = true;
        rooms.push_back(r);
    }

    for (size_t i = 1; i < rooms.size(); ++i) {
        const int ax = rooms[i - 1].x + rooms[i - 1].w / 2;
        const int ay = rooms[i - 1].y + rooms[i - 1].h / 2;
        const int bx = rooms[i].x + rooms[i].w / 2;
        const int by = rooms[i].y + rooms[i].h / 2;
        for (int x = std::min(ax, bx); x <= std::max(ax, bx); ++x) {
            floor[ay][x] = true;
            floor[std::min(rows - 1, ay + 1)][x] = true;
        }
        for (int y = std::min(ay, by); y <= std::max(ay, by); ++y) {
            floor[y][bx] = true;
            floor[y][std::min(cols - 1, bx + 1)] = true;
        }
    }

    // Vents of lava in the floor, in the corridors and away from the middle of
    // each room, never in the room you walk in through: they burn while stood
    // on, so the way through is picked, or jumped.
    std::set<std::pair<int, int>> vents;
    if (lava_vents > 0 && rooms.size() > 1) {
        vector<std::pair<int, int>> spots;
        const Room& first = rooms.front();
        for (int cy = 1; cy < rows - 1; ++cy)
            for (int cx = 1; cx < cols - 1; ++cx) {
                if (!floor[cy][cx]) continue;
                if (cx >= first.x - 3 && cx < first.x + first.w + 3 && cy >= first.y - 3 && cy < first.y + first.h + 3) continue;
                bool centre = false;
                for (const Room& r : rooms)
                    if (abs(cx - (r.x + r.w / 2)) <= 1 && abs(cy - (r.y + r.h / 2)) <= 1) centre = true;
                if (!centre) spots.push_back({cx, cy});
            }
        std::shuffle(spots.begin(), spots.end(), rng);
        for (const auto& sp : spots) {
            if (static_cast<int>(vents.size()) >= lava_vents) break;
            bool next_to = false;
            for (const auto& v : vents) if (abs(v.first - sp.first) <= 1 && abs(v.second - sp.second) <= 1) next_to = true;
            if (!next_to) vents.insert(sp);
        }
    }

    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            if (vents.count({cx, cy})) {
                m.Ground(VariantOf("lava", cx, cy), cx * CELL, cy * CELL, CELL);
                m.Hazard(cx * CELL + 4, cy * CELL + 4, CELL - 8, CELL - 8, 10.0f);
            } else if (floor[cy][cx]) {
                const float v = Fbm(cx * 0.3f, cy * 0.3f, static_cast<int>(seed));
                m.Ground(v > 0.55f ? floor_tile : (floor_tile + "_dark"),
                         cx * CELL, cy * CELL, CELL);
            } else {
                m.Ground(wall_tile, cx * CELL, cy * CELL, CELL);
                m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }
        }

    if (rooms.empty()) { m.Write("maps"); return; }

    // The way out is a stone flight climbing into the first room's top wall
    // toward daylight, and the way down a stairwell in the floor of the last
    // (tools/blender_props.py). Both were door sprites standing in the middle
    // of a room.
    const Room& first = rooms.front();
    // A column of the top wall with solid rock above it either side, so the
    // flight never stands across a corridor coming in from the north.
    int stair_cx = first.x + first.w / 2;
    for (int d = 0; d < first.w / 2; ++d) {
        bool found = false;
        for (int c : {first.x + first.w / 2 - d, first.x + first.w / 2 + d}) {
            if (c - 1 < first.x || c + 1 >= first.x + first.w || first.y < 1) continue;
            if (!floor[first.y - 1][c - 1] && !floor[first.y - 1][c] && !floor[first.y - 1][c + 1]) {
                stair_cx = c; found = true; break;
            }
        }
        if (found) break;
    }
    const int ex = stair_cx * CELL + 16;
    const int stair_base = first.y * CELL + 72;
    const int ey = stair_base + 22;
    m.Spawn("entrance", ex, ey);
    m.Spawn("default",  ex, ey);
    m.Prop("props", "dungeon_stairs_up", ex, stair_base);
    m.SortLift("dungeon_stairs_up", 90);
    m.Portal(ex - 20, stair_base - 38, 40, 36, exit_map, exit_spawn, "Leave", true);
    // The walls of the flight, and its upper steps.
    m.Collision(ex - 26, stair_base - 72, 52, 34);
    m.Collision(ex - 26, stair_base - 38, 6, 38);
    m.Collision(ex + 20, stair_base - 38, 6, 38);

    if (!deeper_map.empty() && rooms.size() > 1) {
        const Room& last = rooms.back();
        const int dx = (last.x + last.w / 2) * CELL + 16;
        const int dy = (last.y + last.h / 2) * CELL + 16;
        // North of the room's middle, clear of the quest chest in front of it.
        m.Prop("props", "dungeon_stairs_down", dx, dy + 2);
        m.SortLift("dungeon_stairs_down", 40);
        m.Portal(dx - 24, dy - 44, 48, 40, deeper_map, "entrance",
                 "Descend", true, deeper_lock);
    }

    // Monsters everywhere but the room you walk in through.
    int placed = 0;
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& r = rooms[i];
        const int count = 1 + static_cast<int>(rng() % 3);
        for (int k = 0; k < count && !monsters.empty(); ++k) {
            const auto& mon = monsters[rng() % monsters.size()];
            int x = (r.x + 1 + static_cast<int>(rng() % std::max(1, r.w - 2))) * CELL + 16;
            int y = (r.y + 1 + static_cast<int>(rng() % std::max(1, r.h - 2))) * CELL + 16;
            // Not stood in a vent.
            if (vents.count({x / CELL, y / CELL})) { x = (r.x + r.w / 2) * CELL + 16; y = (r.y + r.h / 2) * CELL + 16; }
            m.Enemy(mon.first, x, y, mon.second, 40.0f, 320.0f);
            ++placed;
        }
    }
    (void)placed;

    for (int i = 0; i < chest_count && rooms.size() > 1; ++i) {
        const Room& r = rooms[1 + (rng() % (rooms.size() - 1))];
        const int x = (r.x + 1 + static_cast<int>(rng() % std::max(1, r.w - 2))) * CELL + 16;
        const int y = (r.y + 1 + static_cast<int>(rng() % std::max(1, r.h - 2))) * CELL + 16;
        PlaceChest(m, id + "_chest_" + std::to_string(i), x, y, chest_table);
    }

    // Seams of the deeper tiers, one to a room, never in the room you arrive in.
    if (!ores.empty() && rooms.size() > 1) {
        int n = 0;
        for (size_t i = 1; i < rooms.size(); ++i) {
            const Room& r = rooms[i];
            const auto& ore = ores[(i - 1) % ores.size()];
            // Tucked against the top wall of the room, out of the way.
            const int x = (r.x + 1 + static_cast<int>(rng() % std::max(1, r.w - 2))) * CELL + 16;
            const int y = r.y * CELL + 30;
            PlaceRock(m, rng, 900 + n++, x, y, true, ore.second, ore.first);
        }
    }

    // The boss holds the last room on its own.
    if (!boss_type.empty()) {
        const Room& r = rooms.back();
        m.Enemy(boss_type, (r.x + r.w / 2) * CELL + 16,
                (r.y + r.h / 2) * CELL + 16, boss_level, 0.0f, 900.0f);
    }

    // The quest chest goes in the furthest room from the entrance.
    if (!special_id.empty()) {
        const Room& r = rooms.back();
        const int x = (r.x + r.w / 2) * CELL + 16;
        const int y = (r.y + r.h / 2) * CELL + 48;
        PlaceChest(m, special_id, x, y, special_table);
    }

    // The relic, in the chamber furthest in that is not the boss's: a chest
    // that is only there while the quest that sends you for it is running.
    if (!relic_id.empty() && rooms.size() > 2) {
        const Room& r = rooms[rooms.size() - 2];
        // Against the wall, not in the middle: the room's centre is where the
        // ordinary chests and the ore go, and two chests on one tile read as
        // one chest.
        int x = (r.x + 1) * CELL + 16, y = (r.y + 1) * CELL + 16;
        static const int kTry[][2] = {{0, 0}, {1, 0}, {0, 1}, {2, 0}, {0, 2}, {1, 1}};
        for (const auto& t : kTry) {
            const int tx = (r.x + 1 + t[0]) * CELL + 16, ty = (r.y + 1 + t[1]) * CELL + 16;
            if (m.Clear(tx, ty + 12)) { x = tx; y = ty; break; }
        }
        PlaceRelicChest(m, relic_id, x, y, relic_item, relic_quest);
    }

    // Where climbing back up from the level below comes out: beside these
    // stairs, not back at this level's own entrance a map away. Chosen last,
    // once everything solid in the room is down -- the quest chest sits right
    // in front of the stairs, so a fixed offset landed inside it.
    if (!deeper_map.empty() && rooms.size() > 1) {
        const Room& last = rooms.back();
        const int dx = (last.x + last.w / 2) * CELL + 16;
        const int dy = (last.y + last.h / 2) * CELL + 16;
        static const int kTry[][2] = {{0, 40}, {-40, 32}, {40, 32}, {-44, 0}, {44, 0},
                                      {0, 72}, {-72, 40}, {72, 40}, {-40, -32}, {40, -32}};
        int sx = dx, sy = dy + 40;
        for (const auto& t : kTry)
            if (m.Clear(dx + t[0], dy + t[1])) { sx = dx + t[0]; sy = dy + t[1]; break; }
        m.Spawn("from_below", sx, sy);
    }

    PlaceCurios(m);
    m.Write("maps");
}

// =============================================================================
//  The well of Havenbrook
//
//  Two floors cut under the square, both pitch dark: what you can see down
//  here is what you carry, which is the whole point of the lantern. Each floor
//  is four chambers round a hub, one kind of thing to a chamber, so a fight is
//  with the slimes or with the bats and not with both at once -- and the mobs
//  are spaced on a lattice with short leashes, so a room is crossed a fight at
//  a time rather than in one running battle.
// =============================================================================

struct WellMob { const char* type; int level; };

// Cave growth, and only the small sprites: the big mushrooms are as tall as a
// tree and a player standing behind one vanishes, which in a dark map where
// the player is the light source reads as the game having lost them.
static const char* kCaveFungus[] = {
    "mushroom_02", "fungus_00", "fungus_01", "fungus_02"
};

static void BuildWellFloor(const string& id, const string& display, const string& subtitle,
                           unsigned seed, int cols, int rows,
                           const string& floor_tile, const string& wall_tile,
                           const string& up_map, const string& up_spawn, const string& down_map,
                           const vector<vector<WellMob>>& quadrants, int per_quadrant,
                           bool deep) {
    const int CELL = 32;
    MapBuilder m(id, display, cols * CELL, rows * CELL);
    m.Interior(true);
    m.Ambient("dungeon");
    m.Background(6, 6, 9);
    m.Subtitle(subtitle);
    m.dq["dark"] = true;                       // no light of its own
    std::mt19937 rng(seed);

    const int hx = cols / 2, hy = rows / 2;
    const int qx = cols / 4, qy = rows / 4;    // how far a chamber sits from the hub
    const float rx = cols * 0.19f, ry = rows * 0.19f;

    // The four chambers, NW NE SW SE, and the hub they open onto.
    const std::pair<int, int> centres[4] = {{hx - qx, hy - qy}, {hx + qx, hy - qy},
                                            {hx - qx, hy + qy}, {hx + qx, hy + qy}};
    const auto in_chamber = [&](int q, int cx, int cy) {
        const float dx = (cx - centres[q].first) / rx, dy = (cy - centres[q].second) / ry;
        return dx * dx + dy * dy <= 1.0f + (Fbm(cx * 0.16f, cy * 0.16f, static_cast<int>(seed) + q) - 0.5f) * 0.5f;
    };
    const auto in_hub = [&](int cx, int cy) {
        const float dx = (cx - hx) / (cols * 0.09f), dy = (cy - hy) / (rows * 0.10f);
        return dx * dx + dy * dy <= 1.0f;
    };
    // The spring lies at the bottom of the shaft, in a room of its own down a
    // long passage south of the hub. It is deliberately far from the stairs:
    // arriving on a floor and finding the boss already within reach gives the
    // player no room to see it coming.
    const int sprx = hx, spry = rows - 10;
    const float spr_r = 6.5f;
    const auto in_spring = [&](int cx, int cy) {
        const float dx = (cx - sprx) / spr_r, dy = (cy - spry) / (spr_r * 0.86f);
        return dx * dx + dy * dy <= 1.0f;
    };
    const auto in_corridor = [&](int cx, int cy) {
        for (int q = 0; q < 4; ++q) {
            const int ccx = centres[q].first, ccy = centres[q].second;
            // An L from the hub: along x at the hub's row, then down to the chamber.
            if (abs(cy - hy) <= 1 && cx >= std::min(hx, ccx) && cx <= std::max(hx, ccx)) return true;
            if (abs(cx - ccx) <= 1 && cy >= std::min(hy, ccy) && cy <= std::max(hy, ccy)) return true;
        }
        // And, on the deep floor, the passage down to the spring.
        if (deep && abs(cx - sprx) <= 1 && cy >= hy && cy <= spry) return true;
        return false;
    };
    const auto open = [&](int cx, int cy) {
        if (cx < 2 || cy < 2 || cx >= cols - 2 || cy >= rows - 2) return false;
        if (in_hub(cx, cy) || in_corridor(cx, cy)) return true;
        if (deep && in_spring(cx, cy)) return true;
        for (int q = 0; q < 4; ++q) if (in_chamber(q, cx, cy)) return true;
        return false;
    };

    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            if (!open(cx, cy)) {
                m.Ground(wall_tile, cx * CELL, cy * CELL, CELL);
                m.Collision(cx * CELL, cy * CELL, CELL, CELL);
                continue;
            }
            const float v = Fbm(cx * 0.3f, cy * 0.3f, static_cast<int>(seed) + 11);
            // Standing water in the deep cut: it is a well, after all.
            if (deep && v > 0.72f) {
                m.Ground(VariantOf("bog_water", cx, cy), cx * CELL, cy * CELL, CELL);
                m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else {
                m.Ground(v > 0.55f ? floor_tile : (floor_tile + "_dark"), cx * CELL, cy * CELL, CELL);
            }
        }

    // Rubble, webs and fungus, thickest against the walls.
    int rock_i = 0;
    for (int cy = 3; cy < rows - 3; ++cy)
        for (int cx = 3; cx < cols - 3; ++cx) {
            if (!open(cx, cy)) continue;
            // The floor round the spring stays bare: the fight there needs room
            // to circle, and a rock in the way of the basin reads as a bug.
            if (deep && abs(cx - sprx) <= 4 && abs(cy - spry) <= 4) continue;
            const bool wall = !open(cx + 1, cy) || !open(cx - 1, cy) || !open(cx, cy + 1) || !open(cx, cy - 1);
            const float r = Hash2(cx, cy, 7711);
            const int x = cx * CELL + 16, y = cy * CELL + 20;
            if (wall && r < 0.16f) {
                m.Prop("objects", kSmallRocks[(cx + cy) % 4], x, y);
                m.Collision(x - 10, y - 8, 20, 8);
            } else if (wall && r < 0.24f) {
                m.Prop("props", "cobweb", x, y);
            } else if (!wall && r < 0.05f) {
                m.Prop("objects", Pick(kCaveFungus, rng), x, y);
            } else if (!wall && r < 0.065f) {
                PlaceRock(m, rng, 800 + rock_i++, x, y, false, deep ? 20 : 1, deep ? "coal" : "copper_ore");
            }
        }

    // The way up, on the hub's north side, and the way down on its south.
    const int ux = hx * CELL + 16, uy = (hy - 3) * CELL + 16;
    m.Spawn("entrance", ux, uy + 44);
    m.Spawn("default",  ux, uy + 44);
    m.Prop("props", "dungeon_stairs_up", ux, uy);
    m.SortLift("dungeon_stairs_up", 90);
    m.Spawn("from_above", ux, uy + 44);
    m.Portal(ux - 20, uy - 38, 40, 36, up_map, up_spawn, "Climb back up", true);
    m.Collision(ux - 26, uy - 72, 52, 34);

    if (!down_map.empty()) {
        const int dx2 = hx * CELL + 16, dy2 = (hy + 3) * CELL + 16;
        m.Prop("props", "dungeon_stairs_down", dx2, dy2 + 26);
        m.SortLift("dungeon_stairs_down", 40);
        m.Portal(dx2 - 24, dy2 - 16, 48, 40, down_map, "from_above", "Go deeper", true);
        // Climbing back up puts you beside these stairs, not at the shaft you
        // came in by, which is half the hub away.
        m.Spawn("from_below", dx2, dy2 + 44);
    }

    // One kind of thing to a chamber, spaced three cells apart.
    for (int q = 0; q < 4; ++q) {
        const vector<WellMob>& kinds = quadrants[q];
        if (kinds.empty()) continue;
        int placed = 0;
        for (int cy = centres[q].second - 9; cy <= centres[q].second + 9 && placed < per_quadrant; cy += 3)
            for (int cx = centres[q].first - 11; cx <= centres[q].first + 11 && placed < per_quadrant; cx += 3) {
                if (!in_chamber(q, cx, cy) || in_corridor(cx, cy)) continue;
                const int x = cx * CELL + 16, y = cy * CELL + 16;
                if (!m.Clear(x, y)) continue;
                if (Hash2(cx, cy, 8811) > 0.45f) continue;
                const WellMob& mob = kinds[static_cast<size_t>(Hash2(cx, cy, 8822) * 100.0f) % kinds.size()];
                // Short leashes: nothing follows you out of its own chamber.
                m.Enemy(mob.type, x, y, mob.level, 45.0f, 150.0f);
                ++placed;
            }
        // Something worth the walk in two of the four.
        if (q % 3 == 0) {
            const int x = centres[q].first * CELL + 16, y = centres[q].second * CELL + 16;
            if (m.Clear(x, y)) PlaceChest(m, id + "_chest_" + std::to_string(q), x, y, "chest_well");
        }
    }

    // The bottom of it: the spring at the far end of the south passage, with
    // the thing that has been sitting in it standing over the basin. The room
    // is its own, and empty otherwise -- the fight wants floor to move on.
    if (deep) {
        const int sx = sprx * CELL + 16, sy = spry * CELL + 20;
        m.Prop("props", "spring_basin", sx, sy);
        // A low kerb, not a wall: the first cut of this room put a full-width
        // collision across the basin and the warden stood behind it, unable to
        // reach the player and unreachable in turn -- a boss fight fought
        // through a fence.
        m.Collision(sx - 26, sy - 8, 52, 10);
        // What is actually in the way of the water: a plug of fallen stone in
        // the outflow. A note sprite sat here first and read as a letter lying
        // on the floor of a cave.
        json& o = m.Object("spring_well", "lever", sx, sy + 26);
        o["sprite"] = "assets/objects/rocksmall_02.png";
        o["title"]  = "The choked spring";
        // It waits beside the basin, with clear floor between it and the way
        // in, and does not leave the room: the leash is the room's width.
        m.Enemy("well_warden", sx + 104, sy + 24, 1, 0.0f, 300.0f);
        // Two hounds kennelled in the passage itself -- it is three cells wide,
        // so they stand in the middle of it -- and the walk down is not silent.
        for (int cy : {spry - 10, spry - 16}) {
            const int hx2 = sprx * CELL + 16, hy2 = cy * CELL + 16;
            if (m.Clear(hx2, hy2)) m.Enemy("hound", hx2, hy2, 2, 40.0f, 150.0f);
        }
    }

    m.Write("maps");
}

// =============================================================================
//  Ice Spire Peak
//
//  North out of the foothills a goat track climbs switchbacks through snow,
//  between cliffs of frost-riven rock and spires of blue ice. Ice trolls hold
//  the middle slopes and frost wyverns nest round the summit, where the great
//  spire stands and the matriarch of the nests with it. A camp at the foot is
//  a safe place to rest, cook and sleep. The track is wide, the monsters are
//  spaced out along it and do not chase far, so it can be climbed a fight at a
//  time -- hard, not hopeless.
// =============================================================================

namespace peak {
static const int CELL = 32, W = 56, H = 84;
static float PathX(float cy) { return 28.0f + sinf(cy * 0.11f) * 12.0f + sinf(cy * 0.037f + 1.0f) * 3.0f; }
// How far a cell is from the track, in cells; the foot and the summit open out.
static float Gap(int cx, int cy) { return fabsf(cx - PathX(static_cast<float>(cy))); }
static float Width(int cy) {
    float w = 7.0f;
    if (cy > H - 12) w += (cy - (H - 12)) * 1.2f;      // the camp at the foot
    if (cy < 12)     w += (12 - cy) * 1.0f;            // the summit
    return w;
}
// The way west to the Frostreach: a gap in the cliffs off the track, a third
// of the way up, from the track to the edge of the map.
static const int FROST_ROW = 34;
static bool Open(int cx, int cy) {
    if (cx < 1 || cy < 1 || cx >= W - 1 || cy >= H) return false;
    if (abs(cy - FROST_ROW) <= 2 && cx < PathX(static_cast<float>(cy))) return true;
    return Gap(cx, cy) < Width(cy) + (Fbm(cx * 0.3f, cy * 0.3f, 8484) - 0.5f) * 3.0f;
}
}   // namespace peak

static void BuildIceSpire() {
    using namespace peak;
    MapBuilder m("ice_spire_peak", "Ice Spire Peak", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("Where the wyverns nest above the clouds");
    m.Background(206, 220, 232);
    std::mt19937 rng(3131u);
    int rock_i = 0;

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.2f, cy * 0.2f, 8585);
            string tile;
            if (!Open(cx, cy)) {
                tile = "crag";
                // The exit at the foot of the track stays open, and the way west.
                const bool exit = (cy == H - 1 && Gap(cx, cy) < 3.0f) || (cx == 0 && abs(cy - FROST_ROW) <= 1);
                if (!exit) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else if (Gap(cx, cy) < 1.4f) {
                tile = "frost_rock";                       // the trodden track
            } else {
                tile = v > 0.64f ? "ice" : "snow";
            }
            m.Ground(VariantOf(tile, cx, cy), cx * CELL, cy * CELL, CELL);
        }

    // The cliffs: pines and ice along their edges, so a wall of rock reads as
    // a mountainside rather than a grey border.
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (Open(cx, cy)) continue;
            const bool edge = Open(cx + 1, cy) || Open(cx - 1, cy) || Open(cx, cy + 1) || Open(cx, cy - 1);
            const float r = Hash2(cx, cy, 8686);
            const int x = cx * CELL + 16, y = cy * CELL + 28;
            if (edge) {
                if (r < 0.18f)      m.Prop("props", "ice_spire", x, y);
                else if (r < 0.55f) m.Prop("props", "snow_pine", x, y);
                else if (r < 0.68f) m.Prop("props", "ice_crystal", x, y);
            } else if (r < 0.10f) {
                m.Prop("props", "snow_pine", x, y);
            }
        }
    // Crystals scattered in the snow, off the track.
    for (int cy = 2; cy < H - 2; ++cy)
        for (int cx = 2; cx < W - 2; ++cx) {
            if (!Open(cx, cy) || Gap(cx, cy) < 2.5f) continue;
            if (!Open(cx + 1, cy) || !Open(cx - 1, cy) || !Open(cx, cy + 1) || !Open(cx, cy - 1)) continue;
            if (Hash2(cx, cy, 8787) < 0.03f) {
                const int x = cx * CELL + 16, y = cy * CELL + 20;
                m.Prop("props", "ice_crystal", x, y);
                m.Collision(x - 8, y - 6, 16, 6);
            }
        }

    // --- the camp at the foot -----------------------------------------------------
    const int fx = static_cast<int>(PathX(static_cast<float>(H - 3)) * CELL) + 16;
    const int fy = (H - 3) * CELL;
    m.Portal(fx - 96, (H - 1) * CELL, 192, 32, "overworld", "from_peak", "Down to the Hollowmarch", false);
    m.Spawn("from_hollowmarch", fx, fy);
    m.Spawn("default", fx, fy);
    m.Spawn("respawn", fx, fy);
    PlaceCampsite(m, "campsite_peak", fx - 120, fy - 60);
    m.Collision(fx - 150, fy - 76, 60, 16);
    {
        json& o = m.Object("range_peak", "range", fx + 110, fy - 40);
        o["sprite"] = "assets/props/campfire_ring.png";
        o["title"]  = "Camp fire";
        m.Collision(fx + 94, fy - 50, 32, 10);
        json& sign = m.Object("sign_peak_camp", "sign", fx + 60, fy - 110);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "Climbers' stone";
        sign["text"]   = "LAST FIRE BEFORE THE SPIRE.\n\n"
                         "Trolls from the first bend. They are slow; do not let them close.\n"
                         "Wyverns above the frozen pools. They are not slow.\n\n"
                         "Rest here. Go up one fight at a time.";
        m.Collision(fx + 44, fy - 120, 32, 10);
    }

    // --- the way west, to the Frostreach ------------------------------------------------------
    {
        m.Portal(0, FROST_ROW * CELL - 64, 24, 160, "frost_barrows", "from_spire", "West to the Frostreach", false);
        m.Danger(60);
        m.Spawn("from_frostreach", 3 * CELL + 16, FROST_ROW * CELL + 16);
        // Two runestones either side of where it leaves the track, and a stone
        // at the mouth of it that says where it goes.
        const int mx = static_cast<int>(PathX(static_cast<float>(FROST_ROW))) - 3;
        for (int s : {-1, 1}) {
            const int x = mx * CELL + 16, y = (FROST_ROW + s * 3) * CELL + (s > 0 ? 8 : 28);
            m.Prop("props", "runestone", x, y);
            m.Collision(x - 13, y - 10, 26, 10);
        }
        json& o = m.Object("sign_peak_frostreach", "sign", (mx - 3) * CELL, (FROST_ROW - 2) * CELL + 8);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A runestone, and words under the runes";
        o["text"]   = "WEST: THE FROSTREACH\n\nThe barrows of the mountain's old dead, the Glass Mere, the "
                      "glacier, and the Howe of the barrow-kings. Sixty, and up to seventy-five.\n\n"
                      "And something white and tall that is not a troll. Do not follow the tracks.";
        m.Collision((mx - 3) * CELL - 16, (FROST_ROW - 2) * CELL - 2, 32, 10);
    }
    // Snowmen at the camp, and an igloo somebody built and left.
    {
        const int ix = fx + 200, iy = fy - 96;
        if (m.Clear(ix, iy) && m.Clear(ix - 40, iy) && m.Clear(ix + 40, iy)) {
            m.Prop("props", "igloo", ix, iy);
            m.Collision(ix - 42, iy - 66, 84, 46);
            m.Collision(ix - 21, iy - 20, 42, 14);
        }
        for (const auto& sm : {std::pair<int, int>{150, 10}, {-190, -20}, {-60, -150}})
            if (m.Clear(fx + sm.first, fy + sm.second) && m.Clear(fx + sm.first - 10, fy + sm.second)) {
                m.Prop("props", "snowman", fx + sm.first, fy + sm.second);
                m.Collision(fx + sm.first - 10, fy + sm.second - 15, 20, 11);
            }
    }

    // Monsters on and beside the track at a given height, stood on open snow.
    const auto beside = [&](int cy, int side) {
        for (int d = 3; d >= 0; --d) {
            const int cx = static_cast<int>(PathX(static_cast<float>(cy))) + side * d;
            if (Open(cx, cy) && Open(cx + 1, cy) && Open(cx - 1, cy)) return std::pair<int, int>{cx * CELL + 16, cy * CELL + 16};
        }
        return std::pair<int, int>{static_cast<int>(PathX(static_cast<float>(cy)) * CELL) + 16, cy * CELL + 16};
    };
    // Ice trolls on the middle slopes.
    const int trolls[][3] = {{66, 1, 1}, {60, -1, 1}, {54, 1, 2}, {48, -1, 2}, {43, 1, 3}, {38, -1, 3}, {33, 1, 4}};
    for (const auto& t : trolls) {
        const auto [x, y] = beside(t[0], t[1]);
        m.Enemy("ice_troll", x, y, t[2], 50.0f, 200.0f);
    }
    // Ore in the rock the trolls guard, and orichalcum a little higher: it is
    // the tier between damascus and diamond, so it is mined between the ground
    // that yields one and the ground that yields the other.
    for (int k = 0; k < 3; ++k) {
        const auto [x, y] = beside(58 - k * 9, k % 2 ? 1 : -1);
        PlaceRock(m, rng, 700 + rock_i++, x + (k % 2 ? 40 : -40), y, true, 40, "damascus_ore");
    }
    for (int k = 0; k < 3; ++k) {
        const auto [x, y] = beside(40 - k * 7, k % 2 ? -1 : 1);
        PlaceRock(m, rng, 760 + rock_i++, x + (k % 2 ? -44 : 44), y, true, 50, "orichalcum_ore");
    }
    // Wyverns and their nests round the summit.
    const int wyverns[][3] = {{27, -1, 1}, {22, 1, 2}, {17, -1, 3}, {13, 1, 3}, {9, -1, 4}};
    for (const auto& wv : wyverns) {
        const auto [x, y] = beside(wv[0], wv[1]);
        m.Prop("props", "wyvern_nest", x + wv[1] * 56, y + 10);
        m.Enemy("wyvern", x, y, wv[2], 60.0f, 220.0f);
    }
    for (int k = 0; k < 2; ++k) {
        const auto [x, y] = beside(24 - k * 10, k % 2 ? -1 : 1);
        PlaceRock(m, rng, 700 + rock_i++, x + (k % 2 ? -44 : 44), y - 30, true, 70, "platinum_ore");
    }

    // --- the summit ----------------------------------------------------------------
    const int sx = static_cast<int>(PathX(4.0f) * CELL) + 16;
    const int sy = 5 * CELL;
    for (const auto& sp : {std::pair<int, int>{-90, -10}, {0, -40}, {96, -6}}) {
        m.Prop("props", "ice_spire", sx + sp.first, sy + sp.second);
        m.Collision(sx + sp.first - 30, sy + sp.second - 18, 60, 18);
    }
    m.Enemy("wyvern_matriarch", sx, sy + 70, 1, 300.0f, 260.0f);
    PlaceChest(m, "chest_peak_summit", sx + 40, sy + 20, "chest_peak");

    // --- the dragon's ground ---------------------------------------------------------
    // Above the last spire, where the wind stops. Hoarfang has held it for
    // fifty years; the bones round it are what the wyverns bring up. It does
    // not respawn -- killing it is the point of a quest, not a farm.
    {
        const int dx = static_cast<int>(PathX(2.0f) * CELL) + 16;
        const int dy = 2 * CELL + 8;
        for (const auto& sp : {std::pair<int, int>{-150, 26}, {-96, -12}, {104, -16}, {158, 22}}) {
            m.Prop("props", "ice_spire", dx + sp.first, dy + sp.second);
            m.Collision(dx + sp.first - 30, dy + sp.second - 18, 60, 18);
        }
        for (const auto& c : {std::pair<int, int>{-66, 54}, {72, 58}, {-24, 74}, {36, 70}}) {
            m.Prop("props", "ice_crystal", dx + c.first, dy + c.second);
        }
        // Bones of what it has eaten, and of the ten who did not come down.
        for (int k = 0; k < 5; ++k) {
            const int bx = dx - 120 + k * 60, by = dy + 96 + (k % 2) * 22;
            m.Prop("objects", kSmallRocks[(bx + by) % 4], bx, by);
        }
        m.Enemy("frost_dragon", dx, dy + 60, 1, 0.0f, 420.0f);
        PlaceChest(m, "chest_dragon_hoard", dx - 40, dy + 34, "chest_peak");
    }

    // Rime beetles out on the ice, off the track: on the frozen pools, which
    // is what the ground is drawn as where its noise runs high, never in the
    // camp at the foot or up by the dragon.
    {
        int bug_i = 0;
        const int got = PlaceBugs(m, "rime_beetle", 7, CELL, 2, 8, W - 2, H - 12, 7301u, 7.0f, [&](int cx, int cy) {
            if (!Open(cx, cy) || !Open(cx + 1, cy) || !Open(cx - 1, cy) || !Open(cx, cy + 1) || !Open(cx, cy - 1)) return false;
            if (Gap(cx, cy) < 2.0f) return false;
            return Fbm(cx * 0.2f, cy * 0.2f, 8585) > 0.64f;
        }, bug_i);
        std::printf("  Ice Spire Peak: %d rime beetles\n", got);
    }

    // Something walking the track below the summit, up one side and down the
    // other, on some days: none of the camp at its foot, none of the dragon's
    // ground at its top.
    {
        vector<std::array<int, 2>> loop;
        for (int cy = 64; cy >= 18; cy -= 4) loop.push_back({static_cast<int>(PathX(static_cast<float>(cy)) - 1.5f), cy});
        for (int cy = 20; cy <= 62; cy += 4) loop.push_back({static_cast<int>(PathX(static_cast<float>(cy)) + 1.5f), cy});
        RoamOn(m, {"den_mother", "barrow_wight", "broodmother", "lizardman_chief"}, 50, 2, 0.6f, loop);
    }
    // After dark: the white wolves off the Fells, and the Spirewatch's own dead.
    PlaceNightVisitors(m, {{{"greatwolf"}, 1, 0}, {{"tomb_shade"}, 1, 1}}, 5,
                       [](int cx, int cy) { return Open(cx, cy) && Gap(cx, cy) > 1.5f && cy > 12 && cy < H - 10; });
    m.Write("maps");
}

// =============================================================================
//  The Ashen Path
//
//  East out of the Cursed Reach the ground has burnt. A path of ash winds
//  between charred trees and black glass, crossed three times by rivers of
//  lava where the only way over is a scorched ford that burns to walk on.
//  Imps haunt it, and at the end, where two demons stand guard, the hellgate
//  opens on the Infernal Pit.
//
//  North of the road is where two of those rivers come from: the moat of the
//  Brimstone Palace. Laid out from a sketch of the user's -- the moat a U of
//  lava round the palace's forecourt, a dirt road straight up to a drawbridge
//  over its front, the palace's own guards inside it, the path's own
//  creatures all round outside, and a field of embers down its east side.
// =============================================================================

namespace ash {
static const int CELL = 32, W = 96;
// The palace country is new, and north of the old road: everything the road
// had is this many rows further down the map than it used to be.
static const int NORTH = 44, H = 40 + NORTH;
// The palace. Its moat's arms are the two eastern rivers, straight along the
// forecourt's sides; the front of it, three rows of lava, is where they turn
// south; the palace's front stands on PAL_BASE, and the forecourt lies between
// -- on a platform PLATFORM levels up, a stair down its front to the bridge.
static const int MOAT_W = 57, MOAT_E = 80, PAL_BASE = 16, MOAT_FRONT = 27, MOAT_ROWS = 3;
static const int PLATFORM = 3;                        // 42px: the user asked for at least 36
// Where the two streams out of the moat are bridged on their way south: rows,
// the first of the two each bridge spans.
static const int kCrossings[] = {36, 46};
static const float PAL_CX = 68.5f;
static const int BRIDGE_X0 = 67, BRIDGE_X1 = 70;

static float TrailY(float cx) { return NORTH + 20.0f + sinf(cx * 0.07f) * 7.0f + sinf(cx * 0.023f + 2.0f) * 3.0f; }
// The dirt road from the burnt one up to the drawbridge.
static float PalaceRoadX(float cy) { return PAL_CX + sinf(cy * 0.21f) * 1.2f; }
static float LavaX(int river, float cy) {
    static const float kX[] = {30.0f, 57.0f, 80.0f};
    const float wander = sinf((cy - NORTH) * 0.2f + river) * 2.0f;
    if (river == 0) return kX[0] + wander;
    // The two out of the moat run straight down its sides and only begin to
    // wander a few rows south of its front.
    const float k = std::clamp((cy - (MOAT_FRONT + MOAT_ROWS)) / 6.0f, 0.0f, 1.0f);
    return kX[river] + wander * k;
}
static int RiverAt(int cx, int cy) {
    for (int i = 0; i < 3; ++i) {
        if (i > 0 && cy < PAL_BASE - 1) continue;           // the moat starts under the palace's corners
        if (fabsf(cx - LavaX(i, static_cast<float>(cy))) < 1.3f) return i;
    }
    return -1;
}
static bool MoatFront(int cx, int cy) {
    return cy >= MOAT_FRONT && cy < MOAT_FRONT + MOAT_ROWS && cx >= MOAT_W - 1 && cx <= MOAT_E + 1;
}
// A bridge over one of the two streams: those cells of it are walked over.
static bool Crossing(int river, int cy) {
    if (river < 1) return false;
    for (int r : kCrossings)
        if (cy == r || cy == r + 1) return true;
    return false;
}
static bool Bridge(int cx, int cy) { return MoatFront(cx, cy) && cx >= BRIDGE_X0 && cx <= BRIDGE_X1; }
static bool Forecourt(int cx, int cy) { return cx >= MOAT_W + 2 && cx <= MOAT_E - 2 && cy >= PAL_BASE - 1 && cy < MOAT_FRONT; }
// What the far banks of the two streams hold, which is why they are bridged:
// platinum and demonite in the rock, emberbloom in the ash. Cells, and what.
struct BankOre { int cx, cy; const char* what; };
static const BankOre kBankOre[] = {
    {52, 33, "platinum_ore"}, {53, 41, "demonite_ore"}, {51, 50, "platinum_ore"},
    {50, 37, "emberbloom"},   {54, 45, "emberbloom"},
    {85, 34, "demonite_ore"}, {84, 42, "platinum_ore"}, {86, 49, "demonite_ore"},
    {87, 38, "emberbloom"},   {84, 46, "emberbloom"},
};
static bool NearBankOre(int cx, int cy) {
    for (const BankOre& b : kBankOre)
        if (std::abs(cx - b.cx) <= 1 && std::abs(cy - b.cy) <= 1) return true;
    return false;
}
// The climb to Purgatory's Plateau: a road north off the burnt one, west of
// the first river, to a gap in the cliff at the top of the map.
static const int PLAT_X = 14;
static float PlateauRoadX(float cy) { return PLAT_X + sinf(cy * 0.17f) * 1.5f; }
static bool OnPlateauRoad(int cx, int cy, float half) {
    const float x = PlateauRoadX(static_cast<float>(cy));
    return cy <= TrailY(x) && fabsf(cx - x) < half;
}
// A track worn in the cinders between the first river and the moat, in a
// ring, by something that walks it: see the roaming post at the foot of
// BuildAshenPath. Nothing grows on it.
static const float PATROL_CX = 44.0f, PATROL_CY = 26.0f, PATROL_RX = 8.0f, PATROL_RY = 14.0f;
static bool OnPatrol(int cx, int cy) {
    const float dx = (cx + 0.5f - PATROL_CX) / PATROL_RX, dy = (cy + 0.5f - PATROL_CY) / PATROL_RY;
    return fabsf(sqrtf(dx * dx + dy * dy) - 1.0f) < 0.15f;
}
static bool OnPalaceRoad(int cx, int cy, float half) {
    return cy >= MOAT_FRONT + MOAT_ROWS && cy <= static_cast<int>(TrailY(PAL_CX)) + 1 &&
           fabsf(cx + 0.5f - PalaceRoadX(static_cast<float>(cy))) < half;
}
}   // namespace ash

static void BuildAshenPath() {
    using namespace ash;
    MapBuilder m("ashen_path", "The Ashen Path", W * CELL, H * CELL);
    m.Ambient("ash");
    m.Subtitle("The burnt road to the pit");
    m.Background(36, 22, 20);
    std::mt19937 rng(6161u);

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float gap = fabsf(cy - TrailY(static_cast<float>(cx)));
            const float v = Fbm(cx * 0.22f, cy * 0.22f, 6262);
            string tile;
            const int river = RiverAt(cx, cy);
            if (Bridge(cx, cy)) {
                tile = "lava";                                // under the drawbridge, which is laid over it
            } else if (river >= 0 && Crossing(river, cy)) {
                tile = "lava";                                // under a bridge, which is laid over it
            } else if (river >= 0 || MoatFront(cx, cy)) {
                tile = "lava";
                if (river >= 0 && gap < 2.4f)
                    m.Hazard(cx * CELL, cy * CELL, CELL, CELL, 6.0f);     // the ford: passable, and it burns
                else
                    m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else if (Forecourt(cx, cy)) {
                tile = VariantOf("palace_floor", cx, cy);
            } else if (OnPalaceRoad(cx, cy, 1.1f)) {
                tile = "dirt";
            } else if (OnPalaceRoad(cx, cy, 1.9f)) {
                tile = "dirt_dark";
            } else if (OnPlateauRoad(cx, cy, 1.1f)) {
                tile = "dirt";
            } else if (OnPlateauRoad(cx, cy, 1.9f)) {
                tile = "dirt_dark";
            } else if (OnPatrol(cx, cy)) {
                tile = "ash";
            } else if (gap < 1.4f) {
                tile = "ash";
            } else if (gap < 4.0f) {
                tile = v > 0.5f ? "ash" : "cinder";
            } else {
                tile = v > 0.6f ? "cursed_ground" : "cinder";
            }
            m.Ground(tile.rfind("palace", 0) == 0 ? tile : VariantOf(tile, cx, cy), cx * CELL, cy * CELL, CELL);

            // The edges of the world are cliffs, open only where the path leaves.
            const bool west_exit = cx == 0 && gap < 3.0f;
            const bool north_exit = cy == 0 && fabsf(cx - PlateauRoadX(0.0f)) < 1.6f;
            const bool edge = cx == 0 || cy == 0 || cy == H - 1 || cx == W - 1;
            if (edge && !west_exit && !north_exit) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }

    // Burnt trees, black glass and ember vents off the path -- and none in the
    // palace's grounds, on its road, or round its moat.
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            const float gap = fabsf(cy - TrailY(static_cast<float>(cx)));
            if (gap < 3.0f || RiverAt(cx, cy) >= 0 || RiverAt(cx + 1, cy) >= 0 || RiverAt(cx - 1, cy) >= 0) continue;
            if (cx > W - 12 && gap < 7.0f) continue;          // the gate's forecourt
            if (cx >= MOAT_W - 3 && cx <= MOAT_E + 3 && cy <= MOAT_FRONT + 3) continue;
            if (OnPalaceRoad(cx, cy, 3.5f)) continue;
            if (OnPlateauRoad(cx, cy, 3.5f) || (cy < 13 && fabsf(cx - PLAT_X) < 6.0f)) continue;
            if (OnPatrol(cx, cy) || OnPatrol(cx + 1, cy) || OnPatrol(cx - 1, cy)) continue;
            if (NearBankOre(cx, cy)) continue;
            bool at_bridge = false;
            for (int rr : kCrossings)
                for (int i = 1; i <= 2; ++i)
                    at_bridge = at_bridge || (cy >= rr - 1 && cy <= rr + 2 &&
                                              fabsf(cx - LavaX(i, static_cast<float>(rr))) < 4.5f);
            if (at_bridge) continue;
            const float r = Hash2(cx, cy, 6363);
            const int x = cx * CELL + 16, y = cy * CELL + 24;
            // East of the palace, north of the road: the ember field, where the
            // ground has opened in a hundred places and nothing grows at all.
            const bool embers = cx >= MOAT_E + 3 && cy < TrailY(static_cast<float>(cx)) - 6.0f;
            if (embers) {
                if (r < 0.15f) {
                    m.Ground(VariantOf("lava", cx, cy), cx * CELL, cy * CELL, CELL);
                    m.Hazard(cx * CELL + 4, cy * CELL + 4, CELL - 8, CELL - 8, 8.0f);
                } else if (r < 0.21f) {
                    m.Prop("props", "obsidian_rock", x, y);
                    m.Collision(x - 10, y - 8, 20, 8);
                }
                continue;
            }
            if (r < 0.07f) {
                m.Prop("props", "charred_tree", x, y);
                m.Collision(x - 8, y - 8, 16, 8);
            } else if (r < 0.12f) {
                m.Prop("props", "obsidian_rock", x, y);
                m.Collision(x - 10, y - 8, 20, 8);
            } else if (r < 0.14f && gap < 6.0f) {
                m.Ground(VariantOf("lava", cx, cy), cx * CELL, cy * CELL, CELL);
                m.Hazard(cx * CELL + 4, cy * CELL + 4, CELL - 8, CELL - 8, 8.0f);
            }
        }

    // --- the way back west -----------------------------------------------------------
    const int wy = static_cast<int>(TrailY(0.0f) * CELL) + 16;
    m.Portal(0, wy - 72, 24, 144, "overworld", "from_ashen", "To the Hollowmarch", false);
    m.Spawn("from_hollowmarch", 80, wy);
    m.Spawn("default", 80, wy);
    {
        json& o = m.Object("sign_ashen_start", "sign", 150, wy - 70);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A scorched post";
        o["text"]   = "Three rivers of fire cross the path ahead. Where it fords them the ground will burn "
                      "your feet: cross quickly, or jump.\n\nAt the end, the gate. Beyond it, the pit.";
        m.Collision(134, wy - 80, 32, 10);
    }

    // --- the way up to Purgatory's Plateau --------------------------------------------------
    // The arch of bones at the head of the climb, the gap in the cliff behind
    // it, and a stone at the foot of the road where it leaves the burnt one.
    {
        // Ten rows down, so all of it stands on the map: it is nine cells tall.
        const int ax = static_cast<int>(PlateauRoadX(10.0f) * CELL) + 16, ay = 10 * CELL;
        m.Prop("props", "purgatory_arch", ax, ay);
        m.Collision(ax - 92, ay - 22, 46, 22);
        m.Collision(ax + 46, ay - 22, 46, 22);
        const int top = static_cast<int>(PlateauRoadX(0.0f) * CELL) + 16;
        m.Portal(top - 64, 0, 128, 24, "plateau_ascent", "from_ashen", "Up onto Purgatory's Plateau", false);
        m.Danger(50);
        m.Spawn("from_plateau", top, 3 * CELL + 16);
        const int fy = static_cast<int>(TrailY(PlateauRoadX(60.0f))) - 4;
        const int fx = static_cast<int>(PlateauRoadX(static_cast<float>(fy)) * CELL) + 3 * CELL;
        json& o = m.Object("sign_plateau_road", "sign", fx, fy * CELL);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A stone at the foot of a road";
        o["text"]   = "UP\n\nThe road north climbs out of the ash to Purgatory's Plateau.\n\n"
                      "Somebody has scratched a dragon under the word, and under that, five of them, "
                      "and under those: FIFTY. AT LEAST.";
        m.Collision(fx - 16, fy * CELL - 10, 32, 10);
    }

    // --- imps along the way, and the gate's two guards ------------------------------------
    const int imps[][2] = {{12, 1}, {22, 1}, {36, 2}, {46, 2}, {52, 3}, {64, 3}, {72, 4}, {86, 4}};
    for (const auto& im : imps) {
        const int cx = im[0];
        const int cy = static_cast<int>(TrailY(static_cast<float>(cx))) + ((cx / 10) % 2 ? 2 : -2);
        m.Enemy("imp", cx * CELL + 16, cy * CELL + 16, im[1], 45.0f, 220.0f);
    }

    // --- the hellgate ----------------------------------------------------------------------
    const int gcx = W - 6;
    const int gx = gcx * CELL + 16;
    const int gy = static_cast<int>(TrailY(static_cast<float>(gcx)) * CELL) - 8;
    // The art is 144px, standing on its base; its glowing doorway is 34px wide
    // at the centre and its sill 20px above the base.
    m.Prop("props", "hellgate", gx, gy + 20);
    m.Portal(gx - 16, gy - 34, 32, 30, "dungeon_infernal", "entrance", "Enter the Infernal Pit", true);
    m.Danger(45);
    m.Requires(40);
    m.Collision(gx - 70, gy - 70, 52, 72);
    m.Collision(gx + 18, gy - 70, 52, 72);
    m.Collision(gx - 18, gy - 110, 36, 72);
    m.Spawn("from_pit", gx, gy + 56);
    m.Enemy("demon", gx - 90, gy + 60, 1, 90.0f, 200.0f);
    m.Enemy("demon", gx + 90, gy + 60, 2, 90.0f, 200.0f);

    // --- the Brimstone Palace ----------------------------------------------------------------
    // The forecourt stands on a platform three levels up -- the palace's front,
    // its towers and everything in the forecourt stand on it with it -- with a
    // face of the palace's dark stone down into the moat, and a stair down its
    // front to the drawbridge, the one way up.
    {
        vector<int> level(static_cast<size_t>(W) * H, 0);
        for (int cy = PAL_BASE - 1; cy < MOAT_FRONT; ++cy)
            for (int cx = MOAT_W + 2; cx <= MOAT_E - 2; ++cx)
                level[static_cast<size_t>(cy) * W + cx] = PLATFORM;
        for (int k = 1; k < PLATFORM; ++k)                      // the stair: 2 at its head, 1 at its foot
            for (int cx = BRIDGE_X0; cx <= BRIDGE_X1; ++cx)
                level[static_cast<size_t>(MOAT_FRONT - k) * W + cx] = k;
        const vector<Rect4> ramps = {{static_cast<float>(BRIDGE_X0 * CELL),
                                      static_cast<float>((MOAT_FRONT - PLATFORM) * CELL),
                                      static_cast<float>((BRIDGE_X1 - BRIDGE_X0 + 1) * CELL),
                                      static_cast<float>((PLATFORM + 1) * CELL)}};
        m.Elevation(CELL, W, H, level, ramps, "assets/tiles/palace_wallface.png", {206, 158, 66});
    }
    // The bridges over the streams out of the moat, and what is on their far banks.
    for (int r : kCrossings)
        for (int i = 1; i <= 2; ++i) {
            const float x = (LavaX(i, r + 0.5f) + 0.5f) * CELL;
            // The art stands on the foot of its square; the bridge is its lower half.
            m.Overlay("props", "lava_bridge", static_cast<int>(x), (r + 1) * CELL - 32);
        }
    {
        int rocks = 900, herbs = 900;
        for (const BankOre& b : kBankOre) {
            const int x = b.cx * CELL + 16, y = b.cy * CELL + 24;
            if (string(b.what) == "emberbloom") PlaceHerb(m, b.what, x, y, herbs);
            else PlaceRock(m, rng, rocks++, x, y, true, string(b.what) == "demonite_ore" ? 80 : 70, b.what);
        }
    }
    const int px = static_cast<int>(PAL_CX * CELL);
    const int base_y = PAL_BASE * CELL + 16;              // the foot of the palace's steps
    // Its front, and a tower at each corner standing in the head of the moat.
    m.Prop("props", "palace_keep", px, base_y);
    m.Prop("props", "palace_tower", (MOAT_W + 2) * CELL, base_y - 8);
    m.Prop("props", "palace_tower", (MOAT_E - 1) * CELL, base_y - 8);
    // The front is solid to the foot of the gatehouse; the steps up to the door
    // are not, and the door is the way in.
    m.Collision((MOAT_W - 1) * CELL, 0, (MOAT_E - MOAT_W + 3) * CELL, base_y - 58);
    for (int s : {-1, 1}) {
        const int tx = s < 0 ? (MOAT_W + 2) * CELL : (MOAT_E - 1) * CELL;
        m.Collision(tx - 48, base_y - 80, 96, 72);
    }
    m.Portal(px - 26, base_y - 64, 52, 24, "palace_foyer", "entrance", "Enter the Brimstone Palace", true);
    m.Danger(75);
    m.Requires(60);
    m.Spawn("from_palace", px, base_y + 44);
    // Torches either side of the steps, braziers in the forecourt's corners,
    // and a demon in stone either side of the bridge's end -- all lit after dark.
    const auto light = [&](const string& id, const string& art, int x, int y, int cw) {
        json& o = m.Object(id, "lamp", x, y);
        o["sprite"] = "assets/props/" + art + ".png";
        m.Collision(x - cw / 2, y - 8, cw, 8);
    };
    light("palace_torch_w", "palace_torch", px - 92, base_y + 6, 12);
    light("palace_torch_e", "palace_torch", px + 92, base_y + 6, 12);
    light("palace_brazier_sw", "palace_brazier", (MOAT_W + 3) * CELL, (MOAT_FRONT - 1) * CELL, 22);
    light("palace_brazier_se", "palace_brazier", (MOAT_E - 2) * CELL, (MOAT_FRONT - 1) * CELL, 22);
    light("palace_brazier_nw", "palace_brazier", (MOAT_W + 4) * CELL, (PAL_BASE + 2) * CELL, 22);
    light("palace_brazier_ne", "palace_brazier", (MOAT_E - 3) * CELL, (PAL_BASE + 2) * CELL, 22);
    for (int s : {-1, 1}) {
        const int sx = px + s * 3 * CELL;
        m.Prop("props", "demon_statue", sx, (MOAT_FRONT - 1) * CELL + 8);
        m.Collision(sx - 18, (MOAT_FRONT - 1) * CELL - 6, 36, 14);
    }
    // The drawbridge, laid over the moat's front where the road meets it.
    m.Overlay("props", "drawbridge", (BRIDGE_X0 + 2) * CELL, (MOAT_FRONT + 2) * CELL);
    // A board where the road leaves the burnt one.
    {
        const int sy = static_cast<int>(TrailY(PAL_CX)) - 3;
        const int sx = static_cast<int>(PalaceRoadX(static_cast<float>(sy)) * CELL) + 3 * CELL;
        json& o = m.Object("sign_brimstone", "sign", sx, sy * CELL + 16);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A post of black iron";
        o["text"]   = "THE BRIMSTONE PALACE\n\nHis road. His bridge. His house.\n\n"
                      "Scratched under it, in a hand that shook: they are not like the ones on the path. "
                      "Seventy, and more, and do not go at night.";
        m.Collision(sx - 16, sy * CELL + 6, 32, 10);
    }
    // Its guards, inside the moat: the palace's own.
    m.Enemy("abyssal_demon", (MOAT_W + 5) * CELL + 16, (PAL_BASE + 3) * CELL + 16, 1, 120.0f, 220.0f);
    m.Enemy("abyssal_demon", (MOAT_E - 5) * CELL + 16, (PAL_BASE + 3) * CELL + 16, 2, 120.0f, 220.0f);
    m.Enemy("revenant", (MOAT_W + 6) * CELL + 16, (MOAT_FRONT - 3) * CELL + 16, 1, 120.0f, 200.0f);
    m.Enemy("revenant", (MOAT_E - 6) * CELL + 16, (MOAT_FRONT - 3) * CELL + 16, 1, 120.0f, 200.0f);
    // On the road up to it, and round it: what lives on the Ashen Path, grown
    // bigger for living this close. None of them nearer the burnt road than
    // nine rows, so the path itself is no harder than it was.
    const int road[][4] = {   // cx, cy, level, demon?
        {62, 32, 20, 1}, {76, 34, 19, 1}, {60, 38, 24, 0}, {75, 40, 23, 0}, {64, 44, 18, 1}, {73, 46, 22, 0}};
    for (const auto& e : road)
        m.Enemy(e[3] ? "demon" : "imp", e[0] * CELL + 16, e[1] * CELL + 16, e[2], 60.0f, 220.0f);
    const int west[][3] = {{40, 14, 16}, {48, 10, 18}, {46, 24, 15}, {36, 32, 14}, {50, 36, 17}, {42, 46, 13}};
    for (const auto& e : west)
        m.Enemy("imp", e[0] * CELL + 16, e[1] * CELL + 16, e[2], 60.0f, 220.0f);
    m.Enemy("demon", 52 * CELL + 16, 20 * CELL + 16, 12, 60.0f, 220.0f);
    m.Enemy("imp", 88 * CELL + 16, 20 * CELL + 16, 16, 60.0f, 220.0f);
    m.Enemy("imp", 86 * CELL + 16, 36 * CELL + 16, 15, 60.0f, 220.0f);

    // --- firebugs, where the lava runs --------------------------------------------------------
    // On the banks two or three cells off molten rock -- never on it, never on
    // the road, and not inside the palace's moat, where nobody goes to catch
    // anything. Shared out by what the rock is, so the long first river does
    // not have them all: some by each of the three rivers, some by the pools
    // along the road, and some at the edge of the ember field.
    {
        enum Src : uint8_t { NONE = 0, RIVER0, RIVER1, RIVER2, POOL, EMBERS };
        vector<uint8_t> hot(static_cast<size_t>(W) * H, NONE);
        const auto cell = [&](int cx, int cy) -> uint8_t& { return hot[static_cast<size_t>(cy) * W + cx]; };
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const int river = RiverAt(cx, cy);
                if (river >= 0) cell(cx, cy) = static_cast<uint8_t>(RIVER0 + river);
                else if (MoatFront(cx, cy)) cell(cx, cy) = RIVER1;
            }
        for (const auto& h : m.dq["hazards"]) {
            const int hx = h["rect"][0], hy = h["rect"][1], hw = h["rect"][2], hh = h["rect"][3];
            for (int cy = hy / CELL; cy <= (hy + hh - 1) / CELL; ++cy)
                for (int cx = hx / CELL; cx <= (hx + hw - 1) / CELL; ++cx)
                    if (cx >= 0 && cy >= 0 && cx < W && cy < H && cell(cx, cy) == NONE)
                        cell(cx, cy) = (cx >= MOAT_E + 3 && cy < TrailY(static_cast<float>(cx)) - 6.0f) ? EMBERS : POOL;
            }
        // How far the nearest molten rock is, in cells, and what it is.
        const auto nearest = [&](int cx, int cy, uint8_t& what) {
            int d = 99;
            what = NONE;
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= W || y >= H || cell(x, y) == NONE) continue;
                    const int dd = std::max(abs(dx), abs(dy));
                    if (dd < d) { d = dd; what = cell(x, y); }
                }
            return d;
        };
        const auto bank = [&](int cx, int cy, uint8_t by) {
            if (cell(cx, cy) != NONE) return false;
            uint8_t what = NONE;
            const int d = nearest(cx, cy, what);
            if (d < 2 || d > 3 || what != by) return false;
            if (fabsf(cy - TrailY(static_cast<float>(cx))) < 2.0f) return false;
            if (cx >= MOAT_W - 3 && cx <= MOAT_E + 3 && cy <= MOAT_FRONT + 2) return false;
            if (OnPalaceRoad(cx, cy, 2.0f) || OnPlateauRoad(cx, cy, 2.0f) || OnPatrol(cx, cy)) return false;
            return !(cx > W - 12 && fabsf(cy - TrailY(static_cast<float>(cx))) < 7.0f);   // the gate's forecourt
        };
        int bug_i = 0, got = 0;
        vector<std::pair<int, int>> taken;
        const std::pair<uint8_t, int> shares[] = {{RIVER0, 3}, {RIVER1, 3}, {RIVER2, 2}, {POOL, 2}, {EMBERS, 3}};
        for (const auto& sh : shares)
            got += PlaceBugs(m, "firebug", sh.second, CELL, 2, 2, W - 2, H - 2, 7201u + sh.first, 6.0f,
                             [&](int cx, int cy) { return bank(cx, cy, sh.first); }, bug_i, &taken);
        std::printf("  the Ashen Path: %d firebugs\n", got);
    }

    PlaceCurios(m);
    // On some days, one of a pool of bosses walking the track worn round the
    // north of the path, somewhere different on it each day.
    {
        vector<std::array<int, 2>> loop;
        for (int k = 0; k < 14; ++k) {
            const float a = k * 6.2831853f / 14.0f;
            loop.push_back({static_cast<int>(PATROL_CX + cosf(a) * PATROL_RX), static_cast<int>(PATROL_CY + sinf(a) * PATROL_RY)});
        }
        RoamOn(m, {"pit_lord", "orc3", "vampire_lord", "barrow_wight"}, 62, 2, 0.6f, loop);
    }
    // At night: the plateau's own come down its road -- Greater Demons and the
    // Pyre Dragons -- and worse into the palace country. None on the burnt
    // road itself, in the moat, or on the climb.
    PlaceNightVisitors(m, {{{"greater_demon"}, 1, 1}, {{"dragon_fire", "dragon_earth"}, 1, 1}, {{"abyssal_demon"}, 1, 0}}, 7,
                       [&](int cx, int cy) {
                           if (fabsf(cy - TrailY(static_cast<float>(cx))) < 7.0f) return false;
                           if (RiverAt(cx, cy) >= 0 || RiverAt(cx + 1, cy) >= 0 || RiverAt(cx - 1, cy) >= 0) return false;
                           if (cx >= MOAT_W - 4 && cx <= MOAT_E + 4 && cy <= MOAT_FRONT + 5) return false;
                           if (OnPlateauRoad(cx, cy, 4.0f) || OnPalaceRoad(cx, cy, 4.0f) || OnPatrol(cx, cy)) return false;
                           return true;
                       });
    m.Write("maps");
}

// =============================================================================
//  The Brimstone Palace
//
//  palace_foyer      the great hall, from the user's sketch of it: a crimson
//                    runner from the doors to the throne room's, balconies
//                    down both sides, and three walkways crossing overhead
//                    from one to the other. Off the west balcony, the ballroom
//                    and the dining hall; off the east, the chambers wing; and
//                    from the floor a stair down to the dungeon.
//  palace_ballroom   a chequer of black and blood marble, chandeliers, an organ
//  palace_dining     a banquet laid under two chandeliers, a hearth of lava
//  palace_chambers   a corridor of bedchambers, the king's at the end of it
//  palace_dungeon    cells behind bars, and a room with a rack in it
//  palace_throne     the Cinder King's, on a dais between two channels of lava
//
//  Abyssal Demons and demons keep the halls, and Revenants and Bone Knights
//  guard them; its master waits in the throne room.
// =============================================================================

namespace pal {
static const int CELL = 32;

struct Door { char side; int a, b; };

// A hall of the palace. The back wall is `back` rows deep and seen -- its top
// courses plain and the crimson band along its foot -- and the other three
// are seen from above. A door is a gap: 'S' in the front wall, 'E' or 'W' in
// a side wall, from a to b. `floor` names a cell's tile where it is not the
// basalt: the runner, the marble, the dark under a walkway; empty for basalt.
static void Hall(MapBuilder& m, int cols, int rows, int back, const vector<Door>& doors,
                 const std::function<string(int, int)>& floor = nullptr) {
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool backwall = cy < back, front = cy == rows - 1, west = cx == 0, east = cx == cols - 1;
            bool gap = false;
            for (const Door& d : doors) {
                if (d.side == 'S' && front && cx >= d.a && cx <= d.b) gap = true;
                if (d.side == 'E' && east && cy >= d.a && cy <= d.b) gap = true;
                if (d.side == 'W' && west && cy >= d.a && cy <= d.b) gap = true;
            }
            const bool solid = (backwall || front || west || east) && !gap;
            string tile;
            if (solid) {
                if (backwall && !west && !east)
                    tile = cy == back - 1 ? VariantOf("palace_wall", cx, cy) : string("palace_wallface");
                else
                    tile = "palace_walltop";
            } else {
                if (floor) tile = floor(cx, cy);
                if (tile.empty()) tile = VariantOf("palace_floor", cx, cy);
            }
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
}

// A wall inside a room: the top of it, as the side walls are drawn.
static void Wall(MapBuilder& m, int cx0, int cy0, int cx1, int cy1) {
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            m.Ground("palace_walltop", cx * CELL, cy * CELL, CELL);
            m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
}

// The runner's cells, a column at a time: its gold-bordered edges and its field.
static string Runner(int cx, int c0, int c1) {
    if (cx == c0) return "palace_carpet_l";
    if (cx == c1) return "palace_carpet_r";
    return "palace_carpet";
}

// A standing thing with a foot to walk into, and a lit one.
static void Piece(MapBuilder& m, const string& art, int x, int y, int cw, int ch) {
    m.Prop("props", art, x, y);
    if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
}
static void Light(MapBuilder& m, const string& id, const string& art, int x, int y, int cw) {
    json& o = m.Object(id, "lamp", x, y);
    o["sprite"] = "assets/props/" + art + ".png";
    m.Collision(x - cw / 2, y - 8, cw, 8);
}
static void Look(MapBuilder& m, const string& subtitle) {
    m.Interior(true);
    m.Subtitle(subtitle);
    m.Background(14, 10, 14);
}
}   // namespace pal

static void BuildPalaceFoyer() {
    using namespace pal;
    const int cols = 32, rows = 50, back = 5;
    const int run0 = 12, run1 = 19;                       // the runner: eight cells, gold at its edges
    const int bal_w0 = 1, bal_w1 = 4, bal_e0 = 27, bal_e1 = 30;
    const int bal_top = back, bal_end = 42;               // balconies, rows 5-42, stairs below them
    const int walk_rows[3] = {11, 23, 35};                // three walkways overhead, three rows deep each
    const auto under_walk = [&](int cy) {
        for (int w : walk_rows) if (cy >= w && cy <= w + 2) return true;
        return false;
    };
    MapBuilder m("palace_foyer", "The Brimstone Palace", cols * CELL, rows * CELL);
    Look(m, "His hall, and the long walk up it");

    Hall(m, cols, rows, back,
         {{'S', 14, 17}, {'W', 12, 13}, {'W', 28, 29}, {'E', 20, 21}},
         [&](int cx, int cy) -> string {
             if (cx >= run0 && cx <= run1) return Runner(cx, run0, run1);
             // A narrower runner along each balcony, up its stairs, so a
             // gallery reads as a gallery and not as more of the floor.
             if (cy >= back && cy <= bal_end + 2) {
                 if (cx == bal_w0 + 1 || cx == bal_e0 + 1) return "palace_carpet_l";
                 if (cx == bal_w0 + 2 || cx == bal_e0 + 2) return "palace_carpet_r";
             }
             if (under_walk(cy) && cx > bal_w1 && cx < bal_e0) return VariantOf("palace_floor_dark", cx, cy);
             return "";
         });

    // --- the balconies, on real height, and the stairs up to them ----------------------
    // Three levels up, the height of a doorway: the walkways go across at the
    // same height, over the heads of whoever is on the runner. The side walls
    // rise with them, and so do the doorways in them onto the rooms.
    vector<int> level(static_cast<size_t>(cols) * rows, 0);
    const auto at = [&](int cx, int cy) -> int& { return level[static_cast<size_t>(cy) * cols + cx]; };
    for (int cy = bal_top; cy <= bal_end; ++cy)
        for (int cx : {0, bal_w0, bal_w0 + 1, bal_w0 + 2, bal_w1, bal_e0, bal_e0 + 1, bal_e0 + 2, bal_e1, cols - 1})
            at(cx, cy) = 3;
    vector<Rect4> ramps;
    for (int c0 : {bal_w0, bal_e0}) {
        for (int cx = c0; cx < c0 + 4; ++cx) { at(cx, bal_end + 1) = 2; at(cx, bal_end + 2) = 1; }
        ramps.push_back({static_cast<float>(c0 * CELL), static_cast<float>(bal_end * CELL),
                         4.0f * CELL, 4.0f * CELL});
    }
    m.Elevation(CELL, cols, rows, level, ramps, "assets/tiles/palace_wallface.png", {206, 158, 66});

    // A rail along each balcony's edge, but where a walkway meets it.
    for (int cy = bal_top; cy <= bal_end; ++cy) {
        if (under_walk(cy)) continue;
        m.Prop("props", "palace_baluster", bal_w1 * CELL + 26, cy * CELL + 30);
        m.Prop("props", "palace_baluster", bal_e0 * CELL + 6, cy * CELL + 30);
    }
    // The walkways: lengths of bridge laid end to end from one balcony's edge to
    // the other's, on the layer drawn over everyone, lifted to the balconies'
    // height. The floor under each is in its shadow.
    for (int w : walk_rows)
        for (int k = 0; k < 11; ++k)
            m.Prop("props", "palace_walkway", (bal_w1 + 2 + 2 * k) * CELL, (w + 3) * CELL - 42, 2);

    // --- the runner, and what stands along it ------------------------------------------
    // The throne room's doors, at the head of it.
    const int door_x = 16 * CELL, door_y = back * CELL + 6;
    m.Prop("props", "throne_door", door_x, door_y);
    m.Portal(door_x - 40, door_y - 10, 80, 22, "palace_throne", "entrance", "Enter the throne room", true);
    m.Danger(84);
    m.Spawn("from_palace_throne", door_x, door_y + 44);
    for (int cx : {8, 24}) m.Prop("props", "palace_banner", cx * CELL, back * CELL + 2);
    // The sigil woven into it, twice.
    m.Overlay("props", "palace_sigil", 16 * CELL, 30 * CELL);
    m.Overlay("props", "palace_sigil", 16 * CELL, 44 * CELL);
    // The light of the high windows over the balconies, in crimson and gold
    // and violet on the floor between the walkways' shadows, and the great
    // window over the throne room's doors laid down the runner.
    {
        int pane = 0;
        for (int cy : {18, 30})
            for (int cx : {7, 24})
                m.Object("palace_foyer_glass_" + std::to_string(pane++), "glass_light", cx * CELL + 16, cy * CELL + 8);
        m.Object("palace_foyer_glass_" + std::to_string(pane++), "glass_light", 16 * CELL, 18 * CELL + 8);
    }
    // Braziers down both sides of it, and columns along the balconies' edges.
    int lamp = 0;
    for (int cy : {9, 20, 31, 42})
        for (int cx : {run0 - 2, run1 + 2})
            Light(m, "palace_foyer_brazier_" + std::to_string(lamp++), "palace_brazier", cx * CELL + 16, cy * CELL + 24, 22);
    for (int cy : {8, 18, 30, 41})
        for (int cx : {bal_w1 + 1, bal_e0 - 1})
            Piece(m, "palace_pillar", cx * CELL + 16, cy * CELL + 24, 26, 12);
    // The way in, with a demon in stone either side of it.
    Piece(m, "demon_statue", 9 * CELL + 16, 47 * CELL + 16, 34, 14);
    Piece(m, "demon_statue", 22 * CELL + 16, 47 * CELL + 16, 34, 14);
    m.Spawn("entrance", 16 * CELL, 47 * CELL);
    m.Spawn("default",  16 * CELL, 47 * CELL);
    m.Portal(14 * CELL, rows * CELL - 24, 4 * CELL, 24, "ashen_path", "from_palace", "Out to the Ashen Path", false);

    // --- the doors off the balconies, and the stair down -------------------------------
    struct SideDoor { int cx, cy; const char* map; const char* spawn; const char* label; };
    const SideDoor sides[] = {
        {0,        12, "palace_ballroom",  "from_palace_ballroom", "The ballroom"},
        {0,        28, "palace_dining",    "from_palace_dining",   "The dining hall"},
        {cols - 1, 20, "palace_chambers",  "from_palace_chambers", "The chambers wing"},
    };
    for (const SideDoor& d : sides) {
        const bool west = d.cx == 0;
        m.Portal(west ? 0 : cols * CELL - 24, d.cy * CELL, 24, 2 * CELL, d.map, "entrance", d.label, false);
        m.Spawn(d.spawn, (west ? 3 : cols - 3) * CELL, d.cy * CELL + 32);
        // A brazier either side of it on the balcony, so a door in a wall seen
        // end on is still a door.
        const int bx = west ? 2 * CELL + 8 : (cols - 2) * CELL - 8;
        Light(m, string("palace_foyer_door_") + d.map + "_a", "palace_brazier", bx, (d.cy - 1) * CELL + 20, 18);
        Light(m, string("palace_foyer_door_") + d.map + "_b", "palace_brazier", bx, (d.cy + 3) * CELL + 12, 18);
    }
    const int sx = 24 * CELL, sy = 44 * CELL + 16;
    m.Prop("props", "dungeon_stairs_down", sx, sy + 2);
    m.SortLift("dungeon_stairs_down", 40);
    m.Portal(sx - 24, sy - 44, 48, 40, "palace_dungeon", "entrance", "Down to the dungeon", true);
    m.Spawn("from_palace_dungeon", sx, sy + 34);

    // --- who keeps it ----------------------------------------------------------------------
    // Abyssal Demons and demons in the hall, Revenants and Bone Knights on guard,
    // one of them up on each balcony.
    const auto post = [&](const char* type, int cx, int cy, int lv) {
        m.Enemy(type, cx * CELL + 16, cy * CELL + 16, lv, 120.0f, 200.0f);
    };
    post("revenant", 13, 8, 1);
    post("abyssal_demon", 16, 16, 2);
    post("bone_knight", 22, 17, 27);
    post("demon", 9, 21, 22);
    post("abyssal_demon", 16, 28, 1);
    post("demon", 23, 33, 21);
    post("bone_knight", 10, 38, 28);
    post("revenant", 21, 40, 1);
    post("bone_knight", 2, 19, 27);
    post("bone_knight", 29, 31, 27);
    post("demon", 2, 36, 21);
    m.Write("maps");
}

static void BuildPalaceBallroom() {
    using namespace pal;
    const int cols = 30, rows = 22, back = 4;
    MapBuilder m("palace_ballroom", "The Ballroom", cols * CELL, rows * CELL);
    Look(m, "The dead still dance here, to an organ nobody plays");
    Hall(m, cols, rows, back, {{'E', 10, 11}}, [&](int cx, int cy) -> string {
        if (cx >= 5 && cx <= 24 && cy >= 6 && cy <= 19) {
            const bool dark = ((cx + cy) & 1) != 0;
            const bool alt = ((cx * 3 + cy * 5) % 7) < 3;
            return string(dark ? "palace_marble_a" : "palace_marble_b") + (alt ? "_1" : "");
        }
        if ((cy == 10 || cy == 11) && cx >= 25) return "palace_carpet";
        return "";
    });
    Piece(m, "pipe_organ", 15 * CELL, back * CELL + 22, 180, 30);
    Piece(m, "demon_statue", 10 * CELL, back * CELL + 22, 34, 14);
    Piece(m, "demon_statue", 20 * CELL, back * CELL + 22, 34, 14);
    for (int cx : {3, 6, 24, 27}) Piece(m, "palace_mirror", cx * CELL, back * CELL + 6, 30, 8);
    // Chandeliers, hung over the floor on the layer drawn over everyone.
    for (const auto& c : {std::pair<int, int>{10, 10}, {20, 10}, {15, 16}})
        m.Prop("props", "palace_chandelier", c.first * CELL, c.second * CELL, 2);
    int lamp = 0;
    for (const auto& c : {std::pair<int, int>{2, 6}, {27, 6}, {2, 19}, {27, 19}})
        Light(m, "palace_ballroom_brazier_" + std::to_string(lamp++), "palace_brazier", c.first * CELL + 16, c.second * CELL + 24, 22);
    PlaceChest(m, "chest_palace_ballroom", 25 * CELL + 16, back * CELL + 40, "chest_palace");
    m.Spawn("entrance", (cols - 3) * CELL, 11 * CELL);
    m.Spawn("default",  (cols - 3) * CELL, 11 * CELL);
    m.Portal(cols * CELL - 24, 10 * CELL, 24, 2 * CELL, "palace_foyer", "from_palace_ballroom", "Back to the hall", false);
    // Two of the dead in the middle of the floor, still turning; the rest keep
    // the walls.
    m.Enemy("revenant", 13 * CELL, 12 * CELL, 2, 120.0f, 200.0f);
    m.Enemy("revenant", 17 * CELL, 12 * CELL, 2, 120.0f, 200.0f);
    m.Enemy("abyssal_demon", 15 * CELL, 17 * CELL, 1, 120.0f, 200.0f);
    m.Enemy("demon", 7 * CELL, 15 * CELL, 23, 120.0f, 200.0f);
    m.Enemy("demon", 23 * CELL, 15 * CELL, 22, 120.0f, 200.0f);
    m.Write("maps");
}

static void BuildPalaceDining() {
    using namespace pal;
    const int cols = 30, rows = 18, back = 4;
    MapBuilder m("palace_dining", "The Dining Hall", cols * CELL, rows * CELL);
    Look(m, "Dinner is always laid, and never cleared");
    Hall(m, cols, rows, back, {{'E', 8, 9}}, [&](int cx, int cy) -> string {
        if ((cy == 8 || cy == 9) && cx >= 25) return "palace_carpet";
        return "";
    });
    // The banquet: two tables end to end, a chair to every place.
    const int ty = 10 * CELL + 4;
    for (int tx : {15 * CELL - 88, 15 * CELL + 88}) {
        Piece(m, "palace_table", tx, ty, 176, 24);
        for (int off : {-64, -22, 22, 64}) {
            Piece(m, "high_chair", tx + off, ty - 30, 0, 0);
            Piece(m, "high_chair_back", tx + off, ty + 16, 18, 8);
        }
        m.Prop("props", "palace_chandelier", tx, ty - 34, 2);
    }
    // The hearth, the sideboard and the casks.
    Piece(m, "palace_hearth", 5 * CELL, back * CELL + 22, 110, 24);
    Piece(m, "bottle_shelf", 22 * CELL, back * CELL + 16, 60, 14);
    Piece(m, "keg_rack", 26 * CELL, back * CELL + 16, 60, 14);
    for (int cx : {10, 14, 18}) m.Prop("props", "palace_banner", cx * CELL, back * CELL + 2);
    // Columns down the room, and his demons in stone either side of the door.
    for (int cx : {6, 24})
        for (int cy : {7, 14}) Piece(m, "palace_pillar", cx * CELL + 16, cy * CELL + 24, 26, 12);
    Piece(m, "demon_statue", 27 * CELL, 6 * CELL + 16, 34, 14);
    Piece(m, "demon_statue", 27 * CELL, 12 * CELL + 16, 34, 14);
    Piece(m, "barrel", 24 * CELL + 16, back * CELL + 40, 22, 10);
    Piece(m, "barrel", 25 * CELL + 16, back * CELL + 48, 22, 10);
    int lamp = 0;
    for (const auto& c : {std::pair<int, int>{2, 15}, {27, 15}})
        Light(m, "palace_dining_brazier_" + std::to_string(lamp++), "palace_brazier", c.first * CELL + 16, c.second * CELL + 24, 22);
    PlaceChest(m, "chest_palace_dining", 2 * CELL + 16, back * CELL + 72, "chest_palace");
    m.Spawn("entrance", (cols - 3) * CELL, 9 * CELL);
    m.Spawn("default",  (cols - 3) * CELL, 9 * CELL);
    m.Portal(cols * CELL - 24, 8 * CELL, 24, 2 * CELL, "palace_foyer", "from_palace_dining", "Back to the hall", false);
    m.Enemy("demon", 11 * CELL, 13 * CELL, 22, 120.0f, 200.0f);
    m.Enemy("demon", 18 * CELL, 13 * CELL, 23, 120.0f, 200.0f);
    m.Enemy("bone_knight", 7 * CELL, 7 * CELL, 28, 120.0f, 200.0f);
    m.Enemy("bone_knight", 22 * CELL, 7 * CELL, 28, 120.0f, 200.0f);
    m.Enemy("abyssal_demon", 24 * CELL, 13 * CELL, 1, 120.0f, 200.0f);
    m.Write("maps");
}

static void BuildPalaceChambers() {
    using namespace pal;
    const int cols = 34, rows = 24, back = 4;
    MapBuilder m("palace_chambers", "The Chambers Wing", cols * CELL, rows * CELL);
    Look(m, "Where the palace sleeps, when it sleeps");
    // A corridor east from the door, three bedchambers either side of it.
    const auto corridor = [](int cy) { return cy >= 10 && cy <= 13; };
    Hall(m, cols, rows, back, {{'W', 11, 12}}, [&](int cx, int cy) -> string {
        if (corridor(cy) && (cy == 11 || cy == 12)) return "palace_carpet";
        return "";
    });
    // The rooms' walls: across between them, and along the corridor with a
    // doorway into each. North rooms rows 4-8, south rooms rows 15-22.
    for (int wx : {11, 22}) {
        Wall(m, wx, back, wx, 8);
        Wall(m, wx, 15, wx, rows - 2);
    }
    const int doors[3] = {5, 16, 27};
    for (int cx = 1; cx < cols - 1; ++cx) {
        bool gap = false;
        for (int d : doors) gap = gap || cx == d || cx == d + 1;
        if (!gap) { Wall(m, cx, 9, cx, 9); Wall(m, cx, 14, cx, 14); }
    }
    // Furnishing: a bed, a wardrobe, a nightstand and a rug in each, and in the
    // middle of the south side the king's own, larger, with his desk and a chest.
    const int rooms[6][3] = {{1, 4, 0}, {12, 4, 0}, {23, 4, 0}, {1, 15, 1}, {12, 15, 2}, {23, 15, 1}};
    int k = 0;
    for (const auto& r : rooms) {
        const int x0 = r[0] * CELL, y0 = r[1] * CELL;
        const bool south = r[2] > 0, royal = r[2] == 2;
        const int bed_y = y0 + (south ? 4 : 3) * CELL;
        Piece(m, "palace_bed", x0 + 3 * CELL, bed_y, 56, 40);
        Piece(m, "wardrobe", x0 + 8 * CELL, y0 + CELL + 20, 44, 14);
        Piece(m, "nightstand", x0 + 5 * CELL + 8, bed_y - 30, 20, 8);
        if (south) Piece(m, "palace_mirror", x0 + 9 * CELL, y0 + 6 * CELL + 24, 30, 8);
        if (royal) {
            Piece(m, "writing_desk", x0 + 7 * CELL, y0 + 6 * CELL, 46, 12);
            PlaceChest(m, "chest_palace_chambers", x0 + 9 * CELL, y0 + 3 * CELL + 16, "chest_palace_vault");
            m.Prop("props", "palace_banner", x0 + 5 * CELL, y0 + 2);
        }
        Light(m, "palace_chambers_brazier_" + std::to_string(k++), "palace_brazier", x0 + CELL, y0 + (south ? 7 : 3) * CELL + 20, 18);
    }
    m.Spawn("entrance", 3 * CELL, 12 * CELL);
    m.Spawn("default",  3 * CELL, 12 * CELL);
    m.Portal(0, 11 * CELL, 24, 2 * CELL, "palace_foyer", "from_palace_chambers", "Back to the hall", false);
    m.Enemy("revenant", 12 * CELL, 12 * CELL, 1, 120.0f, 200.0f);
    m.Enemy("revenant", 25 * CELL, 11 * CELL, 2, 120.0f, 200.0f);
    m.Enemy("bone_knight", 17 * CELL, 18 * CELL, 28, 120.0f, 200.0f);
    m.Enemy("bone_knight", 6 * CELL, 7 * CELL, 27, 120.0f, 200.0f);
    m.Enemy("demon", 28 * CELL, 7 * CELL, 22, 120.0f, 200.0f);
    m.Write("maps");
}

static void BuildPalaceDungeon() {
    using namespace pal;
    const int cols = 30, rows = 26, back = 3;
    MapBuilder m("palace_dungeon", "The Palace Dungeon", cols * CELL, rows * CELL);
    Look(m, "What he keeps, and what is left of it");
    m.Background(10, 8, 10);
    // Flags underfoot rather than polish, and a corridor east and west through
    // the middle: cells north of it and south of it, behind bars.
    Hall(m, cols, rows, back, {}, [&](int cx, int cy) -> string {
        (void)cx;
        return VariantOf((cy >= 11 && cy <= 14) ? "dungeon_floor" : "dungeon_floor_dark", cx, cy);
    });
    for (int wx : {7, 14, 21, 28}) Wall(m, wx, back, wx, 10);             // between the north cells
    for (int wx : {14, 21, 28}) Wall(m, wx, 15, wx, rows - 2);            // and the south ones
    // The rack room's own wall along the corridor, with a way in the middle of it.
    Wall(m, 1, 15, 5, 15);
    Wall(m, 9, 15, 13, 15);
    // The landing at the foot of the stair, in the north-west; the way up.
    const int ex = 3 * CELL + 16, stair_base = back * CELL + 72;
    m.Prop("props", "dungeon_stairs_up", ex, stair_base);
    m.SortLift("dungeon_stairs_up", 90);
    m.Portal(ex - 20, stair_base - 38, 40, 36, "palace_foyer", "from_palace_dungeon", "Up to the hall", true);
    m.Collision(ex - 26, stair_base - 72, 52, 34);
    m.Collision(ex - 26, stair_base - 38, 6, 38);
    m.Collision(ex + 20, stair_base - 38, 6, 38);
    m.Spawn("entrance", ex, stair_base + 22);
    m.Spawn("default",  ex, stair_base + 22);
    // Bars across the cells' fronts. The middle north cell's are broken open.
    const auto bars = [&](int cx0, int cx1, int y, bool open_door) {
        for (int cx = cx0; cx + 1 <= cx1; cx += 2) {
            const bool door = open_door && cx == cx0 + 2;
            if (door) continue;
            m.Prop("props", cx == cx0 ? "cell_door" : "cell_bars", (cx + 1) * CELL, y);
            m.Collision(cx * CELL, y - 10, 2 * CELL, 10);
        }
    };
    bars(8, 13, 11 * CELL - 2, true);
    bars(15, 20, 11 * CELL - 2, false);
    bars(22, 27, 11 * CELL - 2, false);
    bars(15, 20, 15 * CELL + 8, false);
    bars(22, 27, 15 * CELL + 8, false);
    // What is in the cells: chains on the walls, somebody's bones, the webs.
    for (int cx : {10, 17, 24}) Piece(m, "wall_shackles", cx * CELL, back * CELL + 10, 0, 0);
    Piece(m, "iron_cage", 18 * CELL, 8 * CELL, 30, 12);
    Piece(m, "iron_cage", 25 * CELL, 21 * CELL, 30, 12);
    for (const auto& c : {std::pair<int, int>{27, 4}, {9, 4}, {27, 23}, {16, 23}})
        m.Prop("props", "cobweb", c.first * CELL, c.second * CELL + 8);
    PlaceChest(m, "chest_palace_dungeon", 11 * CELL, 5 * CELL + 16, "chest_palace_vault");
    // The room with the rack in it, open to the corridor, south-west.
    Piece(m, "torture_rack", 6 * CELL, 20 * CELL, 76, 30);
    Piece(m, "wall_shackles", 3 * CELL, 16 * CELL + 10, 0, 0);
    Piece(m, "wall_shackles", 11 * CELL, 16 * CELL + 10, 0, 0);
    Piece(m, "iron_cage", 11 * CELL, 23 * CELL, 30, 12);
    Light(m, "palace_dungeon_brazier_a", "palace_brazier", 2 * CELL + 16, 23 * CELL + 16, 22);
    Light(m, "palace_dungeon_brazier_b", "palace_brazier", 12 * CELL + 16, 13 * CELL + 30, 22);
    Light(m, "palace_dungeon_brazier_c", "palace_brazier", 27 * CELL + 16, 13 * CELL + 30, 22);
    // Its jailers, and what got out.
    m.Enemy("bone_knight", 10 * CELL, 12 * CELL + 16, 29, 120.0f, 200.0f);
    m.Enemy("bone_knight", 20 * CELL, 13 * CELL, 29, 120.0f, 200.0f);
    m.Enemy("revenant", 26 * CELL, 12 * CELL + 16, 2, 120.0f, 200.0f);
    m.Enemy("rime_revenant", 7 * CELL, 22 * CELL, 1, 120.0f, 200.0f);
    PlaceCurios(m);
    m.Write("maps");
}

static void BuildPalaceThrone() {
    using namespace pal;
    const int cols = 28, rows = 34, back = 5;
    const int run0 = 11, run1 = 16;
    MapBuilder m("palace_throne", "The Throne Room", cols * CELL, rows * CELL);
    Look(m, "The Cinder King receives");
    const auto lava = [](int cx, int cy) { return (cx == 7 || cx == 8 || cx == 19 || cx == 20) && cy >= 11 && cy <= 30; };
    Hall(m, cols, rows, back, {{'S', 12, 15}}, [&](int cx, int cy) -> string {
        if (lava(cx, cy)) return "lava";
        if (cx >= run0 && cx <= run1 && cy >= 9) return Runner(cx, run0, run1);
        return "";
    });
    // Two channels of lava either side of the runner: it can be walked through,
    // and it burns.
    for (int cy = 11; cy <= 30; ++cy)
        for (int cx : {7, 8, 19, 20}) m.Hazard(cx * CELL + 2, cy * CELL + 2, CELL - 4, CELL - 4, 12.0f);
    // The dais, two levels up, with its steps down the middle of its front.
    vector<int> level(static_cast<size_t>(cols) * rows, 0);
    for (int cy = back; cy <= 8; ++cy)
        for (int cx = 7; cx <= 20; ++cx) level[static_cast<size_t>(cy) * cols + cx] = 2;
    for (int cx = 12; cx <= 15; ++cx) level[static_cast<size_t>(9) * cols + cx] = 1;
    vector<Rect4> ramps = {{12.0f * CELL, 8.0f * CELL, 4.0f * CELL, 3.0f * CELL}};
    m.Elevation(CELL, cols, rows, level, ramps, "assets/tiles/palace_wallface.png", {206, 158, 66});
    // The throne on it, braziers at its corners, and his banners behind.
    Piece(m, "demon_throne", 14 * CELL, 7 * CELL + 20, 120, 40);
    Light(m, "palace_throne_brazier_w", "palace_brazier", 8 * CELL + 16, 8 * CELL + 20, 22);
    Light(m, "palace_throne_brazier_e", "palace_brazier", 19 * CELL + 16, 8 * CELL + 20, 22);
    for (int cx : {5, 9, 19, 23}) m.Prop("props", "palace_banner", cx * CELL, back * CELL + 2);
    PlaceRelicChest(m, "chest_cinder_king", 18 * CELL, 6 * CELL + 16, "heart_of_cinders", "");
    // Columns down both sides, and his demons in stone at the door.
    for (int cy : {12, 18, 24, 30})
        for (int cx : {3, 24}) Piece(m, "palace_pillar", cx * CELL + 16, cy * CELL + 24, 26, 12);
    Piece(m, "demon_statue", 10 * CELL, 31 * CELL + 16, 34, 14);
    Piece(m, "demon_statue", 18 * CELL, 31 * CELL + 16, 34, 14);
    // His windows' light on the floor between the lava and the walls.
    for (int cy : {16, 25})
        for (int cx : {4, 23})
            m.Object("palace_throne_glass_" + std::to_string(cx) + "_" + std::to_string(cy), "glass_light",
                     cx * CELL + 16, cy * CELL + 8);
    m.Spawn("entrance", 14 * CELL, 31 * CELL);
    m.Spawn("default",  14 * CELL, 31 * CELL);
    m.Portal(12 * CELL, rows * CELL - 24, 4 * CELL, 24, "palace_foyer", "from_palace_throne", "Back to the hall", false);
    // He stands at the foot of his dais.
    m.Enemy("cinder_king", 14 * CELL, 12 * CELL, 1, 0.0f, 900.0f);
    m.Write("maps");
}

static void BuildBrimstonePalace() {
    BuildPalaceFoyer();
    BuildPalaceBallroom();
    BuildPalaceDining();
    BuildPalaceChambers();
    BuildPalaceDungeon();
    BuildPalaceThrone();
}

// =============================================================================
//  The Whisperwood
//
//  East out of the Hollowmarch the trail runs under old forest to a fork: east
//  to Mossvale, a logging village, and north to Fernhollow, a hamlet on a pond.
//  Each is its own zone joined to the next by walking off the edge along the
//  path, the way DragonFable and AdventureQuest Worlds string their towns
//  together, so travelling somewhere is a walk through somewhere.
// =============================================================================

namespace ww {
static const int CELL = 32, W = 100, H = 44;
static const int FORK_CX = 70;

// Where the trail runs, where the branch to Fernhollow runs, and where the
// stream crosses under the plank bridge.
static float TrailY(float cx) {
    return 24.0f + sinf(cx * 0.085f) * 6.0f + sinf(cx * 0.029f + 1.3f) * 3.0f;
}
static float BranchX(float cy) { return FORK_CX + sinf(cy * 0.23f) * 2.5f; }
static float StreamX(float cy) { return 38.0f + sinf(cy * 0.17f) * 2.5f; }

// How far a cell is from the nearest bit of trail, in cells.
static float TrailGap(int cx, int cy) {
    float gap = fabsf(cy - TrailY(static_cast<float>(cx)));
    if (cy <= TrailY(static_cast<float>(FORK_CX)) + 1.0f)
        gap = std::min(gap, fabsf(cx - BranchX(static_cast<float>(cy))));
    return gap;
}
static bool Stream(int cx, int cy) { return fabsf(cx - StreamX(static_cast<float>(cy))) < 1.4f; }
}   // namespace ww

// Standing scenery with a trunk to walk into, not a canopy to walk under.
static void PlaceForestTree(MapBuilder& m, std::mt19937& rng, int x, int y, bool big) {
    m.Prop("objects", big ? Pick(kTrees, rng) : Pick(kSmallTrees, rng), x, y);
    if (big) m.Collision(x - 9, y - 9, 18, 9);
    else     m.Collision(x - 6, y - 6, 12, 6);
}

static void BuildWhisperwood() {
    using namespace ww;
    MapBuilder m("whisperwood_trail", "Whisperwood Trail", W * CELL, H * CELL);
    m.Ambient("forest");
    m.Subtitle("The old road east, under the trees");
    m.Background(22, 34, 24);
    std::mt19937 rng(7070u);

    // The woodcutter's camp, in a clearing north of the trail with a short
    // path down to it.
    const int camp_cx = 54;
    const int camp_cy = static_cast<int>(TrailY(static_cast<float>(camp_cx))) - 7;
    auto in_camp = [&](int cx, int cy) {
        const int dx = cx - camp_cx, dy = cy - camp_cy;
        return dx * dx + dy * dy * 2 < 30;
    };
    auto on_camp_path = [&](int cx, int cy) {
        return abs(cx - camp_cx) <= 1 && cy >= camp_cy &&
               cy <= TrailY(static_cast<float>(cx));
    };

    // --- ground ---------------------------------------------------------------
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float gap = TrailGap(cx, cy);
            const float v = Fbm(cx * 0.18f, cy * 0.18f, 31);
            string tile;
            if (Stream(cx, cy)) {
                // Planks where the trail crosses, water everywhere else.
                tile = (gap < 2.2f) ? VariantOf("plank_floor", cx, cy) : "water";
                if (gap >= 2.2f) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else if (gap < 1.3f || on_camp_path(cx, cy)) {
                tile = VariantOf(v > 0.55f ? "dirt_dark" : "dirt", cx, cy);
            } else if (gap < 3.5f || in_camp(cx, cy)) {
                tile = VariantOf(v > 0.5f ? "grass" : "grass_dark", cx, cy);
            } else {
                // The forest floor: moss and dark grass under the canopy. Not
                // dirt -- patches of it read as side paths that go nowhere.
                tile = VariantOf(v > 0.58f ? "moss" : "grass_dark", cx, cy);
            }
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
        }

    // The edges are forest, open only where the trail leaves.
    const int west_row  = static_cast<int>(TrailY(0.0f));
    const int east_row  = static_cast<int>(TrailY(static_cast<float>(W - 1)));
    const int north_col = static_cast<int>(BranchX(0.0f));
    for (int cx = 0; cx < W; ++cx) {
        if (abs(cx - north_col) > 2) m.Collision(cx * CELL, 0, CELL, CELL);
        m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        if (abs(cy - west_row) > 2) m.Collision(0, cy * CELL, CELL, CELL);
        if (abs(cy - east_row) > 2) m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    // --- the forest -------------------------------------------------------------
    // Dense enough that the canopies overlap and the trail is the way through.
    // A share of the big trees can be felled; the rest are scenery.
    int tree_i = 5000;
    int herb_i = 0;
    // Brookmint on the open banks either side of the stream.
    for (int cy = 2; cy < H - 2; ++cy)
        for (int cx = 2; cx < W - 2; ++cx) {
            const float off = fabsf(cx - StreamX(static_cast<float>(cy)));
            if (off < 1.4f || off >= 2.6f || TrailGap(cx, cy) < 3.0f) continue;
            if (Hash2(cx, cy, 8383) < 0.30f) PlaceHerb(m, "brookmint", cx * CELL + 16, cy * CELL + 20, herb_i);
        }
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (fabsf(cx - StreamX(static_cast<float>(cy))) < 2.6f) continue;
            if (in_camp(cx, cy) || on_camp_path(cx, cy)) continue;
            const float gap = TrailGap(cx, cy);
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            const float r = Hash2(cx, cy, 9191);

            if (gap < 3.5f) {
                // The verge: open, with the odd bush or mushroom at its edge,
                // and the nettles that like the light along a path.
                if (gap > 2.2f) {
                    if (r < 0.05f)      m.Prop("objects", Pick(kSmallBushes, rng), x, y);
                    else if (r < 0.08f) m.Prop("objects", Pick(kFungus, rng), x, y);
                    else if (Hash2(cx, cy, 8181) < 0.06f) PlaceHerb(m, "nettle", x, y, herb_i);
                }
                continue;
            }
            // Glowcaps want the shade just inside the trees, in the gaps
            // between trunks. The Whisperwood is where they grow best.
            if (gap < 6.0f && r >= 0.45f && Hash2(cx, cy, 8282) < 0.10f) {
                PlaceHerb(m, "glowcap", x, y, herb_i);
                continue;
            }
            const bool edge = (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4);
            if (r < (edge ? 0.55f : 0.24f)) {
                if (!edge && Hash2(cx, cy, 1212) < 0.18f)
                    PlaceTree(m, rng, tree_i++, x, y, true, 1, "logs");
                else
                    PlaceForestTree(m, rng, x, y, true);
            } else if (r < 0.32f) {
                PlaceForestTree(m, rng, x, y, false);
            } else if (r < 0.40f) {
                m.Prop("objects", Pick(kBushes, rng), x, y);
            } else if (r < 0.45f) {
                m.Prop("objects", Pick(kFungus, rng), x, y);
            }
        }

    // --- the woodcutter's camp ----------------------------------------------------
    {
        const int cx0 = camp_cx * CELL + 16, cy0 = camp_cy * CELL + 16;
        PlaceCampsite(m, "campsite_bram", cx0 - 72, cy0 - 8);
        m.Collision(cx0 - 72 - 30, cy0 - 8 - 16, 60, 16);
        {
            json& o = m.Object("range_bram", "range", cx0 + 4, cy0 + 34);
            o["sprite"] = "assets/props/campfire_ring.png";
            o["title"]  = "Camp fire";
            m.Collision(cx0 + 4 - 16, cy0 + 34 - 10, 32, 10);
        }
        m.Prop("props", "log_pile", cx0 + 76, cy0 - 6);
        m.Collision(cx0 + 76 - 16, cy0 - 6 - 10, 32, 10);
        m.Prop("props", "crates_sacks", cx0 + 70, cy0 + 50);
        m.Collision(cx0 + 70 - 18, cy0 + 50 - 10, 36, 10);
        m.Npc("npc_bram", "Bram the Woodcutter", "citizen2", cx0 + 36, cy0 + 14, "bram_root", 0)["shop"] = "whisperwood_woodcutter";
        // A pedlar who walks the trail between the villages, stopped at the
        // camp with his pack open.
        m.Prop("props", "travel_chest", cx0 - 136, cy0 + 44);
        m.Collision(cx0 - 136 - 16, cy0 + 34, 32, 10);
        m.Npc("npc_hob", "Hob the Pedlar", "citizen2", cx0 - 100, cy0 + 40, "hob_root", 0)["shop"] = "whisperwood_general";
    }

    // A waystone near the start of the trail, with the one piece of advice the
    // forest is known for.
    {
        const int sx = 18 * CELL + 16;
        const int sy = static_cast<int>(TrailY(18.0f) + 2.8f) * CELL + 16;
        json& o = m.Object("waystone_whisperwood", "sign", sx, sy);
        o["sprite"] = ObjPath("rock_05");
        o["title"]  = "Mossed waystone";
        o["text"]   = "The carving is almost gone under the moss.\n\n"
                      "WHISPERWOOD. KEEP TO THE PATH.\n\n"
                      "Scratched underneath, much later:\n"
                      "the trees are only trees. It is the foxes.";
        m.Collision(sx - 14, sy - 10, 28, 10);
    }

    // The fork.
    {
        const int fx = (FORK_CX + 4) * CELL + 16;
        const int fy = static_cast<int>(TrailY(static_cast<float>(FORK_CX + 4)) + 2.6f) * CELL + 16;
        json& o = m.Object("sign_whisperwood_fork", "sign", fx, fy);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Fork in the trail";
        o["text"]   = "MOSSVALE, east. The lodge fire is always lit.\n"
                      "FERNHOLLOW, north, to the still water.\n"
                      "THE HOLLOWMARCH, west, and Havenbrook beyond it.";
        m.Collision(fx - 16, fy - 10, 32, 10);
    }

    // Cut timber left at the trailside, where the carts load.
    for (int lx : {30, 86}) {
        const int x = lx * CELL + 16;
        const int y = static_cast<int>(TrailY(static_cast<float>(lx)) - 2.8f) * CELL + 16;
        m.Prop("props", "log_pile", x, y);
        m.Collision(x - 16, y - 10, 32, 10);
    }

    // --- wildlife ---------------------------------------------------------------
    // On the verges, where the trail brings the player past them.
    for (int cy = 2; cy < H - 2; cy += 3)
        for (int cx = 10; cx < W - 6; cx += 4) {
            const float gap = TrailGap(cx, cy);
            if (gap < 1.6f || gap > 3.4f || Stream(cx, cy) || in_camp(cx, cy)) continue;
            const float r = Hash2(cx, cy, 3131);
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (r < 0.22f)      m.Enemy("deer", x, y, 3);
            else if (r < 0.42f) m.Enemy("fox", x, y, 4);
            else if (r < 0.58f) m.Enemy("boar", x, y, 4);
            else if (r < 0.68f) m.Enemy("hare", x, y, 2);
        }

    // --- highwaymen ---------------------------------------------------------------
    // Bandits loitering at the trailside in twos, where a cart has to pass
    // them: two pairs before the fork and two past it, one either side of the
    // path. Quicker than an orc and no tougher than a boar, so a traveller
    // who can manage the foxes can manage them. They keep to the verge, and
    // stroll off it only to chase.
    {
        int n = 0;
        for (int lx : {24, 40, 64, 78})
            for (int side : {-1, 1}) {
                const int cx = lx + (side < 0 ? 0 : 2);
                const int cy = static_cast<int>(TrailY(static_cast<float>(cx)) + side * 2.4f);
                if (Stream(cx, cy) || in_camp(cx, cy) || on_camp_path(cx, cy)) continue;
                if (!m.Clear(cx * CELL + 16, cy * CELL + 16)) continue;
                m.Enemy("highwayman", cx * CELL + 16, cy * CELL + 16, 2 + (n++ % 3), 45.0f, 180.0f);
            }
    }

    // --- the ways out -------------------------------------------------------------
    const int wy = static_cast<int>(TrailY(0.0f) * CELL) + 16;
    m.Portal(0, wy - 72, 24, 144, "overworld", "from_whisperwood", "To the Hollowmarch", false);
    m.Spawn("from_hollowmarch", 88, static_cast<int>(TrailY(2.0f) * CELL) + 16);
    m.Spawn("default",          88, static_cast<int>(TrailY(2.0f) * CELL) + 16);

    const int ey = static_cast<int>(TrailY(static_cast<float>(W - 1)) * CELL) + 16;
    m.Portal(W * CELL - 24, ey - 72, 24, 144, "mossvale", "from_trail", "To Mossvale", false);
    m.Spawn("from_mossvale", W * CELL - 88,
            static_cast<int>(TrailY(static_cast<float>(W - 3)) * CELL) + 16);

    const int nx = static_cast<int>(BranchX(0.0f) * CELL) + 16;
    m.Portal(nx - 72, 0, 144, 24, "fernhollow", "from_trail", "To Fernhollow", false);
    m.Spawn("from_fernhollow", static_cast<int>(BranchX(2.0f) * CELL) + 16, 2 * CELL + 16);

    // Fishing: three spots on the stream's east edge, well clear of the bridge.
    {
        int n = 0;
        for (int cy : {7, 15, 34, 39}) {
            if (fabsf(cy - TrailY(StreamX(static_cast<float>(cy)))) < 5.0f || n >= 3) continue;
            for (int cx = W - 2; cx > 1; --cx) {
                if (!Stream(cx, cy)) continue;
                PlaceFishingSpot(m, "fish_stream_" + std::to_string(n++),
                                 cx * CELL + CELL - 8, cy * CELL + CELL / 2, "stream",
                                 {"raw_minnow", "raw_trout", "raw_salmon"}, 1);
                break;
            }
        }
    }

    // --- what comes out at night ----------------------------------------------------
    // Back among the trees, never on the verge: by day the trail is foxes and
    // footpads, and after dark there are wolves in the wood and the Mire's dead
    // have walked this far. The trail itself is left alone.
    for (int gy = 4; gy < H - 4; gy += 5)
        for (int gx = 12; gx < W - 8; gx += 7) {
            // Off the lattice a little, so they are not drawn up in ranks.
            const int cx = gx + static_cast<int>(Hash2(gx, gy, 2022) * 5.0f) - 2;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, 2023) * 5.0f) - 2;
            const float gap = TrailGap(cx, cy);
            if (gap < 5.0f || Stream(cx, cy) || in_camp(cx, cy) || on_camp_path(cx, cy)) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (!m.Clear(x, y) || m.NearestHaven(x, y) < 352.0f) continue;
            if (Hash2(cx, cy, 2020) > 0.30f) continue;
            m.NightEnemy({"wolf", "zombie"}, "night_" + std::to_string(cx) + "_" + std::to_string(cy), x, y, 1, 2, 0.5f);
        }
    std::printf("  the Whisperwood by night: %d posts\n", m.night_posts);

    // Swallowtails where the sun gets in: along the trail's verges and in the
    // woodcutter's clearing, never back under the trees.
    {
        int bug_i = 0;
        const int got = PlaceBugs(m, "swallowtail", 10, CELL, 3, 3, W - 3, H - 3, 7501u, 6.0f, [&](int cx, int cy) {
            if (fabsf(cx - StreamX(static_cast<float>(cy))) < 2.6f || on_camp_path(cx, cy)) return false;
            const float gap = TrailGap(cx, cy);
            return in_camp(cx, cy) || (gap > 1.8f && gap < 4.2f);
        }, bug_i);
        std::printf("  the Whisperwood: %d swallowtails\n", got);
    }

    m.Write("maps");
}

// --- the Westwold and the Brackenwood ---------------------------------------------
//
// West of Havenbrook, out of the gate the cross street always pointed at. The
// Westwold is open downs: Hidewater steading just outside the gate, where
// hides are bought and cloth is woven; ploughed fields with flax at their
// edges; the river Wend and its bridge; wolves on the far side of it; and in
// the west the ground climbs into the Howling Fells, which are not for anyone
// who has only just bought a bow. The Brackenwood is north of the fork: bears.

namespace wold {
struct Pt { float x, y; };

// How far a cell is from a road laid as a run of points, in cells.
static float Gap(const vector<Pt>& road, float cx, float cy) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < road.size(); ++i) {
        const float dx = road[i + 1].x - road[i].x, dy = road[i + 1].y - road[i].y;
        const float len2 = std::max(0.001f, dx * dx + dy * dy);
        const float t = std::clamp(((cx - road[i].x) * dx + (cy - road[i].y) * dy) / len2, 0.0f, 1.0f);
        const float px = road[i].x + dx * t - cx, py = road[i].y + dy * t - cy;
        best = std::min(best, sqrtf(px * px + py * py));
    }
    return best;
}
static float Gap(const vector<vector<Pt>>& roads, float cx, float cy) {
    float best = 1e9f;
    for (const auto& r : roads) best = std::min(best, Gap(r, cx, cy));
    return best;
}

}   // namespace wold

static void BuildWestwold() {
    using namespace wold;
    const int CELL = 32, W = 150, H = 104;
    MapBuilder m("westwold", "The Westwold", W * CELL, H * CELL);
    m.Ambient("overworld");
    m.Subtitle("Open downs west of Havenbrook, and the road across them");
    m.Background(52, 70, 46);
    std::mt19937 rng(6161u);

    // The road in from Havenbrook, over the Wend and on to the fork; from the
    // fork north to the Brackenwood, and west up into the Fells.
    const vector<Pt> main_road = {{149, 52}, {130, 50}, {112, 54}, {94, 50}, {78, 52}, {62, 56}, {44, 50}};
    const vector<Pt> north_road = {{44, 50}, {42, 36}, {39, 18}, {40, 0}};
    const vector<Pt> fell_road = {{44, 50}, {32, 46}, {20, 41}, {8, 35}};
    const vector<Pt> steading_path = {{128, 50}, {128, 43}};
    const vector<vector<Pt>> roads = {main_road, north_road, fell_road, steading_path};

    const auto river_x = [](float cy) { return 71.0f + sinf(cy * 0.11f) * 3.0f + sinf(cy * 0.043f + 0.7f) * 2.0f; };
    const auto river = [&](int cx, int cy) { return fabsf(cx - river_x(static_cast<float>(cy))) < 1.6f; };
    // Where the downs give way to rock: west of a wandering line.
    const auto fells = [](int cx, int cy) { return cx < 27.0f + sinf(cy * 0.13f) * 4.0f + sinf(cy * 0.047f) * 3.0f; };
    const auto high_fells = [](int cx, int cy) { return cx < 13.0f + sinf(cy * 0.17f) * 3.0f; };

    // Hidewater steading, north of the road a little way out of the gate, and
    // two fields either side of the road further on.
    const int st_cx = 128, st_cy = 40;
    const auto steading = [&](int cx, int cy) { return abs(cx - st_cx) <= 8 && abs(cy - st_cy) <= 5; };
    struct Field { int x0, y0, x1, y1; };
    const Field fields[] = {{96, 36, 110, 45}, {100, 59, 116, 67}, {84, 60, 94, 66}};
    const auto in_field = [&](int cx, int cy) {
        for (const Field& f : fields) if (cx >= f.x0 && cx < f.x1 && cy >= f.y0 && cy < f.y1) return true;
        return false;
    };
    const auto near_field = [&](int cx, int cy, int by) {
        for (const Field& f : fields)
            if (cx >= f.x0 - by && cx < f.x1 + by && cy >= f.y0 - by && cy < f.y1 + by) return true;
        return false;
    };
    // The standing stones, on a rise south of the road past the river.
    const int stones_cx = 54, stones_cy = 72;

    // --- ground -----------------------------------------------------------------
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float gap = Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
            const float v = Fbm(cx * 0.16f, cy * 0.16f, 515);
            const float copse = Fbm(cx * 0.07f, cy * 0.07f, 929);
            string tile;
            if (river(cx, cy)) {
                const bool bridge = gap < 2.0f;
                tile = bridge ? VariantOf("plank_floor", cx, cy) : "water";
                if (!bridge) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else if (gap < 1.2f) {
                // Cobbles as far as the town's own gate keeps them up, and a
                // cart track after that.
                tile = VariantOf(cx > 140 ? "road" : fells(cx, cy) ? "dirt_dark" : v > 0.55f ? "dirt_dark" : "dirt", cx, cy);
            } else if (in_field(cx, cy)) {
                // Ploughed in strips, two furrows and a ridge.
                tile = VariantOf(cy % 3 == 0 ? "dirt_dark" : "dirt", cx, cy);
            } else if (steading(cx, cy)) {
                tile = VariantOf(v > 0.55f ? "dirt" : "grass_light", cx, cy);
            } else if (high_fells(cx, cy)) {
                tile = VariantOf(v > 0.62f ? "snow" : v > 0.40f ? "frost_rock" : "crag", cx, cy);
            } else if (fells(cx, cy)) {
                tile = VariantOf(v > 0.60f ? "crag" : v > 0.36f ? "grass_olive" : "dirt_dark", cx, cy);
            } else if (copse > 0.62f && gap > 3.0f) {
                tile = VariantOf(v > 0.5f ? "grass_dark" : "grass", cx, cy);
            } else {
                tile = VariantOf(v > 0.62f ? "grass_light" : v > 0.30f ? "grass" : "grass_olive", cx, cy);
            }
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
        }

    // The edges, open only where a road leaves.
    for (int cx = 0; cx < W; ++cx) {
        if (abs(cx - 40) > 2) m.Collision(cx * CELL, 0, CELL, CELL);
        m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        m.Collision(0, cy * CELL, CELL, CELL);
        if (abs(cy - 52) > 2) m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    // --- what grows -----------------------------------------------------------------
    int tree_i = 7000, rock_i = 7000, herb_i = 0;
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (river(cx, cy) || in_field(cx, cy) || steading(cx, cy)) continue;
            const float gap = Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
            if (gap < 2.4f) continue;
            if (abs(cx - stones_cx) <= 4 && abs(cy - stones_cy) <= 4) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            const float r = Hash2(cx, cy, 4141);
            const float copse = Fbm(cx * 0.07f, cy * 0.07f, 929);
            const bool edge = (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4);
            const float bank = fabsf(cx - river_x(static_cast<float>(cy)));

            // Flax along the headlands, where the plough does not reach.
            if (near_field(cx, cy, 2) && !near_field(cx, cy, 1)) {
                if (Hash2(cx, cy, 2323) < 0.42f) PlaceHerb(m, "flax", x, y + 4, herb_i);
                continue;
            }
            if (near_field(cx, cy, 1)) continue;          // the fence line
            if (bank < 3.2f) {
                if (bank > 1.8f && Hash2(cx, cy, 8383) < 0.22f) PlaceHerb(m, "brookmint", x, y + 4, herb_i);
                else if (bank > 1.7f && r < 0.10f) m.Prop("props", "reeds", x, y + 4);
                continue;
            }
            if (fells(cx, cy)) {
                // Rock, scree and the odd pine; ore in the high ground.
                if (r < (edge ? 0.5f : 0.10f)) {
                    m.Prop("objects", kRocks[(cx * 3 + cy) % 8], x, y);
                    m.Collision(x - 18, y - 10, 36, 10);
                } else if (r < 0.15f) {
                    m.Prop("props", "snow_pine", x, y);
                    m.Collision(x - 8, y - 8, 16, 8);
                } else if (high_fells(cx, cy) && r > 0.965f) {
                    PlaceRock(m, rng, rock_i++, x, y, true, 40, "damascus_ore");
                } else if (r > 0.95f) {
                    PlaceRock(m, rng, rock_i++, x, y, (cx + cy) % 2 == 0, 20, "coal");
                } else if (Hash2(cx, cy, 7171) < 0.03f) {
                    PlaceHerb(m, "mountain_sage", x, y, herb_i);
                }
                continue;
            }
            if (edge && r < 0.6f) { PlaceForestTree(m, rng, x, y, true); continue; }
            if (copse > 0.62f) {
                // A copse: oak to fell, bushes under it, nettles at its edge.
                if (r < 0.20f) {
                    if (Hash2(cx, cy, 1212) < 0.35f) PlaceTree(m, rng, tree_i++, x, y, true, 15, "oak_logs");
                    else PlaceForestTree(m, rng, x, y, true);
                } else if (r < 0.30f) {
                    PlaceForestTree(m, rng, x, y, false);
                } else if (r < 0.38f) {
                    m.Prop("objects", Pick(kBushes, rng), x, y);
                } else if (Hash2(cx, cy, 8181) < 0.06f) {
                    PlaceHerb(m, "nettle", x, y, herb_i);
                }
                continue;
            }
            // Open down: a bush here and there, marigolds, a lone tree.
            if (r < 0.018f)      PlaceForestTree(m, rng, x, y, r < 0.008f);
            else if (r < 0.045f) m.Prop("objects", Pick(kSmallBushes, rng), x, y);
            else if (r < 0.055f) { m.Prop("objects", kSmallRocks[(cx + cy) % 4], x, y); }
            else if (Hash2(cx, cy, 5151) < 0.035f) PlaceHerb(m, "marigold", x, y, herb_i);
        }

    // --- the fields ---------------------------------------------------------------------
    // Fenced, a gate on the road side, a rick in one corner.
    {
        int n = 0;
        for (const Field& f : fields) {
            const bool north_of_road = f.y1 < 52;
            const int gate = (f.x0 + f.x1) / 2;
            Fence(m, CELL, f.x0 - 1, f.x1 + 1, f.y0 - 1, north_of_road ? -999 : gate);
            Fence(m, CELL, f.x0 - 1, f.x1 + 1, f.y1, north_of_road ? gate : -999);
            // The ends, as posts of collision: the fence art runs east-west.
            m.Collision((f.x0 - 1) * CELL + 8, (f.y0 - 1) * CELL + 12, 8, (f.y1 - f.y0 + 1) * CELL);
            m.Collision(f.x1 * CELL + 16, (f.y0 - 1) * CELL + 12, 8, (f.y1 - f.y0 + 1) * CELL);
            const int rx = (f.x1 - 2) * CELL, ry = (f.y0 + 2) * CELL;
            m.Prop("props", "hay_rick", rx, ry);
            m.Collision(rx - 22, ry - 14, 44, 14);
            if (n++ == 0) {
                m.Prop("props", "hay_rick", rx - 3 * CELL, ry + CELL);
                m.Collision(rx - 3 * CELL - 22, ry + CELL - 14, 44, 14);
            }
        }
    }

    // --- Hidewater steading ---------------------------------------------------------------
    {
        const int sx = st_cx * CELL, sy = st_cy * CELL;
        // Drying frames along the north side, hides on every one -- and
        // worked at, every one: they are the steading's station.
        for (int k = 0; k < 4; ++k)
            PlaceTanningRack(m, "rack_hidewater_" + std::to_string(k), sx - 170 + k * 84, sy - 96);
        m.Prop("props", "tent", sx - 196, sy + 10);
        m.Collision(sx - 196 - 30, sy + 10 - 16, 60, 16);
        m.Prop("props", "tent", sx + 200, sy - 30);
        m.Collision(sx + 200 - 30, sy - 30 - 16, 60, 16);
        PlaceCampsite(m, "campsite_hidewater", sx + 200, sy + 40);

        // The dye vat, and a fire to cook on. (There was a carpenter's bench
        // here "where hides become leathers". They become leathers on the
        // frames.)
        PlaceCauldron(m, "cauldron_hidewater", sx + 40, sy + 10);
        {
            json& o = m.Object("range_hidewater", "range", sx + 110, sy + 60);
            o["sprite"] = "assets/props/campfire_ring.png";
            o["title"]  = "Cooking fire";
            m.Collision(sx + 110 - 16, sy + 60 - 12, 32, 12);
        }
        // The weaver's stall, her wheel beside it.
        m.Prop("props", "market_stall", sx + 120, sy - 40);
        m.Collision(sx + 120 - 32, sy - 40 - 14, 64, 14);
        m.Prop("props", "spinning_wheel", sx + 60, sy - 50);
        m.Collision(sx + 60 - 14, sy - 50 - 10, 28, 10);
        m.Prop("props", "crates_sacks", sx - 130, sy + 52);
        m.Collision(sx - 130 - 18, sy + 52 - 10, 36, 10);
        m.Prop("props", "barrel", sx + 4, sy - 60);
        m.Collision(sx + 4 - 14, sy - 60 - 10, 28, 10);

        m.Npc("npc_orla", "Orla the Tanner", "citizen2", sx - 96, sy + 40, "orla_root", 0)["shop"] = "westwold_tanner";
        m.Npc("npc_isolde", "Isolde the Weaver", "citizen1", sx + 84, sy - 22, "isolde_root", 0)["shop"] = "westwold_weaver";

        json& sign = m.Object("sign_hidewater", "sign", st_cx * CELL + 40, 47 * CELL);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "Hidewater";
        sign["text"]   = "HIDEWATER STEADING\n\nHides bought, any beast, any size. Leathers cut to order.\n\n"
                         "Below, in a neater hand: cloth woven and dyed. Bring flax, or buy it.";
        m.Collision(st_cx * CELL + 40 - 16, 47 * CELL - 10, 32, 10);
    }

    // A farmer on his round between the fields, by the clock: see Npc. From
    // the north field's gate he turns west along the headland to his hives
    // and stands looking them over a while, then comes back and goes on
    // across the road to the other two.
    {
        json& n = m.Npc("npc_aldous", "Farmer Aldous", "citizen2", 103 * CELL, 47 * CELL, "aldous_root", 0);
        n["path"] = json::array({json::array({103 * CELL, 47 * CELL, 12.0f, 3}),
                                 json::array({93 * CELL + 16, 46 * CELL + 16, 0.0f, 0}),
                                 json::array({93 * CELL, 39 * CELL + 8, 14.0f, 1}),
                                 json::array({93 * CELL + 16, 46 * CELL + 16, 0.0f, 0}),
                                 json::array({103 * CELL, 47 * CELL + 8, 0.0f, 0}),
                                 json::array({103 * CELL, 52 * CELL, 0.0f, 0}),
                                 json::array({108 * CELL, 56 * CELL + 16, 0.0f, 0}),
                                 json::array({108 * CELL, 58 * CELL, 14.0f, 0}),
                                 json::array({90 * CELL, 57 * CELL, 0.0f, 0}),
                                 json::array({89 * CELL, 59 * CELL, 12.0f, 0})});
        n["ping_pong"] = true;
        n["speed"] = 26.0f;
        n["hours"] = {6.0f, 19.0f};
    }

    // --- the standing stones ---------------------------------------------------------------
    {
        const int cx0 = stones_cx * CELL + 16, cy0 = stones_cy * CELL + 16;
        for (int k = 0; k < 7; ++k) {
            const float a = 6.2831853f * k / 7.0f;
            const int x = cx0 + static_cast<int>(cosf(a) * 84.0f), y = cy0 + static_cast<int>(sinf(a) * 60.0f);
            m.Prop("objects", kRocks[(k * 3) % 8], x, y);
            m.Collision(x - 16, y - 10, 32, 10);
        }
        json& o = m.Object("waystone_shepherds", "sign", cx0, cy0 + 8);
        o["sprite"] = ObjPath("rock_05");
        o["title"]  = "The Shepherds' Ring";
        o["text"]   = "Seven stones, and older than the road.\n\nCut into the tallest, the marks of a tally nobody "
                      "keeps any more: sheep out, sheep back. The second column stops short.";
        m.Collision(cx0 - 14, cy0 - 2, 28, 10);
        PlaceChest(m, "chest_shepherds_ring", cx0 + 40, cy0 + 30, "chest_common");
    }

    // --- signs --------------------------------------------------------------------------------
    {
        json& o = m.Object("sign_westwold_fork", "sign", 46 * CELL, 47 * CELL);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "The fork";
        o["text"]   = "THE BRACKENWOOD, north. Bears. Do not leave food by the path.\n"
                      "THE HOWLING FELLS, west. Do not.\n"
                      "HAVENBROOK, east, and supper.";
        m.Collision(46 * CELL - 16, 47 * CELL - 10, 32, 10);
    }
    {
        json& o = m.Object("sign_fells", "sign", 29 * CELL, 43 * CELL);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A warning, nailed to a post";
        o["text"]   = "THE HOWLING FELLS\n\nGreatwolves hunt the high ground. They are the size of a pony and they "
                      "are not afraid of you.\n\nCombat 55, and company, or turn round.";
        m.Collision(29 * CELL - 16, 43 * CELL - 10, 32, 10);
    }

    // --- wildlife -------------------------------------------------------------------------------
    // East of the Wend it is a walk in the fields; west of it the wolves run in
    // twos and threes; and in the Fells the wolves are something else.
    for (int cy = 4; cy < H - 4; cy += 6)
        for (int cx = 4; cx < W - 6; cx += 7) {
            if (river(cx, cy) || in_field(cx, cy) || near_field(cx, cy, 2) || steading(cx, cy)) continue;
            if (Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) < 2.0f) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (!m.Clear(x, y)) continue;
            const float r = Hash2(cx, cy, 3737);
            if (high_fells(cx, cy)) {
                if (r < 0.55f) m.Enemy("greatwolf", x, y, 1 + static_cast<int>(r * 6.0f) % 3, 60.0f, 320.0f);
            } else if (fells(cx, cy)) {
                if (r < 0.30f) m.Enemy("greatwolf", x, y, 1, 60.0f, 320.0f);
            } else if (cx < river_x(static_cast<float>(cy)) - 3.0f) {
                if (r < 0.34f) {
                    m.Enemy("wolf", x, y, 2 + static_cast<int>(r * 20.0f) % 3, 35.0f, 300.0f);
                    if (r < 0.20f && m.Clear(x + 40, y + 24)) m.Enemy("wolf", x + 40, y + 24, 2, 35.0f, 300.0f);
                } else if (r < 0.46f) m.Enemy("boar", x, y, 5);
                else if (r < 0.56f)   m.Enemy("deer", x, y, 4);
            } else if (cx < 140) {
                if (r < 0.14f)      m.Enemy("hare", x, y, 2);
                else if (r < 0.28f) m.Enemy("deer", x, y, 3);
                else if (r < 0.40f) m.Enemy("fox", x, y, 3);
                else if (r < 0.50f) m.Enemy("boar", x, y, 3);
                else if (r < 0.55f && cx < 96) m.Enemy("wolf", x, y, 1, 35.0f, 300.0f);
            }
        }
    // Highwaymen either end of the bridge, where a cart has to slow.
    {
        int n = 0;
        for (int lx : {66, 76})
            for (int side : {-1, 1}) {
                const int cy = (lx == 66 ? 55 : 52) + side * 3;
                if (!m.Clear(lx * CELL + 16, cy * CELL + 16)) continue;
                m.Enemy("highwayman", lx * CELL + 16, cy * CELL + 16, 3 + (n++ % 3), 45.0f, 180.0f);
            }
    }

    // Fishing on the Wend, away from the bridge.
    {
        int n = 0;
        for (int cy : {14, 30, 70, 88})
            // The east edge of the water on that row, cast at from the bank.
            for (int cx = W - 2; cx > 1; --cx) {
                if (!river(cx, cy)) continue;
                PlaceFishingSpot(m, "fish_wend_" + std::to_string(n++), cx * CELL + CELL - 8, cy * CELL + CELL / 2,
                                 "stream", {"raw_minnow", "raw_trout", "raw_pike"}, 1);
                break;
            }
    }

    // --- the ways out ------------------------------------------------------------------------------
    m.Portal(W * CELL - 24, 52 * CELL - 72, 24, 144, "town_havenbrook", "from_westwold", "To Havenbrook", false);
    m.Spawn("from_havenbrook", W * CELL - 96, 52 * CELL + 16);
    m.Spawn("default",         W * CELL - 96, 52 * CELL + 16);
    m.Portal(40 * CELL - 72, 0, 144, 24, "brackenwood", "from_westwold", "To the Brackenwood", false);
    m.Danger(20);
    m.Spawn("from_brackenwood", 40 * CELL + 16, 3 * CELL);

    // --- what comes out at night ----------------------------------------------------
    // Off the roads and away from Hidewater. East of the Wend, where by day it
    // is a walk in the fields, bats come over the downs and the dead come up
    // out of the low ground -- not wolves: there are wolves here by day, and
    // what comes out at night is what does not live here. West of it, where
    // the wolves are, a bear down from the Brackenwood, or a wraith. The Fells
    // are left as they are: nothing that comes out at night is worse than what
    // lives there.
    for (int gy = 6; gy < H - 6; gy += 9)
        for (int gx = 30; gx < W - 8; gx += 9) {
            // Off the lattice a little, so they are not drawn up in ranks.
            const int cx = gx + static_cast<int>(Hash2(gx, gy, 2022) * 5.0f) - 2;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, 2023) * 5.0f) - 2;
            if (river(cx, cy) || in_field(cx, cy) || near_field(cx, cy, 3) || steading(cx, cy) || fells(cx, cy)) continue;
            if (Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) < 6.0f) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (!m.Clear(x, y) || m.NearestHaven(x, y) < 352.0f) continue;
            const bool west = cx < river_x(static_cast<float>(cy));
            // There is less of the west bank, between the river and the Fells.
            if (Hash2(cx, cy, 2020) > (west ? 0.42f : 0.22f)) continue;
            const string group = "night_" + std::to_string(cx) + "_" + std::to_string(cy);
            if (west) m.NightEnemy({"bear", "wraith"}, group, x, y, 1, 1, 0.5f);
            else {
                m.NightEnemy({"bat", "zombie"}, group, x, y, 1, 2, 0.5f);
                if (Hash2(cx, cy, 2021) < 0.4f && m.Clear(x + 44, y + 26))
                    m.NightEnemy({"bat", "zombie"}, group, x + 44, y + 26, 1, 1, 0.5f);
            }
        }
    std::printf("  the Westwold by night: %d posts\n", m.night_posts);

    // --- Farmer Aldous's bees ------------------------------------------------------------------
    // West of his north field, where his round turns in to look them over: an
    // open bee shed of skeps at the back between two lengths of fence, then
    // two rows of hives -- painted boxes and straw skeps -- with lavender
    // between the rows and along the front, and a board at the corner by the
    // road. Three cells clear of the field's flax and of the road, so neither
    // is touched; laid after the night's posts, and further from every one of
    // them than they keep from anybody, so none of those moves either. The
    // wild marigolds that grew where it stands are dug out: the lavender is
    // there instead.
    {
        const int ax0 = 82 * CELL, ay0 = 35 * CELL, ax1 = 93 * CELL, ay1 = 47 * CELL - 4;
        auto& objs = m.dq["objects"];
        for (size_t i = objs.size(); i-- > 0;) {
            const json& o = objs[i];
            const int x = o["x"], y = o["y"];
            if (o.value("type", string()) == "herb" && x >= ax0 && x < ax1 && y >= ay0 && y < ay1)
                objs.erase(i);
        }
        Fence(m, CELL, 82, 86, 35);
        Fence(m, CELL, 90, 92, 35);
        m.Prop("props", "bee_shed", 2800, 1200);
        m.Collision(2800 - 44, 1200 - 18, 88, 18);
        m.Prop("props", "crates_sacks", 2730, 1206);
        m.Collision(2730 - 18, 1206 - 10, 36, 10);
        m.Prop("props", "barrel", 2868, 1204);
        m.Collision(2868 - 14, 1204 - 10, 28, 10);
        struct Hive { const char* art; int x, y; };
        const Hive hives[] = {
            {"beehive", 2680, 1280}, {"beehive_blue", 2752, 1280}, {"bee_skep", 2824, 1280}, {"beehive_green", 2896, 1280},
            {"bee_skep", 2704, 1408}, {"beehive_green", 2776, 1408}, {"beehive", 2848, 1408}, {"beehive_blue", 2920, 1408},
        };
        int n = 0;
        for (const Hive& h : hives) {
            const bool skep = string(h.art) == "bee_skep";
            PlaceHive(m, "hive_aldous_" + std::to_string(n++), h.art, skep ? "straw skep" : "beehive", h.x, h.y,
                      skep ? 20 : 28);
        }
        for (const auto& b : {std::pair<int, int>{2716, 1344}, {2860, 1344}, {2728, 1468}, {2824, 1468}}) {
            m.Prop("props", "lavender_bed", b.first, b.second);
            m.Collision(b.first - 29, b.second - 12, 58, 12);
        }
        int f = 0;
        for (const auto& p : {std::pair<int, int>{2646, 1330}, {2958, 1322}, {2648, 1446}, {2790, 1378},
                              {2690, 1232}, {2946, 1206}}) {
            m.Overlay("decor", "flowers_" + std::to_string(f++ % 5), p.first, p.second);
        }
        json& sign = m.Object("sign_apiary", "sign", 2944, 1480);
        sign["sprite"] = "assets/props/signpost.png";
        sign["title"]  = "Aldous's bees";
        sign["text"]   = "ALDOUS'S BEES\n\nTake what honey there is and welcome, and shut the hive after you. Go slow and "
                         "they will let you.\n\nDo not take it all. They have to eat too.";
        m.Collision(2944 - 16, 1480 - 10, 32, 10);
    }

    PlaceCurios(m);
    m.Write("maps");
}

static void BuildBrackenwood() {
    using namespace wold;
    const int CELL = 32, W = 130, H = 110;
    MapBuilder m("brackenwood", "The Brackenwood", W * CELL, H * CELL);
    m.Ambient("forest");
    m.Subtitle("Old forest north of the Westwold. It has bears in it");
    m.Background(18, 28, 20);
    std::mt19937 rng(8282u);

    const vector<Pt> trail = {{64, 109}, {62, 94}, {70, 80}, {60, 66}, {66, 52}, {64, 40}, {64, 33}};
    const vector<Pt> west_trail = {{60, 66}, {44, 62}, {32, 54}, {24, 44}};
    const vector<Pt> east_trail = {{70, 80}, {88, 74}, {102, 62}, {108, 50}};
    const vector<Pt> old_trail = {{64, 33}, {72, 20}, {86, 12}, {104, 9}};
    const vector<vector<Pt>> trails = {trail, west_trail, east_trail, old_trail};

    // Clearings: the trapper's camp, the den, and where the side trails end.
    struct Glade { int cx, cy, r; };
    const Glade camp = {53, 68, 5}, den = {64, 29, 8}, west_glade = {22, 42, 6}, east_glade = {109, 48, 6},
                old_glade = {106, 9, 6};
    const Glade glades[] = {camp, den, west_glade, east_glade, old_glade};
    const auto in_glade = [&](int cx, int cy) {
        for (const Glade& g : glades) {
            const int dx = cx - g.cx, dy = cy - g.cy;
            if (dx * dx + dy * dy * 2 < g.r * g.r) return true;
        }
        return false;
    };
    const auto old_growth = [](int cx, int cy) { return cy < 22.0f + sinf(cx * 0.11f) * 3.0f; };

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float gap = Gap(trails, static_cast<float>(cx), static_cast<float>(cy));
            const float v = Fbm(cx * 0.18f, cy * 0.18f, 77);
            string tile;
            if (gap < 1.2f)                       tile = VariantOf(v > 0.55f ? "dirt_dark" : "dirt", cx, cy);
            else if (gap < 3.2f || in_glade(cx, cy)) tile = VariantOf(v > 0.5f ? "grass" : "grass_dark", cx, cy);
            else if (old_growth(cx, cy))          tile = VariantOf(v > 0.5f ? "moss" : "peat", cx, cy);
            else                                  tile = VariantOf(v > 0.58f ? "moss" : "grass_dark", cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
        }

    for (int cx = 0; cx < W; ++cx) {
        m.Collision(cx * CELL, 0, CELL, CELL);
        if (abs(cx - 64) > 2) m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        m.Collision(0, cy * CELL, CELL, CELL);
        m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    // --- the forest -------------------------------------------------------------------
    int tree_i = 8000, herb_i = 0;
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (in_glade(cx, cy)) continue;
            const float gap = Gap(trails, static_cast<float>(cx), static_cast<float>(cy));
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            const float r = Hash2(cx, cy, 6262);
            if (gap < 3.2f) {
                if (gap > 2.0f) {
                    if (r < 0.06f)      m.Prop("objects", Pick(kSmallBushes, rng), x, y);
                    else if (r < 0.09f) m.Prop("objects", Pick(kFungus, rng), x, y);
                    else if (Hash2(cx, cy, 8181) < 0.07f) PlaceHerb(m, "nettle", x, y, herb_i);
                }
                continue;
            }
            if (gap < 6.5f && r >= 0.46f && Hash2(cx, cy, 8282) < (old_growth(cx, cy) ? 0.14f : 0.07f)) {
                PlaceHerb(m, "glowcap", x, y, herb_i);
                continue;
            }
            const bool edge = (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4);
            const float dense = old_growth(cx, cy) ? 0.30f : 0.25f;
            if (r < (edge ? 0.58f : dense)) {
                if (!edge && Hash2(cx, cy, 1212) < 0.16f)
                    PlaceTree(m, rng, tree_i++, x, y, true, old_growth(cx, cy) ? 30 : 15, "oak_logs");
                else
                    PlaceForestTree(m, rng, x, y, true);
            } else if (r < dense + 0.08f) {
                PlaceForestTree(m, rng, x, y, false);
            } else if (r < dense + 0.17f) {
                m.Prop("objects", Pick(kBushes, rng), x, y);
            } else if (r < dense + 0.22f) {
                m.Prop("objects", Pick(kFungus, rng), x, y);
            }
        }

    // --- the trapper's camp -----------------------------------------------------------------
    {
        const int cx0 = camp.cx * CELL + 16, cy0 = camp.cy * CELL + 16;
        PlaceCampsite(m, "campsite_brackenwood", cx0 - 70, cy0 - 12);
        m.Collision(cx0 - 70 - 30, cy0 - 12 - 16, 60, 16);
        {
            json& o = m.Object("range_brackenwood", "range", cx0 + 6, cy0 + 30);
            o["sprite"] = "assets/props/campfire_ring.png";
            o["title"]  = "Camp fire";
            m.Collision(cx0 + 6 - 16, cy0 + 30 - 10, 32, 10);
        }
        // A trapper's frame is a tanner's frame: bear hide can be cut where
        // the bear was.
        PlaceTanningRack(m, "rack_brackenwood", cx0 + 84, cy0 - 20);
        m.Prop("props", "log_pile", cx0 - 10, cy0 - 60);
        m.Collision(cx0 - 10 - 16, cy0 - 70, 32, 10);
        m.Npc("npc_hale", "Hale the Trapper", "player_warden", cx0 + 40, cy0 + 8, "hale_root", 0)["shop"] = "brackenwood_trapper";
    }

    // --- the den ---------------------------------------------------------------------------------
    {
        const int dx0 = den.cx * CELL + 16, dy0 = (den.cy - 3) * CELL;
        m.Prop("props", "bear_den", dx0, dy0);
        m.Collision(dx0 - 66, dy0 - 44, 132, 44);
        m.Enemy("den_mother", dx0, dy0 + 96, 1, 600.0f, 300.0f);
        m.Enemy("bear", dx0 - 130, dy0 + 120, 2, 50.0f, 260.0f);
        m.Enemy("bear", dx0 + 140, dy0 + 100, 2, 50.0f, 260.0f);
        json& o = m.Object("sign_den", "sign", den.cx * CELL + 16 - 120, (den.cy + 5) * CELL);
        o["sprite"] = ObjPath("rock_05");
        o["title"]  = "Claw marks, as high as you can reach";
        o["text"]   = "Four furrows down the face of the stone, a hand deep.\n\n"
                      "Whatever made them stood up to do it.";
        m.Collision(den.cx * CELL + 16 - 134, (den.cy + 5) * CELL - 10, 28, 10);
    }

    // --- signs -----------------------------------------------------------------------------------
    {
        json& o = m.Object("sign_brackenwood", "sign", 66 * CELL + 16, 104 * CELL);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "The Brackenwood";
        o["text"]   = "THE BRACKENWOOD\n\nKeep to the trail. A bear that has not seen you is a bear you can walk round.\n\n"
                      "Hale the trapper camps at the second bend and buys what you bring out.";
        m.Collision(66 * CELL, 104 * CELL - 10, 32, 10);
    }
    {
        json& o = m.Object("sign_old_growth", "sign", 67 * CELL, 25 * CELL);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A board, split down the middle";
        o["text"]   = "THE OLD GROWTH\n\nThe bears past here are not the bears behind you. Dire bears: twice the size, "
                      "and the hide turns a spear.\n\nCombat 65, or go home.";
        m.Collision(67 * CELL - 16, 25 * CELL - 10, 32, 10);
    }
    PlaceChest(m, "chest_brackenwood_west", west_glade.cx * CELL, west_glade.cy * CELL - 30, "chest_common");
    PlaceChest(m, "chest_old_growth", old_glade.cx * CELL + 30, old_glade.cy * CELL, "chest_dungeon");

    // --- what lives here ---------------------------------------------------------------------------
    // Wolves on the way in, bears in the glades and along the inner trails,
    // and the dire bears in the Old Growth.
    for (const Glade& g : {west_glade, east_glade}) {
        m.Enemy("bear", g.cx * CELL - 60, g.cy * CELL + 10, 1, 50.0f, 260.0f);
        m.Enemy("bear", g.cx * CELL + 70, g.cy * CELL + 40, 3, 50.0f, 260.0f);
    }
    m.Enemy("dire_bear", old_glade.cx * CELL - 40, old_glade.cy * CELL + 40, 2, 120.0f, 300.0f);
    for (int cy = 3; cy < H - 3; cy += 3)
        for (int cx = 4; cx < W - 4; cx += 4) {
            const float gap = Gap(trails, static_cast<float>(cx), static_cast<float>(cy));
            if (gap < 1.6f || gap > 5.5f || in_glade(cx, cy)) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (!m.Clear(x, y)) continue;
            const float r = Hash2(cx, cy, 9393);
            if (old_growth(cx, cy)) {
                if (r < 0.55f) m.Enemy("dire_bear", x, y, 1, 120.0f, 300.0f);
            } else if (cy > 84) {
                if (r < 0.45f)      m.Enemy("wolf", x, y, 3 + static_cast<int>(r * 10.0f) % 3, 35.0f, 300.0f);
                else if (r < 0.60f) m.Enemy("boar", x, y, 6);
            } else {
                if (r < 0.34f)      m.Enemy("bear", x, y, 1 + static_cast<int>(r * 10.0f) % 3, 50.0f, 260.0f);
                else if (r < 0.48f) m.Enemy("wolf", x, y, 4, 35.0f, 300.0f);
                else if (r < 0.58f) m.Enemy("deer", x, y, 5);
            }
        }

    m.Portal(64 * CELL - 72, H * CELL - 24, 144, 24, "westwold", "from_brackenwood", "To the Westwold", false);
    m.Spawn("from_westwold", 64 * CELL + 16, (H - 4) * CELL);
    m.Spawn("default",       64 * CELL + 16, (H - 4) * CELL);

    // --- what comes out at night ----------------------------------------------------
    // An old forest has old dead in it. Off the trails and out of the glades:
    // wraiths and the grave-walkers of the deep well in the south of the wood,
    // where by day it is wolves; the walkers and the wailing ones further in,
    // among the bears. The Old Growth is left to the dire bears, who are worse.
    for (int gy = 8; gy < H - 8; gy += 8)
        for (int gx = 8; gx < W - 8; gx += 9) {
            // Off the lattice a little, so they are not drawn up in ranks.
            const int cx = gx + static_cast<int>(Hash2(gx, gy, 2022) * 5.0f) - 2;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, 2023) * 5.0f) - 2;
            const float gap = Gap(trails, static_cast<float>(cx), static_cast<float>(cy));
            // Nor anywhere near it: what is posted beside a dire bear is not the worst thing there.
            if (gap < 5.0f || gap > 14.0f || in_glade(cx, cy) || old_growth(cx, cy - 14)) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (!m.Clear(x, y) || m.NearestHaven(x, y) < 352.0f) continue;
            if (Hash2(cx, cy, 2020) > 0.46f) continue;
            const string group = "night_" + std::to_string(cx) + "_" + std::to_string(cy);
            if (cy > 84) m.NightEnemy({"wraith", "ankou"}, group, x, y, 1, 1, 0.5f);
            else         m.NightEnemy({"ankou", "banshee"}, group, x, y, 1, 1, 0.5f);
        }
    std::printf("  the Brackenwood by night: %d posts\n", m.night_posts);

    // Swallowtails in the glades and along the sunny edges of the trails --
    // not in the Old Growth, and not round the den.
    {
        int bug_i = 0;
        const int got = PlaceBugs(m, "swallowtail", 10, CELL, 3, 3, W - 3, H - 3, 7511u, 7.0f, [&](int cx, int cy) {
            if (old_growth(cx, cy - 3)) return false;
            const int ddx = cx - den.cx, ddy = cy - den.cy;
            if (ddx * ddx + ddy * ddy * 2 < 196) return false;
            const float gap = Gap(trails, static_cast<float>(cx), static_cast<float>(cy));
            return in_glade(cx, cy) || (gap > 1.8f && gap < 4.0f);
        }, bug_i);
        std::printf("  the Brackenwood: %d swallowtails\n", got);
    }

    PlaceCurios(m);
    m.Write("maps");
}

// --- Mossvale ------------------------------------------------------------------

// =============================================================================
//  The Bayou
//
//  The deep swamp west of the lizardmen's camp, laid out from a guide the
//  user drew in LevelEdit-Plus (exports/Bayou/Bayou.mx in that tree). None of
//  the guide's art is used: it says where things go. Its dark ground is the
//  track; its rings of puddles are the shores of the water; its pillars are the
//  corners of decks raised on stilts over two lakes, and its rising posts the
//  ramps up to them; its runs of medium ground are palisades of sharpened
//  stakes round two camps, with a gate wherever it left a gap; its flowers are
//  herbs; its bushes are the map's four corners; and its enemy icons are posts.
//  The tables below were made from it by tools/bayou_guide.py, moved into the
//  game's frame, and are the only place any of that lives.
//
//  What lives in the water does not show itself: the Drowned, the Fen Gators
//  and the Bog Lurkers wait under it and come up when somebody walks too near
//  the edge (EnemySpawnDef::lurk). A ripple is all there is to see.
// =============================================================================
// ---- generated from the Bayou guide by tools/bayou_guide.py: edit that, not this ----
static const int BY_W = 226, BY_H = 88, BY_CELL = 32;
static const std::vector<std::pair<int, int>> kBayouLake_entrance = {{5878, 381}, {5889, 336}, {5845, 29}, {6895, 29}, {6862, 334}, {6837, 367}, {6796, 422}, {6736, 459}, {6649, 510}, {6606, 524}, {6488, 547}, {6373, 552}, {6262, 551}, {6117, 542}, {6004, 487}, {5916, 457}, {5900, 417}};
static const std::vector<std::pair<int, int>> kBayouLake_southeast = {{6195, 2497}, {6300, 2434}, {6434, 2379}, {6565, 2355}, {6711, 2336}, {6879, 2327}, {6961, 2353}, {7024, 2386}, {7032, 2442}, {6740, 2495}, {6850, 2501}, {7029, 2548}, {6946, 2545}, {6605, 2544}, {6345, 2683}, {6431, 2611}, {6378, 2646}, {6485, 2562}, {6255, 2687}, {6157, 2631}, {6145, 2545}};
static const std::vector<std::pair<int, int>> kBayouLake_village_n = {{1751, 26}, {2079, 20}, {2424, 20}, {2712, 20}, {2969, 92}, {3108, 120}, {3235, 348}, {3202, 585}, {3007, 655}, {2664, 668}, {2204, 652}, {1927, 556}, {1741, 335}};
static const std::vector<std::pair<int, int>> kBayouLake_pond_b = {{2675, 1401}, {2712, 1324}, {2853, 1226}, {2930, 1241}, {3020, 1286}, {3083, 1339}, {3106, 1430}, {3082, 1529}, {2983, 1603}, {2795, 1601}, {2667, 1519}};
static const std::vector<std::pair<int, int>> kBayouLake_pond_a = {{1170, 1370}, {1276, 1300}, {1468, 1235}, {1725, 1231}, {1828, 1316}, {1887, 1471}, {1794, 1667}, {1499, 1751}, {1278, 1747}, {1141, 1680}, {1132, 1514}};
static const std::vector<std::pair<int, int>> kBayouLake_glade = {{325, 604}, {399, 427}, {618, 433}, {776, 568}, {809, 712}, {734, 893}, {468, 938}, {301, 745}};
static const std::vector<std::pair<int, int>> kBayouLake_pond_sw = {{3207, 2445}, {3363, 2388}, {3559, 2376}, {3727, 2416}, {3724, 2607}, {3587, 2749}, {3253, 2704}, {3230, 2579}};
static const std::vector<std::pair<int, int>> kBayouLake_village_s = {{347, 2255}, {360, 2126}, {456, 2131}, {600, 2131}, {876, 2214}, {1044, 2235}, {1227, 2116}, {1471, 2098}, {1732, 2093}, {1945, 2088}, {2196, 2165}, {2440, 2243}, {2542, 2425}, {2573, 2715}, {2310, 2713}, {1986, 2721}, {1859, 2728}, {1562, 2712}, {1242, 2696}, {941, 2683}, {453, 2672}, {356, 2426}};
static const std::vector<const std::vector<std::pair<int, int>>*> kBayouLakes = {&kBayouLake_entrance, &kBayouLake_southeast, &kBayouLake_village_n, &kBayouLake_pond_b, &kBayouLake_pond_a, &kBayouLake_glade, &kBayouLake_pond_sw, &kBayouLake_village_s};
static const int kDeck_n_west[4] = {60, 4, 75, 15};   // cells: x0, y0, x1, y1 (exclusive)
static const int kDeck_n_east[4] = {81, 4, 99, 15};   // cells: x0, y0, x1, y1 (exclusive)
static const int kDeck_s_west[4] = {15, 70, 35, 79};   // cells: x0, y0, x1, y1 (exclusive)
static const int kDeck_s_east[4] = {45, 71, 59, 81};   // cells: x0, y0, x1, y1 (exclusive)
static const int kRampCols[2] = {64, 87};   // the north decks' ramps, by cell column
static const std::vector<std::pair<int, int>> kCampNorth = {{3850, 486}, {3871, 455}, {3902, 426}, {3943, 390}, {3990, 359}, {4027, 358}, {4068, 352}, {4104, 352}, {4153, 350}, {4186, 351}, {4220, 349}, {4269, 350}, {4317, 346}, {4367, 343}, {4406, 342}, {4446, 346}, {4490, 346}, {4533, 372}, {4566, 381}, {4613, 396}, {4643, 414}, {4675, 449}, {4664, 486}, {4636, 520}, {4605, 551}, {4596, 599}, {4566, 640}, {4536, 677}, {4487, 686}, {4434, 692}, {4392, 703}, {4350, 707}, {4116, 711}, {4072, 709}, {4029, 706}, {3988, 704}, {3955, 696}, {3923, 674}, {3894, 646}, {3865, 621}, {3837, 600}, {3810, 572}, {3809, 540}, {3825, 511}};
static const std::vector<std::pair<int, int>> kCampMid = {{4517, 1471}, {4546, 1409}, {4578, 1347}, {4622, 1286}, {4670, 1228}, {4732, 1164}, {4830, 1148}, {4926, 1151}, {5008, 1151}, {5078, 1148}, {5364, 1146}, {5428, 1147}, {5515, 1146}, {5590, 1148}, {5671, 1154}, {5734, 1151}, {5793, 1161}, {5840, 1176}, {5874, 1227}, {5898, 1275}, {5918, 1353}, {5928, 1424}, {5944, 1519}, {5947, 1624}, {5916, 1714}, {5860, 1749}, {5794, 1769}, {5746, 1784}, {5682, 1796}, {5605, 1797}, {5518, 1802}, {5432, 1812}, {5143, 1818}, {5097, 1817}, {5038, 1818}, {4977, 1815}, {4931, 1813}, {4876, 1807}, {4829, 1807}, {4764, 1798}, {4698, 1785}, {4634, 1775}, {4576, 1753}, {4531, 1742}, {4490, 1676}, {4492, 1596}, {4497, 1536}};
static const int kCampNorthMid[2] = {4227, 506}, kCampMidMid[2] = {5214, 1504};
static const std::vector<std::pair<int, int>> kTrackEntry = {{7169, 334}, {7138, 364}, {7103, 395}, {7065, 433}, {7031, 463}, {6992, 499}, {6954, 534}, {6907, 569}, {6864, 602}, {6813, 643}, {6752, 679}, {6721, 714}, {6659, 797}, {6541, 871}, {6474, 954}};
static const std::vector<std::pair<int, int>> kTrackLoop = {{6474, 954}, {6314, 910}, {6083, 883}, {5769, 888}, {5598, 921}, {5391, 934}, {5203, 881}, {5044, 858}, {4881, 841}, {4690, 851}, {4445, 870}, {4249, 948}, {4139, 991}, {3976, 1053}, {3886, 1113}, {3823, 1213}, {3786, 1332}, {3755, 1425}, {3734, 1541}, {3773, 1676}, {3822, 1803}, {3879, 1964}, {3968, 2072}, {4055, 2152}, {4170, 2235}, {4423, 2229}, {4671, 2226}, {4953, 2226}, {5299, 2230}, {5634, 2230}, {5896, 2228}, {6120, 2159}, {6196, 2060}, {6262, 1896}, {6235, 1740}, {6269, 1580}, {6260, 1386}, {6262, 1191}, {6230, 1084}, {6302, 1003}, {6474, 954}};
static const std::vector<std::pair<int, int>> kTrackWest = {{3886, 1113}, {3760, 1133}, {3540, 1132}, {3393, 1131}, {3318, 1130}, {3103, 1123}, {2901, 1122}, {2688, 1124}, {2498, 1123}, {2295, 1118}, {2097, 1113}, {1897, 1121}, {1675, 1126}, {1460, 1121}, {1258, 1123}, {1038, 1103}, {886, 1083}, {765, 1089}, {548, 1097}, {319, 1117}};
static const std::vector<std::pair<int, int>> kTrackSpur = {{886, 1083}, {887, 925}, {890, 810}, {876, 617}, {845, 476}, {820, 364}, {810, 269}, {811, 144}, {814, 54}};
static const std::vector<std::pair<int, int>> kTrackWay0 = {{4249, 948}, {4233, 854}, {4233, 714}};
static const std::vector<std::pair<int, int>> kTrackWay1 = {{5203, 881}, {5221, 1014}, {5221, 1147}};
static const std::vector<std::pair<int, int>> kTrackWay2 = {{5299, 2230}, {5288, 2014}, {5288, 1817}};
static const std::vector<std::pair<int, int>> kTrackWay3 = {{2037, 1118}, {2037, 954}, {2037, 724}};
static const std::vector<std::pair<int, int>> kTrackWay4 = {{2773, 1124}, {2773, 954}, {2773, 724}};
static const std::vector<std::pair<int, int>> kTrackWay5 = {{2295, 1118}, {2271, 1404}, {2181, 1774}, {1901, 1954}, {1671, 2044}};
static const std::vector<const std::vector<std::pair<int, int>>*> kBayouTracks = {&kTrackEntry, &kTrackLoop, &kTrackWest, &kTrackSpur, &kTrackWay0, &kTrackWay1, &kTrackWay2, &kTrackWay3, &kTrackWay4, &kTrackWay5};
static const std::vector<std::pair<int, int>> kBayouFlowers = {{592, 962}, {338, 892}, {280, 814}, {279, 677}, {324, 535}, {535, 400}, {735, 444}, {778, 971}, {484, 2036}, {745, 2026}, {1073, 2042}, {1420, 2026}, {1711, 2021}, {1916, 2022}, {1886, 1878}, {2170, 1987}, {2425, 2056}, {2634, 2224}, {2673, 2489}, {2742, 2722}, {3016, 2640}, {3044, 2416}, {3136, 2124}, {2821, 2021}, {2716, 1784}, {2774, 1723}, {2567, 1620}, {2374, 1570}, {2116, 1592}};
static const std::vector<std::pair<int, int>> kBayouDark = {{5267, 396}, {5407, 402}, {5652, 395}, {5656, 460}, {5340, 470}, {5541, 520}, {5405, 637}, {5795, 609}, {7088, 2260}, {7092, 2352}, {7096, 2503}, {7093, 2608}, {7087, 2740}, {6977, 2751}, {6739, 2768}, {6577, 2768}, {6410, 2768}, {6241, 2768}, {6055, 2757}, {5941, 2736}, {7042, 2192}, {7001, 2087}, {6897, 2043}, {6753, 2066}, {6617, 2067}, {6719, 1963}, {6788, 1911}, {6914, 1825}, {6965, 1961}, {6943, 1702}, {6899, 1600}, {6692, 1676}, {6851, 1711}, {6646, 1799}, {6542, 1702}, {6548, 1535}, {6711, 1378}, {6802, 1412}, {6755, 1312}, {6608, 1255}, {6497, 1333}, {6591, 1393}, {6592, 1132}, {6438, 1166}, {6561, 1090}, {6704, 951}, {6821, 934}, {6963, 984}, {7093, 835}, {7061, 716}, {6977, 766}, {6846, 788}, {7140, 573}, {7118, 1119}, {6935, 1219}, {7075, 1414}, {7065, 1627}};
struct BayouPost { const char* type; int x, y, level; bool lurk; const char* pool[3]; };
static const BayouPost kBayouPosts[] = {
    {"bog_lurker", 5987, 386, 1, true, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 5923, 398, 1, true, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 6442, 406, 2, true, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 6726, 359, 1, true, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 6221, 628, 1, false, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 6562, 622, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_gator", 6895, 2433, 1, true, {nullptr, nullptr, nullptr}},
    {"fen_gator", 6542, 2453, 1, true, {nullptr, nullptr, nullptr}},
    {"fen_gator", 6267, 2562, 1, true, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 6156, 2424, 1, false, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 6534, 2317, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_stalker", 6845, 2273, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 5848, 2536, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 5029, 1914, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 5504, 1951, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4524, 1900, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4378, 1633, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4408, 1475, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4482, 1271, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4583, 1155, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4336, 1364, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4265, 1610, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4314, 1888, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4762, 2009, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 5800, 1916, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 5990, 1823, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 6052, 1620, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 6040, 1372, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 6004, 1170, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 5750, 1038, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 5508, 1019, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4957, 1024, 1, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"rot_shambler", 4725, 1012, 2, false, {"rot_shambler", "bog_lurker", "rot_shambler"}},
    {"lizard_shaman", 4004, 431, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 3966, 486, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4001, 560, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 4377, 392, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4500, 432, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4513, 505, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 4409, 558, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4329, 566, 2, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 4128, 461, 3, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 4157, 528, 3, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 4310, 457, 3, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 4291, 535, 3, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 4214, 624, 3, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5105, 1186, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5346, 1188, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5144, 1700, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5413, 1702, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 5113, 1420, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5132, 1544, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 5645, 1285, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 5686, 1548, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4773, 1509, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 4855, 1326, 1, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 4862, 1644, 2, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 5790, 1404, 1, false, {nullptr, nullptr, nullptr}},
    {"lizard_shaman", 4734, 1408, 1, false, {nullptr, nullptr, nullptr}},
    {"drowned_one", 2757, 1444, 1, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 2857, 1341, 1, true, {nullptr, nullptr, nullptr}},
    {"fen_gator", 2989, 1447, 1, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 2836, 1525, 1, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 1322, 1393, 1, true, {nullptr, nullptr, nullptr}},
    {"fen_gator", 1322, 1542, 1, true, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 1669, 1571, 3, true, {nullptr, nullptr, nullptr}},
    {"witchlight", 3633, 2293, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 3650, 2332, 2, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 3708, 2347, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 3745, 2310, 2, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 3758, 2256, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 3722, 2227, 2, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 3673, 2239, 1, false, {nullptr, nullptr, nullptr}},
    {"rot_shambler", 3646, 2265, 2, false, {nullptr, nullptr, nullptr}},
    {"drowned_one", 1982, 542, 1, true, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 2694, 633, 3, true, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 1851, 790, 2, false, {nullptr, nullptr, nullptr}},
    {"swamp_hag", 2381, 807, 2, false, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 3266, 556, 2, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 1707, 433, 1, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 1692, 212, 1, false, {nullptr, nullptr, nullptr}},
    {"witchlight", 1660, 56, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_gator", 1989, 2570, 2, true, {nullptr, nullptr, nullptr}},
    {"fen_gator", 2382, 2595, 2, true, {nullptr, nullptr, nullptr}},
    {"mire_croaker", 2312, 2118, 2, false, {nullptr, nullptr, nullptr}},
    {"bog_lurker", 2560, 2307, 4, false, {nullptr, nullptr, nullptr}},
    {"drowned_one", 1790, 2654, 2, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 1359, 2654, 2, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 937, 2647, 2, true, {nullptr, nullptr, nullptr}},
    {"drowned_one", 513, 2640, 2, true, {nullptr, nullptr, nullptr}},
    {"fen_stalker", 6802, 1412, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_stalker", 6943, 1702, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_stalker", 6897, 2043, 1, false, {nullptr, nullptr, nullptr}},
    {"fen_stalker", 6963, 984, 1, false, {nullptr, nullptr, nullptr}},
};
// ---- end of the generated tables ----

static bool InsidePoly(const std::vector<std::pair<int, int>>& poly, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const float xi = static_cast<float>(poly[i].first), yi = static_cast<float>(poly[i].second);
        const float xj = static_cast<float>(poly[j].first), yj = static_cast<float>(poly[j].second);
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

static float DistToLine(const std::vector<std::pair<int, int>>& line, float x, float y) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        const float ax = static_cast<float>(line[i].first), ay = static_cast<float>(line[i].second);
        const float vx = line[i + 1].first - ax, vy = line[i + 1].second - ay, len2 = vx * vx + vy * vy;
        float t = len2 > 0.0f ? ((x - ax) * vx + (y - ay) * vy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        best = std::min(best, hypotf(ax + vx * t - x, ay + vy * t - y));
    }
    return best;
}

static void BuildBayou() {
    const int CELL = BY_CELL, W = BY_W, H = BY_H;
    MapBuilder m("bayou", "The Bayou", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("The deep swamp past the lizardmen's camp. Mind the water");
    m.Background(14, 22, 18);
    // A low mist, thickest over the open water and beside it.
    m.Fog(0.17f, 1.1f, {184, 198, 178});
    std::mt19937 rng(9191u);

    enum Kind : uint8_t { LAND = 0, WET = 1, DECK = 2, RAMP = 3 };
    vector<uint8_t> kind(static_cast<size_t>(W) * H, LAND);
    vector<int> level(static_cast<size_t>(W) * H, 0);
    vector<uint8_t> keep(static_cast<size_t>(W) * H, 0);     // nothing solid goes here
    const auto at = [&](int cx, int cy) { return static_cast<size_t>(cy) * W + cx; };
    const auto mid = [&](int c) { return static_cast<float>(c * CELL + CELL / 2); };
    const auto reserve = [&](int px, int py, int r) {
        const int cx = px / CELL, cy = py / CELL;
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x)
                if (x >= 0 && y >= 0 && x < W && y < H) keep[at(x, y)] = 1;
    };

    // --- the water: inside every ring the guide drew --------------------------------
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx)
            for (const auto* lake : kBayouLakes)
                if (InsidePoly(*lake, mid(cx), mid(cy))) { kind[at(cx, cy)] = WET; break; }

    // --- the decks, the bridge between the southern two, and the ramps -----------
    // Raised four levels on stilts. Everything round them is water, so the only
    // way on is the ramp; a deck's edge is a drop into the lake.
    const int* decks[4] = {kDeck_n_west, kDeck_n_east, kDeck_s_west, kDeck_s_east};
    for (const int* d : decks)
        for (int cy = d[1]; cy < d[3]; ++cy)
            for (int cx = d[0]; cx < d[2]; ++cx) { kind[at(cx, cy)] = DECK; level[at(cx, cy)] = 4; }
    // The bridge: the south village's far deck is reached across the near one.
    const int bridge_y0 = (std::max(kDeck_s_west[1], kDeck_s_east[1]) + std::min(kDeck_s_west[3], kDeck_s_east[3])) / 2 - 1;
    const int bridge_y1 = bridge_y0 + 2;
    for (int cy = bridge_y0; cy < bridge_y1; ++cy)
        for (int cx = kDeck_s_west[2]; cx < kDeck_s_east[0]; ++cx) { kind[at(cx, cy)] = DECK; level[at(cx, cy)] = 4; }

    vector<Rect4> ramps;
    vector<std::pair<int, int>> ramp_feet;      // where each ramp comes down, in px
    // A ramp two cells wide, from a deck's edge out over the water to the first
    // dry ground and a row past it, stepping down three levels to one. The
    // guide drew its posts rising five times toward the deck; five steps of
    // one level would lift the deck seventy pixels, which is a tower, not a
    // hut on stilts.
    const auto ramp = [&](int c0, int from_row, int dir) {
        vector<int> rows;
        for (int k = 0, cy = from_row; k < 10 && cy >= 1 && cy < H - 1; ++k, cy += dir) {
            rows.push_back(cy);
            const bool dry = kind[at(c0, cy)] != WET && kind[at(c0 + 1, cy)] != WET;
            if (dry && k >= 3) break;
        }
        const int n = static_cast<int>(rows.size());
        for (int k = 0; k < n; ++k) {
            const int lv = std::max(1, 3 - (3 * k) / n);
            for (int cx = c0; cx <= c0 + 1; ++cx) { kind[at(cx, rows[k])] = RAMP; level[at(cx, rows[k])] = lv; }
        }
        const int lo = std::min(rows.front(), rows.back()) - 1, hi = std::max(rows.front(), rows.back()) + 1;
        ramps.push_back({static_cast<float>(c0 * CELL), static_cast<float>(lo * CELL),
                         2.0f * CELL, static_cast<float>((hi - lo + 1) * CELL)});
        ramp_feet.push_back({c0 * CELL + CELL, (rows.back() + dir) * CELL + CELL / 2});
    };
    ramp(kRampCols[0] - 1, kDeck_n_west[3], +1);
    ramp(kRampCols[1] - 1, kDeck_n_east[3], +1);
    ramp((kDeck_s_east[0] + kDeck_s_east[2]) / 2 - 1, kDeck_s_east[1] - 1, -1);
    // Decking is a drop at its edges, and a face of planks rather than a bank of
    // earth: see Map::cliff_texture.
    m.Elevation(CELL, W, H, level, ramps, "assets/tiles/plank_floor_dark.png", {118, 92, 58});

    // --- what the ground is ------------------------------------------------------------
    const auto on_track = [&](float x, float y, float r) {
        for (const auto* t : kBayouTracks) if (DistToLine(*t, x, y) < r) return true;
        return false;
    };
    const auto in_camp = [&](float x, float y) { return InsidePoly(kCampNorth, x, y) || InsidePoly(kCampMid, x, y); };
    const auto dark_here = [&](float x, float y) {
        const float wob = 22.0f * (Fbm(x * 0.01f, y * 0.01f, 3131) - 0.5f);
        for (const auto& p : kBayouDark)
            if (hypotf(x - p.first, y - p.second) < 76.0f + wob) return true;
        return false;
    };
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float x = mid(cx), y = mid(cy);
            // Broad and quiet: a bayou is sedge from one bank to the next, with
            // mud where the water has been and peat in the low places. Cell
            // noise at a fine grain laid mud and grass out as a checkerboard.
            const float v = Fbm(cx * 0.07f, cy * 0.07f, 9292);
            bool damp = false;
            for (int y2 = cy - 2; y2 <= cy + 2 && !damp; ++y2)
                for (int x2 = cx - 2; x2 <= cx + 2 && !damp; ++x2)
                    damp = x2 >= 0 && y2 >= 0 && x2 < W && y2 < H && kind[at(x2, y2)] == WET;
            string tile;
            switch (kind[at(cx, cy)]) {
                case WET:  tile = VariantOf("bog_water", cx, cy); break;
                case DECK: tile = VariantOf("plank_floor", cx, cy); break;
                case RAMP: tile = VariantOf("plank_floor", cx, cy); break;
                default:
                    if (on_track(x, y, 40.0f))  tile = VariantOf(v > 0.55f ? "dirt_dark" : "dirt", cx, cy);
                    else if (in_camp(x, y))     tile = VariantOf("dirt_dark", cx, cy);
                    // The guide's dark grass: the thicket down the east side and
                    // the undergrowth north of the loop. `grass_dark` is jade,
                    // whatever it is called; this is the dull dark olive of
                    // ground nothing has walked on.
                    // (marsh_dark was flecked through it and read, at the
                    // game's zoom, as a scatter of black pits.)
                    else if (dark_here(x, y))   tile = VariantOf("marsh_ground", cx, cy);
                    else if (damp)              tile = VariantOf(Hash2(cx, cy, 9393) < 0.7f ? "swamp_mud" : "swamp_grass", cx, cy);
                    else tile = VariantOf(v > 0.68f ? "peat" : "swamp_grass", cx, cy);
            }
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (kind[at(cx, cy)] == WET) m.Water(cx * CELL, cy * CELL, CELL, CELL);
            if (kind[at(cx, cy)] != LAND || on_track(x, y, 56.0f)) keep[at(cx, cy)] = 1;
        }

    // --- the edge, open on the east where the track comes in -----------------------------
    const int gate_row = kTrackEntry.front().second / CELL;
    // Open on the north too, where the west spur runs off the top of the map:
    // the way into the Hexmire.
    const int hex_col = kTrackSpur.back().first / CELL;
    for (int cx = 0; cx < W; ++cx) {
        if (abs(cx - hex_col) > 1) m.Collision(cx * CELL, 0, CELL, CELL);
        m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        m.Collision(0, cy * CELL, CELL, CELL);
        if (abs(cy - gate_row) > 2) m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }
    m.Portal(W * CELL - 24, gate_row * CELL + 16 - 72, 24, 144, "overworld", "from_bayou", "To the Hollowmarch", false);
    m.Spawn("from_hollowmarch", W * CELL - 104, gate_row * CELL + 16);
    m.Spawn("default",          W * CELL - 104, gate_row * CELL + 16);
    reserve(W * CELL - 104, gate_row * CELL + 16, 4);

    // --- the Hexmire's gateway, over the spur where it leaves ------------------------------
    // The cult's: two black posts and a beam hung with everything it hangs up,
    // so nobody walks north out of the Bayou thinking it is more of the same.
    {
        // Ten rows down: it is six cells tall, and the spur's chest is at its end.
        const int hx = kTrackSpur.back().first, gy = 10 * CELL;
        m.Portal(hx - 64, 0, 128, 24, "hex_drowns", "from_bayou", "North into the Hexmire", false);
        m.Danger(55);
        m.Spawn("from_hexmire", hx, 3 * CELL + 16);
        m.Prop("props", "hex_gateway", hx, gy);
        m.Collision(hx - 88, gy - 16, 28, 16);
        m.Collision(hx + 60, gy - 16, 28, 16);
        reserve(hx, gy, 4);
        json& o = m.Object("sign_bayou_hexmire", "sign", hx + 112, gy + 56);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A post by the gateway";
        o["text"]   = "NORTH: THE HEXMIRE\n\nThe cult that hung this up keeps the swamp past it, and the "
                      "Shellbacks keep the shore. Neither is friendly, and neither is anything the Bayou has.\n\n"
                      "Cut into the post: FIFTY-FIVE, AND UP.";
        m.Collision(hx + 96, gy + 46, 32, 10);
        reserve(hx + 112, gy + 56, 1);
    }

    // --- who lives here, first: everything else keeps clear of them -----------------------
    for (const BayouPost& p : kBayouPosts) reserve(p.x, p.y, 1);

    // --- the camps: stakes round them, with a gate wherever the guide left a gap ----------
    // Laid along the ring the guide drew. A horizontal run is the palisade
    // seen face-on; a run down the side is its stakes seen one behind the
    // other. The collision is laid separately and continuously, in short
    // pieces along the line, so no angle of wall has a gap to slip through.
    const auto stake_ring = [&](const vector<std::pair<int, int>>& ring) {
        const size_t n = ring.size();
        float since = 1e9f;
        for (size_t i = 0; i < n; ++i) {
            const auto a = ring[i], b = ring[(i + 1) % n];
            const float dx = static_cast<float>(b.first - a.first), dy = static_cast<float>(b.second - a.second);
            const float len = hypotf(dx, dy);
            if (len > 150.0f) {
                // A gate: a painted totem either side of the way in.
                for (const auto& g : {a, b}) {
                    m.Prop("props", "lizard_totem", g.first, g.second + 8);
                    m.Collision(g.first - 8, g.second, 16, 8);
                }
                since = 1e9f;
                continue;
            }
            const bool across = fabsf(dx) >= fabsf(dy);
            const float step = across ? 50.0f : 22.0f;
            for (float t = 0.0f; t < len; t += 4.0f) {
                const int x = a.first + static_cast<int>(dx * t / len), y = a.second + static_cast<int>(dy * t / len);
                if (static_cast<int>(t) % 12 == 0) m.Collision(x - 10, y - 14, 20, 16);
                since += 4.0f;
                if (since < step) continue;
                since = 0.0f;
                m.Prop("props", across ? "palisade" : "palisade_side", x, y);
            }
        }
    };
    stake_ring(kCampNorth);
    stake_ring(kCampMid);

    // What stands inside: huts, a fire, a totem, a chest -- each wherever it
    // can go without being on top of somebody or up against the stakes.
    const auto clear_of_posts = [&](int x, int y, float r) {
        for (const BayouPost& p : kBayouPosts) if (hypotf(static_cast<float>(p.x - x), static_cast<float>(p.y - y)) < r) return false;
        return true;
    };
    const auto furnish = [&](const vector<std::pair<int, int>>& ring, const int c[2], int huts, const string& chest_id) {
        const auto inside = [&](int x, int y, float margin) {
            if (!InsidePoly(ring, static_cast<float>(x), static_cast<float>(y))) return false;
            for (const auto& p : ring) if (hypotf(static_cast<float>(p.first - x), static_cast<float>(p.second - y)) < margin) return false;
            return true;
        };
        // The fire at the middle.
        json& fire = m.Object("range_" + chest_id, "range", c[0], c[1] + 6);
        fire["sprite"] = "assets/props/campfire_ring.png";
        fire["title"]  = "Camp fire";
        m.Collision(c[0] - 16, c[1] - 4, 32, 10);
        reserve(c[0], c[1], 1);
        int placed = 0;
        for (int ring_r = 170; ring_r <= 520 && placed < huts; ring_r += 70)
            for (int k = 0; k < 12 && placed < huts; ++k) {
                const float a = 6.2831853f * k / 12.0f + ring_r * 0.01f;
                const int x = c[0] + static_cast<int>(cosf(a) * ring_r), y = c[1] + static_cast<int>(sinf(a) * ring_r * 0.55f);
                if (!inside(x, y - 20, 90.0f) || !clear_of_posts(x, y - 20, 96.0f)) continue;
                bool apart = true;
                for (int ox = -3; ox <= 3 && apart; ++ox)
                    for (int oy = -2; oy <= 1 && apart; ++oy) {
                        const int px = x / CELL + ox, py = y / CELL + oy;
                        if (px >= 0 && py >= 0 && px < W && py < H && keep[at(px, py)]) apart = false;
                    }
                if (!apart) continue;
                m.Prop("props", "lizard_hut", x, y);
                m.Collision(x - 42, y - 26, 84, 26);
                reserve(x, y - 20, 2);
                ++placed;
            }
        // A chest where the shamans keep what they took.
        for (int k = 0; k < 24; ++k) {
            const float a = 6.2831853f * k / 24.0f;
            const int x = c[0] + static_cast<int>(cosf(a) * 110.0f), y = c[1] + static_cast<int>(sinf(a) * 70.0f);
            if (!inside(x, y, 70.0f) || !clear_of_posts(x, y, 60.0f) || keep[at(x / CELL, y / CELL)]) continue;
            PlaceChest(m, chest_id, x, y, "chest_bayou");
            reserve(x, y, 1);
            break;
        }
    };
    furnish(kCampNorth, kCampNorthMid, 2, "chest_bayou_north_camp");
    furnish(kCampMid, kCampMidMid, 4, "chest_bayou_great_camp");
    // Nothing grows inside the stakes.
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx)
            if (in_camp(mid(cx), mid(cy))) keep[at(cx, cy)] = 1;

    // --- the stilt villages -------------------------------------------------------------
    // Huts along the back of each deck, pilings under its front edge (the only
    // edge of a deck on stilts anybody sees from here: the ones behind are under
    // the boards), and whoever lives on it.
    const auto deck_px = [&](const int* d) {
        return std::array<int, 4>{d[0] * CELL, d[1] * CELL, d[2] * CELL, d[3] * CELL};
    };
    const auto pilings = [&](int x0, int x1, int y, int skip_x0 = -1, int skip_x1 = -1) {
        for (int x = x0 + 12; x <= x1 - 12; x += 4 * CELL) {
            if (x >= skip_x0 && x <= skip_x1) continue;
            m.Prop("props", "bayou_piling", x, y);
        }
        m.Prop("props", "bayou_piling", x1 - 12, y);
    };
    const auto huts_on = [&](const int* d, int count) {
        const auto r = deck_px(d);
        const int span = r[2] - r[0];
        for (int i = 0; i < count; ++i) {
            const int x = r[0] + span * (2 * i + 1) / (2 * count), y = r[1] + 4 * CELL + 8;
            m.Prop("props", "bayou_hut", x, y);
            m.Collision(x - 40, y - 24, 80, 24);
            // What a stilt village keeps outside its doors: a barrel of
            // something, crates and sacks off a boat. Beside each hut, never
            // in front of it, so the boards stay a place to walk and fight.
            const int side = (i % 2 == 0) ? -1 : 1;
            m.Prop("props", i % 3 == 1 ? "crates_sacks" : "barrel", x + side * 62, y + 14);
            m.Collision(x + side * 62 - 14, y + 4, 28, 12);
        }
    };
    {
        const auto nw = deck_px(kDeck_n_west), ne = deck_px(kDeck_n_east);
        const int rw = (kRampCols[0] - 1) * CELL, re = (kRampCols[1] - 1) * CELL;
        huts_on(kDeck_n_west, 2);
        huts_on(kDeck_n_east, 3);
        pilings(nw[0], nw[2], nw[3] + 8, rw - 8, rw + 2 * CELL + 8);
        pilings(ne[0], ne[2], ne[3] + 8, re - 8, re + 2 * CELL + 8);
        // Shamans on the boards, where they can see who comes up the ramp.
        m.Enemy("lizard_shaman", (nw[0] + nw[2]) / 2 - 90, nw[3] - 2 * CELL, 1, 60.0f, 200.0f);
        m.Enemy("lizard_shaman", (nw[0] + nw[2]) / 2 + 110, nw[3] - 3 * CELL, 1, 60.0f, 200.0f);
        m.Enemy("lizard_shaman", (ne[0] + ne[2]) / 2 - 140, ne[3] - 2 * CELL, 2, 60.0f, 200.0f);
        m.Enemy("swamp_hag",     (ne[0] + ne[2]) / 2 + 150, ne[3] - 3 * CELL, 3, 60.0f, 200.0f);
        PlaceChest(m, "chest_bayou_north_village", ne[2] - 2 * CELL, ne[1] + 5 * CELL + 20, "chest_bayou");
    }
    {
        const auto sw = deck_px(kDeck_s_west), se = deck_px(kDeck_s_east);
        huts_on(kDeck_s_east, 2);
        // The Mother of the Fen's own: the great hut, in the middle of the back
        // of the far deck, and her hoard beside it.
        const int gx = (sw[0] + sw[2]) / 2, gy = sw[1] + 4 * CELL + 16;
        m.Prop("props", "bayou_hut_great", gx, gy);
        m.Collision(gx - 58, gy - 30, 116, 30);
        pilings(sw[0], sw[2], sw[3] + 8);
        pilings(se[0], se[2], se[3] + 8);
        pilings(sw[2], se[0], bridge_y1 * CELL + 8);
        m.Enemy("lizard_shaman", (se[0] + se[2]) / 2 - 110, se[3] - 2 * CELL, 2, 60.0f, 200.0f);
        m.Enemy("lizard_shaman", (se[0] + se[2]) / 2 + 110, se[3] - 2 * CELL, 2, 60.0f, 200.0f);
        PlaceChest(m, "chest_bayou_south_village", se[2] - 2 * CELL, se[1] + 5 * CELL + 16, "chest_bayou");
        // She waits in the middle of her deck; nobody comes at her but across
        // the bridge.
        m.Enemy("bayou_matriarch", gx, sw[3] - 3 * CELL, 1, 600.0f, 280.0f);
        PlaceChest(m, "chest_fen_mother", gx + 130, gy + 30, "chest_fen_mother");
    }

    // --- the water's edges: reeds on the bank, lilies and drowned trees in it ----------------
    const auto wet = [&](int cx, int cy) {
        return cx >= 0 && cy >= 0 && cx < W && cy < H && kind[at(cx, cy)] == WET;
    };
    // Two rows further to the south than to the north: a deck is lifted four
    // levels, nearly two cells, so it lies over the water up to two rows north
    // of where it stands. Anything put there draws over its planks.
    const auto near_deck = [&](int cx, int cy, int r) {
        for (int y = cy - r; y <= cy + r + 2; ++y)
            for (int x = cx - r; x <= cx + r; ++x)
                if (x >= 0 && y >= 0 && x < W && y < H && (kind[at(x, y)] == DECK || kind[at(x, y)] == RAMP)) return true;
        return false;
    };
    std::map<string, vector<string>> pools;
    const auto pool = [&](const string& prefix) -> const vector<string>& {
        auto it = pools.find(prefix);
        if (it != pools.end()) return it->second;
        vector<string> names;
        for (int i = 0; i < 16 && g_manifest.Has("decor/" + prefix + "_" + std::to_string(i)); ++i)
            names.push_back(prefix + "_" + std::to_string(i));
        return pools[prefix] = names;
    };
    int trees = 0;
    for (int cy = 2; cy < H - 2; ++cy)
        for (int cx = 2; cx < W - 2; ++cx) {
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            const float r = Hash2(cx, cy, 7171);
            const bool shore = !wet(cx, cy) && (wet(cx + 1, cy) || wet(cx - 1, cy) || wet(cx, cy + 1) || wet(cx, cy - 1));
            if (wet(cx, cy)) {
                if (near_deck(cx, cy, 1)) continue;
                const bool edge = !wet(cx + 1, cy) || !wet(cx - 1, cy) || !wet(cx, cy + 1) || !wet(cx, cy - 1);
                if (r < 0.07f) m.Prop("props", "lily_pads", x, y + 8);
                // A cypress standing in the shallows, the way a bayou's do.
                else if (edge && r < 0.13f && !near_deck(cx, cy, 3)) { m.Prop("props", "swamp_tree", x, y); ++trees; }
                continue;
            }
            if (keep[at(cx, cy)]) continue;
            if (shore) {
                if (r < 0.34f) m.Prop("props", "reeds", x, y + 4);
                continue;
            }
            const bool dark = dark_here(static_cast<float>(x), static_cast<float>(y));
            if (r < (dark ? 0.075f : 0.026f)) {
                m.Prop("props", "swamp_tree", x, y);
                m.Collision(x - 8, y - 8, 16, 8);
                ++trees;
            } else if (r < (dark ? 0.13f : 0.045f)) {
                m.Prop("objects", dark ? Pick(kBushes, rng) : Pick(kFungus, rng), x, y);
            } else if (r < 0.16f) {
                const vector<string>& names = pool(dark ? "tuft_dark" : (Hash2(cx, cy, 7272) < 0.7f ? "sedge" : "puddle"));
                if (!names.empty())
                    m.Overlay("decor", names[static_cast<size_t>(Hash2(cx, cy, 7373) * 1000.0f) % names.size()],
                              x + static_cast<int>(Hash2(cx, cy, 7474) * 14.0f) - 7,
                              y + static_cast<int>(Hash2(cx, cy, 7575) * 14.0f) - 7);
            }
        }

    // --- herbs, where the guide drew flowers ------------------------------------------------
    // Bogbean at the water's edge, glowcap where it is shaded and dry.
    int herb_i = 0;
    for (const auto& f : kBayouFlowers) {
        const int cx = f.first / CELL, cy = f.second / CELL;
        bool by_water = false;
        for (int y = cy - 3; y <= cy + 3 && !by_water; ++y)
            for (int x = cx - 3; x <= cx + 3 && !by_water; ++x) by_water = wet(x, y);
        if (wet(cx, cy)) continue;
        PlaceHerb(m, by_water ? "bogbean" : "glowcap", f.first, f.second, herb_i);
    }

    // --- where the tracks give out ------------------------------------------------------------
    // The north spur and the west road both run off into the reeds. Something
    // was left at the end of each by whoever walked them last.
    {
        const auto s = kTrackSpur.back();
        PlaceChest(m, "chest_bayou_spur", s.first + 28, s.second + 40, "chest_bayou");
        const auto w = kTrackWest.back();
        PlaceChest(m, "chest_bayou_west", w.first + 20, w.second - 40, "chest_bayou");
    }

    // --- the posts ------------------------------------------------------------------------------
    // Whatever lurks, lurks at the edge. It is woken by somebody within
    // Enemy::LURK_WAKE, and nobody can walk on water: one the guide drew out in
    // the middle of a lake would wait there for ever. So each is moved to the
    // nearest water a cell or two out from something walkable -- a bank, a
    // ramp, a deck -- which is where it was meant to be waiting anyway.
    const auto shore_px = [&](int cx, int cy) {
        float best = 1e9f;
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x)
                if (x >= 0 && y >= 0 && x < W && y < H && kind[at(x, y)] != WET)
                    best = std::min(best, hypotf(static_cast<float>((x - cx) * CELL), static_cast<float>((y - cy) * CELL)));
        return best;
    };
    const auto at_the_edge = [&](int px, int py) {
        const int ox = px / CELL, oy = py / CELL;
        std::pair<int, int> best{px, py};
        float best_d = 1e9f;
        for (int y = oy - 8; y <= oy + 8; ++y)
            for (int x = ox - 8; x <= ox + 8; ++x) {
                if (!wet(x, y) || !wet(x + 1, y) || !wet(x - 1, y) || !wet(x, y + 1) || !wet(x, y - 1)) continue;
                if (shore_px(x, y) > 72.0f) continue;
                // Not under the lifted edge of a deck, where it would come up
                // drawn over the boards.
                bool under = false;
                for (int k = 1; k <= 2 && !under; ++k)
                    under = y + k < H && (kind[at(x, y + k)] == DECK || kind[at(x, y + k)] == RAMP);
                if (under) continue;
                const float d = hypotf(static_cast<float>((x - ox) * CELL), static_cast<float>((y - oy) * CELL));
                if (d < best_d) { best_d = d; best = {x * CELL + 16, y * CELL + 16}; }
            }
        return best;
    };
    for (const BayouPost& p : kBayouPosts) {
        if (p.lurk) {
            const auto e = at_the_edge(p.x, p.y);
            m.LurkingEnemy(p.type, e.first, e.second, p.level);
        }
        else if (p.pool[0]) m.EnemyPool({p.pool[0], p.pool[1], p.pool[2]}, "", p.x, p.y, p.level, 0);
        else m.Enemy(p.type, p.x, p.y, p.level, 32.0f, 240.0f);
    }

    // --- dragonflies, round every body of water -----------------------------------------------
    // Three to a pond and four round the big water, on the bank a cell or two
    // back from the edge -- over the reeds, where a dragonfly rests -- and well
    // clear of whatever waits under it: somebody reaching for a dragonfly
    // should not be how they find the gator (Enemy::LURK_WAKE is 104 px; the
    // bug is kept 150 px off, and caught from within 58 of it). A pond whose
    // banks are that crowded still gets two, a little closer together.
    int bug_i = 0;
    {
        vector<std::pair<int, int>> lurkers, taken;
        for (const BayouPost& p : kBayouPosts)
            if (p.lurk) lurkers.push_back(at_the_edge(p.x, p.y));
        for (size_t k = 0; k < kBayouLakes.size(); ++k) {
            const auto& lake = *kBayouLakes[k];
            const auto this_water = [&](int cx, int cy) { return wet(cx, cy) && InsidePoly(lake, mid(cx), mid(cy)); };
            const auto bank = [&](int cx, int cy, float keep_off) {
                if (cx < 1 || cy < 1 || cx >= W - 1 || cy >= H - 1) return false;
                if (kind[at(cx, cy)] != LAND || keep[at(cx, cy)] || near_deck(cx, cy, 1)) return false;
                if (in_camp(mid(cx), mid(cy))) return false;
                int d = 99;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (this_water(cx + dx, cy + dy)) d = std::min(d, std::max(abs(dx), abs(dy)));
                if (d > 2) return false;
                for (const auto& l : lurkers)
                    if (hypotf(static_cast<float>(l.first) - mid(cx), static_cast<float>(l.second) - mid(cy)) < keep_off) return false;
                return true;
            };
            int shore = 0;
            for (int cy = 1; cy < H - 1; ++cy)
                for (int cx = 1; cx < W - 1; ++cx)
                    if (!wet(cx, cy) && (this_water(cx + 1, cy) || this_water(cx - 1, cy) || this_water(cx, cy + 1) || this_water(cx, cy - 1)))
                        ++shore;
            const int want = shore > 110 ? 4 : 3;
            const uint32_t salt = 7101u + static_cast<uint32_t>(k) * 17u;
            int got = PlaceBugs(m, "marsh_dragonfly", want, CELL, 1, 1, W - 1, H - 1, salt, 6.0f,
                                [&](int cx, int cy) { return bank(cx, cy, 150.0f); }, bug_i, &taken);
            if (got < 2)
                got += PlaceBugs(m, "marsh_dragonfly", 2 - got, CELL, 1, 1, W - 1, H - 1, salt + 5u, 3.0f,
                                 [&](int cx, int cy) { return bank(cx, cy, 124.0f); }, bug_i, &taken);
            std::printf("  the Bayou: %d dragonflies round lake %zu (%d cells of bank)\n", got, k, shore);
        }
    }
    std::printf("  the Bayou: %d trees, %zu ramps, %d herbs\n", trees, ramps.size(), herb_i);
    PlaceCurios(m);
    PlaceRoamers(m);
    PlaceNightVisitors(m, {{{"blood_thrall", "grave_hound"}, 1, 0}, {{"nosferatu"}, 1, 0},
                           {{"tomb_shade", "grave_hound"}, 1, 0}, {{"dragon_water"}, 1, 0}}, 9,
                       [](int, int) { return true; });
    m.Write("maps");
}

static void BuildMossvale() {
    const int CELL = 32, W = 58, H = 46;
    MapBuilder m("mossvale", "Mossvale", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("A logging village under the Whisperwood");
    m.Background(30, 44, 30);
    std::mt19937 rng(5858u);

    const int gate_row = 26;              // the street comes in from the west gate
    const int sq_cx = 30, sq_cy = 24;     // the square

    auto on_street = [&](int cx, int cy) { return cx <= sq_cx && abs(cy - gate_row) <= 1; };
    auto in_square = [&](int cx, int cy) {
        const float dx = (cx - sq_cx) / 8.5f, dy = (cy - sq_cy) / 5.5f;
        return dx * dx + dy * dy < 1.0f;
    };
    auto on_lane = [&](int cx, int cy) {
        if (abs(cx - sq_cx) <= 1 && cy >= 16 && cy <= sq_cy) return true;        // up to the lodge
        if (abs(cx - 20) <= 1 && cy >= 13 && cy <= 22) return true;              // up to Wynn's door
        if (abs(cy - 22) <= 1 && cx >= 20 && cx <= 23) return true;              // and into the square
        if (abs(cy - 38) <= 1 && cx >= 11 && cx <= 26) return true;             // to the herbalist
        if (abs(cx - 26) <= 1 && cy >= sq_cy && cy <= 38) return true;
        return false;
    };
    // Where buildings stand, so the greenery keeps clear of them.
    auto reserved = [&](int cx, int cy) {
        if (cx >= 25 && cx <= 35 && cy >= 10 && cy <= 17) return true;   // lodge
        if (cx >= 8 && cx <= 16 && cy >= 30 && cy <= 38) return true;    // herbalist
        if (cx >= 42 && cx <= 50 && cy >= 33 && cy <= 39) return true;   // the tanner's
        if (cx >= 15 && cx <= 25 && cy >= 6 && cy <= 13) return true;    // Wynn's
        return false;
    };

    // --- ground ---------------------------------------------------------------
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.22f, cy * 0.22f, 58);
            string tile;
            if (on_street(cx, cy) || on_lane(cx, cy) || in_square(cx, cy))
                tile = VariantOf(v > 0.58f ? "dirt_dark" : "dirt", cx, cy);
            else
                tile = VariantOf(v > 0.62f ? "moss" : (v > 0.3f ? "grass" : "grass_dark"), cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
        }

    // --- the boundary: palisade north and south, forest east and west ---------
    for (int cx = 0; cx < W; ++cx) {
        m.Collision(cx * CELL, 0, CELL, 2 * CELL);
        m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int x = 28; x < W * CELL; x += 54) {
        m.Prop("props", "palisade", x, 2 * CELL + 8);
        m.Prop("props", "palisade", x, H * CELL - 2);
    }
    for (int cy = 0; cy < H; ++cy) {
        if (abs(cy - gate_row) > 1) m.Collision(0, cy * CELL, CELL, CELL);
        m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    // --- buildings ------------------------------------------------------------
    // Both are Mossvale's own buildings, modelled for it: a log lodge, and a
    // thatched cottage for the herbalist. The lodge art is 192 square but drawn across 142 by
    // 132 of it, which is what its collision is cut to.
    PlaceBuilding(m, "mossvale_lodge", sq_cx * CELL + 16, 16 * CELL, 142, 132,
                  "mossvale_lodge_hall", "entrance", "Enter the lodge",
                  "from_mossvale_lodge", "props");
    PlaceBuilding(m, "herbalist_cottage", 12 * CELL + 16, 37 * CELL, 136, 125,
                  "mossvale_herbalist", "entrance", "Enter Oona's cottage",
                  "from_mossvale_herbalist", "props");
    // The tanner's old house, empty since he left. The door is locked and the
    // key is under a stone at the gable end, which is the sort of thing a
    // village innkeeper knows. Once it is open it is the player's: the only
    // place in the world with somewhere to put things down.
    {
        const int tx = 46 * CELL, ty = 38 * CELL;
        PlaceBuilding(m, "building_house_a", tx, ty, 128, 140,
                      "mossvale_cottage", "entrance", "Try the door",
                      "from_mossvale_cottage", "objects");
        m.Lock("mossvale_house_key");

        // The stone the key is under, at the side of the house away from the
        // door, so it is found by walking round rather than by walking up.
        json& o = m.Object("rock_mossvale_key", "search", tx - 84, ty - 18);
        o["sprite"] = ObjPath("rocksmall_00");
        o["title"]  = "Look under the loose stone";
        o["item"]   = "mossvale_house_key";
        o["text"]   = "A key, wrapped in oilcloth.";
        m.Collision(tx - 84 - 14, ty - 18 - 10, 28, 10);

        json& n = m.Object("sign_mossvale_house", "sign", tx + 74, ty - 10);
        n["sprite"] = "assets/props/signpost.png";
        n["title"]  = "A nailed board";
        n["text"]   = "TANNER'S HOUSE\nGone to the coast. Do not wait.\n\n"
                      "Underneath, in a different hand: the key is where it always was.";
        m.Collision(tx + 74 - 16, ty - 10 - 10, 32, 10);
    }

    // --- the square -------------------------------------------------------------
    {
        const int wx = (sq_cx + 4) * CELL, wy = (sq_cy + 1) * CELL;
        m.Prop("props", "well", wx, wy);
        m.Collision(wx - 18, wy - 12, 36, 12);
    }
    m.Prop("props", "market_stall", 21 * CELL, 30 * CELL);
    m.Collision(21 * CELL - 32, 30 * CELL - 14, 64, 14);
    m.Prop("props", "market_stall", 38 * CELL, 30 * CELL);
    m.Collision(38 * CELL - 32, 30 * CELL - 14, 64, 14);
    m.Prop("props", "crates_sacks", 41 * CELL + 16, 30 * CELL);
    m.Collision(41 * CELL, 30 * CELL - 10, 36, 10);
    m.Prop("props", "barrel", 18 * CELL + 16, 30 * CELL);
    m.Collision(18 * CELL + 2, 30 * CELL - 10, 28, 10);

    {
        json& o = m.Object("board_mossvale", "board", (sq_cx - 6) * CELL, 20 * CELL);
        o["sprite"] = ObjPath("guild_noticeboard");
        o["title"]  = "Mossvale Notices";
        o["quests"] = json::array({"q_mossvale_hides", "q_trail_wardens"});
        m.Collision((sq_cx - 6) * CELL - 36, 20 * CELL - 12, 72, 12);
    }
    {
        json& o = m.Object("range_mossvale", "range", (sq_cx + 10) * CELL, 21 * CELL);
        o["sprite"] = "assets/props/campfire_ring.png";
        o["title"]  = "Cooking fire";
        m.Collision((sq_cx + 10) * CELL - 16, 21 * CELL - 10, 32, 10);
    }
    {
        json& o = m.Object("bench_mossvale", "workbench", 44 * CELL, 26 * CELL);
        o["sprite"]  = "assets/props/workbench.png";
        o["title"]   = "Workbench";
        o["station"] = "workbench";
        m.Collision(44 * CELL - 34, 26 * CELL - 18, 67, 18);
        // An anvil beside it, so the trail's metal need not go back to
        // Havenbrook to be smithed.
        json& a = m.Object("anvil_mossvale", "workbench", 47 * CELL, 26 * CELL);
        a["sprite"]  = "assets/props/anvil.png";
        a["title"]   = "Anvil";
        a["station"] = "anvil";
        m.Collision(47 * CELL - 14, 26 * CELL - 12, 28, 12);
    }
    for (const auto& lp : {std::pair<int, int>{35 * CELL, 17 * CELL}, {24 * CELL, 17 * CELL}}) {
        m.Prop("props", "log_pile", lp.first, lp.second);
        m.Collision(lp.first - 16, lp.second - 10, 32, 10);
    }
    {
        const int gx = 3 * CELL, gy = (gate_row - 3) * CELL;
        json& o = m.Object("sign_mossvale_gate", "sign", gx, gy);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Mossvale";
        o["text"]   = "MOSSVALE\nTimber, hides and a warm fire.\n\n"
                      "Reeve's lodge north of the square. Notices by the well.";
        m.Collision(gx - 16, gy - 10, 32, 10);
    }

    // Oona's herb garden, beside her cottage: a row of each of the three
    // plants a beginner brews with.
    {
        int herb_i = 0;
        const char* rows_of[] = {"marigold", "brookmint", "nettle"};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                PlaceHerb(m, rows_of[row], (17 + col) * CELL + 16, (33 + row) * CELL + 12, herb_i);
    }

    // The west gate, where the trail comes in, and the palisade down both
    // sides to meet the one along the top and bottom.
    PlaceSideGate(m, CELL + 8, (gate_row - 1) * CELL, (gate_row + 2) * CELL);
    PalisadeSide(m, 14, 3 * CELL, (H - 1) * CELL, (gate_row - 1) * CELL, (gate_row + 2) * CELL);
    PalisadeSide(m, W * CELL - 14, 3 * CELL, (H - 1) * CELL);

    // --- people -------------------------------------------------------------------
    // Sela keeps the gate, from the foot of its south tower.
    m.Npc("npc_sela",   "Warden Sela",    "player_wayfarer", 2 * CELL + 26, (gate_row + 2) * CELL + 6, "sela_root", 1);
    m.Npc("npc_pell",   "Pell the Trader", "citizen2",     21 * CELL + 50, 30 * CELL + 6, "pell_root", 0)["shop"] = "mossvale_general";
    // --- the waystone -----------------------------------------------------------------
    // On the square, west of the well and clear of the path down from the lodge.
    PlaceWaystone(m, "mossvale", 850, 770);

    // --- Wynn's -----------------------------------------------------------------------
    // She kept a stall on the north side of the square with her loom standing
    // out in the weather beside it, in earshot of Garrow's anvil -- which is no
    // place to keep cloth or to hear yourself count threads. She has a house
    // now, up its own lane in the quiet north-west of the village, with a
    // window full of what she makes: see mossvale_weavers, in
    // BuildWoodlandInteriors. The loom, the wheel and the woman are all in it.
    {
        const int hx = 20 * CELL + 16, hy = 13 * CELL;
        PlaceBuilding(m, "clothier_shop", hx, hy, 144, 146,
                      "mossvale_weavers", "entrance", "Enter Wynn's",
                      "from_mossvale_weavers", "props");
        json& o = m.Object("sign_weaver", "sign", hx + 86, hy + 4);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Wynn, Clothier";
        o["text"]   = "WYNN, CLOTHIER\n\nFLAX BOUGHT. FLEECES BOUGHT. SILK BOUGHT, NO QUESTIONS.\n\n"
                      "HATS, ROBES AND SKIRTS FOR THE COLLEGE. ASK FOR THE BOOK IF YOU CAN SEW. THE LOOM IS INSIDE.";
        m.Collision(hx + 86 - 16, hy + 4 - 10, 32, 10);
        // Flax in a bed by the door, and a tub of rolls out on the step on a dry day.
        m.Prop("props", "fabric_rolls", hx - 84, hy + 6);
        m.Collision(hx - 84 - 14, hy - 4, 28, 10);
        // And where she used to be, a board pointing at where she is.
        json& old = m.Object("sign_weaver_moved", "sign", 33 * CELL, 21 * CELL + 6);
        old["sprite"] = "assets/props/signpost.png";
        old["title"]  = "A board where the stall was";
        old["text"]   = "WYNN HAS MOVED.\n\nUp the lane north-west of the square: the house with the blue door "
                        "and the gowns in the window. It is quieter, and the cloth does not smell of the forge.";
        m.Collision(33 * CELL - 16, 21 * CELL - 4, 32, 10);
    }
    // The smith works the village anvil by the workbench, with his bars in a
    // crate at his elbow.
    m.Npc("npc_garrow", "Garrow the Smith", "fighter2", 49 * CELL + 8, 26 * CELL - 2, "garrow_root", 0)["shop"] = "mossvale_forge";
    m.Prop("props", "ingot_crate", 51 * CELL, 26 * CELL + 4);
    m.Collision(51 * CELL - 17, 26 * CELL - 8, 34, 12);
    m.Npc("npc_tamsin", "Tamsin",         "player_warden", 45 * CELL, 19 * CELL, "tamsin_root", 0, true);

    // --- village life along the street ------------------------------------------
    // Without these the walk from the gate to the square is a bare dirt road
    // through a lawn; a village shows itself before you reach its middle.
    {
        // The woodcutters' cabin: the herbalist's model again, but shut.
        const int hx = 14 * CELL, hy = 22 * CELL;
        m.Prop("props", "herbalist_cottage", hx, hy);
        m.Collision(hx - 68, hy - 125, 136, 125);
        m.Prop("props", "log_pile", 18 * CELL, 23 * CELL + 8);
        m.Collision(18 * CELL - 16, 23 * CELL - 2, 32, 10);
        m.Prop("props", "log_pile", 19 * CELL + 12, 23 * CELL + 8);
        m.Collision(19 * CELL - 4, 23 * CELL - 2, 32, 10);
        m.Prop("props", "barrel", 10 * CELL, 23 * CELL + 4);
        m.Collision(10 * CELL - 14, 23 * CELL - 6, 28, 10);
    }
    {
        // A drying rack of hides by the tanner's, under canvas.
        m.Prop("props", "tent", 41 * CELL, 37 * CELL);
        m.Collision(41 * CELL - 30, 37 * CELL - 24, 60, 24);
        m.Prop("props", "crates_sacks", 38 * CELL, 38 * CELL);
        m.Collision(38 * CELL - 18, 38 * CELL - 10, 36, 10);
    }

    // --- greenery, keeping clear of streets, square and buildings --------------
    auto is_path = [&](int cx, int cy) { return on_street(cx, cy) || on_lane(cx, cy) || in_square(cx, cy); };
    // Trees and tall fungus are drawn up from their base, so one planted on the
    // tile below a street hangs its canopy across it. Keep those two tiles back.
    auto near_path = [&](int cx, int cy) {
        for (int dy = -1; dy <= 3; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                if (is_path(cx + dx, cy - dy)) return true;
        return false;
    };
    auto near_building = [&](int cx, int cy) {
        if (cx >= 10 && cx <= 20 && cy >= 18 && cy <= 24) return true;   // woodcutters' cabin
        if (cx >= 37 && cx <= 43 && cy >= 34 && cy <= 39) return true;   // hide tent
        if (cy >= 28 && cy <= 32 && ((cx >= 17 && cx <= 23) || (cx >= 36 && cx <= 43))) return true;  // stalls
        if (cx >= 42 && cx <= 53 && cy >= 23 && cy <= 28) return true;   // the smith's corner
        return false;
    };
    for (int cy = 3; cy < H - 2; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (is_path(cx, cy) || reserved(cx, cy) || near_building(cx, cy)) continue;
            if (abs(cy - gate_row) <= 3 && cx < 8) continue;         // the gate
            const bool woods = (cx < 5 || cx > W - 6);
            const bool tall_ok = !near_path(cx, cy);
            const float r = Hash2(cx, cy, 5151);
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (woods) {
                if (r < 0.45f && tall_ok) PlaceForestTree(m, rng, x, y, r < 0.28f);
                else if (r < 0.60f)       m.Prop("objects", Pick(kBushes, rng), x, y);
            } else if (r < 0.025f && tall_ok) {
                PlaceForestTree(m, rng, x, y, false);
            } else if (r < 0.06f) {
                m.Prop("objects", Pick(kSmallBushes, rng), x, y);
            } else if (r < 0.075f && tall_ok) {
                m.Prop("objects", Pick(kFungus, rng), x, y);
            }
        }

    // --- the way in and out -----------------------------------------------------
    m.Portal(0, (gate_row - 1) * CELL, 24, 3 * CELL, "whisperwood_trail", "from_mossvale",
             "To the Whisperwood", false);
    m.Spawn("from_trail", 80, gate_row * CELL + 16);
    m.Spawn("default", (sq_cx - 2) * CELL, (sq_cy + 3) * CELL);
    m.Spawn("respawn", (sq_cx - 2) * CELL, (sq_cy + 3) * CELL);

    m.Write("maps");
}

// --- Fernhollow ------------------------------------------------------------------

static void BuildFernhollow() {
    const int CELL = 32, W = 46, H = 36;
    MapBuilder m("fernhollow", "Fernhollow", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("A hamlet on still water, where the trail runs out");
    m.Background(28, 44, 38);
    std::mt19937 rng(4646u);

    const int gate_col = 12;
    // The path wanders up through the hamlet, and straightens to go out of
    // the gate: it used to arrive a cell and a half to one side of it.
    auto path_x = [&](float cy) {
        const float settle = std::clamp((H - 6 - cy) / 6.0f, 0.0f, 1.0f);
        return gate_col + sinf(cy * 0.21f) * 2.2f * settle;
    };
    const float pcx = 32.0f, pcy = 15.0f, prx = 9.0f, pry = 7.0f;
    auto in_pond = [&](int cx, int cy) {
        const float dx = (cx - pcx) / prx, dy = (cy - pcy) / pry;
        return dx * dx + dy * dy < 1.0f;
    };
    auto on_jetty = [&](int cx, int cy) { return cy >= 15 && cy <= 16 && cx >= 22 && cx <= 29; };
    // The college's walk: paved, from the jetty road north to its gatehouse.
    auto on_college_walk = [&](int cx, int cy) { return cx >= 21 && cx <= 23 && cy >= 7 && cy <= 14; };
    auto on_path = [&](int cx, int cy) {
        if (cy >= 12 && fabsf(cx - path_x(static_cast<float>(cy))) < 1.2f) return true;   // from the gate
        if (cy >= 15 && cy <= 16 && cx >= gate_col && cx <= 23) return true;            // to the jetty
        if (on_college_walk(cx, cy)) return true;
        return false;
    };
    auto reserved = [&](int cx, int cy) {
        if (cx >= 8 && cx <= 16 && cy >= 5 && cy <= 12) return true;     // cottage
        if (cx >= 3 && cx <= 10 && cy >= 21 && cy <= 28) return true;    // shrine
        if (cx >= 16 && cx <= 24 && cy >= 23 && cy <= 29) return true;   // camp
        if (cx >= 15 && cx <= 21 && cy >= 17 && cy <= 21) return true;   // Nell's cart
        if (cx >= 18 && cx <= 27 && cy >= 0 && cy <= 9) return true;     // the college's gatehouse
        if (cx >= 17 && cx <= 20 && cy >= 12 && cy <= 15) return true;   // the waystone
        return false;
    };

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.24f, cy * 0.24f, 46);
            string tile;
            if (on_jetty(cx, cy))              tile = VariantOf("plank_floor", cx, cy);
            else if (in_pond(cx, cy))          tile = "water";
            else if (on_college_walk(cx, cy))  tile = VariantOf("college_paving", cx, cy);
            else if (on_path(cx, cy))          tile = VariantOf(v > 0.6f ? "dirt_dark" : "dirt", cx, cy);
            else tile = VariantOf(v > 0.6f ? "grass_olive" : (v > 0.28f ? "grass" : "moss"), cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            // The pond is marked as water rather than as plain collision: it
            // stops everything that walks, exactly as it did, and it is the
            // one place in the Hollowmarch a swimmer can go.
            if (in_pond(cx, cy) && !on_jetty(cx, cy)) m.Water(cx * CELL, cy * CELL, CELL, CELL);
        }

    // Edges: forest, open at the south gate.
    for (int cx = 0; cx < W; ++cx) {
        m.Collision(cx * CELL, 0, CELL, CELL);
        if (abs(cx - gate_col) > 2) m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        m.Collision(0, cy * CELL, CELL, CELL);
        m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    // Fishing: off the end of the jetty, and at the southern and eastern edges.
    {
        int n = 0;
        const auto spot = [&](int cx, int cy, int ox, int oy) {
            PlaceFishingSpot(m, "fish_pond_" + std::to_string(n++),
                             cx * CELL + 16 + ox, cy * CELL + 16 + oy, "pond",
                             {"raw_minnow", "raw_trout", "raw_pike"}, 1);
        };
        spot(30, 15, -4, 10);
        for (int cy = H - 2; cy > 0; --cy)
            if (in_pond(32, cy)) { spot(32, cy, 0, 8); break; }
        for (int cx = W - 2; cx > 0; --cx)
            if (in_pond(cx, 17)) { spot(cx, 17, 8, 0); break; }
    }

    // --- the waystone ---------------------------------------------------------------
    // On the grass north of the jetty road, between the cottage and the water.
    PlaceWaystone(m, "fernhollow", 600, 446);

    // --- the birds on the pond ---------------------------------------------------
    // Ducks and geese, posted on the bank a little way out from the water so
    // that walking into it is something they decide to do rather than where
    // they start. Nothing else in the game can enter the pond; these two spend
    // the day going in and coming out again, which is the whole of the point
    // of them. Their leash is long enough to take in a good stretch of open
    // water, so a bird that goes swimming has somewhere to swim to.
    {
        // A point on the bank at this angle: out along the ellipse until the
        // ground is dry and nothing else is standing there.
        const auto bank = [&](float deg, int& out_x, int& out_y) {
            const float a = deg * 3.14159265f / 180.0f;
            for (float k = 1.05f; k < 1.9f; k += 0.06f) {
                const int cx = static_cast<int>(lroundf(pcx + cosf(a) * prx * k));
                const int cy = static_cast<int>(lroundf(pcy + sinf(a) * pry * k));
                if (cx < 2 || cy < 2 || cx >= W - 2 || cy >= H - 2) break;
                if (in_pond(cx, cy) || on_jetty(cx, cy) || reserved(cx, cy)) continue;
                const int x = cx * CELL + 16, y = cy * CELL + 24;
                if (!m.Clear(x, y)) continue;
                out_x = x;
                out_y = y;
                return true;
            }
            return false;
        };
        int bx = 0, by = 0;
        // Ducks all round it, geese along the north shore where the grass is.
        const float ducks[] = {28.0f, 96.0f, 155.0f, 215.0f, 300.0f, 342.0f};
        for (float deg : ducks)
            if (bank(deg, bx, by)) m.Enemy("duck", bx, by, 1, 90.0f, 170.0f);
        const float geese[] = {248.0f, 272.0f, 320.0f};
        for (float deg : geese)
            if (bank(deg, bx, by)) m.Enemy("goose", bx, by, 2, 110.0f, 170.0f);
    }

    PlaceBuilding(m, "building_house_a", gate_col * CELL + 16, 11 * CELL, 136, 147,
                  "fernhollow_cottage", "entrance", "Enter the ferry cottage",
                  "from_fernhollow_cottage");

    // The college. It was a tower in the south-east corner with one room in
    // it; it is on the north side now and the hamlet has only its gatehouse --
    // two towers under blue slate and an arch between them -- at the head of a
    // paved walk up from the jetty road. What is through it is its own map:
    // see BuildCollege. A way into the college is a door, not a road out of
    // the hamlet, so it is walked up to and gone through like one.
    {
        const int gx = 22 * CELL + 16, gy = 7 * CELL + 8;
        PlaceBuilding(m, "college_gate", gx, gy, 200, 154,
                      "college_grounds", "entrance", "Go through to the college",
                      "from_college_grounds", "props");
        for (int side : {-1, 1}) {
            m.Prop("props", "college_banner", gx + side * 62, gy + 76);
            m.Collision(gx + side * 62 - 6, gy + 68, 12, 8);
            json& lamp = m.Object(side < 0 ? "lamp_college_walk_w" : "lamp_college_walk_e", "lamp", gx + side * 62, gy + 190);
            lamp["sprite"] = "assets/props/college_lamp.png";
            m.Collision(gx + side * 62 - 6, gy + 182, 12, 8);
        }
        json& o = m.Object("sign_college_gate", "sign", gx + 96, gy + 40);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "A brass plate on the gatepost";
        o["text"]   = "THE COLLEGE AT FERNHOLLOW\n\nFOUNDED BEFORE THE HAMLET, AND NOT ANSWERABLE TO IT.\n\n"
                      "Visitors are welcome in the court, the practice hall and the lecture room. "
                      "The council sits in the great hall, and the Magister keeps the old magic there.";
        m.Collision(gx + 96 - 16, gy + 30, 32, 10);
        m.Npc("npc_college_porter", "Porter Hobb", "citizen2", gx - 58, gy + 30, "college_porter_root", 0);
    }

    // The shrine: standing stones in a ring round a candle, and the one thing
    // anyone in Fernhollow will tell you about the pond.
    {
        const int sx = 6 * CELL + 16, sy = 24 * CELL + 16;
        for (int i = 0; i < 6; ++i) {
            const float a = i / 6.0f * 6.2831853f;
            const int rx = sx + static_cast<int>(cosf(a) * 46.0f);
            const int ry = sy + static_cast<int>(sinf(a) * 30.0f);
            m.Prop("objects", Pick(kSmallRocks, rng), rx, ry);
            m.Collision(rx - 8, ry - 6, 16, 6);
        }
        m.Prop("props", "candlestand", sx, sy);
        json& o = m.Object("shrine_fernhollow", "sign", sx + 70, sy + 10);
        o["sprite"] = ObjPath("rock_02");
        o["title"]  = "Shrine stone";
        o["text"]   = "Smooth where hands have touched it for longer than the hamlet has stood.\n\n"
                      "Cut into the stone: THE WATER REMEMBERS.\n\n"
                      "Fresh flowers at its foot, and a coin.";
        m.Collision(sx + 70 - 14, sy, 28, 10);
        m.Npc("npc_mira", "Mira of the Shrine", "citizen1", sx + 20, sy + 64, "mira_root", 0);
        // The table by the stones, where Mira's charms are worked.
        PlaceEnchantingTable(m, "altar_fernhollow", sx - 72, sy + 14);
    }

    // A traveller's camp by the path.
    {
        const int cx0 = 20 * CELL, cy0 = 26 * CELL;
        PlaceCampsite(m, "campsite_fernhollow", cx0, cy0 - 20);
        m.Collision(cx0 - 30, cy0 - 36, 60, 16);
        json& o = m.Object("range_fernhollow", "range", cx0 + 60, cy0 + 28);
        o["sprite"] = "assets/props/campfire_ring.png";
        o["title"]  = "Camp fire";
        m.Collision(cx0 + 60 - 16, cy0 + 18, 32, 10);
        m.Prop("props", "log_pile", cx0 - 60, cy0 + 24);
        m.Collision(cx0 - 76, cy0 + 14, 32, 10);
        PlaceCauldron(m, "cauldron_fernhollow", cx0 + 104, cy0 + 8);
    }

    m.Npc("npc_wendel", "Old Wendel", "citizen2", 28 * CELL, 16 * CELL + 10, "wendel_root", 0)["shop"] = "fernhollow_tackle";

    // A pedlar's cart just off the path to the jetty: the hamlet's only shop.
    {
        const int sx = 18 * CELL, sy = 20 * CELL;
        m.Prop("props", "market_stall", sx, sy);
        m.Collision(sx - 32, sy - 14, 64, 14);
        m.Prop("props", "travel_chest", sx + 52, sy - 2);
        m.Collision(sx + 52 - 16, sy - 12, 32, 10);
        m.Npc("npc_nell", "Nell the Pedlar", "citizen1", sx - 50, sy + 6, "nell_root", 0)["shop"] = "fernhollow_general";
    }

    // Reeds and brookmint round the shore, marigolds in the meadow to the
    // south, trees round everything else.
    int herb_i = 0;
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            if (in_pond(cx, cy) || on_path(cx, cy) || on_jetty(cx, cy) || reserved(cx, cy)) continue;
            if (abs(cx - gate_col) <= 3 && cy > H - 8) continue;
            if (cy > H - 5) continue;                                  // the palisade, and the verge outside it
            const bool shore = in_pond(cx + 1, cy) || in_pond(cx - 1, cy) ||
                               in_pond(cx, cy + 1) || in_pond(cx, cy - 1);
            const bool woods = (cx < 4 || cy < 4 || cx > W - 5 || cy > H - 9);
            // Tall things hang up over the tiles above them; keep them off the path.
            bool tall_ok = true;
            for (int dy = -1; dy <= 3 && tall_ok; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (on_path(cx + dx, cy - dy) || on_jetty(cx + dx, cy - dy)) { tall_ok = false; break; }
            const float r = Hash2(cx, cy, 4747);
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (shore) {
                if (r < 0.45f) m.Prop("objects", Pick(kSmallBushes, rng), x, y);
                // The pond is the best brookmint in the woods.
                else if (Hash2(cx, cy, 4848) < 0.40f) PlaceHerb(m, "brookmint", x, y, herb_i);
            } else if (woods) {
                if (r < 0.50f && tall_ok) PlaceForestTree(m, rng, x, y, r < 0.30f);
                else if (r < 0.62f)       m.Prop("objects", Pick(kBushes, rng), x, y);
            } else if (r < 0.03f) {
                if (tall_ok) PlaceForestTree(m, rng, x, y, false);
            } else if (r < 0.07f && tall_ok) {
                m.Prop("objects", Pick(kFungus, rng), x, y);
            } else if (cy > 20 && Hash2(cx, cy, 4949) < 0.04f) {
                PlaceHerb(m, "marigold", x, y, herb_i);
            }
        }

    // A few animals in the meadow south of the pond.
    m.Enemy("hare", 30 * CELL, 28 * CELL, 2);
    m.Enemy("deer", 36 * CELL, 27 * CELL, 3);
    m.Enemy("fox",  40 * CELL, 30 * CELL, 4);

    // The way in from the trail is a gate, and Ilse keeps it: a stockade along
    // the open south side, with the forest doing the rest of the wall.
    PlaceFrontGate(m, gate_col * CELL + 16, (H - 2) * CELL, 99, W * CELL);
    m.Npc("npc_ilse", "Warden Ilse", "player_hero", FrontGateKeeperX(gate_col * CELL + 16),
          FrontGateKeeperY((H - 2) * CELL), "ilse_root", 0)["tint"] = json::array({226, 240, 226});

    m.Portal(gate_col * CELL - 64, H * CELL - 40, 160, 40, "whisperwood_trail", "from_fernhollow",
             "To the Whisperwood", false);
    m.Spawn("from_trail", gate_col * CELL + 16, (H - 4) * CELL - 8);
    m.Spawn("default",    gate_col * CELL + 16, 20 * CELL);
    m.Spawn("respawn",    gate_col * CELL + 16, 20 * CELL);

    m.Write("maps");
}

// --- the new interiors --------------------------------------------------------------

static void BuildWoodlandInteriors() {
    // The lodge at Mossvale: where the reeve keeps his table and the hunters
    // keep their gear. A great hearth, two long tables, racks on the walls.
    {
        const int CELL = 32, cols = 22, rows = 14;
        MapBuilder m("mossvale_lodge_hall", "Mossvale Lodge", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 18, 14);
        RoomShell(m, cols, rows, CELL, "plank_floor_dark", "plaster_wall_warm",
                  cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "mossvale", "from_mossvale_lodge",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        m.Overlay("props", "inn_rug", dx, 6 * CELL + 16);
        {
            json& o = m.Object("range_lodge", "range", dx, 3 * CELL + 10);
            o["sprite"] = "assets/props/inn_fireplace.png";
            o["title"]  = "Great hearth";
            m.Collision(dx - 50, 3 * CELL + 10 - 24, 100, 24);
        }
        piece("weapon_rack",  3 * CELL,          3 * CELL + 6, 60, 12);
        piece("banner",       6 * CELL + 16,     3 * CELL + 6, 0, 0);
        piece("armour_stand", (cols - 3) * CELL, 3 * CELL + 6, 34, 12);
        piece("banner",       (cols - 6) * CELL, 3 * CELL + 6, 0, 0);

        // Two long tables with benches, either side of the middle of the room.
        for (int side : {-1, 1}) {
            const int tx = dx + side * 190;
            piece("tavern_bench", tx, 6 * CELL + 4,  48, 8);
            piece("table_long",   tx, 8 * CELL + 4,  88, 14);
            piece("tavern_bench", tx, 9 * CELL + 20, 48, 8);
        }
        piece("barrel",    2 * CELL + 8,        (rows - 2) * CELL, 28, 10);
        piece("strongbox", (cols - 2) * CELL,   (rows - 2) * CELL, 32, 10);
        piece("bookshelf", (cols - 2) * CELL - 8, 7 * CELL, 56, 12);

        m.Npc("npc_hadley", "Reeve Hadley", "fighter2", dx + 70, 5 * CELL, "hadley_root", 0);
        m.Write("maps");
    }

    // Oona's cottage: an herbalist's, all drying herbs and books.
    {
        const int CELL = 32, cols = 16, rows = 12;
        MapBuilder m("mossvale_herbalist", "Oona's Cottage", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 18, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor", "plaster_wall",
                  cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "mossvale", "from_mossvale_herbalist",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        {
            json& o = m.Object("range_oona", "range", 12 * CELL + 16, 100);
            o["sprite"] = "assets/props/cottage_hearth.png";
            o["title"]  = "Herb stove";
            m.Collision(12 * CELL + 16 - 32, 74, 64, 26);
        }
        piece("herb_pots",         2 * CELL + 8, 3 * CELL + 4, 30, 10);
        piece("herb_pots",         5 * CELL,     3 * CELL + 4, 30, 10);
        piece("cottage_bookshelf", 8 * CELL,     3 * CELL + 4, 40, 14);
        piece("lectern",           2 * CELL + 16, 7 * CELL,    28, 10);
        piece("table_round",       7 * CELL,     7 * CELL + 8, 40, 12);
        piece("tavern_chair",      5 * CELL + 16, 7 * CELL + 10, 16, 8);
        PlaceBed(m, "bed_oona", "bed_single", 13 * CELL + 8, 8 * CELL, 30, 36);
        PlaceCauldron(m, "cauldron_oona", 4 * CELL + 8, 9 * CELL + 20);
        m.Npc("npc_oona", "Oona the Herbalist", "citizen1", 9 * CELL + 16, 5 * CELL + 10, "oona_root", 0)["shop"] = "mossvale_herbalist";
        m.Write("maps");
    }

    // The house in Mossvale, once the player has the key. Nobody lives here:
    // a hearth to cook at, a bed to sleep in, a bench to work at, and the one
    // chest in the world that keeps what is put in it.
    {
        const int CELL = 32, cols = 16, rows = 12;
        MapBuilder m("mossvale_cottage", "Your House in Mossvale", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("Nobody has been in here for a season");
        m.Background(22, 18, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor", "plaster_wall_warm",
                  cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "mossvale", "from_mossvale_cottage",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        // By the door, where a rug is for. It lay in the middle of the floor,
        // which is the ring's now.
        m.Overlay("props", "rug", dx, 9 * CELL + 16);
        // The ring, in the middle of the floor: where a boss's totem is stood.
        // It is set in the boards -- an overlay, under everybody's feet -- and
        // the thing that is touched has no picture of its own. What stands in
        // it is the character's, and the game draws that: see World, `5`.
        {
            const int rx = (cols / 2) * CELL, ry = (rows / 2) * CELL;
            m.Overlay("props", "totem_circle", rx, ry);
            json& o = m.Object("totem_ring_mossvale", "totem_circle", rx, ry);
            o["title"] = "The ring";

            // And while a totem stands in it, the room is dressed in its
            // colours: the boards and the walls from their undyed pictures,
            // tinted; a rug round the ring with a circle worked on it (its
            // picture sits forty pixels low in its frame, so it is laid that
            // much higher to centre it on the ring); two banners on the bare
            // back wall east of the bookshelf; and the old rug by the door
            // taken up. Nothing about it blocks: the ring keeps its clear floor.
            for (const char* v : {"", "_1", "_2"})
                m.Dress("floor", string("plank_floor") + v, string("assets/tiles/plank_floor_pale") + v + ".png");
            m.Dress("wall", "plaster_wall_warm", "assets/tiles/plaster_wall_pale.png");
            m.Overlay("props", "house_rug", rx, ry - 40);
            m.Overlay("props", "house_rug_trim", rx, ry - 40);
            for (const int hx : {12 * CELL, 14 * CELL}) {
                m.Prop("props", "tapestry_house", hx, 2 * CELL + 4);
                m.Prop("props", "tapestry_house_trim", hx, 2 * CELL + 4);
            }
            m.Dress("cloth", "~house_rug");
            m.Dress("trim", "~house_rug_trim");
            m.Dress("cloth", "tapestry_house");
            m.Dress("trim", "tapestry_house_trim");
            m.Dress("plain", "~rug");
            m.DressLight(rx, ry);
        }
        {
            json& o = m.Object("range_cottage", "range", 3 * CELL, 100);
            o["sprite"] = "assets/props/cottage_hearth.png";
            o["title"]  = "Hearth";
            m.Collision(3 * CELL - 32, 74, 64, 26);
        }
        // The chest. A hundred slots, ten by ten, and what goes in it stays
        // there: it is the player's own, saved with the character rather than
        // with the map.
        {
            json& o = m.Object("storage_mossvale", "storage", 12 * CELL, 4 * CELL + 8);
            o["sprite"]   = "assets/props/travel_chest.png";
            o["title"]    = "Storage Chest";
            o["capacity"] = 100;
            m.Collision(12 * CELL - 16, 4 * CELL + 8 - 14, 32, 14);
        }
        PlaceBed(m, "bed_cottage", "bed_single", 13 * CELL + 8, 8 * CELL, 30, 36);
        // Table and chair in the south-west of the room, clear of the ring:
        // they stood a hand's width from the middle of the floor.
        piece("dining_table", 5 * CELL + 8,  8 * CELL + 8,  46, 14);
        piece("tavern_chair", 3 * CELL + 24, 8 * CELL + 10, 16, 8);
        piece("wardrobe",     6 * CELL,      3 * CELL + 4,  34, 14);
        piece("cottage_bookshelf", 9 * CELL, 3 * CELL + 4,  40, 14);
        piece("barrel",       2 * CELL,      9 * CELL,      28, 10);
        {
            json& o = m.Object("bench_cottage", "workbench", 2 * CELL + 16, 5 * CELL + 16);
            o["sprite"]  = "assets/props/workbench.png";
            o["title"]   = "Workbench";
            o["station"] = "workbench";
            m.Collision(2 * CELL + 16 - 34, 5 * CELL + 16 - 18, 67, 18);
        }
        m.Write("maps");
    }

    // Wynn's, at Mossvale: a draper's. Bolts in racks along the back wall and
    // hangings between them, forms dressed in her work down the front of the
    // shop where the window is, the cutting table in the middle of the floor,
    // the counter she sells over -- and the loom and the wheel, which used to
    // stand outside.
    {
        const int CELL = 32, cols = 22, rows = 15;
        MapBuilder m("mossvale_weavers", "Wynn's", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("Cloth, cut to order");
        m.Background(22, 18, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor", "plaster_wall_warm", cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "mossvale", "from_mossvale_weavers", "Step outside", false);
        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        m.Overlay("props", "inn_rug", dx, 11 * CELL);

        // The back wall: bolts, and hangings between the racks.
        piece("fabric_shelf", 8 * CELL,       3 * CELL + 6, 70, 14);
        piece("fabric_shelf", 11 * CELL + 16, 3 * CELL + 6, 70, 14);
        piece("fabric_shelf", 15 * CELL,      3 * CELL + 6, 70, 14);
        piece("tapestry_red",   5 * CELL + 8,  2 * CELL + 6, 0, 0);
        piece("tapestry_blue",  18 * CELL,     2 * CELL + 6, 0, 0);
        piece("tapestry_green", 20 * CELL + 8, 2 * CELL + 6, 0, 0);

        // The counter, in the corner by the racks, and Wynn behind it.
        piece("shop_counter", 3 * CELL + 8, 5 * CELL + 16, 72, 16);
        m.Npc("npc_wynn", "Wynn the Clothier", "citizen1", 3 * CELL + 8, 4 * CELL + 14, "wynn_root", 0)["shop"] = "mossvale_clothier";

        // The loom and the wheel, down the east side where the light is.
        {
            json& o = m.Object("loom_weaver", "workbench", 18 * CELL, 7 * CELL + 16);
            o["sprite"]  = "assets/props/loom.png";
            // Named so it reads after "the": these titles are dropped into
            // "Use the ..." with only the first letter lowered, so a possessive
            // comes out as "the wynn's Loom".
            o["title"]   = "Weaver's loom";
            o["station"] = "loom";
            m.Collision(18 * CELL - 36, 7 * CELL + 16 - 20, 72, 20);
        }
        piece("spinning_wheel", 19 * CELL, 10 * CELL + 16, 28, 12);
        piece("fabric_rolls",   16 * CELL, 10 * CELL + 8,  28, 10);

        // The cutting table, in the middle of the floor.
        piece("cutting_table", 10 * CELL + 16, 8 * CELL, 70, 18);
        piece("fabric_rolls",  7 * CELL,       8 * CELL + 4, 28, 10);

        // The forms, down the front of the shop: a robe for the college, a gown, a travelling cloak.
        piece("mannequin_robe",  3 * CELL,      11 * CELL + 16, 18, 8);
        piece("mannequin_dress", 5 * CELL + 16, 12 * CELL,      18, 8);
        piece("mannequin_cloak", 8 * CELL,      11 * CELL + 16, 18, 8);
        piece("mannequin_dress", 14 * CELL,     12 * CELL + 8,  18, 8);
        piece("mannequin_robe",  16 * CELL + 16, 12 * CELL + 8, 18, 8);
        m.Write("maps");
    }

    // The ferry cottage at Fernhollow, where Wendel and Hesper live.
    {
        const int CELL = 32, cols = 16, rows = 12;
        MapBuilder m("fernhollow_cottage", "The Ferry Cottage", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Background(22, 18, 16);
        RoomShell(m, cols, rows, CELL, "plank_floor_dark", "plaster_wall_warm",
                  cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "fernhollow", "from_fernhollow_cottage",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        m.Overlay("props", "rug", dx, 6 * CELL + 16);
        {
            json& o = m.Object("range_ferry", "range", 12 * CELL + 16, 100);
            o["sprite"] = "assets/props/cottage_hearth.png";
            o["title"]  = "Hearth";
            m.Collision(12 * CELL + 16 - 32, 74, 64, 26);
        }
        PlaceBed(m, "bed_ferry", "bed_double", 2 * CELL + 16, 5 * CELL, 36, 40);
        piece("wardrobe",     6 * CELL,      3 * CELL + 4,  34, 14);
        piece("dining_table", 8 * CELL + 16, 7 * CELL + 8,  46, 14);
        piece("tavern_chair", 7 * CELL,      7 * CELL + 10, 16, 8);
        piece("tavern_chair", 10 * CELL,     7 * CELL + 10, 16, 8);
        piece("travel_chest", 13 * CELL + 8, 9 * CELL,      28, 12);
        m.Npc("npc_hesper", "Hesper", "citizen2", 11 * CELL, 5 * CELL + 10, "hesper_root", 0);
        m.Write("maps");
    }

    // (The college's hall was built here, as one room under a tower. The
    // college is a place of its own now: see BuildCollege.)
}


// --- the College at Fernhollow ---------------------------------------------------
//
// It was a tower in the corner of the hamlet with one room in it. It is a place
// now: through a gatehouse on the hamlet's north side into a great court, with
// the hall across the north of it and a chamber off the west and the east.
//
//   college_grounds      the court: the fountain, the founders, the lawns
//   fernhollow_college   north: the great hall, where the council sits and the
//                        Magister keeps the circle (the id it always had, so
//                        everything that knew the way to him still does)
//   college_training     west: the practice hall, where they throw fire at straw
//   college_classroom    east: the lecture room
//
// All of it in the college's own tiles -- see make_ground.ps1 -- because the
// point of the place is that it does not look like the Hollowmarch.

// A chamber: chequer floor, banded walls, and its door wherever it is. `door`
// is 'S' for the usual one in the front wall, or 'E' / 'W' for one in a side
// wall, which is how a room off the side of a court is left. The door cells are
// carpet, and open.
//
// `runner` says which cells of the floor are carpet. It has to be said here and
// not laid afterwards: the ground is drawn a tile name at a time in the order of
// the alphabet, so a carpet laid over a floor is under it.
static void CollegeRoom(MapBuilder& m, int cols, int rows, int CELL, char door, int d0, int d1,
                        const std::function<bool(int, int)>& runner = nullptr) {
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool back = cy <= 1, front = cy == rows - 1, west = cx == 0, east = cx == cols - 1;
            bool gap = false;
            if (door == 'S') gap = front && cx >= d0 && cx <= d1;
            if (door == 'E') gap = east && cy >= d0 && cy <= d1;
            if (door == 'W') gap = west && cy >= d0 && cy <= d1;
            const bool solid = (back || front || west || east) && !gap;
            // The back wall is seen: plain ashlar above, the blue band along its
            // foot. The other three are looked down on, and are their tops --
            // the band run up the side walls a tile at a time was a ladder.
            const string wall = (west || east || front) ? string("college_walltop")
                              : cy == 0 ? string("college_wallface") : VariantOf("college_wall", cx, cy);
            const bool carpet = gap || (!solid && runner && runner(cx, cy));
            m.Ground(solid ? wall : carpet ? string("college_carpet")
                           : VariantOf("college_floor", cx, cy), cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
}

// A lamp standard: a thing that stands there and, after dark, is lit. See World's lights.
static void PlaceLamp(MapBuilder& m, const string& id, int x, int y) {
    json& o = m.Object(id, "lamp", x, y);
    o["sprite"] = "assets/props/college_lamp.png";
    m.Collision(x - 6, y - 8, 12, 8);
}

static void BuildCollege() {
    const int CELL = 32;

    // ------------------------------------------------------------------ the court
    {
        const int W = 60, H = 46;
        MapBuilder m("college_grounds", "The College at Fernhollow", W * CELL, H * CELL);
        m.Ambient("grove");
        m.Subtitle("Older than the hamlet, and not of it");
        m.Background(40, 44, 58);

        const int mid = W / 2;                       // the avenue runs up the middle
        const int cross = 25;                        // and the cross-walk from the west door to the east
        auto lawn = [&](int cx, int cy) {
            const bool ew = (cx >= 6 && cx <= 24) || (cx >= 35 && cx <= 53);
            const bool ns = (cy >= 14 && cy <= 21) || (cy >= 29 && cy <= 40);
            return ew && ns;
        };
        auto avenue = [&](int cx, int cy) {
            if (abs(cx - mid) <= 1 || cx == mid - 2) return cy >= 9;                           // gate to hall
            if (cy == cross || cy == cross - 1) return true;                                    // door to door
            const float dx = static_cast<float>(cx - mid) + 0.5f, dy = static_cast<float>(cy - cross) + 0.5f;
            return dx * dx + dy * dy < 30.0f;                                                   // round the fountain
        };
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const bool side = cx <= 1 || cx >= W - 2;
                const bool north = cy <= 1, south = cy >= H - 2;
                const bool west_door = cx <= 1 && (cy == cross - 1 || cy == cross);
                const bool east_door = cx >= W - 2 && (cy == cross - 1 || cy == cross);
                const bool south_gate = south && cx >= mid - 2 && cx <= mid + 1;
                string tile;
                bool solid = false;
                const bool door_run = (cy == cross - 1 || cy == cross) && (cx <= 4 || cx >= W - 5);
                if (west_door || east_door || (door_run && !side)) tile = "college_carpet";
                else if (south_gate)        tile = "college_inlay";
                else if (side || north || south) { tile = "college_walltop"; solid = true; }
                else if (cy <= 3)           { tile = "college_wallface"; solid = true; }     // the north wall, seen
                else if (lawn(cx, cy))      tile = VariantOf("grass", cx, cy);
                else if (avenue(cx, cy))    tile = "college_inlay";
                else                        tile = VariantOf("college_paving", cx, cy);
                m.Ground(tile, cx * CELL, cy * CELL, CELL);
                if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };

        // The great hall, across the north: the council's chamber is behind its doors.
        const int hall_x = mid * CELL, hall_y = 9 * CELL + 20;      // its roofs against the north wall
        PlaceBuilding(m, "college_hall", hall_x, hall_y, 340, 178,
                      "fernhollow_college", "entrance", "Enter the great hall",
                      "from_fernhollow_college", "props");
        // A wing either side of it, so the north of the court is one front: the
        // hall alone was a fifth of the width of the place it was meant to
        // preside over. They stand a little back from it -- their foot is level
        // with the hall's own, which is behind its steps -- and so are drawn
        // behind its towers, which they run in under.
        for (int side : {-1, 1}) {
            const int wx = hall_x + side * 286, wy = hall_y - 22;
            m.Prop("props", "college_wing", wx, wy);
            m.Collision(wx - 122, wy - 114, 244, 114);
        }

        // The fountain, where the two walks cross.
        const int fx = mid * CELL, fy = cross * CELL + 24;
        piece("college_fountain", fx, fy, 84, 34);

        // The lawns: a hedge along each side that faces a walk, a founder in the
        // middle of each of the four, and box in pots at the corners.
        struct Bed { int x0, y0, x1, y1; };
        const Bed beds[4] = {{6, 14, 24, 21}, {35, 14, 53, 21}, {6, 29, 24, 40}, {35, 29, 53, 40}};
        for (const Bed& b : beds) {
            for (int cx = b.x0 + 1; cx < b.x1; cx += 2) {
                piece("hedge", cx * CELL + 16, b.y0 * CELL + 22, 52, 12);
                piece("hedge", cx * CELL + 16, (b.y1 + 1) * CELL - 2, 52, 12);
            }
            const int sx = (b.x0 + b.x1 + 1) * CELL / 2, sy = (b.y0 + b.y1 + 1) * CELL / 2 + 16;
            piece("college_statue", sx, sy, 30, 14);
            for (int cx : {b.x0 + 3, b.x1 - 2})
                piece("topiary", cx * CELL, sy - 4, 18, 8);
        }
        // Benches along the avenue, facing it, and the college's colours up both sides of it.
        for (int cy : {16, 19, 31, 35, 39}) {
            piece("stone_bench", (mid - 4) * CELL, cy * CELL + 16, 40, 10);
            piece("stone_bench", (mid + 4) * CELL, cy * CELL + 16, 40, 10);
        }
        for (int cy : {12, 22, 28, 33, 37, 42}) {
            piece("college_banner", (mid - 3) * CELL + 8, cy * CELL, 12, 8);
            piece("college_banner", (mid + 3) * CELL - 8, cy * CELL, 12, 8);
        }
        // Lamps: at the corners of the walks, and along the cross-walk.
        {
            int k = 0;
            for (int cx : {4, 12, 20, 40, 48, 56})
                for (int cy : {cross - 2, cross + 2})
                    PlaceLamp(m, "lamp_court_" + std::to_string(k++), cx * CELL, cy * CELL + (cy < cross ? 8 : 24));
            for (int cy : {11, 43})
                for (int side : {-1, 1})
                    PlaceLamp(m, "lamp_court_" + std::to_string(k++), (mid + side * 5) * CELL, cy * CELL);
        }
        // A colonnade down the west wall and the east: a column every third cell,
        // broken where the doors are.
        for (int cy = 6; cy < H - 3; cy += 3) {
            if (abs(cy - cross) <= 2) continue;
            piece("college_column", 3 * CELL + 8, cy * CELL, 16, 8);
            piece("college_column", (W - 3) * CELL - 8, cy * CELL, 16, 8);
        }
        // The two side doors: a pair of columns and the colours either side of
        // each, a carpet run out to the walk, and a board saying what is inside.
        for (int side : {-1, 1}) {
            const int wall_x = side < 0 ? 2 * CELL : (W - 2) * CELL;
            for (int cy : {cross - 2, cross + 1}) {
                piece("college_column", wall_x + side * -22, cy * CELL + (cy < cross ? 24 : 40), 16, 8);
            }
        }
        {
            json& o = m.Object("sign_college_training", "sign", 5 * CELL, (cross - 2) * CELL + 8);
            o["sprite"] = "assets/props/signpost.png";
            o["title"]  = "The Practice Hall";
            o["text"]   = "THE PRACTICE HALL\n\nNOTHING IN HERE IS THROWN AT YOU. WALK WHERE YOU LIKE.\n\n"
                          "Chalked underneath: and nothing in here teaches you anything either. It is straw.";
            m.Collision(5 * CELL - 16, (cross - 2) * CELL - 2, 32, 10);
        }
        {
            json& o = m.Object("sign_college_classroom", "sign", (W - 5) * CELL, (cross - 2) * CELL + 8);
            o["sprite"] = "assets/props/signpost.png";
            o["title"]  = "The Lecture Room";
            o["text"]   = "THE LECTURE ROOM\n\nLECTOR MAUD, DAILY. THE FOUR ELEMENTS AND WHAT EACH ONE FEARS.\n\n"
                          "Sit anywhere. Do not touch the orrery.";
            m.Collision((W - 5) * CELL - 16, (cross - 2) * CELL - 2, 32, 10);
        }
        {
            json& o = m.Object("sign_college_founders", "sign", (mid + 3) * CELL, 44 * CELL - 40);
            o["sprite"] = "assets/props/signpost.png";
            o["title"]  = "Cut into the gatepost";
            o["text"]   = "THE COLLEGE AT FERNHOLLOW\n\nThe hall, north. The practice hall, west. The lecture room, east.\n\n"
                          "THE FOUR FOUNDERS STAND ON THE LAWNS. THEY ARE OLDER THAN THE HAMLET AND THEY WILL OUTLAST IT.";
            m.Collision((mid + 3) * CELL - 16, 44 * CELL - 50, 32, 10);
        }

        // People. Two who walk the court by the clock, and three who stand about in it.
        {
            json& a = m.Npc("npc_college_walker_a", "Apprentice Tam", "apprentice", 8 * CELL, cross * CELL + 8,
                            "college_apprentice_root", 0);
            a["path"] = json::array({json::array({8 * CELL, cross * CELL + 8, 6, 1}), json::array({(mid - 6) * CELL, cross * CELL + 8, 2, 0}),
                                     json::array({(mid - 6) * CELL, 12 * CELL + 16, 8, 3}), json::array({(mid - 6) * CELL, cross * CELL + 8, 1, 0})});
            a["speed"] = 30;
            json& b = m.Npc("npc_college_walker_b", "Adept Sorrel", "adept", (W - 8) * CELL, cross * CELL - 8,
                            "college_adept_root", 0);
            b["path"] = json::array({json::array({(W - 8) * CELL, cross * CELL - 8, 5, 2}), json::array({(mid + 6) * CELL, cross * CELL - 8, 2, 0}),
                                     json::array({(mid + 6) * CELL, 42 * CELL, 9, 0}), json::array({(mid + 6) * CELL, cross * CELL - 8, 1, 3})});
            b["speed"] = 28;
            b["phase"] = 40;
        }
        m.Npc("npc_college_reader", "Apprentice Isa", "apprentice", (mid - 4) * CELL, 31 * CELL + 30, "college_reader_root", 2);
        m.Npc("npc_college_gardener", "Old Peverell", "citizen2", 15 * CELL, 28 * CELL, "college_gardener_root", 0);
        m.Npc("npc_college_usher", "Usher Brandt", "magister", (mid + 3) * CELL, 10 * CELL + 24, "college_usher_root", 0);

        // Ways out. South, through the gatehouse, to the hamlet; west and east into the chambers.
        m.Spawn("entrance", mid * CELL - 16, (H - 4) * CELL);
        m.Spawn("default",  mid * CELL - 16, (H - 4) * CELL);
        m.Portal((mid - 2) * CELL, H * CELL - 24, 4 * CELL, 24, "fernhollow", "from_college_grounds", "To Fernhollow", false);
        m.Portal(0, (cross - 1) * CELL, 24, 2 * CELL, "college_training", "entrance", "The practice hall", false);
        m.Spawn("from_college_training", 4 * CELL, cross * CELL);
        m.Portal(W * CELL - 24, (cross - 1) * CELL, 24, 2 * CELL, "college_classroom", "entrance", "The lecture room", false);
        m.Spawn("from_college_classroom", (W - 4) * CELL, cross * CELL);
        m.Write("maps");
    }

    // ------------------------------------------------ west: the practice hall
    // Four lanes, a straw man at the end of each, and somebody at the head of
    // each throwing what they are learning at him. Nothing thrown in here
    // touches anybody: it is walked through. The door is in the east wall,
    // because the room is off the court's west side.
    {
        const int cols = 26, rows = 17;
        MapBuilder m("college_training", "The Practice Hall", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("Four lanes, four straw men, and nobody runs dry");
        m.Background(16, 16, 28);
        // The runner in from the door, and a mark across each lane where its caster stands.
        CollegeRoom(m, cols, rows, CELL, 'E', 12, 13, [&](int cx, int cy) {
            if ((cy == 12 || cy == 13) && cx >= cols - 5) return true;
            return cx == 14 && (cy == 4 || cy == 6 || cy == 8 || cy == 10);
        });
        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        m.Spawn("entrance", (cols - 3) * CELL, 13 * CELL);
        m.Spawn("default",  (cols - 3) * CELL, 13 * CELL);
        m.Portal(cols * CELL - 24, 12 * CELL, 24, 2 * CELL, "college_grounds", "from_college_training", "Back to the court", false);

        struct Lane { int cy; const char* who; const char* look; const char* bolt; float every; const char* talk; };
        const Lane lanes[4] = {
            {4,  "Apprentice Bryn",  "apprentice", "bolt_fire",     3.1f, "college_lane_fire_root"},
            {6,  "Adept Corvane",    "adept",      "bolt_eldritch", 2.6f, "college_lane_arcane_root"},
            {8,  "Apprentice Lisse", "apprentice", "bolt_water",    3.4f, "college_lane_water_root"},
            {10, "Adept Marrin",     "adept",      "bolt_air",      2.9f, "college_lane_air_root"},
        };
        int k = 0;
        for (const Lane& l : lanes) {
            const int dummy_x = 4 * CELL, y = l.cy * CELL + 20, caster_x = 14 * CELL;
            piece("training_dummy", dummy_x, y, 18, 8);
            json& who = m.Npc("npc_college_lane_" + std::to_string(k++), l.who, l.look, caster_x, y, l.talk, 1);
            who["casts"] = {{"bolt", l.bolt}, {"at", json::array({dummy_x, y})}, {"every", l.every}};
        }
        // Crystals in the corners, which is why nobody in here runs dry.
        piece("crystal_pylon", 2 * CELL + 8, 3 * CELL + 20, 22, 10);
        piece("crystal_pylon", 2 * CELL + 8, 14 * CELL + 16, 22, 10);
        piece("crystal_pylon", (cols - 2) * CELL - 8, 3 * CELL + 20, 22, 10);
        // Along the back wall: the colours, and the racks the staves are kept in.
        for (int cx : {7, 12, 17, 22}) piece("tapestry_blue", cx * CELL, 2 * CELL + 4, 0, 0);
        piece("weapon_rack", 19 * CELL + 16, 3 * CELL + 4, 40, 12);
        // Benches along the south wall, for whoever is waiting for a lane.
        piece("stone_bench", 7 * CELL, 15 * CELL + 16, 40, 10);
        piece("stone_bench", 11 * CELL, 15 * CELL + 16, 40, 10);
        piece("stone_bench", 15 * CELL, 15 * CELL + 16, 40, 10);
        // A ring on the floor at the east end where two of them can face each other.
        m.Overlay("props", "spell_circle", 20 * CELL, 7 * CELL);
        m.Npc("npc_college_instructor", "Battlemaster Ysolde", "magister", 17 * CELL, 12 * CELL + 8, "college_instructor_root", 1);
        m.Write("maps");
    }

    // ------------------------------------------------- east: the lecture room
    // A board across the back wall, a lectern, the orrery, and rows of desks
    // with a walk up the middle. The door is in the west wall.
    {
        const int cols = 26, rows = 17;
        MapBuilder m("college_classroom", "The Lecture Room", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("The four elements, and what each one fears");
        m.Background(16, 16, 28);
        // The runner in from the door, and the walk up the middle to the board.
        CollegeRoom(m, cols, rows, CELL, 'W', 12, 13, [&](int cx, int cy) {
            if ((cy == 12 || cy == 13) && cx <= cols / 2) return true;
            return (cx == cols / 2 - 1 || cx == cols / 2) && cy >= 5 && cy <= 14;
        });
        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        m.Spawn("entrance", 3 * CELL, 13 * CELL);
        m.Spawn("default",  3 * CELL, 13 * CELL);
        m.Portal(0, 12 * CELL, 24, 2 * CELL, "college_grounds", "from_college_classroom", "Back to the court", false);
        const int mid_x = (cols / 2) * CELL;

        piece("college_blackboard", mid_x, 3 * CELL + 10, 104, 12);
        piece("lectern", mid_x - 70, 5 * CELL + 8, 28, 10);
        piece("college_orrery", mid_x + 150, 4 * CELL + 16, 26, 10);
        for (int cx : {3, 5, 20, 22}) piece("cottage_bookshelf", cx * CELL + 16, 3 * CELL + 4, 40, 14);
        piece("candlestand", 7 * CELL + 16, 4 * CELL, 16, 8);
        piece("candlestand", 18 * CELL + 16, 4 * CELL, 16, 8);
        piece("tapestry_green", 2 * CELL, 2 * CELL + 4, 0, 0);
        piece("tapestry_red", (cols - 2) * CELL, 2 * CELL + 4, 0, 0);

        // Desks: three rows, two either side of the walk, and somebody at most of them.
        struct Seat { int cx, cy; const char* who; const char* look; const char* talk; };
        const Seat seats[] = {
            {6, 7, "Apprentice Oda", "apprentice", "college_pupil_a_root"},   {9, 7, nullptr, nullptr, nullptr},
            {16, 7, "Adept Hale", "adept", "college_pupil_b_root"},           {19, 7, "Apprentice Wick", "apprentice", "college_pupil_c_root"},
            {6, 10, nullptr, nullptr, nullptr},                                {9, 10, "Apprentice Nan", "apprentice", "college_pupil_a_root"},
            {16, 10, nullptr, nullptr, nullptr},                               {19, 10, "Adept Thessaly", "adept", "college_pupil_b_root"},
            {6, 13, "Apprentice Roe", "apprentice", "college_pupil_c_root"},  {9, 13, nullptr, nullptr, nullptr},
            {16, 13, nullptr, nullptr, nullptr},                               {19, 13, nullptr, nullptr, nullptr},
        };
        int k = 0;
        for (const Seat& s : seats) {
            const int x = s.cx * CELL + 16, y = s.cy * CELL + 8;
            // Not the two at the bottom left: that is where the door's carpet runs.
            if (s.cx < cols / 2 && s.cy >= 12) continue;
            piece("college_desk", x, y, 38, 10);
            if (s.who) m.Npc("npc_college_pupil_" + std::to_string(k++), s.who, s.look, x, y + 22, s.talk, 3);
        }
        m.Npc("npc_college_lector", "Lector Maud", "magister", mid_x + 10, 5 * CELL + 14, "college_lector_root", 0);
        m.Write("maps");
    }

    // ------------------------------------------------- north: the great hall
    // Where the council sits. The table in the middle of the floor, six chairs
    // at it, the Magister at its head with the college's books behind him; and
    // south of the table the circle cut in the floor, which is older than the
    // table, the hall, and the college.
    {
        const int cols = 28, rows = 19;
        MapBuilder m("fernhollow_college", "The Great Hall", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("Where the council sits");
        m.Background(16, 14, 24);
        // The runner, from the doors to the table.
        CollegeRoom(m, cols, rows, CELL, 'S', cols / 2 - 1, cols / 2, [&](int cx, int cy) {
            return (cx == cols / 2 - 1 || cx == cols / 2) && cy >= 10;
        });
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "college_grounds", "from_fernhollow_college",
                 "Back to the court", false);
        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        m.Overlay("props", "spell_circle", dx, 13 * CELL + 16);

        // The table, and the six chairs: three behind it facing the room, three
        // before it with their backs to the door -- all six turned to the table.
        const int tx = dx, ty = 8 * CELL + 16;
        piece("council_table", tx, ty, 116, 26);
        // Forty apart and not further: the table is an oval, and a chair out at
        // the end of it has the rim fall away in front of it and the sitter's
        // knees showing.
        const int seat = 40;
        for (int off : {-seat, 0, seat}) {
            piece("high_chair", tx + off, ty - 38, 0, 0);
            piece("high_chair_back", tx + off, ty + 30, 18, 8);
        }
        // The council sits *in* the three behind it, facing the table and the
        // room across it: each a few pixels south of their chair, so they are
        // drawn over its tall back and under the table, which hides them from
        // the chest down -- and that is somebody sitting at a table. They used
        // to stand north of the chairs, and all that showed of a councillor
        // was the top of a head over the back of an empty chair. The row is
        // walled off so nobody walks through a lap.
        const int sit = ty - 34;
        m.Collision(tx - 66, ty - 50, 132, 24);
        // Orrin at the head of it; he is who he always was and keeps what he kept.
        m.Npc("npc_magister", "Magister Orrin", "magister", tx, sit, "magister_root", 0)["shop"] = "fernhollow_college";
        m.Npc("npc_councillor_ferris", "Councillor Ferris", "adept", tx - seat, sit, "councillor_ferris_root", 0);
        m.Npc("npc_councillor_wren", "Councillor Wren", "magister", tx + seat, sit, "councillor_wren_root", 0)["tint"] =
            json::array({226, 214, 255});

        // The library along the back wall, the college's colours between the cases.
        for (int i = 0; i < 6; ++i)
            piece("cottage_bookshelf", (3 + i * 4) * CELL + 16 + (i >= 3 ? 2 * CELL : 0), 3 * CELL + 4, 40, 14);
        piece("tapestry_blue", dx, 2 * CELL + 4, 0, 0);
        for (int side : {-1, 1}) {
            piece("tapestry_blue", dx + side * 4 * CELL, 2 * CELL + 4, 0, 0);
            // A founder either side of the circle: the hall is theirs.
            piece("college_statue", dx + side * 6 * CELL, 16 * CELL, 30, 14);
        }
        for (int side : {-1, 1}) {
            piece("crystal_pylon", dx + side * 5 * CELL, 5 * CELL + 8, 22, 10);
            piece("candlestand", dx + side * 7 * CELL, 8 * CELL + 8, 16, 8);
            piece("candlestand", dx + side * 7 * CELL, 13 * CELL + 8, 16, 8);
            piece("college_banner", dx + side * 3 * CELL, (rows - 2) * CELL - 4, 12, 8);
        }
        piece("lectern",        4 * CELL,           8 * CELL,       28, 10);
        piece("writing_desk",   3 * CELL + 16,      12 * CELL,      40, 14);
        piece("college_orrery", (cols - 4) * CELL,  8 * CELL,       26, 10);
        piece("travel_chest",   (cols - 3) * CELL,  12 * CELL,      28, 12);
        piece("cottage_bookshelf", (cols - 4) * CELL - 8, 15 * CELL, 40, 14);
        m.Write("maps");
    }
}

// --- the dreamworld ----------------------------------------------------------
//
// Where the player goes when they sleep: the Reverie, and it goes down. Three
// depths, a ladder between each, every one of them islands hung over a starry
// void and joined by plank bridges.
//
//   dreamworld      The Reverie           where a sleeper arrives: the candles, the
//                                         traders, shades and dread boars, and the
//                                         brute on the far plateau, who has the
//                                         ladder behind him
//   dreamworld_2    The Deep Reverie      what the middle of the map is afraid of,
//                                         and the Sleepless
//   dreamworld_3    The Dreaming Dark     what the end of it is afraid of, and the
//                                         Unwaking
//
// Each is harder than the one above it and darker, and there is one more dream
// shard in every kill, crystal and chest for each ladder climbed down: the map
// says how deep it is ("dream_depth") and the game does the rest.
//
// Nothing here is the same two nights running. A platform's posts share a pool
// of the depth's monsters and a group, and which of the pool keeps them is
// settled by the day as the map is walked into (World::ResolveSpawn): shades in
// the grove tonight, wolves tomorrow. Only the three that guard something keep
// their posts every night, because a quest that says "the brute" has to be able
// to find him.
//
// It is built from the waking world's own art. Snow reads as cloud once the
// dream's violet light is over it, and frost-rock and cursed ground as the
// storm cloud underneath; the scenery is the forest's toadstools and saplings;
// the nightmares are the waking world's monsters in a bad night's colours. The
// void is not drawn at all: the game paints stars behind where there is no
// ground, and it is solid, so nobody walks off an edge.

struct DreamIsle { float cx, cy, rx, ry; };

// The islands of one depth and the bridges between them, and the questions
// every builder asks of them.
struct DreamField {
    int W = 0, H = 0, CELL = 32;
    vector<DreamIsle> isles;
    vector<std::pair<int, int>> bridges;
    int wobble_seed = 91;

    // How far inside an island a cell is: below 1 is ground. The rim wobbles,
    // so no island is a perfect ellipse.
    float Depth(int i, float cx, float cy) const {
        const float dx = (cx - isles[i].cx) / isles[i].rx;
        const float dy = (cy - isles[i].cy) / isles[i].ry;
        const float wobble = (Fbm(cx * 0.35f, cy * 0.35f, wobble_seed + i) - 0.5f) * 0.55f;
        return (dx * dx + dy * dy) / (1.0f + wobble);
    }
    int Which(int cx, int cy) const {
        for (size_t i = 0; i < isles.size(); ++i)
            if (Depth(static_cast<int>(i), cx + 0.5f, cy + 0.5f) < 1.0f) return static_cast<int>(i);
        return -1;
    }
    // Distance, in cells, from a point to the nearest bridge's centre line.
    float BridgeDist(float cx, float cy) const {
        float best = 1.0e9f;
        for (const auto& b : bridges) {
            const float ax = isles[b.first].cx, ay = isles[b.first].cy;
            const float vx = isles[b.second].cx - ax, vy = isles[b.second].cy - ay;
            const float t = std::clamp(((cx - ax) * vx + (cy - ay) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
            const float px = ax + vx * t - cx, py = ay + vy * t - cy;
            best = std::min(best, sqrtf(px * px + py * py));
        }
        return best;
    }
    bool OnBridge(int cx, int cy) const { return BridgeDist(cx + 0.5f, cy + 0.5f) < 1.05f; }
    bool NearBridge(int cx, int cy, float cells) const { return BridgeDist(cx + 0.5f, cy + 0.5f) < cells; }
    // A cell back from every rim, so nothing stood there hangs over the void.
    bool DeepInside(int cx, int cy) const {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (Which(cx + dx, cy + dy) < 0) return false;
        return true;
    }
    int Px(float cells) const { return static_cast<int>(cells * CELL); }

    // Cloud where there is island, planks where there is bridge, and nothing
    // -- solid nothing -- everywhere else. `rim`, if it is not empty, is what
    // the outermost ring of an island is made of: in the dark of the third
    // depth an edge has to be paler than what it is the edge of to be seen.
    void Lay(MapBuilder& m, const string& cloud, const string& patch, const string& rim, const string& planks) const {
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                if (Which(cx, cy) >= 0) {
                    string tile = cloud;
                    if (!rim.empty() && !DeepInside(cx, cy)) tile = rim;
                    else if (!patch.empty() && Fbm(cx * 0.3f, cy * 0.3f, wobble_seed + 40) > 0.68f) tile = patch;
                    m.Ground(VariantOf(tile, cx, cy), cx * CELL, cy * CELL, CELL);
                } else if (OnBridge(cx, cy)) {
                    m.Ground(VariantOf(planks, cx, cy), cx * CELL, cy * CELL, CELL);
                } else {
                    m.Collision(cx * CELL, cy * CELL, CELL, CELL);
                }
            }
    }

    // Toadstools, saplings and bushes, kept off the bridges, off the middle of
    // every island -- where the fighting is -- and back from the rims; and the
    // dream's two herbs, starlily on the islands named and moonpetal on the rest.
    void Scatter(MapBuilder& m, std::mt19937& rng, int& herb_i, const std::set<int>& bare,
                 const std::set<int>& starlily, int salt) const {
        for (int cy = 1; cy < H - 1; ++cy)
            for (int cx = 1; cx < W - 1; ++cx) {
                const int isle = Which(cx, cy);
                if (isle < 0 || !DeepInside(cx, cy) || NearBridge(cx, cy, 2.6f)) continue;
                const float to_centre = std::hypot(cx + 0.5f - isles[isle].cx, cy + 0.5f - isles[isle].cy);
                if (to_centre < (bare.count(isle) ? 5.5f : 3.0f)) continue;
                const float r = Hash2(cx, cy, 313 + salt);
                const int x = cx * CELL + 16, y = cy * CELL + 26;
                const float hr = Hash2(cx, cy, 515 + salt);
                if (r >= 0.20f && starlily.count(isle) && hr < 0.22f) { PlaceHerb(m, "starlily", x, y, herb_i); continue; }
                if (r >= 0.20f && !starlily.count(isle) && !bare.count(isle) && hr < 0.12f) {
                    PlaceHerb(m, "moonpetal", x, y, herb_i);
                    continue;
                }
                if (r < 0.09f) {
                    m.Prop("objects", Pick(kFungus, rng), x, y);
                    m.Collision(x - 8, y - 6, 16, 6);
                } else if (r < 0.14f) {
                    m.Prop("objects", Pick(kSmallTrees, rng), x, y);
                    m.Collision(x - 8, y - 8, 16, 8);
                } else if (r < 0.20f) {
                    m.Prop("objects", Pick(kSmallBushes, rng), x, y);
                }
            }
    }
};

// A dream crystal: shards to mine. It gives out like a seam, and grows back
// quickly -- a dream only lasts the night. Deeper ones ask more of the miner,
// and the depth itself puts more shards in each.
static void PlaceDreamCrystal(MapBuilder& m, std::mt19937& rng, const string& id, int x, int y, int mining, int xp) {
    json& o = m.Object(id, "rock", x, y);
    o["sprite"]      = ObjPath(Pick(kRocks, rng));
    o["skill"]       = "Mining";
    o["skill_level"] = mining;
    o["yield"]       = "dream_shard";
    o["yield_xp"]    = xp;
    o["gather_time"] = 3.0f;
    o["title"]       = "dream crystal";
    o["deplete"]     = 0.25f;
    o["regrow"]      = 0.5f;
    m.Collision(x - 14, y - 12, 28, 12);
}

// The way down: a hole through the cloud with the top of a ladder standing out
// of it. (x, y) is the near lip of the hole. Whoever comes up it arrives just
// south of that, as `arrive`.
static void PlaceLadderDown(MapBuilder& m, int x, int y, const string& to, const string& label, int advised,
                            const string& arrive) {
    m.Prop("props", "dream_ladder_down", x, y);
    m.Collision(x - 24, y - 42, 48, 30);
    m.Portal(x - 34, y - 56, 68, 66, to, "from_above", label, true);
    m.Danger(advised);
    m.Spawn(arrive, x, y + 26);
}

// And the other end of it: a ladder standing on the cloud and climbing out of
// sight. Whoever comes down it arrives just south of its foot.
static void PlaceLadderUp(MapBuilder& m, int x, int y, const string& to, const string& label) {
    m.Prop("props", "dream_ladder_up", x, y);
    m.Collision(x - 14, y - 10, 28, 10);
    m.Portal(x - 26, y - 40, 52, 52, to, "from_below", label, true);
    m.Spawn("from_above", x, y + 28);
    m.Spawn("default", x, y + 28);
}

// A waking stone: the dream can be left from any depth of it.
static void PlaceWakingStone(MapBuilder& m, const string& id, int x, int y) {
    json& o = m.Object(id, "dream_wake", x, y);
    o["sprite"] = ObjPath("rock_02");
    o["title"]  = "Waking stone";
    m.Collision(x - 14, y - 10, 28, 10);
}

// =============================================================================
//  Havenbrook, dreaming
//
//  Through the mirror at the bottom of the Dreaming Dark: Havenbrook exactly as
//  it stands -- the same streets, the same roofs, the well in the square -- as a
//  nightmare has it. Nobody lives in it. Nothing in it opens, sells or answers;
//  the doors are only the pictures of doors, and past the gates there is only
//  the dark. What walks its streets are the orcs and the dead, dreamt fifty
//  levels strong and more, and the bosses of the waking world: some in their
//  places every night, and at least one walking the town, a different one on a
//  different road each night.
//
//  Built from the town itself, as it was built: a copy of the finished map with
//  its people, its doors and its beasts taken out and its dream put in, so the
//  one can never drift from the other.
// =============================================================================
static void BuildDreamHavenbrook(const MapBuilder& town) {
    const int CELL = 32;
    MapBuilder m = town;
    m.Rename("dream_havenbrook", "Havenbrook, Dreaming");
    json& dq = m.dq;
    dq["ambient"]  = "dream";
    dq["subtitle"] = "The town as a nightmare has it";
    dq["dream_depth"] = 4;
    dq["background"] = json::array({12, 8, 22, 255});
    m.Fog(0.30f, 0.0f, {150, 118, 214});

    // Nobody lives here.
    dq["npcs"] = json::array();
    // The ways out are shut -- the gates open on the dark -- and the doors are
    // pictures of doors.
    for (const auto& p : dq["portals"])
        if (!p.value("interact", true)) dq["collision"].push_back(p["rect"]);
    dq["portals"] = json::array();
    // Everything that was something to use is only something to look at.
    json kept = json::array();
    for (const auto& o : dq["objects"]) {
        if (!o.contains("sprite")) continue;
        const string type = o.value("type", string(""));
        if (type == "lamp" || type == "glass_light") { kept.push_back(o); continue; }
        json d;
        d["id"]     = "dream_" + o["id"].get<string>();
        d["type"]   = "decor";
        d["x"]      = o["x"];
        d["y"]      = o["y"];
        d["sprite"] = o["sprite"];
        if (o.contains("title")) d["title"] = o["title"];
        kept.push_back(d);
    }
    dq["objects"] = kept;
    dq["enemies"] = json::array();
    dq["spawns"]  = json::object();

    // --- the mirror, in the square -----------------------------------------------------------
    int mx = 21 * CELL, my = 28 * CELL;
    for (int r = 0; r < 12 && !(m.Clear(mx, my) && m.Clear(mx, my + 40)); ++r) mx += 16;
    m.Prop("props", "dream_mirror", mx, my);
    m.Collision(mx - 40, my - 14, 80, 14);
    m.Portal(mx - 34, my - 30, 68, 40, "dreamworld_3", "from_havenbrook", "Step back through the mirror", true);
    m.Spawn("from_mirror", mx, my + 40);
    m.Spawn("default", mx, my + 40);
    PlaceWakingStone(m, "dream_waking_stone_havenbrook", mx - 90, my + 30);
    {
        json& o = m.Object("dream_voice_havenbrook", "sign", mx + 90, my + 30);
        o["sprite"] = ObjPath("rocksmall_02");
        o["title"]  = "A voice in the dream";
        o["text"]   = "Havenbrook. You know the square. You know the well.\n\n"
                      "Nobody is here, and everything that is here should not be: the orcs from under "
                      "Emberfell, and the dead from under Hollowrest, and them -- the ones you have killed "
                      "and killed again, standing about the town as though they lived in it.\n\n"
                      "They count, here, the same as awake. And you cannot die in a dream.";
        m.Collision(mx + 80, my + 22, 20, 8);
    }

    // --- the orcs and the dead ---------------------------------------------------------------
    // A lattice over the streets and yards, each post the orcs' or the dead's
    // by the block it stands in, every one of them shown at fifty or more.
    const vector<string> orcs = {"dream_grunt", "dream_slinger", "dream_bowman", "dream_raider"};
    const vector<string> dead = {"dream_shambler", "dream_ghoul", "dream_knight", "dream_skeleton", "dream_wraith"};
    const int W = m.Width() / CELL, H = m.Height() / CELL;
    int posts = 0;
    // A street's worth of them to a street, not a crowd: every sixth cell
    // or so, and not every one of those.
    for (int gy = 4; gy < H - 3; gy += 6)
        for (int gx = 4; gx < W - 3; gx += 6) {
            const int cx = gx + static_cast<int>(Hash2(gx, gy, 9191) * 3.0f) - 1;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, 9192) * 3.0f) - 1;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            if (Hash2(cx, cy, 9195) > 0.62f) continue;
            if (!m.Clear(x, y) || !m.Clear(x - 14, y) || !m.Clear(x + 14, y)) continue;
            if (std::hypot(static_cast<float>(x - mx), static_cast<float>(y - my)) < 300.0f) continue;
            const int block = (cx / 12) * 7 + (cy / 10);
            const bool orc = Hash2(block, 3, 9193) < 0.5f;
            const string group = string(orc ? "orcs_" : "dead_") + std::to_string(block);
            m.EnemyPool(orc ? orcs : dead, group, x, y, 1, 2, 60.0f, 240.0f);
            m.dq["enemies"].back()["shown"] = 50 + static_cast<int>(Hash2(cx, cy, 9194) * 8.0f);
            ++posts;
        }

    // --- the bosses in their places ---------------------------------------------------------------
    // The Warchief in the square, the Wight on the guild hall's steps, the
    // Broodmother in the farmyard: every night, and every one of them a kill
    // that counts toward its totem.
    const auto boss = [&](const string& type, int cx, int cy, int shown) {
        int x = cx * CELL + 16, y = cy * CELL + 16;
        for (int r = 0; r < 10 && !m.Clear(x, y); ++r) y += 16;
        m.Enemy(type, x, y, 1, 0.0f, 300.0f);
        m.dq["enemies"].back()["shown"] = shown;
    };
    boss("orc3", 36, 29, 60);
    boss("barrow_wight", 28, 17, 58);
    boss("broodmother", 61, 17, 56);

    // --- and at least one walking the town --------------------------------------------------------
    std::printf("  dream_havenbrook: %d posts of orcs and the dead\n", posts);
    PlaceRoamers(m);
    m.Write("maps");
}

// The posts on one island: `spots` are cells from its middle, all in one group,
// so whichever of the pool comes tonight comes as a pack.
static void DreamPack(MapBuilder& m, const DreamField& f, int isle, const string& group,
                      const vector<string>& pool, const vector<std::pair<float, float>>& spots,
                      int level, int spread, float leash = 200.0f) {
    const DreamIsle& i = f.isles[isle];
    for (const auto& sp : spots)
        m.EnemyPool(pool, group, f.Px(i.cx + sp.first), f.Px(i.cy + sp.second), level, spread, 40.0f, leash);
}

static const vector<string> kDreamFirst  = {"nightmare_shade", "dread_boar", "gloom_spider", "pale_stag", "dusk_wolf"};
static const vector<string> kDreamSecond = {"dream_wolf", "dream_lizardman", "dream_wraith", "dream_bat", "dream_skeleton"};
static const vector<string> kDreamThird  = {"dream_bear", "dream_hound", "dream_demon", "dream_banshee", "dream_wyvern",
                                            "dream_ankou"};

static void BuildDreamworld() {
    DreamField f;
    f.W = 72; f.H = 56;
    const int CELL = f.CELL, W = f.W, H = f.H;
    MapBuilder m("dreamworld", "The Reverie", W * CELL, H * CELL);
    m.Ambient("dream");
    m.DreamDepth(1);
    m.Subtitle("Where the Hollowmarch goes when it sleeps");
    m.Background(14, 10, 30);
    std::mt19937 rng(9191u);

    f.isles = {
        {36.0f, 28.0f, 7.0f, 6.0f},    // 0: arrival
        {36.0f,  9.0f, 9.0f, 5.0f},    // 1: the grove
        {60.0f, 28.0f, 8.0f, 7.0f},    // 2: the crystal field
        {12.0f, 28.0f, 8.0f, 8.0f},    // 3: the meadow
        {36.0f, 47.0f, 9.0f, 6.0f},    // 4: the brute's plateau
        // Further out, and only reached across two bridges: a night's worth of
        // somewhere else to go.
        {57.0f,  9.0f, 5.0f, 4.0f},    // 5: the north-east shelf, and its crystals
        {14.0f,  9.0f, 5.0f, 4.0f},    // 6: the north-west shelf
        {13.0f, 46.0f, 5.0f, 4.0f},    // 7: the south-west shelf
        {59.0f, 47.0f, 5.0f, 4.0f},    // 8: the ladder, behind the brute
    };
    // The first four are the ones there always were, out from where you arrive.
    // The rest make a ring of it -- and one spur, which only the brute's plateau
    // leads to.
    f.bridges = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 5}, {2, 5}, {1, 6}, {3, 6}, {3, 7}, {4, 7}, {4, 8}};
    f.Lay(m, "snow", "", "", "plank_floor");

    auto px = [&](float cells) { return f.Px(cells); };
    const auto& isles = f.isles;
    const int ax = px(isles[0].cx + 0.5f), ay = px(isles[0].cy + 1.0f);
    m.Spawn("arrival", ax, ay);
    m.Spawn("default", ax, ay);

    // --- the arrival island: candles, the waking stone, a word of advice ------
    // Eight, turned so none of them stands on the line of a bridge: a candle
    // due north of the spawn used to stop anyone walking out that way.
    for (int i = 0; i < 8; ++i) {
        const float a = (i + 0.5f) / 8.0f * 6.2831853f;
        const int cx = ax + static_cast<int>(cosf(a) * 110.0f);
        const int cy = ay - 10 + static_cast<int>(sinf(a) * 72.0f);
        m.Prop("props", "candlestand", cx, cy);
        m.Collision(cx - 5, cy - 5, 10, 5);
    }
    PlaceWakingStone(m, "dream_waking_stone", ax - 64, ay - 56);
    {
        json& o = m.Object("dream_voice", "sign", ax + 64, ay - 56);
        o["sprite"] = ObjPath("rocksmall_02");
        o["title"]  = "A voice in the dream";
        o["text"]   = "You are asleep, and this is the Reverie.\n\n"
                      "It lasts as long as the night does. When dawn comes you will wake "
                      "where you lay down, rested. If you would rather wake sooner, touch "
                      "the waking stone.\n\n"
                      "Nothing that happens here can kill you. A nightmare that bests you "
                      "only throws you awake -- but it takes the rest of the night with it.\n\n"
                      "The shards the nightmares leave behind are real. They come back with you.\n\n"
                      "It is never the same dream twice. And it goes down: past the brute, "
                      "there is a ladder.";
        m.Collision(ax + 64 - 10, ay - 56 - 8, 20, 8);
    }

    // Two traders the dream keeps, either side of the candles and clear of the
    // east and west bridges: a market for the sleeper's needs, and a collector
    // of the things only dreams leave behind.
    m.Npc("npc_night_pedlar", "The Night Pedlar", "citizen1", ax - 150, ay + 40, "night_pedlar_root", 0)["shop"] = "reverie_general";
    m.Npc("npc_collector", "The Collector", "fighter2", ax + 150, ay + 40, "collector_root", 0)["shop"] = "reverie_curios";
    // And the table the dream keeps by its candles, south-east of them and
    // clear of the bridges, for the charms whose scrolls are only sold here.
    PlaceEnchantingTable(m, "altar_reverie", ax + 90, ay + 84);

    // A slate the dream writes its own requests on: the Reverie's board, where
    // its daily quests are posted, off to the north-east of the candles and
    // clear of every bridge.
    {
        json& o = m.Object("board_reverie", "board", ax + 130, ay - 100);
        o["sprite"] = ObjPath("guild_noticeboard");
        o["title"]  = "The Dreamer's Slate";
        o["quests"] = json::array();
        m.Collision(ax + 130 - 36, ay - 100 - 12, 72, 12);
    }

    // A cauldron by the candles: dream herbs are best brewed where they grow.
    PlaceCauldron(m, "cauldron_reverie", ax - 170, ay - 70);

    // --- scenery ----------------------------------------------------------------
    int herb_i = 0;
    f.Scatter(m, rng, herb_i, {0, 8}, {2}, 0);

    // --- the crystal field: dream shards to mine ---------------------------------
    {
        const DreamIsle& e = isles[2];
        const float spots[][2] = {{-4, -3}, {3, -4}, {5, 1}, {-2, 3}, {2, 4}, {-5, 1}};
        int n = 0;
        for (const auto& sp : spots)
            PlaceDreamCrystal(m, rng, "dream_crystal_" + std::to_string(n++), px(e.cx + sp[0]), px(e.cy + sp[1]), 1, 40);
        DreamPack(m, f, 2, "field", {"nightmare_shade", "pale_stag", "gloom_spider"}, {{1, -1}, {-1, 5}}, 5, 0);
    }

    // --- the grove and the meadow --------------------------------------------------
    // Shades in the grove and boars in the meadow was every night. It is some
    // nights now.
    {
        const DreamIsle& n = isles[1];
        const float spots[][2] = {{-5, -1}, {-1, -2}, {3, -1}, {6, 1}};
        int lv = 3;
        for (const auto& sp : spots)
            m.EnemyPool({"nightmare_shade", "gloom_spider", "dusk_wolf"}, "grove", px(n.cx + sp[0]), px(n.cy + sp[1]),
                        lv++, 0, 40.0f, 200.0f);
    }
    {
        const DreamIsle& w = isles[3];
        const float spots[][2] = {{-4, -4}, {3, -3}, {-3, 4}, {4, 3}};
        int i = 0;
        for (const auto& sp : spots)
            m.EnemyPool({"dread_boar", "pale_stag", "dusk_wolf"}, "meadow", px(w.cx + sp[0]), px(w.cy + sp[1]),
                        3 + (i++ % 2), 0, 40.0f, 200.0f);
    }

    // --- the shelves ---------------------------------------------------------------------
    DreamPack(m, f, 5, "north_east", kDreamFirst, {{-2, 0}, {2, 1}}, 5, 2, 160.0f);
    PlaceDreamCrystal(m, rng, "dream_crystal_ne_0", px(isles[5].cx - 3), px(isles[5].cy - 1.5f), 1, 40);
    PlaceDreamCrystal(m, rng, "dream_crystal_ne_1", px(isles[5].cx + 3), px(isles[5].cy - 1.5f), 1, 40);
    DreamPack(m, f, 6, "north_west", kDreamFirst, {{-2, -1}, {2, 0}, {0, 2}}, 4, 2, 160.0f);
    DreamPack(m, f, 7, "south_west", kDreamFirst, {{-2, 0}, {2, -1}, {0, 2}}, 5, 2, 160.0f);

    // --- the brute's plateau ---------------------------------------------------------
    {
        const DreamIsle& s = isles[4];
        // Himself, every night: two quests send people to him by name.
        m.Enemy("nightmare_brute", px(s.cx + 1), px(s.cy + 2), 10, 90.0f, 220.0f);
        m.EnemyPool({"nightmare_shade", "dusk_wolf"}, "plateau", px(s.cx - 5), px(s.cy), 6, 0, 40.0f, 200.0f);
        m.EnemyPool({"nightmare_shade", "dusk_wolf"}, "plateau", px(s.cx + 6), px(s.cy - 1), 6, 0, 40.0f, 200.0f);
        PlaceChest(m, "chest_dream", px(s.cx + 1), px(s.cy + 4), "chest_dream");
        // Demonite: black glass with a red heat inside, found nowhere but here.
        const float seams[][2] = {{-4, 3}, {5, 3}, {-1, -3}};
        int k = 0;
        for (const auto& sp : seams)
            PlaceRock(m, rng, 950 + k++, px(s.cx + sp[0]), px(s.cy + sp[1]), true, 80, "demonite_ore");
    }

    // --- the ladder ------------------------------------------------------------------------
    {
        const DreamIsle& l = isles[8];
        // East of the middle: the bridge comes in from the west, to the middle.
        const int lx = px(l.cx + 1.5f), ly = px(l.cy + 0.5f);
        PlaceLadderDown(m, lx, ly, "dreamworld_2", "Climb down, deeper into the dream", 25, "from_below");
        json& o = m.Object("dream_voice_ladder", "sign", lx - 96, ly - 50);
        o["sprite"] = ObjPath("rocksmall_02");
        o["title"]  = "A voice in the dream";
        o["text"]   = "The dream is deeper than this.\n\n"
                      "What is down the ladder is not what is up here: it is what you were afraid of "
                      "later, when you were older. Dawn finds you there as it finds you here, and so "
                      "does the waking stone.\n\n"
                      "There is more of the dream the further down you go. Everything that leaves a "
                      "shard leaves one more for each ladder you have climbed down.";
        m.Collision(lx - 96 - 10, ly - 50 - 8, 20, 8);
    }

    m.Write("maps");
}

// The second depth. Eleven islands and sixteen bridges, and nothing on them
// that the first depth would recognise.
static void BuildDreamDeep() {
    DreamField f;
    f.W = 84; f.H = 64;
    f.wobble_seed = 191;
    const int CELL = f.CELL;
    MapBuilder m("dreamworld_2", "The Deep Reverie", f.W * CELL, f.H * CELL);
    m.Ambient("dream");
    m.DreamDepth(2);
    m.Subtitle("Further down than sleep usually goes");
    m.Background(10, 7, 24);
    std::mt19937 rng(9292u);

    f.isles = {
        {42.0f,  8.0f,  6.0f, 5.0f},   // 0: the foot of the ladder
        {22.0f, 14.0f,  7.0f, 6.0f},   // 1: the west shelf
        {62.0f, 14.0f,  7.0f, 6.0f},   // 2: the east shelf
        {42.0f, 26.0f,  8.0f, 6.0f},   // 3: the crossing
        {13.0f, 32.0f,  7.0f, 7.0f},   // 4: the far west, and its crystals
        {71.0f, 32.0f,  7.0f, 7.0f},   // 5: the far east, and its starlilies
        {26.0f, 44.0f,  7.0f, 6.0f},   // 6: south-west
        {58.0f, 44.0f,  7.0f, 6.0f},   // 7: south-east
        {42.0f, 54.0f, 10.0f, 6.0f},   // 8: the Sleepless's plateau
        {72.0f, 56.0f,  5.0f, 4.0f},   // 9: the ladder, behind it
        { 8.0f, 53.0f,  5.0f, 4.0f},   // 10: a shelf at the end of two long bridges, for whoever goes looking
    };
    f.bridges = {{0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3}, {1, 4}, {2, 5}, {3, 6}, {3, 7}, {4, 6}, {5, 7},
                 {6, 8}, {7, 8}, {8, 9}, {4, 10}, {6, 10}};
    // One kind of cloud, as above: a patch of another reads as a square cut out
    // of the dream, not as weather.
    f.Lay(m, "frost_rock", "", "", "plank_floor_dark");

    auto px = [&](float cells) { return f.Px(cells); };
    const auto& isles = f.isles;

    // --- the foot of the ladder ---------------------------------------------------------
    const int lx = px(isles[0].cx + 0.5f), ly = px(isles[0].cy - 0.5f);
    PlaceLadderUp(m, lx, ly, "dreamworld", "Climb up, toward waking");
    PlaceWakingStone(m, "dream_waking_stone_2", lx - 80, ly + 30);
    for (int side = -1; side <= 1; side += 2) {
        m.Prop("props", "candlestand", lx + side * 46, ly + 8);
        m.Collision(lx + side * 46 - 5, ly + 3, 10, 5);
    }
    {
        json& o = m.Object("dream_voice_2", "sign", lx + 80, ly + 30);
        o["sprite"] = ObjPath("rocksmall_02");
        o["title"]  = "A voice in the dream";
        o["text"]   = "The Deep Reverie.\n\n"
                      "Wolves that were never whelped, things that walk in their sleep, the terrors "
                      "that come at three in the morning. They are not the same from one night to the "
                      "next, and neither is where they stand.\n\n"
                      "One of them is always here. It does not sleep, and it has the next ladder "
                      "behind it, on the far side of the southern plateau.";
        m.Collision(lx + 80 - 10, ly + 30 - 8, 20, 8);
    }

    int herb_i = 0;
    f.Scatter(m, rng, herb_i, {0, 9}, {5}, 200);

    // --- who is here tonight ---------------------------------------------------------------
    DreamPack(m, f, 1, "west_shelf", kDreamSecond, {{-3, -1}, {2, -2}, {0, 3}}, 1, 3);
    DreamPack(m, f, 2, "east_shelf", kDreamSecond, {{3, -1}, {-2, -2}, {0, 3}}, 1, 3);
    DreamPack(m, f, 3, "crossing", kDreamSecond, {{-4, -1}, {4, -1}, {-2, 3}, {3, 3}}, 2, 3);
    DreamPack(m, f, 4, "far_west", kDreamSecond, {{-2, -3}, {3, 0}, {-1, 4}}, 2, 3);
    DreamPack(m, f, 5, "far_east", kDreamSecond, {{2, -3}, {-3, 0}, {1, 4}}, 2, 3);
    DreamPack(m, f, 6, "south_west", kDreamSecond, {{-3, 0}, {2, -2}, {1, 3}}, 3, 3);
    DreamPack(m, f, 7, "south_east", kDreamSecond, {{3, 0}, {-2, -2}, {-1, 3}}, 3, 3);
    DreamPack(m, f, 10, "end_shelf", kDreamSecond, {{-1, -1}, {2, 1}}, 4, 2, 150.0f);

    // --- crystals: the far west, and more of them at the end of the long bridges ----
    {
        const float spots[][2] = {{-4, -1}, {-1, -5}, {4, -3}, {4, 3}, {-3, 4}};
        int n = 0;
        for (const auto& sp : spots)
            PlaceDreamCrystal(m, rng, "deep_crystal_" + std::to_string(n++), px(isles[4].cx + sp[0]),
                              px(isles[4].cy + sp[1]), 20, 70);
        const float far[][2] = {{-3, 0}, {0, -2.5f}, {3, -0.5f}};
        for (const auto& sp : far)
            PlaceDreamCrystal(m, rng, "deep_crystal_" + std::to_string(n++), px(isles[10].cx + sp[0]),
                              px(isles[10].cy + sp[1]), 20, 70);
    }

    // --- the Sleepless ----------------------------------------------------------------------------
    {
        const DreamIsle& s = isles[8];
        m.Enemy("nightmare_troll", px(s.cx), px(s.cy + 1), 1, 120.0f, 260.0f);
        m.EnemyPool(kDreamSecond, "plateau", px(s.cx - 6), px(s.cy - 1), 4, 2, 40.0f, 200.0f);
        m.EnemyPool(kDreamSecond, "plateau", px(s.cx + 6), px(s.cy - 1), 4, 2, 40.0f, 200.0f);
        PlaceChest(m, "chest_dream_deep", px(s.cx), px(s.cy + 4), "chest_dream_deep");
        const float seams[][2] = {{-6, 3}, {6, 3}};
        int k = 0;
        for (const auto& sp : seams)
            PlaceRock(m, rng, 960 + k++, px(s.cx + sp[0]), px(s.cy + sp[1]), true, 80, "demonite_ore");
    }

    // --- and the ladder behind it ---------------------------------------------------------------
    PlaceLadderDown(m, px(isles[9].cx + 1.5f), px(isles[9].cy + 0.5f), "dreamworld_3",
                    "Climb down, to the bottom of the dream", 50, "from_below");

    m.Write("maps");
}

// The third, and the last: thirteen islands in the dark, and at the far end of
// them the thing the dream is about.
static void BuildDreamDark() {
    DreamField f;
    f.W = 92; f.H = 72;
    f.wobble_seed = 291;
    const int CELL = f.CELL;
    MapBuilder m("dreamworld_3", "The Dreaming Dark", f.W * CELL, f.H * CELL);
    m.Ambient("dream");
    m.DreamDepth(3);
    m.Subtitle("Where the nightmares come up from");
    m.Background(7, 4, 18);
    std::mt19937 rng(9393u);

    f.isles = {
        {46.0f,  8.0f,  6.0f, 5.0f},   // 0: the foot of the ladder
        {26.0f, 12.0f,  6.0f, 5.0f},   // 1
        {66.0f, 12.0f,  6.0f, 5.0f},   // 2
        {11.0f, 25.0f,  7.0f, 6.0f},   // 3
        {35.0f, 25.0f,  7.0f, 5.0f},   // 4
        {58.0f, 26.0f,  7.0f, 5.0f},   // 5
        {81.0f, 25.0f,  7.0f, 6.0f},   // 6
        {22.0f, 41.0f,  8.0f, 6.0f},   // 7
        {46.0f, 41.0f,  7.0f, 6.0f},   // 8
        {70.0f, 42.0f,  8.0f, 6.0f},   // 9
        { 9.0f, 57.0f,  6.0f, 5.0f},   // 10: crystals, down a dead end
        {83.0f, 58.0f,  6.0f, 5.0f},   // 11: starlilies, down another
        {46.0f, 60.0f, 11.0f, 7.0f},   // 12: the Unwaking's plateau
    };
    f.bridges = {{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 4}, {2, 5}, {2, 6}, {4, 5}, {3, 7}, {4, 8}, {5, 8}, {5, 9},
                 {6, 9}, {7, 8}, {8, 9}, {7, 10}, {9, 11}, {7, 12}, {8, 12}, {9, 12}};
    f.Lay(m, "cursed_ground", "crag", "frost_rock", "plank_floor_dark");

    auto px = [&](float cells) { return f.Px(cells); };
    const auto& isles = f.isles;

    const int lx = px(isles[0].cx + 0.5f), ly = px(isles[0].cy - 0.5f);
    PlaceLadderUp(m, lx, ly, "dreamworld_2", "Climb up, toward waking");
    PlaceWakingStone(m, "dream_waking_stone_3", lx - 80, ly + 30);
    for (int side = -1; side <= 1; side += 2) {
        m.Prop("props", "candlestand", lx + side * 46, ly + 8);
        m.Collision(lx + side * 46 - 5, ly + 3, 10, 5);
    }
    {
        json& o = m.Object("dream_voice_3", "sign", lx + 80, ly + 30);
        o["sprite"] = ObjPath("rocksmall_02");
        o["title"]  = "A voice in the dream";
        o["text"]   = "The Dreaming Dark. There is nothing under this.\n\n"
                      "Everything here was somebody's worst night: the bear that was in the room, the "
                      "hound on the road behind you, the wings. They change. What is at the far end "
                      "does not: it has never once woken, and it is what the rest of them are dreams of.\n\n"
                      "You cannot die here either. It only feels as though you could.";
        m.Collision(lx + 80 - 10, ly + 30 - 8, 20, 8);
    }

    int herb_i = 0;
    f.Scatter(m, rng, herb_i, {0}, {11}, 400);

    DreamPack(m, f, 1, "one", kDreamThird, {{-2, -1}, {2, 1}}, 1, 2);
    DreamPack(m, f, 2, "two", kDreamThird, {{2, -1}, {-2, 1}}, 1, 2);
    DreamPack(m, f, 3, "three", kDreamThird, {{-3, -1}, {2, -2}, {0, 3}}, 1, 3);
    DreamPack(m, f, 4, "four", kDreamThird, {{-3, 0}, {3, -1}, {0, 2.5f}}, 1, 3);
    DreamPack(m, f, 5, "five", kDreamThird, {{3, 0}, {-3, -1}, {0, 2.5f}}, 2, 3);
    DreamPack(m, f, 6, "six", kDreamThird, {{3, -1}, {-2, -2}, {0, 3}}, 2, 3);
    DreamPack(m, f, 7, "seven", kDreamThird, {{-4, 0}, {3, -2}, {1, 3}}, 2, 3);
    DreamPack(m, f, 8, "eight", kDreamThird, {{-3, -2}, {3, -2}, {0, 3}}, 3, 3);
    DreamPack(m, f, 9, "nine", kDreamThird, {{4, 0}, {-3, -2}, {-1, 3}}, 3, 3);
    DreamPack(m, f, 10, "ten", kDreamThird, {{-2, -2}, {2, 1}}, 4, 2, 160.0f);
    DreamPack(m, f, 11, "eleven", kDreamThird, {{2, -2}, {-2, 1}}, 4, 2, 160.0f);

    {
        const float spots[][2] = {{-3, 0}, {0, -3}, {3, -1}, {-1, 3}, {3, 2.5f}};
        int n = 0;
        for (const auto& sp : spots)
            PlaceDreamCrystal(m, rng, "dark_crystal_" + std::to_string(n++), px(isles[10].cx + sp[0]),
                              px(isles[10].cy + sp[1]), 45, 110);
    }

    // --- the Unwaking -----------------------------------------------------------------------------
    {
        const DreamIsle& s = isles[12];
        m.Enemy("nightmare_dragon", px(s.cx), px(s.cy + 1), 1, 180.0f, 300.0f);
        m.EnemyPool(kDreamThird, "plateau", px(s.cx - 7), px(s.cy - 2), 4, 2, 40.0f, 200.0f);
        m.EnemyPool(kDreamThird, "plateau", px(s.cx + 7), px(s.cy - 2), 4, 2, 40.0f, 200.0f);
        PlaceChest(m, "chest_dream_dark", px(s.cx), px(s.cy + 5), "chest_dream_dark");
        const float seams[][2] = {{-7, 3}, {7, 3}, {-4, 5}, {4, 5}};
        int k = 0;
        for (const auto& sp : seams)
            PlaceRock(m, rng, 970 + k++, px(s.cx + sp[0]), px(s.cy + sp[1]), true, 80, "demonite_ore");
    }

    // --- the mirror -----------------------------------------------------------------------
    // Down the dead end in the east, where the starlilies grow: a standing
    // mirror, and in it Havenbrook -- not as it is.
    {
        const DreamIsle& s = isles[11];
        const int mx = px(s.cx), my = px(s.cy - 2.0f);
        m.Prop("props", "dream_mirror", mx, my);
        m.Collision(mx - 40, my - 14, 80, 14);
        m.Portal(mx - 34, my - 30, 68, 40, "dream_havenbrook", "from_mirror", "Step through the mirror", true);
        m.Spawn("from_havenbrook", mx, my + 40);
    }
    // --- and something walking the bridges ------------------------------------------------------
    {
        vector<std::array<int, 2>> loop;
        for (int i : {1, 3, 7, 8, 9, 6, 2, 5, 4})
            loop.push_back({static_cast<int>(isles[i].cx), static_cast<int>(isles[i].cy)});
        RoamOn(m, {"nightmare_troll", "barrow_wight", "vampire_lord"}, 62, 2, 0.7f, loop, CELL, 0, true);
    }
    m.Write("maps");
}

// =============================================================================
//  Purgatory's Plateau
//
//  Four maps round a square, each joined to the two beside it, climbed onto
//  from the north-west corner of the Ashen Path:
//
//      the Scoured Flats  --  the Stronghold  (and its keep)
//            |                     |
//      the Pale Ascent    --  the Brine Terraces
//            |
//      the Ashen Path
//
//  Fifty to seventy, which the waking world had almost nothing at: the five
//  elemental dragons, the Greater Demons, and Cerberus walking round the
//  Stronghold on the days it is out. Every one of the four has a ground of its
//  own -- pale ash and scree, white salt, wet stone and brine, bone-dust and
//  flagstones -- and what lives on each is posted by the level it is shown at.
// =============================================================================
namespace plat {
static const int CELL = 32, W = 72, H = 56;
using wold::Pt;

struct Exit {
    char side;          // 'N', 'S', 'E' or 'W'
    int at;             // the cell along that edge the road leaves by
    const char* to;     // where it goes
    const char* there;  // and where it puts you there
    const char* here;   // where somebody coming the other way arrives here
    const char* label;
};

// A point `d` cells in from where an exit leaves.
static Pt Inside(const Exit& e, float d) {
    switch (e.side) {
        case 'N': return {static_cast<float>(e.at), d};
        case 'S': return {static_cast<float>(e.at), H - 1 - d};
        case 'W': return {d, static_cast<float>(e.at)};
        default:  return {W - 1 - d, static_cast<float>(e.at)};
    }
}

static bool AtExit(const vector<Exit>& exits, int cx, int cy) {
    for (const Exit& e : exits) {
        const bool edge = (e.side == 'N' && cy == 0) || (e.side == 'S' && cy == H - 1) ||
                          (e.side == 'W' && cx == 0) || (e.side == 'E' && cx == W - 1);
        const int along = (e.side == 'N' || e.side == 'S') ? cx : cy;
        if (edge && abs(along - e.at) <= 1) return true;
    }
    return false;
}

// The roads: from each way out, round a bend, to the middle of the map.
static vector<vector<Pt>> Roads(const vector<Exit>& exits, Pt hub, uint32_t seed) {
    vector<vector<Pt>> roads;
    for (const Exit& e : exits) {
        const Pt a = Inside(e, -1.0f), b = Inside(e, 5.0f);
        const Pt bend = {(b.x + hub.x) / 2.0f + (Hash2(e.at, e.side, seed) - 0.5f) * 10.0f,
                         (b.y + hub.y) / 2.0f + (Hash2(e.side, e.at, seed + 1) - 0.5f) * 8.0f};
        roads.push_back({a, b, bend, hub});
    }
    return roads;
}

// What every one of the four has: its ground, cliffs round the edge but where
// a road leaves, the ways out, and where you arrive by each.
static void Frame(MapBuilder& m, const vector<Exit>& exits, const vector<vector<Pt>>& roads,
                  const std::function<string(int, int, float)>& ground, const char* road = "plateau_road") {
    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float gap = wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
            const string tile = gap < 1.3f ? VariantOf(road, cx, cy) : ground(cx, cy, gap);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            const bool edge = cx == 0 || cy == 0 || cx == W - 1 || cy == H - 1;
            if (edge && !AtExit(exits, cx, cy)) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
    for (const Exit& e : exits) {
        switch (e.side) {
            case 'N': m.Portal(e.at * CELL - 64, 0, 160, 24, e.to, e.there, e.label, false); break;
            case 'S': m.Portal(e.at * CELL - 64, H * CELL - 24, 160, 24, e.to, e.there, e.label, false); break;
            case 'W': m.Portal(0, e.at * CELL - 64, 24, 160, e.to, e.there, e.label, false); break;
            default:  m.Portal(W * CELL - 24, e.at * CELL - 64, 24, 160, e.to, e.there, e.label, false); break;
        }
        const Pt in = Inside(e, 3.0f);
        m.Spawn(e.here, static_cast<int>(in.x) * CELL + 16, static_cast<int>(in.y) * CELL + 16);
    }
}

// A standing thing with a foot to walk into.
static void Stand(MapBuilder& m, const string& art, int x, int y, int cw, int ch) {
    m.Prop("props", art, x, y);
    m.Collision(x - cw / 2, y - ch, cw, ch);
}

// Scenery off the roads: `place` is given each cell's middle and a number of
// its own, and puts up whatever stands there.
static void Scatter(const vector<vector<Pt>>& roads, uint32_t salt, float clear_of_road,
                    const std::function<void(int, int, int, int, float, float)>& place) {
    for (int cy = 2; cy < H - 2; ++cy)
        for (int cx = 2; cx < W - 2; ++cx) {
            const float gap = wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
            if (gap < clear_of_road) continue;
            place(cx, cy, cx * CELL + 16, cy * CELL + 24, Hash2(cx, cy, salt), gap);
        }
}

// What lives on a map, on a lattice off its roads: `who` names the kind that
// stands at a point and the level it is to be shown at, or nothing.
struct Kind { const char* type = nullptr; int shown = 0; };
static void Posts(MapBuilder& m, const vector<vector<Pt>>& roads, int step, uint32_t salt,
                  const std::function<Kind(int, int, float)>& who) {
    for (int gy = 3; gy < H - 3; gy += step)
        for (int gx = 3; gx < W - 3; gx += step) {
            const int cx = gx + static_cast<int>(Hash2(gx, gy, salt) * 3.0f) - 1;
            const int cy = gy + static_cast<int>(Hash2(gx, gy, salt + 1) * 3.0f) - 1;
            const float gap = wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
            if (gap < 2.5f) continue;
            const int x = cx * CELL + 16, y = cy * CELL + 16;
            // Room for a dragon's feet, and nobody met on the doorstep.
            if (!m.Clear(x, y) || !m.Clear(x - 20, y) || !m.Clear(x + 20, y) || m.NearestHaven(x, y) < 260.0f) continue;
            const Kind k = who(cx, cy, Hash2(cx, cy, salt + 7));
            if (!k.type) continue;
            m.Enemy(k.type, x, y, SpawnToShow(k.type, k.shown), 90.0f, 280.0f);
        }
}

// The common scenery of the plateau, shared out a little differently on each.
static void Bones(MapBuilder& m, int x, int y, float r) {
    if (r < 0.020f)      Stand(m, "bone_spire", x, y, 34, 12);
    else if (r < 0.040f) Stand(m, "obsidian_rock", x, y, 20, 8);
    else if (r < 0.060f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
}

static void Sign(MapBuilder& m, const string& id, int x, int y, const string& title, const string& text) {
    json& o = m.Object(id, "sign", x, y);
    o["sprite"] = "assets/props/signpost.png";
    o["title"]  = title;
    o["text"]   = text;
    m.Collision(x - 16, y - 10, 32, 10);
}

static const vector<NightOption> kNights = {
    {{"abyssal_demon", "revenant"}, 1, 0},
    {{"nosferatu", "crypt_warden"}, 1, 0},
    {{"rime_revenant"}, 1, 0},
    {{"greater_demon"}, 1, 1},
};
}   // namespace plat

// --- the Pale Ascent: where the climb comes out ------------------------------------------
static void BuildPlateauAscent() {
    using namespace plat;
    MapBuilder m("plateau_ascent", "The Pale Ascent", W * CELL, H * CELL);
    m.Ambient("ash");
    m.Subtitle("Purgatory's Plateau, at the top of the climb");
    m.Background(40, 38, 38);
    m.Fog(0.10f, 0.0f, {206, 204, 206});
    const vector<Exit> exits = {
        {'S', 36, "ashen_path", "from_plateau", "from_ashen", "Down to the Ashen Path"},
        {'N', 36, "plateau_flats", "from_ascent", "from_flats", "To the Scoured Flats"},
        {'E', 28, "plateau_terraces", "from_ascent", "from_terraces", "To the Brine Terraces"},
    };
    const auto roads = Roads(exits, {36.0f, 29.0f}, 5151u);
    Frame(m, exits, roads, [](int cx, int cy, float gap) {
        const float v = Fbm(cx * 0.2f, cy * 0.2f, 5252);
        return VariantOf(gap < 3.0f || v < 0.52f ? "pale_ash" : "scree", cx, cy);
    });
    m.Spawn("default", 36 * CELL + 16, (H - 4) * CELL + 16);

    // The skull of something that did not make it up, off the road to the west.
    Stand(m, "dragon_skull", 17 * CELL, 17 * CELL, 150, 44);
    Scatter(roads, 5353u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        if (abs(cx - 17) < 5 && abs(cy - 16) < 4) return;
        if (r < 0.020f && gap > 5.0f) { Stand(m, "charred_tree", x, y, 16, 8); return; }
        Bones(m, x, y, r);
    });
    Sign(m, "sign_plateau_ascent", 39 * CELL, (H - 5) * CELL, "A pillar of bleached stone",
         "PURGATORY'S PLATEAU\n\nWhat is up here is not what is down there. Dragons, of every kind the sky has, "
         "and the demons that keep the Stronghold at the top of it.\n\n"
         "Scratched under it: fifty, at the least. Seventy by the fort. And a dog at the gate that is three dogs.");

    Posts(m, roads, 8, 5454u, [](int cx, int cy, float r) -> Kind {
        if (cy < 12 && r < 0.22f) return {"greater_demon", 60};
        if (r < 0.40f) return {"dragon_earth", 52 + static_cast<int>(r * 100) % 5};
        if (r < 0.62f) return {"dragon_fire", 55 + static_cast<int>(r * 100) % 2};
        if (r < 0.80f) return {"demon", 50 + static_cast<int>(r * 100) % 5};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Scoured Flats: salt, and the wind across it ----------------------------------------
static void BuildPlateauFlats() {
    using namespace plat;
    MapBuilder m("plateau_flats", "The Scoured Flats", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("Purgatory's Plateau: salt, and the wind across it");
    m.Background(58, 60, 64);
    const vector<Exit> exits = {
        {'S', 36, "plateau_ascent", "from_flats", "from_ascent", "To the Pale Ascent"},
        {'E', 28, "plateau_stronghold", "from_flats", "from_stronghold", "To the Stronghold"},
    };
    const auto roads = Roads(exits, {36.0f, 29.0f}, 6161u);
    Frame(m, exits, roads, [](int cx, int cy, float gap) {
        const float v = Fbm(cx * 0.16f, cy * 0.16f, 6262);
        const bool rim = cx < 4 || cy < 4 || cx > W - 5 || cy > H - 5;
        if (rim && v > 0.45f) return VariantOf("pale_ash", cx, cy);
        return VariantOf(gap < 2.6f || v > 0.58f ? "salt_crust" : "salt_flat", cx, cy);
    });
    m.Spawn("default", 36 * CELL + 16, (H - 4) * CELL + 16);

    // A ring of salt standing where something was burned, in the north-west.
    for (int k = 0; k < 7; ++k) {
        const float a = k * 6.2831853f / 7.0f;
        Stand(m, "salt_pillar", static_cast<int>((18 + cosf(a) * 5.0f) * CELL), static_cast<int>((15 + sinf(a) * 4.0f) * CELL),
              40, 12);
    }
    Scatter(roads, 6363u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (abs(cx - 18) < 8 && abs(cy - 15) < 7) return;
        if (r < 0.012f) { Stand(m, "salt_pillar", x, y, 40, 12); return; }
        if (r < 0.022f) { Stand(m, "bone_spire", x, y, 34, 12); return; }
        if (r < 0.040f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    PlaceChest(m, "chest_salt_ring", 18 * CELL, 15 * CELL + 16, "chest_stronghold");

    Posts(m, roads, 8, 6464u, [](int cx, int cy, float r) -> Kind {
        (void)cx; (void)cy;
        if (r < 0.34f) return {"dragon_air", 61 + static_cast<int>(r * 100) % 3};
        if (r < 0.58f) return {"dragon_fire", 57 + static_cast<int>(r * 100) % 4};
        if (r < 0.78f) return {"greater_demon", 60 + static_cast<int>(r * 100) % 3};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Brine Terraces: the pools the steam comes off ---------------------------------------
static void BuildPlateauTerraces() {
    using namespace plat;
    MapBuilder m("plateau_terraces", "The Brine Terraces", W * CELL, H * CELL);
    m.Ambient("ash");
    m.Subtitle("Purgatory's Plateau: the pools the steam comes off");
    m.Background(30, 44, 48);
    m.Fog(0.18f, 0.9f, {196, 226, 226});
    const vector<Exit> exits = {
        {'W', 28, "plateau_ascent", "from_terraces", "from_ascent", "To the Pale Ascent"},
        {'N', 36, "plateau_stronghold", "from_terraces", "from_stronghold", "To the Stronghold"},
    };
    const auto roads = Roads(exits, {36.0f, 30.0f}, 7171u);
    // The pools, and nowhere near the roads.
    struct Pool { float cx, cy, rx, ry; };
    const Pool pools[] = {{16, 14, 6, 3.5f}, {56, 12, 7, 4}, {14, 42, 5, 3}, {50, 42, 8, 4.5f}, {60, 28, 4, 3}};
    const auto in_pool = [&](int cx, int cy) {
        for (const Pool& p : pools) {
            const float dx = (cx - p.cx) / p.rx, dy = (cy - p.cy) / p.ry;
            if (dx * dx + dy * dy <= 1.0f) return true;
        }
        return false;
    };
    Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (in_pool(cx, cy)) return VariantOf("brine", cx, cy);
        const float v = Fbm(cx * 0.2f, cy * 0.2f, 7272);
        return VariantOf(v > 0.6f ? "scree" : "brine_stone", cx, cy);
    });
    for (int cy = 1; cy < H - 1; ++cy) {
        int run = -1;
        for (int cx = 1; cx <= W - 1; ++cx) {
            const bool wet = cx < W - 1 && in_pool(cx, cy);
            if (wet && run < 0) run = cx;
            // A wall to everything, as every pond but Fernhollow's and the
            // Bayou's is: nothing on the plateau swims.
            if (!wet && run >= 0) { m.Collision(run * CELL, cy * CELL, (cx - run) * CELL, CELL); run = -1; }
        }
    }
    m.Spawn("default", 4 * CELL + 16, 28 * CELL + 16);

    Scatter(roads, 7373u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (in_pool(cx, cy) || in_pool(cx, cy + 1) || in_pool(cx + 1, cy) || in_pool(cx - 1, cy)) return;
        bool shore = false;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) shore = shore || in_pool(cx + dx, cy + dy);
        if (shore && r < 0.10f) { Stand(m, "steam_vent", x, y, 40, 12); return; }
        Bones(m, x, y, r);
    });

    Posts(m, roads, 8, 7474u, [&](int cx, int cy, float r) -> Kind {
        if (in_pool(cx, cy)) return {};
        if (r < 0.40f) return {"dragon_water", 58 + static_cast<int>(r * 100) % 6};
        if (r < 0.60f) return {"dragon_air", 61 + static_cast<int>(r * 100) % 4};
        if (r < 0.76f) return {"greater_demon", 62 + static_cast<int>(r * 100) % 3};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !in_pool(cx, cy) && wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Stronghold ------------------------------------------------------------------------
static void BuildPlateauStronghold() {
    using namespace plat;
    MapBuilder m("plateau_stronghold", "The Stronghold", W * CELL, H * CELL);
    m.Ambient("ash");
    m.Subtitle("Purgatory's Plateau: the fort at the top of it, and the dog at its gate");
    m.Background(34, 32, 34);
    m.Fog(0.08f, 0.0f, {200, 196, 196});
    const vector<Exit> exits = {
        {'W', 28, "plateau_flats", "from_stronghold", "from_flats", "To the Scoured Flats"},
        {'S', 36, "plateau_terraces", "from_stronghold", "from_terraces", "To the Brine Terraces"},
    };
    // The fort: walls on these cells, the gate in the middle of the south wall.
    const int x0 = 22, x1 = 50, y0 = 5, y1 = 23, gx = 36;
    const auto in_fort = [&](int cx, int cy) { return cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1; };
    const auto roads = Roads(exits, {36.0f, static_cast<float>(y1 + 3)}, 8181u);
    Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (in_fort(cx, cy)) return VariantOf(cy > 16 || Fbm(cx * 0.3f, cy * 0.3f, 8282) > 0.4f ? "stronghold_flag"
                                                                                                 : "stronghold_flag_dark", cx, cy);
        const float v = Fbm(cx * 0.2f, cy * 0.2f, 8383);
        return VariantOf(v > 0.55f ? "scree" : "bone_dust", cx, cy);
    });
    // The road on from the gate to the keep's door.
    for (int cy = 16; cy <= y1; ++cy)
        for (int cx = gx - 1; cx <= gx + 1; ++cx) m.Ground(VariantOf("plateau_road", cx, cy), cx * CELL, cy * CELL, CELL);
    m.Spawn("default", 34 * CELL + 16, (H - 4) * CELL + 16);

    // --- the walls -----------------------------------------------------------------------
    const int gate_x = gx * CELL + 16, gate_y = (y1 + 1) * CELL;
    for (int cx = x0 + 2; cx <= x1 - 1; cx += 4) m.Prop("props", "stronghold_wall", cx * CELL + 16, (y0 + 1) * CELL);
    m.Collision(x0 * CELL, y0 * CELL, (x1 - x0 + 1) * CELL, CELL);
    for (int cx = x0 + 2; cx <= x1 - 1; cx += 4) {
        if (abs(cx * CELL + 16 - gate_x) < 240) continue;
        m.Prop("props", "stronghold_wall", cx * CELL + 16, gate_y);
    }
    // And a length either side laid right up to the gatehouse's towers, so
    // the south wall does not stop short of its own gate.
    for (int s : {-1, 1}) m.Prop("props", "stronghold_wall", gate_x + s * 168, gate_y);
    m.Collision(x0 * CELL, y1 * CELL, gate_x - 112 - x0 * CELL, CELL);
    m.Collision(gate_x + 112, y1 * CELL, (x1 + 1) * CELL - (gate_x + 112), CELL);
    for (int wx : {x0, x1}) {
        for (int cy = y0 + 4; cy <= y1; cy += 4) m.Prop("props", "stronghold_wall_v", wx * CELL + 16, (cy + 1) * CELL);
        m.Collision(wx * CELL, y0 * CELL, CELL, (y1 - y0 + 1) * CELL);
    }
    // The gatehouse: its two towers are solid, the way between them is not.
    m.Prop("props", "stronghold_gate", gate_x, gate_y + 12);
    m.Collision(gate_x - 112, gate_y - 70, 70, 82);
    m.Collision(gate_x + 42, gate_y - 70, 70, 82);
    // A tower at each corner.
    for (int tx : {x0, x1})
        for (int ty : {y0, y1}) {
            m.Prop("props", "stronghold_tower", tx * CELL + 16, (ty + 1) * CELL + 12);
            m.Collision(tx * CELL + 16 - 44, (ty + 1) * CELL + 12 - 44, 88, 44);
        }

    // --- the keep ----------------------------------------------------------------------------
    const int keep_x = gx * CELL + 16, keep_y = 16 * CELL;
    m.Prop("props", "stronghold_keep", keep_x, keep_y);
    m.Collision(keep_x - 160, (y0 + 1) * CELL, 320, keep_y - 38 - (y0 + 1) * CELL);
    m.Portal(keep_x - 24, keep_y - 46, 48, 26, "stronghold_keep", "entrance", "Enter the keep", true);
    m.Danger(68);
    m.Spawn("from_keep", keep_x, keep_y + 34);
    const auto light = [&](const string& id, int x, int y) {
        json& o = m.Object(id, "lamp", x, y);
        o["sprite"] = "assets/props/soul_brazier.png";
        m.Collision(x - 12, y - 8, 24, 8);
    };
    light("stronghold_brazier_w", (gx - 4) * CELL, 18 * CELL);
    light("stronghold_brazier_e", (gx + 5) * CELL, 18 * CELL);
    light("stronghold_brazier_gw", gate_x - 150, gate_y + 40);
    light("stronghold_brazier_ge", gate_x + 150, gate_y + 40);
    PlaceChest(m, "chest_stronghold", (x0 + 4) * CELL, 20 * CELL, "chest_stronghold");
    Sign(m, "sign_stronghold", gate_x + 190, gate_y + 86, "A bone set upright in the road",
         "THE STRONGHOLD\n\nWhoever built it, it is the Greater Demons' now: they keep the courtyard and the keep, "
         "and the Storm Dragons have the ground round it.\n\n"
         "And some days something walks round the walls. Three heads. It does not sleep, and it does not "
         "stop at the gate.");

    // --- its garrison, and what has the ground round it ----------------------------------------
    const int court[][3] = {{26, 19, 64}, {46, 19, 66}, {30, 21, 65}, {42, 21, 67}, {28, 17, 68}, {44, 17, 69}};
    for (const auto& c : court)
        m.Enemy("greater_demon", c[0] * CELL + 16, c[1] * CELL + 16, SpawnToShow("greater_demon", c[2]), 120.0f, 220.0f);
    Scatter(roads, 8484u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (cx >= x0 - 3 && cx <= x1 + 3 && cy >= y0 - 2 && cy <= y1 + 4) return;
        Bones(m, x, y, r);
    });
    Posts(m, roads, 8, 8585u, [&](int cx, int cy, float r) -> Kind {
        if (cx >= x0 - 3 && cx <= x1 + 3 && cy >= y0 - 2 && cy <= y1 + 3) return {};
        if (r < 0.34f) return {"dragon_lightning", 64 + static_cast<int>(r * 100) % 5};
        if (r < 0.52f) return {"dragon_air", 63 + static_cast<int>(r * 100) % 4};
        if (r < 0.74f) return {"greater_demon", 64 + static_cast<int>(r * 100) % 7};
        return {};
    });

    // Cerberus: on the days it is out, round the walls, outside them.
    RoamOn(m, {"cerberus"}, 0, 0, 0.6f,
           {{x0 - 3, y0 - 2}, {gx, y0 - 2}, {x1 + 3, y0 - 2}, {x1 + 3, (y0 + y1) / 2}, {x1 + 3, y1 + 3},
            {gx + 6, y1 + 4}, {gx - 6, y1 + 4}, {x0 - 3, y1 + 3}, {x0 - 3, (y0 + y1) / 2}});
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !(cx >= x0 - 4 && cx <= x1 + 4 && cy >= y0 - 3 && cy <= y1 + 5) &&
               wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the keep ---------------------------------------------------------------------------------
static void BuildStrongholdKeep() {
    const int CELL = 32, cols = 26, rows = 30, back = 3;
    MapBuilder m("stronghold_keep", "The Keep", cols * CELL, rows * CELL);
    m.Interior(true);
    m.Subtitle("Inside the Stronghold");
    m.Background(12, 12, 14);
    const int door0 = 12, door1 = 13;
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool backwall = cy < back, front = cy == rows - 1, west = cx == 0, east = cx == cols - 1;
            const bool gap = front && cx >= door0 && cx <= door1;
            const bool solid = (backwall || front || west || east) && !gap;
            string tile;
            if (solid) tile = backwall && !west && !east ? (cy == back - 1 ? VariantOf("keep_wall", cx, cy) : string("keep_wallface"))
                                                         : string("keep_walltop");
            else tile = VariantOf((cx >= 11 && cx <= 14) ? "keep_floor_dark" : "keep_floor", cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
    // The vault: a wall across the top of the hall with a way through the middle.
    for (int cx = 1; cx < cols - 1; ++cx) {
        if (cx >= 11 && cx <= 14) continue;
        m.Ground("keep_walltop", cx * CELL, 9 * CELL, CELL);
        m.Collision(cx * CELL, 9 * CELL, CELL, CELL);
    }
    m.Portal(door0 * CELL, rows * CELL - 24, (door1 - door0 + 1) * CELL, 24, "plateau_stronghold", "from_keep",
             "Leave the keep", false);
    m.Spawn("entrance", 13 * CELL, (rows - 3) * CELL);
    m.Spawn("default",  13 * CELL, (rows - 3) * CELL);
    // Pillars down both sides of the hall, braziers of the pale fire between.
    for (int cy : {13, 18, 23}) {
        for (int cx : {6, 20}) {
            m.Prop("props", "palace_pillar", cx * CELL, cy * CELL + 16);
            m.Collision(cx * CELL - 20, cy * CELL + 2, 40, 14);
        }
    }
    int lamp = 0;
    for (int cy : {15, 20})
        for (int cx : {6, 20}) {
            json& o = m.Object("keep_brazier_" + std::to_string(lamp++), "lamp", cx * CELL, cy * CELL + 16);
            o["sprite"] = "assets/props/soul_brazier.png";
            m.Collision(cx * CELL - 12, cy * CELL + 8, 24, 8);
        }
    for (int cx : {4, 21}) {
        m.Prop("props", "demon_statue", cx * CELL, 6 * CELL);
        m.Collision(cx * CELL - 18, 6 * CELL - 14, 36, 14);
    }
    PlaceChest(m, "chest_keep_vault", 13 * CELL, 5 * CELL, "chest_keep_vault");
    // Its garrison: the Stronghold's best, and a Storm Dragon in the vault.
    const int garrison[][3] = {{9, 16, 66}, {17, 16, 67}, {9, 21, 68}, {17, 21, 68}, {13, 12, 69}};
    for (const auto& g : garrison)
        m.Enemy("greater_demon", g[0] * CELL, g[1] * CELL, SpawnToShow("greater_demon", g[2]), 150.0f, 200.0f);
    m.Enemy("dragon_lightning", 13 * CELL, 7 * CELL, SpawnToShow("dragon_lightning", 70), 150.0f, 200.0f);
    m.Write("maps");
}

static void BuildPurgatoryPlateau() {
    BuildPlateauAscent();
    BuildPlateauFlats();
    BuildPlateauTerraces();
    BuildPlateauStronghold();
    BuildStrongholdKeep();
}

// =============================================================================
//  The Hexmire: four maps north of the Bayou, and the sanctum in the temple
// =============================================================================
//
// Climbed into from the Bayou's north-west, where the west spur ran off the
// top of the map and stopped: 55 to 65, the band just past the Bayou's own.
//
//     the Candle Fens    --  the Hexmire Temple  (and its sanctum)
//           |                        |
//     the Cypress Drowns --  Shellback Strand
//           |
//     the Bayou
//
// Built on Purgatory's Plateau's frame (plat::): the same size of map, the same
// ways out and roads, posts laid by the level they are to show. The cult has
// the fens and the temple; the Shellbacks -- turtle-folk, shells no blade goes
// through and claws like billhooks -- have the strand, and are at war with it.
namespace hexm {
using plat::CELL;
using plat::W;
using plat::H;
using plat::Exit;
using plat::Kind;

// Water nothing on foot goes into: a wall along every run of it, row by row,
// as the Brine Terraces' pools have. It is drawn as water all the same.
static void WallOffWater(MapBuilder& m, const std::function<bool(int, int)>& wet) {
    for (int cy = 1; cy < H - 1; ++cy) {
        int run = -1;
        for (int cx = 1; cx <= W - 1; ++cx) {
            const bool w = cx < W - 1 && wet(cx, cy);
            if (w && run < 0) run = cx;
            if (!w && run >= 0) { m.Collision(run * CELL, cy * CELL, (cx - run) * CELL, CELL); run = -1; }
        }
    }
}

static bool NearWet(const std::function<bool(int, int)>& wet, int cx, int cy, int r) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (wet(cx + dx, cy + dy)) return true;
    return false;
}

// One of the cult's green jars on a pole, lit after dark.
static void Lantern(MapBuilder& m, const string& id, int x, int y) {
    if (!m.Clear(x, y) || !m.Clear(x, y - 12)) return;
    json& o = m.Object(id, "lamp", x, y);
    o["sprite"] = "assets/props/hex_lantern.png";
    m.Collision(x - 8, y - 8, 16, 8);
}

// A jar beside each road, where it bends.
static void RoadLights(MapBuilder& m, const vector<vector<wold::Pt>>& roads, const string& map) {
    int n = 0;
    for (const auto& r : roads) {
        const wold::Pt p = r[2];
        Lantern(m, map + "_lantern_" + std::to_string(n++), static_cast<int>(p.x + 2.5f) * CELL + 16,
                static_cast<int>(p.y) * CELL + 16);
    }
}

// A ring of the cult's poles round a shrine, where they gather.
static void RitualRing(MapBuilder& m, int cx, int cy, float r, int poles) {
    for (int k = 0; k < poles; ++k) {
        const float a = k * 6.2831853f / poles;
        plat::Stand(m, "fetish_pole", static_cast<int>((cx + cosf(a) * r) * CELL) + 16,
                    static_cast<int>((cy + sinf(a) * r * 0.8f) * CELL) + 16, 18, 8);
    }
    plat::Stand(m, "candle_shrine", cx * CELL + 16, cy * CELL + 16, 34, 10);
    plat::Stand(m, "hex_drum", (cx - 2) * CELL + 16, (cy + 1) * CELL + 16, 24, 10);
    plat::Stand(m, "hex_drum", (cx + 2) * CELL + 16, (cy + 1) * CELL + 16, 24, 10);
}

// What comes out at night: the dead, from the crypts and further, that the
// drums call up the swamp -- by the same rules as everywhere (PlaceNightVisitors).
static const vector<NightOption> kNights = {
    {{"nosferatu", "crypt_warden"}, 1, 0},
    {{"revenant"}, 1, 0},
    {{"abyssal_demon"}, 1, 0},
};
}   // namespace hexm

// --- the Cypress Drowns: black water under the cypress -------------------------------
static void BuildHexDrowns() {
    using namespace hexm;
    MapBuilder m("hex_drowns", "The Cypress Drowns", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("The Hexmire: black water under the cypress");
    m.Background(16, 20, 16);
    m.Fog(0.20f, 1.0f, {150, 170, 140});
    const vector<Exit> exits = {
        {'S', 36, "bayou", "from_hexmire", "from_bayou", "Back to the Bayou"},
        {'N', 36, "hex_fens", "from_drowns", "from_fens", "To the Candle Fens"},
        {'E', 28, "hex_strand", "from_drowns", "from_strand", "To Shellback Strand"},
    };
    const auto roads = plat::Roads(exits, {36.0f, 28.0f}, 9151u);
    const auto wet = [&](int cx, int cy) {
        if (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4) return false;
        if (wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) < 4.0f) return false;
        return Fbm(cx * 0.11f, cy * 0.11f, 9252) > 0.60f;
    };
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        if (wet(cx, cy)) return VariantOf("blackwater", cx, cy);
        const float v = Fbm(cx * 0.18f, cy * 0.18f, 9353);
        return VariantOf(gap > 2.0f && v > 0.62f ? "cypress_litter" : "drowned_loam", cx, cy);
    }, "hex_road");
    WallOffWater(m, wet);
    m.Spawn("default", 36 * CELL + 16, (H - 4) * CELL + 16);

    plat::Scatter(roads, 9454u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        if (wet(cx, cy) || wet(cx, cy + 1) || wet(cx - 1, cy) || wet(cx + 1, cy)) return;
        const bool shore = NearWet(wet, cx, cy, 2);
        if (r < 0.040f && gap > 4.0f && !NearWet(wet, cx, cy, 2)) { plat::Stand(m, "cypress_tree", x, y, 40, 14); return; }
        if (shore && r < 0.09f) {
            if (r < 0.04f) plat::Stand(m, "cypress_knees", x, y, 26, 8);
            else m.Prop("props", "reeds", x, y);
            return;
        }
        if (r < 0.050f && gap > 4.0f) { plat::Stand(m, "swamp_tree", x, y, 16, 8); return; }
        if (r < 0.058f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    RoadLights(m, roads, "hex_drowns");
    plat::Sign(m, "sign_hex_drowns", 39 * CELL, (H - 5) * CELL, "A post hung with a jar",
               "THE HEXMIRE\n\nPast here the cult keeps the swamp: the drums at night are theirs, and the lights "
               "in the jars. North, their fens, and their temple beyond. East, the Shellbacks' strand -- turtle-folk, "
               "as tall as a man, with claws like billhooks. They are at war with the cult, and will not stop to ask "
               "whose side you are on.\n\nScratched under it: FIFTY-FIVE. AND IT GETS WORSE.");

    plat::Posts(m, roads, 7, 9555u, [&](int cx, int cy, float r) -> Kind {
        if (NearWet(wet, cx, cy, 1)) return {};
        if (r < 0.26f) return {"hex_cultist", 55 + static_cast<int>(r * 100) % 3};
        if (r < 0.46f) return {"shellback_clawfighter", 56 + static_cast<int>(r * 100) % 3};
        if (r < 0.60f) return {"hex_blowgunner", 57 + static_cast<int>(r * 100) % 2};
        if (r < 0.72f) return {"drowned_one", 55 + static_cast<int>(r * 100) % 3};
        if (r < 0.80f) return {"swamp_hag", 56 + static_cast<int>(r * 100) % 3};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !NearWet(wet, cx, cy, 1) && wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- Shellback Strand: the turtle-folk's shore ---------------------------------------------
static void BuildHexStrand() {
    using namespace hexm;
    MapBuilder m("hex_strand", "Shellback Strand", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("The Hexmire: the Shellbacks' shore, and their village on it");
    m.Background(40, 62, 60);
    m.Fog(0.10f, 0.6f, {196, 214, 208});
    const vector<Exit> exits = {
        {'W', 28, "hex_drowns", "from_strand", "from_drowns", "To the Cypress Drowns"},
        {'N', 36, "hex_temple", "from_strand", "from_temple", "To the Hexmire Temple"},
    };
    const auto roads = plat::Roads(exits, {30.0f, 30.0f}, 9161u);
    // The lagoon: the east of the map, its shore wandering down the length of it.
    const auto shore_x = [](int cy) { return 47.0f + (Fbm(cy * 0.09f, 2.5f, 9262) - 0.5f) * 14.0f; };
    const auto lagoon = [&](int cx, int cy) {
        return cy >= 1 && cy <= H - 2 && cx <= W - 2 && static_cast<float>(cx) > shore_x(cy) &&
               wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 3.0f;
    };
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (lagoon(cx, cy)) return VariantOf("lagoon", cx, cy);
        const float d = shore_x(cy) - static_cast<float>(cx);
        if (d < 3.0f) return VariantOf("shell_sand", cx, cy);
        const float v = Fbm(cx * 0.12f, cy * 0.12f, 9264);
        return VariantOf(v > 0.58f ? "tide_flat" : "shell_sand", cx, cy);
    }, "hex_road");
    WallOffWater(m, lagoon);
    m.Spawn("default", 4 * CELL + 16, 28 * CELL + 16);

    // --- the village: round shell-roofed houses about a fire -------------------------------
    const int vx = 17, vy = 14;
    const int huts[][2] = {{9, 8}, {19, 6}, {27, 11}, {7, 17}, {26, 20}, {14, 22}};
    for (const auto& h : huts) plat::Stand(m, "shellback_hut", h[0] * CELL + 16, h[1] * CELL + 16, 124, 40);
    {
        json& fire = m.Object("range_shellback_fire", "range", vx * CELL + 16, vy * CELL + 16);
        fire["sprite"] = "assets/props/campfire_ring.png";
        fire["title"]  = "Cook fire";
        m.Collision(vx * CELL + 16 - 16, vy * CELL + 6, 32, 10);
    }
    for (const auto& d : {std::pair<int, int>{-3, -2}, {3, 2}}) plat::Stand(m, "hex_drum", (vx + d.first) * CELL + 16,
                                                                              (vy + d.second) * CELL + 16, 24, 10);
    const auto in_village = [&](int cx, int cy) { return abs(cx - vx) <= 13 && abs(cy - vy) <= 11; };
    plat::Scatter(roads, 9365u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (lagoon(cx, cy) || lagoon(cx + 1, cy) || lagoon(cx - 1, cy) || lagoon(cx, cy + 1)) return;
        if (in_village(cx, cy)) {
            if (r < 0.03f && abs(cx - vx) + abs(cy - vy) > 4) plat::Stand(m, "shell_midden", x, y, 34, 8);
            return;
        }
        const float d = shore_x(cy) - static_cast<float>(cx);
        if (d < 5.0f && r < 0.06f) { plat::Stand(m, "driftwood", x, y, 50, 8); return; }
        if (d < 5.0f && r < 0.09f) { plat::Stand(m, "shell_midden", x, y, 34, 8); return; }
        if (r < 0.006f && d > 12.0f) { plat::Stand(m, "cypress_tree", x, y, 40, 14); return; }
        if (r < 0.016f) { plat::Stand(m, "driftwood", x, y, 50, 8); return; }
        if (r < 0.03f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    PlaceChest(m, "chest_strand", (vx + 1) * CELL + 16, (vy - 5) * CELL + 16, "chest_hexmire");
    plat::Sign(m, "sign_hex_strand", 7 * CELL, 25 * CELL, "A turtle's shell nailed to a post",
               "SHELLBACK STRAND\n\nWhat is painted on the shell is not a word anyone reads. What is scratched round "
               "it is: THEY DO NOT TRADE. THEY DO NOT TALK. THEIR ELDERS ARE WORSE.");

    // --- who lives here: the village's own round its fire, and the rest along the shore -------
    const int elders[][3] = {{vx - 2, vy + 3, 63}, {vx + 4, vy - 1, 64}};
    for (const auto& e : elders)
        m.Enemy("shellback_elder", e[0] * CELL + 16, e[1] * CELL + 16, SpawnToShow("shellback_elder", e[2]), 120.0f,
                240.0f);
    const int guards[][3] = {{vx - 6, vy - 1, 60}, {vx + 7, vy + 4, 61}, {vx, vy + 6, 60}, {vx + 1, vy - 7, 61}};
    for (const auto& g : guards)
        m.Enemy("shellback_snapper", g[0] * CELL + 16, g[1] * CELL + 16, SpawnToShow("shellback_snapper", g[2]), 90.0f,
                260.0f);
    plat::Posts(m, roads, 8, 9566u, [&](int cx, int cy, float r) -> Kind {
        if (in_village(cx, cy) || lagoon(cx, cy) || lagoon(cx + 1, cy)) return {};
        if (r < 0.34f) return {"shellback_clawfighter", 57 + static_cast<int>(r * 100) % 3};
        if (r < 0.56f) return {"shellback_snapper", 60 + static_cast<int>(r * 100) % 2};
        if (r < 0.70f) return {"hex_cultist", 58 + static_cast<int>(r * 100) % 2};
        if (r < 0.80f) return {"hex_blowgunner", 58 + static_cast<int>(r * 100) % 2};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !in_village(cx, cy) && !NearWet(lagoon, cx, cy, 1) &&
               wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Candle Fens: where the cult keeps its rites --------------------------------------
static void BuildHexFens() {
    using namespace hexm;
    MapBuilder m("hex_fens", "The Candle Fens", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("The Hexmire: red clay, and the cult's candles in it");
    m.Background(34, 22, 18);
    m.Fog(0.14f, 0.8f, {186, 170, 140});
    const vector<Exit> exits = {
        {'S', 36, "hex_drowns", "from_fens", "from_drowns", "To the Cypress Drowns"},
        {'E', 28, "hex_temple", "from_fens", "from_temple", "To the Hexmire Temple"},
    };
    const auto roads = plat::Roads(exits, {34.0f, 30.0f}, 9171u);
    // Where the cult gathers: three rings of poles round three shrines.
    const int rings[][2] = {{15, 13}, {55, 12}, {17, 42}};
    const auto near_ring = [&](int cx, int cy, int r) {
        for (const auto& g : rings)
            if (std::hypot(static_cast<float>(cx - g[0]), static_cast<float>(cy - g[1])) <= r + 0.5f) return true;
        return false;
    };
    const auto wet = [&](int cx, int cy) {
        if (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4 || near_ring(cx, cy, 7)) return false;
        if (wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) < 4.0f) return false;
        return Fbm(cx * 0.12f, cy * 0.12f, 9272) > 0.64f;
    };
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        if (wet(cx, cy)) return VariantOf("blackwater", cx, cy);
        (void)gap;
        if (near_ring(cx, cy, 5)) return VariantOf("hex_clay", cx, cy);
        const float v = Fbm(cx * 0.13f, cy * 0.13f, 9373);
        return VariantOf(v > 0.64f ? "hex_clay" : "fen_sedge", cx, cy);
    }, "hex_road");
    WallOffWater(m, wet);
    m.Spawn("default", 36 * CELL + 16, (H - 4) * CELL + 16);

    for (const auto& g : rings) RitualRing(m, g[0], g[1], 4.0f, 6);
    PlaceChest(m, "chest_candle_fens", rings[0][0] * CELL + 16, (rings[0][1] - 2) * CELL + 16, "chest_hexmire");
    plat::Scatter(roads, 9474u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        if (near_ring(cx, cy, 6) || wet(cx, cy) || wet(cx, cy + 1) || wet(cx - 1, cy) || wet(cx + 1, cy)) return;
        if (NearWet(wet, cx, cy, 2) && r < 0.08f) { m.Prop("props", "reeds", x, y); return; }
        if (r < 0.010f && gap > 4.0f) { plat::Stand(m, "bottle_tree", x, y, 20, 8); return; }
        if (r < 0.018f) { plat::Stand(m, "fetish_pole", x, y, 18, 8); return; }
        if (r < 0.026f) { plat::Stand(m, "candle_shrine", x, y, 34, 10); return; }
        if (r < 0.040f && gap > 4.0f) { plat::Stand(m, "swamp_tree", x, y, 16, 8); return; }
        if (r < 0.048f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    RoadLights(m, roads, "hex_fens");
    plat::Sign(m, "sign_hex_fens", 39 * CELL, (H - 5) * CELL, "A post with a doll nailed to it",
               "THE CANDLE FENS\n\nWhere the cult keeps its rites. The rings of poles are where they gather; the "
               "shamans stand in the middle of them, and what they throw is worse than an arrow -- it finds you, and "
               "some of it turns you round, or makes you walk to them.\n\nThe doll has a pin through it.");

    // The shamans at their rings, with their people round them.
    for (const auto& g : rings) {
        m.Enemy("voodoo_shaman", g[0] * CELL + 16, (g[1] + 2) * CELL + 16, SpawnToShow("voodoo_shaman", 61), 90.0f,
                240.0f);
        m.Enemy("hex_zealot", (g[0] + 5) * CELL + 16, (g[1] + 3) * CELL + 16, SpawnToShow("hex_zealot", 62), 90.0f,
                260.0f);
    }
    plat::Posts(m, roads, 8, 9575u, [&](int cx, int cy, float r) -> Kind {
        if (near_ring(cx, cy, 5) || NearWet(wet, cx, cy, 1)) return {};
        if (r < 0.26f) return {"voodoo_shaman", 59 + static_cast<int>(r * 100) % 3};
        if (r < 0.48f) return {"hex_cultist", 59 + static_cast<int>(r * 100) % 2};
        if (r < 0.62f) return {"hex_blowgunner", 59 + static_cast<int>(r * 100) % 3};
        if (r < 0.74f) return {"hex_zealot", 62 + static_cast<int>(r * 100) % 2};
        if (r < 0.80f) return {"swamp_hag", 60 + static_cast<int>(r * 100) % 2};
        return {};
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !near_ring(cx, cy, 6) && !NearWet(wet, cx, cy, 1) &&
               wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Hexmire Temple: the cult's stockade, and the temple in it -------------------------
static void BuildHexTemple() {
    using namespace hexm;
    MapBuilder m("hex_temple", "The Hexmire Temple", W * CELL, H * CELL);
    m.Ambient("grove");
    m.Subtitle("The Hexmire: the cult's stockade, and the temple in it");
    m.Background(30, 22, 18);
    m.Fog(0.12f, 0.8f, {180, 176, 150});
    const vector<Exit> exits = {
        {'W', 28, "hex_fens", "from_temple", "from_fens", "To the Candle Fens"},
        {'S', 36, "hex_strand", "from_temple", "from_strand", "To Shellback Strand"},
    };
    // The stockade: stakes on these cells, the gate in the middle of the south side.
    const int x0 = 22, x1 = 50, y0 = 5, y1 = 23, gx = 36;
    const auto in_yard = [&](int cx, int cy) { return cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1; };
    const auto round_yard = [&](int cx, int cy, int pad) {
        return cx >= x0 - pad && cx <= x1 + pad && cy >= y0 - pad && cy <= y1 + pad + 1;
    };
    const auto roads = plat::Roads(exits, {36.0f, static_cast<float>(y1 + 3)}, 9181u);
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (in_yard(cx, cy)) return VariantOf("temple_earth", cx, cy);
        const float v = Fbm(cx * 0.13f, cy * 0.13f, 9282);
        return VariantOf(v > 0.64f ? "drowned_loam" : "fen_sedge", cx, cy);
    }, "hex_road");
    for (int cy = 16; cy <= y1; ++cy)
        for (int cx = gx - 1; cx <= gx + 1; ++cx) m.Ground(VariantOf("hex_road", cx, cy), cx * CELL, cy * CELL, CELL);
    m.Spawn("default", 34 * CELL + 16, (H - 4) * CELL + 16);

    // --- the stockade ---------------------------------------------------------------------
    const int gate_x = gx * CELL + 16, gate_y = (y1 + 1) * CELL;
    for (int cx = x0 + 2; cx <= x1 - 1; cx += 4) m.Prop("props", "stockade_wall", cx * CELL + 16, (y0 + 1) * CELL);
    m.Collision(x0 * CELL, y0 * CELL, (x1 - x0 + 1) * CELL, CELL);
    for (int cx = x0 + 2; cx <= x1 - 1; cx += 4) {
        if (abs(cx * CELL + 16 - gate_x) < 230) continue;
        m.Prop("props", "stockade_wall", cx * CELL + 16, gate_y);
    }
    for (int s : {-1, 1}) m.Prop("props", "stockade_wall", gate_x + s * 168, gate_y);
    m.Collision(x0 * CELL, y1 * CELL, gate_x - 56 - x0 * CELL, CELL);
    m.Collision(gate_x + 56, y1 * CELL, (x1 + 1) * CELL - (gate_x + 56), CELL);
    for (int wx : {x0, x1}) {
        for (int cy = y0 + 4; cy <= y1; cy += 4) m.Prop("props", "stockade_wall_v", wx * CELL + 16, (cy + 1) * CELL);
        m.Collision(wx * CELL, y0 * CELL, CELL, (y1 - y0 + 1) * CELL);
    }
    m.Prop("props", "stockade_gate", gate_x, gate_y + 10);
    m.Collision(gate_x - 110, gate_y - 16, 50, 26);
    m.Collision(gate_x + 60, gate_y - 16, 50, 26);
    for (int tx : {x0, x1})
        for (int ty : {y0, y1}) plat::Stand(m, "fetish_pole", tx * CELL + 16, (ty + 1) * CELL + 14, 18, 8);

    // --- the temple ---------------------------------------------------------------------------
    const int tx = gx * CELL + 16, ty = 16 * CELL;
    m.Prop("props", "hex_temple", tx, ty);
    m.Collision(tx - 152, (y0 + 1) * CELL, 304, ty - 44 - (y0 + 1) * CELL);
    for (int s : {-1, 1}) {
        m.Collision(tx + s * 74 - 6, ty - 8, 12, 8);           // the poles either side of the porch
        m.Collision(tx + s * 38 - 6, ty - 30, 12, 8);          // the porch's own posts
    }
    m.Portal(tx - 24, ty - 44, 48, 22, "hex_sanctum", "entrance", "Enter the temple", true);
    m.Danger(63);
    m.Spawn("from_sanctum", tx, ty + 34);
    for (const auto& p : {std::pair<int, int>{-5, 18}, {5, 18}, {-5, 21}, {5, 21}})
        plat::Stand(m, "fetish_pole", (gx + p.first) * CELL + 16, p.second * CELL + 16, 18, 8);
    plat::Stand(m, "candle_shrine", (gx - 8) * CELL + 16, 19 * CELL + 16, 34, 10);
    plat::Stand(m, "candle_shrine", (gx + 8) * CELL + 16, 19 * CELL + 16, 34, 10);
    plat::Stand(m, "hex_drum", (gx - 8) * CELL + 16, 21 * CELL + 16, 24, 10);
    plat::Stand(m, "hex_drum", (gx + 8) * CELL + 16, 21 * CELL + 16, 24, 10);
    plat::Stand(m, "bottle_tree", (x0 + 3) * CELL + 16, (y1 - 2) * CELL + 16, 20, 8);
    plat::Stand(m, "bottle_tree", (x1 - 3) * CELL + 16, (y1 - 2) * CELL + 16, 20, 8);
    Lantern(m, "hex_temple_lantern_gw", gate_x - 150, gate_y + 42);
    Lantern(m, "hex_temple_lantern_ge", gate_x + 150, gate_y + 42);
    Lantern(m, "hex_temple_lantern_yw", (gx - 3) * CELL, 23 * CELL - 8);
    Lantern(m, "hex_temple_lantern_ye", (gx + 4) * CELL, 23 * CELL - 8);
    PlaceChest(m, "chest_hex_temple", (x1 - 4) * CELL, 20 * CELL, "chest_hexmire");
    plat::Sign(m, "sign_hex_temple", gate_x + 190, gate_y + 86, "A skull on a stake by the road",
               "THE HEXMIRE TEMPLE\n\nThe cult's. Its zealots keep the yard and its shamans the temple, and in the "
               "sanctum, the one they all answer to: the High Priest.\n\nSome days something else walks round the "
               "stakes. The Shellbacks' elders have come up the road to the gate more than once. None of them went "
               "back down it.");

    // --- its garrison, and what has the ground round it ------------------------------------------
    const int yard[][4] = {{26, 19, 62, 0}, {46, 19, 63, 0}, {30, 21, 62, 1}, {42, 21, 63, 1}, {28, 16, 64, 0},
                           {44, 16, 64, 1}};
    for (const auto& c : yard) {
        const char* type = c[3] ? "voodoo_shaman" : "hex_zealot";
        m.Enemy(type, c[0] * CELL + 16, c[1] * CELL + 16, SpawnToShow(type, c[2]), 120.0f, 220.0f);
    }
    plat::Scatter(roads, 9484u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (round_yard(cx, cy, 3)) return;
        if (r < 0.014f) { plat::Stand(m, "cypress_tree", x, y, 40, 14); return; }
        if (r < 0.022f) { plat::Stand(m, "fetish_pole", x, y, 18, 8); return; }
        if (r < 0.034f) { plat::Stand(m, "swamp_tree", x, y, 16, 8); return; }
        if (r < 0.044f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    plat::Posts(m, roads, 8, 9585u, [&](int cx, int cy, float r) -> Kind {
        if (round_yard(cx, cy, 3)) return {};
        // A war-band of the Shellbacks, come up from the strand, on the south-east.
        if (cx > 40 && cy > 30 && r < 0.40f) return {"shellback_elder", 64 + static_cast<int>(r * 100) % 2};
        if (r < 0.30f) return {"hex_zealot", 62 + static_cast<int>(r * 100) % 3};
        if (r < 0.52f) return {"voodoo_shaman", 61 + static_cast<int>(r * 100) % 3};
        if (r < 0.66f) return {"hex_blowgunner", 61 + static_cast<int>(r * 100) % 2};
        if (r < 0.76f) return {"shellback_snapper", 62 + static_cast<int>(r * 100) % 2};
        return {};
    });

    // One of a pool of bosses, some days, round the stakes outside them.
    RoamOn(m, {"pit_lord", "vampire_lord", "bayou_matriarch", "orc3"}, 64, 1, 0.6f,
           {{x0 - 3, y0 - 2}, {gx, y0 - 2}, {x1 + 3, y0 - 2}, {x1 + 3, (y0 + y1) / 2}, {x1 + 3, y1 + 3},
            {gx + 6, y1 + 4}, {gx - 6, y1 + 4}, {x0 - 3, y1 + 3}, {x0 - 3, (y0 + y1) / 2}});
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !round_yard(cx, cy, 4) && wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the sanctum ---------------------------------------------------------------------------
static void BuildHexSanctum() {
    const int CELL = 32, cols = 26, rows = 30, back = 3;
    MapBuilder m("hex_sanctum", "The Sanctum", cols * CELL, rows * CELL);
    m.Interior(true);
    m.Subtitle("Inside the Hexmire Temple");
    m.Background(14, 8, 6);
    const int door0 = 12, door1 = 13;
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool backwall = cy < back, front = cy == rows - 1, west = cx == 0, east = cx == cols - 1;
            const bool gap = front && cx >= door0 && cx <= door1;
            const bool solid = (backwall || front || west || east) && !gap;
            string tile;
            if (solid) tile = backwall && !west && !east ? (cy == back - 1 ? VariantOf("sanctum_wall", cx, cy)
                                                                            : string("sanctum_wallface"))
                                                         : string("sanctum_walltop");
            else tile = VariantOf((cx >= 11 && cx <= 14) ? "sanctum_floor_dark" : "sanctum_floor", cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
    m.Portal(door0 * CELL, rows * CELL - 24, (door1 - door0 + 1) * CELL, 24, "hex_temple", "from_sanctum",
             "Leave the temple", false);
    m.Spawn("entrance", 13 * CELL, (rows - 3) * CELL);
    m.Spawn("default",  13 * CELL, (rows - 3) * CELL);
    // The post everything turns round, in the middle of the floor; the altar at
    // the back, and the High Priest before it.
    m.Prop("props", "poto_mitan", 13 * CELL, 17 * CELL);
    m.Collision(13 * CELL - 16, 17 * CELL - 12, 32, 12);
    m.Prop("props", "hex_altar", 13 * CELL, 6 * CELL);
    m.Collision(13 * CELL - 44, 6 * CELL - 16, 88, 16);
    for (int cy : {11, 16, 21})
        for (int cx : {5, 21}) {
            m.Prop("props", "fetish_pole", cx * CELL, cy * CELL + 16);
            m.Collision(cx * CELL - 9, cy * CELL + 8, 18, 8);
        }
    int lamp = 0;
    for (int cy : {13, 19, 24})
        for (int cx : {3, 23}) {
            json& o = m.Object("sanctum_lantern_" + std::to_string(lamp++), "lamp", cx * CELL, cy * CELL + 16);
            o["sprite"] = "assets/props/hex_lantern.png";
            m.Collision(cx * CELL - 8, cy * CELL + 8, 16, 8);
        }
    for (int cx : {8, 18}) {
        m.Prop("props", "candle_shrine", cx * CELL, 5 * CELL);
        m.Collision(cx * CELL - 17, 5 * CELL - 10, 34, 10);
        m.Prop("props", "hex_drum", cx * CELL, 8 * CELL);
        m.Collision(cx * CELL - 12, 8 * CELL - 10, 24, 10);
    }
    PlaceChest(m, "chest_sanctum", 21 * CELL, 5 * CELL, "chest_sanctum");
    // Its congregation: shamans and zealots down both sides, and the High Priest.
    const int flock[][4] = {{9, 13, 63, 1}, {17, 13, 64, 1}, {9, 20, 63, 0}, {17, 20, 64, 0}, {13, 23, 64, 0}};
    for (const auto& f : flock) {
        const char* type = f[3] ? "voodoo_shaman" : "hex_zealot";
        m.Enemy(type, f[0] * CELL, f[1] * CELL, SpawnToShow(type, f[2]), 150.0f, 200.0f);
    }
    m.Enemy("hex_priest", 13 * CELL, 9 * CELL, 1, 150.0f, 260.0f);
    m.Write("maps");
}

static void BuildHexmire() {
    BuildHexDrowns();
    BuildHexStrand();
    BuildHexFens();
    BuildHexTemple();
    BuildHexSanctum();
}

// =============================================================================
//  The Frostreach: four maps west of the Ice Spire, the Howe under the fourth,
//  and the trapper's cabin in the middle of the Glass Mere
// =============================================================================
//
// Off the Spire's track a third of the way up, through a gap in the cliffs
// between two runestones: 60 to 75, between the Hexmire and the Brimstone
// Palace, and under Hoarfang's 79.
//
//     the Warlord's Howe  --  the Rimefall Glacier
//           |                        |
//     the Glass Mere      --  the Draugr Barrows  --  Ice Spire Peak
//
// Built on the plateau's frame (plat::) like the Hexmire. What is new is the
// Mere: a frozen lake, thin ice over all of it (World::UpdateThinIce), the
// trapper's cabin on an islet in the middle, and nothing to be done about the
// draugr shooting at you from it but walk.
namespace frost {
using plat::CELL;
using plat::W;
using plat::H;
using plat::Exit;
using plat::Kind;

// The edge of every map: pines and spires of ice thick along it, so a wall of
// cells reads as the mountain closing in rather than a border.
static void Rim(MapBuilder& m, const vector<Exit>& exits, uint32_t salt) {
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            const int edge = std::min(std::min(cx, cy), std::min(W - 1 - cx, H - 1 - cy));
            if (edge > 2) continue;
            bool near_exit = false;
            for (const Exit& e : exits) {
                const plat::Pt p = plat::Inside(e, 0.0f);
                near_exit |= std::hypot(cx - p.x, cy - p.y) < 6.0f;
            }
            if (near_exit) continue;
            const float r = Hash2(cx, cy, salt);
            const int x = cx * CELL + 16, y = cy * CELL + 28;
            if (edge == 1 && r < 0.40f) m.Prop("props", r < 0.12f ? "ice_spire" : "snow_pine", x, y);
            else if (edge == 2 && r < 0.18f) m.Prop("props", "snow_pine", x, y);
        }
}

// A light for after dark: the pale blue fire the dead keep.
static void Brazier(MapBuilder& m, const string& id, int x, int y) {
    if (!m.Clear(x, y)) return;
    json& o = m.Object(id, "lamp", x, y);
    o["sprite"] = "assets/props/frost_brazier.png";
    m.Collision(x - 13, y - 18, 26, 14);
}

// What is solid of each, measured off its art: `up` is how far above the
// point it stands on its solid part begins, `deep` how far back it runs.
static void Solid(MapBuilder& m, const string& art, int x, int y, int w, int up, int deep) {
    m.Prop("props", art, x, y);
    m.Collision(x - w / 2, y - up - deep, w, deep);
}
// A barrow of the Frostreach: its mound, and the two stones at its door.
static void Barrow(MapBuilder& m, int x, int y) {
    Solid(m, "frost_barrow", x, y, 150, 20, 60);
    for (int s : {-1, 1}) m.Collision(x + s * 42 - 6, y - 12, 12, 6);
}
static void Igloo(MapBuilder& m, int x, int y) {
    Solid(m, "igloo", x, y, 84, 20, 46);
    m.Collision(x - 21, y - 20, 42, 14);
}

static void Snowman(MapBuilder& m, int x, int y) {
    if (!m.Clear(x, y) || !m.Clear(x - 10, y) || !m.Clear(x + 10, y)) return;
    Solid(m, "snowman", x, y, 20, 4, 11);
}

// What the dark brings up out of the barrows, and down off the glacier: the
// frozen dead, by the same rules as everywhere (PlaceNightVisitors).
static const vector<NightOption> kNights = {
    {{"crypt_warden"}, 1, 0},
    {{"rime_revenant"}, 1, 0},
    {{"revenant", "abyssal_demon"}, 1, 0},
};

// Rime beetles, picking their way over the open ground of a Frostreach map:
// off its roads but within `far` cells of one, back from its rim, wherever
// `fits` says.
static void Beetles(MapBuilder& m, const vector<vector<plat::Pt>>& roads, uint32_t salt, int want,
                    const std::function<bool(int, int)>& fits, float far = 12.0f) {
    int bug_i = 0;
    const int got = PlaceBugs(m, "rime_beetle", want, CELL, 4, 4, W - 4, H - 4, salt, 7.0f, [&](int cx, int cy) {
        const float gap = wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy));
        return gap >= 2.5f && gap <= far && fits(cx, cy);
    }, bug_i);
    std::printf("  %s: %d rime beetles\n", m.Id().c_str(), got);
}
}   // namespace frost

// --- the Draugr Barrows: the heath of the dead -------------------------------------------
static void BuildFrostBarrows() {
    using namespace frost;
    MapBuilder m("frost_barrows", "The Draugr Barrows", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("The Frostreach: where the old dead of the mountain were laid");
    m.Background(206, 214, 224);
    m.Fog(0.10f, 0.0f, {226, 234, 242});
    const vector<Exit> exits = {
        {'E', 28, "ice_spire_peak", "from_frostreach", "from_spire", "Back to the Ice Spire"},
        {'W', 28, "frost_mere", "from_barrows", "from_mere", "To the Glass Mere"},
        {'N', 36, "frost_glacier", "from_barrows", "from_glacier", "To the Rimefall Glacier"},
    };
    const auto roads = plat::Roads(exits, {36.0f, 30.0f}, 10151u);
    plat::Frame(m, exits, roads, [](int cx, int cy, float gap) {
        const float v = Fbm(cx * 0.14f, cy * 0.14f, 10252);
        return VariantOf(gap > 3.0f && v > 0.64f ? "frozen_turf" : "frost_heath", cx, cy);
    }, "frost_road");
    Rim(m, exits, 10353u);
    m.Spawn("default", (W - 5) * CELL + 16, 28 * CELL + 16);

    // The barrows: long mounds, sealed, with their stones, in two rows either
    // side of the road north -- and rings of runestones between.
    const int mounds[][2] = {{14, 10}, {24, 8}, {50, 9}, {60, 13}, {12, 42}, {24, 46}, {52, 44}, {62, 40}};
    for (const auto& b : mounds) Barrow(m, b[0] * CELL + 16, b[1] * CELL + 16);
    for (const auto& ring : {std::pair<int, int>{16, 26}, {56, 26}})
        for (int k = 0; k < 5; ++k) {
            const float a = k * 6.2831853f / 5.0f + 0.3f;
            plat::Stand(m, "runestone", static_cast<int>((ring.first + cosf(a) * 3.5f) * CELL) + 16,
                        static_cast<int>((ring.second + sinf(a) * 2.8f) * CELL) + 16, 26, 10);
        }
    PlaceChest(m, "chest_barrows_ring", 16 * CELL + 16, 26 * CELL + 16, "chest_frostreach");
    Snowman(m, 42 * CELL + 16, 22 * CELL + 16);
    Snowman(m, 30 * CELL + 16, 36 * CELL + 16);
    plat::Scatter(roads, 10454u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        for (const auto& b : mounds) if (abs(cx - b[0]) < 4 && abs(cy - b[1]) < 3) return;
        if (abs(cx - 16) < 5 && abs(cy - 26) < 4) return;
        if (abs(cx - 56) < 5 && abs(cy - 26) < 4) return;
        if (r < 0.018f) { plat::Stand(m, "snow_pine", x, y, 20, 8); return; }
        if (r < 0.026f) { plat::Stand(m, "runestone", x, y, 26, 10); return; }
        if (r < 0.034f) { plat::Stand(m, "ice_crystal", x, y, 16, 6); return; }
        if (r < 0.044f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    Brazier(m, "barrows_brazier_w", 16 * CELL, 30 * CELL);
    Brazier(m, "barrows_brazier_e", 56 * CELL, 30 * CELL);
    plat::Sign(m, "sign_frost_barrows", (W - 7) * CELL, 25 * CELL, "A stone with a rune cut in it",
               "THE FROSTREACH\n\nThe mountain's old dead were laid in these mounds with their swords, and they "
               "do not stay in them. West, the Glass Mere, and the trapper's house in the middle of it. North, the "
               "glacier and its trolls; past that, the Howe, where the dead have lords.\n\n"
               "Under it, in charcoal: SIXTY. AND SOMETHING BIG AND WHITE THAT WALKS.");

    plat::Posts(m, roads, 8, 10555u, [&](int cx, int cy, float r) -> Kind {
        for (const auto& b : mounds) if (abs(cx - b[0]) < 4 && abs(cy - b[1]) < 3) return {};
        if (r < 0.30f) return {"draugr", 62 + static_cast<int>(r * 100) % 2};
        if (r < 0.46f) return {"draugr_archer", 64};
        if (r < 0.64f) return {"ice_troll", 60 + static_cast<int>(r * 100) % 3};
        if (r < 0.78f) return {"greatwolf", 60 + static_cast<int>(r * 100) % 3};
        return {};
    });
    // Rime beetles on the frozen turf between the mounds.
    Beetles(m, roads, 7401u, 6, [&](int cx, int cy) {
        for (const auto& b : mounds) if (abs(cx - b[0]) < 5 && abs(cy - b[1]) < 4) return false;
        return !(abs(cx - 16) < 6 && abs(cy - 26) < 5) && !(abs(cx - 56) < 6 && abs(cy - 26) < 5);
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Glass Mere: a frozen lake, and a house in the middle of it -----------------------
static void BuildFrostMere() {
    using namespace frost;
    MapBuilder m("frost_mere", "The Glass Mere", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("The Frostreach: the ice bears a walker");
    m.Background(196, 214, 226);
    m.Fog(0.12f, 0.4f, {220, 232, 244});
    const vector<Exit> exits = {
        {'E', 28, "frost_barrows", "from_mere", "from_barrows", "To the Draugr Barrows"},
        {'N', 36, "frost_howe", "from_mere", "from_howe", "To the Warlord's Howe"},
    };
    // The roads keep to the north and east shores and meet in the north-east;
    // the lake has the rest.
    const auto roads = plat::Roads(exits, {58.0f, 11.0f}, 10161u);
    const float lx = 30.0f, ly = 32.0f, lrx = 21.0f, lry = 16.0f;       // the lake
    const float ix = 30.0f, iy = 32.0f, irx = 4.6f, iry = 3.6f;         // the islet in it
    const auto ell = [](float cx, float cy, float x, float y, float rx, float ry) {
        const float dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy;
    };
    const auto lake = [&](int cx, int cy) {
        const float wob = (Fbm(cx * 0.2f, cy * 0.2f, 10262) - 0.5f) * 0.25f;
        return ell(lx, ly, cx + 0.5f, cy + 0.5f, lrx, lry) < 1.0f + wob && ell(ix, iy, cx + 0.5f, cy + 0.5f, irx, iry) > 1.0f;
    };
    const auto islet = [&](int cx, int cy) { return ell(ix, iy, cx + 0.5f, cy + 0.5f, irx, iry) <= 1.0f; };
    const auto weak = [&](int cx, int cy) { return lake(cx, cy) && Fbm(cx * 0.16f, cy * 0.16f, 10263) > 0.60f; };
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (lake(cx, cy)) return VariantOf(weak(cx, cy) ? "lake_ice_dark" : "lake_ice", cx, cy);
        if (islet(cx, cy)) return VariantOf("rime_stone", cx, cy);
        const float v = Fbm(cx * 0.15f, cy * 0.15f, 10264);
        return VariantOf(v > 0.66f ? "frozen_turf" : "snow", cx, cy);
    }, "frost_road");
    // The ice: all of the lake, a row at a time, and the dark patches over it
    // again -- weaker, so they strain under a walker too.
    for (int cy = 1; cy < H - 1; ++cy)
        for (int pass = 0; pass < 2; ++pass) {
            int run = -1;
            for (int cx = 1; cx <= W - 1; ++cx) {
                const bool in = cx < W - 1 && (pass == 0 ? lake(cx, cy) : weak(cx, cy));
                if (in && run < 0) run = cx;
                if (!in && run >= 0) {
                    m.ThinIce(run * CELL, cy * CELL, (cx - run) * CELL, CELL, pass == 0 ? 0.0f : 0.18f);
                    run = -1;
                }
            }
        }
    Rim(m, exits, 10365u);
    m.Spawn("default", (W - 5) * CELL + 16, 28 * CELL + 16);

    // --- the cabin, on its islet ------------------------------------------------------------
    const int hx = static_cast<int>(ix * CELL), hy = static_cast<int>((iy + 1.6f) * CELL);
    // Its walls are 134 across with the log-ends, from 35 to 87 above where it
    // stands; the door is in the middle of them, its step in front.
    m.Prop("props", "trapper_cabin", hx, hy);
    m.Collision(hx - 67, hy - 87, 134, 52);
    m.Portal(hx - 18, hy - 44, 36, 18, "frost_cabin", "entrance", "Go into the cabin", true);
    m.Spawn("from_cabin", hx, hy + 22);
    Solid(m, "woodpile", hx + 100, hy - 30, 55, 5, 14);
    Solid(m, "pelt_rack", hx - 104, hy - 34, 61, 5, 4);
    // The trapper's holes in the ice round it, where the fish were.
    for (const auto& h : {std::pair<int, int>{-190, 60}, {170, 80}, {40, 150}, {-80, 190}})
        Solid(m, "ice_hole", hx + h.first, hy + h.second, 30, 8, 26);

    // --- the hunters' camp on the west shore, long left ----------------------------------------
    const int campx = 4 * CELL, campy = 12 * CELL;
    for (const auto& g : {std::pair<int, int>{0, 0}, {120, -40}, {60, 90}})
        Igloo(m, campx + 60 + g.first, campy + g.second);
    {
        json& fire = m.Object("range_mere_camp", "range", campx + 170, campy + 60);
        fire["sprite"] = "assets/props/campfire_ring.png";
        fire["title"]  = "A cold fire, easily lit";
        m.Collision(campx + 154, campy + 50, 32, 10);
    }
    Solid(m, "broken_sled", campx + 230, campy + 10, 54, 13, 14);
    Snowman(m, campx + 240, campy + 100);
    Snowman(m, campx + 30, campy + 130);
    plat::Sign(m, "sign_frost_mere", 54 * CELL, 17 * CELL, "A board nailed to a stake at the water's edge",
               "THE GLASS MERE\n\nThe ice bears a walker. It does not bear a runner: run on it and it cracks behind "
               "you, and if you keep running it lets you in. The dark patches are worse; mind them walking too.\n\n"
               "The house in the middle was Old Harl's. He walked out to it every winter for forty years. Walk.");

    plat::Scatter(roads, 10466u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (lake(cx, cy) || islet(cx, cy)) return;
        bool shore = false;
        for (int dy = -2; dy <= 2 && !shore; ++dy)
            for (int dx = -2; dx <= 2 && !shore; ++dx) shore = lake(cx + dx, cy + dy);
        if (shore) return;
        if (cx < 14 && cy > 6 && cy < 20) return;                 // the camp
        if (r < 0.024f) { plat::Stand(m, "snow_pine", x, y, 20, 8); return; }
        if (r < 0.032f) { plat::Stand(m, "ice_crystal", x, y, 16, 6); return; }
        if (r < 0.042f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });

    // --- who is here: the shores, and the cabin's keepers --------------------------------------
    // Two draugr bowmen on the islet, who will shoot at anyone coming across
    // the ice -- and nothing to do about it but walk.
    for (int s : {-1, 1})
        m.Enemy("draugr_archer", hx + s * 72, hy + 44, SpawnToShow("draugr_archer", 65), 90.0f, 160.0f);
    plat::Posts(m, roads, 8, 10567u, [&](int cx, int cy, float r) -> Kind {
        if (lake(cx, cy) || islet(cx, cy) || lake(cx + 1, cy) || lake(cx - 1, cy)) return {};
        if (cx < 14 && cy > 6 && cy < 20) return {};
        if (r < 0.28f) return {"ice_troll", 62 + static_cast<int>(r * 100) % 4};
        if (r < 0.44f) return {"frostback_troll", 67 + static_cast<int>(r * 100) % 2};
        if (r < 0.62f) return {"draugr", 62 + static_cast<int>(r * 100) % 3};
        if (r < 0.76f) return {"greatwolf", 62 + static_cast<int>(r * 100) % 3};
        return {};
    });
    // Rime beetles along the frozen shore, a cell to three off the lake's ice --
    // not out on it, where somebody stooping after one is somebody standing
    // still on thin ice. All the way round: the shore is the way round here,
    // roads or none.
    Beetles(m, roads, 7411u, 7, [&](int cx, int cy) {
        if (lake(cx, cy) || islet(cx, cy) || (cx < 16 && cy > 4 && cy < 22)) return false;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
                if (lake(cx + dx, cy + dy)) return true;
        return false;
    }, 40.0f);
    // The rare thing that walks round the lake, some days: on the shore, all the way round.
    {
        vector<std::array<int, 2>> loop;
        for (int k = 0; k < 16; ++k) {
            const float a = k * 6.2831853f / 16.0f;
            loop.push_back({static_cast<int>(lx + cosf(a) * (lrx + 3.5f)), static_cast<int>(ly + sinf(a) * (lry + 3.0f))});
        }
        RoamOn(m, {"abominable_snowman"}, 0, 0, 0.15f, loop);
    }
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !lake(cx, cy) && !islet(cx, cy) && !(cx < 14 && cy > 6 && cy < 20) &&
               wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Rimefall Glacier: the trolls' ice ----------------------------------------------------
static void BuildFrostGlacier() {
    using namespace frost;
    MapBuilder m("frost_glacier", "The Rimefall Glacier", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("The Frostreach: the trolls' ice, split with blue");
    m.Background(214, 232, 242);
    m.Fog(0.14f, 0.0f, {232, 242, 250});
    const vector<Exit> exits = {
        {'S', 36, "frost_barrows", "from_glacier", "from_barrows", "To the Draugr Barrows"},
        {'W', 28, "frost_howe", "from_glacier", "from_howe", "To the Warlord's Howe"},
    };
    const auto roads = plat::Roads(exits, {36.0f, 28.0f}, 10171u);
    // Crevasses: long cracks of blue across the glacier, nothing crosses; laid
    // along wandering lines, never over a road.
    const auto crevasse = [&](int cx, int cy) {
        if (cx < 3 || cy < 3 || cx > W - 4 || cy > H - 4) return false;
        if (wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) < 3.5f) return false;
        if (abs(cx - 54) < 9 && abs(cy - 14) < 8) return false;          // the ring of snowmen
        const float v = Fbm(cx * 0.09f, cy * 0.09f, 10272);
        return fabsf(v - 0.5f) < 0.022f;
    };
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        if (crevasse(cx, cy)) return VariantOf("blue_ice", cx, cy);
        const float v = Fbm(cx * 0.2f, cy * 0.2f, 10273);
        return VariantOf(v > 0.64f ? "snow" : "glacier_ice", cx, cy);
    }, "frost_road");
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx)
            if (crevasse(cx, cy)) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
    Rim(m, exits, 10375u);
    m.Spawn("default", 36 * CELL + 16, (H - 4) * CELL + 16);

    // The snowmen nobody built, in a ring on the ice where the white thing sleeps.
    const int dx = 54, dy = 14;
    for (int k = 0; k < 7; ++k) {
        const float a = k * 6.2831853f / 7.0f;
        Snowman(m, static_cast<int>((dx + cosf(a) * 4.5f) * CELL), static_cast<int>((dy + sinf(a) * 3.5f) * CELL));
    }
    PlaceChest(m, "chest_glacier_ring", dx * CELL, dy * CELL, "chest_frostreach");
    plat::Scatter(roads, 10476u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (crevasse(cx, cy) || crevasse(cx, cy + 1) || crevasse(cx - 1, cy) || crevasse(cx + 1, cy)) return;
        if (abs(cx - dx) < 7 && abs(cy - dy) < 6) return;
        if (r < 0.020f) { plat::Stand(m, "ice_spire", x, y, 40, 14); return; }
        if (r < 0.036f) { plat::Stand(m, "ice_crystal", x, y, 16, 6); return; }
        if (r < 0.044f) { plat::Stand(m, "snow_pine", x, y, 20, 8); return; }
        if (r < 0.052f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });
    plat::Sign(m, "sign_frost_glacier", 39 * CELL, (H - 5) * CELL, "A troll's thighbone driven into the ice",
               "THE RIMEFALL GLACIER\n\nTrolls. The big ones grow ice out of their backs, and throw it.\n\n"
               "Somebody has scratched a tall shape with long arms under it, and a ring round it, and the word: "
               "RUN.");

    plat::Posts(m, roads, 8, 10577u, [&](int cx, int cy, float r) -> Kind {
        if (crevasse(cx, cy) || crevasse(cx + 1, cy) || crevasse(cx - 1, cy)) return {};
        if (abs(cx - dx) < 6 && abs(cy - dy) < 5) return {};
        if (r < 0.30f) return {"frostback_troll", 67 + static_cast<int>(r * 100) % 4};
        if (r < 0.52f) return {"ice_troll", 64 + static_cast<int>(r * 100) % 4};
        if (r < 0.66f) return {"wyvern", 64 + static_cast<int>(r * 100) % 4};
        if (r < 0.78f) return {"greatwolf", 64 + static_cast<int>(r * 100) % 3};
        return {};
    });
    // Rime beetles on the bare glacier ice, clear of the crevasses and of the
    // white thing's ring of snowmen.
    Beetles(m, roads, 7421u, 7, [&](int cx, int cy) {
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) if (crevasse(cx + ox, cy + oy)) return false;
        if (abs(cx - dx) < 8 && abs(cy - dy) < 7) return false;
        return Fbm(cx * 0.2f, cy * 0.2f, 10273) <= 0.64f;       // glacier ice, not the snow on it
    });
    // Some days, the white thing: along the trodden ways, since nothing else on
    // the glacier goes far in a line -- from the west road to the south and back.
    {
        const auto& south = roads[0];
        const auto& west = roads[1];
        vector<std::array<int, 2>> loop;
        for (const wold::Pt* p : {&west[1], &west[2], &west[3], &south[2], &south[1], &south[2], &west[3], &west[2]})
            loop.push_back({static_cast<int>(p->x), static_cast<int>(p->y)});
        RoamOn(m, {"abominable_snowman"}, 0, 0, 0.2f, loop);
    }
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !crevasse(cx, cy) && wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Warlord's Howe: where the dead have lords ---------------------------------------------
static void BuildFrostHowe() {
    using namespace frost;
    MapBuilder m("frost_howe", "The Warlord's Howe", W * CELL, H * CELL);
    m.Ambient("snow");
    m.Subtitle("The Frostreach: the barrow of the barrow-kings");
    m.Background(170, 180, 194);
    m.Fog(0.12f, 0.0f, {206, 216, 228});
    const vector<Exit> exits = {
        {'E', 28, "frost_glacier", "from_howe", "from_glacier", "To the Rimefall Glacier"},
        {'S', 36, "frost_mere", "from_howe", "from_mere", "To the Glass Mere"},
    };
    const int hx = 36 * CELL + 16, hy = 17 * CELL;
    const auto roads = plat::Roads(exits, {36.0f, 26.0f}, 10181u);
    plat::Frame(m, exits, roads, [&](int cx, int cy, float gap) {
        (void)gap;
        const float v = Fbm(cx * 0.16f, cy * 0.16f, 10282);
        return VariantOf(v > 0.62f ? "rime_stone" : (v > 0.56f ? "frozen_turf" : "snow"), cx, cy);
    }, "frost_road");
    // The road on up to the Howe's door.
    for (int cy = 17; cy <= 26; ++cy)
        for (int cx = 35; cx <= 37; ++cx) m.Ground(VariantOf("frost_road", cx, cy), cx * CELL, cy * CELL, CELL);
    Rim(m, exits, 10385u);
    m.Spawn("default", (W - 5) * CELL + 16, 28 * CELL + 16);

    // The Howe: its mound (307 across, from 61 to 199 above where it stands),
    // the dry-stone walls either side of its door, the standing stones and
    // braziers that flank the steps -- and the doorway, 58 wide in the middle,
    // its threshold 37 up at the top of the steps.
    m.Prop("props", "howe_hall", hx, hy);
    m.Collision(hx - 153, hy - 199, 306, 138);
    for (int s : {-1, 1}) {
        m.Collision(s < 0 ? hx - 143 : hx + 64, hy - 77, 79, 22);
        m.Collision(hx + s * 59 - 7, hy - 52, 14, 8);
        m.Collision(hx + s * 67 - 12, hy - 22, 24, 18);
    }
    m.Portal(hx - 24, hy - 52, 48, 22, "frost_howe_hall", "entrance", "Go down into the Howe", true);
    m.Danger(72);
    m.Spawn("from_howe_hall", hx, hy + 34);
    // The way up to it: runestones either side, pale fires between.
    for (int k = 0; k < 4; ++k)
        for (int s : {-1, 1}) {
            const int y = (19 + k * 2) * CELL;
            if (k % 2 == 0) plat::Stand(m, "runestone", hx + s * 96, y, 26, 10);
            else Brazier(m, "howe_brazier_" + std::to_string(k) + (s < 0 ? "w" : "e"), hx + s * 96, y);
        }
    const int mounds[][2] = {{12, 10}, {60, 10}, {10, 44}, {60, 46}, {20, 30}};
    for (const auto& b : mounds) Barrow(m, b[0] * CELL + 16, b[1] * CELL + 16);
    PlaceChest(m, "chest_howe_yard", (36 + 7) * CELL, 22 * CELL, "chest_frostreach");
    plat::Sign(m, "sign_frost_howe", 39 * CELL, 44 * CELL, "A spear stood up with a helm on it",
               "THE WARLORD'S HOWE\n\nThe barrow of the barrow-kings. The draugr below are theirs, and so are the "
               "ones in armour you will not get through quickly. Inside, their hall, and the eldest of them on his "
               "seat.\n\nSeventy, and more.");
    plat::Scatter(roads, 10486u, 3.0f, [&](int cx, int cy, int x, int y, float r, float gap) {
        (void)gap;
        if (abs(cx - 36) < 9 && cy < 28) return;
        for (const auto& b : mounds) if (abs(cx - b[0]) < 4 && abs(cy - b[1]) < 3) return;
        if (r < 0.020f) { plat::Stand(m, "snow_pine", x, y, 20, 8); return; }
        if (r < 0.030f) { plat::Stand(m, "runestone", x, y, 26, 10); return; }
        if (r < 0.040f) m.Prop("objects", kSmallRocks[static_cast<int>(r * 1000) % 4], x, y);
    });

    // Warlords before the door, and their dead about the barrows.
    const int guards[][3] = {{31, 21, 71}, {41, 21, 72}, {33, 25, 73}};
    for (const auto& g : guards)
        m.Enemy("undead_warlord", g[0] * CELL + 16, g[1] * CELL + 16, SpawnToShow("undead_warlord", g[2]), 120.0f, 220.0f);
    plat::Posts(m, roads, 8, 10587u, [&](int cx, int cy, float r) -> Kind {
        if (abs(cx - 36) < 9 && cy < 28) return {};
        for (const auto& b : mounds) if (abs(cx - b[0]) < 4 && abs(cy - b[1]) < 3) return {};
        if (r < 0.26f) return {"undead_warlord", 71 + static_cast<int>(r * 100) % 3};
        if (r < 0.46f) return {"draugr", 68 + static_cast<int>(r * 100) % 3};
        if (r < 0.62f) return {"draugr_archer", 68 + static_cast<int>(r * 100) % 3};
        if (r < 0.76f) return {"frostback_troll", 68 + static_cast<int>(r * 100) % 3};
        return {};
    });
    // Rime beetles on the frost-bitten stone about the barrows, out of the Howe's yard.
    Beetles(m, roads, 7431u, 6, [&](int cx, int cy) {
        if (abs(cx - 36) < 11 && cy < 30) return false;
        for (const auto& b : mounds) if (abs(cx - b[0]) < 5 && abs(cy - b[1]) < 4) return false;
        return true;
    });
    PlaceRoamers(m);
    PlaceNightVisitors(m, kNights, 7, [&](int cx, int cy) -> bool {
        return !(abs(cx - 36) < 10 && cy < 29) && wold::Gap(roads, static_cast<float>(cx), static_cast<float>(cy)) > 4.0f;
    }, 10);
    m.Write("maps");
}

// --- the Howe itself ---------------------------------------------------------------------------
static void BuildFrostHoweHall() {
    const int CELL = 32, cols = 26, rows = 30, back = 3;
    MapBuilder m("frost_howe_hall", "The Howe", cols * CELL, rows * CELL);
    m.Interior(true);
    m.Subtitle("Under the Warlord's Howe");
    m.Background(10, 12, 16);
    const int door0 = 12, door1 = 13;
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const bool backwall = cy < back, front = cy == rows - 1, west = cx == 0, east = cx == cols - 1;
            const bool gap = front && cx >= door0 && cx <= door1;
            const bool solid = (backwall || front || west || east) && !gap;
            string tile;
            if (solid) tile = backwall && !west && !east ? (cy == back - 1 ? VariantOf("howe_wall", cx, cy)
                                                                            : string("howe_wallface"))
                                                         : string("howe_walltop");
            else tile = VariantOf((cx >= 11 && cx <= 14) ? "howe_floor_dark" : "howe_floor", cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (solid) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }
    m.Portal(door0 * CELL, rows * CELL - 24, (door1 - door0 + 1) * CELL, 24, "frost_howe", "from_howe_hall",
             "Climb out of the Howe", false);
    m.Spawn("entrance", 13 * CELL, (rows - 3) * CELL);
    m.Spawn("default",  13 * CELL, (rows - 3) * CELL);
    // The barrow-kings laid down both sides of the hall, and the eldest's seat at its head.
    for (int cy : {11, 16, 21})
        for (int cx : {5, 21}) {
            frost::Solid(m, "stone_coffin", cx * CELL, cy * CELL + 16, 76, 6, 25);
        }
    frost::Solid(m, "draugr_throne", 13 * CELL, 6 * CELL, 86, 11, 32);
    int lamp = 0;
    for (int cy : {9, 14, 19, 24})
        for (int cx : {2, 24}) {
            json& o = m.Object("howe_brazier_" + std::to_string(lamp++), "lamp", cx * CELL, cy * CELL + 16);
            o["sprite"] = "assets/props/frost_brazier.png";
            m.Collision(cx * CELL - 12, cy * CELL + 8, 24, 8);
        }
    for (int cx : {7, 19}) {
        m.Prop("props", "runestone", cx * CELL, 5 * CELL);
        m.Collision(cx * CELL - 13, 5 * CELL - 10, 26, 10);
    }
    PlaceChest(m, "chest_howe", 21 * CELL, 5 * CELL, "chest_howe");
    // The eldest on his seat's step, his warlords down the hall, and their dead.
    const int hall[][4] = {{13, 8, 75, 0}, {9, 13, 73, 0}, {17, 13, 73, 0}, {9, 19, 72, 1}, {17, 19, 72, 2},
                           {13, 23, 72, 1}};
    for (const auto& h : hall) {
        const char* type = h[3] == 0 ? "undead_warlord" : (h[3] == 1 ? "draugr" : "draugr_archer");
        m.Enemy(type, h[0] * CELL, h[1] * CELL, SpawnToShow(type, h[2]), 150.0f, 200.0f);
    }
    m.Write("maps");
}

// --- the trapper's cabin -------------------------------------------------------------------
// Old Harl's: a hearth, a bed, a table, his pelts, his journal -- and the
// chest he kept everything worth keeping in, for whoever walked out to it.
static void BuildFrostCabin() {
    const int CELL = 32, cols = 14, rows = 11;
    MapBuilder m("frost_cabin", "Old Harl's Cabin", cols * CELL, rows * CELL);
    m.Interior(true);
    m.Subtitle("Out on the ice, and warm");
    m.Background(20, 16, 14);
    RoomShell(m, cols, rows, CELL, "plank_floor", "log_wall", cols / 2 - 1, cols / 2);
    const int dx = (cols / 2) * CELL;
    m.Spawn("entrance", dx, (rows - 2) * CELL);
    m.Spawn("default",  dx, (rows - 2) * CELL);
    m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "frost_mere", "from_cabin", "Back out onto the ice", false);
    {
        json& o = m.Object("range_harl", "range", 3 * CELL, 3 * CELL + 10);
        o["sprite"] = "assets/props/cottage_hearth.png";
        o["title"]  = "Harl's hearth";
        m.Collision(3 * CELL - 32, 3 * CELL - 16, 64, 26);
    }
    PlaceBed(m, "bed_harl", "bed_single", 11 * CELL + 8, 5 * CELL, 30, 36);
    m.Prop("props", "table_round", 6 * CELL, 6 * CELL + 8);
    m.Collision(6 * CELL - 20, 6 * CELL - 4, 40, 12);
    m.Prop("props", "tavern_chair", 4 * CELL + 16, 6 * CELL + 10);
    frost::Solid(m, "pelt_rack", 8 * CELL, 3 * CELL + 10, 61, 5, 4);
    PlaceChest(m, "chest_trapper", 11 * CELL + 8, 8 * CELL, "chest_trapper");
    {
        json& o = m.Object("journal_harl", "sign", 3 * CELL + 16, 8 * CELL);
        o["sprite"] = "assets/props/lectern.png";
        o["title"]  = "Harl's journal";
        o["text"]   = "Fortieth winter on the Mere.\n\nThe dead are up on the heath again, and the Howe's lords walk "
                      "further every year. The trolls took the east shore. The white one came round the lake twice "
                      "this month; it does not come onto the ice. Nothing does, that knows it.\n\nThe ice will "
                      "carry anyone who walks. I have told every fool who ran at it, and pulled out most of them.\n\n"
                      "What I have is in the chest. I will not need it where I am going.";
        m.Collision(3 * CELL + 2, 8 * CELL - 10, 28, 10);
    }
    m.Write("maps");
}

static void BuildFrostreach() {
    BuildFrostBarrows();
    BuildFrostMere();
    BuildFrostGlacier();
    BuildFrostHowe();
    BuildFrostHoweHall();
    BuildFrostCabin();
}

// --- main --------------------------------------------------------------------

int main() {
    std::printf("genmaps: building the Hollowmarch\n");
    g_manifest.Load("data/asset_manifest.json");
    LoadHerbs();
    LoadBugs();

    BuildOverworld();
    BuildTown();
    BuildInteriors();
    BuildWhisperwood();
    BuildWestwold();
    BuildBrackenwood();
    BuildMossvale();
    BuildFernhollow();
    BuildWoodlandInteriors();
    BuildCollege();
    BuildDreamworld();
    BuildDreamDeep();
    BuildDreamDark();
    BuildIceSpire();
    BuildAshenPath();
    BuildBrimstonePalace();
    BuildPurgatoryPlateau();
    BuildBayou();
    BuildHexmire();
    BuildFrostreach();

    BuildDungeon("dungeon_emberfell_1", "Emberfell Mine, Upper Workings",
                 1001u, 60, 46, 9,
                 "dungeon_floor", "dungeon_wall",
                 "overworld", "from_mine",
                 {{"orc1", 3}, {"orc_slinger", 3}, {"orc1", 4}, {"orc2", 5}},
                 "chest_dungeon", 3,
                 "chest_emberfell_key", "key_emberfell",
                 "dungeon_emberfell_2", "rusted_key",
                 "", 1,
                 {{"iron_ore", 10}, {"coal", 20}});

    BuildDungeon("dungeon_emberfell_2", "Emberfell Mine, Lower Workings",
                 1002u, 54, 42, 8,
                 "dungeon_floor", "dungeon_wall",
                 "dungeon_emberfell_1", "from_below",
                 {{"orc2", 7}, {"orc_bowman", 7}, {"orc2", 9}, {"orc1", 6}},
                 "chest_dungeon", 3,
                 "", "",
                 "", "",
                 // Level 1: the Warchief's own stat block is the whole of him
                 // now, the way every other boss works. Spawning him at 12
                 // added eleven levels to a block that was already a boss's
                 // and put him at an effective 41, in a mine whose orcs top
                 // out at 20.
                 "orc3", 1,
                 {{"damascus_ore", 40}, {"platinum_ore", 70}, {"coal", 20}});

    // The barrow also holds the drowned king's chest, for anyone Orlend has
    // sent back down for it.
    BuildDungeon("dungeon_barrow", "The Barrow Beneath the Mire",
                 2001u, 52, 40, 8,
                 "dungeon_floor", "dungeon_wall",
                 "overworld", "from_barrow",
                 {{"orc1", 6}, {"orc_slinger", 6}, {"orc2", 8}},
                 "chest_barrow", 3,
                 "chest_barrow_seal", "seal_barrow",
                 "", "", "", 1,
                 {{"diamond_ore", 60}, {"azuryte_ore", 30}}, 0,
                 "chest_barrow_hoard", "drowned_king_boots", "q_drowned_hoard");

    // Hollowrest Crypt: three floors under the mausoleum at the head of the
    // burying ground, and the reason the sign at the gate says to shut the
    // gate. Each floor is worse than the one over it -- the vaults where the
    // family's own are, the ossuary where everybody else's bones were stacked
    // when the vaults filled, and under both of those a room nobody in
    // Havenbrook admits was dug -- and each has chests that are worth the walk
    // down. Nothing is locked: the way down is fighting, not fetching a key.
    BuildDungeon("crypt_1", "Hollowrest Crypt, the Vaults",
                 3101u, 48, 38, 8,
                 "cellar_floor", "dungeon_wall",
                 "overworld", "from_crypt",
                 {{"grave_ghoul", 1}, {"bone_archer", 1}, {"grave_ghoul", 3}, {"cryptbound", 1}},
                 "chest_crypt_1", 3,
                 "", "",
                 "crypt_2", "",
                 "", 1);
    BuildDungeon("crypt_2", "Hollowrest Crypt, the Ossuary",
                 3102u, 54, 42, 9,
                 "dungeon_floor", "dungeon_wall",
                 "crypt_1", "from_below",
                 {{"bone_knight", 1}, {"plague_corpse", 1}, {"tomb_shade", 1}, {"grave_hound", 1},
                  {"bone_knight", 3}},
                 "chest_crypt_2", 3,
                 "", "",
                 "crypt_3", "",
                 "", 1,
                 {{"azuryte_ore", 30}, {"diamond_ore", 60}});
    BuildDungeon("crypt_3", "Hollowrest Crypt, the Black Vault",
                 3103u, 58, 44, 9,
                 "dungeon_floor", "dungeon_wall",
                 "crypt_2", "from_below",
                 {{"blood_thrall", 1}, {"bone_colossus", 1}, {"nosferatu", 1},
                  {"blood_thrall", 4}, {"crypt_warden", 1}},
                 "chest_crypt_3", 4,
                 "", "",
                 "", "",
                 "vampire_lord", 1,
                 {{"diamond_ore", 60}, {"platinum_ore", 70}}, 0,
                 "chest_ashcroft", "ashcroft_signet", "");

    // The well under Havenbrook: two dark floors, four chambers to a floor.
    BuildWellFloor("well_shallow", "The Well, Upper Workings",
                   "Cut by the old well-crews, and dark as the inside of a boot",
                   5101u, 92, 72, "cellar_floor", "dungeon_wall",
                   "town_havenbrook", "from_well", "well_deep",
                   {{{"slime", 1}, {"slime", 2}}, {{"rat", 2}, {"rat", 3}},
                    {{"bat", 1}, {"bat", 2}}, {{"slime", 2}, {"bat", 2}, {"rat", 3}}},
                   7, false);
    BuildWellFloor("well_deep", "The Well, the Deep Cut",
                   "Where the water was, and what is in the way of it",
                   5102u, 96, 78, "dungeon_floor", "dungeon_wall",
                   "well_shallow", "from_below", "",
                   {{{"hound", 1}, {"hound", 2}}, {{"ankou", 1}}, {{"banshee", 1}, {"banshee", 2}},
                    {{"hound", 2}, {"banshee", 1}, {"ankou", 1}}},
                   5, true);

    // The Infernal Pit, at the end of the Ashen Path: imps and demons, lava vents
    // in the floors, and the Pit Lord in the last room.
    BuildDungeon("dungeon_infernal", "The Infernal Pit",
                 6661u, 64, 50, 10,
                 "hell_floor", "hell_wall",
                 "ashen_path", "from_pit",
                 {{"imp", 3}, {"imp", 5}, {"demon", 2}, {"demon", 4}},
                 "chest_infernal", 3,
                 "", "", "", "",
                 "pit_lord", 1,
                 {{"demonite_ore", 80}, {"platinum_ore", 70}},
                 26);

    FlushWorldMap();
    if (!g_bugs_missing.empty()) {
        for (const string& bug : g_bugs_missing)
            std::printf("genmaps: %s was placed as a bug, and has no catch block in data/items.json\n", bug.c_str());
        return 1;
    }
    if (!g_manifest.unsized.empty()) {
        for (const string& key : g_manifest.unsized)
            std::printf("genmaps: no size for assets/%s.png -- it was placed at 32x32\n", key.c_str());
        std::printf("genmaps: run tools/make_manifest.ps1, then build the maps again\n");
        return 1;
    }
    std::printf("genmaps: done\n");
    return 0;
}
