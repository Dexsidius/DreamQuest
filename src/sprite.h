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
// Where a layer sits in a character, so the engine knows what it may recolour
// or hide. The CraftPix character packs ship their frames already split this
// way, which is what makes worn equipment possible without new art.
enum class LayerSlot { Shadow, WeaponBack, Body, Head, WeaponFront, Effect };

LayerSlot LayerSlotFromName(const string& name);

struct AnimLayer {
    LayerSlot slot = LayerSlot::Body;
    string    sheet;           // full path, resolved at load time
};

// How a character's layers should be drawn right now: armour tints the body
// and head, the weapon layers take the colour of what is held, and an empty
// hand hides them entirely.
struct LayerStyle {
    SDL_Color body{255, 255, 255, 255};
    SDL_Color head{255, 255, 255, 255};
    SDL_Color weapon{255, 255, 255, 255};
    bool show_weapon = true;
};

struct AnimClip {
    string sheet;              // full path, resolved at load time
    int    frames = 1;         // columns in the sheet
    float  fps    = 10.0f;
    bool   loop   = true;

    // Several CraftPix sheets are padded to the width of their longest row.
    // The player's idle has twelve frames facing down, left and right but only
    // four facing up, and the rest of that row is empty -- playing all twelve
    // makes the character vanish for two thirds of the loop. When a sheet is
    // ragged like that, this holds the real count for each direction row.
    vector<int> row_frames;

    // Drawn in order when the character was imported as separate parts. Empty
    // means this clip is a single flattened sheet.
    vector<AnimLayer> layers;

    int FramesForRow(int row) const {
        if (row < 0 || row >= static_cast<int>(row_frames.size())) return frames;
        return std::max(1, row_frames[row]);
    }
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
    // Frames available for the direction currently being faced.
    int FrameCount() const;

    void Draw(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
              float world_x, float world_y, SDL_Color tint = {255, 255, 255, 255}) const;

    // Draws with no camera transform, for menus and inventory panels.
    void DrawAt(SDL_Renderer* r, TextureCache& cache,
                const SDL_FRect& dst, SDL_Color tint = {255, 255, 255, 255}) const;

    // On-screen size of the current frame, used for hit feedback placement.
    SDL_FRect WorldBounds(float world_x, float world_y) const;

    Facing facing = FACE_DOWN;
    string current;

    // Owner-supplied; ignored by sprites that are not layered.
    LayerStyle style;
    // Layered drawing can be turned off for menu previews, which want the
    // character as authored rather than wearing anything.
    bool use_layers = true;

private:
    // Returns false when this clip has no layer stack to draw.
    bool DrawLayers(SDL_Renderer* r, TextureCache& cache,
                    const SDL_FRect& dst, int shown, int row,
                    SDL_Color tint) const;

    const SpriteDef* def = nullptr;
    const AnimClip*  clip = nullptr;
    int   frame = 0;
    float timer = 0.0f;
    bool  finished = false;
};
