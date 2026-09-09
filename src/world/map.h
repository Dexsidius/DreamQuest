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

struct TileInstance {
    SDL_FRect rect;          // world-space, top-left anchored (already un-centred)
    int   tex = -1;          // index into Map::textures
    int   layer = LAYER_GROUND;
    float sort_y = 0.0f;     // baseline used when interleaving with entities
};

struct Portal {
    SDL_FRect rect{};
    string target_map;       // map id, e.g. "house_elder"
    string target_spawn;     // spawn point name inside that map
    string label;            // shown on the interact prompt
    bool   requires_interact = true;   // false = step-through
    string locked_by;        // item id needed to pass, empty when open
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
    string type;             // chest | note | board | tree | rock | sign
    float  x = 0, y = 0;
    SDL_FRect solid{};       // optional blocking box, w == 0 when not solid
    string loot_table;       // chests
    string text;             // notes and signs
    string starts_quest;     // notes that kick off a quest
    string sprite;           // optional image path drawn at the position
    string sprite_open;      // swapped in once the object has been used
    string skill;            // gathering nodes: Woodcutting / Mining
    int    skill_level = 1;
    string yield;            // item a gathering node produces
    int    yield_xp = 0;
    float  gather_time = 2.6f;
    string title;
    vector<string> quests;   // mission boards
};

class Map {
public:
    bool Load(const string& path);
    void Unload();
    bool Loaded() const { return loaded; }

    const string& Id() const { return id; }
    const string& DisplayName() const { return display_name; }
    float Width() const { return bounds_w; }
    float Height() const { return bounds_h; }
    bool  IsInterior() const { return interior; }
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

    // --- collision -----------------------------------------------------------
    bool  Blocked(const SDL_FRect& box) const;
    // Axis-separated slide; returns the resolved position for the box.
    SDL_FPoint MoveWithCollision(const SDL_FRect& box, float dx, float dy) const;

    // --- lookups -------------------------------------------------------------
    const Portal* PortalAt(const SDL_FRect& box) const;
    bool  Spawn(const string& name, SDL_FPoint& out) const;
    SDL_FPoint DefaultSpawn() const;

    const vector<EnemySpawnDef>& Enemies() const { return enemies; }
    const vector<NpcDef>&        Npcs() const { return npcs; }
    const vector<MapObject>&     Objects() const { return objects; }
    const vector<Portal>&        Portals() const { return portals; }

private:
    void   BuildChunks();
    void   AddCollider(const SDL_FRect& r);
    string ResolveAsset(const string& rel) const;

    struct Chunk { vector<int> layers[3]; vector<int> colliders; };
    const Chunk* ChunkAt(int cx, int cy) const;
    void ForEachChunkInRect(const SDL_FRect& r,
                            const std::function<void(const Chunk&)>& fn) const;

    bool   loaded = false;
    string id, display_name, source_dir, ambient;
    bool   interior = false;
    SDL_Color background{24, 20, 32, 255};

    vector<string>       textures;      // resolved image paths
    vector<TileInstance> tiles;
    vector<SDL_FRect>    colliders;

    vector<Portal>        portals;
    vector<EnemySpawnDef> enemies;
    vector<NpcDef>        npcs;
    vector<MapObject>     objects;
    map<string, SDL_FPoint> spawns;

    float bounds_w = 0, bounds_h = 0;
    int   chunk_cols = 0, chunk_rows = 0;
    vector<Chunk> chunks;

    static constexpr float CHUNK = 256.0f;
};
