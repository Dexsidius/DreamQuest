#pragma once
#include "entity.h"
#include "../input.h"
#include "../systems/skills.h"
#include "../systems/items.h"
#include "../systems/combat.h"
#include "../systems/projectile.h"
#include "../systems/spell.h"

// What the player is currently standing next to and could press Interact on.
struct InteractTarget {
    enum Kind { None, Npc, Object, PortalDoor, Loot } kind = None;
    int    index = -1;          // into the world's npc / object / pickup list
    string label;               // "Talk to Maren", "Open chest", ...
    float  distance = 1e9f;
};

class Player : public Entity {
public:
    Player();

    void Init(const GameContext& ctx, const string& sprite_id);
    void Update(float dt, World& world, const GameContext& ctx) override;
    void Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const override;

    // --- combat ---------------------------------------------------------------
    CombatProfile Profile() const;
    // Melee, Ranged or Magic, decided by what is in the player's hand.
    AttackStyle Style() const;
    // Layer colours for the paperdoll, from what is currently worn.
    LayerStyle BuildLayerStyle(const ItemDatabase* db) const;
    // Called by the world when a swing connects, so the player banks XP for it.
    void AwardCombatXp(int damage, AttackType type);
    void SyncHitpoints();               // keep hp in step with the Hitpoints skill
    bool Attacking() const { return attack.Active(); }
    const AttackState& Attack() const { return attack; }
    // The world applies a swing's hitbox once, then marks it spent.
    bool AttackPending() const { return attack.InActiveWindow() && !attack.consumed; }
    void MarkAttackConsumed() { attack.consumed = true; }

    // 0..1 while the strong button is held past the threshold; 0 otherwise.
    float ChargeProgress() const;

    // True when a new swing may begin: nothing in flight and no cooldown left.
    bool  CanAttack() const { return !attack.Active() && attack_cooldown <= 0.0f; }
    // 0..1 how much of the current cooldown is left, for the HUD.
    float CooldownProgress() const;
    // How fast the equipped weapon swings; 1.0 is the bare-handed baseline.
    float WeaponSpeed() const;
    bool  IsCharging() const { return charging; }

    // --- magic ----------------------------------------------------------------
    // Mana comes from the Magic level and refills over time, so a caster gets
    // more casts as well as bigger ones.
    void  SyncMana();
    int   Mana() const { return mana; }
    int   MaxMana() const { return max_mana; }
    bool  SpendMana(int cost);
    void  RestoreMana() { mana = max_mana; }

    Element SelectedElement() const { return selected_element; }
    void    SelectElement(Element e) { selected_element = e; }
    void    CycleElement(int delta);

    // --- progression ----------------------------------------------------------
    Skills    skills;
    Inventory inventory;
    Equipment equipment;

    // Queued for the HUD: level-ups and XP drops to show.
    vector<LevelUp> TakeLevelUps();
    vector<pair<int,int>> TakeXpDrops();     // skill, amount
    void GrantXp(int skill, int amount);

    // Consume the item in an inventory slot; returns false when it is not
    // edible or the player is already at full health.
    bool Eat(int slot);
    // Wear the item in an inventory slot, swapping out whatever it replaces.
    // Fails when the slot is not equipment or a skill requirement is unmet.
    bool EquipFromInventory(int slot, string& why_not);
    bool UnequipSlot(int equip_slot);

    // --- state ----------------------------------------------------------------
    void  Respawn(float sx, float sy);
    bool  IsDead() const { return dead; }
    float DeathTimer() const { return death_timer; }

    const ItemDatabase* ItemDb() const { return item_db; }

    InteractTarget interact;
    string sprite_id = "player_male";
    float  move_speed = 78.0f;

    // Set by the world when input should not drive the player (dialogue, menus).
    bool input_locked = false;

    json ToJson() const;
    void FromJson(const json& j, const GameContext& ctx);

private:
    void HandleAttackInput(const Input& in, float dt);
    void UpdateAttack(float dt);
    void UpdateAnimation(const Vec2& move);

    AttackState attack;
    // Counts down after a swing finishes. Nothing can start while it is
    // running, which is the whole point: without it the attack button is
    // something you hold rather than something you time.
    float attack_cooldown = 0.0f;
    float cooldown_total = 1.0f;      // what it started at, so the HUD can scale it
    int   combo = 0;
    float combo_window = 0.0f;    // time left to continue the light chain
    bool  charging = false;
    float charge_held = 0.0f;
    bool  strong_armed = false;   // strong button is down, decide on release

    bool  dead = false;
    float death_timer = 0.0f;

    int   mana = 0, max_mana = 0;
    float mana_fraction = 0.0f;      // regen accrues in fractions of a point
    Element selected_element = Element::Fire;

    const ItemDatabase* item_db = nullptr;

    vector<LevelUp> pending_levels;
    vector<pair<int,int>> pending_xp;

    // Combat XP accrues in fractions; bank it and hand over whole points.
    float xp_fraction[SKILL_COUNT] = {0};
};
