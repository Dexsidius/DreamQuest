#include "camera.h"

Camera::Camera(float view_w, float view_h, float deadzone)
    : view_w(view_w), view_h(view_h), dead(deadzone) {}

void Camera::SetViewport(float w, float h) { view_w = w; view_h = h; Clamp(); }
void Camera::SetBounds(float w, float h)   { bounds_w = w; bounds_h = h; Clamp(); }

void Camera::SetZoom(float z) {
    zoom = std::clamp(z, 1.0f, 6.0f);
    Clamp();
}

void Camera::SnapTo(float world_x, float world_y) {
    xpos = view_w / (2.0f * zoom) - world_x;
    ypos = view_h / (2.0f * zoom) - world_y;
    Clamp();
}

void Camera::Follow(float world_x, float world_y, float dt) {
    // Dead zone is expressed in screen pixels; work in world units so the
    // behaviour stays the same at every zoom level.
    const float dz = dead / zoom;
    const float cx = view_w / (2.0f * zoom);
    const float cy = view_h / (2.0f * zoom);

    // Where the target currently sits relative to the centre of the view.
    const float rel_x = (world_x + xpos) - cx;
    const float rel_y = (world_y + ypos) - cy;

    float want_x = xpos, want_y = ypos;
    if (rel_x >  dz) want_x -= (rel_x - dz);
    if (rel_x < -dz) want_x -= (rel_x + dz);
    if (rel_y >  dz) want_y -= (rel_y - dz);
    if (rel_y < -dz) want_y -= (rel_y + dz);

    // Critically-damped-ish smoothing, frame-rate independent.
    const float t = 1.0f - powf(0.0001f, dt);
    xpos += (want_x - xpos) * t;
    ypos += (want_y - ypos) * t;
    Clamp();
}

void Camera::Clamp() {
    if (bounds_w <= 0 || bounds_h <= 0) return;
    const float vw = view_w / zoom;
    const float vh = view_h / zoom;

    // If the map is narrower than the view, centre it rather than cropping.
    if (bounds_w <= vw) xpos = (vw - bounds_w) / 2.0f;
    else                xpos = std::clamp(xpos, -(bounds_w - vw), 0.0f);

    if (bounds_h <= vh) ypos = (vh - bounds_h) / 2.0f;
    else                ypos = std::clamp(ypos, -(bounds_h - vh), 0.0f);
}

SDL_FPoint Camera::ToScreen(float wx, float wy) const {
    return { (wx + xpos) * zoom, (wy + ypos) * zoom };
}

SDL_FPoint Camera::ToWorld(float sx, float sy) const {
    return { sx / zoom - xpos, sy / zoom - ypos };
}

SDL_FRect Camera::ToScreenRect(const SDL_FRect& w) const {
    return { (w.x + xpos) * zoom, (w.y + ypos) * zoom, w.w * zoom, w.h * zoom };
}

SDL_FRect Camera::VisibleWorldRect(float pad) const {
    return { -xpos - pad, -ypos - pad,
             view_w / zoom + pad * 2.0f, view_h / zoom + pad * 2.0f };
}

void Camera::DebugDraw(SDL_Renderer* renderer) const {
    SDL_FRect dz = { view_w / 2 - dead, view_h / 2 - dead, dead * 2, dead * 2 };
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderRect(renderer, &dz);
}
