#include "combat.h"

// Light attacks form a three-hit chain: each link is slightly slower but hits
// harder and reaches a little further, so finishing the combo is worth it.
static const AttackProfile kLight[3] = {
    // windup active recover  mult  reach width  knock  move  cooldown
    {  0.05f, 0.10f, 0.10f,   0.72f, 30.0f, 34.0f, 22.0f, 0.40f, 0.05f },
    {  0.05f, 0.10f, 0.11f,   0.82f, 31.0f, 36.0f, 26.0f, 0.40f, 0.05f },
    // The chain ends here, so this one is followed by a real gap: a finisher
    // you can immediately open a new chain from is not a finisher.
    {  0.08f, 0.12f, 0.22f,   1.10f, 34.0f, 40.0f, 48.0f, 0.22f, 0.34f },
};

static const AttackProfile kStrong  = { 0.16f, 0.13f, 0.30f, 1.45f, 36.0f, 44.0f, 52.0f, 0.14f, 0.40f };
static const AttackProfile kCharged = { 0.14f, 0.17f, 0.38f, 1.00f, 46.0f, 58.0f, 105.0f, 0.08f, 0.55f };
static const AttackProfile kNone;

const AttackProfile& ProfileFor(AttackType type, int combo_index) {
    switch (type) {
        case AttackType::Light:   return kLight[std::clamp(combo_index, 0, 2)];
        case AttackType::Strong:  return kStrong;
        case AttackType::Charged: return kCharged;
        default:                  return kNone;
    }
}

AttackProfile ScaleForSpeed(const AttackProfile& p, float speed) {
    // Clamped because this comes from data. A weapon claiming a speed of zero
    // would otherwise swing in no time at all and hit every frame.
    const float s = std::clamp(speed, 0.35f, 3.0f);
    AttackProfile out = p;
    out.windup   *= s;
    out.active   *= s;
    out.recover  *= s;
    out.cooldown *= s;
    return out;
}

float ChargeRatio(float held_time) {
    if (held_time <= CHARGE_HOLD_THRESHOLD) return 0.0f;
    const float t = (held_time - CHARGE_HOLD_THRESHOLD) /
                    (CHARGE_FULL_TIME - CHARGE_HOLD_THRESHOLD);
    return std::clamp(t, 0.0f, 1.0f);
}

float ChargeMultiplier(float ratio) {
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    // Ease in, so the last part of the hold is where the payoff is.
    const float eased = ratio * ratio * (3.0f - 2.0f * ratio);
    return CHARGE_MIN_MULT + (CHARGE_MAX_MULT - CHARGE_MIN_MULT) * eased;
}

// Effective level in OSRS terms: the level plus the flat stance bonus.
static int Effective(int level) { return level + 8; }

// Tuned so a fresh character with the starting sword tops out around 2, and a
// maxed one with the best blade in the game tops out around 20.
static constexpr float MAX_HIT_DIVISOR = 280.0f;

int MaxHit(const CombatProfile& p, float damage_mult) {
    const float effective = static_cast<float>(Effective(p.strength_level));
    const int base = static_cast<int>(
        floorf(0.5f + effective * (p.strength_bonus + 64) / MAX_HIT_DIVISOR));
    return std::max(1, static_cast<int>(base * damage_mult));
}

int MaxHitFor(const CombatProfile& p, AttackStyle style, float damage_mult) {
    int level = p.strength_level, bonus = p.strength_bonus;
    if (style == AttackStyle::Ranged) { level = p.ranged_level; bonus = p.ranged_bonus; }
    else if (style == AttackStyle::Magic) { level = p.magic_level; bonus = p.magic_bonus; }

    const float effective = static_cast<float>(Effective(level));
    const int base = static_cast<int>(
        floorf(0.5f + effective * (bonus + 64) / MAX_HIT_DIVISOR));
    return std::max(1, static_cast<int>(base * damage_mult));
}

float HitChanceFor(const CombatProfile& attacker, const CombatProfile& defender,
                   AttackStyle style) {
    int level = attacker.attack_level, bonus = attacker.attack_bonus;
    if (style == AttackStyle::Ranged) { level = attacker.ranged_level; bonus = attacker.ranged_bonus; }
    else if (style == AttackStyle::Magic) { level = attacker.magic_level; bonus = attacker.magic_bonus; }

    const float att = Effective(level) * (bonus + 64.0f);
    const float def = Effective(defender.defence_level) * (defender.defence_bonus + 64.0f);

    if (att > def) return 1.0f - (def + 2.0f) / (2.0f * (att + 1.0f));
    return att / (2.0f * (def + 1.0f));
}

DamageResult RollAttack(const CombatProfile& attacker, const CombatProfile& defender,
                        AttackStyle style, float damage_mult, std::mt19937& rng) {
    DamageResult r;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) > HitChanceFor(attacker, defender, style)) return r;

    const int max_hit = MaxHitFor(attacker, style, damage_mult);
    std::uniform_int_distribution<int> roll(0, max_hit);
    r.damage  = roll(rng);
    r.hit     = true;
    r.max_hit = (r.damage == max_hit && max_hit > 1);

    // A committed shot or cast that rolls nothing still chips, the same way a
    // charged melee swing does.
    if (r.damage == 0 && damage_mult >= CHARGE_MIN_MULT) r.damage = 1;
    return r;
}

float HitChance(const CombatProfile& attacker, const CombatProfile& defender) {
    const float att = Effective(attacker.attack_level) * (attacker.attack_bonus + 64.0f);
    const float def = Effective(defender.defence_level) * (defender.defence_bonus + 64.0f);

    if (att > def) return 1.0f - (def + 2.0f) / (2.0f * (att + 1.0f));
    return att / (2.0f * (def + 1.0f));
}

DamageResult RollMelee(const CombatProfile& attacker, const CombatProfile& defender,
                       float damage_mult, std::mt19937& rng) {
    DamageResult r;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) > HitChance(attacker, defender)) return r;   // splash

    const int max_hit = MaxHit(attacker, damage_mult);
    std::uniform_int_distribution<int> roll(0, max_hit);
    r.damage  = roll(rng);
    r.hit     = true;
    r.max_hit = (r.damage == max_hit && max_hit > 1);

    // A connecting swing that rolls zero still chips, so committing to a big
    // charged attack never feels like it did literally nothing.
    if (r.damage == 0 && damage_mult >= CHARGE_MIN_MULT) r.damage = 1;
    return r;
}

SDL_FRect AttackHitbox(float x, float y, Facing facing, const AttackProfile& p,
                       float reach_scale) {
    const float reach = p.reach * reach_scale;
    const float width = p.width;

    // The attacker's origin is at the feet; aim the box at torso height.
    const float cy = y - 16.0f;

    switch (facing) {
        case FACE_UP:    return {x - width / 2.0f, cy - reach,        width, reach};
        case FACE_DOWN:  return {x - width / 2.0f, cy,                width, reach};
        case FACE_LEFT:  return {x - reach,        cy - width / 2.0f, reach, width};
        case FACE_RIGHT: return {x,                cy - width / 2.0f, reach, width};
    }
    return {x, cy, width, reach};
}
