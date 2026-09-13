#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "ui.h"

class World;

// Clips one row of the glass to the edges of the baked image, shifting where it
// lands by however much came off the left. Returns the width left to draw,
// which is zero or less when that row is entirely off the map -- which is what
// happens every frame the player stands near a corner of the world.
inline float MinimapClipSpan(float& src_x, float& dst_x, float width, int img_w) {
    if (src_x < 0.0f) {
        dst_x  -= src_x;        // src_x is negative: the row starts further right
        width  += src_x;
        src_x   = 0.0f;
    }
    if (src_x + width > static_cast<float>(img_w))
        width = static_cast<float>(img_w) - src_x;
    return width;
}

// The round map in the corner of the HUD.
//
// The terrain is baked once per map into a small image -- one pixel to every
// SCALE world pixels -- because sampling seventeen thousand tiles every frame
// to fill a 124-pixel circle would be absurd. Drawing it is then a stack of
// one-pixel-tall strips, each as wide as the circle is at that height, which is
// what makes the map round without a mask texture or a shader, and keeps it at
// one image pixel per screen pixel so it stays crisp.
class Minimap {
public:
    ~Minimap();

    // World pixels to one minimap pixel. Drawn 1:1, so this is also how far the
    // glass sees: radius * scale world pixels in every direction. A house is a
    // fraction of the size of the overworld, and at the coarse scale a room
    // came out as a thumbnail adrift in an empty dial, so small maps are baked
    // at the finer one.
    static constexpr int SCALE_COARSE = 8;
    static constexpr int SCALE_FINE   = 4;
    static constexpr float FINE_UNDER = 1600.0f;   // maps smaller than this, either way

    void Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
              float cx, float cy, float radius);

    // Drops the baked image; the next draw rebuilds it. Called when the
    // renderer is going away.
    void Forget();

private:
    bool Build(SDL_Renderer* r, TextureCache& cache, const World& world);

    SDL_Texture* terrain = nullptr;
    string built_for;               // map id the image was baked from
    int    img_w = 0, img_h = 0;
    int    scale = SCALE_COARSE;    // world pixels per dot, chosen per map
};
