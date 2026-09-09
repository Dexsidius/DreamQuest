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

void TextureCache::Clear() {
    for (auto& kv : textures)
        if (kv.second) SDL_DestroyTexture(kv.second);
    textures.clear();
    warned.clear();
}
