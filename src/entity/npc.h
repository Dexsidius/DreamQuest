#pragma once
#include "entity.h"
#include "../world/map.h"

// Townsfolk: they stand around (or wander a little), turn to face the player
// when spoken to, and hand off to the dialogue system.
//
// Some of them walk a round: out of a door in the morning, along the street to
// the well, the stall, the gate, a while stood at each, and home again. Where
// one is on its round is worked out from the world's clock and nothing else --
// no state is carried from frame to frame -- so every machine in a co-op game
// puts the same villager in the same place without a word being said about it,
// and the host, asked whether a friend could really have spoken to them, looks
// in the right spot. A round begins only inside the villager's hours and always
// ends where it began, so nobody appears or vanishes in the middle of the road.
// Spoken to, they stop; let go, they hurry along their round until they are
// back where the clock says they should be.
class Npc : public Entity {
public:
    void Init(const NpcDef& def, const GameContext& ctx);
    void Update(float dt, World& world, const GameContext& ctx) override;
    void Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const override;

    // Indoors: not drawn, not spoken to, not on the map.
    bool Away() const { return away; }
    bool Walks() const { return !legs.empty(); }
    // Seconds one round takes.
    float RoundTime() const { return round_time; }
    // Where the round has them at this many seconds into it.
    SDL_FPoint PlaceAt(float into_round, Facing* face = nullptr, bool* walking = nullptr) const;

    void FaceToward(float tx, float ty);

    const string& Id() const { return id; }
    const string& Name() const { return name; }
    const string& DialogueRoot() const { return dialogue_root; }
    const string& Shop() const { return shop; }

    bool talking = false;        // frozen while in conversation
    // Whether they are someone who practises, and whether a cast is under way.
    bool Practises() const { return cast_every > 0.0f && !cast_bolt.empty(); }
    bool Casting() const { return casting > 0.0f; }

private:
    string id, name, dialogue_root, shop;
    Facing home_facing = FACE_DOWN;
    float  home_x = 0, home_y = 0;
    bool   wanders = false;
    float  wander_timer = 0.0f;
    float  wander_dx = 0, wander_dy = 0;

    // A round, as legs: stood at a stop, then walked to the next.
    struct Leg { float x0, y0, x1, y1, start, length; bool walk; Facing facing; };
    vector<Leg> legs;
    float  round_time = 0.0f, phase = 0.0f;
    float  from_hour = 0.0f, to_hour = 0.0f;
    float  shown = -1.0f;        // seconds into the round they are drawn at
    bool   away = false;
    SDL_Color tint{255, 255, 255, 255};

    // Practice: see NpcDef::cast_bolt.
    string cast_bolt;
    float  cast_x = 0.0f, cast_y = 0.0f, cast_every = 0.0f;
    float  cast_timer = 0.0f;    // until the next
    float  casting = 0.0f;       // seconds into this one; 0 when not
    bool   cast_thrown = false;
    static constexpr float CAST_TIME = 0.55f, CAST_RELEASE = 0.22f;
};
