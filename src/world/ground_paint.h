#pragma once
#include "../headers.h"

class Map;
class Camera;

// ---------------------------------------------------------------------------
//  Painted ground -- the Cozy look's floor
//
//  A map's floor drawn as a picture instead of tile by tile: the same cells,
//  but grass that runs over the edge of a path in tufts, with a dark line
//  along it and the path in its shade; earth whose edges wander, laid stone
//  whose edges keep nearly straight, boards and walls that keep to theirs;
//  cobbles and flagstones with a lit lip and a shaded one; a lip and a ring
//  of foam round water; tufts, clumps and flowers in the open grass.
//
//  Every floor there is has a role (Ground): cover that grows over the edges
//  of what it meets (grass, moss, snow), earth, laid stone, flat things that
//  keep their edges (boards, walls), and fluids. The commonest grounds have
//  painters of their own -- the meadow's grass, the Bayou's lusher swamp
//  grass, the forest's, moss, dirt, the roads, the square, sand, boards --
//  and every other ground is painted from its own tile's art, softened, with
//  the same edges.
//
//  Nothing of it is stored: it is painted from the map's own floor tiles, a
//  piece of 256 px at a time as the view comes to it (the pieces it needs at
//  once, at once; the ones round it, in the background), and the same tiles
//  always paint the same picture. Raised ground is lifted as its tiles are,
//  so the banks drawn after it meet it.
//
//  Water and lava are still drawn as their tiles, underneath: the picture is
//  clear where they show, so their shaders still run them. So is anything
//  laid over the floor that is not a ground (a rug, a flight of stairs).
//
//  Out of doors only, and only in the Cozy look (the Art Style on the Visual
//  Effects page): see Wants.
// ---------------------------------------------------------------------------

namespace GroundPaint {

// What a ground does where it meets another.
enum Role : Uint8 {
    NONE = 0,   // nothing to paint (no tile at all)
    COVER,      // grows over the edge of what it meets: grass, moss, snow
    EARTH,      // its edge wanders: dirt, sand, ash, mud
    STONE,      // laid: its edge keeps nearly straight
    FLAT,       // boards, carpet, walls: keeps exactly to its edge
    FLUID       // water, lava: drawn as tiles under a clear picture
};

struct Ground {
    Role role = NONE;
    Uint8 painter = 0;     // which of the painters (ground_paint.cpp), or its own art
    float tone = 0.0f;     // the meadow's: lighter (+1) or olive (-1)
    bool known = false;    // named in the table, not worked out from the name
};
// What a floor tile is, by its art's name: grass, grass_light_2 and
// grass_olive are the meadow's grass; water and lava are fluids whatever
// their shade; anything not in the table is earth, painted from its own art
// (or a wall, flat, if it is called one).
const Ground& GroundOf(const string& tile_path);

// The maps that are painted: out of doors (the waking world and its dreams),
// not a house, a cellar or a dungeon.
bool Paints(const Map& map);
// This map is drawn painted just now: one that is, in the Cozy look.
bool Wants(const Map& map);
// A floor tile the picture paints, so not to be drawn on its own: any ground
// but a fluid; and of what lies on the floor, only a ground the table names.
bool Covers(const string& tile_path, bool overlay);

// Pictures worked out on the CPU: RGBA, w * h * 4, alpha 0 where tiles show
// through. A region is in the view's terms -- raised ground already lifted.
// For the renderer, and for the self-test.
struct Picture {
    int x = 0, y = 0, w = 0, h = 0;
    vector<Uint8> rgba;
    float seconds = 0.0f;   // how long the painting took
};
Picture PaintRegion(const Map& map, const SDL_Rect& region);
Picture Paint(const Map& map);   // the whole of it

// Draw the map's picture over what of the view the camera sees: after the
// floor tiles it does not paint, before anything lying on the floor.
void Draw(SDL_Renderer* renderer, const Map& map, const Camera& camera);

// Let go of every picture (before the renderer goes).
void Release();

}  // namespace GroundPaint
