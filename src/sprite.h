#pragma once
#include "headers.h"
#include "texturecache.h"
#include "camera.h"

// Animated 4-direction sprites.
//
// Every CraftPix character sheet in this project is laid out the same way:
// a fixed-size square frame, one row per facing in the order down / left /
// right / up, and one column per frame. So a clip only has to name its sheet
// and its frame count; the frame size falls out of the texture dimensions.
struct AnimClip {
    string sheet;              // full path, resolved at load time
    int    frames = 1;
    float  fps    = 10.0f;
    bool   loop   = true;
};

// Shared, immutable description of one character's whole animation set.
struct SpriteDef {
    string name;
    map<string, AnimClip> clips;
    int   rows = 4;            // facings in the sheet; 1 means non-directional
    float anchor_y = 54.0f;    // where the feet sit inside the frame
    float scale = 1.0f;

    const AnimClip* Find(const string& clip) const;
};

// Loads and owns every SpriteDef, keyed by id (e.g. "player_male", "orc1").
class SpriteLibrary {
public:
    bool Load(const string& json_path);
    const SpriteDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }

private:
    map<string, SpriteDef> defs;
};

// Per-entity playback state pointing at a shared SpriteDef.
class Sprite {
public:
    void SetDef(const SpriteDef* d) { def = d; }
    const SpriteDef* Def() const { return def; }

    // Switching to the clip already playing is a no-op unless restart is set,
    // so callers can drive this straight from movement state each frame.
    void Play(const string& clip, bool restart = false);
    void Update(float dt);

    // True once a non-looping clip has shown its last frame.
    bool Finished() const { return finished; }
    // 0..1 through the current clip.
    float Progress() const;

    void Draw(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
              float world_x, float world_y, SDL_Color tint = {255, 255, 255, 255}) const;

    // Draws with no camera transform, for menus and inventory panels.
    void DrawAt(SDL_Renderer* r, TextureCache& cache,
                const SDL_FRect& dst, SDL_Color tint = {255, 255, 255, 255}) const;

    // On-screen size of the current frame, used for hit feedback placement.
    SDL_FRect WorldBounds(float world_x, float world_y) const;

    Facing facing = FACE_DOWN;
    string current;

private:
    const SpriteDef* def = nullptr;
    const AnimClip*  clip = nullptr;
    int   frame = 0;
    float timer = 0.0f;
    bool  finished = false;
};
