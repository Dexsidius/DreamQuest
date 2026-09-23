#pragma once
#include "../headers.h"
#include "element.h"

// -----------------------------------------------------------------------------
//  Status effects.
//
//  What a blow can leave on a monster -- or, from a monster, on the player --
//  besides the damage: it burns, it is soaked, it is concussed, it bleeds, it
//  is poisoned, it is chilled or frozen; and, cast on the player only, it is
//  charmed or confused.
//  A blow does not always leave one -- what throws it says how likely that is
//  (`StatusProc`, on a projectile in data/projectiles.json and on a weapon in
//  data/tiers.json and data/items.json) -- and what one does while it lasts is
//  data too, in data/statuses.json:
//
//    burn       the blow's damage, in part, again over three seconds. Water puts
//               it out, and nothing soaked can be set burning.
//    wet        soaked through: it cannot burn, the wind and the cold bite it
//               harder, and an Ice Touch freezes it where it stands.
//    concussed  it reels as it takes it, and until its head clears it swings
//               wide and guards badly: its Attack and its Defence are down.
//    bleed      a wound left open. A second wound adds to the first.
//    poison     slower and longer than a burn, and its hide gives way: Defence
//               is down while it lasts.
//    chill      slowed, in its legs and in its arm.
//    frozen     held fast. It thaws into a chill. The great ones cannot be held.
//    electrified  arcing. It takes a sharp share of the blow again over a few
//               seconds, its arm goes slow and its aim goes wide, and it reels
//               the moment it takes. Anything soaked is twice as easy to leave
//               arcing: see `invites`.
//    charm      (players only) beguiled: they walk to whoever cast it and
//               cannot bring themselves to strike. The next blow breaks it.
//    confused   (players only) befuddled: which way is which is backwards.
//
//  On the player a status runs for `player_share` of its time -- a player held
//  frozen for as long as a monster is is a player watching themselves die --
//  and what hinders is felt the same way a monster feels it: slower feet, a
//  worse Attack and Defence. See Player::Afflict.
//
//  Defence is where most of them meet the rest of the fight: a blow lands or it
//  does not by the attacker's Attack against the target's Defence (see
//  combat.h), so a concussed or a poisoned monster is one that is hit more
//  often -- by everybody.
//
//  The set on a monster is a few floats a status, and nothing in it is a
//  pointer: the host's goes to a friend's machine as one byte of bits.
// -----------------------------------------------------------------------------

enum class Status : uint8_t { Burn = 0, Wet, Concussed, Bleed, Poison, Chill, Frozen, Electrified, Charm, Confused,
                              COUNT };
static constexpr int STATUS_COUNT = static_cast<int>(Status::COUNT);

const char* StatusId(Status s);
// COUNT for a name that is not one.
Status      StatusFromId(const string& id);

struct StatusDef {
    Status kind = Status::Burn;
    string name;               // "Burning": what floats up over a monster as it takes
    SDL_Color color{255, 255, 255, 255};
    float seconds = 3.0f;
    // Damage over time: this share of the blow that left it, again, spread over
    // `seconds` -- and never less than `dot_min` in all. Nothing for a status
    // that only hinders.
    float dot_share = 0.0f;
    int   dot_min = 0;
    // A second one adds to what is still owed (a bleed) rather than the two
    // being the greater of them (a burn): fast hands would otherwise stack a
    // fire without end.
    bool  stacks = false;
    // While it lasts: how fast it moves, how well it swings and guards, and how
    // long between its swings.
    float speed = 1.0f, attack = 1.0f, defence = 1.0f, cooldown = 1.0f;
    float stagger = 0.0f;      // it reels for this long as it takes
    bool  holds = false;       // it cannot move or act at all while it lasts
    // What this one ends when it takes (wet puts out burn), what stops it
    // taking at all (burn, on something wet), and what it is instead on a
    // target that has `if_has` (a chill on something wet is frozen).
    vector<Status> ends, blocked_by;
    Status if_has = Status::COUNT, becomes = Status::COUNT;
    // What it leaves when it ends of itself: frozen thaws into chill.
    Status then = Status::COUNT;
    // Elements that bite harder while it lasts, and by how much.
    vector<Element> weak_to;
    float weak_mult = 1.0f;
    // Statuses that take hold more easily while this one lasts, and by how
    // much: a soaked thing is twice as easy to leave arcing. The pair to
    // `weak_to` -- that one is about the damage, this one about what is left
    // behind. See World::TryAfflict.
    vector<Status> invites;
    float invite_mult = 1.0f;
    // The great ones shake things off: this share of the time, for a boss.
    float boss_share = 0.5f;
    // On a player: this share of the time. And whether only a player can have
    // it at all (a monster is never charmed), and whether the next blow that
    // lands ends it (the pain of it wakes them from a charm).
    float player_share = 1.0f;
    bool  players_only = false;
    bool  breaks_on_hit = false;
};

class StatusDatabase {
public:
    bool Load(const string& path);
    const StatusDef* Get(Status s) const {
        const int i = static_cast<int>(s);
        return i >= 0 && i < STATUS_COUNT && loaded[i] ? &defs[i] : nullptr;
    }
    bool Empty() const;

private:
    StatusDef defs[STATUS_COUNT];
    bool      loaded[STATUS_COUNT] = {};
};

// A chance of a status, carried by what throws it.
struct StatusProc {
    Status kind = Status::COUNT;
    float  chance = 0.0f;
    bool   Any() const { return kind != Status::COUNT && chance > 0.0f; }
};
// Reads {"id": "burn", "chance": 0.3}; nothing for anything else.
StatusProc StatusProcFromJson(const json& j);

// What a monster -- or a player -- has on it now.
struct StatusSet {
    float left[STATUS_COUNT] = {};     // seconds still to run
    float rate[STATUS_COUNT] = {};     // damage a second still owed
    float bank[STATUS_COUNT] = {};     // the fraction of a point not yet dealt

    bool    Has(Status s) const { return left[static_cast<int>(s)] > 0.0f; }
    bool    Any() const { for (float l : left) if (l > 0.0f) return true; return false; }
    uint16_t Bits() const {
        uint16_t b = 0;
        for (int i = 0; i < STATUS_COUNT; ++i) if (left[i] > 0.0f) b |= static_cast<uint16_t>(1u << i);
        return b;
    }
    void    End(Status s) { const int i = static_cast<int>(s); left[i] = rate[i] = bank[i] = 0.0f; }
    void    Clear() { for (int i = 0; i < STATUS_COUNT; ++i) End(static_cast<Status>(i)); }
    // The damage still to come from one, for the tests to read.
    float   Owed(Status s) const { const int i = static_cast<int>(s); return left[i] * rate[i] + bank[i]; }
};
