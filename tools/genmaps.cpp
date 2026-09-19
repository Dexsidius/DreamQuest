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

    void Spawn(const string& name, int x, int y) {
        dq["spawns"][name] = json::array({x + ox, y});
    }

    // True when a player standing with their feet at (x, y) touches none of
    // the collision placed so far. The foot box matches the self-test's.
    bool Clear(int x, int y) const {
        x += ox;
        for (const auto& c : dq["collision"]) {
            const int cx = c[0], cy = c[1], cw = c[2], ch = c[3];
            if (x - 8 < cx + cw && cx < x + 8 && y - 10 < cy + ch && cy < y) return false;
        }
        return true;
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

    // Returns the NPC so a trader can be given its shop: m.Npc(...)["shop"] = id.
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
    void Elevation(int cell, int cols, int rows,
                   const vector<int>& levels, const vector<Rect4>& ramps) {
        json e;
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

    void Interior(bool v) { dq["interior"] = v; }
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
    o["yield_xp"]    = big ? 65 : 25;
    o["gather_time"] = big ? 3.0f : 2.2f;
    o["title"]       = big ? "oak" : "sapling";
    // Not for ever: on each log there is a chance the tree comes down, and
    // then a stump stands there for a while. A sapling goes sooner than an
    // oak and is back sooner.
    o["deplete"]     = big ? 0.125f : 0.25f;
    o["regrow"]      = big ? 1.5f : 1.0f;
    o["sprite_open"] = ObjPath(big ? "stump" : "stumpsmall");

    m.Collision(x - 9, y - 9, 18, 9);
}

static void PlaceRock(MapBuilder& m, std::mt19937& rng, int index,
                      int x, int y, bool big, int level, const string& yield) {
    const string art = big ? Pick(kRocks, rng) : Pick(kSmallRocks, rng);

    json& o = m.Object("rock_" + std::to_string(index), "rock", x, y);
    o["sprite"]      = ObjPath(art);
    o["skill"]       = "Mining";
    o["skill_level"] = level;
    o["yield"]       = yield;
    // Deeper ore is slower to work and worth more for it.
    o["yield_xp"]    = static_cast<int>((big ? 60 : 24) * (1.0f + level / 12.0f));
    o["gather_time"] = (big ? 3.2f : 2.4f) + level * 0.02f;
    // Named for the ore, because every ore uses the same rock art: a plain
    // "outcrop" left a new miner walking up to iron they could not touch with
    // nothing to say which of the rocks around it was copper.
    static const std::map<string, string> kOre = {
        {"copper_ore", "copper"}, {"iron_ore", "iron"}, {"coal", "coal"},
        {"azuryte_ore", "azuryte"}, {"damascus_ore", "damascus"},
        {"orichalcum_ore", "orichalcum"},
        {"diamond_ore", "diamond"}, {"platinum_ore", "platinum"}, {"demonite_ore", "demonite"}};
    const auto name = kOre.find(yield);
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

// A bed anyone may sleep in after dusk. Drawn from the same prop art as the
// rest of the furniture, but placed as an object so it can be used.
static void PlaceBed(MapBuilder& m, const string& bed_id, const string& art,
                     int x, int y, int cw, int ch) {
    json& o = m.Object(bed_id, "bed", x, y);
    o["sprite"] = "assets/props/" + art + ".png";
    o["title"]  = (art == "bed_double") ? "Double bed" : "Bed";
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
    // Game hours to grow back: a marigold in three, a starlily in nearly nine.
    o["regrow"]      = 3.0f + it->second.level / 12.0f;
    string title = it->second.name;
    for (char& c : title) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    o["title"]       = title;
}

// A cauldron to brew at.
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
                if (r < 0.06f)       { m.Enemy("orc1", x, y, 2); ++spawned; }
                else if (r < 0.08f)  { m.Enemy("orc2", x, y, 4); ++spawned; }
            } else if (b == MIRE) {
                // The swamp belongs to the lizardmen now.
                if (fabsf(cx - MIRE_CAMP_X) <= 7 && fabsf(cy - MIRE_CAMP_Y) <= 7) continue;
                if (r < 0.055f)      { m.Enemy("lizardman", x, y, 1 + static_cast<int>(Hash2(cx, cy, 99) * 3)); ++spawned; }
                else if (r < 0.065f) { m.Enemy("orc1", x, y, 5); ++spawned; }
            } else if (b == CURSED) {
                if (r < 0.09f)       { m.Enemy("orc2", x, y, 8); ++spawned; }
            }

            // The Sunken Road is where the orc contract is actually filled --
            // and, under the trees along it, where the highwaymen wait.
            if (b != WATER && road_gap > 2.0f && road_gap < 6.0f && cy > 24 && cy < 74) {
                if (r > 0.90f) { m.Enemy("orc1", x, y, 3, 24.0f, 200.0f); ++spawned; }
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
        m.Danger(6);
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
        m.Danger(10);
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
        {
            const auto [cx0, cy0] = at(GRAVE_CX, GRAVE_CY + 8, 16, 30);
            m.Prop("props", "crypt", cx0, cy0);
            m.SortLift("crypt", 74);
            m.Collision(cx0 - 78, cy0 - 40, 156, 44);
            PlaceChest(m, "chest_hollowrest", cx0 - 132, cy0 - 26, "chest_hollowrest");
            // In the aisle in front of its own door, not behind the crypt:
            // south of it is outside the fence.
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

    m.Write("maps");
    WriteWorldMap("data", (OW_W - OW_X0) * OW_CELL, OW_PX_H, m.ox);
}

// --- town --------------------------------------------------------------------

static void BuildTown() {
    const int CELL = 32, W = 56, H = 44;
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

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.25f, cy * 0.25f, 77);
            // A crossroads through the middle of the village.
            const bool on_road = (abs(cy - 22) <= 1) || (abs(cx - 28) <= 1);
            string tile = on_road ? VariantOf("road", cx, cy)
                        : (v > 0.6f ? "grass_light" : (v > 0.3f ? "grass" : "grass_olive"));
            if (in_pit(cx, cy))   tile = v > 0.6f ? "sand" : (v > 0.3f ? "dirt" : "dirt_dark");
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
        PlaceBed(m, "bed_inn_1", "bed_single", 150, 250, 30, 36);
        piece("nightstand",   178, 214, 18, 8);
        piece("wardrobe",     258, 246, 34, 14);
        piece("washstand",    258, 330, 22, 8);
        piece("travel_chest", 150, 292, 28, 12);

        // Room two: the good room.
        m.Overlay("props", "inn_rug", 400, 326);
        PlaceBed(m, "bed_inn_2", "bed_double", 356, 258, 48, 36);
        piece("nightstand",   448, 214, 18, 8);
        piece("washstand",    452, 322, 22, 8);
        piece("travel_chest", 356, 300, 28, 12);

        // Room three: another single.
        PlaceBed(m, "bed_inn_3", "bed_single", 556, 250, 30, 36);
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
static bool Open(int cx, int cy) {
    if (cx < 1 || cy < 1 || cx >= W - 1 || cy >= H) return false;
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
                // The exit at the foot of the track stays open.
                const bool exit = cy == H - 1 && Gap(cx, cy) < 3.0f;
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
// =============================================================================

namespace ash {
static const int CELL = 32, W = 96, H = 40;
static float TrailY(float cx) { return 20.0f + sinf(cx * 0.07f) * 7.0f + sinf(cx * 0.023f + 2.0f) * 3.0f; }
static float LavaX(int river, float cy) {
    static const float kX[] = {30.0f, 57.0f, 80.0f};
    return kX[river] + sinf(cy * 0.2f + river) * 2.0f;
}
static int RiverAt(int cx, int cy) {
    for (int i = 0; i < 3; ++i)
        if (fabsf(cx - LavaX(i, static_cast<float>(cy))) < 1.3f) return i;
    return -1;
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
            if (river >= 0) {
                tile = "lava";
                if (gap < 2.4f)
                    m.Hazard(cx * CELL, cy * CELL, CELL, CELL, 6.0f);     // the ford: passable, and it burns
                else
                    m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            } else if (gap < 1.4f) {
                tile = "ash";
            } else if (gap < 4.0f) {
                tile = v > 0.5f ? "ash" : "cinder";
            } else {
                tile = v > 0.6f ? "cursed_ground" : "cinder";
            }
            m.Ground(VariantOf(tile, cx, cy), cx * CELL, cy * CELL, CELL);

            // The edges of the world are cliffs, open only where the path leaves.
            const bool west_exit = cx == 0 && gap < 3.0f;
            const bool edge = cx == 0 || cy == 0 || cy == H - 1 || cx == W - 1;
            if (edge && !west_exit) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
        }

    // Burnt trees, black glass and ember vents off the path.
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            const float gap = fabsf(cy - TrailY(static_cast<float>(cx)));
            if (gap < 3.0f || RiverAt(cx, cy) >= 0 || RiverAt(cx + 1, cy) >= 0 || RiverAt(cx - 1, cy) >= 0) continue;
            if (cx > W - 12 && gap < 7.0f) continue;          // the gate's forecourt
            const float r = Hash2(cx, cy, 6363);
            const int x = cx * CELL + 16, y = cy * CELL + 24;
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

    m.Write("maps");
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

// A length of split-rail fence along a line of cells, with a gap left for a gate.
static void Fence(MapBuilder& m, int CELL, int cx0, int cx1, int cy, int gate_cx = -999) {
    for (int cx = cx0; cx < cx1; cx += 2) {
        if (abs(cx - gate_cx) <= 1) continue;
        const int x = cx * CELL + CELL, y = cy * CELL + 20;
        m.Prop("props", "rail_fence", x, y);
        m.Collision(x - 30, y - 8, 60, 8);
    }
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
        // Drying frames along the north side, hides on every one.
        for (int k = 0; k < 4; ++k) {
            const int x = sx - 170 + k * 84, y = sy - 96;
            m.Prop("props", "tanning_rack", x, y);
            m.Collision(x - 24, y - 10, 48, 10);
        }
        m.Prop("props", "tent", sx - 196, sy + 10);
        m.Collision(sx - 196 - 30, sy + 10 - 16, 60, 16);
        m.Prop("props", "tent", sx + 200, sy - 30);
        m.Collision(sx + 200 - 30, sy - 30 - 16, 60, 16);
        PlaceCampsite(m, "campsite_hidewater", sx + 200, sy + 40);

        // The tanner's bench, where hides become leathers.
        {
            json& o = m.Object("bench_hidewater", "workbench", sx - 60, sy + 6);
            o["sprite"]  = "assets/props/workbench.png";
            o["title"]   = "Tanner's bench";
            o["station"] = "workbench";
            m.Collision(sx - 60 - 34, sy + 6 - 18, 67, 18);
        }
        // The dye vat, and a fire to cook on.
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

    // A farmer on his round between the fields, by the clock: see Npc.
    {
        json& n = m.Npc("npc_aldous", "Farmer Aldous", "citizen2", 103 * CELL, 47 * CELL, "aldous_root", 0);
        n["path"] = json::array({json::array({103 * CELL, 47 * CELL, 12.0f, 3}),
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
        m.Prop("props", "tanning_rack", cx0 + 84, cy0 - 20);
        m.Collision(cx0 + 84 - 24, cy0 - 30, 48, 10);
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

    m.Write("maps");
}

// --- Mossvale ------------------------------------------------------------------

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
        if (abs(cy - 38) <= 1 && cx >= 11 && cx <= 26) return true;             // to the herbalist
        if (abs(cx - 26) <= 1 && cy >= sq_cy && cy <= 38) return true;
        return false;
    };
    // Where buildings stand, so the greenery keeps clear of them.
    auto reserved = [&](int cx, int cy) {
        if (cx >= 25 && cx <= 35 && cy >= 10 && cy <= 17) return true;   // lodge
        if (cx >= 8 && cx <= 16 && cy >= 30 && cy <= 38) return true;    // herbalist
        if (cx >= 42 && cx <= 50 && cy >= 33 && cy <= 39) return true;   // the tanner's
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
    auto on_path = [&](int cx, int cy) {
        if (cy >= 12 && fabsf(cx - path_x(static_cast<float>(cy))) < 1.2f) return true;   // from the gate
        if (cy >= 15 && cy <= 16 && cx >= gate_col && cx <= 23) return true;            // to the jetty
        return false;
    };
    auto reserved = [&](int cx, int cy) {
        if (cx >= 8 && cx <= 16 && cy >= 5 && cy <= 12) return true;     // cottage
        if (cx >= 3 && cx <= 10 && cy >= 21 && cy <= 28) return true;    // shrine
        if (cx >= 16 && cx <= 24 && cy >= 23 && cy <= 29) return true;   // camp
        if (cx >= 15 && cx <= 21 && cy >= 17 && cy <= 21) return true;   // Nell's cart
        if (cx >= 30 && cx <= 44 && cy >= 23 && cy <= 34) return true;   // the college, and its doorstep
        return false;
    };

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.24f, cy * 0.24f, 46);
            string tile;
            if (on_jetty(cx, cy))              tile = VariantOf("plank_floor", cx, cy);
            else if (in_pond(cx, cy))          tile = "water";
            else if (on_path(cx, cy))          tile = VariantOf(v > 0.6f ? "dirt_dark" : "dirt", cx, cy);
            else tile = VariantOf(v > 0.6f ? "grass_olive" : (v > 0.28f ? "grass" : "moss"), cx, cy);
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
            if (in_pond(cx, cy) && !on_jetty(cx, cy)) m.Collision(cx * CELL, cy * CELL, CELL, CELL);
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

    PlaceBuilding(m, "building_house_a", gate_col * CELL + 16, 11 * CELL, 136, 147,
                  "fernhollow_cottage", "entrance", "Enter the ferry cottage",
                  "from_fernhollow_cottage");

    // The mage college: a stone tower south-east of the pond, older than the
    // hamlet, where the ancient magic is taught. Its own model, in props.
    PlaceBuilding(m, "mage_college", 37 * CELL, 31 * CELL, 150, 176,
                  "fernhollow_college", "entrance", "Enter the college",
                  "from_fernhollow_college", "props");

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
        m.Overlay("props", "rug", dx, 6 * CELL + 16);
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
        piece("dining_table", 7 * CELL,      7 * CELL + 8,  46, 14);
        piece("tavern_chair", 5 * CELL + 16, 7 * CELL + 10, 16, 8);
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

    // The college's hall: a round of stone under the tower, the circle cut
    // into the floor at its middle, shelves of the library along the back
    // wall, the magister's lectern, and the copying room's chalk board.
    {
        const int CELL = 32, cols = 20, rows = 14;
        MapBuilder m("fernhollow_college", "The College", cols * CELL, rows * CELL);
        m.Interior(true);
        m.Subtitle("Older than the hamlet round it");
        m.Background(16, 14, 24);
        RoomShell(m, cols, rows, CELL, "cellar_floor", "forge_wall", cols / 2 - 1, cols / 2);
        const int dx = (cols / 2) * CELL;
        m.Spawn("entrance", dx, (rows - 2) * CELL);
        m.Spawn("default",  dx, (rows - 2) * CELL);
        m.Portal(dx - 32, (rows - 1) * CELL, 64, 32, "fernhollow", "from_fernhollow_college",
                 "Step outside", false);

        auto piece = [&](const string& art, int x, int y, int cw, int ch) {
            m.Prop("props", art, x, y);
            if (cw > 0) m.Collision(x - cw / 2, y - ch, cw, ch);
        };
        // The circle, on the floor at the middle of the hall.
        m.Overlay("props", "spell_circle", dx, 7 * CELL + 16);
        for (int i = 0; i < 4; ++i) piece("bookshelf", (3 + i * 3) * CELL + 16, 3 * CELL + 4, 44, 14);
        piece("cottage_bookshelf", 16 * CELL + 16, 3 * CELL + 4, 40, 14);
        piece("lectern",      dx - 16,          5 * CELL + 8,  28, 10);
        piece("chalk_board",  17 * CELL,        6 * CELL + 4,  30, 10);
        piece("table_round",  3 * CELL + 16,    9 * CELL + 8,  40, 12);
        piece("tavern_chair", 2 * CELL + 16,    9 * CELL + 10, 16, 8);
        piece("candlestand",  6 * CELL,         6 * CELL + 8,  16, 8);
        piece("candlestand",  14 * CELL,        6 * CELL + 8,  16, 8);
        piece("candlestand",  6 * CELL,         10 * CELL + 8, 16, 8);
        piece("candlestand",  14 * CELL,        10 * CELL + 8, 16, 8);
        piece("travel_chest", 17 * CELL + 8,    10 * CELL,     28, 12);
        piece("writing_desk", 3 * CELL,         6 * CELL + 4,  40, 14);
        m.Npc("npc_magister", "Magister Orrin", "magister", dx + 40, 6 * CELL + 8, "magister_root", 0)["shop"] = "fernhollow_college";
        m.Write("maps");
    }
}

// --- the dreamworld ----------------------------------------------------------
//
// Where the player goes when they sleep. Five islands hang over a starry void,
// joined to the one you arrive on by plank bridges: a grove of shades to the
// north, dread boars grazing to the west, crystals worth mining to the east,
// and to the south the brute that the other nightmares keep away from.
//
// It is built from the waking world's own art. Snow reads as cloud once the
// dream's violet light is over it, the scenery is the forest's toadstools and
// saplings, and the nightmares are orcs and boars in a bad night's colours.
// The void is not drawn at all: the game paints stars behind where there is
// no ground, and it is solid, so nobody walks off an edge.

static void BuildDreamworld() {
    const int CELL = 32, W = 72, H = 56;
    MapBuilder m("dreamworld", "The Reverie", W * CELL, H * CELL);
    m.Ambient("dream");
    m.Subtitle("Where the Hollowmarch goes when it sleeps");
    m.Background(14, 10, 30);
    std::mt19937 rng(9191u);

    struct Isle { float cx, cy, rx, ry; };
    const Isle isles[] = {
        {36.0f, 28.0f, 7.0f, 6.0f},    // 0: arrival
        {36.0f,  9.0f, 9.0f, 5.0f},    // 1: the shade grove
        {60.0f, 28.0f, 8.0f, 7.0f},    // 2: the crystal field
        {12.0f, 28.0f, 8.0f, 8.0f},    // 3: the boar meadow
        {36.0f, 47.0f, 9.0f, 6.0f},    // 4: the brute's plateau
    };
    const int ISLES = 5;

    // How far inside an island a cell is: below 1 is ground. The rim wobbles,
    // so no island is a perfect ellipse.
    auto isle_depth = [&](int i, float cx, float cy) {
        const float dx = (cx - isles[i].cx) / isles[i].rx;
        const float dy = (cy - isles[i].cy) / isles[i].ry;
        const float wobble = (Fbm(cx * 0.35f, cy * 0.35f, 91 + i) - 0.5f) * 0.55f;
        return (dx * dx + dy * dy) / (1.0f + wobble);
    };
    auto which_isle = [&](int cx, int cy) {
        for (int i = 0; i < ISLES; ++i)
            if (isle_depth(i, cx + 0.5f, cy + 0.5f) < 1.0f) return i;
        return -1;
    };
    // Distance, in cells, from a cell to the bridge running from the arrival
    // island to island i.
    auto bridge_dist = [&](int i, float cx, float cy) {
        const float ax = isles[0].cx, ay = isles[0].cy, bx = isles[i].cx, by = isles[i].cy;
        const float vx = bx - ax, vy = by - ay;
        const float t = std::clamp(((cx - ax) * vx + (cy - ay) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
        const float px = ax + vx * t - cx, py = ay + vy * t - cy;
        return sqrtf(px * px + py * py);
    };
    auto on_bridge = [&](int cx, int cy) {
        for (int i = 1; i < ISLES; ++i)
            if (bridge_dist(i, cx + 0.5f, cy + 0.5f) < 1.05f) return true;
        return false;
    };
    auto near_bridge = [&](int cx, int cy, float cells) {
        for (int i = 1; i < ISLES; ++i)
            if (bridge_dist(i, cx + 0.5f, cy + 0.5f) < cells) return true;
        return false;
    };

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const int isle = which_isle(cx, cy);
            if (isle >= 0) {
                // All cloud: patches of grass on it read as squares cut out of
                // the dream rather than as anything growing there.
                m.Ground(VariantOf("snow", cx, cy), cx * CELL, cy * CELL, CELL);
            } else if (on_bridge(cx, cy)) {
                m.Ground(VariantOf("plank_floor", cx, cy), cx * CELL, cy * CELL, CELL);
            } else {
                m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }
        }

    auto px = [&](float cells) { return static_cast<int>(cells * CELL); };
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
    {
        json& o = m.Object("dream_waking_stone", "dream_wake", ax - 64, ay - 56);
        o["sprite"] = ObjPath("rock_02");
        o["title"]  = "Waking stone";
        m.Collision(ax - 64 - 14, ay - 56 - 10, 28, 10);
    }
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
                      "The shards the nightmares leave behind are real. They come back with you.";
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
    // Toadstools, saplings and bushes on the islands, kept off the bridges, off
    // the plaza you arrive in, and a cell back from every rim so nothing hangs
    // over the void.
    auto deep_inside = [&](int cx, int cy) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (which_isle(cx + dx, cy + dy) < 0) return false;
        return true;
    };
    int herb_i = 0;
    for (int cy = 1; cy < H - 1; ++cy)
        for (int cx = 1; cx < W - 1; ++cx) {
            const int isle = which_isle(cx, cy);
            if (isle < 0 || !deep_inside(cx, cy) || near_bridge(cx, cy, 2.6f)) continue;
            const float to_centre = std::hypot(cx + 0.5f - isles[isle].cx, cy + 0.5f - isles[isle].cy);
            if (to_centre < (isle == 0 ? 5.5f : 3.0f)) continue;
            const float r = Hash2(cx, cy, 313);
            const int x = cx * CELL + 16, y = cy * CELL + 26;
            // Starlily only in the crystal field; moonpetal on every other island
            // but the arrival plaza.
            const float hr = Hash2(cx, cy, 515);
            if (r >= 0.20f && isle == 2 && hr < 0.22f) {
                PlaceHerb(m, "starlily", x, y, herb_i);
                continue;
            }
            if (r >= 0.20f && isle != 2 && isle != 0 && hr < 0.12f) {
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

    // --- the crystal field: dream shards to mine ---------------------------------
    {
        const Isle& e = isles[2];
        const float spots[][2] = {{-4, -3}, {3, -4}, {5, 1}, {-2, 3}, {2, 4}, {-5, 1}};
        int n = 0;
        for (const auto& sp : spots) {
            const int x = px(e.cx + sp[0]), y = px(e.cy + sp[1]);
            json& o = m.Object("dream_crystal_" + std::to_string(n++), "rock", x, y);
            o["sprite"]      = ObjPath(Pick(kRocks, rng));
            o["skill"]       = "Mining";
            o["skill_level"] = 1;
            o["yield"]       = "dream_shard";
            o["yield_xp"]    = 40;
            o["gather_time"] = 3.0f;
            o["title"]       = "dream crystal";
            // A crystal gives out like a seam, and grows back quickly: a
            // dream only lasts the night.
            o["deplete"]     = 0.25f;
            o["regrow"]      = 0.5f;
            m.Collision(x - 14, y - 12, 28, 12);
        }
        m.Enemy("nightmare_shade", px(e.cx + 1), px(e.cy - 1), 5, 40.0f, 200.0f);
        m.Enemy("nightmare_shade", px(e.cx - 1), px(e.cy + 5), 5, 40.0f, 200.0f);
    }

    // --- the shade grove and the boar meadow -------------------------------------
    {
        const Isle& n = isles[1];
        const float spots[][2] = {{-5, -1}, {-1, -2}, {3, -1}, {6, 1}};
        int lv = 3;
        for (const auto& sp : spots)
            m.Enemy("nightmare_shade", px(n.cx + sp[0]), px(n.cy + sp[1]), lv++, 40.0f, 200.0f);
    }
    {
        const Isle& w = isles[3];
        const float spots[][2] = {{-4, -4}, {3, -3}, {-3, 4}, {4, 3}};
        int i = 0;
        for (const auto& sp : spots)
            m.Enemy("dread_boar", px(w.cx + sp[0]), px(w.cy + sp[1]), 3 + (i++ % 2), 40.0f, 200.0f);
    }

    // --- the brute's plateau ---------------------------------------------------------
    {
        const Isle& s = isles[4];
        m.Enemy("nightmare_brute", px(s.cx + 1), px(s.cy + 2), 10, 90.0f, 220.0f);
        m.Enemy("nightmare_shade", px(s.cx - 5), px(s.cy), 6, 40.0f, 200.0f);
        m.Enemy("nightmare_shade", px(s.cx + 6), px(s.cy - 1), 6, 40.0f, 200.0f);
        PlaceChest(m, "chest_dream", px(s.cx + 1), px(s.cy + 4), "chest_dream");
        // Demonite: black glass with a red heat inside, found nowhere but here.
        const float seams[][2] = {{-4, 3}, {5, 3}, {-1, -3}};
        int k = 0;
        for (const auto& sp : seams)
            PlaceRock(m, rng, 950 + k++, px(s.cx + sp[0]), px(s.cy + sp[1]), true, 80, "demonite_ore");
    }

    m.Write("maps");
}

// --- main --------------------------------------------------------------------

int main() {
    std::printf("genmaps: building the Hollowmarch\n");
    g_manifest.Load("data/asset_manifest.json");
    LoadHerbs();

    BuildOverworld();
    BuildTown();
    BuildInteriors();
    BuildWhisperwood();
    BuildWestwold();
    BuildBrackenwood();
    BuildMossvale();
    BuildFernhollow();
    BuildWoodlandInteriors();
    BuildDreamworld();
    BuildIceSpire();
    BuildAshenPath();

    BuildDungeon("dungeon_emberfell_1", "Emberfell Mine, Upper Workings",
                 1001u, 60, 46, 9,
                 "dungeon_floor", "dungeon_wall",
                 "overworld", "from_mine",
                 {{"orc1", 3}, {"orc1", 4}, {"orc2", 5}},
                 "chest_dungeon", 3,
                 "chest_emberfell_key", "key_emberfell",
                 "dungeon_emberfell_2", "rusted_key",
                 "", 1,
                 {{"iron_ore", 10}, {"coal", 20}});

    BuildDungeon("dungeon_emberfell_2", "Emberfell Mine, Lower Workings",
                 1002u, 54, 42, 8,
                 "dungeon_floor", "dungeon_wall",
                 "dungeon_emberfell_1", "from_below",
                 {{"orc2", 7}, {"orc2", 9}, {"orc1", 6}},
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
                 {{"orc1", 6}, {"orc2", 8}},
                 "chest_barrow", 3,
                 "chest_barrow_seal", "seal_barrow",
                 "", "", "", 1,
                 {{"diamond_ore", 60}, {"azuryte_ore", 30}}, 0,
                 "chest_barrow_hoard", "drowned_king_boots", "q_drowned_hoard");

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
    if (!g_manifest.unsized.empty()) {
        for (const string& key : g_manifest.unsized)
            std::printf("genmaps: no size for assets/%s.png -- it was placed at 32x32\n", key.c_str());
        std::printf("genmaps: run tools/make_manifest.ps1, then build the maps again\n");
        return 1;
    }
    std::printf("genmaps: done\n");
    return 0;
}
