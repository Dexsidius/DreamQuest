#include "player.h"
#include "../world/map.h"
#include "../world/world.h"
#include "../systems/audio.h"

static constexpr float COMBO_WINDOW   = 0.42f;
static constexpr float RUN_THRESHOLD  = 0.62f;
static constexpr float KNOCK_DECAY    = 9.0f;
static constexpr float DEATH_DURATION = 2.4f;

Player::Player() : inventory(nullptr), equipment(nullptr) {
    foot_box = {-8.0f, -10.0f, 16.0f, 10.0f};
    body_box = {-13.0f, -42.0f, 26.0f, 42.0f};
}

void Player::Init(const GameContext& ctx, const string& id) {
    sprite_id = id;
    item_db = ctx.items;
    if (ctx.sprites) {
        // A save may name a character this build no longer has -- the two
        // pack-art characters were dropped when the game went to art it can
        // distribute. Falling back keeps those saves playable instead of
        // loading them as an invisible player.
        const SpriteDef* def = ctx.sprites->Get(sprite_id);
        if (!def) {
            SDL_Log("Player: no sprite '%s'; falling back to %s",
                    sprite_id.c_str(), kDefaultCharacter);
            sprite_id = kDefaultCharacter;
            def = ctx.sprites->Get(sprite_id);
        }
        sprite.SetDef(def);
    }
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
    talents.SetDatabase(ctx.trees);
    sprite.Play("idle");
    SyncHitpoints();
    hp = max_hp;
    SyncMana();
    mana = max_mana;
}

void Player::SyncMana() {
    max_mana = SpellBook::MaxMana(skills.Level(SKILL_MAGIC));
    mana = std::clamp(mana, 0, max_mana);
}

bool Player::SpendMana(int cost) {
    if (cost <= 0) return true;
    if (mana < cost) return false;
    mana -= cost;
    return true;
}

void Player::CycleElement(int delta) {
    // Elements run Fire, Water, Earth, Air; None is not selectable.
    const int count = static_cast<int>(Element::COUNT) - 1;
    int index = static_cast<int>(selected_element) - 1;
    index = ((index + delta) % count + count) % count;
    selected_element = static_cast<Element>(index + 1);
}

AttackStyle Player::Style() const {
    switch (equipment.Kind()) {
        case WeaponKind::Bow:   return AttackStyle::Ranged;
        case WeaponKind::Staff: return AttackStyle::Magic;
        default:                return AttackStyle::Melee;
    }
}

LayerStyle Player::BuildLayerStyle(const ItemDatabase* db) const {
    LayerStyle s;
    const SDL_Color armour = equipment.ArmourTint();
    s.body = armour;
    // The head only takes the tint when something is actually worn on it, so a
    // bare-headed character keeps their own colouring.
    s.head = equipment.InSlot(SLOT_HEAD).empty() ? SDL_Color{255, 255, 255, 255} : armour;
    s.weapon = equipment.WeaponTint();
    s.show_weapon = !equipment.InSlot(SLOT_WEAPON).empty();
    s.attachments = equipment.Attachments();

    // Worn plate, a layer at a time: each piece turns on the sheet for its own
    // slot and paints it its own metal, so a bronze cuirass over iron greaves
    // is drawn as a bronze cuirass over iron greaves rather than as one
    // averaged colour over the whole character.
    if (db) {
        struct Wear { int slot; ArmourLayer layer; const char* name; };
        static const Wear kWear[] = {
            {SLOT_LEGS,   ARMOUR_LEGS,   "legs"},
            {SLOT_BODY,   ARMOUR_BODY,   "body"},
            {SLOT_HANDS,  ARMOUR_HANDS,  "hands"},
            {SLOT_HEAD,   ARMOUR_HEAD,   "head"},
            {SLOT_SHIELD, ARMOUR_SHIELD, "shield"},
        };
        for (const Wear& w : kWear) {
            const ItemDef* d = db->Get(equipment.InSlot(w.slot));
            if (!d || d->armour_layer != w.name) continue;
            s.armour[w.layer].show = true;
            s.armour[w.layer].tint = d->tint;
            s.armour[w.layer].cut = d->armour_cut;
        }
    }

    // The rig's own weapon layers draw a sword. When the equipped weapon
    // carries its own art, hide them and let that stand in instead, or the
    // character ends up holding a staff and a sword at once.
    if (db) {
        const ItemDef* w = db->Get(equipment.InSlot(SLOT_WEAPON));
        if (w && w->worn && !w->worn_sprite.empty()) s.show_weapon = false;
        if (w) s.weapon_model = w->model;
    }
    // Picking herbs is done bare-handed, so the weapon is put away.
    if (gather_clip == "gather") s.show_weapon = false;
    // At work the hands hold the tool, whatever is normally in them.
    if (!gather_model.empty()) {
        s.weapon_model = gather_model;
        s.show_weapon = true;
        s.weapon = {255, 255, 255, 255};
    }
    return s;
}

void Player::SyncHitpoints() {
    max_hp = std::max(1, skills.Level(SKILL_HITPOINTS));
    hp = std::clamp(skills.Current(SKILL_HITPOINTS), 0, max_hp);
}

CombatProfile Player::Profile() const {
    CombatProfile p;
    p.attack_level   = skills.Current(SKILL_ATTACK);
    p.strength_level = skills.Current(SKILL_STRENGTH);
    p.defence_level  = skills.Current(SKILL_DEFENCE);
    p.ranged_level   = skills.Current(SKILL_RANGED);
    p.magic_level    = skills.Current(SKILL_MAGIC);
    p.attack_bonus   = equipment.AttackBonus();
    p.strength_bonus = equipment.StrengthBonus();
    p.defence_bonus  = equipment.DefenceBonus() + static_cast<int>(talents.Global("defence"));
    p.ranged_bonus   = equipment.RangedBonus();
    p.magic_bonus    = equipment.MagicBonus();
    return p;
}

void Player::GrantXp(int skill, int amount) {
    if (skill < 0 || skill >= SKILL_COUNT || amount <= 0) return;
    LevelUp up;
    if (skills.AddXp(skill, amount, up)) {
        pending_levels.push_back(up);
        if (up.skill == SKILL_HITPOINTS) SyncHitpoints();
    }
    pending_xp.emplace_back(skill, amount);
}

// XP follows the style used, the way OSRS ties training to how you fight:
// light swings feed Attack, heavy swings feed Strength, and everything feeds
// Hitpoints.
void Player::AwardCombatXp(int damage, AttackType type) {
    if (damage <= 0) return;

    auto bank = [&](int skill, float amount) {
        xp_fraction[skill] += amount;
        const int whole = static_cast<int>(xp_fraction[skill]);
        if (whole > 0) {
            xp_fraction[skill] -= whole;
            GrantXp(skill, whole);
        }
    };

    const float d = static_cast<float>(damage);

    // A bow trains Ranged and a staff trains Magic whichever button fired it;
    // only melee splits its XP by how heavy the swing was.
    switch (Style()) {
        case AttackStyle::Ranged:
            bank(SKILL_RANGED, d * 4.0f);
            break;
        case AttackStyle::Magic:
            bank(SKILL_MAGIC, d * 4.0f);
            break;
        default:
            switch (type) {
                case AttackType::Light:   bank(SKILL_ATTACK, d * 4.0f); break;
                case AttackType::Strong:  bank(SKILL_STRENGTH, d * 4.0f); break;
                case AttackType::Charged: bank(SKILL_ATTACK, d * 2.0f);
                                          bank(SKILL_STRENGTH, d * 2.0f); break;
                default: break;
            }
            break;
    }
    bank(SKILL_HITPOINTS, d * 1.33f);
}

float Player::ChargeProgress() const {
    return charging ? ChargeRatio(charge_held) : 0.0f;
}

vector<LevelUp> Player::TakeLevelUps() {
    vector<LevelUp> out;
    out.swap(pending_levels);
    return out;
}

vector<pair<int,int>> Player::TakeXpDrops() {
    vector<pair<int,int>> out;
    out.swap(pending_xp);
    return out;
}

void Player::StartGathering(const string& clip, const string& tool_model, float tx, float ty) {
    gather_clip = clip;
    gather_model = tool_model;
    // Face the tree, the rock or the water, not whichever way the walk ended.
    const float dx = tx - x, dy = ty - y;
    if (Length(dx, dy) > 1.0f) {
        if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
        else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
        sprite.facing = facing;
    }
}

void Player::StopGathering() {
    gather_clip.clear();
    gather_model.clear();
}

void Player::FacePoint(float tx, float ty) {
    const float dx = tx - x, dy = ty - (y - 16.0f);
    if (Length(dx, dy) < 1.0f) return;
    if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
    else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    sprite.facing = facing;
}

void Player::TurnToTarget(const World& world) {
    const Enemy* t = world.targeting.Current();
    if (!t) return;
    const SDL_FPoint a = Targeting::AimPoint(*t);
    if (Style() == AttackStyle::Melee &&
        Length(a.x - x, a.y - (y - 16.0f)) > Targeting::MELEE_ASSIST * WeaponReach()) return;
    FacePoint(a.x, a.y);
}

float Player::WeaponReach() const {
    const ItemDef* w = equipment.Weapon();
    return (w && Style() == AttackStyle::Melee) ? std::max(0.5f, w->reach) : 1.0f;
}

void Player::ShapeForWeapon(AttackProfile& p) const {
    const ItemDef* w = equipment.Weapon();
    if (!w || Style() != AttackStyle::Melee) return;
    p.reach     *= std::max(0.5f, w->reach);
    p.width     *= std::clamp(w->sweep, 0.3f, 2.0f);
    p.knockback *= std::max(0.0f, w->push);
}

string Player::AttackClip() const {
    const ItemDef* w = equipment.Weapon();
    if (w && !w->attack_clip.empty() && sprite.Def() && sprite.Def()->Find(w->attack_clip))
        return w->attack_clip;
    return "attack";
}

void Player::HandleAttackInput(const Input& in, float dt, const World& world) {
    // A swing already under way locks out new input until it recovers, except
    // for buffering the next link of a light chain.
    const float speed = WeaponSpeed();

    if (in.Pressed(Action::LightAttack) && CanAttack()) {
        const int index = (combo_window > 0.0f) ? std::min(combo + 1, 2) : 0;
        combo = index;
        attack.type        = AttackType::Light;
        attack.profile     = ScaleForSpeed(ProfileFor(AttackType::Light, index), speed);
        ShapeForWeapon(attack.profile);
        attack.rate        = speed;
        attack.damage_mult = attack.profile.damage_mult;
        attack.reach_scale = 1.0f;
        attack.combo       = index;
        attack.timer       = 0.0f;
        attack.consumed    = false;
        TurnToTarget(world);
        sprite.speed_scale = 1.0f / std::clamp(speed, 0.35f, 3.0f);
        sprite.Play(AttackClip(), true);
        combo_window = 0.0f;
        // A bow or a staff makes its own noise when the shot leaves.
        if (Style() == AttackStyle::Melee) Audio::Play(Sfx::Swing, 1.0f, 1.0f + 0.06f * index);
    }

    // Strong and charged share a button: press starts the hold, release
    // decides which one actually comes out.
    if (in.Pressed(Action::StrongAttack) && CanAttack()) {
        strong_armed = true;
        charge_held  = 0.0f;
        charging     = false;
    }

    if (strong_armed && in.Down(Action::StrongAttack)) {
        charge_held += dt * (1.0f + talents.Global("charge"));
        if (charge_held >= CHARGE_HOLD_THRESHOLD) charging = true;
    }

    if (strong_armed && in.Released(Action::StrongAttack)) {
        const bool was_charged = charging && charge_held >= CHARGE_HOLD_THRESHOLD;
        const float ratio = ChargeRatio(charge_held);

        attack.type    = was_charged ? AttackType::Charged : AttackType::Strong;
        attack.profile = ScaleForSpeed(ProfileFor(attack.type), speed);
        ShapeForWeapon(attack.profile);
        attack.rate    = speed;
        attack.damage_mult = was_charged ? ChargeMultiplier(ratio)
                                         : attack.profile.damage_mult;
        // A fuller charge also swings wider.
        attack.reach_scale = was_charged ? (1.0f + 0.35f * ratio) : 1.0f;
        attack.combo    = 0;
        attack.timer    = 0.0f;
        attack.consumed = false;
        TurnToTarget(world);
        sprite.speed_scale = 1.0f / std::clamp(speed, 0.35f, 3.0f);
        sprite.Play(AttackClip(), true);
        if (Style() == AttackStyle::Melee)
            Audio::Play(Sfx::SwingHeavy, was_charged ? 1.0f : 0.85f, was_charged ? 0.85f : 1.0f);

        strong_armed = false;
        charging     = false;
        charge_held  = 0.0f;
        combo        = 0;
        combo_window = 0.0f;
    }

    // Releasing off-screen or with the button remapped mid-hold: fail safe.
    if (strong_armed && !in.Down(Action::StrongAttack) && !in.Released(Action::StrongAttack)) {
        strong_armed = false;
        charging = false;
        charge_held = 0.0f;
    }
}

void Player::UpdateAttack(float dt) {
    if (!attack.Active()) {
        if (attack_cooldown > 0.0f) {
            attack_cooldown -= dt;
            if (attack_cooldown <= 0.0f) {
                attack_cooldown = 0.0f;
                // The swing is fully over, so hand the frame rate back to the
                // walk and idle clips.
                sprite.speed_scale = 1.0f;
            }
        }
        return;
    }
    attack.timer += dt;
    if (attack.Finished()) {
        // Only light attacks leave a window open to continue the chain.
        combo_window = (attack.type == AttackType::Light) ? COMBO_WINDOW : 0.0f;
        if (attack.type != AttackType::Light) combo = 0;
        attack_cooldown = attack.profile.cooldown;
        cooldown_total  = std::max(0.0001f, attack.profile.cooldown);
        attack.Clear();
    }
}

Player::JumpPlan Player::PlanJump(const Map& map, float dir_x, float dir_y) const {
    JumpPlan plan;
    const float len = Length(dir_x, dir_y);
    if (len < 0.01f) return plan;
    const float ux = dir_x / len, uy = dir_y / len;

    const SDL_FRect here = Bounds();
    const int from_level = map.LevelAt(x, y);

    auto box_at = [&](float px, float py) {
        return SDL_FRect{here.x + (px - x), here.y + (py - y), here.w, here.h};
    };

    // Walk outward looking for the edge. Every sample on the way has to be
    // clear of walls and within reach of the starting level, so a jump never
    // passes through a tree or over the top of a sheer three-level cliff to
    // land on the far side of it.
    constexpr float STEP = 4.0f, REACH = 64.0f;
    for (float d = STEP; d <= REACH; d += STEP) {
        const float px = x + ux * d, py = y + uy * d;
        if (map.Blocked(box_at(px, py))) break;

        const int level = map.LevelAt(px, py);
        const int diff = level - from_level;
        if (std::abs(diff) > CLIMB_LEVELS) break;

        if (diff != 0) {
            // Found the ledge. Carry on a little past the edge so the whole
            // body lands on the new level rather than teetering on its lip.
            const float land = d + 14.0f;
            const float lx = x + ux * land, ly = y + uy * land;
            if (map.Blocked(box_at(lx, ly)) || map.LevelAt(lx, ly) != level)
                break;
            plan.ok = true;
            plan.x = lx;
            plan.y = ly;
            plan.levels = diff;
            return plan;
        }
    }

    // No ledge within reach: a short hop along flat ground, if there is room.
    constexpr float HOP = 22.0f;
    const float hx = x + ux * HOP, hy = y + uy * HOP;
    if (!map.Blocked(box_at(hx, hy)) && map.LevelAt(hx, hy) == from_level &&
        !map.LevelChangeBlocked(x, y, hx, hy)) {
        plan.ok = true;
        plan.x = hx;
        plan.y = hy;
        plan.levels = 0;
    }
    return plan;
}

void Player::UpdateJump(float dt, const Map& map) {
    (void)map;
    jump_timer += dt;
    const float t = std::clamp(jump_timer / JUMP_DURATION, 0.0f, 1.0f);
    // Eased, so the take-off and landing read as effort rather than as a
    // constant-speed slide between two points.
    const float e = t * t * (3.0f - 2.0f * t);
    x = jump_from_x + (jump_to_x - jump_from_x) * e;
    y = jump_from_y + (jump_to_y - jump_from_y) * e;

    if (t >= 1.0f) {
        jumping = false;
        x = jump_to_x;
        y = jump_to_y;
        Audio::Play(Sfx::Land, 0.8f);
    }
}

float Player::JumpLift() const {
    if (!jumping) return 0.0f;
    const float t = std::clamp(jump_timer / JUMP_DURATION, 0.0f, 1.0f);
    const float e = t * t * (3.0f - 2.0f * t);
    const float ground = jump_lift_from + (jump_lift_to - jump_lift_from) * e;
    return ground + sinf(t * 3.14159265f) * JUMP_HEIGHT;
}

float Player::CooldownProgress() const {
    if (attack_cooldown <= 0.0f) return 0.0f;
    return std::clamp(attack_cooldown / cooldown_total, 0.0f, 1.0f);
}

float Player::TalentDamage(AttackStyle style, AttackType type) const {
    float mult = 1.0f + talents.Effect("damage", style);
    if (type == AttackType::Charged) mult += talents.Effect("charged_damage", style);
    if (max_hp > 0 && hp * 3 < max_hp) mult += talents.Effect("low_hp_damage", style);
    return mult;
}

float Player::WeaponSpeed() const {
    const float base = item_db ? equipment.AttackSpeed() : 1.0f;
    // Below one is faster, so a speed talent takes a share off the time.
    return std::max(0.35f, base * (1.0f - talents.Effect("speed", Style())));
}

void Player::UpdateAnimation(const Vec2& move) {
    if (dead) { sprite.Play("death"); return; }
    if (attack.Active()) return;                     // attack clip owns the frames

    const float mag = Length(move.x, move.y);
    // The work, looped for as long as it goes on. A rig with no clip for it
    // swings its attack over and over instead.
    if (!gather_clip.empty() && mag < 0.05f) {
        const bool has_clip = sprite.Def() && sprite.Def()->Find(gather_clip);
        if (has_clip) {
            sprite.Play(gather_clip);
        } else if (sprite.current != "attack" || sprite.Finished()) {
            sprite.Play("attack", true);
        }
        if (attack_cooldown <= 0.0f) sprite.speed_scale = 1.0f;
        return;
    }
    // Playback speed belongs to the attack until its cooldown ends.
    const bool own_speed = attack_cooldown <= 0.0f;
    if (mag < 0.05f)              sprite.Play("idle");
    else if (mag < RUN_THRESHOLD) sprite.Play("walk");
    else if (sprinting) {
        // A rig with its own sprint plays it. One without runs faster, so
        // its feet still keep up with the ground going by.
        const bool has_clip = sprite.Def() && sprite.Def()->Find("sprint");
        sprite.Play(has_clip ? "sprint" : "run");
        if (own_speed) sprite.speed_scale = has_clip ? 1.0f : SPRINT_MULT * 0.85f;
        return;
    }
    else                          sprite.Play("run");
    if (own_speed) sprite.speed_scale = 1.0f;
}

void Player::Update(float dt, World& world, const GameContext& ctx) {
    if (hurt_flash > 0.0f) hurt_flash = std::max(0.0f, hurt_flash - dt);
    // Every source of damage lowers hp; listening for that catches them all.
    if (heard_hp >= 0 && hp < heard_hp && hp > 0) {
        Audio::Play(Sfx::PlayerHurt);
        // A hit breaks a sprint and holds it off long enough to matter.
        sprint_lockout = SPRINT_LOCKOUT;
        sprinting = false;
    }
    heard_hp = hp;
    if (sprint_lockout > 0.0f) sprint_lockout = std::max(0.0f, sprint_lockout - dt);
    if (combo_window > 0.0f) {
        combo_window -= dt;
        if (combo_window <= 0.0f) combo = 0;
    }

    // --- death ---------------------------------------------------------------
    if (hp <= 0 && !dead) {
        dead = true;
        sprinting = false;
        look_ahead = {0, 0};
        death_timer = DEATH_DURATION;
        attack.Clear();
        attack_cooldown = 0.0f;
        sprite.speed_scale = 1.0f;
        charging = strong_armed = false;
        jumping = false;
        sprite.Play("death", true);
        Audio::Play(Sfx::PlayerDie);
    }
    if (dead) {
        death_timer = std::max(0.0f, death_timer - dt);
        sprite.Update(dt);
        return;
    }

    // --- airborne ------------------------------------------------------------
    // A jump owns the player for its whole length: no steering, no attacks,
    // no knockback. It was validated when it started, so nothing mid-air can
    // make it land somewhere it should not.
    if (jumping) {
        UpdateJump(dt, world.map);
        climb_hint.clear();
        sprite.style = BuildLayerStyle(item_db);
        sprite.Update(dt);
        return;
    }

    // --- input ---------------------------------------------------------------
    Vec2 move{0, 0};
    moving = false;
    if (!input_locked && ctx.input) {
        move = ctx.input->MoveAxis();
        moving = Length(move.x, move.y) > 0.3f;
        HandleAttackInput(*ctx.input, dt, world);

        // Which way a jump would go: where you are steering, or failing that
        // where you are facing.
        float jx = move.x, jy = move.y;
        if (Length(jx, jy) < 0.3f) {
            jx = (facing == FACE_LEFT) ? -1.0f : (facing == FACE_RIGHT ? 1.0f : 0.0f);
            jy = (facing == FACE_UP)   ? -1.0f : (facing == FACE_DOWN  ? 1.0f : 0.0f);
        }

        const JumpPlan plan = PlanJump(world.map, jx, jy);

        // Only a ledge earns a prompt, and only while you are pushing at it --
        // standing near an edge you have no intention of climbing is not
        // something the screen needs to keep pointing out.
        climb_hint.clear();
        if (plan.ok && plan.levels != 0 && Length(move.x, move.y) > 0.3f)
            climb_hint = plan.levels > 0 ? "Climb up" : "Drop down";

        if (ctx.input->Pressed(Action::Jump) && !attack.Active() && !charging) {
            jumping      = true;
            sprinting    = false;
            jump_timer   = 0.0f;
            jump_from_x  = x;  jump_from_y = y;
            // A jump nothing can land from is still a jump: a hop in place,
            // so the button always answers.
            jump_to_x    = plan.ok ? plan.x : x;
            jump_to_y    = plan.ok ? plan.y : y;
            jump_lift_from = world.map.HeightAt(x, y);
            jump_lift_to   = world.map.HeightAt(jump_to_x, jump_to_y);
            knock_x = knock_y = 0.0f;
            sprite.speed_scale = 1.0f;
            sprite.Play("jump", true);
            Audio::Play(Sfx::Jump);
            climb_hint.clear();
            sprite.style = BuildLayerStyle(item_db);
            sprite.Update(dt);
            return;
        }
    } else {
        climb_hint.clear();
        // Dropping input mid-charge should not leave a swing armed.
        strong_armed = false;
        charging = false;
        charge_held = 0.0f;
    }

    UpdateAttack(dt);

    // Sprinting: the button held, a real push on the stick, and nothing else
    // going on. A light tilt stays a walk however hard the button is held.
    sprinting = !input_locked && ctx.input && ctx.input->Down(Action::Sprint) &&
                Length(move.x, move.y) >= RUN_THRESHOLD &&
                !attack.Active() && !charging && !strong_armed &&
                sprint_lockout <= 0.0f && !winded && stamina > 0.0f;

    // Stamina: spent by the second while sprinting, back after a breather.
    if (sprinting) {
        stamina = std::max(0.0f, stamina - STAMINA_DRAIN * dt);
        stamina_delay = STAMINA_DELAY;
        if (stamina <= 0.0f) {
            // Out of breath. The sprint ends now, not next frame.
            winded = true;
            sprinting = false;
            Audio::Play(Sfx::Winded);
        }
    } else if (stamina_delay > 0.0f) {
        stamina_delay = std::max(0.0f, stamina_delay - dt);
    } else if (stamina < MaxStamina()) {
        const float rate = STAMINA_REGEN * (1.0f + talents.Global("stamina_regen")) *
            (Length(move.x, move.y) > 0.05f ? STAMINA_REGEN_MOVE : 1.0f);
        stamina = std::min(MaxStamina(), stamina + rate * dt);
    }
    if (winded && stamina >= MaxStamina() * STAMINA_RECOVER) winded = false;
    {
        const float lead = sprinting ? 56.0f : 0.0f;
        const float k = std::min(1.0f, dt * (sprinting ? 2.5f : 4.0f));
        look_ahead.x += (move.x * lead - look_ahead.x) * k;
        look_ahead.y += (move.y * lead - look_ahead.y) * k;
    }

    // Face the way you are moving, but never mid-swing. Standing still with a
    // lock on, face the locked monster, so the next shot does not have to turn.
    if (!attack.Active() && Length(move.x, move.y) > 0.05f) {
        if (fabsf(move.x) > fabsf(move.y)) facing = (move.x > 0) ? FACE_RIGHT : FACE_LEFT;
        else                               facing = (move.y > 0) ? FACE_DOWN  : FACE_UP;
    } else if (!attack.Active()) {
        if (const Enemy* t = world.targeting.Locked()) {
            const SDL_FPoint a = Targeting::AimPoint(*t);
            FacePoint(a.x, a.y);
        }
    }
    sprite.facing = facing;

    // --- movement ------------------------------------------------------------
    float speed = move_speed * (1.0f + talents.Global("move_speed"));
    if (Passive(PASSIVE_MARSHSTRIDE)) speed *= MARSHSTRIDE_SPEED;
    if (sprinting)            speed *= SPRINT_MULT;
    if (attack.Active())      speed *= attack.profile.move_scale;
    else if (charging)        speed *= 0.42f;      // charging slows you to a walk

    float dx = move.x * speed * dt;
    float dy = move.y * speed * dt;

    // Knockback rides on top of steering and decays quickly.
    dx += knock_x * dt;
    dy += knock_y * dt;
    const float decay = std::max(0.0f, 1.0f - KNOCK_DECAY * dt);
    knock_x *= decay;
    knock_y *= decay;

    const SDL_FRect box = Bounds();
    const SDL_FPoint resolved = world.map.MoveWithCollision(box, dx, dy);
    const float moved = Length(resolved.x - foot_box.x - x, resolved.y - foot_box.y - y);
    x = resolved.x - foot_box.x;
    y = resolved.y - foot_box.y;

    // Footsteps: by distance actually covered, so pushing into a wall is
    // silent. Plank floors indoors, stone in the mines, earth outside.
    if (Length(move.x, move.y) > 0.05f && moved > 0.0f) {
        stride += moved;
        // A sprint covers more ground per step, and each one kicks up dust.
        const float STRIDE = sprinting ? 30.0f : 21.0f;
        if (stride >= STRIDE) {
            stride -= STRIDE;
            if (sprinting && !world.map.IsInterior()) world.AddDust(x, y, move.x, move.y);
            const Map& m = world.map;
            const Sfx step = (m.Ambient() == "dungeon") ? Sfx::FootstepStone
                           : m.IsInterior()             ? Sfx::FootstepWood
                                                        : Sfx::Footstep;
            Audio::Play(step, sprinting ? 1.0f : 0.9f, sprinting ? 1.08f : 1.0f);
        }
    } else {
        stride = 14.0f;     // the first step after standing comes quickly
    }

    // --- boosts wearing off -----------------------------------------------------
    // Hitpoints is its own thing -- it drains with damage -- so only the
    // levels a potion can lift drift back toward the real level.
    boost_timer += dt;
    if (boost_timer >= BOOST_DECAY) {
        boost_timer -= BOOST_DECAY;
        for (int s = 0; s < SKILL_COUNT; ++s) {
            if (s == SKILL_HITPOINTS) continue;
            const int level = skills.Level(s), now = skills.Current(s);
            if (now > level) skills.SetCurrent(s, now - 1);
            else if (now < level) skills.SetCurrent(s, now + 1);
        }
    }

    // --- mana ----------------------------------------------------------------
    SyncMana();
    if (mana < max_mana) {
        mana_fraction += SpellBook::RegenPerSecond(skills.Level(SKILL_MAGIC)) *
                         (1.0f + talents.Global("mana_regen")) * dt;
        const int whole = static_cast<int>(mana_fraction);
        if (whole > 0) {
            mana_fraction -= whole;
            mana = std::min(max_mana, mana + whole);
        }
    } else {
        mana_fraction = 0.0f;
    }

    UpdateAnimation(move);
    sprite.style = BuildLayerStyle(item_db);
    sprite.Update(dt);
    skills.SetCurrent(SKILL_HITPOINTS, hp);
}

void Player::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    SDL_Color tint{255, 255, 255, 255};
    if (hurt_flash > 0.0f)   tint = {255, 110, 110, 255};
    else if (charging) {
        // Warm glow that builds with the charge, so the wind-up reads.
        const float t = ChargeRatio(charge_held);
        tint = {255,
                static_cast<Uint8>(255 - 90 * t),
                static_cast<Uint8>(255 - 150 * t), 255};
    }
    sprite.Draw(r, cache, cam, x, y - draw_lift, tint);
}

void Player::Respawn(float sx, float sy) {
    dead = false;
    death_timer = 0.0f;
    x = sx;
    y = sy;
    knock_x = knock_y = 0.0f;
    attack.Clear();
    attack_cooldown = 0.0f;
    sprite.speed_scale = 1.0f;
    charging = strong_armed = false;
    jumping = false;
    sprinting = winded = false;
    stamina = MaxStamina();
    stamina_delay = 0.0f;
    climb_hint.clear();
    skills.ResetCurrent();
    SyncHitpoints();
    hp = max_hp;
    SyncMana();
    RestoreMana();
    sprite.Play("idle", true);
}

void Player::Rest() {
    if (dead) return;
    SyncHitpoints();
    hp = max_hp;
    skills.SetCurrent(SKILL_HITPOINTS, hp);
    heard_hp = hp;
    SyncMana();
    RestoreMana();
    stamina = MaxStamina();
    stamina_delay = 0.0f;
    winded = false;
}

bool Player::Eat(int slot) {
    string why;
    return Consume(slot, why);
}

bool Player::Consume(int slot, string& why_not) {
    why_not.clear();
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;
    const ItemStack& s = inventory.Slot(slot);
    if (s.Empty()) return false;

    const ItemDef* def = item_db->Get(s.id);
    if (!def || !def->consumable) { why_not = "You cannot eat that."; return false; }

    // Only worth using if something would change: food at full health is
    // refused, but a potion that also boosts or restores is not.
    bool helps = (def->heal > 0 && hp < max_hp) || (def->mana > 0 && mana < max_mana) ||
                 (def->stamina && stamina < MaxStamina());
    for (const auto& b : def->boosts) {
        const int level = skills.Level(b.first);
        const int target = level + b.second.first + static_cast<int>(level * b.second.second);
        if (skills.Current(b.first) < target) helps = true;
    }
    if (!helps) {
        why_not = def->boosts.empty() && def->mana == 0 ? "You are already at full health."
                                                        : "It would do nothing for you right now.";
        return false;
    }

    if (def->heal > 0) Heal(def->heal);
    if (def->mana > 0) { SyncMana(); mana = std::min(max_mana, mana + def->mana); }
    if (def->stamina) { stamina = MaxStamina(); stamina_delay = 0.0f; winded = false; }
    for (const auto& b : def->boosts) {
        const int level = skills.Level(b.first);
        const int target = level + b.second.first + static_cast<int>(level * b.second.second);
        // A boost never stacks past its own ceiling, and never lowers one.
        skills.SetCurrent(b.first, std::max(skills.Current(b.first), target));
    }
    if (!def->boosts.empty()) boost_timer = 0.0f;
    skills.SetCurrent(SKILL_HITPOINTS, hp);
    inventory.RemoveSlot(slot, 1);
    return true;
}

bool Player::EquipFromInventory(int slot, string& why_not) {
    why_not.clear();
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;

    const ItemStack& stack = inventory.Slot(slot);
    if (stack.Empty()) return false;

    const ItemDef* def = item_db->Get(stack.id);
    if (!def || def->slot == SLOT_NONE) {
        why_not = "You cannot wear that.";
        return false;
    }

    for (const auto& req : def->requirements) {
        if (skills.Level(req.first) < req.second) {
            why_not = "Needs " + std::to_string(req.second) + " " + SkillName(req.first) + ".";
            return false;
        }
    }

    // A bow takes both hands. Equipping one takes the shield off, and a
    // shield takes the bow off -- but only if the bag has room for what comes
    // off, or the item would be lost.
    int unseat = SLOT_NONE;
    if (item_db && (def->slot == SLOT_WEAPON || def->slot == SLOT_SHIELD)) {
        const ItemDef* worn_weapon = item_db->Get(equipment.InSlot(SLOT_WEAPON));
        const bool bow_in   = def->slot == SLOT_WEAPON && def->kind == WeaponKind::Bow;
        const bool shield_in = def->slot == SLOT_SHIELD;
        const int  clash = (bow_in && !equipment.InSlot(SLOT_SHIELD).empty()) ? SLOT_SHIELD
                         : (shield_in && worn_weapon && worn_weapon->kind == WeaponKind::Bow)
                               ? SLOT_WEAPON : SLOT_NONE;
        if (clash != SLOT_NONE) {
            const int freed = (stack.qty == 1) ? 1 : 0;
            const int needed = 1 + (equipment.InSlot(def->slot).empty() ? 0 : 1);
            if (inventory.FreeSlots() + freed < needed) {
                why_not = clash == SLOT_SHIELD ? "A bow needs both hands, and your pack has no room for the shield."
                                               : "A shield needs a free hand, and your pack has no room for the bow.";
                return false;
            }
            unseat = clash;
        }
    }

    const string item_id = stack.id;
    inventory.RemoveSlot(slot, 1);
    const string displaced = equipment.Equip(def->slot, item_id);
    // The freed slot guarantees room for whatever came off.
    if (!displaced.empty()) inventory.Add(displaced, 1);
    // After the item has left the bag, so its slot counts toward the room.
    if (unseat != SLOT_NONE) inventory.Add(equipment.Unequip(unseat), 1);
    return true;
}

bool Player::UnequipSlot(int equip_slot) {
    const string id = equipment.InSlot(equip_slot);
    if (id.empty()) return false;
    if (inventory.Full()) return false;
    equipment.Unequip(equip_slot);
    inventory.Add(id, 1);
    return true;
}

json Player::ToJson() const {
    return json{
        {"sprite",    sprite_id},
        {"x",         x},
        {"y",         y},
        {"facing",    static_cast<int>(facing)},
        {"hp",        hp},
        {"mana",      mana},
        {"element",   ElementName(selected_element)},
        {"skills",    skills.ToJson()},
        {"inventory", inventory.ToJson()},
        {"equipment", equipment.ToJson()},
        {"talents",   talents.ToJson()},
    };
}

void Player::FromJson(const json& j, const GameContext& ctx) {
    sprite_id = j.value("sprite", string(kDefaultCharacter));
    item_db = ctx.items;
    if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(sprite_id));
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
    talents.SetDatabase(ctx.trees);
    talents.FromJson(j.value("talents", json::object()));

    x = j.value("x", 0.0f);
    y = j.value("y", 0.0f);
    facing = static_cast<Facing>(j.value("facing", 0));

    if (j.contains("skills"))    skills.FromJson(j["skills"]);
    if (j.contains("inventory")) inventory.FromJson(j["inventory"]);
    if (j.contains("equipment")) equipment.FromJson(j["equipment"]);

    SyncHitpoints();
    hp = std::clamp(j.value("hp", max_hp), 1, max_hp);
    SyncMana();
    mana = std::clamp(j.value("mana", max_mana), 0, max_mana);
    selected_element = ElementFromName(j.value("element", string("fire")));
    if (selected_element == Element::None) selected_element = Element::Fire;
    dead = false;
    death_timer = 0.0f;
    sprite.facing = facing;
    sprite.Play("idle", true);
}
