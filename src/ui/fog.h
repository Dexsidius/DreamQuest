#pragma once
#include "../headers.h"
#include "../systems/exploration.h"

// -----------------------------------------------------------------------------
//  The fog of war, as a picture.
//
//  One pixel to each of a map's exploration squares (Exploration::CELL world
//  pixels on a side): a low, dark cloud, mottled blue-grey so it reads as
//  weather rather than as a hole, as thick as that square is unseen. Whatever
//  draws a map -- the minimap's dial, a page of the map screen -- stretches it
//  over the map with smoothing on, so its edge is a gradient across a square
//  and not a staircase of them. Repainted only when what has been seen has
//  changed; and then only how thick it is, since the cloud's colours are the
//  squares' own.
// -----------------------------------------------------------------------------

class FogTexture {
public:
    ~FogTexture() { Forget(); }

    // How thick the fog is where nothing has been seen, 0..255 -- just short
    // of hiding the ground entirely, so it reads as fog.
    static constexpr int THICK = 246;
    // The cloud's colour at one square: the same from one frame, one picture
    // and one machine to the next. Public so the self-test can tell fog from
    // ground.
    static SDL_Color Colour(int cx, int cy);

    // Brings the picture up to date with what `seen` holds of `map_id`, a map
    // `world_w` by `world_h` pixels: all fog where nothing of it has been seen
    // yet. False when there is nothing to draw with.
    bool Update(SDL_Renderer* r, const Exploration& seen, const string& map_id, float world_w, float world_h);
    SDL_Texture* Texture() const { return tex; }
    // Squares across and down: the source rectangle of the whole map is these
    // times the map's size over CELL, never more.
    int Width() const { return w; }
    int Height() const { return h; }
    void Forget();

private:
    SDL_Texture* tex = nullptr;
    string   map;                 // the map it was painted for
    int      w = 0, h = 0;
    uint32_t stamp = 0;           // the grid's stamp when it was painted
    vector<uint8_t> pixels;       // RGBA, w * h
};
