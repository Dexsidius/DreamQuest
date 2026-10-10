#include "texturecache.h"
#include "entity/looks.h"

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

SDL_FRect TextureCache::OpaqueBoundsIn(const string& path, const SDL_Rect& cell) {
    const string key = path + "#" + std::to_string(cell.x) + "," + std::to_string(cell.y) + "," +
                       std::to_string(cell.w) + "," + std::to_string(cell.h);
    auto it = opaque_cells.find(key);
    if (it != opaque_cells.end()) return it->second;

    SDL_FRect out{0.0f, 0.0f, static_cast<float>(cell.w), static_cast<float>(cell.h)};
    if (SDL_Surface* loaded = IMG_Load(path.c_str())) {
        if (SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32)) {
            if (SDL_LockSurface(s)) {
                const int x0 = std::clamp(cell.x, 0, s->w), y0 = std::clamp(cell.y, 0, s->h);
                const int x1 = std::clamp(cell.x + cell.w, 0, s->w), y1 = std::clamp(cell.y + cell.h, 0, s->h);
                int minx = x1, miny = y1, maxx = -1, maxy = -1;
                const Uint8* px = static_cast<const Uint8*>(s->pixels);
                for (int y = y0; y < y1; ++y) {
                    const Uint8* row = px + y * s->pitch;
                    for (int x = x0; x < x1; ++x) {
                        // As OpaqueBounds: a faint fringe or a shadow is not the figure.
                        if (row[x * 4 + 3] <= 24) continue;
                        minx = std::min(minx, x); maxx = std::max(maxx, x);
                        miny = std::min(miny, y); maxy = std::max(maxy, y);
                    }
                }
                SDL_UnlockSurface(s);
                if (maxx >= minx && maxy >= miny)
                    out = {static_cast<float>(minx - cell.x), static_cast<float>(miny - cell.y),
                           static_cast<float>(maxx - minx + 1), static_cast<float>(maxy - miny + 1)};
            }
            SDL_DestroySurface(s);
        }
        SDL_DestroySurface(loaded);
    }
    opaque_cells[key] = out;
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

// About forty of the player's sheets, at the half megabyte a layer sheet is:
// four players' worth of what is on screen at once, and more besides.
static constexpr size_t kDyedBudget = 48u * 1024u * 1024u;

SDL_Texture* TextureCache::GetDyed(const string& path, const DyeTable& dyes, const Looks& looks) {
    if (!looks.Any() || !dyes.Loaded()) return Get(path);
    const string key = path + "|" + looks.Key();
    auto it = dyed.find(key);
    if (it != dyed.end()) {
        it->second.used = ++dye_clock;
        return it->second.tex;
    }

    SDL_Texture* tex = nullptr;
    size_t bytes = 0;
    if (SDL_Surface* loaded = IMG_Load(path.c_str())) {
        if (SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32)) {
            dyes.Apply(s, looks);
            tex = SDL_CreateTextureFromSurface(renderer, s);
            bytes = static_cast<size_t>(s->w) * s->h * 4;
            SDL_DestroySurface(s);
        }
        SDL_DestroySurface(loaded);
    }
    // The art as drawn rather than nothing.
    if (!tex) return Get(path);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    dyed[key] = {tex, bytes, ++dye_clock};
    dyed_bytes += bytes;

    // Over the budget, the least lately drawn go -- never the one just made.
    while (dyed_bytes > kDyedBudget && dyed.size() > 1) {
        auto oldest = dyed.end();
        for (auto d = dyed.begin(); d != dyed.end(); ++d)
            if (d->first != key && (oldest == dyed.end() || d->second.used < oldest->second.used)) oldest = d;
        if (oldest == dyed.end()) break;
        SDL_DestroyTexture(oldest->second.tex);
        dyed_bytes -= std::min(dyed_bytes, oldest->second.bytes);
        dyed.erase(oldest);
    }
    return tex;
}

void TextureCache::ForgetDyed() {
    for (auto& kv : dyed)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    dyed.clear();
    dyed_bytes = 0;
}

void TextureCache::Clear() {
    for (auto& kv : textures)
        if (kv.second) SDL_DestroyTexture(kv.second);
    textures.clear();
    ForgetDyed();
    warned.clear();
    opaque.clear();
    opaque_cells.clear();
    average.clear();
}
