#pragma once
#include "../world/world.h"
#include "../net/session.h"
#include <deque>

// ---------------------------------------------------------------------------
//  Co-op, milestone 1: two bodies
//
//  Where the wire meets the world. src/net/ moves bytes and knows nothing of
//  the game; this knows both. Two halves:
//
//  Host    runs beside the host's own World. It keeps World::guests matching
//          the roster, steps each friend's character with the inputs their
//          machine sent -- by their clock, not ours -- tells each of them
//          which map to load and where to stand, passes outfits round, and
//          twenty times a second sends everyone where everyone is.
//
//  Guest   runs beside a friend's World, which is a window: the same map, no
//          monsters of its own (those are the host's, and arrive with M2).
//          Its own character is predicted: it steps the instant the keys are
//          read, with exactly the numbers it then sends. When the server says
//          where that step really ended, the difference is put right. Everyone
//          else is a puppet, drawn a tenth of a second in the past between the
//          two snapshots that bracket that moment.
//
//  Putting right, not rolling back. The plan has the client snap to the
//  server's state and replay its inputs since. A Player carries its bag, its
//  skills and its journal as well as its feet, so the whole object cannot be
//  rolled back without undoing a level gained in between -- and only motion
//  would be replayed anyway. So the error measured at the acknowledged step
//  is added to where the character is now, and to the remembered path, which
//  is the same answer wherever movement does not depend on position: that
//  is, everywhere but hard against a wall, where it converges in a snapshot
//  or two. A large error is a teleport (a new map) and is snapped.
//
//  Until M4 the host leads: there is one World on the server, the host's, so
//  everyone is on the host's map and goes through a door when the host does.
// ---------------------------------------------------------------------------

namespace coop {

static constexpr float SNAPSHOT_INTERVAL   = 1.0f / 20.0f;
static constexpr float INTERP_DELAY_MS     = 100.0f;
static constexpr float RECONCILE_THRESHOLD = 2.0f;     // px: below this it is float dust
static constexpr float GLIDE_DISTANCE      = 8.0f;     // up to here, put right at once: nobody sees it
static constexpr float SNAP_DISTANCE       = 96.0f;    // past here it is a teleport, not an error
static constexpr size_t INPUT_REDUNDANCY   = 8;        // steps repeated in every packet
static constexpr size_t MAX_QUEUED_STEPS   = 120;
static constexpr int    MAX_STEPS_A_FRAME  = 12;
static constexpr float  IDLE_AFTER         = 0.15f;    // a silent guest is stepped with empty hands

// A frame's dt as it will cross the wire: whole microseconds. A client steps
// its world with this, not with what the clock said, so the host steps its
// copy with the very same number.
float QuantiseDt(float dt);
net::InputStep ToStep(const PlayerInput& hands, float quantised_dt);
PlayerInput    ToHands(const net::InputStep& step);
inline float   StepSeconds(const net::InputStep& s) { return static_cast<float>(s.dt_us) * 1e-6f; }

net::PlayerState StateOf(const Player& p, uint8_t seat);
net::Outfit OutfitOf(const Player& p, uint8_t seat);
// Dresses a character as an outfit says: the look, then each slot.
void Wear(Player& p, const net::Outfit& outfit, const GameContext& ctx);

// -----------------------------------------------------------------------------
class Host {
public:
    // Every frame while hosting, whatever screen the host is on. `in_world`
    // is false while the host has no game running: guests wait in the lobby.
    void Update(float dt, net::Server& server, World& world, const GameContext& ctx, bool in_world);
    // Forgets everyone; the next Update starts over from the roster.
    void Reset(World& world);

    // For the self-test and the HUD.
    uint32_t LastApplied(uint8_t seat) const;
    size_t   Queued(uint8_t seat) const;

private:
    struct Seat {
        uint32_t next_seq = 1;          // the first step not yet queued
        uint32_t last_applied = 0;
        std::deque<std::pair<uint32_t, net::InputStep>> queue;   // numbered, in order
        string   entered_map;           // the map they were last told to load
        float    idle = 0.0f;
        net::Outfit outfit;
        bool     has_outfit = false;
    };
    void SendEnter(net::Server& server, uint8_t seat, Seat& s, World& world);
    void SendOutfits(net::Server& server, uint8_t to_seat, World& world);

    std::map<uint8_t, Seat> seats;
    net::Outfit host_outfit;
    bool     host_outfit_sent = false;
    float    since_snapshot = 0.0f;
    double   clock_ms = 0.0;
};

// -----------------------------------------------------------------------------
class Guest {
public:
    // Before the world steps: this frame's hands, as they will cross the wire.
    void BeforeStep(World& world, const Input* device);
    // After it has: remember where that put the character, and send the step.
    void AfterStep(World& world, net::Client& client, float quantised_dt);
    // Every frame, stepped or not: what the host has said, the correction to
    // our own character, and the puppets.
    void Update(float dt, net::Client& client, World& world, const GameContext& ctx);

    // An Enter that has arrived and not been acted on: the game loads the map
    // and calls Arrived().
    bool HasEnter() const { return pending_enter; }
    const net::Enter& PendingEnter() const { return enter; }
    void Arrived(World& world);
    void Reset(World& world);

    // For the self-test and the HUD.
    uint32_t Sent() const { return seq; }
    uint32_t Acked() const { return acked; }
    float    LastError() const { return last_error; }
    int      Corrections() const { return corrections; }

private:
    struct Remembered { uint32_t seq; float x, y; };
    struct Heard { uint32_t time_ms; net::PlayerState state; };
    struct Puppet {
        std::deque<Heard> heard;
        net::Outfit outfit;
        bool  has_outfit = false, worn = false;
        float silent = 0.0f;
    };
    void OnSnapshot(const net::Snapshot& snap, net::Client& client, World& world);
    void PosePuppets(float dt, net::Client& client, World& world, const GameContext& ctx);

    uint32_t seq = 0, acked = 0;
    std::deque<Remembered> path;
    std::deque<net::InputStep> recent;      // the newest INPUT_REDUNDANCY steps; the last is `seq`
    std::map<uint8_t, Puppet> puppets;
    net::Enter enter;
    bool   pending_enter = false;
    net::Outfit my_outfit;
    bool   outfit_sent = false;
    uint32_t newest_ms = 0;
    bool   heard_any = false;
    double play_ms = 0.0;
    float  last_error = 0.0f;
    int    corrections = 0;
};

} // namespace coop
