#include "protocol.h"

namespace net {

namespace {

void WriteSeats(ByteWriter& w, const std::vector<SeatInfo>& seats) {
    const size_t n = seats.size() < static_cast<size_t>(MAX_SEATS) ? seats.size() : MAX_SEATS;
    w.U8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) {
        w.U8(seats[i].seat);
        w.Str(seats[i].name, MAX_NAME);
        w.Str(seats[i].look, MAX_LOOK);
        w.Bool(seats[i].host);
    }
}

bool ReadSeats(ByteReader& r, std::vector<SeatInfo>& seats) {
    seats.clear();
    const uint8_t n = r.U8();
    if (!r.Ok() || n > MAX_SEATS) return false;
    for (uint8_t i = 0; i < n; ++i) {
        SeatInfo s;
        s.seat = r.U8();
        s.name = r.Str(MAX_NAME);
        s.look = r.Str(MAX_LOOK);
        s.host = r.Bool();
        if (!r.Ok()) return false;
        seats.push_back(std::move(s));
    }
    return true;
}

// Opens a packet as a message of one type: false if it is another.
bool Open(ByteReader& r, MsgType want) {
    return r.U8() == static_cast<uint8_t>(want) && r.Ok();
}

} // namespace

Bytes Encode(const Hello& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Hello));
    w.U32(m.magic);
    w.U16(m.version);
    w.U64(m.data_hash);
    w.U64(m.maps_hash);
    w.Str(m.name, MAX_NAME);
    w.Str(m.look, MAX_LOOK);
    return w.Take();
}

bool Decode(const Bytes& b, Hello& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Hello)) return false;
    out.magic     = r.U32();
    out.version   = r.U16();
    // A Hello from another version may be laid out differently from here on.
    // The door needs the two numbers above to say so, so reading stops being
    // strict once they are known not to match.
    if (r.Ok() && (out.magic != PROTOCOL_MAGIC || out.version != PROTOCOL_VERSION)) return true;
    out.data_hash = r.U64();
    out.maps_hash = r.U64();
    out.name      = r.Str(MAX_NAME);
    out.look      = r.Str(MAX_LOOK);
    return r.Done();
}

Bytes Encode(const Welcome& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Welcome));
    w.U8(m.seat);
    w.U8(m.max_seats);
    w.U8(m.tick_rate);
    w.Str(m.world_name, MAX_NAME * 2);
    WriteSeats(w, m.roster);
    return w.Take();
}

bool Decode(const Bytes& b, Welcome& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Welcome)) return false;
    out.seat       = r.U8();
    out.max_seats  = r.U8();
    out.tick_rate  = r.U8();
    out.world_name = r.Str(MAX_NAME * 2);
    return ReadSeats(r, out.roster) && r.Done();
}

Bytes Encode(const Refuse& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Refuse));
    w.U8(static_cast<uint8_t>(m.reason));
    w.Str(m.text, MAX_REASON);
    return w.Take();
}

bool Decode(const Bytes& b, Refuse& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Refuse)) return false;
    out.reason = static_cast<RefuseReason>(r.U8());
    out.text   = r.Str(MAX_REASON);
    return r.Done();
}

Bytes Encode(const Roster& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Roster));
    WriteSeats(w, m.seats);
    return w.Take();
}

bool Decode(const Bytes& b, Roster& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Roster)) return false;
    return ReadSeats(r, out.seats) && r.Done();
}

Bytes Encode(const Say& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Say));
    w.Str(m.text, MAX_CHAT);
    return w.Take();
}

bool Decode(const Bytes& b, Say& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Say)) return false;
    out.text = r.Str(MAX_CHAT);
    return r.Done();
}

Bytes Encode(const Chat& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Chat));
    w.U8(m.seat);
    w.Str(m.text, MAX_CHAT);
    return w.Take();
}

bool Decode(const Bytes& b, Chat& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Chat)) return false;
    out.seat = r.U8();
    out.text = r.Str(MAX_CHAT);
    return r.Done();
}

std::string CleanLine(const std::string& text, size_t limit) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (c == '\t') { out.push_back(' '); continue; }
        if (c < 0x20 || c == 0x7F) continue;      // newlines, escapes, bells
        out.push_back(static_cast<char>(c));
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);

    if (out.size() > limit) {
        size_t cut = limit;
        // Back off a continuation byte (10xxxxxx) so a character is whole or
        // absent, never half.
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
        const size_t last = out.find_last_not_of(' ');
        out.resize(last == std::string::npos ? 0 : last + 1);
    }
    return out;
}

const char* RefuseReasonName(RefuseReason r) {
    switch (r) {
        case RefuseReason::NotDreamQuest: return "not DreamQuest";
        case RefuseReason::Version:       return "version";
        case RefuseReason::Data:          return "data";
        case RefuseReason::Maps:          return "maps";
        case RefuseReason::Full:          return "full";
        case RefuseReason::Malformed:     return "malformed";
        default:                          return "none";
    }
}

} // namespace net
