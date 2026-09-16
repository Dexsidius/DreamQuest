#pragma once
#include "entity.h"
#include "../input.h"
#include "../systems/skills.h"
#include "../systems/items.h"
#include "../systems/combat.h"
#include "../systems/projectile.h"
#include "../systems/spell.h"
#include "../systems/talents.h"

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

    // True when a new swing may begin: nothing in flight, no cooldown left,
    // and both feet on the ground.
    bool  CanAttack() const {
        return !attack.Active() && attack_cooldown <= 0.0f && !jumping;
    }

    // --- jumping ---------------------------------------------------------------
    // A hop in the direction you are steering, or facing if you are not. On
    // flat ground it is a short hop; into a ledge up to CLIMB_LEVELS high it
    // carries you up onto it, and off one it drops you down. That is what makes
    // every rise in the terrain that is not a sheer cliff something you can
    // cross, rather than something you have to find a ramp around.
    static constexpr int   CLIMB_LEVELS  = 2;
    static constexpr float JUMP_DURATION = 0.42f;
    static constexpr float JUMP_HEIGHT   = 14.0f;   // screen pixels at the top of the arc

    bool  IsJumping() const { return jumping; }
    // Screen lift while airborne: the terrain height blended from where the
    // jump started to where it lands, plus the arc. The world uses this in
    // place of the ground height so a climb rises smoothly instead of snapping
    // up at the edge.
    float JumpLift() const;

    // What pressing jump would do right now, for the on-screen prompt. Empty
    // when there is nothing worth saying -- a hop on flat ground does not need
    // announcing, a ledge does.
    const string& ClimbHint() const { return climb_hint; }
    // 0..1 how much of the current cooldown is left, for the HUD.
    float CooldownProgress() const;
    // How fast the equipped weapon swings; 1.0 is the bare-handed baseline.
    float WeaponSpeed() const;
    // How far the weapon in hand reaches, as a multiplier on a bare swing's.
    float WeaponReach() const;
    // Whether something worn carries a named passive.
    bool Passive(const string& id) const { return equipment.HasPassive(id); }
    // What the Drowned King's boots do: a quicker step, and ground that burns
    // takes half as much out of you.
    static constexpr const char* PASSIVE_MARSHSTRIDE = "marshstride";
    static constexpr float MARSHSTRIDE_SPEED = 1.15f;
    // A melee strike takes the shape of the weapon it is made with.
    void ShapeForWeapon(AttackProfile& p) const;
    // The clip a strike plays: the weapon's own, when the rig has it.
    string AttackClip() const;
    bool  IsCharging() const { return charging; }

    // --- magic ----------------------------------------------------------------
    // Mana comes from the Magic level and refills over time, so a caster gets
    // more casts as well as bigger ones.
    void  SyncMana();
    int   Mana() const { return mana; }
    int   MaxMana() const { return max_mana; }
    bool  SpendMana(int cost);
    void  RestoreMana() { mana = max_mana; }
    // A night's sleep: health, mana and breath all back to full.
    void  Rest();

    Element SelectedElement() const { return selected_element; }
    void    SelectElement(Element e) { selected_element = e; }
    void    CycleElement(int delta);

    // --- progression ----------------------------------------------------------
    Skills    skills;
    Inventory inventory;
    Equipment equipment;
    // Skill trees: learned nodes and chosen techniques.
    Talents   talents;

    // The talents' damage multiplier for an attack of this style and type,
    // including the ones that depend on the moment (low health, a charge).
    float TalentDamage(AttackStyle style, AttackType type) const;
    // The technique a charged attack with the current weapon comes out as, or
    // empty for a plain charged attack.
    const string& ActiveTechnique() const { return talents.Technique(Style()); }

    // Queued for the HUD: level-ups and XP drops to show.
    vector<LevelUp> TakeLevelUps();
    vector<pair<int,int>> TakeXpDrops();     // skill, amount
    void GrantXp(int skill, int amount);

    // Consume the item in an inventory slot: food heals, a potion can also
    // restore mana and stamina and boost combat levels. Returns false, with
    // the reason, when it would do nothing.
    bool Eat(int slot);
    bool Consume(int slot, string& why_not);
    // A boost above a level wears off one point every BOOST_DECAY seconds.
    static constexpr float BOOST_DECAY = 45.0f;
    // Wear the item in an inventory slot, swapping out whatever it replaces.
    // Fails when the slot is not equipment or a skill requirement is unmet.
    bool EquipFromInventory(int slot, string& why_not);
    bool UnequipSlot(int equip_slot);

    // --- state ----------------------------------------------------------------
    void  Respawn(float sx, float sy);
    bool  IsDead() const { return dead; }
    float DeathTimer() const { return death_timer; }

    const ItemDatabase* ItemDb() const { return item_db; }

    // The character every fallback lands on: the game's own art, always
    // present, where the pack characters were only there if someone had run
    // the importer with those packs installed.
    static constexpr const char* kDefaultCharacter = "player_hero";

    InteractTarget interact;
    string sprite_id = kDefaultCharacter;
    float  move_speed = 78.0f;

    // --- sprinting ------------------------------------------------------------
    // Held to cross the world faster. Not a combat move: it cannot start in a
    // swing or a charge, and a hit knocks the player out of it for a moment.
    static constexpr float SPRINT_MULT     = 1.6f;
    static constexpr float SPRINT_LOCKOUT  = 0.8f;
    bool  Sprinting() const { return sprinting; }

    // --- stamina --------------------------------------------------------------
    // What a sprint costs. It drains while sprinting and comes back after a
    // short breather, faster standing still than on the move. Running it dry
    // leaves the player winded: no sprinting until it has refilled past a
    // threshold, so an empty bar cannot be feathered into a stuttering sprint.
    static constexpr float MAX_STAMINA        = 100.0f;
    static constexpr float STAMINA_DRAIN      = 22.0f;   // per second sprinting
    static constexpr float STAMINA_REGEN      = 30.0f;   // per second at rest
    static constexpr float STAMINA_REGEN_MOVE = 0.6f;    // share of that while moving
    static constexpr float STAMINA_DELAY      = 0.8f;    // breather before regen starts
    static constexpr float STAMINA_RECOVER    = 0.35f;   // share needed to sprint again
    float Stamina() const { return stamina; }
    float MaxStamina() const { return MAX_STAMINA * (1.0f + talents.Global("stamina")); }
    bool  Winded() const { return winded; }
    // Where the camera should lead, in world pixels: ahead of a sprint so the
    // player sees what they are running into, and back to centre otherwise.
    Vec2  LookAhead() const { return look_ahead; }

    // --- gathering ------------------------------------------------------------
    // While chopping, mining or fishing, the player turns to the work, plays
    // that clip, and holds the tool rather than the weapon.
    void StartGathering(const string& clip, const string& tool_model, float tx, float ty);
    void StopGathering();
    const string& GatherClip() const { return gather_clip; }
    // True while the stick or keys are pushing the player somewhere.
    bool Moving() const { return moving; }

    // Set by the world when input should not drive the player (dialogue, menus).
    bool input_locked = false;

    json ToJson() const;
    void FromJson(const json& j, const GameContext& ctx);

private:
    void HandleAttackInput(const Input& in, float dt, const World& world);
    // An attack starting turns to face the target: always for a bow or a
    // staff, and for a sword when the target is within reach of a swing.
    void TurnToTarget(const World& world);
    void FacePoint(float tx, float ty);
    void UpdateAttack(float dt);
    void UpdateAnimation(const Vec2& move);

    AttackState attack;
    // Counts down after a swing finishes. Nothing can start while it is
    // running, which is the whole point: without it the attack button is
    // something you hold rather than something you time.
    float attack_cooldown = 0.0f;

    bool  jumping = false;
    float jump_timer = 0.0f;
    float jump_from_x = 0, jump_from_y = 0, jump_to_x = 0, jump_to_y = 0;
    float jump_lift_from = 0, jump_lift_to = 0;
    string climb_hint;

    // Where a jump from here along (dx, dy) would land, and whether it can.
    struct JumpPlan { bool ok = false; float x = 0, y = 0; int levels = 0; };
    JumpPlan PlanJump(const class Map& map, float dir_x, float dir_y) const;
    void     UpdateJump(float dt, const class Map& map);
    float cooldown_total = 1.0f;      // what it started at, so the HUD can scale it
    int   combo = 0;
    float combo_window = 0.0f;    // time left to continue the light chain
    bool  charging = false;
    float charge_held = 0.0f;
    bool  strong_armed = false;   // strong button is down, decide on release

    bool  dead = false;
    float death_timer = 0.0f;
    // Sound bookkeeping: health last frame, to hear a hit however it landed,
    // and distance walked since the last footstep.
    int   heard_hp = -1;
    float stride = 0.0f;

    string gather_clip, gather_model;
    bool  moving = false;

    bool  sprinting = false;
    float sprint_lockout = 0.0f;
    float stamina = MAX_STAMINA;
    float stamina_delay = 0.0f;
    bool  winded = false;
    Vec2  look_ahead{0, 0};

    float boost_timer = 0.0f;
    int   mana = 0, max_mana = 0;
    float mana_fraction = 0.0f;      // regen accrues in fractions of a point
    Element selected_element = Element::Fire;

    const ItemDatabase* item_db = nullptr;

    vector<LevelUp> pending_levels;
    vector<pair<int,int>> pending_xp;

    // Combat XP accrues in fractions; bank it and hand over whole points.
    float xp_fraction[SKILL_COUNT] = {0};
};
