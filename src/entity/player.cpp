#include "player.h"
#include "../world/world.h"

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
    if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(sprite_id));
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
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
    (void)db;
    LayerStyle s;
    const SDL_Color armour = equipment.ArmourTint();
    s.body = armour;
    // The head only takes the tint when something is actually worn on it, so a
    // bare-headed character keeps their own colouring.
    s.head = equipment.InSlot(SLOT_HEAD).empty() ? SDL_Color{255, 255, 255, 255} : armour;
    s.weapon = equipment.WeaponTint();
    s.show_weapon = !equipment.InSlot(SLOT_WEAPON).empty();
    s.attachments = equipment.Attachments();
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
    p.defence_bonus  = equipment.DefenceBonus();
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

void Player::HandleAttackInput(const Input& in, float dt) {
    // A swing already under way locks out new input until it recovers, except
    // for buffering the next link of a light chain.
    if (in.Pressed(Action::LightAttack) && !attack.Active()) {
        const int index = (combo_window > 0.0f) ? std::min(combo + 1, 2) : 0;
        combo = index;
        attack.type        = AttackType::Light;
        attack.profile     = ProfileFor(AttackType::Light, index);
        attack.damage_mult = attack.profile.damage_mult;
        attack.reach_scale = 1.0f;
        attack.combo       = index;
        attack.timer       = 0.0f;
        attack.consumed    = false;
        sprite.Play("attack", true);
        combo_window = 0.0f;
    }

    // Strong and charged share a button: press starts the hold, release
    // decides which one actually comes out.
    if (in.Pressed(Action::StrongAttack) && !attack.Active()) {
        strong_armed = true;
        charge_held  = 0.0f;
        charging     = false;
    }

    if (strong_armed && in.Down(Action::StrongAttack)) {
        charge_held += dt;
        if (charge_held >= CHARGE_HOLD_THRESHOLD) charging = true;
    }

    if (strong_armed && in.Released(Action::StrongAttack)) {
        const bool was_charged = charging && charge_held >= CHARGE_HOLD_THRESHOLD;
        const float ratio = ChargeRatio(charge_held);

        attack.type    = was_charged ? AttackType::Charged : AttackType::Strong;
        attack.profile = ProfileFor(attack.type);
        attack.damage_mult = was_charged ? ChargeMultiplier(ratio)
                                         : attack.profile.damage_mult;
        // A fuller charge also swings wider.
        attack.reach_scale = was_charged ? (1.0f + 0.35f * ratio) : 1.0f;
        attack.combo    = 0;
        attack.timer    = 0.0f;
        attack.consumed = false;
        sprite.Play("attack", true);

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
    if (!attack.Active()) return;
    attack.timer += dt;
    if (attack.Finished()) {
        // Only light attacks leave a window open to continue the chain.
        combo_window = (attack.type == AttackType::Light) ? COMBO_WINDOW : 0.0f;
        if (attack.type != AttackType::Light) combo = 0;
        attack.Clear();
    }
}

void Player::UpdateAnimation(const Vec2& move) {
    if (dead) { sprite.Play("death"); return; }
    if (attack.Active()) return;                     // attack clip owns the frames

    const float mag = Length(move.x, move.y);
    if (mag < 0.05f)            sprite.Play("idle");
    else if (mag < RUN_THRESHOLD) sprite.Play("walk");
    else                        sprite.Play("run");
}

void Player::Update(float dt, World& world, const GameContext& ctx) {
    if (hurt_flash > 0.0f) hurt_flash = std::max(0.0f, hurt_flash - dt);
    if (combo_window > 0.0f) {
        combo_window -= dt;
        if (combo_window <= 0.0f) combo = 0;
    }

    // --- death ---------------------------------------------------------------
    if (hp <= 0 && !dead) {
        dead = true;
        death_timer = DEATH_DURATION;
        attack.Clear();
        charging = strong_armed = false;
        sprite.Play("death", true);
    }
    if (dead) {
        death_timer = std::max(0.0f, death_timer - dt);
        sprite.Update(dt);
        return;
    }

    // --- input ---------------------------------------------------------------
    Vec2 move{0, 0};
    if (!input_locked && ctx.input) {
        move = ctx.input->MoveAxis();
        HandleAttackInput(*ctx.input, dt);
    } else {
        // Dropping input mid-charge should not leave a swing armed.
        strong_armed = false;
        charging = false;
        charge_held = 0.0f;
    }

    UpdateAttack(dt);

    // Face the way you are moving, but never mid-swing.
    if (!attack.Active() && Length(move.x, move.y) > 0.05f) {
        if (fabsf(move.x) > fabsf(move.y)) facing = (move.x > 0) ? FACE_RIGHT : FACE_LEFT;
        else                               facing = (move.y > 0) ? FACE_DOWN  : FACE_UP;
    }
    sprite.facing = facing;

    // --- movement ------------------------------------------------------------
    float speed = move_speed;
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
    x = resolved.x - foot_box.x;
    y = resolved.y - foot_box.y;

    // --- mana ----------------------------------------------------------------
    SyncMana();
    if (mana < max_mana) {
        mana_fraction += SpellBook::RegenPerSecond(skills.Level(SKILL_MAGIC)) * dt;
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
    sprite.Draw(r, cache, cam, x, y, tint);
}

void Player::Respawn(float sx, float sy) {
    dead = false;
    death_timer = 0.0f;
    x = sx;
    y = sy;
    knock_x = knock_y = 0.0f;
    attack.Clear();
    charging = strong_armed = false;
    skills.ResetCurrent();
    SyncHitpoints();
    hp = max_hp;
    SyncMana();
    RestoreMana();
    sprite.Play("idle", true);
}

bool Player::Eat(int slot) {
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;
    const ItemStack& s = inventory.Slot(slot);
    if (s.Empty()) return false;

    const ItemDef* def = item_db->Get(s.id);
    if (!def || !def->consumable || def->heal <= 0) return false;
    if (hp >= max_hp) return false;

    Heal(def->heal);
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

    const string item_id = stack.id;
    inventory.RemoveSlot(slot, 1);
    const string displaced = equipment.Equip(def->slot, item_id);
    // The freed slot guarantees room for whatever came off.
    if (!displaced.empty()) inventory.Add(displaced, 1);
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
    };
}

void Player::FromJson(const json& j, const GameContext& ctx) {
    sprite_id = j.value("sprite", string("player_male"));
    item_db = ctx.items;
    if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(sprite_id));
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);

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
