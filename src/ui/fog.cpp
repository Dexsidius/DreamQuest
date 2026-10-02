#include "fog.h"

SDL_Color FogTexture::Colour(int cx, int cy) {
    // A cloud with some body to it -- a smooth wash over squares four apart --
    // and a grain on top, from a hash of the square.
    const auto hash = [](int x, int y) {
        uint32_t v = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return static_cast<float>((v ^ (v >> 16)) & 0xFFFFu) / 65535.0f;
    };
    const int gx = cx / 4, gy = cy / 4;
    float tx = static_cast<float>(cx % 4) / 4.0f, ty = static_cast<float>(cy % 4) / 4.0f;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float top = hash(gx, gy) * (1.0f - tx) + hash(gx + 1, gy) * tx;
    const float bottom = hash(gx, gy + 1) * (1.0f - tx) + hash(gx + 1, gy + 1) * tx;
    const float n = 0.75f * (top * (1.0f - ty) + bottom * ty) + 0.25f * hash(cx + 101, cy - 37);
    static constexpr SDL_Color DARK{16, 18, 24, 255}, LIGHT{46, 49, 60, 255};
    const auto mix = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(a + (b - a) * n + 0.5f); };
    return {mix(DARK.r, LIGHT.r), mix(DARK.g, LIGHT.g), mix(DARK.b, LIGHT.b), 255};
}

void FogTexture::Forget() {
    if (tex) SDL_DestroyTexture(tex);
    tex = nullptr;
    map.clear();
    w = h = 0;
    stamp = 0;
}

bool FogTexture::Update(SDL_Renderer* r, const Exploration& seen, const string& map_id, float world_w, float world_h) {
    const Exploration::Grid* g = seen.GridFor(map_id);
    // Before the first step on a map there is no grid yet: it is all fog.
    const int gw = g ? g->w : std::max(1, static_cast<int>(ceilf(world_w / Exploration::CELL)));
    const int gh = g ? g->h : std::max(1, static_cast<int>(ceilf(world_h / Exploration::CELL)));
    const uint32_t now = g ? g->stamp : 0;
    if (tex && map == map_id && w == gw && h == gh && stamp == now) return true;

    if (!tex || w != gw || h != gh) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, gw, gh);
        if (!tex) { w = h = 0; return false; }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        w = gw;
        h = gh;
        pixels.resize(static_cast<size_t>(w) * h * 4);
        for (int cy = 0; cy < h; ++cy)
            for (int cx = 0; cx < w; ++cx) {
                const SDL_Color c = Colour(cx, cy);
                uint8_t* p = &pixels[(static_cast<size_t>(cy) * w + cx) * 4];
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
            }
    }
    for (int cy = 0; cy < h; ++cy)
        for (int cx = 0; cx < w; ++cx) {
            const int clear = g ? g->At(cx, cy) : 0;
            pixels[(static_cast<size_t>(cy) * w + cx) * 4 + 3] = static_cast<uint8_t>(THICK * (255 - clear) / 255);
        }
    if (!SDL_UpdateTexture(tex, nullptr, pixels.data(), w * 4)) return false;
    map = map_id;
    stamp = now;
    return true;
}
