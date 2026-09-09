#pragma once
#include "headers.h"

// Viewport "through the player's lens".
//
// Keeps the offset convention from LevelEdit-Plus (screen = world + pos) so
// maps authored in the editor line up with what the game draws, and adds the
// zoom / dead-zone follow / bounds clamping a scrolling game needs. Maps are
// free to be much larger than the window; the camera is what makes that work.
class Camera {
public:
    Camera(float view_w, float view_h, float deadzone = 96.0f);

    // Follow target (a world-space point, normally the player's feet).
    void Follow(float world_x, float world_y, float dt);
    // Jump straight to a target with no easing (map changes, loading a save).
    void SnapTo(float world_x, float world_y);

    void SetViewport(float w, float h);
    void SetBounds(float w, float h);          // map size in world pixels
    void SetZoom(float z);

    // world <-> screen
    SDL_FPoint ToScreen(float wx, float wy) const;
    SDL_FPoint ToWorld(float sx, float sy) const;
    SDL_FRect  ToScreenRect(const SDL_FRect& world) const;

    // World-space rectangle currently visible, padded for culling.
    SDL_FRect VisibleWorldRect(float pad = 64.0f) const;

    void DebugDraw(SDL_Renderer* renderer) const;

    float xpos = 0, ypos = 0;   // offset added to world coords (pre-zoom)
    float zoom = 2.0f;

private:
    void Clamp();

    float view_w, view_h;
    float dead;                 // half-size of the dead zone, in screen px
    float bounds_w = 0, bounds_h = 0;
};
