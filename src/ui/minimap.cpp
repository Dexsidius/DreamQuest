#include "minimap.h"
#include "../world/world.h"

// Off the edge of the world, and under the glass before the terrain lands.
static constexpr SDL_Color VOID_COLOR{12, 10, 9, 235};

Minimap::~Minimap() { Forget(); }

void Minimap::Forget() {
    if (terrain) SDL_DestroyTexture(terrain);
    terrain = nullptr;
    built_for.clear();
}

bool Minimap::Build(SDL_Renderer* r, TextureCache& cache, const World& world) {
    Forget();
    const Map& map = world.CurrentMap();
    if (!map.Loaded() || map.Width() <= 0.0f || map.Height() <= 0.0f) return false;

    // A cottage gets the fine scale, the overworld the coarse one.
    scale = (std::max(map.Width(), map.Height()) < FINE_UNDER) ? SCALE_FINE : SCALE_COARSE;
    img_w = std::max(1, static_cast<int>(ceilf(map.Width() / scale)));
    img_h = std::max(1, static_cast<int>(ceilf(map.Height() / scale)));

    SDL_Surface* s = SDL_CreateSurface(img_w, img_h, SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    SDL_FillSurfaceRect(s, nullptr,
                        SDL_MapSurfaceRGBA(s, VOID_COLOR.r, VOID_COLOR.g, VOID_COLOR.b, 255));

    // Every ground tile in its average colour, so the map reads the way the
    // terrain does: roads pale, water blue, dungeon walls dark. Two passes, the
    // way the world draws it, so a rug or a flight of stairs lands on top of
    // the floor it is laid on rather than under it.
    for (int pass = 0; pass < 2; ++pass) {
        for (const TileInstance& t : map.Tiles()) {
            if (t.layer != LAYER_GROUND || t.overlay != (pass == 1)) continue;
            const string& path = map.TexturePath(t);
            if (path.empty()) continue;

            const SDL_Color c = cache.AverageColor(path);
            const SDL_Rect cell = {
                static_cast<int>(t.rect.x / scale), static_cast<int>(t.rect.y / scale),
                std::max(1, static_cast<int>(t.rect.w / scale)),
                std::max(1, static_cast<int>(t.rect.h / scale))};
            SDL_FillSurfaceRect(s, &cell, SDL_MapSurfaceRGBA(s, c.r, c.g, c.b, 255));
        }
    }

    terrain = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!terrain) return false;

    SDL_SetTextureScaleMode(terrain, SDL_SCALEMODE_NEAREST);
    built_for = world.MapId();
    return true;
}

void Minimap::Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
                   float cx, float cy, float radius) {
    if (built_for != world.MapId() || !terrain) {
        if (!Build(r, cache, world)) return;
    }

    const Player& p = world.player;
    const float px = p.x / scale;          // the player, in image pixels
    const float py = p.y / scale;
    const int   rad = static_cast<int>(radius);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // The dark under the glass first, so the edge of the world reads as empty
    // rather than as a hole cut in the HUD.
    SDL_SetRenderDrawColor(r, VOID_COLOR.r, VOID_COLOR.g, VOID_COLOR.b, VOID_COLOR.a);
    for (int dy = -rad; dy <= rad; ++dy) {
        const int half = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dy * dy)));
        const SDL_FRect row = {cx - half, cy + dy, static_cast<float>(half * 2), 1.0f};
        SDL_RenderFillRect(r, &row);
    }

    // Then the terrain, a row at a time: a circle is a stack of strips.
    for (int dy = -rad; dy <= rad; ++dy) {
        const int half = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dy * dy)));
        if (half <= 0) continue;

        float sx = px - half, sw = static_cast<float>(half * 2);
        const float sy = floorf(py) + dy;
        float dx = cx - half;
        if (sy < 0.0f || sy >= img_h) continue;             // above or below the map

        sw = MinimapClipSpan(sx, dx, sw, img_w);            // past either side
        if (sw <= 0.0f) continue;

        const SDL_FRect src = {sx, sy, sw, 1.0f};
        const SDL_FRect dst = {roundf(dx), cy + dy, sw, 1.0f};
        SDL_RenderTexture(r, terrain, &src, &dst);
    }

    // Dots for anything worth steering towards, clipped to the glass.
    const float inner = radius - 3.0f;
    auto blip = [&](float wx, float wy, SDL_Color c, float size) {
        const float ox = (wx - p.x) / scale;
        const float oy = (wy - p.y) / scale;
        if (ox * ox + oy * oy > inner * inner) return;

        const SDL_FRect box = {roundf(cx + ox - size / 2.0f), roundf(cy + oy - size / 2.0f),
                               size, size};
        SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
        const SDL_FRect seat = {box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f};
        SDL_RenderFillRect(r, &seat);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(r, &box);
    };

    for (const Portal& portal : world.CurrentMap().Portals())
        blip(portal.rect.x + portal.rect.w / 2.0f, portal.rect.y + portal.rect.h / 2.0f,
             {104, 206, 116, 255}, 3.0f);
    for (const auto& n : world.npcs) if (!n->Away()) blip(n->x, n->y, {104, 176, 240, 255}, 3.0f);
    for (const auto& e : world.enemies) {
        if (e->CurrentState() == Enemy::State::Dead) continue;
        blip(e->x, e->y, {214, 72, 60, 255}, 3.0f);
    }

    // The player last, so nothing is drawn over them, with a nose showing which
    // way they are facing.
    blip(p.x, p.y, Palette::Highlight, 4.0f);
    float nx = 0.0f, ny = 0.0f;
    switch (p.facing) {
        case FACE_UP:    ny = -1.0f; break;
        case FACE_DOWN:  ny =  1.0f; break;
        case FACE_LEFT:  nx = -1.0f; break;
        case FACE_RIGHT: nx =  1.0f; break;
    }
    SDL_SetRenderDrawColor(r, Palette::Highlight.r, Palette::Highlight.g,
                           Palette::Highlight.b, 255);
    const SDL_FRect nose = {roundf(cx + nx * 5.0f - 1.0f), roundf(cy + ny * 5.0f - 1.0f),
                            2.0f, 2.0f};
    SDL_RenderFillRect(r, &nose);

    // The bezel over the top: its middle is transparent, so it trims the edge
    // of the glass and hides the stair-stepping of the strips.
    if (SDL_Texture* ring = cache.Get("assets/ui/minimap_ring.png")) {
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(ring, &tw, &th);
        const SDL_FRect dst = {roundf(cx - tw / 2.0f), roundf(cy - th / 2.0f), tw, th};
        SDL_RenderTexture(r, ring, nullptr, &dst);
    }

    // Where you are, under the dial.
    ui.TextShadowed(world.CurrentMap().DisplayName(), cx, cy + radius + 14.0f,
                    TextSize::Small, Palette::TextDim, Align::Center);
}
