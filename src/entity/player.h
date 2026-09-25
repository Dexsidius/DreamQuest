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
    // How a character looks holding what they set out with, for a preview that
    // is not a character yet: the character-select screen's cards.
    static LayerStyle KitStyle(const string& character_id, const ItemDatabase* db);
    // Called by the world when a swing connects, so the player banks XP for it.
    // `worth` is the monster's own multiplier (`xp_mult` in enemies.json),
    // which was read from the file and then by nothing.
    void AwardCombatXp(int damage, AttackType type, float worth = 1.0f);
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
        return reload_left <= 0.0f && !attack.Active() && attack_cooldown <= 0.0f && !jumping;
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
    // `clip`, or -- with a weapon that takes both hands and a rig that has it --
    // the same swing made with both hands on the hilt: "crush" -> "crush_2h".
    string BothHands(const string& clip) const;
    // A strike whose clip asks to be fitted ("fit" in sprites.json) is played to
    // last exactly as long as the attack now under way. Call after Play.
    void   FitSwing();
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

    // How long since a blow last got through, for the Hellish Rebuke: inside
    // the window it is an answer, and lands half as hard again.
    static constexpr float REBUKE_WINDOW = 4.0f, REBUKE_DAMAGE = 1.5f;
    float   SinceHurt() const { return since_hurt; }
    void    NoteHurt() { since_hurt = 0.0f; }

    // --- the armoury ----------------------------------------------------------------
    // What this weapon calls a combo, and what is different about it: see
    // ItemDef::ComboTwist. The sword's name where the weapon has none.
    string ComboLabel(ComboMove move) const;
    const ItemDef::ComboTwist* Twist(ComboMove move) const;
    // A crossbow being spanned: nothing can be let off until it is.
    bool  Reloading() const { return reload_left > 0.0f; }
    float ReloadProgress() const { return reload_time > 0.0f ? 1.0f - reload_left / reload_time : 1.0f; }
    void  StartReload();
    // A staff given over to one element, and which of its four spells is
    // chosen: see ItemDef::element. None, and 1 to 4 are the elements.
    Element StaffElement() const;
    int   SpellSlot() const { return spell_slot; }
    void  SelectSlot(int slot) { spell_slot = std::clamp(slot, 0, 3); if (StaffElement() != Element::None) selected_element = StaffElement(); }

    Element SelectedElement() const { return StaffElement() != Element::None && selected_element != Element::Arcane
                                                 ? StaffElement() : selected_element; }
    void    SelectElement(Element e) { if (e != Element::Arcane || !arcane_spell.empty()) selected_element = e; }
    void    CycleElement(int delta);
    // The same, knowing which ancient spells have been learned: with any, the
    // fifth slot is in the round. It was only in it once 5 had been pressed --
    // that is what first put a spell on it -- and a pad has no 5: it has this.
    void    CycleElement(int delta, const vector<string>& known);
    // The ancient magic: chooses the arcane school with the first spell of
    // `known` -- the ids learned, in the order they are learned -- or, already
    // on it, steps to the next one known. Nothing happens with none known.
    void    SelectArcane(const vector<string>& known);
    const string& ArcaneSpell() const { return arcane_spell; }
    // Lightning works the way the ancient magic does, and for the same reason:
    // it has five spells and there are only so many keys. The fifth key
    // chooses the element, and the fifth key again steps to the next of them
    // the Magic level reaches. `known` is that list, weakest first.
    void    SelectElectric(const vector<string>& known);
    const string& ElectricSpell() const { return electric_spell; }
    void    SetElectricSpell(const string& id) { electric_spell = id; }

    // --- the battery ----------------------------------------------------------------
    // Lightning's resource, and nothing else's: a share of a bar, 0 to 1,
    // empty when a character is made. Zap fills it a little on every hit and
    // everything else in the school spends it. It is not restored by resting
    // or by a night's sleep -- it is not a pool, it is what has been stored --
    // but dying empties it, the way a fight's momentum goes.
    float   Battery() const { return battery; }
    void    AddBattery(float share) { battery = std::clamp(battery + share, 0.0f, 1.0f); }
    void    ClearBattery() { battery = 0.0f; }
    // Takes at most `want`, and answers with what was actually there to take:
    // a discharge is worth what it spent.
    float   SpendBattery(float want) {
        const float took = std::clamp(std::min(want, battery), 0.0f, 1.0f);
        battery -= took;
        return took;
    }
    // Puts an ancient spell on the fifth slot without choosing the slot: the
    // spellbook's page does this.
    void    SetArcaneSpell(const string& id) { arcane_spell = id; }
    // The spell an element is held to, or nothing for the strongest: see
    // SpellBook::Chosen. Fire, water, earth and air only.
    const string& HeldSpell(Element e) const;
    void    HoldSpell(Element e, const string& id);
    const SpellDef* SpellOf(Element e, const SpellBook& book) const {
        // An element's own staff casts the spell on the slot chosen; anything
        // else casts what it is held to, out of what the weapon in hand reaches
        // of that element, and the element's strongest first spell otherwise.
        if (StaffElement() == e && spell_slot > 0) return book.ForSlot(e, spell_slot + 1, skills.Level(SKILL_MAGIC));
        const vector<int>& slots = SpellSlots(e);
        if (!slots.empty()) return book.ChosenFor(e, slots, skills.Level(SKILL_MAGIC), HeldSpell(e));
        return book.Chosen(e, skills.Level(SKILL_MAGIC), HeldSpell(e));
    }
    // What there is to choose between in the slot chosen now, in order: for
    // fire, water, earth and air what the weapon in hand reaches of it that
    // the Magic level casts (as the spellbook's page offers it); an element's
    // own staff's four; the lightning and the ancient magic known. `arcane`
    // and `electric` are the ids known of those two, as Game works them out.
    vector<const SpellDef*> SpellChoices(const SpellBook& book, const vector<string>& arcane,
                                         const vector<string>& electric) const;
    // One along that list (`step` -1 or 1), and round. False, changing
    // nothing, with fewer than two to choose from. Everything it sets is on
    // the character sheet, which is how a friend's host hears of it.
    bool StepSpell(int step, const SpellBook& book, const vector<string>& arcane,
                   const vector<string>& electric);
    // Which of an element's four the weapon in hand reaches: see
    // ItemDef::spell_slots. Empty with nothing in hand that casts.
    const vector<int>& SpellSlots(Element e) const {
        static const vector<int> none;
        const ItemDef* w = equipment.Weapon();
        return w ? w->SpellSlotsFor(e) : none;
    }

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

    // --- abilities --------------------------------------------------------------
    // Moves of their own, learned in the character's tree -- six to a tree --
    // and three carried at once: guard held and the light button, the heavy
    // button, or lock on.
    // Each has a cooldown and a cost. What an ability does to the player --
    // the roll, the step through the air, the shout's strength -- happens here,
    // so a friend's machine predicts it; what it does to the world is left in
    // `pending_ability` for the world to do, which on a friend's machine is
    // the host.
    static constexpr float TUMBLE_SPEED    = 820.0f;   // px/s, spent in a third of a second
    static constexpr float TUMBLE_TIME     = 0.34f;    // untouchable this long
    static constexpr float BLINK_DISTANCE  = 116.0f;
    static constexpr float WAR_CRY_TIME    = 8.0f;
    static constexpr float WAR_CRY_DAMAGE  = 0.25f;
    static constexpr float MANA_SHIELD_TIME = 10.0f;
    static constexpr int   MANA_PER_HP     = 2;
    static constexpr float RIPOSTE_WINDOW  = 3.0f;
    static constexpr float HIT_RUN_TIME    = 3.0f;
    static constexpr int   ATTUNE_MAX      = 5;
    static constexpr int   MOMENTUM_MAX    = 10;
    static constexpr float FRENZY_TIME     = 6.0f;
    static constexpr float FRENZY_SPEED    = 0.30f;    // off the time a swing takes
    static constexpr float STAND_FAST_TIME = 6.0f;
    static constexpr float STAND_FAST_SHARE = 0.60f;   // of a blow still taken
    static constexpr float AIM_WINDOW      = 6.0f;
    static constexpr float AIM_DAMAGE      = 1.5f;
    static constexpr float RAPID_TIME      = 5.0f;
    static constexpr float RAPID_SPEED     = 0.40f;
    static constexpr float OVERLOAD_WINDOW = 6.0f;
    static constexpr float OVERLOAD_DAMAGE = 2.0f;
    static constexpr float INVOKE_TIME     = 4.0f;
    static constexpr float INVOKE_SHARE    = 0.5f;     // of all the mana there is
    static constexpr int   WEAK_POINT_MAX  = 4;
    static constexpr int   BLEED_CHAIN     = 3;        // hits into a chain before wounds stay open
    bool  TryAbility(int slot, World& world);
    float AbilityCooldown(int slot) const { return ability_cd[std::clamp(slot, 0, SkillTrees::ABILITY_SLOTS - 1)]; }
    // The ability begun this step, once, for the world to finish.
    string TakeAbility() { string a; a.swap(pending_ability); return a; }
    bool  Untouchable() const { return tumble_timer > 0.0f; }
    bool  WarCry() const { return war_cry_timer > 0.0f; }
    bool  ManaShield() const { return mana_shield_timer > 0.0f; }
    float ManaShieldLeft() const { return mana_shield_timer; }
    // A friend's shield, which is their machine's business to count down: the
    // puppet is only told whether it is up, so that the dome can be drawn over
    // them as it is over you.
    bool  shield_shown = false;
    // Gone through the ice and not out yet: not drawn. Set by the world
    // stepping them (World::UpdateThinIce), or for a friend's puppet by the
    // host's word (net::PlayerState::Under).
    bool  under_ice = false;
    bool  ShieldUp() const { return ManaShield() || shield_shown; }
    // Seconds since the shield last took a blow, for the ripple across it.
    float ShieldStruck() const { return shield_struck; }
    float WarCryLeft() const { return war_cry_timer; }
    bool  Frenzied() const { return frenzy_timer > 0.0f; }
    float FrenzyLeft() const { return frenzy_timer; }
    bool  StandingFast() const { return stand_fast_timer > 0.0f; }
    float StandFastLeft() const { return stand_fast_timer; }
    bool  RapidFire() const { return rapid_timer > 0.0f; }
    float RapidFireLeft() const { return rapid_timer; }
    bool  Invoking() const { return invoke_timer > 0.0f; }
    // Waiting for the shot or the spell they go into, which spends them the
    // moment it is let go -- in Player::UpdateAttack, so a friend's window
    // spends them when the host does.
    bool  Aiming() const { return aim_timer > 0.0f; }
    bool  Overloaded() const { return overload_timer > 0.0f; }
    // Weak Point: shots in a row on one target. `who` is only ever compared.
    int   NoteShotOn(const void* who);
    int   WeakPointStacks() const { return weak_stacks; }
    // Half of a blow paid in mana, while the shield is up and there is mana
    // to pay with. Returns what is left to take in blood.
    int   AbsorbWithMana(int damage);
    // A blow caught on the shield: Riposte is owed.
    void  NoteBlock();
    bool  RiposteReady() const { return riposte_timer > 0.0f; }
    void  SpendRiposte() { riposte_timer = 0.0f; }
    // A shot that landed: Hit and Run.
    void  NoteRangedHit() { hit_run_timer = HIT_RUN_TIME; }
    // A spell cast: Attunement counts casts of one element in a row.
    void  NoteCast(Element e);
    int   AttuneStacks() const { return attune_stacks; }
    void  GainStamina(float amount) { stamina = std::min(MaxStamina(), stamina + std::max(0.0f, amount)); }
    void  GainMana(int amount) { mana = std::clamp(mana + std::max(0, amount), 0, max_mana); }

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

    // --- bags -------------------------------------------------------------------
    // The bag starts at four rows of seven. A satchel, a pack, a rucksack and a
    // haversack each add a row when they are put on, once each and in any
    // order; they are made at a workbench out of a great deal of hide, or found
    // in a chest by someone lucky. What has been put on is the character's and
    // is kept with them, and the size of the bag follows from it.
    const vector<string>& Bags() const { return bags; }
    int  BagSlots() const;
    // Puts on the bag in an inventory slot. False, with the reason, if it is
    // not one, or one like it is already worn.
    bool WearBag(int slot, string& why_not);

    // Consume the item in an inventory slot: food heals, a potion can also
    // restore mana and stamina and boost combat levels. Returns false, with
    // the reason, when it would do nothing.
    bool Eat(int slot);
    bool Consume(int slot, string& why_not);
    // A boost above a level wears off one point every BOOST_DECAY seconds.
    static constexpr float BOOST_DECAY = 45.0f;

    // --- one mouthful at a time ---------------------------------------------------
    // Anything that heals takes a moment to get down. Without this a pack of
    // food was a second health bar: opening the bag stops the world, and
    // nothing stopped twenty-eight suppers being eaten in the time it took to
    // press the button twenty-eight times. The moment is counted in the
    // world's time, so a bag that stops the world stops the chewing too --
    // one bite a visit, however long the visit.
    //
    // A draught that heals nothing -- a strength potion, a mana one -- is not
    // food and is not held to it.
    static constexpr float EAT_COOLDOWN = 1.5f;
    float EatCooldown() const { return eat_cooldown; }

    // --- what is to hand ------------------------------------------------------------
    // The one thing in the pack that can be used without opening it: guard and
    // interact eats or drinks it, guard and sprint steps to the next. It is an
    // item and not a slot, so it follows the food around the bag, and it stays
    // chosen when the last one is eaten so that buying more puts it back.
    const string& QuickItem() const { return quick_item; }
    void SetQuickItem(const string& id) { quick_item = id; }
    // Everything in the pack that could be the quick item, once each, in the
    // order it is carried.
    vector<string> QuickChoices() const;
    // Steps to the next of them; returns what it landed on, or nothing.
    string CycleQuickItem();
    // Eats or drinks it. False with a reason if there is none, or it would do
    // nothing, or the last mouthful is still going down.
    bool UseQuickItem(string& why_not);
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
    // The seat number a stand-in holds: a map only friends are on has a Player
    // for the host that is not there, and it must not answer to a number a real
    // seat could have. Seats are handed out from zero, so the first friend to
    // sit down at the host's own machine is seat 0 -- and so was the stand-in,
    // which made every monster on that map think about both of them.
    static constexpr uint8_t NO_SEAT = 255;
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
    // For the self-test, which has to ask what happens on a nearly empty bar
    // without sprinting a character round a field to get one.
    void  SetStamina(float v) { stamina = std::clamp(v, 0.0f, MaxStamina()); }
    float MaxStamina() const {
        return MAX_STAMINA * (1.0f + talents.Global("stamina") + (meal ? meal->dish_max_stamina : 0.0f));
    }

    // --- a meal --------------------------------------------------------------------
    // What was last eaten that was worth more than the hit points in it, and
    // how long is left of it. One at a time: a second dish replaces the first.
    // What it does is on the ItemDef (see ItemDef::IsDish); holding the pointer
    // rather than a copy keeps the numbers in one place, and the id is what is
    // saved, so a meal survives a reload with whatever is left of it.
    const ItemDef* Meal() const { return meal; }
    float MealLeft() const { return meal_left; }
    void  SetMeal(const ItemDef* dish, float seconds) { meal = dish; meal_left = dish ? seconds : 0.0f; }
    // The levels a meal is holding up, re-applied as the ordinary boost decay
    // tries to walk them back down. Called every frame by Update.
    void  HoldMeal();
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
    // How far the ground under the feet lifts the character, settled toward the
    // terrain's height a little each frame: see World::UpdateElevation.
    float ground_lift = 0.0f;

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

    // --- parrying ---------------------------------------------------------------
    // A dagger has no shield behind it: B raises it to parry instead. The
    // first moment of the stance catches a blow outright -- nothing gets
    // through, and whoever threw it is left reeling -- and after that it is a
    // poor guard, stopping a little of each blow for some breath. Letting go
    // and raising it again opens a fresh moment, though not straight away. A
    // dagger with a shield behind it blocks with the shield, as anything does.
    static constexpr float PARRY_WINDOW        = 0.25f;  // the moment that catches a blow outright
    static constexpr float PARRY_REST          = 0.45f;  // after letting go, before a fresh moment
    static constexpr float PARRY_GUARD         = 0.35f;  // of a blow, what the stance stops after it
    static constexpr float PARRY_STAMINA       = 6.0f;   // what catching one costs
    static constexpr float PARRY_STAGGER       = 0.6f;   // how long the parried reel
    static constexpr float PARRY_HEAVY_STAGGER = 0.4f;   // and longer, if it was a leader's heavy blow
    // Counter (Footwork). Rank one leaves whoever was parried open: staggered
    // longer, and the next blow on them lands harder. Rank two owes a
    // riposte: a light attack within a moment of the parry is a lunge at
    // them that always lands critically.
    static constexpr float OPENING_STAGGER = 0.8f, OPENING_BONUS = 0.3f, OPENING_TIME = 2.5f;
    static constexpr float RIPOSTE_TIME = 1.0f, RIPOSTE_DAMAGE = 1.4f, RIPOSTE_REACH = 90.0f,
                           RIPOSTE_LUNGE = 40.0f;
    bool  ParryStyle() const;               // a dagger in hand and no shield: B parries
    bool  CanParry() const;
    bool  Parrying() const { return parrying; }
    bool  ParryOpen() const { return parrying && parry_age <= PARRY_WINDOW; }
    float ParryAge() const { return parry_age; }
    // Whether a blow from (from_x, from_y) would meet the parry at all.
    bool  ParryFacing(float from_x, float from_y) const;
    // A blow on the parry: caught outright (`parried`), or -- after the
    // moment -- what the poor guard made of it, as TryBlock says it.
    BlockOutcome TryParry(int damage, int attacker_level, float from_x, float from_y, bool& parried);
    // The world says a parry landed on `who` (a monster, or null for a shot):
    // the opening, and the riposte owed, as far as Counter goes.
    void  NoteParry(const void* who);
    // How many blows have been caught outright: the host counts a friend's,
    // and tells their window so it owes the riposte too.
    int   Parries() const { return parries; }
    int   CounterRank() const;
    bool  Opened(const void* who) const { return who && who == opened && opening_timer > 0.0f; }
    void  SpendOpening() { opening_timer = 0.0f; opened = nullptr; }
    bool  RiposteOwed() const { return riposte_owed > 0.0f; }
    float RiposteOwedLeft() const { return riposte_owed; }
    // The lunge itself, at whoever was parried when they are near enough.
    bool  StartRiposte(const World& world);

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

    // --- what a monster has left on them -------------------------------------
    // A spider's poison, a wolf's bite left bleeding, the frost's chill, a
    // hag's charm or befuddling: see World::AfflictPlayer, which rolls for it,
    // and data/statuses.json for what each does. Poison, burns and bleeding
    // hurt over time; a chill slows them and a frost holds them; a charm walks
    // them to whoever cast it and will not let them strike; a confusion turns
    // left into right. Dying, waking and resting clear the lot.
    StatusSet statuses;
    float charm_x = 0.0f, charm_y = 0.0f;   // where a charm draws them
    bool  Afflicted(Status s) const { return statuses.Has(s); }
    bool  Charmed() const { return statuses.Has(Status::Charm); }
    bool  Confused() const { return statuses.Has(Status::Confused); }
    bool  Held() const;                     // frozen: they cannot move or act
    // As Enemy::Afflict: the status that took (a chill on the soaked is a
    // frost), or COUNT if nothing did.
    Status Afflict(Status kind, int blow, const StatusDatabase& db, float from_x, float from_y);
    // A blow landing: whatever the next blow ends (a charm) ends, unless it is
    // `except`, the one the blow brings. True if something did.
    bool  ShakeOff(const StatusDatabase& db, Status except);
    float StatusInvites(Status s) const;    // a soaked player is easier to leave arcing
    float StatusSpeed() const;              // how much their feet are slowed
    // A friend's machine is not told how long each has to run, only which are
    // on them and where a charm is drawing them: enough to steer as the host
    // will, and to draw them.
    void  ShowStatuses(uint16_t bits, float cx, float cy);

private:
    const StatusDatabase* status_db = nullptr;
    // Counts what is on them down, and -- where this machine decides such
    // things -- deals what it owes.
    void TickStatuses(float dt, World& world);
    // The hearts over someone charmed, the stars round someone confused.
    void DrawDazes(SDL_Renderer* r, const Camera& cam) const;
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
    // With a dagger in each hand: whose turn the next light blow is.
    bool  left_hand_next = false;
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
    bool  parrying = false;
    float parry_age = 0.0f, parry_rest = 0.0f;
    float opening_timer = 0.0f, riposte_owed = 0.0f;
    int   parries = 0;
    const void* opened = nullptr;
    const void* riposte_on = nullptr;
    bool  lunging = false;
    float lunge_dx = 0.0f, lunge_dy = 0.0f, lunge_left = 0.0f;
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

    vector<string> bags;          // bag items put on, in the order they were
    void  SizeBag();              // makes the inventory as big as `bags` says
    float ability_cd[SkillTrees::ABILITY_SLOTS] = {};
    string pending_ability;
    float frenzy_timer = 0.0f, stand_fast_timer = 0.0f, aim_timer = 0.0f, rapid_timer = 0.0f;
    float overload_timer = 0.0f, invoke_timer = 0.0f, invoke_bank = 0.0f;
    const void* weak_target = nullptr;
    int   weak_stacks = 0;
    float tumble_timer = 0.0f, war_cry_timer = 0.0f, mana_shield_timer = 0.0f;
    float shield_struck = 99.0f;
    float riposte_timer = 0.0f, hit_run_timer = 0.0f;
    int   attune_stacks = 0;
    Element attune_element = Element::None;

    float boost_timer = 0.0f;
    const ItemDef* meal = nullptr;
    float meal_left = 0.0f;
    int   mana = 0, max_mana = 0;
    float mana_fraction = 0.0f;      // regen accrues in fractions of a point
    Element selected_element = Element::Fire;
    float   battery = 0.0f;               // the lightning's charge, 0 to 1
    string  electric_spell;               // which of the five is on the button
    string  arcane_spell;                 // the ancient spell chosen with 5
    string  held_spell[4];                // fire, water, earth, air: see HeldSpell
    float   since_hurt = 1.0e6f;
    float   reload_left = 0.0f, reload_time = 0.0f;
    int     spell_slot = 0;

    const ItemDatabase* item_db = nullptr;

    vector<LevelUp> pending_levels;
    vector<pair<int,int>> pending_xp;
    float  eat_cooldown = 0.0f;
    string quick_item;

    // Combat XP accrues in fractions; bank it and hand over whole points.
    float xp_fraction[SKILL_COUNT] = {0};
    void  BankXp(int skill, float amount);
};
