#pragma once
#include "../world/world.h"
#include "../net/session.h"
#include <deque>

// ---------------------------------------------------------------------------
//  Co-op: where the wire meets the world
//
//  src/net/ moves bytes and knows nothing of the game; this knows both.
//
//  Host    runs the realm: the host's own World, and one more for every map
//          that only friends are on. It steps each friend's character with the
//          inputs their machine sent -- by their clock, acting as them, so
//          their swing lands, their axe bites and the coin at their feet is
//          theirs -- takes them through doors, and tells every machine what
//          it needs to draw: twenty times a second where everyone and
//          everything near them is, and as it happens what the world did that
//          is theirs (a panel to open, something for the bag, experience, a
//          line in the journal) or everyone's (a chest opened, a tree down).
//
//  Guest   runs beside a friend's World, which is a window: it decides
//          nothing. Its own character is predicted, and put right when the
//          host disagrees; everyone and everything else is posed a tenth of a
//          second in the past. What its player does to the place is sent as
//          an Action and comes back as a Delta.
//
//  Who owns what. The world -- monsters, loot on the ground, chests, trees,
//  the clock, the traders' shelves -- is the host's, and only the host rolls
//  dice over it. A character -- bag, skills, journal, recipes -- is its
//  player's, kept on their own machine and saved there; the host holds a copy
//  (the Sheet) so that its rolls use the right numbers, and says what it
//  added or took. The plan drew the character on the server too, with every
//  panel a round trip; among friends that buys nothing, and this way every
//  panel in the game works for a guest exactly as it does alone.
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
static constexpr float  RELEVANCE_RADIUS   = 1000.0f;  // nothing farther from a player than this is told to them
static constexpr float  RECONNECT_GRACE    = 30.0f;    // a dropped friend stands where they were this long
static constexpr float  EMPTY_WORLD_LIFE   = 60.0f;    // a map nobody is on is let go after this
static constexpr float  REWIND_SECONDS     = 0.15f;    // a friend's swing lands on where the monster was when they saw it

float QuantiseDt(float dt);
net::InputStep ToStep(const PlayerInput& hands, float quantised_dt);
PlayerInput    ToHands(const net::InputStep& step);
inline float   StepSeconds(const net::InputStep& s) { return static_cast<float>(s.dt_us) * 1e-6f; }

net::PlayerState StateOf(const Player& p, uint8_t seat);
net::Outfit OutfitOf(const Player& p, uint8_t seat);
void Wear(Player& p, const net::Outfit& outfit, const GameContext& ctx);

// Which flags are a player's own -- recipes learned, places seen -- rather
// than the world's: never told to anyone else, and sent to the host in the
// Sheet so it can answer for them.
bool PrivateFlag(const string& key);

// --- a character, kept ---------------------------------------------------------
// A guest's character lives on their own machine: the player, the journal,
// the flags that are theirs and what is in their storage chest. One file a
// name, or a name and a world where the world keeps its own.
struct Character {
    json player, quests, storage;
    vector<string> flags;
    float playtime = 0.0f;
    bool  Empty() const { return player.is_null(); }
};
string CharacterPath(const string& dir, const string& name, const string& world, bool bring_your_own);
bool   SaveCharacter(const string& path, const Character& c);
bool   LoadCharacter(const string& path, Character& out);

// -----------------------------------------------------------------------------
class Host {
public:
    // Where friends' characters are kept, as the host last saw them: a second
    // copy, in case their own machine loses the first. Tests point it away
    // from saves/.
    string kept_dir = "saves/characters/kept";
    // The map a friend arrives on when there is no host to arrive beside.
    string start_map = "town_havenbrook";

    // Every frame while hosting, whatever screen the host is on. `in_world`
    // is false while the host has no game running: guests wait in the lobby.
    // On the headless server `home.player.absent` is set and it is always true.
    void Update(float dt, net::Server& server, World& home, const GameContext& ctx, bool in_world);
    void Reset(World& home);
    // Takes its ear out of Audio if it still has it there: the tap it leaves
    // points back at it, and a sound played after it is gone was a crash.
    ~Host();

    // --- someone at this machine who is not the host ----------------------------
    // Player Two, in split screen. They are a seat in the realm like a friend
    // across the wire -- they can go their own way, sleep, fall and get up --
    // but there is no wire: their hands are given each frame, their character
    // is the one standing in the world, their journal is a real one, and their
    // seat has a camera, because someone here is looking through it. The seat
    // number comes from net::Server::ReserveSeat. `character` is a kept
    // Player::ToJson, or null for someone new.
    void AddLocal(uint8_t seat_no, const string& name, const string& look, QuestLog* journal, const json& character);
    void RemoveLocal(uint8_t seat_no);
    void FeedLocal(uint8_t seat_no, const PlayerInput& hands);
    bool IsLocal(uint8_t seat_no) const;
    // E, a bed answered, getting up after a fall: done for them, here.
    void LocalAct(uint8_t seat_no, const net::Action& a, const GameContext& ctx);
    Player*  PlayerOf(uint8_t seat);

    World*   WorldOf(uint8_t seat);
    size_t   Worlds() const { return away.size() + 1; }
    uint32_t LastApplied(uint8_t seat) const;
    size_t   Queued(uint8_t seat) const;
    // Everyone, wherever they are: for the party strip.
    struct Member { uint8_t seat = 0; string name, map; int hp = 0, max_hp = 1; bool resting = false, away = false; };
    vector<Member> Party(World& home, net::Server& server);

private:
    struct Seat {
        string   name, look;
        World*   where = nullptr;           // null: waiting in the lobby
        uint32_t next_seq = 1, last_applied = 0;
        std::deque<std::pair<uint32_t, net::InputStep>> queue;
        bool     told_lobby = false;
        float    idle = 0.0f;
        net::Outfit outfit;
        bool     has_outfit = false;
        string   sheet;                     // their character as last sent, for the kept copy
        std::map<string, int> mirror;       // their bag, as their machine believes it
        uint16_t heals = 0;
        int      chain = 0;
        uint16_t aim_id = 0;
        bool     aim_locked = false;
        net::Delta tell;                    // what is theirs to hear next
        // Someone at this machine: no wire.
        bool        local = false;
        PlayerInput hands;
        QuestLog*   journal = nullptr;
        json        character;
    };
    struct Place { string map; float x = 0, y = 0; };
    struct Gone  { uint8_t seat = 0; World* where = nullptr; float left = RECONNECT_GRACE; };

    void SyncRoster(net::Server& server, World& home, const GameContext& ctx, bool in_world);
    void Arrive(uint8_t seat_no, Seat& s, net::Server& server, World& home, const GameContext& ctx);
    void Hear(net::Server& server, World& home, const GameContext& ctx);
    void Act(uint8_t seat_no, Seat& s, const net::Action& a, const GameContext& ctx);
    void StepSeats(float dt, const GameContext& ctx);
    void Doors(net::Server& server, World& home, const GameContext& ctx);
    bool Transfer(uint8_t seat_no, Seat& s, const string& map, const string& spawn, bool has_point,
                  float px, float py, int waking, net::Server& server, World& home, const GameContext& ctx);
    void Night(World& home);
    void Gather(uint8_t seat_no, Seat& s);
    void Journals(World& home);
    void Tell(float dt, net::Server& server, World& home);
    void SendEnter(net::Server& server, uint8_t seat_no, Seat& s, bool woke, const string& caption);
    void SendOutfits(net::Server& server, uint8_t to_seat, World& home);
    void Keep(const Seat& s, const Player* g);
    World* WorldFor(const string& map, World& home, const GameContext& ctx, bool in_world);
    void SplitOff(World& home);
    void Adopt(World& home);
    vector<World*> AllWorlds(World& home);

    std::map<uint8_t, Seat> seats;
    std::map<string, std::unique_ptr<World>> away;      // maps only friends are on
    std::map<string, float> empty_for;
    std::map<string, Place> last_place;                 // by name: where they were
    std::map<string, Gone>  gone;                       // by name: standing in for a dropped line
    uint8_t  host_seat = 0;
    World*   realm_home = nullptr;
    net::Outfit host_outfit;
    bool     host_outfit_sent = false;
    string   ledger_told;
    float    since_snapshot = 0.0f, since_ledger = 0.0f;
    double   clock_ms = 0.0;
    // What was heard this frame, where, and during whose step.
    struct Heard { uint8_t sfx; bool placed; float x, y, volume, pitch; World* where; int seat; };
    vector<Heard> heard;
    World* ear_world = nullptr;
    int    ear_seat = -1;
    void Sounds();
    // Where every monster was, a little while ago, for a friend's swing.
    struct Trail { float t = 0; vector<std::pair<float, float>> at; };
    std::map<World*, std::deque<Trail>> trails;
    double   trail_clock = 0.0;
};

// -----------------------------------------------------------------------------
class Guest {
public:
    void BeforeStep(World& world, const Input* device);
    void AfterStep(World& world, net::Client& client, float quantised_dt);
    // Every frame, stepped or not. `journal` is the guest's own quest log.
    void Update(float dt, net::Client& client, World& world, const GameContext& ctx);

    bool HasEnter() const { return pending_enter; }
    const net::Enter& PendingEnter() const { return enter; }
    // Before the map it names is loaded: what day it is there. Who keeps a
    // dream's platforms tonight is worked out from the day as a map loads
    // (World::ResolveSpawn), and the host is only going to say where its
    // monsters are, not what they are.
    void SetTheDay(World& world) const;
    // The map is loaded: take in what the Enter said about the world.
    void Arrived(World& world);
    void Reset(World& world);

    // The character as the host should know it: everything but where they
    // stand and how hurt they are, which the host knows better.
    static string MakeSheet(const World& world, const QuestLog* journal);

    uint32_t Sent() const { return seq; }
    uint32_t Acked() const { return acked; }
    float    LastError() const { return last_error; }
    int      Corrections() const { return corrections; }
    bool     Resting() const { return resting; }

private:
    struct Remembered { uint32_t seq; float x, y; };
    struct Heard { uint32_t time_ms; net::PlayerState state; };
    struct Puppet {
        std::deque<Heard> heard;
        net::Outfit outfit;
        bool  has_outfit = false, worn = false;
        float silent = 0.0f;
    };
    struct Beast { std::deque<std::pair<uint32_t, net::EnemyState>> heard; float silent = 0.0f; bool shown = false; };

    void OnSnapshot(const net::Snapshot& snap, net::Client& client, World& world, const GameContext& ctx);
    void OnDelta(const net::Delta& d, World& world, const GameContext& ctx);
    void PosePuppets(float dt, net::Client& client, World& world, const GameContext& ctx);
    void PoseBeasts(float dt, World& world);
    void SendActs(net::Client& client, World& world);

    uint32_t seq = 0, acked = 0;
    PlayerInput device_hands;               // what the device said, abed or not
    std::deque<Remembered> path;
    std::deque<net::InputStep> recent;
    std::map<uint8_t, Puppet> puppets;
    std::map<uint16_t, Beast> beasts;
    net::Enter enter;
    bool   pending_enter = false;
    string sheet_sent;
    float  since_sheet = 0.0f;
    uint32_t newest_ms = 0;
    bool   heard_any = false;
    double play_ms = 0.0;
    float  last_error = 0.0f;
    int    corrections = 0;
    // Hit points are the host's to say, but food is eaten here.
    int      hp_applied = -1;
    uint16_t heals_sent = 0;
    bool     resting = false;
};

} // namespace coop
