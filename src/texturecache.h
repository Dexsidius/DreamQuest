#pragma once
#include "headers.h"

// Path -> SDL_Texture, so the thousands of tile instances in a map share one
// texture per distinct image. Owns everything it hands out.
class TextureCache {
public:
    explicit TextureCache(SDL_Renderer* r) : renderer(r) {}
    ~TextureCache() { Clear(); }

    // Returns nullptr (and logs once) if the file is missing.
    SDL_Texture* Get(const string& path);

    // Width/height of a cached texture; {0,0} when it failed to load.
    SDL_Point Size(const string& path);

    void Clear();

private:
    SDL_Renderer* renderer;
    unordered_map<string, SDL_Texture*> textures;
    unordered_map<string, bool> warned;
};
