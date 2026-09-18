#pragma once
#include "../headers.h"

class Enemy;
class Player;
class Map;

// ---------------------------------------------------------------------------
//  Targeting
//
//  Who the player is fighting, so nothing ever has to be aimed by hand.
//
//  Out of combat there is no target at all: an arrow or a spell flies the way
//  the character is facing, and a grazing deer in front of you is not
//  something the game decides you meant to shoot.
//
//  Combat starts when a monster is in the fight -- chasing or swinging at the
//  player, or already struck by them -- or when the player locks on. From then
//  on there is a target, and shots go to it:
//
//  The combat target is picked afresh every frame from the monsters in the
//  fight: the nearest with a clear line to it, weighted toward the way the
//  player faces so turning toward one is how you choose between two.
//
//  The lock is put on with the Target button, onto anything in reach, fighting
//  or not. It holds while the player turns away, until the monster dies, gets
//  out of range, or Target is pressed past the last monster in reach -- so
//  tapping it steps nearest to furthest and then back off.
//
//  Monsters are held by pointer. The world's list can be cleared or rebuilt
//  under us (a map load, the self-test), so every pointer is checked against
//  the list before it is used, and anything no longer in it is dropped.
// ---------------------------------------------------------------------------

class Targeting {
public:
    static constexpr float COMBAT_RANGE = 280.0f;  // how far a fight reaches, world pixels
    static constexpr float LOCK_RANGE   = 300.0f;  // how far a lock can reach out to
    static constexpr float LOCK_BREAK   = 380.0f;  // and how far it holds before letting go
    static constexpr float MELEE_ASSIST = 64.0f;   // a swing turns toward a target this close

    // What happened to the lock this frame, for the world to make a sound about.
    enum class Change { None, Locked, Switched, Released };

    Change Update(const Player& player, const vector<std::unique_ptr<Enemy>>& enemies,
                  const Map& map, bool cycle);
    void   Clear() { combat = locked = nullptr; }
    // Who a friend's machine says they are fighting: the host takes its word,
    // so their swings turn to the same monster on both screens.
    void   Force(Enemy* e, bool lock) { combat = e; locked = lock ? e : nullptr; }

    // The lock if there is one, otherwise the combat target; null out of combat.
    Enemy* Current() const  { return locked ? locked : combat; }
    Enemy* Combat() const   { return combat; }
    Enemy* Locked() const   { return locked; }
    bool   IsLocked() const { return locked != nullptr; }
    bool   InCombat() const { return Current() != nullptr; }

    // Whether this monster counts as part of a fight with the player.
    static bool InFight(const Enemy& e);
    // Where on a monster to aim: the middle of its body.
    static SDL_FPoint AimPoint(const Enemy& e);
    // Alive, and not a corpse waiting to fade.
    static bool Targetable(const Enemy& e);
    // Whether a shot from one point would reach the other without a wall.
    static bool ClearLine(const Map& map, float x0, float y0, float x1, float y1);
    // Where shots leave the player from: chest height.
    static SDL_FPoint Muzzle(const Player& p);

private:
    Enemy* combat = nullptr;
    Enemy* locked = nullptr;
};
