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
    cast_bolt  = def.cast_bolt;
    cast_x     = def.cast_x;
    cast_y     = def.cast_y;
    cast_every = def.cast_every;
    // Not all at once: each starts somewhere in their own interval, by where they stand.
    cast_timer = cast_every > 0.0f ? fmodf(fabsf(def.x * 0.37f + def.y * 0.11f), cast_every) + 0.4f : 0.0f;
    tint = def.tint;

    // The round, laid out in time: a wait at each stop and a walk to the next,
    // and for one that goes there and back, the same again the other way.
    legs.clear();
    round_time = 0.0f;
    phase = def.phase;
    from_hour = def.from_hour;
    to_hour = def.to_hour;
    shown = -1.0f;
    away = false;
    if (def.path.size() >= 2) {
        vector<NpcStop> stops = def.path;
        if (def.ping_pong)
            for (int i = static_cast<int>(def.path.size()) - 2; i >= 1; --i) stops.push_back(def.path[i]);
        const float pace = std::max(8.0f, def.speed);
        for (size_t i = 0; i < stops.size(); ++i) {
            const NpcStop& a = stops[i];
            const NpcStop& b = stops[(i + 1) % stops.size()];
            if (a.pause > 0.0f) {
                legs.push_back({a.x, a.y, a.x, a.y, round_time, a.pause, false, a.facing});
                round_time += a.pause;
            }
            const float d = Length(b.x - a.x, b.y - a.y);
            if (d < 0.5f) continue;
            Facing f = fabsf(b.x - a.x) > fabsf(b.y - a.y) ? (b.x > a.x ? FACE_RIGHT : FACE_LEFT)
                                                           : (b.y > a.y ? FACE_DOWN : FACE_UP);
            legs.push_back({a.x, a.y, b.x, b.y, round_time, d / pace, true, f});
            round_time += d / pace;
        }
        if (round_time < 1.0f) legs.clear();
    }
}

SDL_FPoint Npc::PlaceAt(float into_round, Facing* face, bool* walking) const {
    if (legs.empty()) return {x, y};
    const float t = std::clamp(into_round, 0.0f, std::max(0.0f, round_time - 0.001f));
    const Leg* leg = &legs.back();
    for (const Leg& l : legs)
        if (t < l.start + l.length) { leg = &l; break; }
    const float k = leg->length > 0.0f ? std::clamp((t - leg->start) / leg->length, 0.0f, 1.0f) : 0.0f;
    if (face) *face = leg->facing;
    if (walking) *walking = leg->walk;
    return {leg->x0 + (leg->x1 - leg->x0) * k, leg->y0 + (leg->y1 - leg->y0) * k};
}

void Npc::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    if (away) return;
    sprite.Draw(r, cache, cam, x, y - draw_lift, tint);
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

    // --- a round, by the clock -------------------------------------------------
    if (Walks()) {
        const double now = (static_cast<double>(world.clock.Day()) * 24.0 + world.clock.Hours()) *
                           WorldClock::SECONDS_PER_HOUR + phase;
        const double round_no = std::floor(now / round_time);
        const float want = static_cast<float>(now - round_no * round_time);
        // A round may begin only inside their hours, and is always finished.
        bool out = true;
        if (from_hour != to_hour) {
            const double began = (round_no * round_time - phase) / WorldClock::SECONDS_PER_HOUR;
            const float hour = static_cast<float>(began - std::floor(began / 24.0) * 24.0);
            out = from_hour < to_hour ? (hour >= from_hour && hour < to_hour)
                                      : (hour >= from_hour || hour < to_hour);
        }
        away = !out;
        if (away) { shown = -1.0f; return; }

        // Let go of a conversation, they are behind where the clock has them:
        // they make it up at a brisk walk rather than being put there.
        float behind = shown < 0.0f ? 0.0f : want - shown;
        if (behind < 0.0f) behind += round_time;
        if (shown < 0.0f || behind > 40.0f || behind <= dt * 1.5f) shown = want;
        else {
            shown += dt * 2.4f;
            if (shown >= round_time) shown -= round_time;
        }
        bool walking = false;
        Facing face = facing;
        const SDL_FPoint at = PlaceAt(shown, &face, &walking);
        x = at.x;
        y = at.y;
        facing = face;
        sprite.facing = facing;
        sprite.Play(walking ? "walk" : "idle");
        sprite.Update(dt);
        return;
    }

    // --- practice ---------------------------------------------------------------
    // Stood at their mark, facing the dummy; every so often, a cast. The bolt is
    // let go a little way into the throw, the way the player's is, and is the
    // world's to fly: it touches nobody and bursts on the dummy.
    if (Practises()) {
        FaceToward(cast_x, cast_y);
        sprite.facing = facing;
        if (casting > 0.0f) {
            casting += dt;
            if (!cast_thrown && casting >= CAST_RELEASE) {
                cast_thrown = true;
                world.ThrowPracticeBolt(cast_bolt, x, y - 22.0f, cast_x, cast_y - 26.0f, ctx);
            }
            if (casting >= CAST_TIME) { casting = 0.0f; sprite.Play("idle", true); }
        } else {
            cast_timer -= dt;
            if (cast_timer <= 0.0f) {
                // Give or take a third, so a row of them is not a metronome.
                cast_timer = cast_every * (0.78f + (rand() % 45) / 100.0f);
                casting = 0.001f;
                cast_thrown = false;
                sprite.Play("attack", true);
            } else {
                sprite.Play("idle");
            }
        }
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
