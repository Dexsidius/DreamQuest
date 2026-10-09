// Light out of doors and in: the leaves' shade under every tree, moving as
// the leaves do; shafts of sun down through the Whisperwood's canopy; the
// moon on still water at night; and a room's window light. See Ambience.
#include "ambience.h"
#include "../systems/shaders.h"

namespace {
float Hash01(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^
                 static_cast<uint32_t>(seed) * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}
}   // namespace

void Ambience::DrawDapples(SDL_Renderer* r, const Camera& cam) const {
    if (!lively || !dapples || place.trees.empty() || !OutdoorsKind()) return;
    const float k = std::clamp((daylight - 0.25f) / 0.5f, 0.0f, 1.0f) * (1.0f - 0.6f * rain_shown);
    if (k <= 0.02f) return;
    const SDL_FRect view = cam.VisibleWorldRect(80.0f);
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetTextureColorMod(dapples, 14, 28, 22);
    constexpr int size = 64;
    for (const Spot& t : place.trees) {
        if (t.x < view.x || t.x > view.x + view.w || t.y < view.y || t.y > view.y + view.h) continue;
        // Under the crown and off a little to the east, where the afternoon
        // throws it: as wide as the crown and flattened onto the ground.
        const float w = std::clamp(t.w * 1.15f, 20.0f, 150.0f);
        const float h = w * 0.5f;
        const SDL_FRect at = {t.x - w * 0.42f, t.y - h * 0.62f, w, h};
        // Which way the leaves are, a frame of the four, as the gusts move them.
        const float g = Shaders::GustAt(t.x, t.y);
        const int frame = static_cast<int>(now * (1.0f + 3.0f * g) + Hash01(static_cast<int>(t.x), static_cast<int>(t.y), 5) * 4.0f) %
                          DAPPLE_FRAMES;
        const SDL_FRect src = {static_cast<float>(frame * size), 0.0f, static_cast<float>(size), static_cast<float>(size)};
        SDL_SetTextureAlphaMod(dapples, static_cast<Uint8>(255.0f * 0.34f * k));
        const SDL_FRect dst = cam.ToScreenRect(at);
        SDL_RenderTexture(r, dapples, &src, &dst);
    }
    SDL_SetTextureAlphaMod(dapples, 255);
}

void Ambience::DrawShafts(SDL_Renderer* r, const Camera& cam) const {
    // Only where the trees close over: the Whisperwood and the Brackenwood.
    if (!lively || !beam || kind != Kind::Forest) return;
    const float k = std::clamp((daylight - 0.5f) / 0.4f, 0.0f, 1.0f) * (1.0f - rain_shown);
    if (k <= 0.02f) return;
    const SDL_FRect view = cam.VisibleWorldRect(200.0f);
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    // Each shaft belongs to a square of the ground, so it stays where it is as
    // the view moves: one in a few squares has a gap in the canopy over it.
    constexpr float cell = 230.0f;
    const int c0 = static_cast<int>(std::floor(view.x / cell)), c1 = static_cast<int>(std::floor((view.x + view.w) / cell));
    const int r0 = static_cast<int>(std::floor(view.y / cell)), r1 = static_cast<int>(std::floor((view.y + view.h) / cell));
    SDL_SetTextureColorMod(beam, 255, 236, 186);
    for (int cy = r0; cy <= r1; ++cy)
        for (int cx = c0; cx <= c1; ++cx) {
            if (Hash01(cx, cy, 71) > 0.6f) continue;
            const float x = (cx + 0.2f + 0.6f * Hash01(cx, cy, 72)) * cell;
            const float y = (cy + 0.2f + 0.6f * Hash01(cx, cy, 73)) * cell;
            // Coming and going as the leaves over it move.
            const float breathe = 0.55f + 0.45f * sinf(now * (0.25f + 0.2f * Hash01(cx, cy, 74)) + Hash01(cx, cy, 75) * 6.28f);
            const float wide = 56.0f + 50.0f * Hash01(cx, cy, 76), length = 220.0f + 90.0f * Hash01(cx, cy, 77);
            SDL_SetTextureAlphaMod(beam, static_cast<Uint8>(255.0f * 0.3f * k * breathe));
            // Down from the upper left, as the sun comes: drawn from its top,
            // turned about it.
            const SDL_FRect dst = cam.ToScreenRect({x - wide / 2.0f, y - length, wide, length});
            const SDL_FPoint pivot = {dst.w / 2.0f, 0.0f};
            SDL_RenderTextureRotated(r, beam, nullptr, &dst, -28.0, &pivot, SDL_FLIP_NONE);
        }
    SDL_SetTextureAlphaMod(beam, 255);
    SDL_SetTextureColorMod(beam, 255, 255, 255);
}

void Ambience::DrawMoon(SDL_Renderer* r, const Camera& cam) const {
    // The moon on the water: still enough to hold it, on a clear night.
    if (!lively || place.water_rects.empty() || !OutdoorsKind() || kind == Kind::Ash) return;
    const float k = std::clamp((0.4f - daylight) / 0.3f, 0.0f, 1.0f) * (1.0f - rain_shown);
    if (k <= 0.02f) return;
    const SDL_FRect view = cam.VisibleWorldRect(0.0f);
    // Where it shows is where the eye is: up and to the right of the middle of
    // the view, moving with it, as a reflection does.
    const float mx = view.x + view.w * 0.6f, my = view.y + view.h * 0.5f;
    const float z = cam.zoom;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_Rect old_clip{};
    const bool clipped = SDL_RenderClipEnabled(r);
    if (clipped) SDL_GetRenderClipRect(r, &old_clip);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const SDL_FRect& w : place.water_rects) {
        if (w.x > mx + 60.0f || w.x + w.w < mx - 60.0f || w.y > my + 90.0f || w.y + w.h < my - 20.0f) continue;
        const SDL_FRect s = cam.ToScreenRect(w);
        const SDL_Rect clip = {static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                               static_cast<int>(std::ceil(s.w)) + 1, static_cast<int>(std::ceil(s.h)) + 1};
        SDL_SetRenderClipRect(r, &clip);
        const SDL_FPoint c = cam.ToScreen(mx, my);
        // The disc, wavering a pixel row by row.
        const float rad = 7.0f;
        for (int row = -static_cast<int>(rad); row <= static_cast<int>(rad); ++row) {
            const float half = std::sqrt(std::max(0.0f, rad * rad - static_cast<float>(row * row))) * 1.4f;
            const float shift = roundf(sinf(now * 2.2f + row * 0.9f) * 0.8f);
            SDL_SetRenderDrawColor(r, 236, 240, 255, static_cast<Uint8>(150.0f * k));
            const SDL_FRect line = {roundf((c.x + (shift - half) * z) / z) * z, roundf((c.y + row * z * 0.6f) / z) * z,
                                    roundf(half * 2.0f) * z, z};
            SDL_RenderFillRect(r, &line);
        }
        // And its road on the water, toward whoever is looking: broken bars
        // of light that come and go.
        for (int b = 1; b < 14; ++b) {
            const float y = c.y + (rad * 0.6f + b * 2.6f) * z;
            const float flick = 0.5f + 0.5f * sinf(now * 3.1f + b * 1.7f);
            if (flick < 0.25f) continue;
            const float half = (2.0f + 3.0f * flick) * (1.0f - b / 16.0f);
            const float shift = roundf(sinf(now * 1.7f + b * 2.3f) * 2.0f);
            SDL_SetRenderDrawColor(r, 200, 214, 250, static_cast<Uint8>(150.0f * k * flick * (1.0f - b / 15.0f)));
            const SDL_FRect bar = {roundf((c.x + (shift - half) * z) / z) * z, roundf(y / z) * z, roundf(half * 2.0f) * z, z};
            SDL_RenderFillRect(r, &bar);
        }
    }
    SDL_SetRenderClipRect(r, clipped ? &old_clip : nullptr);
}

void Ambience::DrawRoomLight(SDL_Renderer* r, const Camera& cam) const {
    // Light falling into a room from its windows, while the day is out there.
    if (!lively || kind != Kind::Room || !beam || place.windows.empty()) return;
    const float k = std::clamp((sun - 0.3f) / 0.5f, 0.0f, 1.0f);
    if (k <= 0.02f) return;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetTextureColorMod(beam, 255, 232, 180);
    for (const Spot& w : place.windows) {
        const float breathe = 0.85f + 0.15f * sinf(now * 0.4f + w.x * 0.01f);
        SDL_SetTextureAlphaMod(beam, static_cast<Uint8>(255.0f * 0.22f * k * breathe));
        const float wide = std::max(18.0f, w.w * 0.9f), length = 90.0f;
        const SDL_FRect dst = cam.ToScreenRect({w.x - wide / 2.0f, w.y - w.h * 0.6f, wide, length});
        const SDL_FPoint pivot = {dst.w / 2.0f, 0.0f};
        SDL_RenderTextureRotated(r, beam, nullptr, &dst, 14.0, &pivot, SDL_FLIP_NONE);
    }
    SDL_SetTextureAlphaMod(beam, 255);
    SDL_SetTextureColorMod(beam, 255, 255, 255);
}
