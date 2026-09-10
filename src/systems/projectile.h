#pragma once
#include "../headers.h"
#include "combat.h"

class TextureCache;
class Camera;

// -----------------------------------------------------------------------------
//  Elements, projectiles and the ground they leave behind.
//
//  One projectile type covers arrows and spells alike: everything that differs
//  between a bowshot and a firebolt -- speed, reach, how many targets it passes
//  through, what it leaves on the ground -- is data in data/projectiles.json.
//
//  The art is a single sprite drawn along the direction of travel, so one arrow
//  image covers every angle rather than needing a frame per facing.
// -----------------------------------------------------------------------------

enum class Element { None = 0, Fire, Water, Earth, Air, COUNT };

const char* ElementName(Element e);
Element     ElementFromName(const string& name);
SDL_Color   ElementColor(Element e);

// The cycle is Water over Fire over Earth over Air over Water: water douses
// fire, fire scorches earth, earth smothers air, air disperses water.
Element ElementBeats(Element e);

// Damage multiplier for attacker's element against defender's.
//   1.60  the attacker's element beats the defender's
//   0.60  the defender's element beats the attacker's
//   0.75  same element, which resists itself
//   1.00  anything else, including untyped
float ElementMultiplier(Element attacker, Element defender);

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

    int   pierce    = 0;        // extra targets it passes through
    float knockback = 40.0f;
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
    int   pierce_left = 0;
    bool  finished = false;
    // Entities already struck, so one shot cannot hit the same target twice.
    vector<const void*> already_hit;

    SDL_FRect Bounds() const {
        return {x - def_radius(), y - def_radius(), def_radius() * 2, def_radius() * 2};
    }
    float def_radius() const { return def ? def->radius : 6.0f; }
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
    bool  burst = false;         // one big hit rather than damage over time
    bool  finished = false;

    bool Active() const { return delay <= 0.0f; }
};
