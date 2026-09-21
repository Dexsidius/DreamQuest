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
    float xp_multiplier = 1.0f;
    string loot_table;
    string kill_target;            // what Kill quest objectives match on
    SDL_FRect foot_box{-9.0f, -12.0f, 18.0f, 12.0f};
    SDL_FRect body_box{-14.0f, -42.0f, 28.0f, 42.0f};
    float scale = 1.0f;
    bool  is_boss = false;
    // It can get into water. Only waterfowl do, and only where the map has
    // said which of its collision is water -- everywhere else a pond is a
    // wall to everything, which is how it has always been.
    bool  swims = false;
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
    enum class State { Idle, Chase, Attack, Hurt, Dead, Return, Heavy };

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
    void  Stagger(float seconds);
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
    // What is on it slows it: its pace, and the gap between its swings.
    float  MoveSpeed() const;
    float  AttackCooldown() const;
    StatusSet statuses;
    // Stand Fast: for this long it is after that seat and nobody else.
    void  Taunt(int seat, float seconds) { taunt_seat = seat; taunted = seconds; }
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
        int   hp = 0;
        string clip;
    };
    void Pose(const Posed& p);
    // The same, the other way: what the host tells.
    Posed Told() const;

private:
    void SetState(State s);

    const EnemyDef* def = nullptr;
    const StatusDatabase* status_db = nullptr;     // what the statuses on it do, from the context it was made in
    string type_id;
    State  state = State::Idle;

    float leash = 220.0f;
    bool  provoked = false;       // it has been hurt: it does not need to see them
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

    float heavy_timer = 0.0f;     // until the next heavy attack may start
    bool  heavy_landed = false;   // the blow has been delivered this heavy
    bool  swing_landed = false;   // one hit per swing
    float swing_timer = 0.0f;
    bool  swinging = false;

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
