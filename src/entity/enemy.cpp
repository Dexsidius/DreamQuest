#include "enemy.h"
#include "../systems/audio.h"
#include "../world/world.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include <fstream>

static constexpr float HURT_STAGGER = 0.22f;
static constexpr float DEATH_LINGER = 1.1f;   // longest wait for a death clip to finish
static constexpr float CORPSE_HOLD  = 0.35f;  // body lies still after the clip
static constexpr float CORPSE_FADE  = 0.6f;   // then fades out over this
static constexpr float TRAIL_HOLD   = 0.3f;   // damage band holds after a hit
static constexpr float TRAIL_DRAIN  = 1.2f;   // then drains, in bar-widths a second
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
        d.shoots          = o.value("shoots", string(""));
        d.shoot_range     = o.value("shoot_range", 0.0f);
        d.shoot_cooldown  = o.value("shoot_cooldown", 2.4f);
        d.attack_cooldown = o.value("attack_cooldown", 1.6f);
        d.xp_multiplier   = o.value("xp_mult", 1.0f);
        d.loot_table      = o.value("loot", string(""));
        d.kill_target     = o.value("kill_target", d.id);
        d.scale           = o.value("scale", 1.0f);
        d.is_boss         = o.value("boss", false);
        d.swims           = o.value("swims", false);
        d.paddles         = o.value("paddles", d.swims);
        if (o.contains("immune") && o["immune"].is_array())
            for (const auto& v : o["immune"])
                if (v.is_string() && StatusFromId(v.get<string>()) != Status::COUNT)
                    d.immune[static_cast<int>(StatusFromId(v.get<string>()))] = true;
        d.element         = ElementFromName(o.value("element", string("none")));
        if (o.contains("tint") && o["tint"].is_array() && o["tint"].size() >= 3)
            d.tint = {static_cast<Uint8>(o["tint"][0].get<int>()),
                      static_cast<Uint8>(o["tint"][1].get<int>()),
                      static_cast<Uint8>(o["tint"][2].get<int>()), 255};

        if (o.contains("heavy") && o["heavy"].is_object()) {
            const json& h = o["heavy"];
            d.heavy.enabled   = true;
            d.heavy.windup    = std::max(0.3f, h.value("windup", d.heavy.windup));
            d.heavy.damage    = h.value("damage", d.heavy.damage);
            d.heavy.reach     = h.value("reach", d.heavy.reach);
            d.heavy.width     = h.value("width", d.heavy.width);
            d.heavy.cooldown  = h.value("cooldown", d.heavy.cooldown);
            d.heavy.opening   = h.value("opening", d.heavy.opening);
            d.heavy.knockback = h.value("knockback", d.heavy.knockback);
        }

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

// The same shape as the player's Combat level (Skills::CombatLevel): a quarter
// of defence and hit points, plus a third of the two attacking stats. A monster
// has no skill levels, so its stat block stands in for them -- its hit points
// are a pool rather than a level, so they are read as the level a player with
// that many would have, which is what makes a 320-hitpoint bear read as the
// forty-odd-level thing it is.
int Enemy::ShownLevelOf(const EnemyDef& def, int spawn_level) {
    const int bump = std::max(0, spawn_level - 1);
    const float hp = def.hp * (1.0f + 0.12f * bump);
    // Hitpoints as a level: the player's curve gives about 10 + level*4 at the
    // low end and flattens after that, and this is the same shape read backwards.
    const float hp_level = std::clamp(sqrtf(std::max(0.0f, hp - 6.0f)) * 3.4f, 1.0f, 99.0f);
    const float att = static_cast<float>(def.attack_level + bump);
    const float str = static_cast<float>(def.strength_level + bump);
    const float dfn = static_cast<float>(def.defence_level + bump);
    const float base = 0.25f * (dfn + hp_level);
    const float melee = 0.325f * (att + str);
    return std::clamp(static_cast<int>(floorf(base + melee)), 1, 99);
}

int Enemy::ShownLevel() const { return def ? ShownLevelOf(*def, level) : level; }

void Enemy::Init(const EnemyDef* d, const EnemySpawnDef& spawn, const GameContext& ctx) {
    def     = d;
    status_db = ctx.statuses;
    type_id = spawn.type;
    level   = std::max(1, spawn.level);
    x = home_x = spawn.x;
    y = home_y = spawn.y;
    leash = spawn.leash;
    respawn_delay = spawn.respawn;
    night        = spawn.night;
    night_chance = spawn.chance;
    night_group  = spawn.group;
    lurks        = spawn.lurk;
    emerge       = lurks ? 0.0f : 1.0f;
    rising       = false;
    sink_wait    = 0.0f;

    if (def) {
        // Levels scale the stat block, so the same monster can staff an early
        // field and a deep dungeon floor.
        const float s = 1.0f + 0.12f * (level - 1);
        max_hp = std::max(1, static_cast<int>(def->hp * s));
        foot_box = def->foot_box;
        body_box = def->body_box;
        if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(def->sprite));
        sprite.size_scale = def->scale;
    }
    hp = max_hp;
    last_hp = hp;
    bar_trail = 1.0f;
    bar_revealed = false;
    corpse_timer = 0.0f;
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
    if (sundered > 0.0f) {
        p.defence_level = static_cast<int>(p.defence_level * SUNDER_SHARE);
        p.defence_bonus = static_cast<int>(p.defence_bonus * SUNDER_SHARE);
    }
    // What is on it: concussed, it swings wide and guards badly; poisoned, its
    // hide gives way. Each is a share of what it has, and they multiply.
    if (status_db)
        for (int i = 0; i < STATUS_COUNT; ++i) {
            const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
            if (!d) continue;
            p.attack_level  = std::max(1, static_cast<int>(p.attack_level * d->attack));
            p.attack_bonus  = static_cast<int>(p.attack_bonus * d->attack);
            p.defence_level = std::max(1, static_cast<int>(p.defence_level * d->defence));
            p.defence_bonus = static_cast<int>(p.defence_bonus * d->defence);
        }
    return p;
}

// -----------------------------------------------------------------------------
//  Statuses
// -----------------------------------------------------------------------------

bool Enemy::ImmuneTo(Status s) const {
    if (!def || s == Status::COUNT) return true;
    if (def->immune[static_cast<int>(s)]) return true;
    // Nothing made of fire can be set burning.
    return s == Status::Burn && def->element == Element::Fire;
}

Status Enemy::Afflict(Status kind, int blow, const StatusDatabase& db) {
    if (state == State::Dead || hp <= 0 || kind == Status::COUNT) return Status::COUNT;
    status_db = &db;
    const StatusDef* d = db.Get(kind);
    if (!d || ImmuneTo(kind)) return Status::COUNT;
    const auto lasts = [&](const StatusDef& of) { return of.seconds * (def->is_boss ? of.boss_share : 1.0f); };

    // A chill on something soaked is frozen -- where it can be: the great ones
    // are never held, and are chilled like anything else.
    if (d->becomes != Status::COUNT && d->if_has != Status::COUNT && statuses.Has(d->if_has)) {
        const StatusDef* other = db.Get(d->becomes);
        if (other && !ImmuneTo(d->becomes) && lasts(*other) > 0.0f) { kind = d->becomes; d = other; }
    }
    for (Status stops : d->blocked_by) if (statuses.Has(stops)) return Status::COUNT;
    const float seconds = lasts(*d);
    if (seconds <= 0.0f) return Status::COUNT;

    for (Status over : d->ends) statuses.End(over);

    const int i = static_cast<int>(kind);
    if (d->dot_share > 0.0f) {
        const float fresh = std::max(static_cast<float>(d->dot_min), static_cast<float>(blow) * d->dot_share);
        const float owed = statuses.left[i] * statuses.rate[i];
        const float total = d->stacks ? owed + fresh : std::max(owed, fresh);
        statuses.rate[i] = total / seconds;
    }
    statuses.left[i] = std::max(statuses.left[i], seconds);
    if (d->holds)              Stagger(seconds);
    else if (d->stagger > 0.0f) Stagger(d->stagger);
    return kind;
}

float Enemy::StatusWeakness(Element e) const {
    float mult = 1.0f;
    if (!status_db || e == Element::None) return mult;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
        if (d && std::find(d->weak_to.begin(), d->weak_to.end(), e) != d->weak_to.end()) mult *= d->weak_mult;
    }
    return mult;
}

float Enemy::StatusInvites(Status s) const {
    float mult = 1.0f;
    if (!status_db || s == Status::COUNT) return mult;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
        if (d && std::find(d->invites.begin(), d->invites.end(), s) != d->invites.end()) mult *= d->invite_mult;
    }
    return mult;
}

float Enemy::MoveSpeed() const {
    float speed = def ? def->speed : 0.0f;
    if (status_db)
        for (int i = 0; i < STATUS_COUNT; ++i)
            if (const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr)
                speed *= d->speed;
    return speed;
}

float Enemy::AttackCooldown() const {
    float gap = def ? def->attack_cooldown : 1.6f;
    if (status_db)
        for (int i = 0; i < STATUS_COUNT; ++i)
            if (const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr)
                gap *= d->cooldown;
    return gap;
}

void Enemy::Pose(const Posed& p) {
    x = p.x;
    y = p.y;
    facing = static_cast<Facing>(std::min<uint8_t>(p.facing, 3));
    sprite.facing = facing;
    hp = std::clamp(p.hp, 0, max_hp);
    // Which statuses are on it, for the drawing: a puppet's run down nowhere,
    // so each is simply on or off as the host last said.
    for (int i = 0; i < STATUS_COUNT; ++i) statuses.left[i] = (p.statuses >> i) & 1 ? 1.0f : 0.0f;
    const State was = state;
    state = static_cast<State>(std::min<uint8_t>(p.state, static_cast<uint8_t>(State::Heavy)));
    if (state != was) state_timer = 0.0f;
    heavy_landed = false;
    if (state == State::Heavy && def) state_timer = (p.heavy / 255.0f) * def->heavy.windup;
    if (state == State::Dead) corpse_timer = CORPSE_HOLD + (1.0f - p.alpha / 255.0f) * CORPSE_FADE;
    else corpse_timer = 0.0f;
    if (lurks && state != State::Dead) emerge = p.alpha / 255.0f;
    hurt_flash = p.hurt ? std::max(hurt_flash, 0.08f) : 0.0f;
    bar_revealed = p.bar;
    bar_trail = std::max(HealthFraction(), bar_trail - 0.02f);
    sprite.Play(p.clip.empty() ? string("idle") : p.clip);
    sprite.SetFrame(p.frame);
}

Enemy::Posed Enemy::Told() const {
    Posed p;
    p.x = x; p.y = y;
    p.facing = static_cast<uint8_t>(facing);
    p.state = static_cast<uint8_t>(state);
    p.frame = static_cast<uint8_t>(std::clamp(sprite.Frame(), 0, 255));
    p.heavy = static_cast<uint8_t>(std::lround(HeavyCharge() * 255.0f));
    // A lurker's alpha is how far out of the water it is.
    p.alpha = (lurks && state != State::Dead) ? static_cast<uint8_t>(std::lround(emerge * 255.0f)) : CorpseAlpha();
    p.hurt = hurt_flash > 0.0f;
    p.bar = bar_revealed;
    p.hp = hp;
    p.clip = sprite.current;
    p.statuses = statuses.Bits();
    return p;
}

float Enemy::HeavyCharge() const {
    if (state != State::Heavy || !def || heavy_landed) return 0.0f;
    return std::clamp(state_timer / std::max(0.001f, def->heavy.windup), 0.0f, 1.0f);
}

SDL_FRect Enemy::HeavyHitbox() const {
    AttackProfile p = ProfileFor(AttackType::Strong);
    if (def) {
        p.reach = std::max(40.0f, def->attack_range * def->heavy.reach);
        p.width = std::max(44.0f, body_box.w * def->heavy.width);
    }
    return AttackHitbox(x, y, facing, p, 1.0f);
}

StrikeArc Enemy::SwingArc() const {
    AttackProfile p = ProfileFor(AttackType::Strong);
    // As far as the range it swings from, less the width of whoever it is
    // swinging at, which ArcHits gives back; and as wide as it is itself.
    p.reach = std::max(p.reach * 0.9f, def ? def->attack_range - 6.0f : 0.0f);
    p.width = std::max(p.width, body_box.w * 0.9f);
    const SDL_FPoint g = GroundCentre();
    return ArcFor(g.x, g.y, facing, p, 1.0f);
}

StrikeArc Enemy::HeavyArc() const {
    AttackProfile p = ProfileFor(AttackType::Strong);
    if (def) {
        p.reach = std::max(40.0f, def->attack_range * def->heavy.reach);
        p.width = std::max(44.0f, body_box.w * def->heavy.width);
    }
    const SDL_FPoint g = GroundCentre();
    return ArcFor(g.x, g.y, facing, p, 1.0f);
}

int Enemy::HeavyDamage(std::mt19937* rng) const {
    if (!def) return 1;
    // Near the top of the monster's range every time: after a wind-up that
    // long, a blow that rolls low reads as the game letting you off.
    std::uniform_real_distribution<float> roll(0.8f, 1.0f);
    const float r = rng ? roll(*rng) : 0.9f;
    return std::max(1, static_cast<int>(std::lround(MaxHit(Profile(), 1.0f) * def->heavy.damage * r)));
}

void Enemy::SetState(State s) {
    if (state == s) return;
    // The first heavy of a fight waits for its opening, so a leader walks in
    // swinging like anything else before it starts to wind up.
    if (s == State::Chase && (state == State::Idle || state == State::Return) && def && def->heavy.enabled)
        heavy_timer = std::max(heavy_timer, def->heavy.opening);
    state = s;
    state_timer = 0.0f;
    switch (s) {
        case State::Idle:   sprite.Play("idle"); break;
        case State::Chase:  sprite.Play("walk"); break;
        case State::Heavy:  heavy_landed = false; sprite.Play("idle", true); break;
        case State::Attack: sprite.Play("attack", true); break;
        case State::Hurt:   sprite.Play("hurt", true); break;
        case State::Dead:   sprite.Play("death", true); break;
        case State::Return: sprite.Play("walk"); break;
    }
}

void Enemy::Stagger(float seconds) {
    if (state == State::Dead || state == State::Heavy) return;
    hurt_for = std::max(hurt_for, seconds);
    swinging = false;
    swing_landed = false;
    SetState(State::Hurt);
    // A second blow starts the reel again rather than adding to it.
    state_timer = 0.0f;
}

void Enemy::Provoke(int seat) {
    if (state == State::Dead) return;
    provoked = true;
    chase_run = 0.0f;              // a blow taken is a fight: the count starts again
    if (seat >= 0) { grudge_seat = seat; grudge = GRUDGE_TIME; }
    // Reeling, swinging or winding up, it carries on with that and comes after.
    if (state == State::Idle || state == State::Return) SetState(State::Chase);
}

void Enemy::Bleed(float damage) {
    if (damage <= 0.0f || state == State::Dead || ImmuneTo(Status::Bleed)) return;
    const int i = static_cast<int>(Status::Bleed);
    const float owed = statuses.rate[i] * statuses.left[i] + damage;
    statuses.left[i] = BLEED_TIME;
    statuses.rate[i] = owed / BLEED_TIME;
}

void Enemy::TickRespawn(float dt) {
    if (state == State::Dead && respawn_at > 0.0f)
        respawn_at = std::max(0.0f, respawn_at - dt);
}

void Enemy::Revive() {
    statuses.Clear();
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
    // Back under, if that is where it lives.
    emerge = lurks ? 0.0f : 1.0f;
    rising = false;
    sink_wait = 0.0f;
    // A fresh monster: no bar until it is attacked again, and a body to draw.
    last_hp = hp;
    bar_trail = 1.0f;
    bar_trail_hold = 0.0f;
    bar_revealed = false;
    corpse_timer = 0.0f;
    provoked = false;
    chase_run = 0.0f;
    grudge = 0.0f;
    grudge_seat = -1;
    SetState(State::Idle);
    sprite.Play("idle", true);
}

void Enemy::OnKilled(World& world, const GameContext& ctx) {
    // A boss is killed once a day, and so is what came out in the night: both
    // are remembered by where they stood.
    if ((def && def->is_boss) || night) world.NoteSlain(post);
    if (!def) return;

    // Loot first, so the drop lands where the body fell.
    if (!def->loot_table.empty())
        world.SpawnLoot(def->loot_table, x, y, ctx);

    {
        QuestEvent e;
        e.type   = ObjectiveType::Kill;
        e.target = def->kill_target;
        e.amount = 1;
        e.map_id = world.MapId();
        // Which boss, if it was one. A chief's kill target is "lizardman", for
        // the contracts' sake, so the target cannot say; and this is what goes
        // down the wire to a friend's machine, where their character is.
        if (def->is_boss) e.secondary = def->id;
        // To everyone who is here, not only whoever struck the blow: a fight
        // shared is a kill shared. The world hands it round once the frame's
        // acting-as is over.
        world.CreditKill(e);
        (void)ctx;
    }
}

void Enemy::Update(float dt, World& world, const GameContext& ctx) {
    if (puppet) return;
    marked   = std::max(0.0f, marked - dt);
    sundered = std::max(0.0f, sundered - dt);
    taunted  = std::max(0.0f, taunted - dt);
    if (!status_db) status_db = ctx.statuses;
    if (state == State::Dead) { marked = sundered = taunted = 0.0f; statuses.Clear(); }
    // What is on it runs down, and what burns, bleeds or sickens takes its
    // share as it goes -- in whole points, in the status's own colour.
    for (int i = 0; i < STATUS_COUNT && hp > 0; ++i) {
        if (statuses.left[i] <= 0.0f) continue;
        const Status kind = static_cast<Status>(i);
        const StatusDef* d = status_db ? status_db->Get(kind) : nullptr;
        const float step = std::min(dt, statuses.left[i]);
        statuses.left[i] -= step;
        statuses.bank[i] += statuses.rate[i] * step;
        const int whole = static_cast<int>(statuses.bank[i]);
        if (whole > 0) {
            statuses.bank[i] -= static_cast<float>(whole);
            // Taken without the red flash of a blow: a burn ticks six times a
            // second, and flashed for each the monster is simply red -- which
            // hides the colour that says what is on it, and what a blow
            // landing looks like.
            const float flash = hurt_flash;
            Damage(whole);
            hurt_flash = flash;
            const SDL_Color c = d ? d->color : SDL_Color{200, 60, 70, 255};
            world.AddText(std::to_string(whole), x + 10.0f, y - 38.0f, c, 0.7f);
        }
        if (statuses.left[i] <= 0.0f) {
            statuses.End(kind);
            // Frozen thaws into a chill.
            if (d && d->then != Status::COUNT && status_db && hp > 0) Afflict(d->then, 0, *status_db);
        }
    }
    if (hurt_flash > 0.0f) hurt_flash = std::max(0.0f, hurt_flash - dt);
    state_timer += dt;
    if (attack_timer > 0.0f) attack_timer -= dt;
    if (heavy_timer > 0.0f && state != State::Heavy) heavy_timer = std::max(0.0f, heavy_timer - dt);
    if (shoot_timer > 0.0f) shoot_timer = std::max(0.0f, shoot_timer - dt);

    // --- health bar trail ---------------------------------------------------------
    // The fill is always exactly hp / max_hp; this only moves the lighter band
    // that shows how much the last hit took, so the loss is readable.
    grudge = std::max(0.0f, grudge - dt);
    if (hp < last_hp) {
        bar_trail_hold = TRAIL_HOLD;
        // Hurt, from however far and by whatever: it is a fight now.
        if (hp > 0) Provoke();
    }
    last_hp = hp;
    {
        const float frac = HealthFraction();
        if (bar_trail < frac) {
            bar_trail = frac;
        } else if (bar_trail > frac) {
            if (bar_trail_hold > 0.0f) bar_trail_hold -= dt;
            else bar_trail = std::max(frac, bar_trail - TRAIL_DRAIN * dt);
        }
    }

    // --- death ----------------------------------------------------------------
    if (hp <= 0 && state != State::Dead) {
        Audio::PlayAt(Sfx::EnemyDie, x, y);
        SetState(State::Dead);
        respawn_at = respawn_delay;
        OnKilled(world, ctx);
    }
    if (state == State::Dead) {
        sprite.Update(dt);
        // The body used to lie there, dimmed, for the whole respawn delay --
        // and forever for anything that does not respawn. Now it plays its
        // death animation, holds, fades and is gone. A monster missing its
        // death clip falls back to a looping idle that never finishes, so
        // time caps the wait.
        if (sprite.Finished() || state_timer >= DEATH_LINGER) corpse_timer += dt;
        return;
    }

    Player& player = world.player;
    const float dx = player.x - x;
    const float dy = player.y - y;
    const float dist = Length(dx, dy);
    const float home_dist = Length(x - home_x, y - home_y);

    // --- lurking --------------------------------------------------------------
    // See Enemy::Hidden. Handled before everything else, because under the
    // water there is nothing else: it does not wander, does not hear a fight
    // across the bog, and cannot be knocked about by anything.
    if (lurks) {
        if (emerge <= 0.0f && !rising) {
            knock_x = knock_y = 0.0f;
            if (!player.IsDead() && dist < LURK_WAKE) {
                rising = true;
                // `size` is a multiplier on a bolt's splash, not a radius: 1.4
                // is a body breaking the surface, and 16 was a ring the width
                // of the screen.
                world.BurstOf(Element::Water, x, y, draw_lift, 1.4f, 0.0f, -1.0f);
                Audio::PlayAt(Sfx::Splash, x, y, 0.95f, 0.8f);
            } else {
                sprite.Update(dt);
                return;
            }
        }
        if (rising) {
            emerge = std::min(1.0f, emerge + dt / LURK_RISE);
            sprite.Update(dt);
            if (emerge < 1.0f) return;          // coming up: it cannot act yet
            rising = false;
            // At whoever woke it -- but only seen, not hurt: somebody who backs
            // off out of sight is let go, and it goes back into the water. One
            // that has been struck is provoked like anything else, and follows.
            chase_run = 0.0f;
            SetState(State::Chase);
        } else {
            // Home, in its own water, with nobody near: after a while it goes
            // back under, and comes up whole the next time.
            const bool resting = state == State::Idle && world.map.InWater(x, y) &&
                                 (player.IsDead() || dist > LURK_WAKE * 1.8f);
            sink_wait = resting ? sink_wait + dt : 0.0f;
            if (sink_wait > LURK_WAIT) {
                emerge = std::max(0.0f, emerge - dt / LURK_SINK);
                if (emerge <= 0.0f) {
                    sink_wait = 0.0f;
                    hp = max_hp;
                    last_hp = hp;
                    bar_trail = 1.0f;
                    bar_revealed = false;
                    provoked = false;
                    statuses.Clear();
                    world.BurstOf(Element::Water, x, y, draw_lift, 0.7f, 0.0f, -1.0f);
                }
                sprite.Update(dt);
                return;
            }
            // Disturbed on the way down: back up.
            if (emerge < 1.0f) emerge = std::min(1.0f, emerge + dt / LURK_RISE);
        }
    }

    // --- knockback ------------------------------------------------------------
    // Braced while winding up a heavy: knocked about, it would drift out of
    // the reach it is charging into and the blow would go wide of what the
    // bar promised.
    if (state == State::Heavy) { knock_x *= 0.2f; knock_y *= 0.2f; }
    if (fabsf(knock_x) > 1.0f || fabsf(knock_y) > 1.0f) {
        const SDL_FPoint p = world.map.MoveWithCollision(Bounds(), knock_x * dt, knock_y * dt,
                                                        def->swims);
        x = p.x - foot_box.x;
        y = p.y - foot_box.y;
        const float decay = std::max(0.0f, 1.0f - 10.0f * dt);
        knock_x *= decay;
        knock_y *= decay;
    }

    float move_x = 0, move_y = 0;

    switch (state) {
        case State::Hurt:
            if (state_timer >= std::max(HURT_STAGGER, hurt_for)) {
                hurt_for = 0.0f;
                SetState(provoked || dist < def->aggro_range ? State::Chase : State::Idle);
            }
            break;

        case State::Idle: {
            // Something that lurks does not amble about its post: it waits in
            // its water, and goes back under if it is left there. Wandering it
            // would step up on to the bank, where it can never sink again.
            if (lurks) {
                if (!player.IsDead() && dist < def->aggro_range) { chase_run = 0.0f; SetState(State::Chase); }
                break;
            }
            // Waterfowl keep their own counsel: see Enemy::Paddle.
            if (def->swims && def->paddles) {
                Paddle(world, ctx, dt, move_x, move_y);
                if (!player.IsDead() && dist < def->aggro_range) { chase_run = 0.0f; SetState(State::Chase); }
                break;
            }
            // Drift around the post so a field of monsters is not a still life.
            wander_timer -= dt;
            if (wander_timer <= 0.0f) {
                // From the game's own generator, not the C library's. rand()
                // is one hidden sequence shared by everything, unseeded, so
                // where a monster ambled depended on how many numbers anything
                // else had drawn first -- which made every fight with a
                // monster that had been idle for a moment impossible to replay,
                // and a self-test of a swing's arc pass or fail depending on
                // which tests had run before it.
                const auto roll = [&](int n) {
                    return ctx.rng ? static_cast<int>((*ctx.rng)() % static_cast<unsigned>(n)) : rand() % n;
                };
                wander_timer = 1.6f + roll(100) / 40.0f;
                if (roll(3) == 0) {
                    const float angle = roll(628) / 100.0f;
                    wander_dx = cosf(angle);
                    wander_dy = sinf(angle);
                } else {
                    wander_dx = wander_dy = 0.0f;
                }
            }
            if (home_dist > 48.0f) { wander_dx = (home_x - x) / home_dist; wander_dy = (home_y - y) / home_dist; }
            move_x = wander_dx * MoveSpeed() * 0.35f;
            move_y = wander_dy * MoveSpeed() * 0.35f;

            if (!player.IsDead() && dist < def->aggro_range) { chase_run = 0.0f; SetState(State::Chase); }
            break;
        }

        case State::Chase: {
            // It gives up when there is nobody to chase; when it has run its
            // budget with nothing happening; or, if all it ever did was see
            // them, when it has lost sight of them. Not for being far from
            // home: see the top of enemy.h.
            const bool lost = !provoked && dist > def->aggro_range * 1.6f;
            if (player.IsDead() || lost || chase_run >= ChaseBudget()) {
                provoked = false;
                chase_run = 0.0f;
                SetState(State::Return);
                break;
            }

            // A leader with its heavy rested winds it up instead of a swing,
            // from a little further out -- the blow reaches further too.
            if (def->heavy.enabled && heavy_timer <= 0.0f &&
                dist <= std::max(40.0f, def->attack_range * def->heavy.reach) * 0.9f) {
                SetState(State::Heavy);
                chase_run = 0.0f;              // a blow begun is a fight
                Audio::PlayAt(Sfx::SwingHeavy, x, y, 0.8f, 0.55f);
                break;
            }
            // Something that throws looses from where it stands rather than
            // closing: inside its own range, and no nearer than a swing's.
            if (!def->shoots.empty() && shoot_timer <= 0.0f && attack_timer <= 0.0f &&
                dist <= def->shoot_range && dist > def->attack_range) {
                SetState(State::Attack);
                chase_run = 0.0f;
                Audio::PlayAt(Sfx::BowShot, x, y, 0.5f, 0.95f);
                shooting = true;
                swinging = true;
                swing_landed = false;
                swing_timer = 0.0f;
                break;
            }
            if (dist <= def->attack_range && attack_timer <= 0.0f) {
                SetState(State::Attack);
                chase_run = 0.0f;
                Audio::PlayAt(Sfx::Swing, x, y, 0.55f, 0.8f);
                shooting = false;
                swinging = true;
                swing_landed = false;
                swing_timer = 0.0f;
                break;
            }
            // Close to striking distance and hold there while the attack
            // cools down. This used to keep walking until it was within a
            // pixel, so between swings a boar stood exactly where the player
            // was and, drawn after them, hid the character completely.
            // A shooter wants to be out where it can shoot and the player
            // cannot reach; everything else wants to be at arm's length.
            const bool afar = !def->shoots.empty() && def->shoot_range > def->attack_range;
            const float standoff = afar ? def->shoot_range * 0.70f : def->attack_range * 0.75f;
            const float too_close = afar ? def->shoot_range * 0.42f : def->attack_range * 0.4f;
            if (dist > standoff) {
                move_x = (dx / dist) * MoveSpeed();
                move_y = (dy / dist) * MoveSpeed();
            } else if (dist > 0.5f && dist < too_close) {
                // And if the player walks into it, give ground rather than
                // sharing a tile with them.
                move_x = -(dx / dist) * MoveSpeed() * 0.5f;
                move_y = -(dy / dist) * MoveSpeed() * 0.5f;
            }
            break;
        }

        case State::Attack: {
            swing_timer += dt;
            // The hit lands partway through the swing, not on the first frame,
            // so there is a window to step out of it.
            if (swinging && !swing_landed && swing_timer >= SWING_WINDUP && shooting) {
                // It throws. What it throws is a projectile like any other and
                // is resolved where every other shot is: see UpdateProjectiles.
                swing_landed = true;
                const float len = std::max(1.0f, dist);
                world.SpawnProjectile(def->shoots, x, y - 18.0f, dx / len, dy / len,
                                      Profile(), AttackStyle::Ranged, 1.0f, false, ctx);
            } else if (swinging && !swing_landed && swing_timer >= SWING_WINDUP) {
                swing_landed = true;
                // On the ground, and not up or down a cliff: see StrikeArc.
                const SDL_FPoint at = player.GroundCentre();
                const bool level = std::abs(world.map.LevelAt(x, y) - world.map.LevelAt(player.x, player.y)) <= 1;
                if (!player.IsDead() && level && ArcHits(SwingArc(), at.x, at.y, player.GroundRadius())) {
                    DamageResult r = RollMelee(Profile(), player.Profile(), 1.0f, *ctx.rng);
                    if (r.hit && r.damage <= 0) {
                        world.AddText("0", player.x, player.y - 44.0f, {120, 160, 220, 255});
                    } else if (r.hit) {
                        const float len = std::max(1.0f, dist);
                        world.HitPlayer(r.damage, Profile(), x, y,
                                        (dx / len) * 55.0f, (dy / len) * 55.0f);
                    } else {
                        world.AddText("miss", player.x, player.y - 44.0f, {150, 150, 168, 235});
                    }
                }
            }
            if (swing_timer >= ProfileFor(AttackType::Strong).Total()) {
                swinging = false;
                if (shooting) shoot_timer = def->shoot_cooldown;
                shooting = false;
                attack_timer = AttackCooldown();
                SetState(State::Chase);
            }
            break;
        }

        case State::Heavy: {
            // The wind-up: rooted, turning to follow the player for most of
            // it and then committed to that facing for the rest. That last
            // stretch is the tell -- step out of the line now and it lands on
            // nothing.
            if (player.IsDead()) { SetState(State::Chase); break; }
            const float windup = def->heavy.windup;
            if (!heavy_landed && state_timer < windup * HEAVY_LOCK) {
                if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
                else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
            }
            if (!heavy_landed && state_timer >= windup) {
                heavy_landed = true;
                sprite.Play("attack", true);
                Audio::PlayAt(Sfx::Impact, x, y, 1.0f, 0.55f);
                // The ground rings under it; a boss's shakes the screen.
                world.Shock(x, y, def->is_boss ? 0.9f : 0.4f, def->is_boss ? 0.6f : 0.18f);
                const SDL_FPoint at = player.GroundCentre();
                const bool level = std::abs(world.map.LevelAt(x, y) - world.map.LevelAt(player.x, player.y)) <= 1;
                if (level && ArcHits(HeavyArc(), at.x, at.y, player.GroundRadius())) {
                    const float len = std::max(1.0f, dist);
                    world.HeavyHitPlayer(HeavyDamage(ctx.rng), x, y,
                                         (dx / len) * def->heavy.knockback,
                                         (dy / len) * def->heavy.knockback);
                } else {
                    world.AddText("miss", player.x, player.y - 44.0f, {150, 150, 168, 235});
                }
            }
            if (heavy_landed && state_timer >= windup + HEAVY_RECOVER) {
                heavy_timer = def->heavy.cooldown;
                attack_timer = std::max(attack_timer, AttackCooldown() * 0.5f);
                SetState(State::Chase);
            }
            break;
        }

        case State::Return: {
            if (home_dist < 8.0f) { SetState(State::Idle); break; }
            move_x = ((home_x - x) / home_dist) * MoveSpeed() * 0.8f;
            move_y = ((home_y - y) / home_dist) * MoveSpeed() * 0.8f;
            // Re-engage if the player steps back into range on the way home,
            // wherever on the way that is.
            if (!player.IsDead() && dist < def->aggro_range * 0.6f) { chase_run = 0.0f; SetState(State::Chase); }
            break;
        }

        default: break;
    }

    if (move_x != 0.0f || move_y != 0.0f) {
        // Something giving ground while it fights keeps its eyes on the player;
        // otherwise its next swing would go the way it was stepping.
        const bool squaring_up = (state == State::Chase && dist <= def->attack_range);
        const float fx = squaring_up ? dx : move_x;
        const float fy = squaring_up ? dy : move_y;
        if (fabsf(fx) > fabsf(fy)) facing = (fx > 0) ? FACE_RIGHT : FACE_LEFT;
        else                       facing = (fy > 0) ? FACE_DOWN  : FACE_UP;

        const SDL_FPoint p = world.map.MoveWithCollision(Bounds(), move_x * dt, move_y * dt,
                                                        def->swims);
        x = p.x - foot_box.x;
        y = p.y - foot_box.y;
        // Every stride toward them is counted, and giving ground in a fight is
        // not. The stride it meant to take, not the one the map let it: a
        // monster walking into the foot of a cliff after someone on top of it
        // is getting nowhere, and tires of that as fast as of a long run.
        if (state == State::Chase && dist > def->attack_range) chase_run += Length(move_x, move_y) * dt;
    } else if (state == State::Attack || state == State::Chase) {
        // Keep facing the player while swinging.
        if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
        else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    }

    // Asked here, after the step has been taken and whatever the state: the
    // frame a bird leaves the pond is the frame it stops being drawn sitting
    // on it, and a duck someone has taken a swing at comes out in a chase,
    // which is not a state that paddles. Paddle() reads last frame's answer,
    // which is what it wants anyway -- it decides before it moves.
    if (def->swims) afloat = world.map.InWater(x, y);

    if (state == State::Idle || state == State::Chase || state == State::Return) {
        const bool moving = (fabsf(move_x) + fabsf(move_y)) > 1.0f;
        sprite.Play(moving ? "walk" : "idle");
        // Sitting on the water is its own picture: legs gone, body low. A
        // swimmer whose rig has no such clip simply walks, so this is safe to
        // ask of anything.
        if (afloat && sprite.Def() && sprite.Def()->Find("swim")) sprite.Play("swim");
    }

    sprite.facing = facing;
    sprite.Update(dt);
}

// --- waterfowl ---------------------------------------------------------------
// Somewhere to be, wet or dry. Tried as a handful of points on a ring round
// home rather than a search: a duck does not need the nearest water, only some
// water, and eight guesses find it on any bank worth putting ducks on.
bool Enemy::PickHaunt(World& world, const GameContext& ctx, bool wet) {
    const auto roll = [&](int n) {
        return ctx.rng ? static_cast<int>((*ctx.rng)() % static_cast<unsigned>(n)) : rand() % n;
    };
    const float reach = std::max(48.0f, leash);
    for (int tries = 0; tries < 10; ++tries) {
        const float angle = roll(628) / 100.0f;
        const float dist = reach * (0.25f + roll(100) / 133.0f);
        const float tx = home_x + cosf(angle) * dist;
        const float ty = home_y + sinf(angle) * dist;
        if (world.map.InWater(tx, ty) != wet) continue;
        // Somewhere it could actually be: the foot box has to fit, with the
        // water let through only because this one swims.
        SDL_FRect box = foot_box;
        box.x += tx;
        box.y += ty;
        if (world.map.Blocked(box, true)) continue;
        goal_x = tx;
        goal_y = ty;
        goal_wet = wet;
        has_goal = true;
        return true;
    }
    return false;
}

void Enemy::Paddle(World& world, const GameContext& ctx, float dt,
                   float& move_x, float& move_y) {
    const auto roll = [&](int n) {
        return ctx.rng ? static_cast<int>((*ctx.rng)() % static_cast<unsigned>(n)) : rand() % n;
    };
    goal_timer -= dt;

    // Time to think of somewhere else. Where it goes next is mostly the other
    // element: a bird on the bank is likely to get in, and one on the water is
    // likely to come out, so over a minute it does both without being told to.
    if (!has_goal && goal_timer <= 0.0f) {
        // On the water it usually comes out; on the bank it is a toss-up. Over
        // a minute that is a bird that goes in and comes out again without
        // anybody scripting a path for it.
        const bool want_wet = afloat ? (roll(3) == 0) : (roll(2) == 0);
        if (!PickHaunt(world, ctx, want_wet)) PickHaunt(world, ctx, !want_wet);
        if (!has_goal) goal_timer = 2.0f;     // no pond here; stand about
        goal_dist = 1e9f;
        stuck_for = 0.0f;
    }

    if (has_goal) {
        const float dx = goal_x - x, dy = goal_y - y;
        const float d = Length(dx, dy);
        if (d < 9.0f) {
            has_goal = false;
            // Longer on the water than on the bank: swimming is what it came
            // for, and a duck that touched the pond and left again would look
            // like it had changed its mind.
            goal_timer = (goal_wet ? 5.0f : 3.0f) + roll(100) / 25.0f;
        } else {
            // Paddling is slower than walking, and a bird heading somewhere
            // walks rather than ambles.
            const float pace = MoveSpeed() * (afloat ? 0.42f : 0.60f);
            move_x = dx / d * pace;
            move_y = dy / d * pace;
            // Give up on a goal it is not getting closer to. Measured as
            // progress rather than as time spent, so a long waddle across the
            // green is not mistaken for a bird leaning on a fence.
            if (d < goal_dist - 1.0f) { goal_dist = d; stuck_for = 0.0f; }
            else if ((stuck_for += dt) > 2.5f) {
                has_goal = false;
                stuck_for = 0.0f;
                goal_timer = 1.0f;
            }
            return;
        }
    }

    // Between somewheres: potter about on the spot.
    stuck_for = 0.0f;
    wander_timer -= dt;
    if (wander_timer <= 0.0f) {
        wander_timer = 0.9f + roll(100) / 60.0f;
        if (roll(3) == 0) {
            const float angle = roll(628) / 100.0f;
            wander_dx = cosf(angle);
            wander_dy = sinf(angle);
        } else {
            wander_dx = wander_dy = 0.0f;
        }
    }
    const float pace = MoveSpeed() * (afloat ? 0.20f : 0.28f);
    move_x = wander_dx * pace;
    move_y = wander_dy * pace;
}

void Enemy::LieDead() {
    hp = 0;
    state = State::Dead;
    state_timer = 99.0f;
    corpse_timer = 99.0f;          // nothing left to see
    respawn_at = 0.0f;
    respawn_delay = 0.0f;          // and not back while this map is up
    provoked = false;
}

void Enemy::GoToGround() {
    if (state == State::Dead) return;
    hp = 0;
    last_hp = 0;
    state = State::Dead;           // not SetState: it is not dying, and plays no death
    state_timer = DEATH_LINGER;    // so the fade begins now
    corpse_timer = CORPSE_HOLD;
    respawn_at = 0.0f;
    swinging = false;
    knock_x = knock_y = 0.0f;
    provoked = false;
    grudge = 0.0f;
    bar_revealed = false;
}

bool Enemy::CorpseGone() const {
    return state == State::Dead && corpse_timer >= CORPSE_HOLD + CORPSE_FADE;
}

Uint8 Enemy::CorpseAlpha() const {
    if (state != State::Dead || corpse_timer <= CORPSE_HOLD) return 255;
    const float t = std::clamp((corpse_timer - CORPSE_HOLD) / CORPSE_FADE, 0.0f, 1.0f);
    return static_cast<Uint8>(255.0f * (1.0f - t));
}

int Enemy::DissolveKind() const {
    if (!def) return 0;
    const string& id = def->id;
    const auto has = [&](const char* w) { return id.find(w) != string::npos; };
    // What is hardly there to begin with goes up like smoke.
    if (has("wisp") || has("ghost") || has("shade") || has("wraith") || has("spirit") || has("specter") ||
        has("spectre") || has("phantom") || has("gloom") || has("banshee") || has("wight"))
        return 3;
    // What burns goes out in embers.
    if (def->element == Element::Fire || has("demon") || has("imp") || has("cinder") || has("infernal") ||
        has("abyss") || has("ember") || has("pit_lord") || has("hell"))
        return 2;
    // The dead -- what cannot bleed and cannot be poisoned -- crumble.
    if (def->immune[static_cast<int>(Status::Bleed)] && def->immune[static_cast<int>(Status::Poison)]) return 1;
    return 0;
}

void Enemy::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    if (CorpseGone()) return;
    // Under the water, or not all the way out of it. The tell is fair warning:
    // rings on the surface where something waits, a bubble now and then --
    // enough for somebody watching the water to walk round it.
    float sunk = 0.0f;
    if (lurks && state != State::Dead && emerge < 1.0f) {
        const float z = cam.zoom;
        const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f + (home_x + home_y) * 0.013f;
        const SDL_FPoint at = cam.ToScreen(x, y - draw_lift);
        const float under = 1.0f - emerge;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        // With the effects on, the rings are in the water itself: see
        // World::UpdateRipples.
        for (int ring = 0; ring < (Shaders::Effects() ? 0 : 2); ++ring) {
            const float t = fmodf(now * 0.55f + ring * 0.5f, 1.0f);
            const float rx = (5.0f + 17.0f * t) * z, ry = rx * 0.42f;
            const Uint8 a = static_cast<Uint8>(95.0f * (1.0f - t) * under);
            SDL_SetRenderDrawColor(r, 196, 222, 226, a);
            const int dots = 14 + ring * 4;
            for (int i = 0; i < dots; ++i) {
                const float ang = 6.2831853f * i / dots;
                const SDL_FRect px = {roundf((at.x + cosf(ang) * rx) / z) * z,
                                      roundf((at.y + sinf(ang) * ry) / z) * z, z, z};
                SDL_RenderFillRect(r, &px);
            }
        }
        if (fmodf(now * 0.8f, 1.0f) < 0.18f) {
            SDL_SetRenderDrawColor(r, 220, 236, 238, static_cast<Uint8>(150.0f * under));
            const SDL_FRect bubble = {roundf((at.x + 3.0f * z) / z) * z, roundf((at.y - 2.0f * z) / z) * z,
                                      2.0f * z, 2.0f * z};
            SDL_RenderFillRect(r, &bubble);
        }
        if (emerge <= 0.0f) return;
        // Coming up out of it (or going down into it): drawn sunk by what is
        // still under, and faded by the same.
        sunk = 18.0f * under;
    }
    // With the effects on, what is happening to it is drawn on it by the
    // sprite shader, and a tint is only what it was before: see SpriteFx.
    const bool shaded = Shaders::Effects();
    const bool flashes = shaded && Shaders::GetOptions().flashes;
    Shaders::SpriteFx fx;
    const float dying = (state == State::Dead && corpse_timer > CORPSE_HOLD)
        ? std::clamp((corpse_timer - CORPSE_HOLD) / CORPSE_FADE, 0.0f, 1.0f) : 0.0f;

    SDL_Color tint{255, 255, 255, shaded ? Uint8{255} : CorpseAlpha()};
    if (lurks && state != State::Dead) tint.a = static_cast<Uint8>(255.0f * std::clamp(emerge, 0.0f, 1.0f));
    if (def) tint = {def->tint.r, def->tint.g, def->tint.b, tint.a};
    if (shaded) {
        fx.seed = static_cast<float>((static_cast<int>(home_x) * 31 + static_cast<int>(home_y) * 17) % 97);
        if (statuses.Any() && state != State::Dead) {
            fx.burn = statuses.Has(Status::Burn) ? 1.0f : 0.0f;
            fx.cold = statuses.Has(Status::Frozen) ? 1.0f : statuses.Has(Status::Chill) ? 0.5f : 0.0f;
            fx.electrified = statuses.Has(Status::Electrified) ? 1.0f : 0.0f;
            fx.poison = statuses.Has(Status::Poison) ? 1.0f : 0.0f;
            fx.wet = statuses.Has(Status::Wet) ? 1.0f : 0.0f;
            fx.bleed = statuses.Has(Status::Bleed) ? 1.0f : 0.0f;
        }
        if (dying > 0.0f) {
            fx.dissolve = dying;
            fx.dissolve_kind = DissolveKind();
        }
    }
    // What is on it shows on it: its own colours pulled toward the status's --
    // blue for the cold and the wet, green for poison, a throb of orange for a
    // burn. Frozen is nearly all of the way there.
    if (!shaded && statuses.Any() && state != State::Dead) {
        const auto toward = [&](SDL_Color c, float k) {
            tint = {static_cast<Uint8>(tint.r + (c.r - tint.r) * k), static_cast<Uint8>(tint.g + (c.g - tint.g) * k),
                    static_cast<Uint8>(tint.b + (c.b - tint.b) * k), tint.a};
        };
        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        if (statuses.Has(Status::Wet))    toward({150, 190, 255, 255}, 0.35f);
        if (statuses.Has(Status::Poison)) toward({150, 230, 120, 255}, 0.45f);
        if (statuses.Has(Status::Burn))   toward({255, 150, 80, 255}, 0.30f + 0.20f * sinf(t * 14.0f));
        if (statuses.Has(Status::Chill))  toward({170, 215, 255, 255}, 0.50f);
        if (statuses.Has(Status::Frozen)) toward({190, 232, 255, 255}, 0.85f);
    }
    // A blow flashes it: to white, or the colour of what struck it -- which a
    // tint cannot do, a tint only ever darkens. Without the shader, or with
    // flashes turned off, it goes red as it always did.
    if (hurt_flash > 0.0f) {
        if (flashes)
            fx.flash = {flash_color.r / 255.0f, flash_color.g / 255.0f, flash_color.b / 255.0f,
                        std::clamp(hurt_flash / 0.1f, 0.0f, 1.0f) * 0.85f};
        else
            tint = {255, 110, 110, tint.a};
    }

    // Winding up a heavy, it glows red. Two parts: a halo -- its own frame in
    // flat red, a little larger, drawn behind it -- and its own colours pulled
    // towards red. Adding red light on top of the sprite was tried first; on a
    // green orc that comes out beige, not red. Both grow as the charge fills
    // and throb faster as it nears the end. With the shader the halo is a
    // glow round its outline instead, which follows the shape exactly.
    const float charge = HeavyCharge();
    if (charge > 0.0f) {
        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        const float pulse = 0.6f + 0.4f * sinf(t * (8.0f + 18.0f * charge));
        if (shaded) {
            fx.glow = {1.0f, 0.18f, 0.08f, std::clamp((0.35f + 0.65f * charge) * pulse, 0.0f, 1.0f)};
        } else {
            const Uint8 halo_a = static_cast<Uint8>(std::clamp((70.0f + 170.0f * charge) * pulse, 0.0f, 255.0f));
            const float grow = 1.06f + 0.06f * charge * pulse;
            sprite.Draw(r, cache, cam, x, y - draw_lift, {255, 36, 20, halo_a}, SDL_BLENDMODE_BLEND, grow);
        }
        const float k = 0.25f + 0.5f * charge * pulse;
        tint = {tint.r,
                static_cast<Uint8>(tint.g * (1.0f - k)),
                static_cast<Uint8>(tint.b * (1.0f - k)), tint.a};
    }
    // Lifted by the ground under it, as the player and NPCs are. This drew at
    // the raw feet position, so a monster up on a ledge sank into the cliff --
    // and a health bar placed from the terrain height would have floated off it.
    sprite.Draw(r, cache, cam, x, y - draw_lift + sunk, tint, SDL_BLENDMODE_BLEND, 1.0f,
                shaded && fx.Any() ? &fx : nullptr);
}
