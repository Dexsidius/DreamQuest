#pragma once
#include "transport.h"
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
//  What goes down the wire
//
//  Little-endian, explicit widths, no padding and no structs memcpy'd whole:
//  two machines built by two compilers agree on every byte. A message is one
//  byte of MsgType and then its fields in the order they are written here.
//
//  A reader never trusts what it is reading. Every read checks what is left,
//  every string has a limit, and a short or overlong packet sets `ok` false
//  and yields zeroes from then on, so Decode can read straight through and
//  ask once at the end. Friends are not attackers, but a truncated datagram
//  or a newer build's message must not walk off the end of a buffer.
//
//  Milestone 0 carries the door and the chat line. InputFrame, Snapshot,
//  Event, Request and Sync join them from M1 on; PROTOCOL_VERSION goes up
//  whenever the bytes of any message change, and a Hello with another
//  version is refused at the door with both numbers in the reason.
// ---------------------------------------------------------------------------

namespace net {

static constexpr uint32_t PROTOCOL_MAGIC   = 0x31514448;   // "HDQ1", little-endian
// 2: M1's InputFrames, Snapshot, Enter, Outfit.
// 3: the world shared -- monsters, shots and loot in the snapshot, Sheet,
//    Action and Delta, a password at the door.
static constexpr uint16_t PROTOCOL_VERSION = 7;   // 4: a patch says what kind it is. 5: a monster says what is on it. 6, 7: a slab swung, and one dropped

static constexpr int    MAX_SEATS     = 4;
static constexpr size_t MAX_NAME      = 16;    // characters of a player's name
static constexpr size_t MAX_LOOK      = 32;    // a character id: "player_warden"
static constexpr size_t MAX_CHAT      = 160;
static constexpr size_t MAX_REASON    = 240;
static constexpr uint8_t SERVER_SEAT  = 255;   // a chat line from the world itself

class ByteWriter {
public:
    void U8(uint8_t v)   { out.push_back(v); }
    void U16(uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void U32(uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void U64(uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void I16(int16_t v)  { U16(static_cast<uint16_t>(v)); }
    void I8(int8_t v)    { U8(static_cast<uint8_t>(v)); }
    void Bool(bool v)    { U8(v ? 1 : 0); }
    // A float as its own bits: a player's position has to arrive exactly, or
    // a client could never tell a misprediction from a rounding.
    void F32(float v)    { uint32_t u; std::memcpy(&u, &v, 4); U32(u); }
    void F64(double v)   { uint64_t u; std::memcpy(&u, &v, 8); U64(u); }
    void I32(int32_t v)  { U32(static_cast<uint32_t>(v)); }
    // Length-prefixed, cut to `limit` bytes so a writer cannot produce what a
    // reader would refuse.
    void Str(const std::string& s, size_t limit) {
        const size_t n = s.size() < limit ? s.size() : limit;
        U16(static_cast<uint16_t>(n));
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
    Bytes Take() { return std::move(out); }
    size_t Size() const { return out.size(); }

private:
    Bytes out;
};

class ByteReader {
public:
    explicit ByteReader(const Bytes& b) : data(b.data()), size(b.size()) {}
    ByteReader(const uint8_t* p, size_t n) : data(p), size(n) {}

    uint8_t  U8()  { return static_cast<uint8_t>(Read(1)); }
    uint16_t U16() { return static_cast<uint16_t>(Read(2)); }
    uint32_t U32() { return static_cast<uint32_t>(Read(4)); }
    uint64_t U64() { return Read(8); }
    int16_t  I16() { return static_cast<int16_t>(U16()); }
    int8_t   I8()  { return static_cast<int8_t>(U8()); }
    bool     Bool(){ return U8() != 0; }
    float    F32() { const uint32_t u = U32(); float v; std::memcpy(&v, &u, 4); return v; }
    double   F64() { const uint64_t u = U64(); double v; std::memcpy(&v, &u, 8); return v; }
    int32_t  I32() { return static_cast<int32_t>(U32()); }
    std::string Str(size_t limit) {
        const size_t n = U16();
        if (!ok || n > limit || n > size - at) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(data + at), n);
        at += n;
        return s;
    }

    bool   Ok() const   { return ok; }
    // True when the whole packet was read and nothing was left over.
    bool   Done() const { return ok && at == size; }
    size_t Left() const { return size - at; }

private:
    uint64_t Read(size_t n) {
        if (!ok || n > size - at) { ok = false; return 0; }
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= static_cast<uint64_t>(data[at + i]) << (8 * i);
        at += n;
        return v;
    }
    const uint8_t* data;
    size_t size, at = 0;
    bool   ok = true;
};

enum class MsgType : uint8_t {
    Hello   = 1,   // client -> server, first thing said
    Welcome = 2,   // server -> client: you have a seat
    Refuse  = 3,   // server -> client: you do not, and why
    Roster  = 4,   // server -> client: who is here, whenever that changes
    Say     = 5,   // client -> server: a typed line
    Chat    = 6,   // server -> client: a line, and whose

    // From here up the net layer does not look inside: these are the game's,
    // handed to it whole. See src/coop/.
    GAME_FIRST  = 16,
    InputFrames = 16,  // client -> server, unreliable: the last few steps of the player's hands
    Snapshot    = 17,  // server -> client, unreliable: where everyone is
    Enter       = 18,  // server -> client, reliable: load this map, stand here
    Outfit      = 19,  // server -> client, reliable: what a seat looks like and wears
    Sheet       = 20,  // client -> server, reliable: my character, as it now is
    Action      = 21,  // client -> server, reliable: something I did that the world must hear of
    Delta       = 22,  // server -> client, reliable: what the world did that is yours, or everyone's
};
inline bool IsGameMessage(uint8_t type) { return type >= static_cast<uint8_t>(MsgType::GAME_FIRST); }

enum class RefuseReason : uint8_t {
    None = 0,
    NotDreamQuest,   // the magic number is wrong: something else is knocking
    Version,         // a different build of the protocol
    Data,            // data/ differs: their orc is not our orc
    Maps,            // maps/ differs
    Full,            // every seat is taken
    Malformed,       // a Hello that could not be read
    Password,        // the world has one, and that was not it
};

struct SeatInfo {
    uint8_t     seat = 0;
    std::string name;
    std::string look;
    bool        host = false;   // the one playing on the machine that runs the world
};

struct Hello {
    uint32_t    magic = PROTOCOL_MAGIC;
    uint16_t    version = PROTOCOL_VERSION;
    uint64_t    data_hash = 0, maps_hash = 0;
    std::string name, look;
    std::string password;            // empty where the world has none
};
static constexpr size_t MAX_PASSWORD = 48;

struct Welcome {
    uint8_t     seat = 0;
    uint8_t     max_seats = MAX_SEATS;
    uint8_t     tick_rate = 60;
    // Whether characters travel here with their players (the default), or
    // this world keeps its own: a character made here stays here.
    bool        bring_your_own = true;
    std::string world_name;          // what the host calls this world
    std::vector<SeatInfo> roster;
};

struct Refuse {
    RefuseReason reason = RefuseReason::None;
    std::string  text;
};

struct Roster { std::vector<SeatInfo> seats; };
struct Say    { std::string text; };
struct Chat   { uint8_t seat = SERVER_SEAT; std::string text; };

// --- M1: two bodies -----------------------------------------------------------
//
// One step of a player's hands, and the clock it was taken on. The client
// steps its own character with exactly these numbers the moment it reads
// them, and the server steps its copy with the same numbers when they arrive:
// the same code on the same inputs, so the two agree without a shared tick.
// (The plan drew a fixed 60 Hz tick on both ends. The game's loop runs at the
// display's rate -- 72 Hz on the machine this was written on -- so the step
// carries its own dt instead, the way Quake's usercmd does.)
struct InputStep {
    uint16_t dt_us = 0;              // microseconds, at most 50 000
    int8_t   move_x = 0, move_y = 0; // the axis, -127..127
    uint8_t  down = 0, pressed = 0, released = 0;   // PlayerInput's bits
};
static constexpr size_t MAX_INPUT_STEPS = 16;
static constexpr uint16_t MAX_STEP_US   = 50000;

// The newest steps, oldest first; steps[i] is number first_seq + i. Sent every
// frame and never resent: each packet repeats the few before it, so one that
// is lost costs nothing, and a tap that fell in it is still seen.
struct InputFrames {
    uint32_t first_seq = 0;
    // Who the player is fighting, by the monster's number, and whether that
    // is a lock: the host aims their swings and shots at the same one. And
    // their mana, which is theirs to keep count of.
    uint16_t aim_id = 0;
    bool     aim_locked = false;
    uint16_t mana = 0;
    std::vector<InputStep> steps;
};

struct PlayerState {
    enum Flag : uint8_t { Jumping = 1, Blocking = 2, Charging = 4, Dead = 8, Sprinting = 16, Hurt = 32 };
    uint8_t  seat = 0;
    float    x = 0.0f, y = 0.0f, lift = 0.0f;
    uint8_t  facing = 0, flags = 0, frame = 0;
    int16_t  hp = 0, max_hp = 0;
    std::string clip;
};
static constexpr size_t MAX_CLIP = 48;

// A monster, in twelve bytes. Which one it is is its number: monsters come
// from the map's own list and both ends have the same map, so number n is
// the nth of them and nothing else about it need be said.
struct EnemyState {
    uint16_t id = 0;
    int16_t  x = 0, y = 0;
    uint8_t  bits = 0;           // facing (2) | state (3) << 2 | hurt << 5 | bar shown << 6
    uint8_t  clip = 0;           // index into its sprite's clips, which are kept in name order
    uint8_t  frame = 0, heavy = 0, alpha = 255;
    uint8_t  statuses = 0;       // StatusSet::Bits: burning, soaked, concussed...
    uint16_t hp = 0;
};
struct PickupState {
    uint32_t id = 0;
    int16_t  x = 0, y = 0;
    uint16_t qty = 1;
    std::string item;
};
struct ShotState {
    uint32_t id = 0;
    int16_t  x = 0, y = 0, vx = 0, vy = 0;
    bool     from_player = true;
    std::string def;
};
struct PatchState {              // burning ground, a rune, a storm
    int16_t  x = 0, y = 0;
    uint16_t radius = 0;
    uint8_t  element = 0, life = 0, max_life = 0;   // tenths of a second
    bool     active = true, from_player = true;
    uint8_t  kind = 0;           // 0 a patch of something, 1 Arrow Rain: the guest draws its own arrows
    // Not ground at all: the Slabstrike's square of it. x, y is the caster it
    // is swung about (or, for SLAB_DROP, where it comes down), `radius` how far
    // out it is swung, `max_life` how big the square is, and `life` the way it
    // is swung: see AngleByte.
    static constexpr uint8_t SLAB = 32, SLAB_DROP = 33;
};
// A direction in a byte, a degree and a half at a time.
inline uint8_t AngleByte(float radians) {
    const float turns = radians / 6.2831853f;
    return static_cast<uint8_t>(static_cast<int>((turns - floorf(turns)) * 256.0f + 0.5f) & 255);
}
inline float ByteAngle(uint8_t b) { return static_cast<float>(b) * (6.2831853f / 256.0f); }
static constexpr size_t MAX_ENEMIES_TOLD = 160, MAX_PICKUPS_TOLD = 96, MAX_SHOTS_TOLD = 96, MAX_PATCHES_TOLD = 48;

struct Snapshot {
    uint32_t time_ms = 0;        // the server's clock when it was taken
    uint32_t ack_seq = 0;        // the last of the receiver's steps the server has taken
    uint16_t day = 1;
    float    hours = 9.0f;       // the clock is the realm's: one day for everyone
    uint32_t map_tag = 0;        // which map this is about: a hash of its id
    uint16_t heal_ack = 0;       // how many of the receiver's heals the server has counted
    uint8_t  gather = 0;         // how far through the log they are chopping, 0..255
    bool     resting = false;    // the receiver is lying down for the night
    std::string gather_clip;     // and the clip that goes with it, or empty
    std::vector<PlayerState> players;   // everyone on the receiver's map, the receiver included
    std::vector<EnemyState>  enemies;   // within sight of the receiver
    std::vector<PickupState> pickups;
    std::vector<ShotState>   shots;
    std::vector<PatchState>  patches;
};
uint32_t MapTag(const std::string& map_id);

// "Load this map and stand here." An empty map means the host has left the
// world, and the guest goes back to the lobby.
struct Enter {
    std::string map;
    float    x = 0.0f, y = 0.0f;
    uint16_t day = 1;
    float    hours = 9.0f;
    // The world's one-shots as they stand: chests opened, levers thrown,
    // trees felled and when they are back, what the traders have sold today.
    std::vector<std::string> flags;
    std::vector<std::pair<std::string, double>> picked;
    std::string ledger;          // ShopLedger's own JSON
    bool     woke = false;       // this is a waking, not a door: rise rested
    std::string caption;         // and what to say about it
};
static constexpr size_t MAX_FLAG = 96, MAX_FLAGS = 4000, MAX_JSON = 60000, MAX_TEXT = 200;

// My character, as it now is: Player::ToJson, the quests being done, and the
// flags that are mine alone. Sent when it changes. The character is its
// player's; the host keeps a copy so that its rolls use the right numbers.
struct Sheet {
    std::string json;
};

// Something I did that the world must hear of.
struct Action {
    enum Kind : uint8_t {
        Interact = 1,   // a = "npc" | "object", b = its id: E was pressed on it
        Drop     = 2,   // a = item, n = how many: on the ground at my feet
        Heal     = 3,   // n = hit points I gave myself (food, a potion); x = the count so far
        Respawn  = 4,   // I have read the death screen
        Sleep    = 5,   // n = 0 sleep the night through, 1 the Reverie
        ShopSold = 6,   // a = shop, b = item, n = how many I bought of its limited stock
        Storage  = 7,   // reserved
    };
    uint8_t kind = 0;
    std::string a, b;
    int32_t n = 0;
    float   x = 0.0f, y = 0.0f;
};

// What the world did that is yours, or everyone's. Every section is a list
// and most are empty most of the time.
struct Delta {
    struct Text    { std::string text; int16_t x = 0, y = 0; uint8_t r = 255, g = 255, b = 255, a = 255; uint8_t life = 9; };
    struct Panel   { uint8_t type = 0; std::string id, title, text; std::vector<std::string> list; int32_t count = 0; };
    struct Item    { std::string id; int32_t qty = 0; };          // negative takes away
    struct Xp      { uint8_t skill = 0; int32_t amount = 0; };
    struct Quest   { uint8_t type = 0; std::string target, secondary, map; int32_t amount = 1; };
    struct Sound   { uint8_t sfx = 0, volume = 255, pitch = 100; bool placed = false; int16_t x = 0, y = 0; };
    std::vector<Text>  texts;
    std::vector<std::string> flags;
    std::vector<std::pair<std::string, double>> picked;
    std::vector<Panel> panels;
    std::vector<Item>  bag;
    std::vector<Xp>    xp;
    std::vector<Quest> quests;
    std::vector<std::string> chain;      // a blow of yours landed: its name, for the counter
    std::vector<Sound> sounds;
    std::string ledger;                  // the traders' day, when it changes
    bool Empty() const {
        return texts.empty() && flags.empty() && picked.empty() && panels.empty() && bag.empty() && xp.empty() &&
               quests.empty() && chain.empty() && sounds.empty() && ledger.empty();
    }
};
static constexpr size_t MAX_MAP_ID = 48;

// What a seat looks like: the character, and what is in each equipment slot.
// A client sends its own when it changes; the server passes it on to the rest.
struct Outfit {
    uint8_t seat = 0;
    std::string look;
    std::vector<std::string> worn;      // by slot index
};
static constexpr size_t MAX_ITEM_ID = 48, MAX_WORN = 16;

// The type of a packet, or 0 for an empty one.
inline uint8_t PeekType(const Bytes& b) { return b.empty() ? 0 : b[0]; }

Bytes Encode(const Hello& m);
Bytes Encode(const Welcome& m);
Bytes Encode(const Refuse& m);
Bytes Encode(const Roster& m);
Bytes Encode(const Say& m);
Bytes Encode(const Chat& m);
Bytes Encode(const InputFrames& m);
Bytes Encode(const Snapshot& m);
Bytes Encode(const Enter& m);
Bytes Encode(const Outfit& m);
Bytes Encode(const Sheet& m);
Bytes Encode(const Action& m);
Bytes Encode(const Delta& m);

// False for a packet that is not this message, is short, is long, or carries
// a string past its limit. `out` is unspecified then.
bool Decode(const Bytes& b, Hello& out);
bool Decode(const Bytes& b, Welcome& out);
bool Decode(const Bytes& b, Refuse& out);
bool Decode(const Bytes& b, Roster& out);
bool Decode(const Bytes& b, Say& out);
bool Decode(const Bytes& b, Chat& out);
bool Decode(const Bytes& b, InputFrames& out);
bool Decode(const Bytes& b, Snapshot& out);
bool Decode(const Bytes& b, Enter& out);
bool Decode(const Bytes& b, Outfit& out);
bool Decode(const Bytes& b, Sheet& out);
bool Decode(const Bytes& b, Action& out);
bool Decode(const Bytes& b, Delta& out);

// A name or a chat line made safe to show and to store: control characters
// out, whitespace trimmed, cut to `limit` bytes without splitting a UTF-8
// character. Empty is a legitimate answer.
std::string CleanLine(const std::string& text, size_t limit);

const char* RefuseReasonName(RefuseReason r);

} // namespace net
