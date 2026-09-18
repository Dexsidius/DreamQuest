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

// The combos. What the last swing was decides what the next press means:
//
//   Light, Heavy          Crushing Blow  an overhead that leaves what it hits
//                                        reeling for a moment
//   Light, Light, Heavy   Cleave         a wide level sweep that ends the chain
//                                        and throws everything in it back
//   Heavy, Light          Backhand       an instant cut off the heavy's
//                                        follow-through, standing in for the
//                                        first two links of a chain
//   Light + Heavy at once Cross Cut      a turn on the spot striking everything
//                                        round the player, for stamina
//
// Each is a swing of its own -- its own shape, timing and gap -- and rides the
// same state machine as the rest, as a Light or a Strong with a move on it.
enum class ComboMove { None, Crush, Cleave, Backhand, CrossCut };
const char* ComboName(ComboMove move);

// Seconds the Crushing Blow leaves a monster reeling.
static constexpr float CRUSH_STAGGER     = 1.0f;
// What the Cross Cut costs, out of a hundred.
static constexpr float CROSS_CUT_STAMINA = 25.0f;
// The two buttons within this of each other are "together".
static constexpr float TOGETHER_WINDOW   = 0.08f;
// A press during a swing is kept this long, for the moment a swing may start.
static constexpr float BUFFER_WINDOW     = 0.25f;

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
    // Dead time after the swing has fully played out, before another can
    // start. Recovery is part of the swing and you are still committed during
    // it; this is the gap after it, and it is what stops the attack button
    // being a thing you hold down. Mid-combo links have almost none, which is
    // what makes continuing a chain quicker than starting a new one.
    float cooldown = 0.14f;

    float Total() const { return windup + active + recover; }
};

const AttackProfile& ProfileFor(AttackType type, int combo_index = 0);
const AttackProfile& ProfileForCombo(ComboMove move);

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
//
// `floor_damage` starts the damage die at 1 instead of 0, so a swing that beat
// the accuracy roll always does something. It is passed for the player's
// attacks and not for the monsters': a hit of your own that lands for nothing
// reads as the game ignoring you, where a monster rolling low is just a quiet
// moment. Making it symmetric raised every monster's average damage by half
// at low levels, which is the opposite of what it is for.
DamageResult RollMelee(const CombatProfile& attacker, const CombatProfile& defender,
                       float damage_mult, std::mt19937& rng, bool floor_damage = false);

// Rolls an attack of any style. Melee is identical to RollMelee.
DamageResult RollAttack(const CombatProfile& attacker, const CombatProfile& defender,
                        AttackStyle style, float damage_mult, std::mt19937& rng,
                        bool floor_damage = false);

int MaxHit(const CombatProfile& p, float damage_mult);
float HitChance(const CombatProfile& attacker, const CombatProfile& defender);

// Style-aware versions. Melee defers to the two above.
int   MaxHitFor(const CombatProfile& p, AttackStyle style, float damage_mult);
float HitChanceFor(const CombatProfile& attacker, const CombatProfile& defender,
                   AttackStyle style);

// --- blocking ----------------------------------------------------------------
//
// Holding a shield up in front of a blow. What is stopped depends on the
// shield: each tier turns aside a larger share of a hit. What it costs is
// stamina, and the cost is the blow itself:
//
//     stamina = incoming damage x the attacker's combat level x the shield's
//               stamina multiplier
//
// so a rat's nip is nothing to catch and a dragon's bite empties the bar, and a
// better shield makes the same blow cheaper to take. When there is not enough
// stamina left to pay for it, the block holds for the share that was paid and
// the guard breaks.
struct BlockOutcome {
    int   taken   = 0;      // what still gets through
    int   blocked = 0;      // what the shield stopped
    float stamina = 0.0f;   // what it cost
    bool  broke   = false;  // ran out of stamina paying for it
};

BlockOutcome ResolveBlock(int damage, int attacker_level, float mitigation,
                          float stamina_mult, float stamina_available);

// A combatant's level for the purposes of blocking: the highest of its combat
// levels, which is the "effective level" the monster table in the README gives.
int CombatLevelOf(const CombatProfile& p);

// Whether a blow coming from (dx, dy) relative to the defender is in front of
// them. A little over a half circle, so a monster standing at a diagonal to a
// four-way facing is still in front of the shield rather than beside it.
bool InFrontOf(Facing facing, float dx, float dy);

// The rectangle a swing sweeps, in front of the attacker.
SDL_FRect AttackHitbox(float x, float y, Facing facing, const AttackProfile& p,
                       float reach_scale = 1.0f);

// Applies a weapon's attack speed to a swing's timings. A speed below one is a
// faster weapon: every phase, and the cooldown after it, is shortened by the
// same factor, so a dagger's whole rhythm scales rather than just the part of
// it you can see. Reach, width and knockback are deliberately untouched --
// they are properties of the weapon's shape, not of how quickly it moves.
AttackProfile ScaleForSpeed(const AttackProfile& p, float speed);

// One in-flight swing, owned by whoever is attacking.
struct AttackState {
    AttackType type = AttackType::None;
    float      timer = 0.0f;
    // What the weapon did to the timings, kept so the animation can be played
    // at a matching rate and so the HUD can say how fast this weapon is.
    float      rate = 1.0f;
    float      damage_mult = 1.0f;
    float      reach_scale = 1.0f;
    int        combo = 0;
    // Which combo this swing is, or None for a plain attack.
    ComboMove  move = ComboMove::None;
    bool       consumed = false;   // hitbox already applied this swing
    AttackProfile profile;

    bool Active() const { return type != AttackType::None; }
    // True only during the frames where the hitbox should be tested.
    bool InActiveWindow() const {
        return Active() && timer >= profile.windup &&
               timer < profile.windup + profile.active;
    }
    bool Finished() const { return Active() && timer >= profile.Total(); }
    void Clear() { type = AttackType::None; move = ComboMove::None; timer = 0.0f; consumed = false; }
};
