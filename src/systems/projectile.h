#pragma once
#include "../headers.h"
#include "combat.h"
#include "element.h"
#include "status.h"

class TextureCache;
class Camera;

// -----------------------------------------------------------------------------
//  Elements, projectiles and the ground they leave behind.
//
//  One projectile type covers arrows and spells alike: everything that differs
//  between a bowshot and a firebolt -- speed, reach, how many targets it passes
//  through, what it leaves on the ground -- is data in data/projectiles.json.
//
//  The art is drawn along the direction of travel, so one arrow image covers
//  every angle rather than needing a frame per facing. A spell is a strip of
//  frames and not a still -- a fireball's flames stream, a gust's lines run --
//  and sheds things as it flies: see Mote, below.
// -----------------------------------------------------------------------------

struct ProjectileDef {
    string id;
    string sprite;
    float speed  = 260.0f;      // world pixels per second
    float life   = 1.6f;        // seconds before it expires
    float radius = 6.0f;        // hit radius
    float scale  = 1.0f;
    // Degrees added when drawing, so art that was authored pointing some other
    // way still lines up with the direction of travel.
    float sprite_angle = 0.0f;
    bool  spin = false;         // tumbles instead of pointing along its path

    // --- how it is drawn -------------------------------------------------------
    // The sprite is a strip of this many frames read left to right, played at
    // `fps`. One frame is a still, which is what an arrow is.
    int   frames = 1;
    float fps    = 12.0f;
    // The point of a frame that sits on the projectile's own position, and that
    // it is turned about: the head of a fireball, not the middle of its tail.
    // Negative is the middle of the frame.
    float pivot_x = -1.0f, pivot_y = -1.0f;
    // Drawn as it stands, whichever way it is going. A ball of water has its
    // highlight top-left flying east or west; turned with its flight it would
    // carry the sun round with it.
    bool  upright = false;
    // What streams off the back of something upright: a second strip, drawn
    // under the first and turned to the way it is going.
    string tail;
    int    tail_frames = 1;
    float  tail_pivot_x = -1.0f, tail_pivot_y = -1.0f;
    // A light added under it by day, this many world pixels across. (By night
    // anything elemental is a light already: see World::CollectLights.)
    float glow = 0.0f;
    // What it sheds as it flies and throws up where it lands -- embers, drops,
    // chips of stone, streaks of air. Its own element's, unless "trail" says
    // otherwise; "none" sheds nothing.
    Element shed = Element::None;
    // The colour of the sparks an ancient spell sheds: acid is not violet.
    // Alpha nothing for the school's own.
    SDL_Color shed_color{0, 0, 0, 0};

    // --- what it can leave on what it strikes -----------------------------------
    // A status, some of the time: see systems/status.h. What it leaves on the
    // ground -- a fire's burning patch, a stone's eruption -- carries the same.
    StatusProc status;
    // This share of the damage it does comes back to whoever threw it, as
    // health: the Vampiric Touch.
    float leech = 0.0f;
    // The share of a target's Defence it goes past: a crossbow's bolt.
    float armour_pierce = 0.0f;

    int   pierce    = 0;        // extra targets it passes through
    float knockback = 40.0f;
    // Radians a second it may turn toward the monster it was loosed at, so a
    // shot at something moving still arrives. Zero flies straight.
    float homing    = 0.0f;

    // --- what it does when it meets a wall -----------------------------------
    // Most things stop. A few ricochet: bounces is how many times, and each one
    // costs bounce_damping of the remaining speed, so a shot that rattles down
    // a corridor eventually settles instead of pinging forever.
    int   bounces        = 0;
    float bounce_damping = 0.25f;
    // Radius of the mark left where it struck, in world pixels. Zero draws
    // nothing, which is right for something that is only ever cast in the open.
    float impact_size    = 5.0f;
    Element element = Element::None;
    SDL_Color tint{255, 255, 255, 255};

    // --- signature behaviours ------------------------------------------------
    // Fire: leaves burning ground where it lands.
    float patch_time   = 0.0f;
    float patch_radius = 0.0f;
    int   patch_damage = 0;
    float patch_tick   = 0.5f;

    // Earth: bursts where it stops, after a short wind-up.
    bool  erupts       = false;
    float erupt_radius = 0.0f;
    float erupt_delay  = 0.0f;
    int   erupt_damage = 0;
};

class ProjectileDatabase {
public:
    bool Load(const string& path);
    const ProjectileDef* Get(const string& id) const;
    bool Has(const string& id) const { return defs.count(id) > 0; }
    const map<string, ProjectileDef>& All() const { return defs; }

private:
    map<string, ProjectileDef> defs;
};

// One projectile in flight.
struct Projectile {
    const ProjectileDef* def = nullptr;
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float angle = 0.0f;          // radians of travel, for drawing
    float spin_angle = 0.0f;
    float life = 0.0f;
    float damage_mult = 1.0f;
    CombatProfile owner;
    AttackStyle style = AttackStyle::Ranged;
    Element element = Element::None;
    bool  from_player = true;
    // Which player: the seat at this machine, or a friend's by number. What
    // it hits is theirs -- the experience, the chain, the kill.
    bool    owner_local = true;
    uint8_t owner_seat = 0;
    uint32_t net_id = 0;
    // Which cast let it go, so the cast can be paid for when this lands on
    // something -- see World::OpenCast. Nothing, for an arrow or a monster's.
    uint32_t cast_id = 0;
    int   pierce_left = 0;
    int   bounces_left = 0;
    bool  finished = false;
    // Set when a wall stopped it, so the caller can put the impact -- and
    // anything the projectile leaves behind -- on the surface rather than
    // wherever the last movement step happened to land.
    bool  hit_wall = false;
    // The monster the player's targeting had when this was loosed, for homing.
    // Checked against the world's list before use; null flies straight.
    const void* target = nullptr;
    // What the player's talents added: more knockback, and steering for a shot
    // whose own data has none.
    float knockback_mult = 1.0f;
    float extra_homing = 0.0f;
    bool  sure_crit = false;     // loosed with Take Aim: it strikes critically, whatever the dice say
    // Thrown for practice, at a training dummy: it flies `show_left` pixels,
    // bursts there, and on the way touches nobody -- not a monster, not a
    // player who walks through the line of it -- and leaves nothing burning.
    bool  show = false;
    float show_left = 0.0f;
    // How far the ground it was loosed from lifts it on screen; found the first
    // time it is drawn, which is why drawing may write it.
    mutable float lift = -1.0f;
    // Entities already struck, so one shot cannot hit the same target twice.
    vector<const void*> already_hit;

    SDL_FRect Bounds() const {
        return {x - def_radius(), y - def_radius(), def_radius() * 2, def_radius() * 2};
    }
    float def_radius() const { return def ? def->radius : 6.0f; }
};

// Something small and short-lived that a spell sheds or throws up: an ember, a
// drop, a chip of stone, a streak of air, a ring opening on the ground. Only
// ever for show -- nothing in the game asks where one is -- so they are made
// the same way on the host's machine and on a friend's, by each for itself.
struct Mote {
    enum class Kind : uint8_t { Speck, Streak, Ring };
    Kind  kind = Kind::Speck;
    float x = 0, y = 0, vx = 0, vy = 0;
    float gravity = 0.0f;        // world pixels a second, every second; negative rises
    float drag = 0.0f;           // share of its speed lost a second
    float life = 0.0f, max_life = 0.4f;
    // Art pixels: a speck's side, a streak's length, a ring's radius. And how
    // fast that grows, for smoke that spreads and a ring that opens.
    float size = 1.0f, grow = 0.0f;
    // A speck this many art pixels taller than it is wide, standing up from
    // where it is: a tongue of flame and not an ember. It sinks as it dies.
    float tall = 0.0f;
    float lift = 0.0f;           // how far the ground it was shed over lifts it
    SDL_Color from{255, 255, 255, 255}, to{255, 255, 255, 0};
};

// A short-lived mark where something struck a wall. Purely visual: without it
// a bolt simply stops existing at a surface, and it is genuinely unclear
// whether it was blocked or fizzled out of range.
struct Impact {
    float x = 0, y = 0;
    float nx = 0, ny = 0;        // face it struck, so the spray points outwards
    float radius = 5.0f;
    float life = 0.0f, max_life = 0.22f;
    SDL_Color color{255, 255, 255, 255};
    bool  finished = false;
};

// Burning ground, an earth eruption waiting to go off: anything that damages
// what stands in it rather than what it touches.
struct GroundEffect {
    float x = 0, y = 0;
    float radius = 24.0f;
    float life = 0.0f, max_life = 1.0f;
    float delay = 0.0f;          // counts down before it becomes active
    float tick_timer = 0.0f, tick_interval = 0.5f;
    int   damage = 1;
    Element element = Element::None;
    CombatProfile owner;
    bool  from_player = true;
    bool    owner_local = true;
    uint8_t owner_seat = 0;
    uint32_t cast_id = 0;        // the cast it came of, as a projectile's is
    bool  burst = false;         // one big hit rather than damage over time
    // Something else on the screen is already saying where this is about to
    // land -- the Slabstrike's slab, falling, with its own shadow drawing in
    // under it -- so do not draw the usual disc over it as well.
    bool  quiet = false;
    // A technique's strike: resolved as this style at this damage multiplier,
    // rather than as a spell scaled by damage. Negative for the old behaviour.
    AttackStyle style = AttackStyle::Magic;
    float hit_mult = -1.0f;
    float knockback = 8.0f;
    // What standing in it can leave on a monster: see ProjectileDef::status.
    StatusProc status;
    // --- the elements' bigger spells -----------------------------------------------
    // A whirlpool drags what is in it toward its middle, this many pixels a
    // second. A tornado and the turbulence throw what they catch, each tick,
    // in whatever direction the dice say, this hard at most -- and what is
    // thrown is hurt by how hard: `fling_hurt` is the damage multiplier for a
    // throw of a hundred. A tornado walks: (drift_x, drift_y), a second. And
    // the turbulence is the caster's own weather: it goes where they go.
    float pull = 0.0f;
    float fling = 0.0f, fling_hurt = 0.0f;
    float drift_x = 0.0f, drift_y = 0.0f;
    bool  follows = false;
    // How it is drawn, where a disc of its colour is not it.
    // Blades: the Hail of Blades, which is the tornado's turning column with a
    // conjured blade on every ring of it instead of a speck of dust.
    enum class Draw : uint8_t { Disc = 0, Rain = 1, Whirlpool = 2, Tornado = 3, Turbulence = 4, Blades = 5 };
    Draw draw = Draw::Disc;
    // What it looks like, where that is not what it is: a Hellish Rebuke is
    // the ancient magic's and is drawn as the fire it is. None for its own.
    Element look = Element::None;
    Element Look() const { return look != Element::None ? look : element; }
    float stagger = 0.0f;        // seconds each tick staggers what it cuts: caltrops
    bool  once = false;          // a snare: it takes the first thing to step in it, and is sprung
    bool  sure_crit = false;     // loosed with Take Aim
    // Arrow Rain: arrows keep coming down on the circle for as long as it
    // lasts, a volley every tick, and are drawn falling into it and standing in
    // the ground afterwards. It was one hit and a disc that faded in a third of
    // a second, which is a thump and not a rain.
    bool  rain = false;
    int   volleys = 0;           // how many have landed: the self-test counts them
    bool  finished = false;

    bool Active() const { return delay <= 0.0f; }

    // Arrow Rain's numbers, in one place: how long it comes down for, how often
    // a volley lands, and what each is worth of the charged shot it was. Seven
    // volleys at a third each is a little over twice the shot for something
    // that stands in all of it, and most things do not.
    static constexpr float RAIN_TIME = 2.4f, RAIN_EVERY = 0.4f, RAIN_SHARE = 0.32f, RAIN_RADIUS = 56.0f;
    // And after the last volley, the arrows that are standing in the ground
    // get to finish fading before the effect is taken away.
    static constexpr float RAIN_LINGER = 0.55f;
};
