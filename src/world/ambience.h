#pragma once
#include "../headers.h"
#include "../camera.h"

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
// None of it touches gameplay: nothing here can be struck, blocks a step or
// is told to a friend (each machine has its own birds). It exists so that a
// forest reads as a forest rather than as a green map with trees on it,
// which is most of what makes the walk between two towns feel like a journey.
class Ambience {
public:
    // Picks what drifts through the air from a map's "ambient" name, and
    // clears whatever the last map had. Called on every map load.
    void SetKind(const string& ambient, bool interior);

    // What the air needs of the world it is in, each frame.
    struct World {
        float daylight = 1.0f;                       // 1 by day, 0 at night (birds roost)
        vector<SDL_FPoint> walkers;                  // the players: birds keep away from them
        std::function<bool(float, float)> stand;     // open ground a bird could stand on
        bool lively = true;                          // Visual Effects on: clouds, birds, streaks
    };
    void Update(float dt, const Camera& cam, const World& world);

    // World-space motes, then a screen-space vignette. Drawn over the world
    // and under the HUD.
    void Render(SDL_Renderer* r, const Camera& cam) const;
    // On the floor, under everything that stands on it: the birds on the
    // ground, and the shadows of those in the air.
    void RenderGround(SDL_Renderer* r, const Camera& cam) const;
    // Over everything, under the night: the clouds' shadows, then the birds
    // in the air.
    void RenderSky(SDL_Renderer* r, const Camera& cam) const;
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

    // The birds, for the self-test: where each is, how high, and which.
    struct BirdView { float x = 0, y = 0, height = 0; bool flying = false; int species = 0; };
    vector<BirdView> Birds() const;
    // What is in the air, for the self-test: where each mote is, and what.
    enum MoteKind { LEAF, FIREFLY, POLLEN, DUST, WISP, SNOW, EMBER, FLURRY, BUBBLE, WIND, RAIN, STREAK, ASH, SOOT };
    struct MoteView { float x = 0, y = 0; MoteKind kind = POLLEN; };
    vector<MoteView> Motes() const;
    // How thick the smoke drifting over a burnt place is, 0 where there is none
    // (and with Visual Effects off).
    float Haze() const;
    // The clouds' shadows as a mask, `size` square, that tiles: 0 clear sky,
    // 255 under a cloud, soft between.
    static vector<Uint8> CloudMask(int size);

    // The bird sheet (tools/make_birds.py): a row a bird, six frames of 16 px.
    static constexpr const char* BIRDS_SHEET = "assets/effects/birds.png";
    enum Species { SPARROW = 0, CROW = 1, EGRET = 2 };

private:
    // The Primordium's: bubbles rising in the Deeps, the wind over the
    // Firmament, rain and lightning in the Tempest, and in the Conflux motes
    // of all five elements going up together.
    // The Scoured Flats are Salt: the plateau's ash coming down on them, and
    // the wind driving the salt along the ground in its gusts.
    enum class Kind { None, Field, Town, Forest, Grove, Dungeon, Dream, Snow, Ash, Salt, Deep, Gale, Storm, Conflux };

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
        enum State { LANDING, STANDING, PECKING, HOPPING, FLEEING, PASSING };
        State state = STANDING;
        float x = 0, y = 0, h = 0;   // where on the ground, and how high over it
        float vx = 0, vy = 0;        // world px / s
        float tx = 0, ty = 0;        // where it is landing, or hopping to
        float t = 0, until = 0;      // time in this state, and when it ends
        float flap = 0;              // wings' phase
        float glide = 0;             // a flock's: its own beat between flapping and gliding
        int species = SPARROW;
        bool left = false;           // facing (flying: heading) left
    };

    void Populate(const SDL_FRect& view);
    Mote Make(MoteKind k, const SDL_FRect& view);
    void UpdateBirds(float dt, const SDL_FRect& view, const World& world);
    int PickSpecies(bool flock);
    // Where the air is of a kind that has clouds over it, and how dark they are.
    float CloudStrength() const;
    SDL_Texture* Sheet(SDL_Renderer* r) const;
    void DrawBird(SDL_Renderer* r, const Camera& cam, const Bird& b, bool shadow) const;

    Kind kind = Kind::None;
    vector<Mote> motes;
    vector<Bird> birds;
    std::mt19937 rng{20260913u};
    bool seeded = false;
    float gust = 0.0f, gust_wait = 7.0f, gust_age = -1.0f, gust_len = 0.0f;
    float flash = 0.0f, flash_wait = 4.0f, flash_age = -1.0f;
    float land_wait = 1.5f, flock_wait = 9.0f;
    float daylight = 1.0f;
    bool lively = true;

    // Made the first time they are drawn, for the renderer they are drawn with.
    mutable SDL_Renderer* made_for = nullptr;
    mutable SDL_Texture* sheet = nullptr;
    mutable SDL_Texture* clouds = nullptr;
};
