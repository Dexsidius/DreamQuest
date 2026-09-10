#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  Combat.
//
//  Three melee attacks share one state machine:
//
//    Light   - tap the light button. Fast, cheap, and chains into a three hit
//              combo where each link adds a little damage.
//    Strong  - tap the strong button. Slower, hits considerably harder.
//    Charged - hold the strong button. Past the hold threshold the swing
//              starts charging; releasing it fires a scaled-up strong attack
//              whose damage, reach and knockback all grow with charge time.
//
//  Damage resolution follows the Old School RuneScape shape: an accuracy roll
//  of attack against defence decides whether the hit lands, and a separate max
//  hit derived from Strength decides how hard.
// -----------------------------------------------------------------------------

enum class AttackType { None, Light, Strong, Charged };

// Timing and geometry for one swing, in seconds and world pixels.
struct AttackProfile {
    float windup   = 0.10f;   // before the hitbox exists
    float active   = 0.10f;   // hitbox is live
    float recover  = 0.18f;   // committed, cannot act
    float damage_mult = 1.0f;
    float reach    = 26.0f;
    float width    = 30.0f;
    float knockback = 40.0f;
    float move_scale = 0.25f; // how much the attacker can still walk

    float Total() const { return windup + active + recover; }
};

const AttackProfile& ProfileFor(AttackType type, int combo_index = 0);

// Charge tuning, shared by the player HUD so the meter matches the maths.
static constexpr float CHARGE_HOLD_THRESHOLD = 0.22f;  // hold before charging starts
static constexpr float CHARGE_FULL_TIME      = 1.20f;  // hold for maximum power
static constexpr float CHARGE_MIN_MULT       = 1.55f;
static constexpr float CHARGE_MAX_MULT       = 3.10f;

// 0..1 charge progress from how long the button has been held.
float ChargeRatio(float held_time);
// Damage multiplier for a charged release at that ratio.
float ChargeMultiplier(float ratio);

// The numbers a combatant brings to a swing, a shot or a cast.
struct CombatProfile {
    int attack_level = 1, strength_level = 1, defence_level = 1;
    int attack_bonus = 0, strength_bonus = 0, defence_bonus = 0;
    int ranged_level = 1, magic_level = 1;
    int ranged_bonus = 0, magic_bonus = 0;
};

// Which of the three styles an attack is resolved as. Ranged and magic use
// their own level and bonus for both accuracy and damage, the way OSRS does,
// so a bow does nothing for a character who never trained Ranged.
enum class AttackStyle { Melee, Ranged, Magic };

struct DamageResult {
    bool hit = false;
    int  damage = 0;
    bool max_hit = false;     // rolled the top of the range, worth flashing
};

// Accuracy roll then damage roll.
DamageResult RollMelee(const CombatProfile& attacker, const CombatProfile& defender,
                       float damage_mult, std::mt19937& rng);

// Rolls an attack of any style. Melee is identical to RollMelee.
DamageResult RollAttack(const CombatProfile& attacker, const CombatProfile& defender,
                        AttackStyle style, float damage_mult, std::mt19937& rng);

int MaxHit(const CombatProfile& p, float damage_mult);
float HitChance(const CombatProfile& attacker, const CombatProfile& defender);

// Style-aware versions. Melee defers to the two above.
int   MaxHitFor(const CombatProfile& p, AttackStyle style, float damage_mult);
float HitChanceFor(const CombatProfile& attacker, const CombatProfile& defender,
                   AttackStyle style);

// The rectangle a swing sweeps, in front of the attacker.
SDL_FRect AttackHitbox(float x, float y, Facing facing, const AttackProfile& p,
                       float reach_scale = 1.0f);

// One in-flight swing, owned by whoever is attacking.
struct AttackState {
    AttackType type = AttackType::None;
    float      timer = 0.0f;
    float      damage_mult = 1.0f;
    float      reach_scale = 1.0f;
    int        combo = 0;
    bool       consumed = false;   // hitbox already applied this swing
    AttackProfile profile;

    bool Active() const { return type != AttackType::None; }
    // True only during the frames where the hitbox should be tested.
    bool InActiveWindow() const {
        return Active() && timer >= profile.windup &&
               timer < profile.windup + profile.active;
    }
    bool Finished() const { return Active() && timer >= profile.Total(); }
    void Clear() { type = AttackType::None; timer = 0.0f; consumed = false; }
};
