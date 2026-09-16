#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "ui.h"

class World;
class ShopDatabase;

// -----------------------------------------------------------------------------
//  The world map: the whole Hollowmarch on one screen, opened from anywhere.
//
//  The terrain is baked once, the way the minimap bakes its dial, but from
//  maps/overworld.mx rather than from whatever map is loaded -- the point of it
//  is to be readable while you are three rooms deep in a dungeon and have lost
//  track of which way the road runs. The Map it bakes from is thrown away
//  afterwards; only the picture is kept.
//
//  What is marked on it comes from data/worldmap.json, written by genmaps as it
//  places things: it knows which portal is a dungeon mouth and which is the
//  road to another zone, which a runtime scan of portals could only guess at.
//  A town's shops are not in that file -- they are read from the shop database,
//  so a trader added to a town appears on the map without anything being
//  written down twice.
// -----------------------------------------------------------------------------

struct WorldMark {
    string kind;       // dungeon | path | town | camp | grave | landmark
    string label;
    float  x = 0, y = 0;
    string town;
    // Shop types in this town, filled in from the shop database.
    vector<string> shops;
};

class WorldMapPanel {
public:
    ~WorldMapPanel();

    // Reads the marks. Cheap, and safe to call before the renderer exists.
    bool Load(const string& path, const ShopDatabase& shops);

    // Draws the whole screen. Bakes the terrain the first time it is asked to.
    void Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
              const string& close_prompt);

    void Forget();   // drops the baked picture; the next draw rebuilds it

    // What a kind of mark is called in the legend, and the letter it is drawn
    // with. Public so the self-test can hold the legend to the same table.
    static const char* KindName(const string& kind);
    static const char* KindGlyph(const string& kind);
    static SDL_Color   KindColour(const string& kind);
    // The same, for the little shop icons inside a town's marker.
    static const char* ShopGlyph(const string& type);
    static const char* ShopName(const string& type);

    const vector<WorldMark>& Marks() const { return marks; }

private:
    bool Bake(SDL_Renderer* r, TextureCache& cache);

    vector<WorldMark> marks;
    SDL_Texture* terrain = nullptr;
    int   img_w = 0, img_h = 0;
    float world_w = 0.0f, world_h = 0.0f;
    bool  baked = false, bake_failed = false;
};
