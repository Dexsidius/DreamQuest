#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "../camera.h"

// -----------------------------------------------------------------------------
//  Map: loads the LevelEdit-Plus ".mx" export format.
//
//  The editor writes a flat dictionary of tile-name -> image + placements:
//
//      { "name": "...",
//        "tiles": { "Grass": { "filepath": "assets/grass.png",
//                              "locations": [[cx, cy, w, h], ...] } } }
//
//  Placements are centre-anchored, exactly as GameTile::Render does in the
//  editor, so a map drawn here lines up pixel-for-pixel with the editor view.
//
//  Everything a game needs on top of that -- draw layers, collision, portals,
//  spawn points, enemies, NPCs -- lives under a separate "dreamquest" key.
//  LevelEdit-Plus ignores keys it does not know, so these maps stay openable
//  and editable in the editor while carrying the extra gameplay data.
// -----------------------------------------------------------------------------

enum TileLayer { LAYER_GROUND = 0, LAYER_DECOR = 1, LAYER_OVERHEAD = 2 };

// -----------------------------------------------------------------------------
//  Elevation
//
//  A coarse height grid laid over the map, one level per cell, stored under the
//  "dreamquest" key as:
//
//      "elevation": { "cell": 32, "cols": 128, "rows": 96,
//                     "levels": [ ... cols*rows small ints, row major ... ],
//                     "ramps":  [ [x, y, w, h], ... ] }
//
//  Level 0 is the ground everything used to sit on, so a map with no elevation
//  block behaves exactly as it did before.
//
//  Each level lifts what stands on it by ELEVATION_RISE pixels on screen. It is
//  a lift, not a projection: this is a top-down game, and a cell at level two is
//  drawn where a cell at level two would be if you were looking down at a
//  terrace from a shallow angle. What sells it is the face -- the wall of earth
//  exposed on the downhill side -- which is drawn separately.
//
//  Movement between cells of different levels is blocked, which is what makes a
//  cliff a cliff. Ramps are rectangles where that rule is suspended.
// -----------------------------------------------------------------------------
constexpr float ELEVATION_RISE = 14.0f;   // screen pixels per level
constexpr int   ELEVATION_MAX  = 6;

struct TileInstance {
    SDL_FRect rect;          // world-space, top-left anchored (already un-centred)
    int   tex = -1;          // index into Map::textures
    int   layer = LAYER_GROUND;
    float sort_y = 0.0f;     // baseline used when interleaving with entities
    // Lies on the floor over the other ground tiles -- a rug, a flight of
    // stairs. Named with a leading '~' in the .mx file.
    bool  overlay = false;
};

struct Portal {
    SDL_FRect rect{};
    string target_map;       // map id, e.g. "house_elder"
    string target_spawn;     // spawn point name inside that map
    string label;            // shown on the interact prompt
    bool   requires_interact = true;   // false = step-through
    string locked_by;        // item id needed to pass, empty when open
    int    danger_level = 0; // Combat level advised beyond it; 0 when safe
    // Combat level needed to go through at all; 0 when anyone may. The Ice
    // Spire and the way to the pit are closed to a character who would only
    // die there.
    int    min_combat = 0;
};

// Ground that hurts to stand on: lava vents, burning ash. Damage per second
// while the player's feet are on it.
struct Hazard {
    SDL_FRect rect{};
    float dps = 4.0f;
    string kind = "fire";
};

struct EnemySpawnDef {
    string type;             // key into data/enemies.json
    float  x = 0, y = 0;
    int    level = 1;
    float  respawn = 25.0f;  // seconds; <= 0 means it stays dead
    float  leash = 220.0f;   // how far it will chase from its post
};

struct NpcDef {
    string id, name, sprite;
    float  x = 0, y = 0;
    string dialogue;         // root node in data/dialogue.json
    Facing facing = FACE_DOWN;
    bool   wanders = false;
    string shop;             // shop id, empty when the NPC does not trade
};

struct MapObject {
    string id;               // unique across the save, e.g. "chest_mine_01"
    string type;             // chest | storage | search | note | board | tree | rock | sign
    float  x = 0, y = 0;
    SDL_FRect solid{};       // optional blocking box, w == 0 when not solid
    string loot_table;       // chests
    // A chest holding one named thing rather than a table roll: the only way
    // to put an item in the world that no loot table can ever produce.
    string loot_item;
    int    loot_qty = 1;
    // When set, the object is only in the world while this quest is being
    // done: before it is taken and after it is finished, it is not there to
    // be seen or opened.
    string needs_quest;
    string text;             // notes and signs
    string starts_quest;     // notes that kick off a quest
    string sprite;           // optional image path drawn at the position
    string sprite_open;      // swapped in once the object has been used
    string skill;            // gathering nodes: Woodcutting / Mining
    int    skill_level = 1;
    string yield;            // item a gathering node produces
    int    yield_xp = 0;
    float  gather_time = 2.6f;
    // Herbs: game hours before a picked plant grows back.
    float  regrow_hours = 6.0f;
    string title;
    string station;          // crafting objects: "workbench" or "anvil"
    // A storage chest: how many slots it holds. What is in it is the player's
    // and lives in the save, not here.
    int    capacity = 0;
    vector<string> quests;   // mission boards
    vector<string> fish;     // fishing spots: what can be caught there
};

class Map {
public:
    bool Load(const string& path);
    void Unload();
    bool Loaded() const { return loaded; }

    const string& Id() const { return id; }
    const string& DisplayName() const { return display_name; }
    // The line under the zone name on the banner shown on arrival.
    const string& Subtitle() const { return subtitle; }
    float Width() const { return bounds_w; }
    float Height() const { return bounds_h; }
    bool  IsInterior() const { return interior; }
    // A place with no light of its own: what you can see is what you carry.
    bool  IsDark() const { return dark; }
    const string& Ambient() const { return ambient; }
    SDL_Color BackgroundColor() const { return background; }

    // Draw one layer, culled to what the camera can see.
    void RenderLayer(SDL_Renderer* r, TextureCache& cache, const Camera& cam, int layer) const;
    // Decor tiles that must interleave with entities by baseline.
    void CollectDecor(const Camera& cam, vector<const TileInstance*>& out) const;
    // Draws a single tile, so the world's sorted pass can interleave decor
    // with entities without needing to know about the texture list.
    void RenderTile(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
                    const TileInstance& t, Uint8 alpha = 255) const;
    // Every tile on the map, for anything that needs to walk the whole set
    // rather than draw what the camera can see -- the minimap bakes its image
    // from these.
    const vector<TileInstance>& Tiles() const { return tiles; }
    // The image a tile draws, or an empty string.
    const string& TexturePath(const TileInstance& t) const {
        static const string none;
        return (t.tex >= 0 && t.tex < static_cast<int>(textures.size())) ? textures[t.tex] : none;
    }

    // --- collision -----------------------------------------------------------
    bool  Blocked(const SDL_FRect& box) const;
    // Axis-separated slide; returns the resolved position for the box.
    SDL_FPoint MoveWithCollision(const SDL_FRect& box, float dx, float dy) const;

    // Where a moving box first meets a wall, and which way that wall faces.
    //
    // Walking wants to slide along an obstacle, which is what MoveWithCollision
    // does. Anything that hits a wall and reacts to it -- a bolt that stops, an
    // arrow that ricochets -- needs to know two more things: the last position
    // that was actually clear, so the impact is drawn on the surface rather
    // than inside it, and the normal of the face it struck, so a bounce leaves
    // in a plausible direction.
    struct Contact {
        bool  hit = false;
        float x = 0.0f, y = 0.0f;     // last clear position of the box's centre
        float nx = 0.0f, ny = 0.0f;   // unit normal, pointing back out of the wall
    };
    Contact SweepPoint(float x, float y, float dx, float dy, float radius) const;

    // --- elevation -----------------------------------------------------------
    bool  HasElevation() const { return elev_cols > 0 && elev_rows > 0; }
    float ElevationCell() const { return elev_cell; }
    // Terrain level under a world point. Off the map, the nearest edge cell's
    // level, so something walking out of bounds does not fall off a cliff that
    // is not there.
    int   LevelAt(float x, float y) const;
    // What that level is worth on screen, in pixels of lift.
    float HeightAt(float x, float y) const { return LevelAt(x, y) * ELEVATION_RISE; }
    // True when a ramp covers this point, so a level change here is walkable.
    bool  RampAt(float x, float y) const;
    // True when stepping from one point to the other crosses a level change
    // that no ramp covers -- which is what makes a cliff impassable.
    bool  LevelChangeBlocked(float from_x, float from_y,
                             float to_x, float to_y) const;
    // Draws the exposed faces of every raised cell the camera can see. Called
    // between the ground and everything that stands on it.
    void  RenderCliffs(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const;

    // --- lookups -------------------------------------------------------------
    const Portal* PortalAt(const SDL_FRect& box) const;
    bool  Spawn(const string& name, SDL_FPoint& out) const;
    SDL_FPoint DefaultSpawn() const;

    const vector<EnemySpawnDef>& Enemies() const { return enemies; }
    const vector<NpcDef>&        Npcs() const { return npcs; }
    const vector<MapObject>&     Objects() const { return objects; }
    const vector<Portal>&        Portals() const { return portals; }
    const vector<Hazard>&        Hazards() const { return hazards; }
    // The hazard under a box, or null.
    const Hazard* HazardAt(const SDL_FRect& box) const;
    // Objects that are not in the file: the player's own camp. They last until
    // the map is loaded again, so the world puts them back each time.
    void AddObject(const MapObject& o) { objects.push_back(o); }
    void RemoveObjects(const string& id_prefix) {
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                          [&](const MapObject& o) { return o.id.rfind(id_prefix, 0) == 0; }),
                      objects.end());
    }

private:
    void   BuildChunks();
    void   AddCollider(const SDL_FRect& r);
    string ResolveAsset(const string& rel) const;

    struct Chunk { vector<int> layers[3]; vector<int> colliders; };
    const Chunk* ChunkAt(int cx, int cy) const;
    void ForEachChunkInRect(const SDL_FRect& r,
                            const std::function<void(const Chunk&)>& fn) const;

    bool   loaded = false;
    string id, display_name, source_dir, ambient, subtitle;
    bool   interior = false;
    bool   dark = false;
    SDL_Color background{24, 20, 32, 255};

    vector<string>       textures;      // resolved image paths
    vector<TileInstance> tiles;
    vector<SDL_FRect>    colliders;

    vector<Portal>        portals;
    vector<Hazard>        hazards;
    vector<EnemySpawnDef> enemies;
    vector<NpcDef>        npcs;
    vector<MapObject>     objects;
    map<string, SDL_FPoint> spawns;

    // Height grid. Empty on a map that does not use elevation.
    vector<uint8_t>   elev;
    string cliff_texture;         // what an exposed bank is made of
    vector<SDL_FRect> ramps;
    int   elev_cols = 0, elev_rows = 0;
    float elev_cell = 32.0f;
    int   LevelCell(int cx, int cy) const;
    // Colour multiplier for ground standing this many levels up.
    static Uint8 LevelShade(int level);

    float bounds_w = 0, bounds_h = 0;
    int   chunk_cols = 0, chunk_rows = 0;
    vector<Chunk> chunks;

    static constexpr float CHUNK = 256.0f;
};
