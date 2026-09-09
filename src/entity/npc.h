#pragma once
#include "entity.h"
#include "../world/map.h"

// Townsfolk: they stand around (or wander a little), turn to face the player
// when spoken to, and hand off to the dialogue system.
class Npc : public Entity {
public:
    void Init(const NpcDef& def, const GameContext& ctx);
    void Update(float dt, World& world, const GameContext& ctx) override;

    void FaceToward(float tx, float ty);

    const string& Id() const { return id; }
    const string& Name() const { return name; }
    const string& DialogueRoot() const { return dialogue_root; }
    const string& Shop() const { return shop; }

    bool talking = false;        // frozen while in conversation

private:
    string id, name, dialogue_root, shop;
    Facing home_facing = FACE_DOWN;
    float  home_x = 0, home_y = 0;
    bool   wanders = false;
    float  wander_timer = 0.0f;
    float  wander_dx = 0, wander_dy = 0;
};
