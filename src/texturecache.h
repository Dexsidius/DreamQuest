#pragma once
#include "headers.h"

class DyeTable;
struct Looks;

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
    // The same for one cell of a sheet -- a frame of an animation -- in pixels
    // from the cell's own corner: the whole cell when nothing in it is drawn,
    // or the file cannot be read.
    SDL_FRect OpaqueBoundsIn(const string& path, const SDL_Rect& cell);

    // The average colour of everything drawn in the image, ignoring what is
    // transparent. The minimap paints a whole map out of these, so it is worked
    // out once per image and kept.
    SDL_Color AverageColor(const string& path);

    // The same image in a character's own colours -- their hair, skin and
    // clothes (see entity/looks.h) -- made from the file the first time it is
    // asked for and kept, a few dozen at most: the least lately drawn are let
    // go once they pass a budget, and made again if they are wanted. The plain
    // image when the looks change nothing.
    SDL_Texture* GetDyed(const string& path, const DyeTable& dyes, const Looks& looks);
    // Lets every recoloured image go: the screen that chooses the colours
    // makes a new set at every step.
    void ForgetDyed();

    void Clear();

private:
    struct Dyed {
        SDL_Texture* tex = nullptr;
        size_t bytes = 0;
        uint64_t used = 0;
    };
    unordered_map<string, Dyed> dyed;
    size_t dyed_bytes = 0;
    uint64_t dye_clock = 0;

    SDL_Renderer* renderer;
    unordered_map<string, SDL_Texture*> textures;
    unordered_map<string, SDL_FRect> opaque;
    unordered_map<string, SDL_FRect> opaque_cells;
    unordered_map<string, SDL_Color> average;
    unordered_map<string, bool> warned;
};
