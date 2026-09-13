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

    // The part of the image that is actually drawn, as fractions of its full
    // size ({0,0,1,1} when unknown). Scenery art sits on a generous transparent
    // canvas, so anything that asks "is this in front of that" wants this
    // rather than the canvas. Worked out once per image from its pixels.
    SDL_FRect OpaqueBounds(const string& path);

    // The average colour of everything drawn in the image, ignoring what is
    // transparent. The minimap paints a whole map out of these, so it is worked
    // out once per image and kept.
    SDL_Color AverageColor(const string& path);

    void Clear();

private:
    SDL_Renderer* renderer;
    unordered_map<string, SDL_Texture*> textures;
    unordered_map<string, SDL_FRect> opaque;
    unordered_map<string, SDL_Color> average;
    unordered_map<string, bool> warned;
};
