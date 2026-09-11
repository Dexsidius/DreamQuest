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
#include <map>
#include <algorithm>
#include <fstream>
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

    std::pair<int, int> Size(const string& key, int fallback = 32) const {
        auto it = size.find(key);
        if (it == size.end()) return {fallback, fallback};
        return it->second;
    }

    const vector<string>& Family(const string& name) const {
        static const vector<string> empty;
        auto it = families.find(name);
        return it == families.end() ? empty : it->second;
    }
};

static Manifest g_manifest;

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
        g.locations.push_back({x + w / 2, y + h / 2, w, h});
    }

    // True for tiles whose art has visible detail rather than being one colour.
    static bool Textured(const string& name) {
        return name == "road" || name == "dungeon_wall" ||
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
        dq["collision"].push_back(json::array({x, y, w, h}));
    }

    void Spawn(const string& name, int x, int y) {
        dq["spawns"][name] = json::array({x, y});
    }

    // True when a player standing with their feet at (x, y) touches none of
    // the collision placed so far. The foot box matches the self-test's.
    bool Clear(int x, int y) const {
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
        p["rect"]     = json::array({x, y, w, h});
        p["target"]   = target;
        p["spawn"]    = spawn;
        p["label"]    = label;
        p["interact"] = interact;
        if (!locked_by.empty()) p["locked_by"] = locked_by;
        dq["portals"].push_back(p);
    }

    void Enemy(const string& type, int x, int y, int level,
               float respawn = 28.0f, float leash = 260.0f) {
        json e;
        e["type"]    = type;
        e["x"]       = x;
        e["y"]       = y;
        e["level"]   = level;
        e["respawn"] = respawn;
        e["leash"]   = leash;
        dq["enemies"].push_back(e);
    }

    void Npc(const string& npc_id, const string& name, const string& sprite,
             int x, int y, const string& dialogue, int facing = 0,
             bool wanders = false) {
        json n;
        n["id"]       = npc_id;
        n["name"]     = name;
        n["sprite"]   = sprite;
        n["x"]        = x;
        n["y"]        = y;
        n["dialogue"] = dialogue;
        n["facing"]   = facing;
        n["wanders"]  = wanders;
        dq["npcs"].push_back(n);
    }

    json& Object(const string& obj_id, const string& type, int x, int y) {
        json o;
        o["id"]   = obj_id;
        o["type"] = type;
        o["x"]    = x;
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
            rr.push_back(json::array({r.x, r.y, r.w, r.h}));
        e["ramps"] = rr;
        dq["elevation"] = e;
    }

    void Interior(bool v) { dq["interior"] = v; }
    void Ambient(const string& v) { dq["ambient"] = v; }
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
    o["yield_xp"]    = big ? 60 : 24;
    o["gather_time"] = big ? 3.2f : 2.4f;
    // Named for the ore, because copper and iron use the same rock art: a
    // plain "outcrop" left a new miner walking up to iron they could not
    // touch with nothing to say which of the rocks around it was copper.
    const string ore = (yield == "iron_ore") ? "iron " : "copper ";
    o["title"]       = ore + (big ? "seam" : "outcrop");

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

// --- overworld ---------------------------------------------------------------

enum Biome { MEADOW, GREENWOOD, FOOTHILLS, MIRE, CURSED, WATER, ROAD };

static const int OW_CELL = 32;
static const int OW_W = 128, OW_H = 96;                 // cells
static const int OW_PX_W = OW_W * OW_CELL;              // 4096
static const int OW_PX_H = OW_H * OW_CELL;              // 3072

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

static Biome BiomeAt(int cx, int cy) {
    const float n = Fbm(cx * 0.045f, cy * 0.045f, 1337);

    // The river cuts across the south-west before feeding the mire.
    const float river = fabsf((cy - 66.0f) - sinf(cx * 0.09f) * 5.0f);
    if (cx < 44 && river < 2.2f + n * 1.4f) return WATER;

    if (fabsf(cx - RoadX(cy)) < 1.6f && cy > 12 && cy < 88) return ROAD;

    if (cx > 96 && cy < 34 && n > 0.42f) return CURSED;
    if (cy < 20 + n * 8.0f) return FOOTHILLS;
    if (cx < 24 + n * 10.0f) return MIRE;
    if (cx > 86 - n * 10.0f) return GREENWOOD;
    return MEADOW;
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
    MapBuilder m("overworld", "The Hollowmarch", OW_PX_W, OW_PX_H);
    m.Ambient("overworld");
    m.Background(38, 52, 40);
    std::mt19937 rng(20260909u);

    // --- ground ---------------------------------------------------------------
    for (int cy = 0; cy < OW_H; ++cy) {
        for (int cx = 0; cx < OW_W; ++cx) {
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
                case ROAD:      tile = "road"; break;
                case FOOTHILLS: tile = (v > 0.62f) ? "dirt_dark" : (v > 0.34f ? "dirt" : "sand"); break;
                case MIRE:      tile = (v > 0.6f) ? "marsh_dark" : (v > 0.32f ? "marsh_ground" : "marsh_stone"); break;
                case CURSED:    tile = (v > 0.5f) ? "cursed_ground" : "cursed_sand"; break;
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
        const int ecols = OW_PX_W / EL, erows = OW_PX_H / EL;
        const int per = EL / OW_CELL;          // tile cells per height cell

        vector<int> levels(static_cast<size_t>(ecols) * erows, 0);
        for (int ey = 0; ey < erows; ++ey)
            for (int ex = 0; ex < ecols; ++ex)
                levels[static_cast<size_t>(ey) * ecols + ex] =
                    ElevationAt(ex * per + per / 2, ey * per + per / 2);

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
        clearing(static_cast<int>(RoadX(88)), 88, 4);   // the town gate
        clearing(static_cast<int>(RoadX(10)), 9,  4);   // the mine
        clearing(12, 44, 4);                            // the barrow
        clearing(96, 78, 3);                            // meadow chest
        clearing(114, 52, 3);                           // wood chest
        clearing(10, 60, 3);                            // mire chest

        m.Elevation(EL, ecols, erows, levels, ramps);
    }

    // Water is impassable; walling it off per cell is cheap and exact.
    for (int cy = 0; cy < OW_H; ++cy)
        for (int cx = 0; cx < OW_W; ++cx)
            if (BiomeAt(cx, cy) == WATER)
                m.Collision(cx * OW_CELL, cy * OW_CELL, OW_CELL, OW_CELL);

    // --- ground decals --------------------------------------------------------
    // The CraftPix ground set ships its variation as loose patches meant to be
    // dropped over a flat fill; scattering them is what stops the base layer
    // looking like coloured squares.
    for (int cy = 1; cy < OW_H - 1; ++cy) {
        for (int cx = 1; cx < OW_W - 1; ++cx) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER || b == ROAD) continue;

            const float r = Hash2(cx, cy, 5150);
            if (r > 0.12f) continue;

            // Match the decal to the terrain it is lying on.
            const char* family = "grass";
            if (b == FOOTHILLS || b == CURSED) family = "dirt";
            else if (b == MIRE)                family = "dirt";

            const vector<string>& pool = g_manifest.Family(family);
            if (pool.empty()) continue;

            const size_t pick = static_cast<size_t>(Hash2(cx, cy, 6161) * 1000.0f) % pool.size();
            m.Flat("decor", pool[pick], cx * OW_CELL + 16, cy * OW_CELL + 16);
        }
    }

    // --- scenery and gathering nodes -----------------------------------------
    int tree_index = 0, rock_index = 0;

    for (int cy = 2; cy < OW_H - 2; ++cy) {
        for (int cx = 2; cx < OW_W - 2; ++cx) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER || b == ROAD) continue;
            // Keep a clear verge either side of the road.
            if (fabsf(cx - RoadX(cy)) < 3.2f) continue;

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
            } else if (b == MIRE) {
                if (r < 0.028f)      m.Prop("objects", Pick(kSmallTrees, rng), x, y);
                else if (r < 0.05f)  m.Prop("objects", Pick(kFungus, rng), x, y);
                else if (r < 0.062f) PlaceRock(m, rng, rock_index++, x, y, false, 5, "iron_ore");
            } else if (b == CURSED) {
                if (r < 0.04f)       m.Prop("objects", Pick(kSmallRocks, rng), x, y);
                else if (r < 0.055f) PlaceRock(m, rng, rock_index++, x, y, true, 10, "iron_ore");
            }
        }
    }

    // --- wildlife and orcs ----------------------------------------------------
    int spawned = 0;
    for (int cy = 4; cy < OW_H - 4; cy += 3) {
        for (int cx = 4; cx < OW_W - 4; cx += 3) {
            const Biome b = BiomeAt(cx, cy);
            if (b == WATER) continue;

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
                if (r < 0.05f)       { m.Enemy("orc1", x, y, 5); ++spawned; }
                else if (r < 0.07f)  { m.Enemy("fox", x, y, 4); ++spawned; }
            } else if (b == CURSED) {
                if (r < 0.09f)       { m.Enemy("orc2", x, y, 8); ++spawned; }
            }

            // The Sunken Road is where the orc contract is actually filled.
            if (b != WATER && road_gap > 2.0f && road_gap < 6.0f && cy > 24 && cy < 74) {
                if (r > 0.90f) { m.Enemy("orc1", x, y, 3, 24.0f, 200.0f); ++spawned; }
            }
        }
    }
    (void)spawned;

    // --- landmarks ------------------------------------------------------------
    const int gate_x = static_cast<int>(RoadX(86)) * OW_CELL + 16;
    const int gate_y = 88 * OW_CELL;
    m.Spawn("start", gate_x, gate_y - 40);
    m.Spawn("from_town", gate_x, gate_y - 40);
    m.Portal(gate_x - 48, gate_y, 96, 48, "town_havenbrook", "from_field",
             "Enter Havenbrook", false);

    {
        json& o = m.Object("sign_gate", "sign", gate_x + 56, gate_y - 8);
        o["sprite"] = "assets/props/signpost.png";
        o["title"]  = "Waymarker";
        o["text"]   = "HAVENBROOK, south.\nEMBERFELL MINE, north along the Sunken Road.\n\nBelow, scratched later and deeper:\nthe road is not safe after the second milestone.";
        m.Collision(gate_x + 40, gate_y - 16, 32, 12);
    }

    // Mine entrance in the northern foothills.
    const int mine_x = static_cast<int>(RoadX(10)) * OW_CELL + 16;
    const int mine_y = 9 * OW_CELL;
    m.Spawn("from_mine", mine_x, mine_y + 56);
    m.Prop("objects", "door", mine_x, mine_y + 24);
    m.Portal(mine_x - 28, mine_y - 16, 56, 40, "dungeon_emberfell_1", "entrance",
             "Enter the Emberfell mine");
    m.Collision(mine_x - 48, mine_y - 20, 40, 28);
    m.Collision(mine_x + 28, mine_y - 20, 40, 28);

    // Barrow entrance, out in the mire.
    const int barrow_x = 12 * OW_CELL + 16;
    const int barrow_y = 44 * OW_CELL;
    m.Spawn("from_barrow", barrow_x, barrow_y + 56);
    m.Prop("objects", "door", barrow_x, barrow_y + 24);
    m.Portal(barrow_x - 28, barrow_y - 16, 56, 40, "dungeon_barrow", "entrance",
             "Enter the barrow");
    m.Collision(barrow_x - 48, barrow_y - 20, 40, 28);
    m.Collision(barrow_x + 28, barrow_y - 20, 40, 28);

    // The note that starts the barrow quest, left where someone turned back.
    {
        json& o = m.Object("note_mire", "note", barrow_x + 72, barrow_y + 96);
        o["title"]        = "A water-stained note";
        o["text"]         = "If you are reading this I did not come back out, and you should not go in.\n\nI am going anyway. There is a seal down there with my grandmother's name pressed into it, and the guild has known about it for forty years.\n\nIf you find the seal, do not give it to Orlend. Ask him why he never told anyone first.";
        o["starts_quest"] = "q_barrow_seal";
        o["sprite"]       = ObjPath("rocksmall_00");
    }

    // The surveyor's page, halfway up the Sunken Road.
    {
        const int px = static_cast<int>(RoadX(48)) * OW_CELL + 76;
        const int py = 48 * OW_CELL + 16;
        json& o = m.Object("note_surveyor", "note", px, py);
        o["title"]  = "Torn survey page";
        o["text"]   = "Third day on the road. Counted twelve of them at the second milestone. Counted thirty at the third.\n\nThey are not raiding. They are walking north, in order, and they are all walking to the same place.\n\nI have drawn it below as best I can from the ridge. It is a mine adit. It is the Emberfell adit.";
        o["loot"]   = "page_surveyor";
        o["sprite"] = ObjPath("rocksmall_02");
    }

    // A couple of chests off the road for the curious.
    PlaceChest(m, "chest_meadow_01", 96 * OW_CELL, 78 * OW_CELL, "chest_common");
    PlaceChest(m, "chest_wood_01",  114 * OW_CELL, 52 * OW_CELL, "chest_common");
    PlaceChest(m, "chest_mire_01",   10 * OW_CELL, 60 * OW_CELL, "chest_common");

    m.Write("maps");
}

// --- town --------------------------------------------------------------------

static void BuildTown() {
    const int CELL = 32, W = 56, H = 44;
    MapBuilder m("town_havenbrook", "Havenbrook", W * CELL, H * CELL);
    m.Ambient("town");
    m.Background(44, 58, 44);
    std::mt19937 rng(4242u);

    for (int cy = 0; cy < H; ++cy)
        for (int cx = 0; cx < W; ++cx) {
            const float v = Fbm(cx * 0.25f, cy * 0.25f, 77);
            // A crossroads through the middle of the village.
            const bool on_road = (abs(cy - 22) <= 1) || (abs(cx - 28) <= 1);
            string tile = on_road ? "road"
                        : (v > 0.6f ? "grass_light" : (v > 0.3f ? "grass" : "grass_olive"));
            m.Ground(tile, cx * CELL, cy * CELL, CELL);
        }

    // Fence the village in, leaving the south gate open.
    for (int cx = 0; cx < W; ++cx) {
        if (abs(cx - 28) > 2) m.Collision(cx * CELL, (H - 1) * CELL, CELL, CELL);
        m.Collision(cx * CELL, 0, CELL, CELL);
    }
    for (int cy = 0; cy < H; ++cy) {
        m.Collision(0, cy * CELL, CELL, CELL);
        m.Collision((W - 1) * CELL, cy * CELL, CELL, CELL);
    }

    m.Spawn("from_field", 28 * CELL + 16, (H - 3) * CELL);
    m.Spawn("respawn",    28 * CELL + 16, 26 * CELL);
    m.Spawn("default",    28 * CELL + 16, 26 * CELL);
    m.Portal(26 * CELL, (H - 1) * CELL - 8, 5 * CELL, 40,
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

    // A cooking fire anyone may use.
    {
        json& o = m.Object("range_town", "range", 40 * CELL, 30 * CELL);
        o["sprite"] = ObjPath("campfire");
        o["title"]  = "Cooking fire";
        m.Collision(40 * CELL - 16, 30 * CELL - 12, 32, 12);
    }

    // A workbench by the forge.
    {
        json& o = m.Object("bench_town", "workbench", 20 * CELL, 34 * CELL);
        o["sprite"] = "assets/props/workbench.png";
        o["title"]  = "Workbench";
        m.Collision(20 * CELL - 34, 34 * CELL - 18, 67, 18);
    }

    m.Npc("npc_guard",  "Watchman Corrin", "fighter2", 30 * CELL, 39 * CELL, "guard_root", 3);
    m.Npc("npc_hunter", "Hunter Ivo",      "citizen2", 46 * CELL, 30 * CELL, "hunter_root", 0, true);

    // Greenery so the village is not a bare field.
    for (int i = 0; i < 26; ++i) {
        const int x = 2 * CELL + static_cast<int>(rng() % ((W - 4) * CELL));
        const int y = 3 * CELL + static_cast<int>(rng() % ((H - 6) * CELL));
        if (abs(y - 22 * CELL) < 80 || abs(x - 28 * CELL) < 80) continue;
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
        piece("bed_single",   70, 150, 30, 36);
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
        m.Npc("npc_cook", "Innkeeper Bess", "citizen1", 512, 168, "cook_root", 0);
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
        piece("bed_single",   150, 250, 30, 36);
        piece("nightstand",   178, 214, 18, 8);
        piece("wardrobe",     258, 246, 34, 14);
        piece("washstand",    258, 330, 22, 8);
        piece("travel_chest", 150, 292, 28, 12);

        // Room two: the good room.
        m.Overlay("props", "inn_rug", 400, 326);
        piece("bed_double",   356, 258, 48, 36);
        piece("nightstand",   448, 214, 18, 8);
        piece("washstand",    452, 322, 22, 8);
        piece("travel_chest", 356, 300, 28, 12);

        // Room three: another single.
        piece("bed_single",   556, 250, 30, 36);
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
            o["sprite"] = "assets/props/anvil.png";
            o["title"]  = "Anvil";
            m.Collision(ax - 14, ay - 12, 28, 12);
        }

        // --- the shop -------------------------------------------------------
        piece("shop_counter", 13 * CELL + 8, 8 * CELL + 8, 76, 16);
        m.Npc("npc_smith", "Smith Halda", "citizen2", 13 * CELL + 8, 6 * CELL + 28,
              "smith_root", 0);
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
                         const string& boss_type = "", int boss_level = 1) {
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

    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            if (floor[cy][cx]) {
                const float v = Fbm(cx * 0.3f, cy * 0.3f, static_cast<int>(seed));
                m.Ground(v > 0.55f ? floor_tile : (floor_tile + "_dark"),
                         cx * CELL, cy * CELL, CELL);
            } else {
                m.Ground(wall_tile, cx * CELL, cy * CELL, CELL);
                m.Collision(cx * CELL, cy * CELL, CELL, CELL);
            }
        }

    if (rooms.empty()) { m.Write("maps"); return; }

    // Entrance sits in the first room, the stairs down in the last.
    const Room& first = rooms.front();
    const int ex = (first.x + first.w / 2) * CELL + 16;
    const int ey = (first.y + first.h / 2) * CELL + 16;
    m.Spawn("entrance", ex, ey);
    m.Spawn("default",  ex, ey);
    m.Prop("objects", "door_open", ex, ey - 8);
    m.Portal(ex - 24, ey + 4, 48, 32, exit_map, exit_spawn, "Leave", true);

    if (!deeper_map.empty() && rooms.size() > 1) {
        const Room& last = rooms.back();
        const int dx = (last.x + last.w / 2) * CELL + 16;
        const int dy = (last.y + last.h / 2) * CELL + 16;
        m.Prop("objects", "door", dx, dy + 8);
        m.Portal(dx - 24, dy - 24, 48, 40, deeper_map, "entrance",
                 "Descend", true, deeper_lock);
    }

    // Monsters everywhere but the room you walk in through.
    int placed = 0;
    for (size_t i = 1; i < rooms.size(); ++i) {
        const Room& r = rooms[i];
        const int count = 1 + static_cast<int>(rng() % 3);
        for (int k = 0; k < count && !monsters.empty(); ++k) {
            const auto& mon = monsters[rng() % monsters.size()];
            const int x = (r.x + 1 + static_cast<int>(rng() % std::max(1, r.w - 2))) * CELL + 16;
            const int y = (r.y + 1 + static_cast<int>(rng() % std::max(1, r.h - 2))) * CELL + 16;
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

// --- main --------------------------------------------------------------------

int main() {
    std::printf("genmaps: building the Hollowmarch\n");
    g_manifest.Load("data/asset_manifest.json");

    BuildOverworld();
    BuildTown();
    BuildInteriors();

    BuildDungeon("dungeon_emberfell_1", "Emberfell Mine, Upper Workings",
                 1001u, 60, 46, 9,
                 "dungeon_floor", "dungeon_wall",
                 "overworld", "from_mine",
                 {{"orc1", 3}, {"orc1", 4}, {"orc2", 5}},
                 "chest_dungeon", 3,
                 "chest_emberfell_key", "key_emberfell",
                 "dungeon_emberfell_2", "rusted_key");

    BuildDungeon("dungeon_emberfell_2", "Emberfell Mine, Lower Workings",
                 1002u, 54, 42, 8,
                 "dungeon_floor", "dungeon_wall",
                 "dungeon_emberfell_1", "from_below",
                 {{"orc2", 7}, {"orc2", 9}, {"orc1", 6}},
                 "chest_dungeon", 3,
                 "", "",
                 "", "",
                 "orc3", 12);

    BuildDungeon("dungeon_barrow", "The Barrow Beneath the Mire",
                 2001u, 52, 40, 8,
                 "dungeon_floor", "dungeon_wall",
                 "overworld", "from_barrow",
                 {{"orc1", 6}, {"orc2", 8}},
                 "chest_barrow", 3,
                 "chest_barrow_seal", "seal_barrow",
                 "", "");

    std::printf("genmaps: done\n");
    return 0;
}
