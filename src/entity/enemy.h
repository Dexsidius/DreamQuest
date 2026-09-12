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

    // --- health bar -------------------------------------------------------------
    // Hidden until the player first attacks this monster -- a hit, a miss or a
    // hit for nothing all count -- then drawn over its head until the corpse
    // goes. A revived monster starts hidden again.
    void  RevealHealthBar() { bar_revealed = true; }
    bool  HealthBarVisible() const {
        return bar_revealed && !(state == State::Dead && corpse_timer > 0.0f);
    }
    // Exactly hp / max_hp. The bar's fill is this; nothing smooths it.
    float HealthFraction() const {
        return max_hp > 0 ? std::clamp(static_cast<float>(hp) / max_hp, 0.0f, 1.0f) : 0.0f;
    }
    // A lighter band marking damage just taken, never below HealthFraction();
    // it holds for a moment after a hit and then drains down to meet the fill.
    float HealthTrail() const { return std::max(bar_trail, HealthFraction()); }

    // --- corpse -----------------------------------------------------------------
    // After its death animation the body holds briefly, fades, and is gone. The
    // entity stays in the world's list, invisible, to count down its respawn.
    bool  CorpseGone() const;
    Uint8 CorpseAlpha() const;

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

    bool  bar_revealed = false;
    float bar_trail = 1.0f;
    float bar_trail_hold = 0.0f;  // pause before the trail starts draining
    int   last_hp = 0;            // to notice a hit landing between updates
    float corpse_timer = 0.0f;    // time since the death animation finished
};

// Screen pixels of fill for a health bar `inner` pixels wide. Exact to the
// nearest pixel, except that a living monster always shows some red and a
// wounded one never shows a full bar: plain rounding would draw a boar on 1 hp
// of 100 as already dead, and one scratched for 1 of 95 as untouched.
inline int HealthBarFillPixels(int hp, int max_hp, int inner) {
    if (inner <= 0 || max_hp <= 0 || hp <= 0) return 0;
    if (hp >= max_hp) return inner;
    const int fill = static_cast<int>(std::lround(static_cast<double>(inner) * hp / max_hp));
    return std::clamp(fill, 1, std::max(1, inner - 1));
}
