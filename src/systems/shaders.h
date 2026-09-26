#pragma once
#include "../headers.h"

class Map;
class Camera;

// ---------------------------------------------------------------------------
//  Shaders
//
//  The game draws through SDL's GPU renderer, on Vulkan, and a handful of
//  fragment shaders of its own ride on top of it (src/shaders, compiled to
//  assets/shaders by build.ps1):
//
//    water    -- every water tile: the art runs along the current and sways,
//                with glints and foam at the banks, and rings spread where
//                something breaks the surface.
//    lava     -- every lava tile: the crust drifts downstream and churns, hot
//                spots pulse, the odd bubble swells.
//    sprite   -- a character with something to show: the flash of a blow, a
//                heavy's glow, what a status is doing to it, how it dies.
//    prop     -- scenery that moves or lights: plants in the wind, cloth,
//                windows that come on at dusk, a fountain, a woken waystone,
//                and the glow of what is lit from inside after dark.
//    fx       -- shapes of light: the Mana Shield's dome, an Electro-Node,
//                stained glass on a floor, the halo round a lamp.
//    fog      -- ground fog over the Bayou's water, the graveyard, the crypt.
//    reflect  -- scenery seen upside down in the water beside it, and the
//                palace's towers in their moat.
//    post     -- the whole view on its way to the screen: heat over lava and
//                fires, shockwaves, the dream's swim and bloom, each place's
//                colours, a flash.
//
//  Which way water runs is worked out once per map from the shape of its
//  water (a long narrow run is a river; a wide one is a pond) into a small
//  texture, the field, that several of them read.
//
//  None of it is needed. If the GPU renderer cannot be had -- no Vulkan, or
//  SDL_RENDER_DRIVER set to something else -- or the player turns Visual
//  Effects off, every call here is a harmless no-op and the game draws as it
//  always did.
// ---------------------------------------------------------------------------

namespace Shaders {

// What a ground tile is made of, as far as the shaders care.
enum Surface : Uint8 { PLAIN = 0, WATER = 1, LAVA = 2 };

// From the tile's art: assets/tiles/water.png, water_1 ... bog_water_2 are
// water; lava and lava_1 are lava. A lava_bridge is not lava.
Surface SurfaceOfTile(const string& path);

// How a piece of scenery moves or lights, from its art's name.
enum PropKind : Uint8 {
    PROP_NONE = 0,
    PROP_GRASS,       // tufts, sedge, flowers, reeds, herbs: all of it sways
    PROP_TREE,        // trees and bushes: the crown sways, less
    PROP_CLOTH,       // banners and tapestries, hung from the top
    PROP_STAKED,      // a tent, a banner on its pole: held at the foot as well
                      // as the top, so only the middle of it stirs
    PROP_WINDOWS,     // a building: its windows are glass by day and lit at night
    PROP_FOUNTAIN,    // a fountain, a well, a trough: the water in it runs
    PROP_PULSE,       // a woken waystone, a crystal: the light in it breathes
    PROP_KINDS
};
struct Art {
    PropKind kind = PROP_NONE;
    bool glows = false;     // lit from inside: shines through the night
    bool hot = false;       // a fire: the air over it wavers
    bool halo = false;      // a light: the air round it glows after dark
};
const Art& ArtOf(const string& path);

// The player's choices, from the Visual Effects page of Options.
struct Options {
    bool effects = true;      // all of it; off is the plain look
    bool shake = true;        // the screen shakes under a big blow
    bool flashes = true;      // the screen and a struck sprite flash
    bool fringing = true;     // colours come apart in a dream and a shockwave
    bool distortion = true;   // heat, shockwaves and the dream bend the picture
};
void SetOptions(const Options& o);
const Options& GetOptions();

// The GPU renderer on Vulkan if it can be had, else whatever SDL would pick.
// Setting SDL_RENDER_DRIVER (or SDL_GPU_DRIVER) in the environment overrides
// the choice, which is how to compare against Direct3D 11.
SDL_Renderer* CreateRenderer(SDL_Window* window);

bool Init(SDL_Renderer* renderer);   // false (and no effects) off the GPU renderer
void Shutdown();                     // before the renderer is destroyed
bool Enabled();                      // the shaders are loaded
bool Effects();                      // ...and the player wants them

// Before drawing a view of the world into the current target. Returns a
// texture to draw the world into instead -- hand it to DrawPost afterwards,
// with the target put back -- or nullptr to draw straight through.
SDL_Texture* BeginView(SDL_Renderer* renderer, const Map& map, const Camera& camera);

// What the world tells the shaders about the view it is drawing: given once,
// at the start of World::Render, before anything is drawn.
struct Ripple   { float x = 0, y = 0, age = 0, strength = 0; };     // world px; seconds old
struct Shock    { float x = 0, y = 0, radius = 0, strength = 0; };  // in the view's pixels
struct HeatSpot { float x = 0, y = 0, radius = 0, strength = 0; };  // world px
struct Fog {
    bool  on = false;
    float r = 0.78f, g = 0.82f, b = 0.84f;
    float density = 0.3f;
    float by_water = 0.0f;         // how much thicker over and beside water
    float drift = 6.0f;            // world px a second
    SDL_FRect region{};            // only in here (w == 0: everywhere), soft at the edge
};
struct Frame {
    float night = 0.0f;            // how dark, 0..1: windows light, glows show
    float wind = 0.7f;             // how hard it blows here, 0..1
    vector<Ripple> ripples;
    Fog fog;
    float grade[4] = {1, 1, 1, 1}; // rgb multiply; saturation
    float contrast = 1.0f, lift = 0.0f;
    float flash[4] = {0, 0, 0, 0}; // rgb; how much
    vector<Shock> shocks;
    vector<HeatSpot> heats;
    float dream = 0.0f;            // how far down the Reverie, as a strength
};
void SetFrame(const Frame& frame);

// Switch the shader the next ground tiles or scenery are drawn with. Cheap
// when nothing changes; PLAIN and PROP_NONE is no shader at all.
void UseTile(SDL_Renderer* renderer, Surface surface, PropKind kind = PROP_NONE);
inline void UseSurface(SDL_Renderer* renderer, Surface surface) { UseTile(renderer, surface); }
void UsePlain(SDL_Renderer* renderer);
// The glow pass: only the lit pixels of what is drawn, meant to be added.
bool UseGlow(SDL_Renderer* renderer);
// Reflections: what is drawn shows only over water or lava, dimmed and wavering.
bool UseReflection(SDL_Renderer* renderer);

// A character with something happening to it. UseSprite sets up the next
// draw of `src` out of a sheet `tw` by `th`; EndSprite puts things back.
struct SpriteFx {
    SDL_FColor flash{1, 1, 1, 0};        // to this colour, by a
    SDL_FColor glow{1, 0.2f, 0.1f, 0};   // round its outline, by a
    float burn = 0, cold = 0, electrified = 0, poison = 0, wet = 0, bleed = 0;
    float dissolve = 0;                  // dying, 0..1
    int   dissolve_kind = 0;             // 0 plainly, 1 to dust, 2 to embers, 3 into the air
    float seed = 0;
    bool  rim = true;                    // may draw past its outline (not a layer over the body)
    bool Any() const {
        return flash.a > 0 || glow.a > 0 || burn > 0 || cold > 0 || electrified > 0 ||
               poison > 0 || wet > 0 || bleed > 0 || dissolve > 0;
    }
};
bool UseSprite(SDL_Renderer* renderer, const SpriteFx& fx, const SDL_FRect& src, float tw, float th);
void EndSprite(SDL_Renderer* renderer);

// A shape of light over `dst` (screen pixels).
enum Shape { SHAPE_DOME = 0, SHAPE_NODE = 1, SHAPE_GLASS = 2, SHAPE_HALO = 3,
             // The combo strikes' marks (World::DrawStrikes). Each reads its
             // four numbers from hit_x, hit_y, hit_age and extra: see fx.frag.
             SHAPE_SLASH = 4, SHAPE_IMPACT = 5, SHAPE_THRUST = 6, SHAPE_CROSS = 7, SHAPE_CIRCLE = 8,
             // The techniques' and the abilities' marks, the same way.
             SHAPE_VORTEX = 9, SHAPE_CRACKS = 10, SHAPE_PILLAR = 11, SHAPE_SIGIL = 12,
             SHAPE_STREAK = 13, SHAPE_RETICLE = 14, SHAPE_SHARDS = 15, SHAPE_WAVE = 16,
             SHAPE_LAST_STRIKE = SHAPE_WAVE };
struct ShapeFx {
    Shape shape = SHAPE_HALO;
    float fade = 1.0f, seed = 0.0f;
    SDL_FColor colour{1, 1, 1, 1};       // a is the strength
    float foot = 0.0f;                   // the dome: its ring on the ground, as a share of its height
    float hit_x = 0, hit_y = 0, hit_age = -1.0f;   // the dome: where it was struck, how long ago (0..1)
    float extra = 0.0f;                            // the fourth of a strike's numbers
};
bool DrawShape(SDL_Renderer* renderer, const SDL_FRect& dst, const ShapeFx& fx, SDL_BlendMode blend);

// The fog of this view, over the whole of it (a no-op where there is none).
void DrawFog(SDL_Renderer* renderer);

// Draw the world drawn into BeginView's texture onto the current target,
// through the post pass.
void DrawPost(SDL_Renderer* renderer, SDL_Texture* scene);

// What the ground is at a point of the view being drawn -- where it is drawn,
// lifted with its terrain -- as far as the field knows. PLAIN off the GPU.
Surface FluidAt(float x, float y);

// The field as the shaders get it, worked out on the CPU from a map's ground
// tiles: one texel per `texel` world pixels, what the ground is there and
// which way it runs (fx, fy; 0 where it is still). For the self-test.
struct FieldData {
    float texel = 16.0f;
    int cols = 0, rows = 0;
    vector<Uint8> kind;        // Surface
    vector<float> fx, fy;
    bool any = false;          // any water or lava at all
};
FieldData FieldOf(const Map& map);

// The shader files the game loads, for the self-test.
vector<string> ShaderFiles();

}  // namespace Shaders
