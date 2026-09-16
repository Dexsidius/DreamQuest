#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "ui.h"

// -----------------------------------------------------------------------------
//  The title backdrop: the game's cover painting behind the front-end menus,
//  with a night sky moving over it.
//
//  The painting is the one piece of art in the repository rather than under
//  assets/ -- it is the game's own, not CraftPix content -- so the menus have
//  something to stand on in a fresh checkout, before any pack is imported.
//
//  It is drawn straight from the source file, cropped to whatever shape the
//  window is and scaled to fill it, so there is no pre-rendered background at
//  some guessed resolution to keep in step with the window. The crop is biased
//  upward: the composition is a sword under a crescent moon, and a centred
//  16:9 band off a square canvas cuts the top off the moon.
//
//  Over the top of it: stars that breathe, a few four-pointed sparkles in the
//  painting's own style, and the occasional meteor. They are laid out away from
//  the middle column, where the moon, the blade and the menu panel are, so the
//  animation frames the art instead of crawling over it.
// -----------------------------------------------------------------------------

namespace TitleScreen {
    inline constexpr const char* kArtPath  = "art/dreamquest_cover.png";
    inline constexpr const char* kIconPath = "art/app_icon.png";
}

class TitleBackdrop {
public:
    // Paints the whole viewport: art, scrim, sky. Safe to call with the art
    // missing -- it falls back to the flat menu colour and still shows stars.
    void Draw(SDL_Renderer* renderer, TextureCache& textures, UI& ui);

private:
    struct Star {
        float x = 0, y = 0;       // fractions of the viewport
        float size = 1.0f;        // in 720p-relative pixels
        float base = 0.6f;        // how bright it sits at rest
        float speed = 1.0f;       // twinkles per second
        float phase = 0.0f;
        bool  sparkle = false;    // drawn as a four-pointed star
        SDL_Color colour{255, 255, 255, 255};
    };
    struct Meteor {
        bool  alive = false;
        float x = 0, y = 0, vx = 0, vy = 0;
        float life = 0, span = 1.0f;
    };

    void Seed();
    void Advance(float dt, float aspect);

    vector<Star> stars;
    Meteor meteors[2];
    bool   seeded = false;
    float  clock = 0.0f;          // seconds since the menu first drew
    float  last_ticks = -1.0f;
    float  next_meteor = 3.0f;
    std::mt19937 rng{0xD6EA33u};
};
