#include "npc.h"
#include "../world/world.h"

void Npc::Init(const NpcDef& def, const GameContext& ctx) {
    id            = def.id;
    name          = def.name;
    dialogue_root = def.dialogue;
    shown_name     = name;
    shown_dialogue = dialogue_root;
    states        = def.states;
    state_index   = -1;
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
    if (away || alpha <= 0.0f) return;
    float a = alpha;
    if (flicker) {
        // Coming and going, never quite the same twice: a slow swell and a
        // quicker stutter over it, out of step with every other one.
        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f + x * 0.013f + y * 0.007f;
        const float swell = 0.5f + 0.5f * sinf(t * 1.7f);
        const float stutter = sinf(t * 11.0f) * sinf(t * 7.3f + 1.0f);
        a *= std::clamp(0.35f + 0.5f * swell + 0.25f * stutter, 0.05f, 1.0f);
    }
    // Adrift: up and down, slowly, out of step with anything else afloat.
    const float drift = bob != 0.0f
        ? roundf(bob * sinf(static_cast<float>(SDL_GetTicks()) / 1000.0f * 1.1f + x * 0.021f + y * 0.013f))
        : 0.0f;
    if (!image.empty()) {
        SDL_Texture* tex = cache.Get(image);
        if (!tex) return;
        const SDL_Point size = cache.Size(image);
        const float from = static_cast<float>(std::clamp(image_from, 0, size.y - 1));
        const float h = static_cast<float>(size.y) - from;
        // Sunk: as many rows of it under the water as it is pulled down, and
        // the rest drawn that much lower, so its foot stays at the waterline.
        const float under = std::clamp(roundf(sink), 0.0f, h - 1.0f);
        const SDL_FRect src = {0.0f, from, static_cast<float>(size.x), h - under};
        const SDL_FRect world = {x - size.x / 2.0f, y - lift - drift - draw_lift - h + under, static_cast<float>(size.x),
                                 h - under};
        const SDL_FRect dst = cam.ToScreenRect(world);
        SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(255.0f * std::clamp(a, 0.0f, 1.0f)));
        SDL_RenderTexture(r, tex, &src, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        return;
    }
    SDL_Color t = tint;
    t.a = static_cast<Uint8>(255.0f * std::clamp(a, 0.0f, 1.0f));
    if (dissolve > 0.0f && Shaders::Effects()) {
        // Going to smoke, or coming out of it: the sprite shader eats the
        // figure away from the edges and lets it drift up into the air.
        Shaders::SpriteFx fx;
        fx.dissolve = std::clamp(dissolve, 0.0f, 1.0f);
        fx.dissolve_kind = 3;
        fx.seed = fmodf(x * 0.31f + y * 0.17f, 7.0f);
        fx.rim = true;
        sprite.Draw(r, cache, cam, x, y - lift - drift - draw_lift, t, SDL_BLENDMODE_BLEND, 1.0f, &fx);
        return;
    }
    if (dissolve > 0.0f) t.a = static_cast<Uint8>(t.a * (1.0f - std::clamp(dissolve, 0.0f, 1.0f)));
    sprite.Draw(r, cache, cam, x, y - lift - drift - draw_lift, t);
}

void Npc::ApplyState(int index) {
    if (index >= static_cast<int>(states.size())) index = -1;
    state_index = index;
    shown_name = name;
    shown_dialogue = dialogue_root;
    alpha = 1.0f;
    flicker = false;
    sort_bias = 0.0f;
    if (index < 0) {
        // Back as defined: where they were placed, facing the way they face.
        away = false;
        x = home_x;
        y = home_y;
        facing = home_facing;
        sprite.facing = facing;
        shown = -1.0f;
        return;
    }
    const NpcState& s = states[index];
    away = s.hidden;
    if (s.moved) { x = s.x; y = s.y; }
    else         { x = home_x; y = home_y; }
    facing = s.turned ? s.facing : home_facing;
    sprite.facing = facing;
    if (!s.name.empty())     shown_name = s.name;
    if (!s.dialogue.empty()) shown_dialogue = s.dialogue;
    alpha = s.alpha;
    flicker = s.flicker;
    sort_bias = s.sort_bias;
    sprite.Play(s.pose.empty() ? string("idle") : s.pose, true);
}

string Npc::AsleepText() const {
    if (state_index < 0) return "";
    const NpcState& s = states[state_index];
    if (!s.asleep_text.empty()) return s.asleep_text;
    return shown_name + " is asleep. Nothing you do wakes them.";
}

void Npc::WalkTo(float tx, float ty, float speed, const string& clip) {
    scripted = true;
    walk_on = true;
    walk_x = tx;
    walk_y = ty;
    walk_speed = std::max(1.0f, speed);
    walk_clip = clip.empty() ? string("walk") : clip;
}

void Npc::Hold(const string& clip, bool restart) {
    hold_clip = clip;
    if (!walk_on) sprite.Play(clip.empty() ? string("idle") : clip, restart);
}

void Npc::FaceToward(float tx, float ty) {
    const float dx = tx - x, dy = ty - y;
    if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
    else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    sprite.facing = facing;
}

void Npc::Update(float dt, World& world, const GameContext& ctx) {
    (void)ctx;

    // --- held by a scene ----------------------------------------------------------------
    if (scripted) {
        if (walk_on) {
            const float dx = walk_x - x, dy = walk_y - y;
            const float d = Length(dx, dy);
            const float step = walk_speed * dt;
            if (d <= step || d < 0.5f) {
                x = walk_x;
                y = walk_y;
                walk_on = false;
                sprite.Play(hold_clip.empty() ? string("idle") : hold_clip, !hold_clip.empty());
            } else {
                x += dx / d * step;
                y += dy / d * step;
                if (fabsf(dx) > fabsf(dy)) facing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
                else                       facing = dy > 0 ? FACE_DOWN : FACE_UP;
                sprite.Play(walk_clip);
            }
        } else if (hold_clip.empty()) {
            sprite.Play("idle");
        }
        sprite.facing = facing;
        sprite.Update(dt);
        return;
    }

    // --- held by a story: asleep in a doorway, slumped over a stall -----------------------
    if (state_index >= 0) {
        if (away) return;
        const NpcState& s = states[state_index];
        if (talking && !s.asleep) FaceToward(world.player.x, world.player.y);
        else {
            facing = s.turned ? s.facing : home_facing;
            sprite.facing = facing;
        }
        sprite.Play(s.pose.empty() ? string("idle") : s.pose);
        sprite.Update(dt);
        return;
    }

    if (talking) {
        // Hold still and keep looking at whoever is talking.
        FaceToward(world.player.x, world.player.y);
        sprite.Play("idle");
        sprite.Update(dt);
        return;
    }

    // --- a round, by the clock -------------------------------------------------
    if (Walks()) {
        const double now = world.WorldSeconds() + phase;
        const double round_no = std::floor(now / round_time);
        const float want = static_cast<float>(now - round_no * round_time);
        // A round may begin only inside their hours, and is always finished.
        bool out = true;
        if (from_hour != to_hour) {
            const double began = WorldClock::HoursAt(round_no * round_time - phase);
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
