#pragma once
#include "../headers.h"

class Map;
class Camera;

// ---------------------------------------------------------------------------
//  Shaders
//
//  The game draws through SDL's GPU renderer, on Vulkan, and three little
//  fragment shaders of its own ride on top of it (src/shaders, compiled to
//  assets/shaders by build.ps1):
//
//    water  -- every water tile: the art runs along the current and sways,
//              with glints and foam at the banks. Ponds only sway and glitter.
//    lava   -- every lava tile: the crust drifts downstream and churns, hot
//              spots pulse, the odd bubble swells.
//    heat   -- the air over lava wavers, a pixel either way, and everything
//              standing in it wavers with it.
//
//  Which way water runs is worked out once per map from the shape of its
//  water (a long narrow run is a river; a wide one is a pond) into a small
//  texture, the field, that all three read.
//
//  None of it is needed. If the GPU renderer cannot be had -- no Vulkan, or
//  SDL_RENDER_DRIVER set to something else -- the game makes whatever
//  renderer SDL would have and every call here is a harmless no-op: the
//  water and the lava are simply still, as they always were.
// ---------------------------------------------------------------------------

namespace Shaders {

// What a ground tile is made of, as far as the shaders care.
enum Surface : Uint8 { PLAIN = 0, WATER = 1, LAVA = 2 };

// From the tile's art: assets/tiles/water.png, water_1 ... bog_water_2 are
// water; lava and lava_1 are lava. A lava_bridge is not lava.
Surface SurfaceOfTile(const string& path);

// The GPU renderer on Vulkan if it can be had, else whatever SDL would pick.
// Setting SDL_RENDER_DRIVER (or SDL_GPU_DRIVER) in the environment overrides
// the choice, which is how to compare against Direct3D 11.
SDL_Renderer* CreateRenderer(SDL_Window* window);

bool Init(SDL_Renderer* renderer);   // false (and no effects) off the GPU renderer
void Shutdown();                     // before the renderer is destroyed
bool Enabled();

// Before drawing a view of the world into the current target. Returns a
// texture to draw the world into instead, when there is lava near enough to
// shimmer -- hand it to DrawHeat afterwards, with the target put back -- or
// nullptr to draw straight through.
SDL_Texture* BeginView(SDL_Renderer* renderer, const Map& map, const Camera& camera);

// Switch the shader the next tiles are drawn with. Cheap when nothing changes.
void UseSurface(SDL_Renderer* renderer, Surface surface);

// Draw the world drawn into BeginView's texture onto the current target,
// wavering over the lava.
void DrawHeat(SDL_Renderer* renderer, SDL_Texture* scene);

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

}  // namespace Shaders
