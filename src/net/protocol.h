#pragma once
#include "transport.h"
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
static constexpr uint16_t PROTOCOL_VERSION = 2;    // 2: M1's InputFrames, Snapshot, Enter, Outfit

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
    Outfit      = 19,  // both ways, reliable: what a seat looks like and wears
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
};

struct Welcome {
    uint8_t     seat = 0;
    uint8_t     max_seats = MAX_SEATS;
    uint8_t     tick_rate = 60;
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
static constexpr size_t MAX_CLIP = 24;

struct Snapshot {
    uint32_t time_ms = 0;        // the server's clock when it was taken
    uint32_t ack_seq = 0;        // the last of the receiver's steps the server has taken
    uint16_t day = 1;
    float    hours = 9.0f;       // the clock is the realm's: one day for everyone
    std::vector<PlayerState> players;   // everyone on the receiver's map, the receiver included
};

// "Load this map and stand here." An empty map means the host has left the
// world, and the guest goes back to the lobby.
struct Enter {
    std::string map;
    float    x = 0.0f, y = 0.0f;
    uint16_t day = 1;
    float    hours = 9.0f;
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

// A name or a chat line made safe to show and to store: control characters
// out, whitespace trimmed, cut to `limit` bytes without splitting a UTF-8
// character. Empty is a legitimate answer.
std::string CleanLine(const std::string& text, size_t limit);

const char* RefuseReasonName(RefuseReason r);

} // namespace net
