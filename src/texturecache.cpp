#include "texturecache.h"

SDL_Texture* TextureCache::Get(const string& path) {
    auto it = textures.find(path);
    if (it != textures.end()) return it->second;

    SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
    if (!tex) {
        if (!warned[path]) {
            SDL_Log("TextureCache: could not load '%s': %s", path.c_str(), SDL_GetError());
            warned[path] = true;
        }
    } else {
        // Pixel art: keep edges crisp at any zoom.
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    }
    textures[path] = tex;
    return tex;
}

SDL_Point TextureCache::Size(const string& path) {
    SDL_Texture* tex = Get(path);
    if (!tex) return {0, 0};
    float w = 0, h = 0;
    SDL_GetTextureSize(tex, &w, &h);
    return {static_cast<int>(w), static_cast<int>(h)};
}

SDL_FRect TextureCache::OpaqueBounds(const string& path) {
    auto it = opaque.find(path);
    if (it != opaque.end()) return it->second;

    SDL_FRect out{0.0f, 0.0f, 1.0f, 1.0f};
    if (SDL_Surface* loaded = IMG_Load(path.c_str())) {
        if (SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32)) {
            if (SDL_LockSurface(s)) {
                int minx = s->w, miny = s->h, maxx = -1, maxy = -1;
                const Uint8* px = static_cast<const Uint8*>(s->pixels);
                for (int y = 0; y < s->h; ++y) {
                    const Uint8* row = px + y * s->pitch;
                    for (int x = 0; x < s->w; ++x) {
                        // Faint fringe and soft shadow do not count as art.
                        if (row[x * 4 + 3] <= 24) continue;
                        minx = std::min(minx, x); maxx = std::max(maxx, x);
                        miny = std::min(miny, y); maxy = std::max(maxy, y);
                    }
                }
                SDL_UnlockSurface(s);
                if (maxx >= minx && maxy >= miny && s->w > 0 && s->h > 0)
                    out = {static_cast<float>(minx) / s->w, static_cast<float>(miny) / s->h,
                           static_cast<float>(maxx - minx + 1) / s->w,
                           static_cast<float>(maxy - miny + 1) / s->h};
            }
            SDL_DestroySurface(s);
        }
        SDL_DestroySurface(loaded);
    }
    opaque[path] = out;
    return out;
}

SDL_Color TextureCache::AverageColor(const string& path) {
    auto it = average.find(path);
    if (it != average.end()) return it->second;

    SDL_Color out{120, 116, 110, 255};
    if (SDL_Surface* loaded = IMG_Load(path.c_str())) {
        if (SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32)) {
            if (SDL_LockSurface(s)) {
                long long r = 0, g = 0, b = 0, n = 0;
                const Uint8* px = static_cast<const Uint8*>(s->pixels);
                for (int y = 0; y < s->h; ++y) {
                    const Uint8* row = px + y * s->pitch;
                    for (int x = 0; x < s->w; ++x) {
                        if (row[x * 4 + 3] <= 128) continue;   // transparent: not art
                        r += row[x * 4 + 0];
                        g += row[x * 4 + 1];
                        b += row[x * 4 + 2];
                        ++n;
                    }
                }
                SDL_UnlockSurface(s);
                if (n > 0)
                    out = {static_cast<Uint8>(r / n), static_cast<Uint8>(g / n),
                           static_cast<Uint8>(b / n), 255};
            }
            SDL_DestroySurface(s);
        }
        SDL_DestroySurface(loaded);
    }
    average[path] = out;
    return out;
}

void TextureCache::Clear() {
    for (auto& kv : textures)
        if (kv.second) SDL_DestroyTexture(kv.second);
    textures.clear();
    warned.clear();
    opaque.clear();
    average.clear();
}
