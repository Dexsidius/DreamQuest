#pragma once
#include "../headers.h"
#include "../camera.h"

// The air of a place: leaves coming down in the Whisperwood, fireflies over
// Fernhollow's pond, dust hanging in the mines, and the edges of the screen
// dimming under a canopy.
//
// None of it touches gameplay. It exists so that a forest reads as a forest
// rather than as a green map with trees on it, which is most of what makes the
// walk between two towns feel like a journey.
class Ambience {
public:
    // Picks what drifts through the air from a map's "ambient" name, and
    // clears whatever the last map had. Called on every map load.
    void SetKind(const string& ambient, bool interior);

    void Update(float dt, const Camera& cam);

    // World-space motes, then a screen-space vignette. Drawn over the world
    // and under the HUD.
    void Render(SDL_Renderer* r, const Camera& cam) const;

private:
    enum class Kind { None, Field, Town, Forest, Grove, Dungeon, Dream };
    enum MoteKind { LEAF, FIREFLY, POLLEN, DUST, WISP };

    struct Mote {
        float x = 0, y = 0;          // world position
        float vx = 0, vy = 0;        // drift, world px / s
        float phase = 0, speed = 1;  // sway or pulse
        float size = 1;              // world px
        SDL_Color color{255, 255, 255, 255};
        MoteKind kind = POLLEN;
    };

    void Populate(const SDL_FRect& view);
    Mote Make(MoteKind k, const SDL_FRect& view);

    Kind kind = Kind::None;
    vector<Mote> motes;
    std::mt19937 rng{20260913u};
    bool seeded = false;
};
