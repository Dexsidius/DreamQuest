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
    w.Str(m.password, MAX_PASSWORD);
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
    out.password  = r.Str(MAX_PASSWORD);
    return r.Done();
}

Bytes Encode(const Welcome& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Welcome));
    w.U8(m.seat);
    w.U8(m.max_seats);
    w.U8(m.tick_rate);
    w.Bool(m.bring_your_own);
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
    out.bring_your_own = r.Bool();
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

Bytes Encode(const InputFrames& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::InputFrames));
    w.U32(m.first_seq);
    w.U16(m.aim_id);
    w.Bool(m.aim_locked);
    w.U16(m.mana);
    const size_t n = m.steps.size() < MAX_INPUT_STEPS ? m.steps.size() : MAX_INPUT_STEPS;
    w.U8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const InputStep& s = m.steps[i];
        w.U16(s.dt_us);
        w.I8(s.move_x); w.I8(s.move_y);
        w.U8(s.down); w.U8(s.pressed); w.U8(s.released);
    }
    return w.Take();
}

bool Decode(const Bytes& b, InputFrames& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::InputFrames)) return false;
    out.first_seq = r.U32();
    out.aim_id = r.U16();
    out.aim_locked = r.Bool();
    out.mana = r.U16();
    const uint8_t n = r.U8();
    if (!r.Ok() || n > MAX_INPUT_STEPS) return false;
    out.steps.clear();
    for (uint8_t i = 0; i < n; ++i) {
        InputStep s;
        s.dt_us = r.U16();
        s.move_x = r.I8(); s.move_y = r.I8();
        s.down = r.U8(); s.pressed = r.U8(); s.released = r.U8();
        if (s.dt_us > MAX_STEP_US) s.dt_us = MAX_STEP_US;
        out.steps.push_back(s);
    }
    return r.Done();
}

Bytes Encode(const Snapshot& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Snapshot));
    w.U32(m.time_ms);
    w.U32(m.ack_seq);
    w.U16(m.day);
    w.F32(m.hours);
    w.U32(m.map_tag);
    w.U16(m.heal_ack);
    w.U8(m.gather);
    w.Bool(m.resting);
    w.Str(m.gather_clip, MAX_CLIP);
    const size_t n = m.players.size() < static_cast<size_t>(MAX_SEATS) ? m.players.size() : MAX_SEATS;
    w.U8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const PlayerState& p = m.players[i];
        w.U8(p.seat);
        w.F32(p.x); w.F32(p.y); w.F32(p.lift);
        w.U8(p.facing); w.U8(p.flags); w.U8(p.frame);
        w.I16(p.hp); w.I16(p.max_hp);
        w.Str(p.clip, MAX_CLIP);
    }
    const auto count = [&](size_t have, size_t limit) {
        const size_t c = have < limit ? have : limit;
        w.U16(static_cast<uint16_t>(c));
        return c;
    };
    for (size_t i = 0, c = count(m.enemies.size(), MAX_ENEMIES_TOLD); i < c; ++i) {
        const EnemyState& e = m.enemies[i];
        w.U16(e.id); w.I16(e.x); w.I16(e.y);
        w.U8(e.bits); w.U8(e.clip); w.U8(e.frame); w.U8(e.heavy); w.U8(e.alpha);
        w.U16(e.hp);
    }
    for (size_t i = 0, c = count(m.pickups.size(), MAX_PICKUPS_TOLD); i < c; ++i) {
        const PickupState& p = m.pickups[i];
        w.U32(p.id); w.I16(p.x); w.I16(p.y); w.U16(p.qty); w.Str(p.item, MAX_ITEM_ID);
    }
    for (size_t i = 0, c = count(m.shots.size(), MAX_SHOTS_TOLD); i < c; ++i) {
        const ShotState& s = m.shots[i];
        w.U32(s.id); w.I16(s.x); w.I16(s.y); w.I16(s.vx); w.I16(s.vy); w.Bool(s.from_player); w.Str(s.def, MAX_ITEM_ID);
    }
    for (size_t i = 0, c = count(m.patches.size(), MAX_PATCHES_TOLD); i < c; ++i) {
        const PatchState& g = m.patches[i];
        w.I16(g.x); w.I16(g.y); w.U16(g.radius); w.U8(g.element); w.U8(g.life); w.U8(g.max_life);
        w.Bool(g.active); w.Bool(g.from_player);
    }
    return w.Take();
}

bool Decode(const Bytes& b, Snapshot& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Snapshot)) return false;
    out.time_ms = r.U32();
    out.ack_seq = r.U32();
    out.day = r.U16();
    out.hours = r.F32();
    out.map_tag = r.U32();
    out.heal_ack = r.U16();
    out.gather = r.U8();
    out.resting = r.Bool();
    out.gather_clip = r.Str(MAX_CLIP);
    const uint8_t n = r.U8();
    if (!r.Ok() || n > MAX_SEATS) return false;
    out.players.clear();
    for (uint8_t i = 0; i < n; ++i) {
        PlayerState p;
        p.seat = r.U8();
        p.x = r.F32(); p.y = r.F32(); p.lift = r.F32();
        p.facing = r.U8(); p.flags = r.U8(); p.frame = r.U8();
        p.hp = r.I16(); p.max_hp = r.I16();
        p.clip = r.Str(MAX_CLIP);
        if (!r.Ok()) return false;
        out.players.push_back(std::move(p));
    }
    out.enemies.clear(); out.pickups.clear(); out.shots.clear(); out.patches.clear();
    uint16_t c = r.U16();
    if (!r.Ok() || c > MAX_ENEMIES_TOLD) return false;
    for (uint16_t i = 0; i < c; ++i) {
        EnemyState e;
        e.id = r.U16(); e.x = r.I16(); e.y = r.I16();
        e.bits = r.U8(); e.clip = r.U8(); e.frame = r.U8(); e.heavy = r.U8(); e.alpha = r.U8();
        e.hp = r.U16();
        out.enemies.push_back(e);
    }
    c = r.U16();
    if (!r.Ok() || c > MAX_PICKUPS_TOLD) return false;
    for (uint16_t i = 0; i < c; ++i) {
        PickupState p;
        p.id = r.U32(); p.x = r.I16(); p.y = r.I16(); p.qty = r.U16(); p.item = r.Str(MAX_ITEM_ID);
        if (!r.Ok()) return false;
        out.pickups.push_back(std::move(p));
    }
    c = r.U16();
    if (!r.Ok() || c > MAX_SHOTS_TOLD) return false;
    for (uint16_t i = 0; i < c; ++i) {
        ShotState s;
        s.id = r.U32(); s.x = r.I16(); s.y = r.I16(); s.vx = r.I16(); s.vy = r.I16();
        s.from_player = r.Bool(); s.def = r.Str(MAX_ITEM_ID);
        if (!r.Ok()) return false;
        out.shots.push_back(std::move(s));
    }
    c = r.U16();
    if (!r.Ok() || c > MAX_PATCHES_TOLD) return false;
    for (uint16_t i = 0; i < c; ++i) {
        PatchState g;
        g.x = r.I16(); g.y = r.I16(); g.radius = r.U16(); g.element = r.U8(); g.life = r.U8(); g.max_life = r.U8();
        g.active = r.Bool(); g.from_player = r.Bool();
        out.patches.push_back(g);
    }
    return r.Done();
}

uint32_t MapTag(const std::string& map_id) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : map_id) h = (h ^ ch) * 16777619u;
    return h;
}

Bytes Encode(const Enter& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Enter));
    w.Str(m.map, MAX_MAP_ID);
    w.F32(m.x); w.F32(m.y);
    w.U16(m.day);
    w.F32(m.hours);
    const size_t nf = m.flags.size() < MAX_FLAGS ? m.flags.size() : MAX_FLAGS;
    w.U16(static_cast<uint16_t>(nf));
    for (size_t i = 0; i < nf; ++i) w.Str(m.flags[i], MAX_FLAG);
    const size_t np = m.picked.size() < MAX_FLAGS ? m.picked.size() : MAX_FLAGS;
    w.U16(static_cast<uint16_t>(np));
    for (size_t i = 0; i < np; ++i) { w.Str(m.picked[i].first, MAX_FLAG); w.F64(m.picked[i].second); }
    w.Str(m.ledger, MAX_JSON);
    w.Bool(m.woke);
    w.Str(m.caption, MAX_TEXT);
    return w.Take();
}

bool Decode(const Bytes& b, Enter& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Enter)) return false;
    out.map = r.Str(MAX_MAP_ID);
    out.x = r.F32(); out.y = r.F32();
    out.day = r.U16();
    out.hours = r.F32();
    out.flags.clear(); out.picked.clear();
    uint16_t n = r.U16();
    if (!r.Ok() || n > MAX_FLAGS) return false;
    for (uint16_t i = 0; i < n; ++i) { out.flags.push_back(r.Str(MAX_FLAG)); if (!r.Ok()) return false; }
    n = r.U16();
    if (!r.Ok() || n > MAX_FLAGS) return false;
    for (uint16_t i = 0; i < n; ++i) {
        std::string key = r.Str(MAX_FLAG);
        const double when = r.F64();
        if (!r.Ok()) return false;
        out.picked.push_back({std::move(key), when});
    }
    out.ledger = r.Str(MAX_JSON);
    out.woke = r.Bool();
    out.caption = r.Str(MAX_TEXT);
    return r.Done();
}

Bytes Encode(const Outfit& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Outfit));
    w.U8(m.seat);
    w.Str(m.look, MAX_LOOK);
    const size_t n = m.worn.size() < MAX_WORN ? m.worn.size() : MAX_WORN;
    w.U8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) w.Str(m.worn[i], MAX_ITEM_ID);
    return w.Take();
}

bool Decode(const Bytes& b, Outfit& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Outfit)) return false;
    out.seat = r.U8();
    out.look = r.Str(MAX_LOOK);
    const uint8_t n = r.U8();
    if (!r.Ok() || n > MAX_WORN) return false;
    out.worn.clear();
    for (uint8_t i = 0; i < n; ++i) {
        out.worn.push_back(r.Str(MAX_ITEM_ID));
        if (!r.Ok()) return false;
    }
    return r.Done();
}

Bytes Encode(const Sheet& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Sheet));
    w.Str(m.json, MAX_JSON);
    return w.Take();
}

bool Decode(const Bytes& b, Sheet& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Sheet)) return false;
    out.json = r.Str(MAX_JSON);
    return r.Done();
}

Bytes Encode(const Action& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Action));
    w.U8(m.kind);
    w.Str(m.a, MAX_FLAG);
    w.Str(m.b, MAX_FLAG);
    w.I32(m.n);
    w.F32(m.x); w.F32(m.y);
    return w.Take();
}

bool Decode(const Bytes& b, Action& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Action)) return false;
    out.kind = r.U8();
    out.a = r.Str(MAX_FLAG);
    out.b = r.Str(MAX_FLAG);
    out.n = r.I32();
    out.x = r.F32(); out.y = r.F32();
    return r.Done();
}

namespace {
constexpr uint16_t MAX_SECTION = 512;
}

Bytes Encode(const Delta& m) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(MsgType::Delta));
    const auto count = [&](size_t have) {
        const size_t c = have < MAX_SECTION ? have : MAX_SECTION;
        w.U16(static_cast<uint16_t>(c));
        return c;
    };
    for (size_t i = 0, c = count(m.texts.size()); i < c; ++i) {
        const Delta::Text& t = m.texts[i];
        w.Str(t.text, MAX_TEXT); w.I16(t.x); w.I16(t.y);
        w.U8(t.r); w.U8(t.g); w.U8(t.b); w.U8(t.a); w.U8(t.life);
    }
    for (size_t i = 0, c = count(m.flags.size()); i < c; ++i) w.Str(m.flags[i], MAX_FLAG);
    for (size_t i = 0, c = count(m.picked.size()); i < c; ++i) { w.Str(m.picked[i].first, MAX_FLAG); w.F64(m.picked[i].second); }
    for (size_t i = 0, c = count(m.panels.size()); i < c; ++i) {
        const Delta::Panel& p = m.panels[i];
        w.U8(p.type); w.Str(p.id, MAX_FLAG); w.Str(p.title, MAX_TEXT); w.Str(p.text, 4000);
        const size_t nl = p.list.size() < 64 ? p.list.size() : 64;
        w.U8(static_cast<uint8_t>(nl));
        for (size_t k = 0; k < nl; ++k) w.Str(p.list[k], MAX_FLAG);
        w.I32(p.count);
    }
    for (size_t i = 0, c = count(m.bag.size()); i < c; ++i) { w.Str(m.bag[i].id, MAX_ITEM_ID); w.I32(m.bag[i].qty); }
    for (size_t i = 0, c = count(m.xp.size()); i < c; ++i) { w.U8(m.xp[i].skill); w.I32(m.xp[i].amount); }
    for (size_t i = 0, c = count(m.quests.size()); i < c; ++i) {
        const Delta::Quest& q = m.quests[i];
        w.U8(q.type); w.Str(q.target, MAX_FLAG); w.Str(q.secondary, MAX_FLAG); w.Str(q.map, MAX_MAP_ID); w.I32(q.amount);
    }
    for (size_t i = 0, c = count(m.chain.size()); i < c; ++i) w.Str(m.chain[i], MAX_CLIP);
    for (size_t i = 0, c = count(m.sounds.size()); i < c; ++i) {
        const Delta::Sound& snd = m.sounds[i];
        w.U8(snd.sfx); w.U8(snd.volume); w.U8(snd.pitch); w.Bool(snd.placed); w.I16(snd.x); w.I16(snd.y);
    }
    w.Str(m.ledger, MAX_JSON);
    return w.Take();
}

bool Decode(const Bytes& b, Delta& out) {
    ByteReader r(b);
    if (!Open(r, MsgType::Delta)) return false;
    out = Delta();
    const auto count = [&](uint16_t& c) { c = r.U16(); return r.Ok() && c <= MAX_SECTION; };
    uint16_t c = 0;
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        Delta::Text t;
        t.text = r.Str(MAX_TEXT); t.x = r.I16(); t.y = r.I16();
        t.r = r.U8(); t.g = r.U8(); t.b = r.U8(); t.a = r.U8(); t.life = r.U8();
        if (!r.Ok()) return false;
        out.texts.push_back(std::move(t));
    }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) { out.flags.push_back(r.Str(MAX_FLAG)); if (!r.Ok()) return false; }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        std::string key = r.Str(MAX_FLAG);
        const double when = r.F64();
        if (!r.Ok()) return false;
        out.picked.push_back({std::move(key), when});
    }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        Delta::Panel p;
        p.type = r.U8(); p.id = r.Str(MAX_FLAG); p.title = r.Str(MAX_TEXT); p.text = r.Str(4000);
        const uint8_t nl = r.U8();
        if (!r.Ok() || nl > 64) return false;
        for (uint8_t k = 0; k < nl; ++k) p.list.push_back(r.Str(MAX_FLAG));
        p.count = r.I32();
        if (!r.Ok()) return false;
        out.panels.push_back(std::move(p));
    }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        Delta::Item it; it.id = r.Str(MAX_ITEM_ID); it.qty = r.I32();
        if (!r.Ok()) return false;
        out.bag.push_back(std::move(it));
    }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) { Delta::Xp x; x.skill = r.U8(); x.amount = r.I32(); out.xp.push_back(x); }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        Delta::Quest q;
        q.type = r.U8(); q.target = r.Str(MAX_FLAG); q.secondary = r.Str(MAX_FLAG); q.map = r.Str(MAX_MAP_ID); q.amount = r.I32();
        if (!r.Ok()) return false;
        out.quests.push_back(std::move(q));
    }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) { out.chain.push_back(r.Str(MAX_CLIP)); if (!r.Ok()) return false; }
    if (!count(c)) return false;
    for (uint16_t i = 0; i < c; ++i) {
        Delta::Sound snd;
        snd.sfx = r.U8(); snd.volume = r.U8(); snd.pitch = r.U8(); snd.placed = r.Bool(); snd.x = r.I16(); snd.y = r.I16();
        out.sounds.push_back(snd);
    }
    out.ledger = r.Str(MAX_JSON);
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
        case RefuseReason::Password:      return "password";
        default:                          return "none";
    }
}

} // namespace net
