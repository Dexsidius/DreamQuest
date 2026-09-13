#pragma once
#include "../headers.h"
#include "../camera.h"

// ---------------------------------------------------------------------------
//  Lighting
//
//  Night is a light map multiplied over the finished scene. Each frame a
//  screen-sized target is cleared to the colour of the ambient light -- white
//  at noon, amber at sunset, a cold blue at midnight -- and every light is
//  added onto it as a soft round glow. The target is then drawn over the world
//  with a multiply, so where the map is white the scene is untouched and where
//  it is blue the scene is dimmed toward blue, except in the pools the fires
//  cast.
//
//  At full daylight with no lights, nothing is drawn at all.
// ---------------------------------------------------------------------------

struct Light {
    float x = 0, y = 0;          // world pixels
    float radius = 96.0f;        // world pixels to where it fades out
    SDL_Color color{255, 200, 140, 255};
    float intensity = 1.0f;      // 0..1
};

class Lighting {
public:
    // Multiplies the light map over whatever has been drawn so far.
    void Render(SDL_Renderer* r, const Camera& cam, SDL_Color ambient,
                const vector<Light>& lights);

private:
    bool Prepare(SDL_Renderer* r);

    // Owned by the renderer, which frees them when it goes; the world can
    // outlive the renderer at shutdown, so these are never destroyed here.
    SDL_Renderer* owner = nullptr;
    SDL_Texture*  target = nullptr;
    SDL_Texture*  glow = nullptr;
    int target_w = 0, target_h = 0;
};
