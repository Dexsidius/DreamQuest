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
    // As the story has them now: a state may call them something else, or
    // give them something else to say.
    const string& Name() const { return shown_name; }
    const string& DialogueRoot() const { return shown_dialogue; }
    const string& Shop() const { return shop; }

    // --- what a story has made of them -------------------------------------------------
    // Which of NpcDef::states holds, settled by the world whenever its flags
    // change (World::SettleStory); -1 is as defined. While one holds they stand
    // where it puts them, or where they were placed, in its pose, and walk no
    // round and wander nowhere.
    void ApplyState(int index);
    int  StateIndex() const { return state_index; }
    const vector<NpcState>& States() const { return states; }
    bool Asleep() const { return state_index >= 0 && states[state_index].asleep; }
    // What trying to wake them shows.
    string AsleepText() const;
    // The story's prompt for them and its marker over them: see NpcState.
    const string& Prompt() const { static const string none; return state_index >= 0 ? states[state_index].prompt : none; }
    bool  Marked() const { return state_index >= 0 && states[state_index].mark && !away; }

    // --- what a scene is doing with them ------------------------------------------------
    // While a scene holds them (StoryDirector), nothing else moves them: not
    // the clock, not a wander, not practice.
    bool  scripted = false;
    // Made for a scene, not the map: gone when it is over, and nobody to talk to.
    bool  actor = false;
    // Walked by a scene to a point, straight, at a pace, in a clip ("walk",
    // "run"); facing the way they go. Arrived, they stand in `hold`.
    void  WalkTo(float tx, float ty, float speed, const string& clip = "walk");
    bool  Walking() const { return walk_on; }
    void  StopWalking() { walk_on = false; }
    // A clip held when not walking: "lie", "slump", the stranger's gesture.
    // A one-shot plays once and stays on its last frame. Empty: idle.
    void  Hold(const string& clip, bool restart = false);
    const string& Held() const { return hold_clip; }
    void  SetName(const string& n) { shown_name = n; }
    // A picture rather than a person: a cart a scene pushes along the road.
    // Drawn standing on (x, y), not animated -- from this row of it down, so
    // a bed's blanket can be laid over whoever is lying in it.
    string image;
    int   image_from = 0;
    float alpha = 1.0f;          // how solid: 0 is not there to see
    float scale = 1.0f;          // how big: a child is a smaller townsperson
    float dissolve = 0.0f;       // gone to smoke: 0 whole, 1 nothing left
    bool  flicker = false;       // a faint shape coming and going
    float lift = 0.0f;           // drawn this far above their feet (a mattress afloat)
    float bob = 0.0f;            // and rising and falling this far either side of it, slowly
    float sink = 0.0f;           // a picture this many rows down into the water it floats on: a bobber pulled under

    bool talking = false;        // frozen while in conversation
    // Whether they are someone who practises, and whether a cast is under way.
    // Not while asleep: the college's practising mages, before the cure.
    bool Practises() const { return cast_every > 0.0f && !cast_bolt.empty() && !Asleep() && !away; }
    bool Casting() const { return casting > 0.0f; }

private:
    string id, name, dialogue_root, shop;
    string shown_name, shown_dialogue;
    // Their own sheet, and where another one a state names is looked up.
    const SpriteDef*     own_def = nullptr;
    const SpriteLibrary* library = nullptr;
    // Put them in `def` (their own when null), the clip they were in carried
    // over: a state's body is a change of clothes, not of what they are doing.
    void Wear(const SpriteDef* def);
    vector<NpcState> states;
    int    state_index = -1;
    // A scene's walk.
    bool   walk_on = false;
    float  walk_x = 0, walk_y = 0, walk_speed = 60.0f;
    string walk_clip = "walk", hold_clip;
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
public:
    // A colour laid over them: a temper rising (StoryDirector "tint", or a
    // state's own -- NpcState::tint).
    SDL_Color tint{255, 255, 255, 255};
    // Steam off them every so often, while their state says (NpcState::steam):
    // seconds between puffs, 0 for none.
    float SteamEvery() const { return state_index >= 0 && !away ? states[state_index].steam : 0.0f; }
    float steam_timer = 0.0f;
private:
    SDL_Color home_tint{255, 255, 255, 255};

    // Practice: see NpcDef::cast_bolt.
    string cast_bolt;
    float  cast_x = 0.0f, cast_y = 0.0f, cast_every = 0.0f;
    float  cast_timer = 0.0f;    // until the next
    float  casting = 0.0f;       // seconds into this one; 0 when not
    bool   cast_thrown = false;
    static constexpr float CAST_TIME = 0.55f, CAST_RELEASE = 0.22f;
};
