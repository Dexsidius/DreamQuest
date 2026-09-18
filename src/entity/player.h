#pragma once
#include "entity.h"
#include "../input.h"
#include "player_input.h"
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

    // --- combos ---------------------------------------------------------------
    // See ComboMove in combat.h. What a press of the light or the heavy
    // button would come out as right now, or None for a plain attack -- the
    // HUD prints it while the chain is open. Only with a melee weapon: a bow
    // or a staff has no chain to mix a heavy into.
    ComboMove NextCombo(bool light) const;
    // True while the last swing has left a window to go on from.
    bool  ComboOpen() const { return combo_window > 0.0f; }
    // The link the chain is on, 0 to 2.
    int   ComboLink() const { return combo; }

    // --- the chain counter ------------------------------------------------------
    // Melee swings that connected one after another, and what each was, for
    // the HUD. A swing that lands on nothing, a blow taken, or a pause longer
    // than CHAIN_HOLD after the last hit ends the run; it stays on screen for
    // that long and fades over the last half second.
    static constexpr float CHAIN_HOLD = 1.6f;
    static constexpr float CHAIN_FADE = 0.5f;
    void  CountChainHit(const string& label);
    void  BreakChain();
    int   ChainHits() const { return chain_hits; }
    // The last few swings of the run, oldest first: "Light", "Cleave", ...
    const vector<string>& ChainTrail() const { return chain_trail; }
    // 1 while the run is live, falling to 0 as it fades.
    float ChainFade() const;

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
    void    SelectElement(Element e) { if (e != Element::Arcane || !arcane_spell.empty()) selected_element = e; }
    void    CycleElement(int delta);
    // The ancient magic: chooses the arcane school with the first spell of
    // `known` -- the ids learned, in the order they are learned -- or, already
    // on it, steps to the next one known. Nothing happens with none known.
    void    SelectArcane(const vector<string>& known);
    const string& ArcaneSpell() const { return arcane_spell; }

    // --- progression ----------------------------------------------------------
    Skills    skills;
    Inventory inventory;
    Equipment equipment;
    // Skill trees: learned nodes and chosen techniques.
    Talents   talents;

    // The talents' damage multiplier for an attack of this style and type,
    // including the ones that depend on the moment (low health, a charge),
    // and the character's affinity when the style is theirs.
    float TalentDamage(AttackStyle style, AttackType type) const;

    // --- affinity ---------------------------------------------------------------
    // Each of the three characters favours one way of fighting: the hero the
    // blade, the warden the bow, the wayfarer the staff. Attacks of that
    // style hit a tenth harder and carry a little more accuracy, from the
    // first swing and for good. It is who they are, not something learned.
    static constexpr float AFFINITY_DAMAGE = 0.10f;
    static constexpr int   AFFINITY_BONUS  = 8;
    static AttackStyle AffinityFor(const string& character_id);
    static const char* AffinityName(AttackStyle style);    // "the blade", "the bow", "the staff"
    AttackStyle Affinity() const { return AffinityFor(sprite_id); }
    // What a new character of this look is handed and wears from the first
    // step: the wood tier's weapon of their affinity, a cuirass, and a shield
    // where the weapon leaves a hand for one. The weapon is first in the list.
    static vector<string> StartingKit(const string& character_id);
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
    // Nobody to fight: fallen, or not there at all. `absent` is the seat at a
    // machine that has no player of its own -- the headless server, or a map
    // only friends are on. `away` is a friend whose line has dropped, standing
    // where they were for a while in case they come back.
    bool  IsDead() const { return dead || absent || away; }
    bool  Fallen() const { return dead; }
    bool  absent = false, away = false;
    // Lying down for the night, in company: out of the fight until dawn or
    // until they get up.
    bool  resting = false;
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

    // --- Rushing Strike -------------------------------------------------------
    // Learned in the melee tree's Footwork branch. A light attack started at a
    // sprint -- the sprint button held and the character actually running --
    // with a melee weapon, is a leap: the character springs at whatever
    // they are fighting -- or on along the way they were running -- and brings
    // the weapon down as they land, for 1.4 times the damage of the light
    // attack it replaced. Then it rests for three seconds, during which a
    // running light attack is an ordinary one.
    static constexpr float RUSH_COOLDOWN = 3.0f;
    static constexpr float RUSH_DAMAGE   = 1.4f;    // times a light attack's
    static constexpr float RUSH_DISTANCE = 86.0f;   // world px the leap covers
    static constexpr float RUSH_HEIGHT   = 12.0f;   // screen px at the top of the arc
    static constexpr float RUSH_SEEK     = 150.0f;  // how far away a target is leapt at
    // Whether a light attack right now would come out as the leap, running aside.
    bool  CanRush() const;
    bool  Rushing() const { return rushing; }
    float RushCooldown() const { return rush_cooldown; }
    // Screen lift through the leap, like JumpLift for a jump.
    float RushLift() const;

    // --- blocking -------------------------------------------------------------
    // Held, with a shield in the off hand. The guard stops blows from in front
    // for as long as there is stamina to pay for them (see ResolveBlock in
    // combat.h for the rule), trains Defence by what it stops, and slows the
    // player to a guarded step: no swinging, no sprinting. Running the bar dry
    // mid-block breaks the guard, and it will not come up again until the bar
    // has refilled past the same share a winded sprint waits for.
    static constexpr float BLOCK_MOVE_SCALE    = 0.45f;
    static constexpr float BLOCK_XP_PER_DAMAGE = 4.0f;   // the rate a hit trains its skill
    // The shield in the off hand, or null when there is nothing there that
    // blocks -- including a lantern.
    const ItemDef* Shield() const;
    // Whether the guard could come up this instant.
    bool  CanBlock() const;
    bool  Blocking() const { return blocking; }
    bool  GuardBroken() const { return guard_broken; }
    // A blow about to land, from an attacker of this level standing at
    // (from_x, from_y). Returns what the shield did with it -- nothing, when
    // the guard is down or the blow came from behind -- and has already spent
    // the stamina and banked the Defence XP.
    BlockOutcome TryBlock(int damage, int attacker_level, float from_x, float from_y);
    // Whether a blow from (from_x, from_y) would be met by the raised shield.
    bool  GuardFacing(float from_x, float from_y) const;
    // What a heavy attack does to a raised guard: the bar emptied, the guard
    // broken, and a longer wait before stamina starts coming back.
    void  ShatterGuard();

    // --- gathering ------------------------------------------------------------
    // While chopping, mining or fishing, the player turns to the work, plays
    // that clip, and holds the tool rather than the weapon.
    void StartGathering(const string& clip, const string& tool_model, float tx, float ty);
    void StopGathering();
    const string& GatherClip() const { return gather_clip; }
    const string& GatherModel() const { return gather_model; }
    // True while the stick or keys are pushing the player somewhere.
    bool Moving() const { return moving; }

    // Set by the world when input should not drive the player (dialogue, menus).
    bool input_locked = false;

    // --- seats -----------------------------------------------------------------
    // What this character's hands are doing this step; see player_input.h.
    // The world fills it from the device for the seat this machine drives,
    // unless `hands_external` says someone else is filling it: the co-op
    // client, which quantises it first so it predicts with what it sends.
    PlayerInput hands;
    bool hands_external = false;
    // The seat at this machine, whose targeting and camera the world's are.
    // False for everyone in World::guests. A guest does not turn to face the
    // host's target.
    bool local = true;
    // A guest drawn from what the server says rather than stepped here: a
    // friend, as a client sees them. Never updated, only posed.
    bool puppet = false;
    uint8_t seat = 0;
    string  name;
    // Poses a puppet: where, which way, which clip and which frame of it.
    void Pose(float px, float py, Facing face, const string& clip, int frame, const ItemDatabase* db);
    // The clip playing and the frame it is on, for the server to tell.
    const string& Clip() const { return sprite.current; }
    int ClipFrame() const { return sprite.Frame(); }

    json ToJson() const;
    void FromJson(const json& j, const GameContext& ctx);
    // The character sheet alone -- skills, bag, equipment, talents, the spell
    // chosen -- laid over a character that is up and about: where they stand,
    // what they are doing and how hurt they are is left as it is. This is how
    // the host keeps its copy of a friend's character up to date.
    void ApplySheet(const json& j, const GameContext& ctx);
    void SetMana(int v) { mana = std::clamp(v, 0, max_mana); }

private:
    void HandleAttackInput(const PlayerInput& in, float dt, const World& world);
    // What this seat is fighting, or null: only the local seat has targeting.
    const class Enemy* CurrentTarget(const World& world) const;
    const class Enemy* LockedTarget(const World& world) const;
    // An attack starting turns to face the target: always for a bow or a
    // staff, and for a sword when the target is within reach of a swing.
    void TurnToTarget(const World& world);
    void FacePoint(float tx, float ty);
    void UpdateAttack(float dt);
    // Starts the leap in place of a light attack. False, doing nothing, when a
    // leap is not possible right now.
    bool StartRush(const World& world);
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
    // The last swing was a plain strong, so a light inside the window is a
    // Backhand and a heavy is a fresh hold rather than a combo.
    bool  after_strong = false;
    // Presses made inside a swing, kept for the moment the next may start.
    float buf_light = 0.0f, buf_strong = 0.0f;
    int   chain_hits = 0;
    vector<string> chain_trail;
    float chain_show = 0.0f;
    // Starts one of the combos as the swing in flight.
    void  StartCombo(ComboMove move, AttackType type, const World& world);
    // Fires the strong or charged attack the heavy button's hold decided on.
    void  FireStrong(bool charged, float ratio, const World& world);
    // The clip a combo plays: its own, or the plain swing on a rig without it.
    string ComboClip(ComboMove move) const;

    bool  dead = false;
    float death_timer = 0.0f;
    // Sound bookkeeping: health last frame, to hear a hit however it landed,
    // and distance walked since the last footstep.
    int   heard_hp = -1;
    float stride = 0.0f;

    string gather_clip, gather_model;
    bool  moving = false;

    bool  sprinting = false;
    bool  blocking = false;
    bool  rushing = false;
    float rush_cooldown = 0.0f;
    float rush_dx = 0.0f, rush_dy = 0.0f;   // unit direction of the leap
    Vec2  move_axis{0, 0};                   // this frame's steering, for the attack input
    bool  guard_broken = false;
    float sprint_lockout = 0.0f;
    float stamina = MAX_STAMINA;
    float stamina_delay = 0.0f;
    bool  winded = false;
    Vec2  look_ahead{0, 0};

    float boost_timer = 0.0f;
    int   mana = 0, max_mana = 0;
    float mana_fraction = 0.0f;      // regen accrues in fractions of a point
    Element selected_element = Element::Fire;
    string  arcane_spell;                 // the ancient spell chosen with 5

    const ItemDatabase* item_db = nullptr;

    vector<LevelUp> pending_levels;
    vector<pair<int,int>> pending_xp;

    // Combat XP accrues in fractions; bank it and hand over whole points.
    float xp_fraction[SKILL_COUNT] = {0};
    void  BankXp(int skill, float amount);
};
