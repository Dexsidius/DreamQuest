#pragma once
#include "../systems/status.h"
#include "entity.h"
#include "../systems/combat.h"
#include "../systems/projectile.h"
#include "../world/map.h"

// A leader's heavy attack: a long, telegraphed wind-up and a blow that no
// shield stops. Only what carries a "heavy" block in data/enemies.json has one.
struct HeavyAttackDef {
    bool  enabled   = false;
    float windup    = 1.5f;   // seconds the charge takes, bar filling the whole way
    float damage    = 2.4f;   // times the monster's own max hit
    float reach     = 1.3f;   // times its attack range
    float width     = 1.6f;   // times its body's width
    float cooldown  = 9.0f;   // seconds between one and the next
    float opening   = 3.5f;   // seconds into a fight before the first
    float knockback = 220.0f;
    // What it can leave on whoever it lands on: a brute's slam concusses. None
    // given, it is the monster's own `on_hit` at twice the chance.
    StatusProc status;
};

// One of a boss's own moves, beside its swing and its heavy: what makes a
// fight with it its own. Each has a tell -- `windup` seconds of it glowing and
// turning to follow, then committed -- then goes on for `active` seconds and
// stands for `recover`, and is not tried again for `cooldown`.
//
//   charge   runs at them along the line it committed to, `speed` times its
//            pace, and strikes once if it reaches them (Ashen Vanguard)
//   double   two quick strikes, one at the start and one halfway (a fury)
//   spin     whirls in place, edging after them, striking everything within
//            `reach` pixels every half second (the Forge Demon)
//   flame    breathes a fan of `reach` shots of `shot` across `width` degrees
//   shot     the same, quietly: one thing spat (`reach` 1) -- a spider's web
//   sweep    one lash of everything within `reach` pixels, across `width`
//            degrees (360, all round): the Anchor's threads
//   howl     no blow: every one of its own kind within 320 px is roused,
//            quicker and harder for `active` seconds (the wolves)
//
// `min_gap`/`max_gap` is how far off it has to be to try it, and `phase` 2
// keeps it for the second half of the fight (EnemyDef::phase2).
struct EnemyMove {
    string kind;
    float  windup = 0.8f, active = 0.6f, recover = 0.6f, cooldown = 8.0f, opening = 2.5f;
    float  damage = 1.2f;          // times its max hit
    float  reach = 120.0f;         // see the kinds
    float  width = 360.0f;         // degrees, for flame and sweep
    float  speed = 3.0f;           // a charge's, times its pace
    float  knockback = 140.0f;
    float  min_gap = 0.0f, max_gap = 100000.0f;
    int    phase = 0;
    string clip, shot;
    StatusProc status;
    // A spin's: seconds between its blows all round, and its pace while it
    // whirls, as a share of its own (the Forge Demon edges after you at 0.45;
    // the Shear Mannequin whirls across the floor).
    float  tick = 0.5f, pace = 0.45f;
    // The frame of `clip` its blow lands on, when that matters: the clip is
    // played at whatever speed puts that frame at the end of the wind-up --
    // the snip's blades shut on the frame they are seen to shut. -1: as it is.
    int    strike_frame = -1;
    // Of the damage its blow does, this share back as its own health; below
    // nought, the monster's own (EnemyDef::lifesteal). The Ashlord's quick
    // fury feeds it less than its claws do (73).
    float  lifesteal = -1.0f;
    // "gusts": a ring of `reach` gusts of `shot` every `tick` while it is
    // active, each with a gap `width` degrees wide turned somewhere new.
    // "beam": a red line from it to where the player stands, for the wind-up,
    // and then `shot` along it. "slam": a red mark where the player stands,
    // and down onto it, `reach` round. "summon": the next of `shot`_1.._`reach`
    // set; the posts that come with it wait on those (EnemySpawnDef when).
};

// What a boss turns into at half its health (or wherever `at` says): quicker
// on its feet and with its blows, in its own colours -- and, with
// `fire_trail`, leaving burning ground ahead of every blow that lands.
struct PhaseTwoDef {
    bool  enabled = false;
    // And a flag set the moment it turns: the rival at half health, when
    // Vexel takes a hand (70).
    string flag;
    float at = 0.5f;
    float speed = 1.3f, cooldown = 0.7f;
    bool  fire_trail = false;
    SDL_Color tint{255, 255, 255, 255};
};

// Stat block for one kind of monster, from data/enemies.json.
struct EnemyDef {
    string id, name, sprite;
    int   hp = 10;
    int   attack_level = 1, strength_level = 1, defence_level = 1;
    int   attack_bonus = 0, strength_bonus = 0, defence_bonus = 0;
    float speed = 42.0f;
    float aggro_range = 150.0f;
    float attack_range = 26.0f;
    float attack_cooldown = 1.6f;
    // What it throws, and from how far. Empty for everything that fights with
    // its hands. A shooter holds its distance rather than closing: see Chase.
    string shoots;
    // A caster's spells: thrown in turn, one each time it shoots, so the Swamp
    // Hag's rot, beguiling and befuddling hexes come round in order and no
    // dice are thrown to choose. `shoots` is the first of them.
    vector<string> spells;
    // What its throat sounds like, where it has one of its own: "dragon" bites
    // with a growl and a snap of the jaws (Sfx::Bite) and roars as it winds up
    // its heavy blow (Sfx::Roar), where everything else is heard swinging.
    // Its breath is its projectile's to say (ProjectileDef::breath).
    string voice;
    bool   DragonVoice() const { return voice == "dragon"; }
    // What its blows can leave on the player: a spider's poison, a wolf's
    // bleeding bite, the frost's chill. Its shots carry their own, in
    // data/projectiles.json.
    StatusProc on_hit;
    float shoot_range = 0.0f;
    float shoot_cooldown = 2.4f;
    float xp_multiplier = 1.0f;
    string loot_table;
    // A light where its eyes are, once it is awake: colour (alpha 0 none) and
    // how high up it is.
    SDL_Color eye_light{0, 0, 0, 0};
    float     eye_height = 30.0f;
    string kill_target;            // what Kill quest objectives match on
    SDL_FRect foot_box{-9.0f, -12.0f, 18.0f, 12.0f};
    SDL_FRect body_box{-14.0f, -42.0f, 28.0f, 42.0f};
    float scale = 1.0f;
    bool  is_boss = false;
    // A story's boss (Act I's Ashen Vanguard, Forge Demon and Anchor): a boss
    // to look at and to fight -- its bar, its roar, its weight, the statuses it
    // shrugs -- but met once, so it is no part of what a boss gives for its
    // first kill and its fifteenth (Talents::SlayBoss), nor kept once a day.
    bool  story_boss = false;
    bool  Boss() const { return is_boss || story_boss; }
    // It can get into water. Only waterfowl do, and only where the map has
    // said which of its collision is water -- everywhere else a pond is a
    // wall to everything, which is how it has always been.
    bool  swims = false;
    // Whether it potters between the water and the bank when left alone, the
    // way the ducks do. Something that swims to hunt -- a gator, the drowned --
    // keeps to its post instead, in the water it came out of.
    bool  paddles = false;
    // What the creature is aligned to, for the elemental matchup. Untyped
    // monsters take normal damage from everything.
    Element element = Element::None;
    // Statuses it cannot take: the dead do not bleed and cannot be poisoned.
    // Besides these, nothing made of fire can be set burning.
    bool immune[STATUS_COUNT] = {};
    // Multiplied over the sprite: the dream's nightmares are the waking
    // world's orcs and boars, drawn in the colours of a bad night.
    SDL_Color tint{255, 255, 255, 255};
    HeavyAttackDef heavy;
    vector<EnemyMove> moves;
    PhaseTwoDef phase2;
    // It never moves from where it stands, and nothing moves it: the Anchor.
    bool  rooted = false;
    // A pack animal: while its swing cools it circles whoever it is after
    // rather than standing off, so a pack comes from every side.
    bool  circles = false;
    // After a spin or a flame its seams are soft for this long: every blow
    // lands twice as hard (Enemy::Weak), and it glows to say so.
    float weak_after = 0.0f;
    // Of the damage its blows do, this share back as its own health: the
    // Ashlord's blood-dipped claws (73).
    float lifesteal = 0.0f;
    // Nothing reaches it -- no blow, no shot, no lock -- and nothing it does
    // can be stopped by fighting it: Vexel on the college steps, taking a hand
    // in the rival's fight (70). The story ends it.
    bool  untouchable = false;
};

class EnemyDatabase {
public:
    bool Load(const string& path);
    const EnemyDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }
    const map<string, EnemyDef>& All() const { return defs; }

private:
    map<string, EnemyDef> defs;
};

// Simple, readable monster AI: sit at your post; come for whoever walks into
// range or draws blood, from however far; swing when close enough; and go home
// once the chase has gone on too long with no fight in it.
//
// What starts a fight is either of two things. Someone inside `aggro_range`:
// it has seen them. Or taking damage -- an arrow from across a field, a wound
// still bleeding -- which it answers from any distance: it used to stand and be
// shot by anyone outside the range it could see.
//
// What ends one is the ground it has covered since anything last happened. It
// used to be how far it had got from its post, so a monster fought at the edge
// of that ring turned in the middle of a swing and walked home. Now every
// stride of a chase is counted, and anything that is a fight -- a blow taken, a
// swing begun -- starts the count again; when the count reaches its leash
// (`ChaseBudget`) and nothing has happened, it gives up. A spawn's `leash` is
// that budget: how far it will run after you, not how far it may be from home.
class Enemy : public Entity {
public:
    // Heavy: a leader winding up and delivering its heavy attack; see
    // HeavyAttackDef. The charge, the blow and a moment to recover from it.
    enum class State { Idle, Chase, Attack, Hurt, Dead, Return, Heavy, Move };

    void Init(const EnemyDef* def, const EnemySpawnDef& spawn, const GameContext& ctx);
    void Update(float dt, World& world, const GameContext& ctx) override;
    void Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const override;

    CombatProfile Profile() const;
    void OnKilled(World& world, const GameContext& ctx);

    // Respawn bookkeeping, run by the world while the body is gone.
    bool  AwaitingRespawn() const { return state == State::Dead && respawn_at > 0.0f; }
    void  TickRespawn(float dt);
    bool  ReadyToRespawn() const { return state == State::Dead && respawn_at <= 0.0f && respawn_delay > 0.0f; }
    void  Revive();

    const EnemyDef* Def() const { return def; }
    // Which of the map's posts it keeps, so the world can remember a boss by
    // where it stood. And the state a boss already killed today is put in when
    // its map is loaded again: there, because everyone who counts monsters
    // counts them by their place in this list, and gone.
    int  post = -1;
    void LieDead();
    // Dormant: stood on its pedestal, unseeing, until `wake_flag` is set or it
    // is struck -- then it steps down (`perch` falling to nothing) and comes
    // on. See EnemySpawnDef::dormant.
    bool   dormant = false;
    string wake_flag;
    float  perch = 0.0f;         // drawn this far up while it stands on its pedestal
    void   WakeUp();
    // A story's post (EnemySpawnDef squad/appear): the squad it is one of, and
    // -- held back until its `when` holds -- when it comes: `appear_in` counts
    // down to it, out of smoke. A post held back or still coming is not down.
    string squad;
    bool   held_back = false;
    float  appear_after = 0.0f;
    float  appear_in = -1.0f;
    FlagCond appear_when;
    bool   Pending() const { return held_back || appear_in >= 0.0f; }
    // A night visitor: see EnemySpawnDef::night. What it needs of its post to
    // be asked, each frame, whether tonight is one of its nights.
    bool   night = false;
    float  night_chance = 1.0f;
    string night_group;
    // Dawn: it goes to ground. Not a death -- no cry, no loot, nothing counted
    // -- it stands as it was and fades, the way a body does, and is gone.
    void GoToGround();
    Element ElementOf() const { return def ? def->element : Element::None; }
    const string& TypeId() const { return type_id; }
    State CurrentState() const { return state; }
    // Set while the player is engaged, so the HUD can show a target bar.
    bool  Engaged() const { return state == State::Chase || state == State::Attack || state == State::Heavy; }
    // Drawing blood: it comes for whoever did it, from wherever they did it.
    // `seat` is who, for a world with friends in it, or -1 when it cannot be
    // said -- a wound bleeding out.
    static constexpr float GRUDGE_TIME = 6.0f;
    void  Provoke(int seat = -1);
    bool  Provoked() const { return provoked; }
    // Whoever it is angriest with just now, or -1.
    int   GrudgeSeat() const { return grudge > 0.0f ? grudge_seat : -1; }
    // How far it will run after someone with nothing happening, and how far it
    // has run: half as far again as its leash, which is about what the old
    // ring let a straight chase from its post come to, and never so short that
    // it is no chase at all.
    float ChaseBudget() const { return std::max(200.0f, leash * 1.5f); }
    float ChaseRun() const { return chase_run; }

    // --- its own moves (EnemyMove) -----------------------------------------------------
    // Soft after a spin or a flame: blows land WEAK_DAMAGE times as hard.
    static constexpr float WEAK_DAMAGE = 2.0f;
    bool  Weak() const { return weak_left > 0.0f; }
    bool  PhaseTwo() const { return phase_two; }
    // Roused by a packmate's howl: quicker and harder for this long.
    float enraged = 0.0f;

    // --- heavy attack -------------------------------------------------------------
    // 0 to 1 through the wind-up, for the bar over its head and the red glow;
    // 0 when it is not charging.
    float HeavyCharge() const;
    bool  ChargingHeavy() const { return HeavyCharge() > 0.0f; }
    // The rectangle the blow lands in, from where the monster stands facing
    // the way it is facing. Wider and longer than an ordinary swing.
    SDL_FRect HeavyHitbox() const;
    // Where its blows land: see StrikeArc. A swing reaches as far as the range
    // it was begun from, so one begun in range lands on whoever stands still.
    StrikeArc SwingArc() const;
    StrikeArc HeavyArc() const;
    // The damage it will do before any punishment for blocking it.
    int   HeavyDamage(std::mt19937* rng) const;
    // How long until the next one may start.
    float HeavyCooldown() const { return heavy_timer; }
    static constexpr float HEAVY_RECOVER = 0.7f;   // standing after the blow
    static constexpr float HEAVY_LOCK    = 0.7f;   // share of the wind-up it keeps turning to follow

    // --- staggering -------------------------------------------------------------
    // Reeling from a blow -- the Crushing Blow's -- for this long: no moving,
    // no swinging. A leader braced in its heavy's wind-up shrugs it off, and
    // the dead are past it.
    // Reeling for `seconds`. A heavy blow being wound up shrugs it off -- unless
    // `force`: the heavy blow itself caught on a parry.
    void  Stagger(float seconds, bool force = false);
    // What a player's abilities leave on it. Marked, it takes a quarter more
    // from every blow, whoever's. Sundered, its defence is down by a third.
    static constexpr float MARK_DAMAGE = 0.25f, SUNDER_SHARE = 0.67f;
    void  Mark(float seconds)   { marked = std::max(marked, seconds); }
    void  Sunder(float seconds) { sundered = std::max(sundered, seconds); }
    bool  Marked() const   { return marked > 0.0f; }
    bool  Sundered() const { return sundered > 0.0f; }
    float marked = 0.0f, sundered = 0.0f;
    bool  Staggered() const { return state == State::Hurt; }
    // A wound left open: this much more, bled out over BLEED_TIME. A second
    // wound adds to what is left rather than starting over. (It is a status
    // like the rest now -- see below -- and this is the way to open one for a
    // known amount, which is what Open Wounds does.)
    static constexpr float BLEED_TIME = 4.0f;
    void  Bleed(float damage);
    bool  Bleeding() const { return statuses.Has(Status::Bleed); }

    // --- statuses -----------------------------------------------------------------
    // What blows have left on it: see systems/status.h. `Afflict` is a blow of
    // `blow` damage leaving `kind`; it answers with what was actually left,
    // which may be another (a chill on something soaked is frozen) or nothing
    // (immune, blocked, dead).
    Status Afflict(Status kind, int blow, const StatusDatabase& db);
    bool   Afflicted(Status s) const { return statuses.Has(s); }
    bool   ImmuneTo(Status s) const;
    // How much harder this element bites for what is on it: the wind, on
    // something soaked.
    float  StatusWeakness(Element e) const;
    // How much more likely a status is to take, for what is already on it:
    // see StatusDef::invites.
    float  StatusInvites(Status s) const;
    // What is on it slows it: its pace, and the gap between its swings.
    float  MoveSpeed() const;
    float  AttackCooldown() const;
    StatusSet statuses;
    // Stand Fast: for this long it is after that seat and nobody else.
    void  Taunt(int seat, float seconds) { taunt_seat = seat; taunted = seconds; }
    // Held back for a while to this share of its pace and of the gap between
    // its swings: inside a Sanctuary, Challenged by an Unmoving Lamp. The
    // slowest one going holds.
    void  Slow(float seconds, float share) {
        if (slowed <= 0.0f || share < slow_share) slow_share = share;
        slowed = std::max(slowed, seconds);
    }
    bool  Slowed() const { return slowed > 0.0f; }
    float slowed = 0.0f, slow_share = 1.0f;
    // A burn of the player's own made longer and fiercer (Embers): `seconds`
    // more of it, `fiercer` times as much a second. Nothing on it, nothing done.
    void  Stoke(Status kind, float seconds, float fiercer) {
        const int i = static_cast<int>(kind);
        if (statuses.left[i] <= 0.0f) return;
        statuses.left[i] += std::max(0.0f, seconds);
        statuses.rate[i] *= std::max(0.0f, fiercer);
    }
    int   TauntedBy() const { return taunted > 0.0f ? taunt_seat : -1; }
    float taunted = 0.0f;
    int   taunt_seat = -1;

    // --- health bar -------------------------------------------------------------
    // Hidden until the player first attacks this monster -- a hit, a miss or a
    // hit for nothing all count -- then drawn over its head until the corpse
    // goes. A revived monster starts hidden again.
    void  RevealHealthBar() { bar_revealed = true; }
    bool  HealthBarVisible() const {
        return bar_revealed && !(state == State::Dead && corpse_timer > 0.0f);
    }
    // Exactly hp / max_hp. The bar's fill is this; nothing smooths it.
    float HealthFraction() const {
        return max_hp > 0 ? std::clamp(static_cast<float>(hp) / max_hp, 0.0f, 1.0f) : 0.0f;
    }
    // A lighter band marking damage just taken, never below HealthFraction();
    // it holds for a moment after a hit and then drains down to meet the fill.
    float HealthTrail() const { return std::max(bar_trail, HealthFraction()); }

    // --- corpse -----------------------------------------------------------------
    // After its death animation the body holds briefly, fades, and is gone. The
    // entity stays in the world's list, invisible, to count down its respawn.
    bool  CorpseGone() const;

    // --- lurking ----------------------------------------------------------------
    // Something that waits under the water for whoever comes too close to the
    // edge: see EnemySpawnDef::lurk. Under, it is not drawn, not struck and not
    // targeted -- a ripple on the surface is all there is of it -- and it
    // comes up out of the water when a player is within LURK_WAKE. Left alone
    // at home in the water for a while, it goes back under, whole again.
    //
    // How far out it is travels to a friend's machine as its alpha, which is
    // what alpha already meant for a body fading: nothing new on the wire.
    static constexpr float LURK_WAKE = 104.0f;   // how close is too close
    static constexpr float LURK_RISE = 0.6f;     // seconds to come up
    static constexpr float LURK_SINK = 0.8f;     // and to go back down
    static constexpr float LURK_WAIT = 3.5f;     // idle at home before it does
    bool  Lurks() const { return lurks; }
    bool  Submerged() const { return lurks && emerge <= 0.0f; }
    bool  Afloat() const { return afloat; }
    // Not all the way out of the water, coming or going: nothing can touch it.
    bool  Hidden() const { return lurks && state != State::Dead && emerge < 0.999f; }
    // How it goes when it dies, for the sprite shader: 0 it fades, 1 the dead
    // crumble to dust, 2 what burns goes to embers, 3 what is hardly there
    // goes up into the air.
    int   DissolveKind() const;
    float Emerged() const { return lurks ? emerge : 1.0f; }
    Uint8 CorpseAlpha() const;

    // How much stronger than its kind this one is: 1 is the stat block in
    // data/enemies.json, and each step above that is the bump Init applies.
    // It is not what the player is shown -- see ShownLevel.
    int   level = 1;

    // The number over its head, and the one the bestiary prints: what this
    // thing would be as a Combat level, worked out from the stats it actually
    // fights with. A dire bear hits like Combat 62 and used to say "Lv 1",
    // because the spawn's level is a nudge on a stat block and never was a
    // measure of anything; the Brackenwood is advised at Combat 20 and was
    // full of things that called themselves level 1 to 3.
    //
    // Nothing about a fight changes with this: the same stats, the same
    // damage, the same hit points. Only the number is honest now.
    int   ShownLevel() const;
    static int ShownLevelOf(const EnemyDef& def, int spawn_level);
    // The other way: the least spawn level at which `def` shows `shown` or
    // more. For a post that says how strong it should look (EnemySpawnDef::
    // shown) and leaves the stat block to be scaled to it.
    static int LevelToShow(const EnemyDef& def, int shown);
    // The spawn level a post's monster of this kind is made at, before the
    // day's spread: the written level, or -- with EnemySpawnDef::shown -- the
    // one it takes to look that strong.
    static int PostLevel(const EnemyDef& def, const EnemySpawnDef& spawn);
    // How much more health a monster shown at `shown` has than its stat block
    // says: TOUGH_LOW at level 1, rising in a straight line to TOUGH_HIGH at
    // TOUGH_FULL and flat after it. Everything died too fast, and nowhere more
    // than high up, where one charged blow is a quarter of a bar.
    //
    // Only the pool: ShownLevelOf still reads the stat block's hit points, so
    // no number over any head moves, and a monster posted "at 60" is still the
    // monster the level ladder says is 60. It is the same thing, harder to kill.
    static float Toughness(int shown);
    static constexpr float TOUGH_LOW  = 1.3f;
    static constexpr float TOUGH_HIGH = 2.0f;
    static constexpr int   TOUGH_FULL = 50;

    // --- roaming --------------------------------------------------------------
    // A monster that walks the map instead of keeping a post: see
    // EnemySpawnDef::route. It goes round the loop a point at a time, and its
    // home is the point it is making for -- so a chase it gives up brings it
    // back to the loop where it left it, and it goes on from there.
    vector<SDL_FPoint> route;
    int   route_at = 0;               // the point it is making for
    bool  Roams() const { return !route.empty(); }
    // Stood on point `at` of its loop, making for the next.
    void  StartRoute(int at);
    static constexpr float ROAM_PACE  = 0.5f;    // of its running speed: it is walking
    static constexpr float ROAM_STUCK = 3.0f;    // seconds getting no nearer before it gives a point up

    float home_x = 0, home_y = 0;

    // --- co-op ------------------------------------------------------------------
    // Whose it is after: the seat number, kept from frame to frame so two
    // friends standing either side of a boar do not have it spinning.
    int   target_seat = -1;
    // Drawn from what the host says rather than thought about here: a monster
    // as a friend's machine sees it. Never updated, only posed.
    bool  puppet = false;
    struct Posed {
        float x = 0, y = 0;
        uint8_t facing = 0, state = 0, frame = 0, heavy = 0, alpha = 255;
        uint8_t statuses = 0;          // StatusSet::Bits: what a friend's machine draws on it
        bool  hurt = false, bar = false;
        bool  weak = false;            // its seams soft (Weak): drawn glowing
        int   hp = 0;
        string clip;
    };
    void Pose(const Posed& p);
    // The same, the other way: what the host tells.
    Posed Told() const;

private:
    void SetState(State s);
    // Heard as a breath leaves the mouth (ProjectileDef::breath).
    void Breathe(Element e);
    // A share of what a blow of its took, back as its own health, shown.
    void Feed(World& world, int taken, float share);
public:
    // See EnemyDef::untouchable.
    bool Untouchable() const { return def && def->untouchable; }
private:

    const EnemyDef* def = nullptr;
    const StatusDatabase* status_db = nullptr;     // what the statuses on it do, from the context it was made in
    string type_id;
    State  state = State::Idle;

    float leash = 220.0f;
    bool  provoked = false;       // it has been hurt: it does not need to see them
    bool  lurks = false;          // see Hidden
    float emerge = 1.0f;          // 0 under the water, 1 all the way out
    bool  rising = false;         // coming up, and cannot act until it is
    float sink_wait = 0.0f;       // how long it has sat at home with nobody near
    float chase_run = 0.0f;       // ground covered in this chase since anything happened
    float grudge = 0.0f;
    int   grudge_seat = -1;
    float attack_timer = 0.0f;
    float state_timer = 0.0f;
    float hurt_for = 0.0f;         // how long the current reel lasts, past the usual flinch
    float respawn_delay = 25.0f;
    float respawn_at = 0.0f;
    float wander_timer = 0.0f;
    float wander_dx = 0, wander_dy = 0;

    // --- waterfowl -------------------------------------------------------
    // A duck does not drift the way a boar does. It picks somewhere to be --
    // a patch of bank, or a bit of open water -- waddles there in a straight
    // line, and pokes about for a while before deciding on somewhere else.
    // Half of those somewheres are wet, so it spends its day going in and
    // out of the pond of its own accord.
    bool  afloat = false;         // over water this frame
    unsigned casts = 0;           // spells thrown, for which is next: see EnemyDef::spells
    bool  has_goal = false;
    float goal_x = 0.0f, goal_y = 0.0f;
    bool  goal_wet = false;       // the goal is a place in the water
    float goal_timer = 0.0f;      // until it thinks of somewhere else
    float goal_dist = 0.0f;       // closest it has come to the goal so far
    float stuck_for = 0.0f;       // how long it has made no headway
    // Picks somewhere to go: wet or dry as asked, within the leash of home,
    // and somewhere it could actually float or stand. False if it cannot find
    // one, which is what happens to a duck on a map with no pond in it.
    bool  PickHaunt(World& world, const GameContext& ctx, bool wet);
    // The whole of the waterfowl idle: returns the step to take this frame.
    void  Paddle(World& world, const GameContext& ctx, float dt,
                 float& move_x, float& move_y);
    // The whole of a roamer's idle, the same way.
    void  Roam(float dt, float& move_x, float& move_y);
    void  NextWaypoint();
    float roam_best = 1e9f;       // the nearest it has come to the point it is making for
    float roam_stuck = 0.0f;      // how long it has come no nearer

    float heavy_timer = 0.0f;     // until the next heavy attack may start
    // The move under way (State::Move), how far into it, which way it is
    // committed to, and whether it has struck; and when each may next start.
    int   move_i = -1;
    float move_t = 0.0f, move_dx = 0.0f, move_dy = 1.0f, spin_tick = 0.0f;
    bool  move_begun = false, move_hit = false, move_second = false;
    // A snip's: where the blades opened, which is where they shut -- and a
    // slam's mark, and where a beam was aimed. A slam's pace across to it.
    float snip_x = 0.0f, snip_y = 0.0f, slam_speed = 0.0f;
    vector<float> move_ready;
    bool  phase_two = false;
    float weak_left = 0.0f;
    // Starts a move whose time has come, from `gap` away; true if one did.
    bool  TryMove(World& world, const GameContext& ctx, float gap);
    void  UpdateMove(float dt, World& world, const GameContext& ctx, float dx, float dy, float dist, float gap,
                     float& move_x, float& move_y);
    // Burning ground ahead of a blow, in the second phase of a boss that leaves it.
    void  FireTrail(World& world);
    float shoot_timer = 0.0f;     // until it may throw again
    bool  shooting = false;       // this attack is a shot, not a swing
    bool  heavy_landed = false;   // the blow has been delivered this heavy
    bool  swing_landed = false;   // one hit per swing
    float swing_timer = 0.0f;
    bool  swinging = false;
    // Who a swing, or a heavy, is coming for: the seat of whoever was in the
    // arc of it as it began -- as a heavy was committed -- or -1 if nobody
    // was. Out of it by the time it lands, they dodged it (World::Dodged).
    // Asked once each.
    int   swing_at = -1, heavy_at = -1;
    bool  swing_asked = false, heavy_asked = false;

    bool  bar_revealed = false;
    float bar_trail = 1.0f;
    float bar_trail_hold = 0.0f;  // pause before the trail starts draining
    int   last_hp = 0;            // to notice a hit landing between updates
    float corpse_timer = 0.0f;    // time since the death animation finished
};

// Screen pixels of fill for a health bar `inner` pixels wide. Exact to the
// nearest pixel, except that a living monster always shows some red and a
// wounded one never shows a full bar: plain rounding would draw a boar on 1 hp
// of 100 as already dead, and one scratched for 1 of 95 as untouched.
inline int HealthBarFillPixels(int hp, int max_hp, int inner) {
    if (inner <= 0 || max_hp <= 0 || hp <= 0) return 0;
    if (hp >= max_hp) return inner;
    const int fill = static_cast<int>(std::lround(static_cast<double>(inner) * hp / max_hp));
    return std::clamp(fill, 1, std::max(1, inner - 1));
}
