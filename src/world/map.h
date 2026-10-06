#pragma once
#include "../headers.h"
#include "../texturecache.h"
#include "../camera.h"
#include "../systems/shaders.h"

// -----------------------------------------------------------------------------
//  Map: loads the LevelEdit-Plus ".mx" export format.
//
//  The editor writes a flat dictionary of tile-name -> image + placements:
//
//      { "name": "...",
//        "tiles": { "Grass": { "filepath": "assets/grass.png",
//                              "locations": [[cx, cy, w, h], ...] } } }
//
//  Placements are centre-anchored, exactly as GameTile::Render does in the
//  editor, so a map drawn here lines up pixel-for-pixel with the editor view.
//
//  Everything a game needs on top of that -- draw layers, collision, portals,
//  spawn points, enemies, NPCs -- lives under a separate "dreamquest" key.
//  LevelEdit-Plus ignores keys it does not know, so these maps stay openable
//  and editable in the editor while carrying the extra gameplay data.
// -----------------------------------------------------------------------------

enum TileLayer { LAYER_GROUND = 0, LAYER_DECOR = 1, LAYER_OVERHEAD = 2 };

// -----------------------------------------------------------------------------
//  Elevation
//
//  A coarse height grid laid over the map, one level per cell, stored under the
//  "dreamquest" key as:
//
//      "elevation": { "cell": 32, "cols": 128, "rows": 96,
//                     "levels": [ ... cols*rows small ints, row major ... ],
//                     "ramps":  [ [x, y, w, h], ... ] }
//
//  Level 0 is the ground everything used to sit on, so a map with no elevation
//  block behaves exactly as it did before.
//
//  Each level lifts what stands on it by ELEVATION_RISE pixels on screen. It is
//  a lift, not a projection: this is a top-down game, and a cell at level two is
//  drawn where a cell at level two would be if you were looking down at a
//  terrace from a shallow angle. What sells it is the face -- the wall of earth
//  exposed on the downhill side -- which is drawn separately.
//
//  Movement between cells of different levels is blocked, which is what makes a
//  cliff a cliff. Ramps are rectangles where that rule is suspended.
// -----------------------------------------------------------------------------
constexpr float ELEVATION_RISE = 14.0f;   // screen pixels per level
constexpr int   ELEVATION_MAX  = 6;

struct TileInstance {
    SDL_FRect rect;          // world-space, top-left anchored (already un-centred)
    int   tex = -1;          // index into Map::textures
    int   layer = LAYER_GROUND;
    float sort_y = 0.0f;     // baseline used when interleaving with entities
    // Lies on the floor over the other ground tiles -- a rug, a flight of
    // stairs. Named with a leading '~' in the .mx file.
    bool  overlay = false;
    // Degrees it leans, its foot where it stands and its top pushed over to
    // the right (or left, below zero): a house in a dream of the town. A fifth
    // number after a placement's size in the .mx file.
    float lean = 0.0f;
};

// A condition on the world's flags: every one of `all` set, and none of
// `none`. Empty, it always holds. It is how a story says when somebody is
// where, asleep or not there at all, when a door stands open and when a way
// is shut -- written in the map, read against the save, so a game loaded
// halfway through the prologue finds the town as it was left.
//
//     "when": {"flags": ["HAVENBROOK_ASLEEP"], "not": ["ACT1_TOWN_WOKEN"]}
//     "when": "PRO_05_MAYOR_TALK"            one flag, set
//     "when": "!PRO_15_FOYER_CLEARED"        one flag, not set
//
// And, because a story's flags are only ever set and never cleared (a friend's
// machine hears a flag set, and never one cleared), two ways to say "or":
//
//     "any": ["A", "B"]                      at least one of them set
//     "unless": {"flags": ["HAVENBROOK_ASLEEP"], "not": ["ACT1_BESS_AWAKE"]}
//                                            and this other condition does not
//                                            hold (a list of them: none holds)
struct FlagCond {
    vector<string> all, none, any;
    vector<FlagCond> unless;
    bool Empty() const { return all.empty() && none.empty() && any.empty() && unless.empty(); }
    template <class Has> bool Holds(const Has& has) const {
        for (const string& f : all)  if (!has(f)) return false;
        for (const string& f : none) if (has(f))  return false;
        if (!any.empty()) {
            bool one = false;
            for (const string& f : any) if (has(f)) { one = true; break; }
            if (!one) return false;
        }
        for (const FlagCond& u : unless) if (u.Holds(has)) return false;
        return true;
    }
    // Every flag it names, however deep: for checking they are all real.
    void Names(vector<string>& out) const {
        out.insert(out.end(), all.begin(), all.end());
        out.insert(out.end(), none.begin(), none.end());
        out.insert(out.end(), any.begin(), any.end());
        for (const FlagCond& u : unless) u.Names(out);
    }
    static FlagCond FromJson(const json& j);
};

struct Portal {
    SDL_FRect rect{};
    string target_map;       // map id, e.g. "house_elder"
    string target_spawn;     // spawn point name inside that map
    string label;            // shown on the interact prompt
    bool   requires_interact = true;   // false = step-through
    string locked_by;        // item id needed to pass, empty when open
    int    danger_level = 0; // Combat level advised beyond it; 0 when safe
    // Combat level needed to go through at all; 0 when anyone may. The Ice
    // Spire and the way to the pit are closed to a character who would only
    // die there.
    int    min_combat = 0;
    // Shut while a story says so, and what it says when tried: "No one
    // answers." at a house nobody will open, "The gate is chained shut."
    // The first rule that holds is the one; none, and the way is open.
    // `ask`, when there is one, is a flag: the door asks the host (text the
    // question) whether they are ready, and a yes sets it -- which opens it.
    // A friend is not asked, and goes through: the question is the story's.
    struct ShutRule { FlagCond when; string text; string ask; };
    vector<ShutRule> shut;
    template <class Has> const ShutRule* ShutRuleBy(const Has& has) const {
        for (const ShutRule& r : shut)
            if (r.when.Holds(has)) return &r;
        return nullptr;
    }
    template <class Has> const string* ShutBy(const Has& has) const {
        const ShutRule* r = ShutRuleBy(has);
        return r ? &r->text : nullptr;
    }

    // What it says of what lies beyond it to somebody at `combat_level`, to
    // go after its label: that it is shut to them, or dangerous, or nothing.
    // A door's prompt and the label at a walk-through exit both say it.
    string Warning(int combat_level) const;
};

// Ground that hurts to stand on: lava vents, burning ash. Damage per second
// while the player's feet are on it.
struct Hazard {
    SDL_FRect rect{};
    float dps = 4.0f;
    string kind = "fire";
};

// Ice that will not bear a runner: a frozen lake. Walked on it holds; sprinted
// on, it cracks, and a crack run too long gives way under them -- see
// World::UpdateThinIce. `weak` is a patch darker than the rest, that strains
// a little under a walker too: this much of the way to breaking a second.
struct ThinIce {
    SDL_FRect rect{};
    float weak = 0.0f;
};

struct EnemySpawnDef {
    string type;             // key into data/enemies.json
    float  x = 0, y = 0;
    int    level = 1;
    float  respawn = 25.0f;  // seconds; <= 0 means it stays dead
    float  leash = 220.0f;   // how far it will chase from its post
    // A post that is not always kept by the same thing. With a `pool`, `type`
    // is only the first of it: which of them stands here is settled when the
    // map is walked into, by the day -- see World::ResolveSpawn. Posts that
    // share a `group` settle it together, so a platform has a pack of one kind
    // on it and not one of each. `spread` is how many levels over `level` it
    // may come out at.
    vector<string> pool;
    string group;
    int    spread = 0;
    // A post kept only after dark, and not every night: something that does
    // not live here, abroad from nightfall to dawn. `chance` is the share of
    // nights it is kept at all -- a `group` comes or stays away together --
    // and once killed it is gone until the next night. See World::Abroad.
    bool   night = false;
    float  chance = 1.0f;
    // Kept under the water until somebody comes too close to the edge, and
    // then it comes up out of it: see Enemy::Hidden. Only means anything for a
    // post in water, and a monster that swims.
    bool   lurk = false;
    // A post that walks the map rather than keeping its ground: whatever holds
    // it goes round `route`, a loop of points laid out about the map, and comes
    // for anybody who strays too near -- see Enemy::Roam. Where on the loop the
    // day finds it, and whether it is out at all that day (`chance`, a share of
    // days as a night post's is of nights), are the day's to say: see
    // World::RoamDraw. Everybody's machine works both out the same way.
    vector<SDL_FPoint> route;
    // A post whose level is worked out rather than written: the least spawn
    // level at which whatever the day put here shows at least this, so that a
    // pool of bosses of every natural strength all come out close to it. 0
    // means the written level stands. See Enemy::LevelToShow.
    int    shown = 0;
    // A post that is nobody's until a ritual calls it: which pool of a map's
    // ritual posts it belongs to, and in which of the ritual's waves it comes.
    // It lies still until then, anywhere on the map -- the ritual puts it where
    // it is wanted -- and does not come back by itself. See World::Ritual.
    string ritual;
    int    wave = 0;
    // A post kept only while a story says so: the foyer's armour stands until
    // it has been beaten, and is not there again after.
    FlagCond when;
    // A post that stands still, unseeing and unmoving, until this flag is set
    // -- or until it is struck: a suit of armour on a pedestal. See
    // Enemy::Dormant. `perch` is how high the pedestal is: 0, on the floor.
    string dormant;
    float  perch = 10.0f;
    // One of a squad: when every post of the squad on the map is down, the
    // flag of that name is set -- the guards round a Nightmare Hold, the
    // knights in the forge. A post not yet come (`appear`) is not down.
    string squad;
    // Comes when its `when` comes to hold while the map is up -- out of
    // smoke, where it was put, `appear_after` seconds later -- rather than
    // only being there or not as the map is walked into: knights forming out
    // of the shadows one by one, a dream's next wave.
    bool   appear = false;
    float  appear_after = 0.0f;
    // A level fitted to the player's: their combat level and `fit` more (and
    // `spread` on top), however strong they have grown -- Act I's Hushed,
    // never walling a player who comes late or early. See World::FitLevel.
    bool   fitted = false;
    int    fit = 0;
};

// Somewhere a walking villager stops, and for how long.
struct NpcStop {
    float  x = 0, y = 0;
    float  pause = 0.0f;     // seconds stood here before going on
    Facing facing = FACE_DOWN;
};

// One of an NPC's states: what a story has made of them while `when` holds.
// The first state that holds is the one; none, and they are as the rest of the
// definition has them. Anything a state leaves unsaid stays as it was.
struct NpcState {
    FlagCond when;
    bool   hidden = false;        // not there at all
    bool   moved = false;         // stands at x, y instead
    float  x = 0, y = 0;
    bool   turned = false;        // and faces this way
    Facing facing = FACE_DOWN;
    string pose;                  // a clip held instead of idle: "slump", "lie"
    // And the one held while somebody talks to them, if not that: Vask stops
    // chopping at the Hushed and holds his guard to answer, rather than
    // turning round and swinging his cane at whoever spoke.
    string talk_pose;
    // Another body: the same person drawn from another sheet. Elder Vask is
    // on his feet in front of his chair in his own dream (scene 19) and back
    // in it, rocking, in the finale. Empty is their own.
    string sprite;
    // Asleep: spoken to, they say nothing, and this is what is seen instead.
    bool   asleep = false;
    string asleep_text;
    string name, dialogue;        // another name, another conversation
    float  alpha = 1.0f;          // a faint shape, half there
    bool   flicker = false;       // and coming and going
    float  sort_bias = 0.0f;      // drawn as though this much further down
    // What pressing to interact with them says, in place of "Talk to" or "Try
    // to wake": "Use Dreamcatcher". The story's own -- the host's, in company.
    string prompt;
    // A little bell hung over them: there is something of the story's to do
    // here (a sleeper whose dream can be caught). The host's alone.
    bool   mark = false;
    // A colour laid over them, and a puff of steam off them every `steam`
    // seconds: Elder Vask, red in the face, until he is ready to talk (63).
    bool   tinted = false;
    SDL_Color tint{255, 255, 255, 255};
    float  steam = 0.0f;
};

struct NpcDef {
    string id, name, sprite;
    float  x = 0, y = 0;
    string dialogue;         // root node in data/dialogue.json
    Facing facing = FACE_DOWN;
    bool   wanders = false;
    string shop;             // shop id, empty when the NPC does not trade
    // A round walked over and over: see Npc. The first stop is a doorway or a
    // gate, because that is where they come out in the morning and go in at
    // night. `hours` is when a round may begin; both zero is all day.
    vector<NpcStop> path;
    bool   ping_pong = false;   // there and back, rather than round and round
    float  speed = 30.0f;       // pixels a second
    float  phase = 0.0f;        // seconds into the round at midnight of day one
    float  from_hour = 0.0f, to_hour = 0.0f;
    SDL_Color tint{255, 255, 255, 255};
    // Practising. Every `cast_every` seconds, give or take, they turn to
    // (cast_x, cast_y) -- a training dummy -- and throw `cast_bolt` at it, which
    // is one of data/projectiles.json's and does nothing to anybody: see
    // Projectile::show. "casts": {"bolt": ..., "at": [x, y], "every": seconds}.
    string cast_bolt;
    float  cast_x = 0.0f, cast_y = 0.0f, cast_every = 0.0f;
    vector<NpcState> states;
};

struct MapObject {
    string id;               // unique across the save, e.g. "chest_mine_01"
    string type;             // chest | storage | search | note | board | tree | rock | sign
    float  x = 0, y = 0;
    SDL_FRect solid{};       // optional blocking box, w == 0 when not solid
    string loot_table;       // chests
    // A chest holding one named thing rather than a table roll: the only way
    // to put an item in the world that no loot table can ever produce.
    string loot_item;
    int    loot_qty = 1;
    // When set, the object is only in the world while this quest is being
    // done: before it is taken and after it is finished, it is not there to
    // be seen or opened.
    string needs_quest;
    // A boss's own chest is its to give: there only once that boss, posted on
    // this map, has fallen today -- and, once opened, there for good (open).
    string needs_slain;
    // Used once by each character rather than once by the world: a key, a
    // seal, a relic, a lever or a stone a quest asks for. Friends each open
    // their own, and what one has done is no step past for another. See
    // World::Used.
    bool   own = false;
    string text;             // notes and signs
    string starts_quest;     // notes that kick off a quest
    string sprite;           // optional image path drawn at the position
    string sprite_open;      // swapped in once the object has been used
    string skill;            // gathering nodes: Woodcutting / Mining
    int    skill_level = 1;
    string yield;            // item a gathering node produces
    int    yield_xp = 0;
    float  gather_time = 2.6f;
    // Herbs: game hours before a picked plant grows back. Trees and seams
    // use it too, for how long a felled tree or a worked-out seam is gone.
    float  regrow_hours = 6.0f;
    // Trees and seams: the chance, on each log or ore, that the node is spent
    // -- the tree comes down, the seam gives out -- and stays so for
    // `regrow_hours`. Zero is a node that never runs out.
    float  deplete = 0.0f;
    string title;
    string station;          // crafting objects: "workbench" or "anvil"
    // What using it costs, in coins: a bed at an inn. Nothing, for anything
    // that does not say -- your own bed, a camp, a fire by the road.
    int    fee = 0;
    // A storage chest: how many slots it holds. What is in it is the player's
    // and lives in the save, not here.
    int    capacity = 0;
    vector<string> quests;   // mission boards
    vector<string> fish;     // fishing spots: what can be caught there

    // --- what a story makes of it -------------------------------------------------
    // There only while this holds: the stall shuttered on the morning the
    // town will not wake, the papers on the Mayor's floor once he has.
    FlagCond when;
    // A door ("door"): open while `open_if` holds, shut -- solid, and saying
    // `text` when tried -- otherwise. Tried while shut, a door that `opens`
    // sets that flag and so opens: in the Reverie, the cell door whose bolt
    // has rusted away. An `echo` door rings as it goes, the sound of a change
    // carrying from one world into the other.
    FlagCond open_if;
    string opens;
    bool   echo = false;
    int    collider = -1;    // its solid box, among the map's colliders
    // A bed ("bed") that sends a sleeper somewhere of its own rather than into
    // the Reverie's first depth, and lets them lie down at any hour.
    string dream_map;
    bool   any_hour = false;
    // A light of its own, whatever the object is: the cold shaft from a cell's
    // grate, a pool of colour under a stained window, a brazier's blue flame.
    // Colour alpha 0 is none.
    SDL_Color light{0, 0, 0, 0};
    float  light_radius = 110.0f, light_height = 20.0f, light_strength = 0.9f;
    bool   light_flicker = false;
    // Drawn this far up from where it stands, still sorted by its foot: a cup
    // on a desk.
    float  lift = 0.0f;
    // Not to be used while this holds -- it says `closed_text` instead: the
    // smith's anvil while the smith is still asleep, Act I.
    FlagCond closed;
    string closed_text;
    // A tether ("tether"): the Anchor's thread from a sleeper -- the NPC
    // `tie`'s feet as their art draws them, `tie_dx` across (mirrored when
    // they face left) and `tie_dy` down: an ankle, a belt -- to the knot at
    // x, y - lift. Taut and humming until `slack` holds, then lying loose on
    // the floor; gone with the object. Halda's ankle to the great anvil in
    // her dream (scenes 33-34); the Tanner's belt to the post (42-43).
    string   tie;
    float    tie_dx = 0.0f, tie_dy = 0.0f;
    FlagCond slack;
};

// A skill the story has not given the player yet: while `when` holds, the
// work is refused and `text` is said (story.json "locks"). Act I's gathering,
// before Elder Vask wakes.
struct SkillLock {
    string   skill;          // "Woodcutting", as a gathering node names it
    FlagCond when;
    string   text;
};

class Map {
public:
    bool Load(const string& path);
    // Only the monsters' posts of the map file at `path`, without loading the
    // rest of it: what a map has out on any night can be worked out from them
    // and the day alone (World::ResolveSpawn), from anywhere. Empty if the
    // file cannot be read.
    static vector<EnemySpawnDef> ReadPosts(const string& path);
    void Unload();
    bool Loaded() const { return loaded; }

    const string& Id() const { return id; }
    const string& DisplayName() const { return display_name; }
    // The line under the zone name on the banner shown on arrival.
    const string& Subtitle() const { return subtitle; }
    float Width() const { return bounds_w; }
    float Height() const { return bounds_h; }
    bool  IsInterior() const { return interior; }
    // A place with no light of its own: what you can see is what you carry.
    bool  IsDark() const { return dark; }
    const string& Ambient() const { return ambient; }
    SDL_Color BackgroundColor() const { return background; }

    // --- a room dressed for what stands in its ring ------------------------------
    // A map's `themed` block (written by genmaps; docs/MAP_FORMAT.md) says
    // which of its tile groups are the floor and the walls, with a pale
    // "undyed" picture for each, and which are cloth, trim, or the plain things
    // that are there only while nothing stands in the ring. While the dress is
    // on, the floor and walls are drawn from their undyed pictures in the
    // dress's colours, the cloth and trim are drawn in theirs, and the plain
    // things are put away; while it is off the room is as it always was.
    // Which dress is the world's to say, every frame, for whoever the frame is
    // drawn for (World::HouseDress): it is that player's totem, not the room's.
    enum DressRole : Uint8 { DRESS_NONE = 0, DRESS_FLOOR, DRESS_WALL, DRESS_CLOTH, DRESS_TRIM, DRESS_PLAIN };
    struct Dress {
        bool on = false;
        SDL_Color floor{255, 255, 255, 255}, wall{255, 255, 255, 255}, cloth{255, 255, 255, 255},
                  trim{255, 255, 255, 255};
    };
    bool HasDress() const { return has_dress; }
    // Where the ring's light stands, in world pixels.
    SDL_FPoint DressLight() const { return dress_light; }
    void SetDress(const Dress& d) const { dress = d; }
    const Dress& CurrentDress() const { return dress; }
    DressRole DressRoleOf(int tex) const {
        return (tex >= 0 && tex < static_cast<int>(dress_role.size())) ? static_cast<DressRole>(dress_role[tex]) : DRESS_NONE;
    }
    // Whether a tile of this texture is put away under the present dress.
    bool DressHides(int tex) const;

    // Draw one layer, culled to what the camera can see. The ground is drawn
    // in two passes, the floor (1) and what lies on it (2) -- a rug, a bridge
    // -- and `passes` says which; both, unless something goes between them.
    void RenderLayer(SDL_Renderer* r, TextureCache& cache, const Camera& cam, int layer, int passes = 3) const;
    // Decor tiles that must interleave with entities by baseline.
    void CollectDecor(const Camera& cam, vector<const TileInstance*>& out) const;
    // Draws a single tile, so the world's sorted pass can interleave decor
    // with entities without needing to know about the texture list.
    void RenderTile(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
                    const TileInstance& t, Uint8 alpha = 255) const;
    // Every tile on the map, for anything that needs to walk the whole set
    // rather than draw what the camera can see -- the minimap bakes its image
    // from these.
    const vector<TileInstance>& Tiles() const { return tiles; }
    // The image a tile draws, or an empty string.
    const string& TexturePath(const TileInstance& t) const {
        static const string none;
        return (t.tex >= 0 && t.tex < static_cast<int>(textures.size())) ? textures[t.tex] : none;
    }
    // What a texture's tiles are made of, for the water and lava shaders
    // (Shaders::Surface): 0 plain ground, 1 water, 2 lava.
    Uint8 SurfaceOf(int tex) const {
        return (tex >= 0 && tex < static_cast<int>(surfaces.size())) ? surfaces[tex] : 0;
    }
    // How a texture's scenery moves and lights: see Shaders::ArtOf.
    const Shaders::Art& ArtOf(int tex) const {
        static const Shaders::Art none;
        return (tex >= 0 && tex < static_cast<int>(arts.size()) && arts[tex]) ? *arts[tex] : none;
    }
    // Ground fog, from the map's "fog" block; off where it has none.
    const Shaders::Fog& GroundFog() const { return fog; }
    // Where the floor is `surface` inside `rect`, as it is drawn (lifted with
    // its terrain): the lava, for the light map to leave lit.
    void SurfaceRects(const SDL_FRect& rect, Uint8 surface, vector<SDL_FRect>& out) const;
    // Where the ground is `surface` inside `rect`: one point, the middle of
    // what there is, for each `cell`-sized square that has any. Lifted with
    // the terrain, as it is drawn.
    void SurfaceSpots(const SDL_FRect& rect, Uint8 surface, float cell, vector<SDL_FPoint>& out) const;

    // --- collision -----------------------------------------------------------
    //
    // Water is collision like any other: the pond at Fernhollow is a wall to
    // everything that walks. What makes it its own kind is that a few things
    // swim, and for those the water is the one obstacle that is not there --
    // so `swims` asks the same question with the water left out. Every other
    // map's water is plain collision and nobody can swim in it, which is what
    // keeps a duck in its own pond.
    bool  Blocked(const SDL_FRect& box, bool swims = false) const;
    // True when this point is over water, whoever is standing there.
    bool  InWater(float x, float y) const;
    // True when the map has water a swimmer could get into at all.
    bool  HasWater() const { return water_count > 0; }
    // Axis-separated slide; returns the resolved position for the box.
    SDL_FPoint MoveWithCollision(const SDL_FRect& box, float dx, float dy,
                                 bool swims = false) const;

    // Where a moving box first meets a wall, and which way that wall faces.
    //
    // Walking wants to slide along an obstacle, which is what MoveWithCollision
    // does. Anything that hits a wall and reacts to it -- a bolt that stops, an
    // arrow that ricochets -- needs to know two more things: the last position
    // that was actually clear, so the impact is drawn on the surface rather
    // than inside it, and the normal of the face it struck, so a bounce leaves
    // in a plausible direction.
    struct Contact {
        bool  hit = false;
        float x = 0.0f, y = 0.0f;     // last clear position of the box's centre
        float nx = 0.0f, ny = 0.0f;   // unit normal, pointing back out of the wall
    };
    Contact SweepPoint(float x, float y, float dx, float dy, float radius) const;

    // --- elevation -----------------------------------------------------------
    bool  HasElevation() const { return elev_cols > 0 && elev_rows > 0; }
    float ElevationCell() const { return elev_cell; }
    // Terrain level under a world point. Off the map, the nearest edge cell's
    // level, so something walking out of bounds does not fall off a cliff that
    // is not there.
    int   LevelAt(float x, float y) const;
    // What that level is worth on screen, in pixels of lift.
    float HeightAt(float x, float y) const { return LevelAt(x, y) * ELEVATION_RISE; }
    // True when a ramp covers this point, so a level change here is walkable.
    bool  RampAt(float x, float y) const;
    // True when stepping from one point to the other crosses a level change
    // that no ramp covers -- which is what makes a cliff impassable.
    bool  LevelChangeBlocked(float from_x, float from_y,
                             float to_x, float to_y) const;

    // --- a ring nothing crosses ------------------------------------------------------
    // A ritual's ring of fire (see World::Ritual): while it burns, whatever
    // is inside it stays in and whatever is outside stays out. Like a change
    // of level it is a property of the movement rather than of the ground --
    // standing in the flames is nothing; crossing them is what is refused --
    // and every step taken through MoveWithCollision is held to it.
    void  SetRing(float x, float y, float radius) { ring_on = true; ring_x = x; ring_y = y; ring_r = radius; }
    void  ClearRing() { ring_on = false; }
    bool  RingUp() const { return ring_on; }
    SDL_FPoint RingCentre() const { return {ring_x, ring_y}; }
    float RingRadius() const { return ring_r; }
    bool  InsideRing(float x, float y) const;
    // True when a step between the two points passes through the ring.
    bool  RingCrossed(float from_x, float from_y, float to_x, float to_y) const;
    // Draws the exposed faces of every raised cell the camera can see. Called
    // between the ground and everything that stands on it.
    void  RenderCliffs(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const;

    // --- lookups -------------------------------------------------------------
    const Portal* PortalAt(const SDL_FRect& box) const;
    // A collider can be switched off and on again: a door opening is its
    // solid box going out of the way. Off, nothing stops at it.
    void  SetColliderOn(int index, bool on) {
        if (index >= 0 && index < static_cast<int>(collider_off.size())) collider_off[index] = on ? 0 : 1;
    }
    bool  Spawn(const string& name, SDL_FPoint& out) const;
    SDL_FPoint DefaultSpawn() const;
    // A point a scene names -- where a camera looks, an actor stands, a cart
    // stops. Not an arrival: nobody comes into the map there, so it need not
    // be on the floor (a camera over a roof, the player lying in a bed).
    bool  Mark(const string& name, SDL_FPoint& out) const;

    const vector<EnemySpawnDef>& Enemies() const { return enemies; }
    // How far down the Reverie this is: 1 where a sleeper arrives, 2 and 3
    // down the ladders, and 0 for anywhere awake.
    int DreamDepth() const { return dream_depth; }
    const vector<NpcDef>&        Npcs() const { return npcs; }
    const vector<MapObject>&     Objects() const { return objects; }
    const vector<Portal>&        Portals() const { return portals; }
    const vector<Hazard>&        Hazards() const { return hazards; }
    const vector<ThinIce>&       ThinIces() const { return thin_ice; }
    // The thin ice under a point, or null; where two overlap, the weaker.
    const ThinIce* ThinIceAt(float x, float y) const;
    // The hazard under a box, or null.
    const Hazard* HazardAt(const SDL_FRect& box) const;
    // Objects that are not in the file: the player's own camp. They last until
    // the map is loaded again, so the world puts them back each time.
    void AddObject(const MapObject& o) { objects.push_back(o); }
    void RemoveObjects(const string& id_prefix) {
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                          [&](const MapObject& o) { return o.id.rfind(id_prefix, 0) == 0; }),
                      objects.end());
    }

private:
    void   BuildChunks();
    int    AddCollider(const SDL_FRect& r, bool water = false);
    string ResolveAsset(const string& rel) const;

    struct Chunk { vector<int> layers[3]; vector<int> colliders; };
    const Chunk* ChunkAt(int cx, int cy) const;
    void ForEachChunkInRect(const SDL_FRect& r,
                            const std::function<void(const Chunk&)>& fn) const;

    bool   loaded = false;
    string id, display_name, source_dir, ambient, subtitle;
    int    dream_depth = 0;
    bool   interior = false;
    bool   dark = false;
    SDL_Color background{24, 20, 32, 255};

    vector<string>       textures;      // resolved image paths
    vector<Uint8>        surfaces;      // Shaders::Surface of each, parallel to textures
    // The dress (see HasDress): a role and, for the floor and walls, the undyed
    // picture of every texture; what is on just now; where the light stands.
    vector<Uint8>        dress_role;
    vector<string>       dress_undyed;
    bool                 has_dress = false;
    SDL_FPoint           dress_light{};
    mutable Dress        dress;
    // The picture a tile draws under the present dress, and its tint; null
    // when it is put away.
    SDL_Texture* DressedTexture(TextureCache& cache, int tex, SDL_Color& tint) const;
    vector<const Shaders::Art*> arts;   // and how each moves and lights
    Shaders::Fog         fog;
    vector<TileInstance> tiles;
    vector<SDL_FRect>    colliders;
    // Parallel to `colliders`: which of them are water. A bitmap beside the
    // rects rather than a second list, so the chunk index built over the
    // colliders serves both questions.
    vector<uint8_t>      collider_water;
    // And which of them are switched off: see SetColliderOn.
    vector<uint8_t>      collider_off;
    int                  water_count = 0;
    // The ring of fire, while one burns: see SetRing.
    bool  ring_on = false;
    float ring_x = 0.0f, ring_y = 0.0f, ring_r = 0.0f;

    vector<Portal>        portals;
    vector<Hazard>        hazards;
    vector<ThinIce>       thin_ice;
    vector<EnemySpawnDef> enemies;
    vector<NpcDef>        npcs;
    vector<MapObject>     objects;
    map<string, SDL_FPoint> spawns;
    map<string, SDL_FPoint> marks;

    // Height grid. Empty on a map that does not use elevation.
    vector<uint8_t>   elev;
    string cliff_texture;         // what an exposed bank is made of
    // What rolls over its top edge: grass, on a bank of earth; the lip of a
    // plank, on a deck in the Bayou.
    SDL_Color cliff_lip{108, 138, 74, 255};
    vector<SDL_FRect> ramps;
    int   elev_cols = 0, elev_rows = 0;
    float elev_cell = 32.0f;
    int   LevelCell(int cx, int cy) const;
    // Colour multiplier for ground standing this many levels up.
    static Uint8 LevelShade(int level);

    float bounds_w = 0, bounds_h = 0;
    int   chunk_cols = 0, chunk_rows = 0;
    vector<Chunk> chunks;

    static constexpr float CHUNK = 256.0f;
};
