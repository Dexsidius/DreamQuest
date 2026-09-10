#include "enemy.h"
#include "../world/world.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include <fstream>

static constexpr float HURT_STAGGER = 0.22f;
static constexpr float DEATH_LINGER = 1.1f;
static constexpr float SWING_WINDUP = 0.32f;

static SDL_FRect BoxFromJson(const json& j, SDL_FRect fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

bool EnemyDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("EnemyDatabase: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("EnemyDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        EnemyDef d;
        d.id     = it.key();
        d.name   = o.value("name", d.id);
        d.sprite = o.value("sprite", d.id);
        d.hp     = std::max(1, o.value("hp", 10));

        d.attack_level   = o.value("attack", 1);
        d.strength_level = o.value("strength", 1);
        d.defence_level  = o.value("defence", 1);
        d.attack_bonus   = o.value("attack_bonus", 0);
        d.strength_bonus = o.value("strength_bonus", 0);
        d.defence_bonus  = o.value("defence_bonus", 0);

        d.speed           = o.value("speed", 42.0f);
        d.aggro_range     = o.value("aggro", 150.0f);
        d.attack_range    = o.value("attack_range", 26.0f);
        d.attack_cooldown = o.value("attack_cooldown", 1.6f);
        d.xp_multiplier   = o.value("xp_mult", 1.0f);
        d.loot_table      = o.value("loot", string(""));
        d.kill_target     = o.value("kill_target", d.id);
        d.scale           = o.value("scale", 1.0f);
        d.is_boss         = o.value("boss", false);
        d.element         = ElementFromName(o.value("element", string("none")));

        d.foot_box = BoxFromJson(o.contains("foot_box") ? o["foot_box"] : json(), d.foot_box);
        d.body_box = BoxFromJson(o.contains("body_box") ? o["body_box"] : json(), d.body_box);

        defs[d.id] = d;
    }

    SDL_Log("EnemyDatabase: loaded %d enemy types", static_cast<int>(defs.size()));
    return true;
}

const EnemyDef* EnemyDatabase::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

void Enemy::Init(const EnemyDef* d, const EnemySpawnDef& spawn, const GameContext& ctx) {
    def     = d;
    type_id = spawn.type;
    level   = std::max(1, spawn.level);
    x = home_x = spawn.x;
    y = home_y = spawn.y;
    leash = spawn.leash;
    respawn_delay = spawn.respawn;

    if (def) {
        // Levels scale the stat block, so the same monster can staff an early
        // field and a deep dungeon floor.
        const float s = 1.0f + 0.12f * (level - 1);
        max_hp = std::max(1, static_cast<int>(def->hp * s));
        foot_box = def->foot_box;
        body_box = def->body_box;
        if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(def->sprite));
    }
    hp = max_hp;
    sprite.Play("idle", true);
    SetState(State::Idle);
}

CombatProfile Enemy::Profile() const {
    CombatProfile p;
    if (!def) return p;
    const int bump = level - 1;
    p.attack_level   = def->attack_level + bump;
    p.strength_level = def->strength_level + bump;
    p.defence_level  = def->defence_level + bump;
    p.attack_bonus   = def->attack_bonus;
    p.strength_bonus = def->strength_bonus;
    p.defence_bonus  = def->defence_bonus;
    return p;
}

void Enemy::SetState(State s) {
    if (state == s) return;
    state = s;
    state_timer = 0.0f;
    switch (s) {
        case State::Idle:   sprite.Play("idle"); break;
        case State::Chase:  sprite.Play("walk"); break;
        case State::Attack: sprite.Play("attack", true); break;
        case State::Hurt:   sprite.Play("hurt", true); break;
        case State::Dead:   sprite.Play("death", true); break;
        case State::Return: sprite.Play("walk"); break;
    }
}

void Enemy::TickRespawn(float dt) {
    if (state == State::Dead && respawn_at > 0.0f)
        respawn_at = std::max(0.0f, respawn_at - dt);
}

void Enemy::Revive() {
    hp = max_hp;
    x = home_x;
    y = home_y;
    knock_x = knock_y = 0.0f;
    hurt_flash = 0.0f;
    swinging = false;
    swing_landed = false;
    attack_timer = 0.0f;
    respawn_at = 0.0f;
    remove = false;
    SetState(State::Idle);
    sprite.Play("idle", true);
}

void Enemy::OnKilled(World& world, const GameContext& ctx) {
    if (!def) return;

    // Loot first, so the drop lands where the body fell.
    if (!def->loot_table.empty())
        world.SpawnLoot(def->loot_table, x, y, ctx);

    if (ctx.quests) {
        QuestEvent e;
        e.type   = ObjectiveType::Kill;
        e.target = def->kill_target;
        e.amount = 1;
        ctx.quests->Notify(e, world.player.inventory);
    }
}

void Enemy::Update(float dt, World& world, const GameContext& ctx) {
    if (hurt_flash > 0.0f) hurt_flash = std::max(0.0f, hurt_flash - dt);
    state_timer += dt;
    if (attack_timer > 0.0f) attack_timer -= dt;

    // --- death ----------------------------------------------------------------
    if (hp <= 0 && state != State::Dead) {
        SetState(State::Dead);
        respawn_at = respawn_delay;
        OnKilled(world, ctx);
    }
    if (state == State::Dead) {
        sprite.Update(dt);
        return;
    }

    Player& player = world.player;
    const float dx = player.x - x;
    const float dy = player.y - y;
    const float dist = Length(dx, dy);
    const float home_dist = Length(x - home_x, y - home_y);

    // --- knockback ------------------------------------------------------------
    if (fabsf(knock_x) > 1.0f || fabsf(knock_y) > 1.0f) {
        const SDL_FPoint p = world.map.MoveWithCollision(Bounds(), knock_x * dt, knock_y * dt);
        x = p.x - foot_box.x;
        y = p.y - foot_box.y;
        const float decay = std::max(0.0f, 1.0f - 10.0f * dt);
        knock_x *= decay;
        knock_y *= decay;
    }

    float move_x = 0, move_y = 0;

    switch (state) {
        case State::Hurt:
            if (state_timer >= HURT_STAGGER) SetState(dist < def->aggro_range ? State::Chase : State::Idle);
            break;

        case State::Idle: {
            // Drift around the post so a field of monsters is not a still life.
            wander_timer -= dt;
            if (wander_timer <= 0.0f) {
                wander_timer = 1.6f + (rand() % 100) / 40.0f;
                if (rand() % 3 == 0) {
                    const float angle = (rand() % 628) / 100.0f;
                    wander_dx = cosf(angle);
                    wander_dy = sinf(angle);
                } else {
                    wander_dx = wander_dy = 0.0f;
                }
            }
            if (home_dist > 48.0f) { wander_dx = (home_x - x) / home_dist; wander_dy = (home_y - y) / home_dist; }
            move_x = wander_dx * def->speed * 0.35f;
            move_y = wander_dy * def->speed * 0.35f;

            if (!player.IsDead() && dist < def->aggro_range) SetState(State::Chase);
            break;
        }

        case State::Chase: {
            if (player.IsDead() || home_dist > leash) { SetState(State::Return); break; }
            if (dist > def->aggro_range * 1.6f)        { SetState(State::Return); break; }

            if (dist <= def->attack_range && attack_timer <= 0.0f) {
                SetState(State::Attack);
                swinging = true;
                swing_landed = false;
                swing_timer = 0.0f;
                break;
            }
            if (dist > 1.0f) {
                move_x = (dx / dist) * def->speed;
                move_y = (dy / dist) * def->speed;
            }
            break;
        }

        case State::Attack: {
            swing_timer += dt;
            // The hit lands partway through the swing, not on the first frame,
            // so there is a window to step out of it.
            if (swinging && !swing_landed && swing_timer >= SWING_WINDUP) {
                swing_landed = true;
                const SDL_FRect hit = AttackHitbox(x, y, facing, ProfileFor(AttackType::Strong), 0.9f);
                if (!player.IsDead() && RectsOverlap(hit, player.BodyBox())) {
                    DamageResult r = RollMelee(Profile(), player.Profile(), 1.0f, *ctx.rng);
                    if (r.hit && r.damage <= 0) {
                        world.AddText("0", player.x, player.y - 44.0f, {120, 160, 220, 255});
                    } else if (r.hit) {
                        player.Damage(r.damage);
                        player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
                        const float len = std::max(1.0f, dist);
                        player.knock_x += (dx / len) * 55.0f;
                        player.knock_y += (dy / len) * 55.0f;
                        world.AddText(std::to_string(r.damage), player.x, player.y - 44.0f,
                                      {235, 70, 70, 255});
                        // Taking a hit trains Defence, as it does in OSRS.
                        player.GrantXp(SKILL_DEFENCE, std::max(1, r.damage));
                    } else {
                        world.AddText("miss", player.x, player.y - 44.0f, {150, 150, 168, 235});
                    }
                }
            }
            if (swing_timer >= ProfileFor(AttackType::Strong).Total()) {
                swinging = false;
                attack_timer = def->attack_cooldown;
                SetState(State::Chase);
            }
            break;
        }

        case State::Return: {
            if (home_dist < 8.0f) { SetState(State::Idle); break; }
            move_x = ((home_x - x) / home_dist) * def->speed * 0.8f;
            move_y = ((home_y - y) / home_dist) * def->speed * 0.8f;
            // Re-engage if the player steps back into range on the way home.
            if (!player.IsDead() && dist < def->aggro_range * 0.6f && home_dist < leash)
                SetState(State::Chase);
            break;
        }

        default: break;
    }

    if (move_x != 0.0f || move_y != 0.0f) {
        if (fabsf(move_x) > fabsf(move_y)) facing = (move_x > 0) ? FACE_RIGHT : FACE_LEFT;
        else                               facing = (move_y > 0) ? FACE_DOWN  : FACE_UP;

        const SDL_FPoint p = world.map.MoveWithCollision(Bounds(), move_x * dt, move_y * dt);
        x = p.x - foot_box.x;
        y = p.y - foot_box.y;
    } else if (state == State::Attack || state == State::Chase) {
        // Keep facing the player while swinging.
        if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
        else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    }

    if (state == State::Idle || state == State::Chase || state == State::Return) {
        const bool moving = (fabsf(move_x) + fabsf(move_y)) > 1.0f;
        sprite.Play(moving ? "walk" : "idle");
    }

    sprite.facing = facing;
    sprite.Update(dt);
}

void Enemy::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    // Fade the corpse out over its linger time rather than popping it away.
    SDL_Color tint{255, 255, 255, 255};
    if (state == State::Dead) {
        const float t = std::clamp(respawn_at <= 0.0f ? 1.0f : 0.0f, 0.0f, 1.0f);
        (void)t;
        tint.a = 210;
    }
    if (hurt_flash > 0.0f) tint = {255, 110, 110, tint.a};
    sprite.Draw(r, cache, cam, x, y, tint);
}
