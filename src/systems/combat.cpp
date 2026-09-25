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

// The combos. The Crushing Blow is a strong that comes out quicker and hits
// harder, and what it does beyond that -- the stagger -- is the world's. The
// Cleave is the finisher's finisher: slower, much wider, and it throws. The
// Backhand has almost no wind-up and a light's gap, so the chain goes on from
// it. The Cross Cut is a light's cost in time for a strong's damage all
// round, paid for in stamina; its reach is the radius of the turn.
//   windup active recover  mult  reach width  knock  move  cooldown
static const AttackProfile kCrush    = { 0.10f, 0.12f, 0.26f, 1.60f, 34.0f, 36.0f, 30.0f, 0.10f, 0.32f };
// The Cleave goes from shoulder to shoulder: ninety-five degrees either side,
// so what stands beside the character is in it, and only what is behind is not.
// Its reach is what its old rectangle's half-width was, so it still catches
// what it caught at the character's side.
static const AttackProfile kCleave   = { 0.12f, 0.14f, 0.30f, 1.90f, 46.0f, 88.0f, 95.0f, 0.10f, 0.48f, 95.0f };
static const AttackProfile kBackhand = { 0.03f, 0.10f, 0.12f, 1.00f, 32.0f, 36.0f, 30.0f, 0.40f, 0.05f };
static const AttackProfile kCrossCut = { 0.10f, 0.16f, 0.30f, 1.25f, 40.0f, 40.0f, 80.0f, 0.05f, 0.55f };

const AttackProfile& ProfileForCombo(ComboMove move) {
    switch (move) {
        case ComboMove::Crush:    return kCrush;
        case ComboMove::Cleave:   return kCleave;
        case ComboMove::Backhand: return kBackhand;
        case ComboMove::CrossCut: return kCrossCut;
        default:                  return kNone;
    }
}

const char* ComboName(ComboMove move) {
    switch (move) {
        case ComboMove::Crush:    return "Crushing Blow";
        case ComboMove::Cleave:   return "Cleave";
        case ComboMove::Backhand: return "Backhand";
        case ComboMove::CrossCut: return "Cross Cut";
        default:                  return "";
    }
}

const char* ComboNameFor(ComboMove move, AttackStyle style) {
    if (style == AttackStyle::Ranged) {
        switch (move) {
            case ComboMove::Crush:    return "Split Shot";
            case ComboMove::Cleave:   return "Barbed Shot";
            case ComboMove::Backhand: return "Snap Shot";
            case ComboMove::CrossCut: return "Twin Shot";
            default:                  return "";
        }
    }
    if (style == AttackStyle::Magic) {
        switch (move) {
            case ComboMove::Crush:    return "Surge";
            case ComboMove::Cleave:   return "Cascade";
            case ComboMove::Backhand: return "Flicker";
            case ComboMove::CrossCut: return "Pulse";
            default:                  return "";
        }
    }
    return ComboName(move);
}

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

// Tuned so a fresh character with the starting sword tops out around 2. The
// other end has moved a long way since this was first written: twelve tiers of
// gear later, Strength 99 with the last sword and plate is a base of about
// 148, before any swing's multiplier -- a fully charged blow can pass 450.
// Strength sets this ceiling; Attack decides how often anything lands at all.
static constexpr float MAX_HIT_DIVISOR = 280.0f;

// The unmodified top of the damage range: what the character could hit for
// with a swing that has no multiplier on it at all.
static int BaseMaxHit(int level, int bonus) {
    const float effective = static_cast<float>(Effective(level));
    return std::max(1, static_cast<int>(
        floorf(0.5f + effective * (bonus + 64) / MAX_HIT_DIVISOR)));
}

// A swing's multiplier scales the damage that was rolled, not the size of the
// die it was rolled on. Scaling the die and truncating it to an int made the
// opening links of the light chain (x0.72 and x0.82) collapse a level 1
// character's range of 0-2 to 0-1 -- so half of every connecting hit did
// nothing, and the first two thirds of every combo were strictly worse than
// they read. Above about level 20 the two orderings agree; below it, this one
// is the one that matches what the numbers promise.
static int Scaled(int rolled, float damage_mult) {
    return std::max(1, static_cast<int>(lroundf(rolled * damage_mult)));
}

int MaxHit(const CombatProfile& p, float damage_mult) {
    return Scaled(BaseMaxHit(p.strength_level, p.strength_bonus), damage_mult);
}

int MaxHitFor(const CombatProfile& p, AttackStyle style, float damage_mult) {
    int level = p.strength_level, bonus = p.strength_bonus;
    if (style == AttackStyle::Ranged) { level = p.ranged_level; bonus = p.ranged_bonus; }
    else if (style == AttackStyle::Magic) { level = p.magic_level; bonus = p.magic_bonus; }
    return Scaled(BaseMaxHit(level, bonus), damage_mult);
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
                        AttackStyle style, float damage_mult, std::mt19937& rng,
                        bool floor_damage) {
    DamageResult r;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) > HitChanceFor(attacker, defender, style)) return r;

    // For the player the die starts at 1 rather than 0: a connecting shot that
    // deals nothing reads as the game ignoring you, and early on it was most
    // of them -- at level 1 a 0-2 range meant a third of everything that got
    // through did no damage, on top of the accuracy roll that had already
    // eaten half the swings. The floor matters least where the numbers are
    // biggest: half a point of average damage at level 40, double it at 1.
    int level = attacker.strength_level, bonus = attacker.strength_bonus;
    if (style == AttackStyle::Ranged) { level = attacker.ranged_level; bonus = attacker.ranged_bonus; }
    else if (style == AttackStyle::Magic) { level = attacker.magic_level; bonus = attacker.magic_bonus; }

    const int base = BaseMaxHit(level, bonus);
    std::uniform_int_distribution<int> roll(floor_damage ? 1 : 0, base);
    r.damage  = roll(rng);
    r.hit     = true;
    // The swing's multiplier scales what came up rather than the die it came
    // up on, so a rolled zero stays zero.
    if (r.damage > 0) r.damage = Scaled(r.damage, damage_mult);
    else if (damage_mult >= CHARGE_MIN_MULT) r.damage = 1;   // a committed shot still chips
    r.max_hit = (base > 1 && r.damage >= Scaled(base, damage_mult));
    return r;
}

float HitChance(const CombatProfile& attacker, const CombatProfile& defender) {
    const float att = Effective(attacker.attack_level) * (attacker.attack_bonus + 64.0f);
    const float def = Effective(defender.defence_level) * (defender.defence_bonus + 64.0f);

    if (att > def) return 1.0f - (def + 2.0f) / (2.0f * (att + 1.0f));
    return att / (2.0f * (def + 1.0f));
}

// --- a monster's blow on the player -----------------------------------------------------

static int StyleMax(const CombatProfile& p, AttackStyle style) {
    int level = p.strength_level, bonus = p.strength_bonus;
    if (style == AttackStyle::Ranged) { level = p.ranged_level; bonus = p.ranged_bonus; }
    else if (style == AttackStyle::Magic) { level = p.magic_level; bonus = p.magic_bonus; }
    return BaseMaxHit(level, bonus);
}

float MonsterThrough(const CombatProfile& monster, const CombatProfile& player, AttackStyle style) {
    return std::clamp(MONSTER_THROUGH * HitChanceFor(monster, player, style), 0.0f, 1.0f);
}

int MonsterMinimum(const CombatProfile& monster, AttackStyle style, float damage_mult) {
    const int top = Scaled(StyleMax(monster, style), damage_mult);
    return std::max(1, static_cast<int>(std::lround(top * MONSTER_MIN_SHARE)));
}

// One face of the die: what that roll does once armour has taken its share.
static int BlowFor(int face, float damage_mult, float through, int least) {
    return std::max(least, static_cast<int>(std::lround(Scaled(face, damage_mult) * through)));
}

DamageResult RollMonsterBlow(const CombatProfile& monster, const CombatProfile& player,
                             AttackStyle style, float damage_mult, std::mt19937& rng) {
    DamageResult r;
    const int top = StyleMax(monster, style);
    std::uniform_int_distribution<int> roll((top + 1) / 2, top);
    const int face = roll(rng);
    r.hit     = true;
    r.damage  = BlowFor(face, damage_mult, MonsterThrough(monster, player, style),
                        MonsterMinimum(monster, style, damage_mult));
    r.max_hit = (top > 1 && face >= top);
    return r;
}

float ExpectedMonsterBlow(const CombatProfile& monster, const CombatProfile& player,
                          AttackStyle style, float damage_mult) {
    const int top = StyleMax(monster, style);
    const float through = MonsterThrough(monster, player, style);
    const int least = MonsterMinimum(monster, style, damage_mult);
    float sum = 0.0f;
    int n = 0;
    for (int face = (top + 1) / 2; face <= top; ++face, ++n) sum += static_cast<float>(BlowFor(face, damage_mult, through, least));
    return n > 0 ? sum / n : 0.0f;
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

StrikeArc ArcFor(float x, float y, Facing facing, const AttackProfile& p, float reach_scale) {
    StrikeArc arc;
    arc.x = x;
    arc.y = y;
    arc.dir_x = facing == FACE_LEFT ? -1.0f : facing == FACE_RIGHT ? 1.0f : 0.0f;
    arc.dir_y = facing == FACE_UP   ? -1.0f : facing == FACE_DOWN  ? 1.0f : 0.0f;
    arc.reach = std::max(1.0f, p.reach * reach_scale);
    arc.half_angle = p.HalfAngle(arc.reach);
    return arc;
}

bool ArcHits(const StrikeArc& arc, float tx, float ty, float radius) {
    const float dx = tx - arc.x, dy = ty - arc.y;
    const float dist = Length(dx, dy);
    if (dist > arc.reach + radius) return false;
    // Standing on the attacker's feet is inside any swing.
    if (arc.all_round || dist <= radius + 4.0f) return true;
    const float along = std::clamp((dx * arc.dir_x + dy * arc.dir_y) / dist, -1.0f, 1.0f);
    // Someone wide is caught by an arc that only reaches their shoulder.
    const float allowance = asinf(std::clamp(radius / dist, 0.0f, 1.0f));
    return acosf(along) <= arc.half_angle + allowance;
}

// --- armour against a heavy blow ---------------------------------------------------

float HeavySoak(int defence_level, int defence_bonus) {
    const float armour = static_cast<float>(std::max(0, defence_level) + std::max(0, defence_bonus));
    return std::min(HEAVY_SOAK_CAP, armour / (armour + HEAVY_SOAK_SCALE));
}

int SoakHeavy(int damage, int defence_level, int defence_bonus) {
    if (damage <= 0) return 0;
    const float kept = 1.0f - HeavySoak(defence_level, defence_bonus);
    return std::max(1, static_cast<int>(std::lround(damage * kept)));
}

// --- blocking ----------------------------------------------------------------

float BlockCost(int damage, int attacker_level, float stamina_mult) {
    return static_cast<float>(std::max(0, damage)) * sqrtf(static_cast<float>(std::max(1, attacker_level))) *
           BLOCK_COST_SCALE * std::max(0.0f, stamina_mult);
}

BlockOutcome ResolveBlock(int damage, int attacker_level, float mitigation,
                          float stamina_mult, float stamina_available) {
    BlockOutcome out;
    out.taken = std::max(0, damage);
    if (damage <= 0 || mitigation <= 0.0f) return out;

    const float cost = BlockCost(damage, attacker_level, stamina_mult);
    const float have = std::max(0.0f, stamina_available);
    // The share of the block that could be paid for. All of it, or as much as
    // was left in the bar when the bar ran out.
    float share = 1.0f;
    if (cost > have) {
        share = cost > 0.0f ? have / cost : 1.0f;
        out.broke = true;
    }
    out.stamina = std::min(cost, have);
    out.blocked = std::clamp(static_cast<int>(std::lround(damage * std::min(1.0f, mitigation) * share)),
                             0, damage);
    out.taken = damage - out.blocked;
    return out;
}

int CombatLevelOf(const CombatProfile& p) {
    return std::max({p.attack_level, p.strength_level, p.defence_level,
                     p.ranged_level, p.magic_level, 1});
}

bool InFrontOf(Facing facing, float dx, float dy) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return true;             // on top of you is in front of you
    const float fx = facing == FACE_LEFT ? -1.0f : facing == FACE_RIGHT ? 1.0f : 0.0f;
    const float fy = facing == FACE_UP   ? -1.0f : facing == FACE_DOWN  ? 1.0f : 0.0f;
    // cos(100 degrees): a hundred degrees either side of straight ahead.
    return (fx * dx + fy * dy) / len > -0.18f;
}
