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
// Draw order is the order the sheets are listed in, which comes from the
// number in each layer's filename. WeaponAlt is every tier's weapon sheet: they
// all sit at the weapon's own index and only one of them is ever drawn, chosen
// by the model in hand, so the rest are skipped rather than stacked -- which is
// exactly what used to happen, and the character went about holding a sword, a
// spear, a bow and a staff at once.
enum class LayerSlot {
    Shadow, WeaponBack, Body, Head, WeaponFront, WeaponAlt, Effect,
    // Worn plate, each piece its own sheet so a bronze cuirass over iron
    // greaves is drawn as exactly that.
    ArmourLegs, ArmourBody, ArmourHands, ArmourHead, ArmourShield,
    // The same five pieces in another cut -- armour_body_light and the like.
    // Like WeaponAlt these are alternates, never drawn on their own: the piece
    // that is worn picks its cut and the plain sheet swaps to that one.
    ArmourAlt,
};

// The five armour layers, in the order they are drawn.
enum ArmourLayer { ARMOUR_LEGS, ARMOUR_BODY, ARMOUR_HANDS, ARMOUR_HEAD,
                   ARMOUR_SHIELD, ARMOUR_LAYER_COUNT };

// Which armour layer a slot paints, or -1 for a slot that is not armour.
int ArmourLayerOf(LayerSlot slot);

LayerSlot LayerSlotFromName(const string& name);

struct AnimLayer {
    LayerSlot slot = LayerSlot::Body;
    string    sheet;           // full path, resolved at load time
};

// A piece of worn kit drawn on top of the character's own layers.
//
// The rectangle is given in frame pixels -- coordinates inside the 64x64 (or
// 32x32) animation frame -- so a helmet is authored once against the rig and
// lands correctly whatever the camera zoom is. Art that only has one view can
// be limited to the facings it actually reads in.
struct Attachment {
    // Held art is drawn on the character's left, which is right for three of
    // the four facings. Facing right it would be held backwards, so a weapon
    // asks to be mirrored across the character for that one.
    bool      mirror_facing_right = false;
    string    sprite;
    LayerSlot after = LayerSlot::Head;    // drawn immediately after this layer
    SDL_FRect rect{24, 17, 16, 16};       // in frame pixels
    bool      facings[4] = {true, true, true, true};   // down, left, right, up
    SDL_Color tint{255, 255, 255, 255};
};

// A worn piece that has art of its own: whether to draw that layer at all, and
// the metal to paint it. The sheets are rendered in pale steel so a colour
// multiply lands where it should -- bronze, iron, azuryte and the rest are the
// same plate in a different metal, and each slot carries its own, so mixing
// tiers looks like mixing tiers.
struct WornLayer {
    bool      show = false;
    SDL_Color tint{255, 255, 255, 255};
    // Which cut of armour: "light" for hide and mail, "ornate" for the horned
    // and winged harness, empty for the plain plate the sheets are named
    // after. So a bronze jerkin and a demonite warplate are not the same
    // silhouette in two colours.
    string    cut;
};

// How a character's layers should be drawn right now: what plate is worn and in
// what metal, the weapon layers take the colour of what is held, and an empty
// hand hides them entirely. Body and head keep a tint for anything worn that
// has no art of its own.
struct LayerStyle {
    SDL_Color body{255, 255, 255, 255};
    SDL_Color head{255, 255, 255, 255};
    SDL_Color weapon{255, 255, 255, 255};
    WornLayer armour[ARMOUR_LAYER_COUNT];
    bool show_weapon = true;
    // The weapon model in hand, "sword_iron" and so on. A rig that has a layer
    // sheet for it draws that, in its own colours, in place of its sword;
    // one that does not falls back to the sword, tinted.
    string weapon_model;
    // And a second one in the other hand -- "dagger_iron" -- drawn from its own
    // sheets, layers/<clip>_4_weapon_off_<model>.png, straight after the first.
    string offhand_model;
    // Worn pieces, in the order they should be drawn.
    vector<Attachment> attachments;
};

struct AnimClip {
    string sheet;              // full path, resolved at load time
    int    frames = 1;         // columns in the sheet
    float  fps    = 10.0f;
    bool   loop   = true;
    // A strike that is played to last exactly as long as the attack it belongs
    // to, however long that is: see Player::FitSwing. `fps` is what it plays at
    // when nothing is fitting it.
    bool   fit    = false;

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
    // Where this character's own folder is, and the folder the weapon in hand
    // is drawn from. The three playable characters are one rig in three sets
    // of clothes, so every tier's sword, bow, staff and spear is rendered once,
    // in the hero's hand, and the warden and the wayfarer hold the same
    // sheets: "weapons_from" in sprites.json names whose. Empty is their own.
    string dir, weapon_dir;
    int   rows = 4;            // facings in the sheet; 1 means non-directional
    float anchor_y = 54.0f;    // where the feet sit inside the frame
    float scale = 1.0f;

    const AnimClip* Find(const string& clip) const;
    // The sheet that draws `model` ("bow_wood", "sword_iron") in hand over a
    // generic weapon layer: layers/attack_4_weapon_front.png becomes
    // layers/attack_4_weapon_sword_iron.png, in `weapon_dir` if there is one.
    // Empty if the layer is not a weapon layer.
    string WeaponSheet(const string& generic_sheet, const string& model) const;
};

// Loads and owns every SpriteDef, keyed by id (e.g. "player_male", "orc1").
class SpriteLibrary {
public:
    bool Load(const string& json_path);
    const SpriteDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }
    // Every sprite there is, for anything that has to walk the whole set --
    // the self-test measures each sheet's rows against each other.
    vector<string> Ids() const {
        vector<string> out;
        out.reserve(defs.size());
        for (const auto& kv : defs) out.push_back(kv.first);
        return out;
    }

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
    // The frame showing, and a way to put the clip on one: for a figure posed
    // from outside rather than played -- a friend, drawn from what the server
    // says. Clamped to the frames the facing has.
    int  Frame() const { return frame; }
    void SetFrame(int f) {
        const int count = FrameCount();
        frame = count > 0 ? std::clamp(f, 0, count - 1) : 0;
        timer = 0.0f;
    }
    // Frames available for the direction currently being faced.
    int FrameCount() const;
    // Side of one animation frame, in source pixels.
    int FrameSize() const;

    // `blend` and `grow` let the same frame be drawn as an effect: `grow` scales
    // it about the middle of the frame, so a copy drawn a little larger in one
    // flat colour, behind the real thing, is a halo round its silhouette. That
    // is how a leader glows red while it charges.
    void Draw(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
              float world_x, float world_y, SDL_Color tint = {255, 255, 255, 255},
              SDL_BlendMode blend = SDL_BLENDMODE_BLEND, float grow = 1.0f) const;

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

    // Multiplier on playback speed. A dagger swings in two thirds the time a
    // sword does, and the animation has to agree with that or the character is
    // still following through when the hitbox has already gone.
    float speed_scale = 1.0f;
    // Drawn this much bigger than the sheet's own scale: a boss is the same art
    // as its kin, only larger.
    float size_scale = 1.0f;

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
