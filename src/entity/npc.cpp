#include "npc.h"
#include "../world/world.h"

void Npc::Init(const NpcDef& def, const GameContext& ctx) {
    id            = def.id;
    name          = def.name;
    dialogue_root = def.dialogue;
    shop          = def.shop;
    x = home_x    = def.x;
    y = home_y    = def.y;
    facing        = def.facing;
    home_facing   = def.facing;
    wanders       = def.wanders;

    hp = max_hp = 1;
    foot_box = {-8.0f, -10.0f, 16.0f, 10.0f};
    body_box = {-12.0f, -38.0f, 24.0f, 38.0f};

    if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(def.sprite));
    sprite.facing = facing;
    sprite.Play("idle", true);
}

void Npc::FaceToward(float tx, float ty) {
    const float dx = tx - x, dy = ty - y;
    if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
    else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    sprite.facing = facing;
}

void Npc::Update(float dt, World& world, const GameContext& ctx) {
    (void)ctx;

    if (talking) {
        // Hold still and keep looking at whoever is talking.
        FaceToward(world.player.x, world.player.y);
        sprite.Play("idle");
        sprite.Update(dt);
        return;
    }

    float move_x = 0, move_y = 0;

    if (wanders) {
        wander_timer -= dt;
        if (wander_timer <= 0.0f) {
            wander_timer = 2.0f + (rand() % 100) / 25.0f;
            if (rand() % 2 == 0) {
                const float angle = (rand() % 628) / 100.0f;
                wander_dx = cosf(angle);
                wander_dy = sinf(angle);
            } else {
                wander_dx = wander_dy = 0.0f;
            }
        }

        // Stay near the spot they were placed at, so a villager does not
        // wander out of the village.
        const float home_dist = Length(x - home_x, y - home_y);
        if (home_dist > 56.0f) {
            wander_dx = (home_x - x) / home_dist;
            wander_dy = (home_y - y) / home_dist;
        }

        move_x = wander_dx * 26.0f;
        move_y = wander_dy * 26.0f;
    }

    if (fabsf(move_x) + fabsf(move_y) > 0.5f) {
        if (fabsf(move_x) > fabsf(move_y)) facing = (move_x > 0) ? FACE_RIGHT : FACE_LEFT;
        else                               facing = (move_y > 0) ? FACE_DOWN  : FACE_UP;

        const SDL_FPoint p = world.map.MoveWithCollision(Bounds(), move_x * dt, move_y * dt);
        x = p.x - foot_box.x;
        y = p.y - foot_box.y;
        sprite.Play("walk");
    } else {
        if (!wanders) facing = home_facing;
        sprite.Play("idle");
    }

    sprite.facing = facing;
    sprite.Update(dt);
}
