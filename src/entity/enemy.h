#pragma once
#include "entity.h"
#include "../systems/combat.h"
#include "../systems/projectile.h"
#include "../world/map.h"

// Stat block for one kind of monster, from data/enemies.json.
struct EnemyDef {
    string id, name, sprite;
    int   hp = 10;
    int   attack_level = 1, strength_level = 1, defence_level = 1;
    int   attack_bonus = 0, strength_bonus = 0, defence_bonus = 0;
    float speed = 42.0f;
    float aggro_range = 150.0f;
    float attack_range = 26.0f;
    float attack_cooldown = 1.6f;
    float xp_multiplier = 1.0f;
    string loot_table;
    string kill_target;            // what Kill quest objectives match on
    SDL_FRect foot_box{-9.0f, -12.0f, 18.0f, 12.0f};
    SDL_FRect body_box{-14.0f, -42.0f, 28.0f, 42.0f};
    float scale = 1.0f;
    bool  is_boss = false;
    // What the creature is aligned to, for the elemental matchup. Untyped
    // monsters take normal damage from everything.
    Element element = Element::None;
};

class EnemyDatabase {
public:
    bool Load(const string& path);
    const EnemyDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }

private:
    map<string, EnemyDef> defs;
};

// Simple, readable monster AI: sit at your post, notice the player, chase
// within a leash, swing when close enough, and go home when they run away.
class Enemy : public Entity {
public:
    enum class State { Idle, Chase, Attack, Hurt, Dead, Return };

    void Init(const EnemyDef* def, const EnemySpawnDef& spawn, const GameContext& ctx);
    void Update(float dt, World& world, const GameContext& ctx) override;
    void Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const override;

    CombatProfile Profile() const;
    void OnKilled(World& world, const GameContext& ctx);

    // Respawn bookkeeping, run by the world while the body is gone.
    bool  AwaitingRespawn() const { return state == State::Dead && respawn_at > 0.0f; }
    void  TickRespawn(float dt);
    bool  ReadyToRespawn() const { return state == State::Dead && respawn_at <= 0.0f && respawn_delay > 0.0f; }
    void  Revive();

    const EnemyDef* Def() const { return def; }
    Element ElementOf() const { return def ? def->element : Element::None; }
    const string& TypeId() const { return type_id; }
    State CurrentState() const { return state; }
    // Set while the player is engaged, so the HUD can show a target bar.
    bool  Engaged() const { return state == State::Chase || state == State::Attack; }

    int   level = 1;
    float home_x = 0, home_y = 0;

private:
    void SetState(State s);

    const EnemyDef* def = nullptr;
    string type_id;
    State  state = State::Idle;

    float leash = 220.0f;
    float attack_timer = 0.0f;
    float state_timer = 0.0f;
    float respawn_delay = 25.0f;
    float respawn_at = 0.0f;
    float wander_timer = 0.0f;
    float wander_dx = 0, wander_dy = 0;

    bool  swing_landed = false;   // one hit per swing
    float swing_timer = 0.0f;
    bool  swinging = false;
};
