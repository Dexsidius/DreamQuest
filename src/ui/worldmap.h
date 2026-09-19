#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "ui.h"

class World;
class Map;
class ShopDatabase;

// -----------------------------------------------------------------------------
//  The map screen: where you are, and the Hollowmarch.
//
//  It opens on **the map of wherever the player is** -- Fernhollow in
//  Fernhollow, the Ashen Path on the Ashen Path, the lower workings in the
//  lower workings -- and a press turns the page to the whole Hollowmarch and
//  back. It used to be the Hollowmarch and nothing else, with a line of text
//  to say "you are in Mossvale", which is not much of a map of Mossvale.
//
//  A page is baked once, the way the minimap bakes its dial, from the map's own
//  file rather than from whatever is loaded -- the Map it bakes from is thrown
//  away afterwards and only the picture kept -- so any page can be drawn from
//  anywhere. Ground first; then whatever stands on it, as a smudge of its own
//  colour where its foot is, which is what makes a wood a wood and a village a
//  village at eight pixels to the tile.
//
//  What is marked on the Hollowmarch comes from data/worldmap.json, written by
//  genmaps as it places things: it knows which portal is a dungeon mouth and
//  which is the road to another land. What is marked on every other page is
//  read off that map itself: its ways out, told apart by what genmaps says the
//  far side is; its traders, by trade; and the few things worth walking to --
//  a bench, a board, a camp. A town's shops are never in a file -- they are read
//  from the shop database, so a trader added to a town is on the map without
//  anything being written down twice.
//
//  A room is not given a page: inside the Barley and Bell the map is
//  Havenbrook, with the dot on the inn's door.
// -----------------------------------------------------------------------------

struct WorldMark {
    string kind;       // dungeon | path | town | camp | grave | landmark | door | trader | craft
    string label;
    float  x = 0, y = 0;
    string town;
    // Shop types here, filled in from the shop database.
    vector<string> shops;
};

// What genmaps says a map is: see AreaEntry there.
struct AreaInfo {
    string name;
    string kind;               // land | dungeon | interior
    vector<string> exits;      // map ids its ways out lead to
};

class WorldMapPanel {
public:
    static constexpr const char* OVERWORLD = "overworld";

    ~WorldMapPanel();

    // Reads the marks and the list of areas. Cheap, and safe to call before
    // the renderer exists.
    bool Load(const string& path, const ShopDatabase& shops);

    // Draws the whole screen: the page for where the player is, or the
    // Hollowmarch if `overview`. Bakes a page the first time it is asked for.
    void Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
              const string& close_prompt, const string& turn_prompt, bool overview);

    void Forget();   // drops the baked pictures; the next draw rebuilds them

    // What a kind of mark is called in the legend, and the letter it is drawn
    // with. Public so the self-test can hold the legend to the same table.
    static const char* KindName(const string& kind);
    static const char* KindGlyph(const string& kind);
    static SDL_Color   KindColour(const string& kind);
    // The same, for the little shop icons inside a town's marker.
    static const char* ShopGlyph(const string& type);
    static const char* ShopName(const string& type);

    const vector<WorldMark>& Marks() const { return marks; }
    const std::map<string, AreaInfo>& Areas() const { return areas; }

    // Which map's page is shown for someone standing on `map_id`: itself, or
    // for a room the place the room is in. `door` is the map whose doorway on
    // that page they went in by -- the room itself, or for an upstairs the
    // building it is upstairs in -- and empty when they are out on the page.
    string PageFor(const string& map_id, string* door = nullptr) const;
    // Whether the Hollowmarch is a different page from that one.
    bool   HasOverview(const string& map_id) const { return PageFor(map_id) != OVERWORLD; }
    // The map, next to the Hollowmarch, that leads to `map_id` -- the first
    // step of the way there -- or empty if no road does: the Reverie.
    string WayFrom(const string& map_id) const;
    // What is worth marking on a map, read off the map itself.
    vector<WorldMark> MarksOf(const Map& map) const;

private:
    struct Page {
        SDL_Texture* terrain = nullptr;
        int   img_w = 0, img_h = 0;
        float world_w = 0.0f, world_h = 0.0f;
        string title;
        vector<WorldMark> marks;
        // Where each way out is, by where it leads: for putting the dot on a door.
        std::map<string, SDL_FPoint> doors;
        bool  failed = false;
    };
    Page& PageOf(const string& map_id, SDL_Renderer* r, TextureCache& cache);

    vector<WorldMark> marks;                 // the Hollowmarch's, from the file
    std::map<string, AreaInfo> areas;
    std::map<string, Page> pages;
    const ShopDatabase* shop_db = nullptr;
};
