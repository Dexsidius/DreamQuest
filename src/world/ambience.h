#pragma once
#include "../headers.h"
#include "../camera.h"

class Map;

// The air of a place: leaves coming down in the Whisperwood, fireflies over
// Fernhollow's pond, dust hanging in the mines, and the edges of the screen
// dimming under a canopy. Out of doors by day, the wind as well -- leaves
// tumbling along it and faint streaks of it, both quickening as a gust rolls
// through (Shaders::GustAt, the same gusts the grass and trees lean in) --
// the shadows of clouds drifting over the ground, and birds: pecking about
// on open ground until somebody comes close, then up and away, and now and
// then a flock going over, its shadows crossing the ground under it. Where
// the land is burnt -- the Ashen Path, Purgatory's Plateau -- ash coming down
// and soot blowing through it, under a haze of smoke drifting over.
//
// And the rest of what lives and happens out there (ambience_*.cpp):
//   - the weather (Weather::Rain): showers, puddles, rings on the water, and
//     the snow coming down softly over the Frostreach, with an aurora by night
//   - small animals (critters): rabbits and squirrels that bolt for cover,
//     deer at the woods' edge at dawn, frogs on the lily pads, fish jumping,
//     bats at dusk, rats along a dungeon wall, lizards on the burnt rocks,
//     hens and cats about the villages, gulls over the water
//   - what is left underfoot: prints in snow, sand, salt, mud and ash, and
//     splashes in the wet
//   - light: shafts of sun through the Whisperwood's canopy, leaf shadows
//     moving under the trees, dust turning in a room's window light, the moon
//     on still water
//   - the breath of whoever is out in the cold, and in the Reverie feathers
//     and petals falling and a figure that is never there when you get close
//
// None of it touches gameplay: nothing here can be struck, blocks a step or
// is told to a friend (each machine has its own birds). It exists so that a
// forest reads as a forest rather than as a green map with trees on it,
// which is most of what makes the walk between two towns feel like a journey.
class Ambience {
public:
    // Picks what drifts through the air from a map's "ambient" name, and
    // clears whatever the last map had. Called on every map load.
    void SetKind(const string& ambient, bool interior);
    // What there is on this map for the life in the air to use: the ground
    // under each step, the trees and bushes (shade, and cover to bolt for),
    // the lily pads, the gravestones, the water, where the hens and the cats
    // live (a map's "critters" objects), which animals it already has to hunt
    // (so none of theirs wanders about here as scenery). After SetKind, on
    // every map load. Without it -- the self-test's bare air -- there is no
    // ground to read and nothing standing.
    void SetPlace(const Map& map);

    // Somebody out in it: the players.
    struct Walker { float x = 0, y = 0; bool sprinting = false; };
    // What the air needs of the world it is in, each frame.
    struct World {
        float daylight = 1.0f;                       // 1 by day, 0 at night (birds roost)
        float sun = 1.0f;                            // the day outside, even from indoors: a window's light
        float hour = 12.0f;                          // the clock: deer at dawn, bats at dusk
        float rain = 0.0f;                           // how hard a shower is coming down here, 0..1
        float wet = 0.0f;                            // how wet the ground still is, 0..1
        vector<Walker> walkers;                      // the players: everything keeps away from them
        std::function<bool(float, float)> stand;     // open ground a bird could stand on
        bool lively = true;                          // Visual Effects on: clouds, birds, streaks
    };
    void Update(float dt, const Camera& cam, const World& world);

    // World-space motes, the rain and the light over everything, then a
    // screen-space vignette. Drawn over the world (and the night) and under
    // the HUD.
    void Render(SDL_Renderer* r, const Camera& cam) const;
    // On the floor, under everything that stands on it: puddles and rings,
    // the leaf shadows under the trees, footprints, the shadows of what is
    // about, and the birds on the ground.
    void RenderGround(SDL_Renderer* r, const Camera& cam) const;
    // Over everything, under the night: the clouds' shadows, the smoke, the
    // sun through the trees, breath, and whatever is in the air.
    void RenderSky(SDL_Renderer* r, const Camera& cam) const;
    // The critters that stand on the ground sort with everything else that
    // stands (World::Render): each one's foot, and its draw.
    struct Standing { float sort_y = 0; int index = 0; };
    void CollectStanding(const SDL_FRect& view, vector<Standing>& out) const;
    void DrawStanding(SDL_Renderer* r, const Camera& cam, int index) const;
    // How far into a Cozy-look night it is out of doors, 0..1, set before each
    // Render: the edges of the view close in, dusk-blue, as it darkens.
    mutable float dusk = 0.0f;

    // Flurries, on the mountain: every so often the wind gets up for a few
    // seconds -- the snow driven sideways, streaks of it low over the ground,
    // the view gone white round the edges -- and then drops again. 0 in the
    // calm, 1 at the height of a gust.
    float Gust() const { return gust; }
    static constexpr float GUST_RISE = 1.5f, GUST_FALL = 2.0f;
    // The Tempest's lightning: the whole view lit for a moment every few
    // seconds, brightest as it strikes and gone in a quarter of one. 0 between.
    float Flash() const { return flash; }
    static constexpr float FLASH_TIME = 0.25f;

    // --- what is underfoot ---------------------------------------------------------
    enum Footing : Uint8 { FOOT_NONE, FOOT_GRASS, FOOT_EARTH, FOOT_STONE, FOOT_WOOD, FOOT_SAND, FOOT_SNOW, FOOT_SALT,
                           FOOT_MUD, FOOT_ASH, FOOT_ICE };
    // What a ground tile is to walk on, from its art's name.
    static Footing FootingOf(const string& tile_path);
    Footing FootingAt(float x, float y) const;
    // What a sprint kicks up here (World::AddDust): dust off earth and roads,
    // grey off ash, white off snow and salt, drops off mud and wet ground.
    SDL_Color KickedUp(float x, float y) const;

    // --- for the self-test -----------------------------------------------------------
    // The birds: where each is, how high, and which.
    struct BirdView { float x = 0, y = 0, height = 0; bool flying = false; int species = 0; };
    vector<BirdView> Birds() const;
    // What is in the air: where each mote is, and what.
    enum MoteKind { LEAF, FIREFLY, POLLEN, DUST, WISP, SNOW, EMBER, FLURRY, BUBBLE, WIND, RAIN, STREAK, ASH, SOOT,
                    FEATHER, PETAL };
    struct MoteView { float x = 0, y = 0; MoteKind kind = POLLEN; };
    vector<MoteView> Motes() const;
    // How thick the smoke drifting over a burnt place is, 0 where there is none
    // (and with Visual Effects off).
    float Haze() const;
    // The small animals (tools/make_critters.py has a row of the sheet each).
    enum CritterKind { RABBIT = 0, SQUIRREL, HEN, CAT, FROG, FISH, BAT, RAT, LIZARD, DEER, WATCHER, CRITTER_KINDS };
    struct CritterView { float x = 0, y = 0, height = 0; int kind = 0; bool fleeing = false, hidden = false; };
    vector<CritterView> Critters() const;
    // Prints in the ground, and the rain's work: how many puddles are standing
    // and how hard it is coming down as drawn.
    struct PrintView { float x = 0, y = 0; Footing on = FOOT_NONE; float age = 0; };
    vector<PrintView> Prints() const;
    int PuddlesShown() const;
    float RainShown() const { return rain_shown; }
    // The breath of anybody out in the cold, in puffs alive.
    int Breaths() const { return static_cast<int>(breath.size()); }
    // Whether an aurora is over this place now (the Frostreach, at night).
    bool Aurora() const;
    // The trees, bushes and the rest the place survey found.
    struct PlaceView { int trees = 0, bushes = 0, lilies = 0, graves = 0, rocks = 0, nests = 0, water = 0, puddles = 0; };
    PlaceView Place() const;
    // A post of the huntable animal of this kind within a stone's throw: none
    // of that kind is scenery there.
    bool TwinNear(int critter, float x, float y) const;

    // The clouds' shadows as a mask, `size` square, that tiles: 0 clear sky,
    // 255 under a cloud, soft between. The leaves' shadows the same way:
    // blotches of shade with holes of sun in them.
    static vector<Uint8> CloudMask(int size);
    // `frame` of DAPPLE_FRAMES: the holes moved a little each, as the leaves move.
    static vector<Uint8> DappleMask(int size, int frame);
    static constexpr int DAPPLE_FRAMES = 4;

    // The bird sheet (tools/make_birds.py): a row a bird, six frames of 16 px.
    static constexpr const char* BIRDS_SHEET = "assets/effects/birds.png";
    enum Species { SPARROW = 0, CROW = 1, EGRET = 2, GULL = 3 };
    // The critter sheet (tools/make_critters.py): where a kind's frame is.
    static constexpr const char* CRITTERS_SHEET = "assets/effects/critters.png";
    static SDL_Rect CritterCell(int kind, int frame);
    static int CritterFrames(int kind);

private:
    // The Primordium's: bubbles rising in the Deeps, the wind over the
    // Firmament, rain and lightning in the Tempest, and in the Conflux motes
    // of all five elements going up together.
    // The Scoured Flats are Salt: the plateau's ash coming down on them, and
    // the wind driving the salt along the ground in its gusts. A Room is
    // indoors anywhere above ground: dust turning in the window light.
    enum class Kind { None, Field, Town, Forest, Grove, Dungeon, Dream, Snow, Ash, Salt, Deep, Gale, Storm, Conflux, Room };

    struct Mote {
        float x = 0, y = 0;          // world position
        float vx = 0, vy = 0;        // drift, world px / s
        float phase = 0, speed = 1;  // sway or pulse
        float size = 1;              // world px
        float age = 0, life = 0;     // a streak's: how long it has shown, and will;
                                     // a flake of ash still smouldering: how long it has
                                     // burned, and will (0, a cold one)
        SDL_Color color{255, 255, 255, 255};
        MoteKind kind = POLLEN;
    };

    struct Bird {
        enum State { LANDING, STANDING, PECKING, HOPPING, FLEEING, PASSING, CIRCLING };
        State state = STANDING;
        float x = 0, y = 0, h = 0;   // where on the ground, and how high over it
        float vx = 0, vy = 0;        // world px / s
        float tx = 0, ty = 0;        // where it is landing, or hopping to; a gull's: what it wheels round
        float t = 0, until = 0;      // time in this state, and when it ends
        float flap = 0;              // wings' phase
        float glide = 0;             // a flock's: its own beat between flapping and gliding
        int species = SPARROW;
        bool left = false;           // facing (flying: heading) left
    };

    struct Critter {
        enum State { IDLE, BUSY, ALERT, FLEE, HIDING, LEAVING };
        int kind = RABBIT;
        State state = IDLE;
        float x = 0, y = 0, h = 0;   // where on the ground, and how high over it (a bat, a fish's leap)
        float vx = 0, vy = 0;
        float tx = 0, ty = 0;        // where it is going: cover, home, the water
        float t = 0, until = 0;
        float anim = 0;              // its own beat
        float home_x = 0, home_y = 0, radius = 0;
        float fade = 1;              // 1 there; down to 0 as it goes
        int nest = -1;               // the place's nest (or pad, or roost) it keeps to
        bool left = false;
        SDL_Color tint{255, 255, 255, 255};
    };

    // A print in the ground, a splash, a ring opening on water, a puff of breath.
    struct Print { float x = 0, y = 0, dx = 0, dy = 1, age = 0, life = 20; Uint8 on = FOOT_NONE; bool left = false; };
    struct Splash { float x = 0, y = 0, age = 0, life = 0.6f, size = 4; bool ring = true; };
    struct Puff { float x = 0, y = 0, vx = 0, vy = 0, age = 0, life = 1.2f; bool dark = false; };

    // What a map has, surveyed once on arrival (SetPlace).
    struct Spot { float x = 0, y = 0, w = 0, h = 0; };
    struct Nest { int kind = HEN; float x = 0, y = 0, radius = 60; int count = 3; };
    struct Survey {
        string id;
        float width = 0, height = 0;
        float cell = 32.0f;
        int cols = 0, rows = 0;
        vector<Uint8> footing;
        vector<Spot> trees, bushes, lilies, graves, rocks, windows;
        vector<SDL_FPoint> water;    // a point in each 48 px square that has any
        vector<SDL_FRect> water_rects;   // the water as it is drawn: the moon is only on it
        vector<SDL_FPoint> puddles;  // where the rain stands in a dip of the earth or the road
        vector<Nest> nests;
        // Where the map posts animals to be hunted, by the kind they would be
        // here as scenery: none of that kind is scenery near them.
        vector<SDL_FPoint> twins[CRITTER_KINDS];
        bool frost = false;          // the Frostreach: soft snow, an aurora
        bool reverie = false;        // down in the Reverie: the figure
        bool gulls = false;          // water a gull would wheel over
        bool crypt = false;          // under the ground among the dead: bats
    };

    void Populate(const SDL_FRect& view);
    Mote Make(MoteKind k, const SDL_FRect& view);
    void UpdateBirds(float dt, const SDL_FRect& view, const World& world);
    int PickSpecies(bool flock);
    // Where the air is of a kind that has clouds over it, and how dark they are.
    float CloudStrength() const;
    SDL_Texture* Sheet(SDL_Renderer* r) const;
    void DrawBird(SDL_Renderer* r, const Camera& cam, const Bird& b, bool shadow) const;

    // ambience_critters.cpp
    void UpdateCritters(float dt, const SDL_FRect& view, const World& world);
    void DrawCritter(SDL_Renderer* r, const Camera& cam, const Critter& c, bool shadow) const;
    bool Stands(float x, float y, const World& world) const;
    const Spot* NearestCover(float x, float y, float reach, bool trees_only) const;
    // ambience_weather.cpp: the rain and the cold
    void UpdateWeather(float dt, const SDL_FRect& view, const World& world);
    void UpdateSteps(float dt, const World& world);
    void DrawWet(SDL_Renderer* r, const Camera& cam) const;
    void DrawPrints(SDL_Renderer* r, const Camera& cam) const;
    void DrawBreath(SDL_Renderer* r, const Camera& cam) const;
    void DrawAurora(SDL_Renderer* r) const;
    // ambience_light.cpp
    void DrawDapples(SDL_Renderer* r, const Camera& cam) const;
    void DrawShafts(SDL_Renderer* r, const Camera& cam) const;
    void DrawMoon(SDL_Renderer* r, const Camera& cam) const;
    void DrawRoomLight(SDL_Renderer* r, const Camera& cam) const;
    bool OutdoorsKind() const;
    bool RainsHere() const;

    Kind kind = Kind::None;
    Survey place;
    vector<Mote> motes;
    vector<Bird> birds;
    vector<Critter> critters;
    vector<Print> prints;
    vector<Splash> splashes;
    vector<Puff> breath;
    vector<float> step_left;         // each walker's distance to their next print
    vector<SDL_FPoint> step_at;      // and where they were last frame
    vector<int> step_n;              // and how many steps they have taken: left, right
    std::mt19937 rng{20260913u};
    bool seeded = false;
    float gust = 0.0f, gust_wait = 7.0f, gust_age = -1.0f, gust_len = 0.0f;
    float flash = 0.0f, flash_wait = 4.0f, flash_age = -1.0f;
    float land_wait = 1.5f, flock_wait = 9.0f;
    float critter_wait = -1.0f, fish_wait = 3.0f, watcher_wait = 30.0f, breath_wait = 0.0f;
    float daylight = 1.0f, sun = 1.0f, hour = 12.0f, rain = 0.0f, wet = 0.0f, rain_shown = 0.0f;
    float drip_wait = 0.0f;
    bool lively = true;

    // Made the first time they are drawn, for the renderer they are drawn with.
    mutable SDL_Renderer* made_for = nullptr;
    mutable SDL_Texture* sheet = nullptr;
    mutable SDL_Texture* critter_sheet = nullptr;
    mutable SDL_Texture* clouds = nullptr;
    mutable SDL_Texture* dapples = nullptr;
    mutable SDL_Texture* beam = nullptr;
    mutable float critter_light = 1.0f;   // what is drawn over the night is dimmed by it
};
