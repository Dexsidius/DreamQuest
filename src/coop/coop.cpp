#include "coop.h"
#include "../systems/audio.h"
#include <filesystem>
#include <fstream>

namespace coop {

// -----------------------------------------------------------------------------
//  Shared
// -----------------------------------------------------------------------------

float QuantiseDt(float dt) {
    const long us = std::lround(std::clamp(dt, 0.0f, 0.05f) * 1e6f);
    return static_cast<float>(std::clamp<long>(us, 0, net::MAX_STEP_US)) * 1e-6f;
}

net::InputStep ToStep(const PlayerInput& hands, float quantised_dt) {
    net::InputStep s;
    s.dt_us    = static_cast<uint16_t>(std::clamp<long>(std::lround(quantised_dt * 1e6f), 0, net::MAX_STEP_US));
    s.move_x   = PlayerInput::ToWire(hands.move.x);
    s.move_y   = PlayerInput::ToWire(hands.move.y);
    s.down     = hands.down;
    s.pressed  = hands.pressed;
    s.released = hands.released;
    return s;
}

PlayerInput ToHands(const net::InputStep& s) {
    PlayerInput h;
    h.move     = {PlayerInput::FromWire(s.move_x), PlayerInput::FromWire(s.move_y)};
    h.down     = s.down;
    h.pressed  = s.pressed;
    h.released = s.released;
    return h;
}

net::PlayerState StateOf(const Player& p, uint8_t seat) {
    net::PlayerState s;
    s.seat   = seat;
    s.x      = p.x;
    s.y      = p.y;
    s.lift   = p.draw_lift;
    s.facing = static_cast<uint8_t>(p.facing);
    s.frame  = static_cast<uint8_t>(std::clamp(p.ClipFrame(), 0, 255));
    s.clip   = p.Clip();
    s.hp     = static_cast<int16_t>(std::clamp(p.hp, 0, 32767));
    s.max_hp = static_cast<int16_t>(std::clamp(p.max_hp, 0, 32767));
    if (p.IsJumping())  s.flags |= net::PlayerState::Jumping;
    if (p.Blocking())   s.flags |= net::PlayerState::Blocking;
    if (p.IsCharging()) s.flags |= net::PlayerState::Charging;
    if (p.Fallen())     s.flags |= net::PlayerState::Dead;
    if (p.Sprinting())  s.flags |= net::PlayerState::Sprinting;
    return s;
}

net::Outfit OutfitOf(const Player& p, uint8_t seat) {
    net::Outfit o;
    o.seat = seat;
    o.look = p.sprite_id;
    for (int slot = 0; slot < SLOT_COUNT; ++slot) o.worn.push_back(p.equipment.InSlot(slot));
    return o;
}

static bool SameOutfit(const net::Outfit& a, const net::Outfit& b) {
    return a.look == b.look && a.worn == b.worn;
}

void Wear(Player& p, const net::Outfit& outfit, const GameContext& ctx) {
    // Another character altogether is another sprite: start it again, keeping
    // who and where it is.
    if (!outfit.look.empty() && outfit.look != p.sprite_id && ctx.sprites && ctx.sprites->Has(outfit.look)) {
        const float x = p.x, y = p.y;
        const bool local = p.local, puppet = p.puppet;
        const uint8_t seat = p.seat;
        const string name = p.name;
        p = Player();
        p.Init(ctx, outfit.look);
        p.x = x; p.y = y;
        p.local = local; p.puppet = puppet; p.seat = seat; p.name = name;
    }
    p.equipment.Clear();
    for (size_t slot = 0; slot < outfit.worn.size() && slot < static_cast<size_t>(SLOT_COUNT); ++slot) {
        const string& id = outfit.worn[slot];
        // Only what exists, and only where it goes.
        const ItemDef* def = (ctx.items && !id.empty()) ? ctx.items->Get(id) : nullptr;
        if (def && def->slot == static_cast<int>(slot)) p.equipment.Equip(static_cast<int>(slot), id);
    }
}

bool PrivateFlag(const string& key) { return World::PrivateFlag(key); }

namespace {

std::map<string, int> Counts(const Inventory& bag) {
    std::map<string, int> out;
    for (int i = 0; i < bag.SlotCount(); ++i)
        if (!bag.Slot(i).Empty()) out[bag.Slot(i).id] += bag.Slot(i).qty;
    return out;
}

int16_t Px(float v) { return static_cast<int16_t>(std::clamp(std::lround(v), -32768l, 32767l)); }

uint8_t ClipIndex(const SpriteDef* def, const string& name) {
    if (!def) return 0;
    uint8_t i = 0;
    for (const auto& kv : def->clips) { if (kv.first == name) return i; if (i == 255) break; ++i; }
    return 0;
}

const string& ClipName(const SpriteDef* def, uint8_t index) {
    static const string idle = "idle";
    if (!def) return idle;
    uint8_t i = 0;
    for (const auto& kv : def->clips) { if (i == index) return kv.first; ++i; }
    return idle;
}

string SafeFileName(const string& s) {
    string out;
    for (unsigned char c : s) out.push_back(std::isalnum(c) || c == '-' || c == '_' ? static_cast<char>(c) : '_');
    return out.empty() ? string("traveller") : out;
}

net::Delta::Panel ToPanel(const WorldRequest& r) {
    net::Delta::Panel p;
    p.type = static_cast<uint8_t>(r.type);
    p.id = r.id; p.title = r.title; p.text = r.text; p.list = r.list; p.count = r.count;
    return p;
}

WorldRequest FromPanel(const net::Delta::Panel& p) {
    WorldRequest r;
    r.type = static_cast<WorldRequest::Type>(std::min<uint8_t>(p.type, static_cast<uint8_t>(WorldRequest::Type::Sleep)));
    r.id = p.id; r.title = p.title; r.text = p.text; r.list = p.list; r.count = p.count;
    return r;
}

WorldRequest Toast(const string& text) {
    WorldRequest r;
    r.type = WorldRequest::Type::Toast;
    r.text = text;
    return r;
}

} // namespace

// -----------------------------------------------------------------------------
//  A character, kept
// -----------------------------------------------------------------------------

string CharacterPath(const string& dir, const string& name, const string& world, bool bring_your_own) {
    const string file = bring_your_own ? SafeFileName(name) : SafeFileName(name) + "@" + SafeFileName(world);
    return dir + "/" + file + ".json";
}

bool SaveCharacter(const string& path, const Character& c) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    const json j = {{"version", 1}, {"player", c.player}, {"quests", c.quests}, {"storage", c.storage},
                    {"flags", c.flags}, {"playtime", c.playtime}};
    out << j.dump(1);
    return out.good();
}

bool LoadCharacter(const string& path, Character& out) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        json j;
        in >> j;
        out.player = j.value("player", json());
        out.quests = j.value("quests", json::object());
        out.storage = j.value("storage", json::object());
        out.flags.clear();
        if (j.contains("flags") && j["flags"].is_array())
            for (const json& f : j["flags"]) if (f.is_string()) out.flags.push_back(f.get<string>());
        out.playtime = j.value("playtime", 0.0f);
    } catch (const std::exception&) {
        return false;
    }
    return !out.Empty() && out.player.is_object();
}

// -----------------------------------------------------------------------------
//  Host
// -----------------------------------------------------------------------------

void Host::Reset(World& home) {
    seats.clear();
    away.clear();
    empty_for.clear();
    gone.clear();
    trails.clear();
    home.guests.clear();
    home.before_unload = nullptr;
    home.after_load = nullptr;
    home.company = false;
    home.journal = false;
    home.text_log.clear(); home.flag_log.clear(); home.picked_log.clear();
    host_outfit_sent = false;
    since_snapshot = 0.0f;
    heard.clear();
    Audio::SetTap(nullptr);
    Audio::SetMuted(0);
}

uint32_t Host::LastApplied(uint8_t seat) const {
    auto it = seats.find(seat);
    return it == seats.end() ? 0 : it->second.last_applied;
}

size_t Host::Queued(uint8_t seat) const {
    auto it = seats.find(seat);
    return it == seats.end() ? 0 : it->second.queue.size();
}

void Host::AddLocal(uint8_t seat_no, const string& name, const string& look, QuestLog* journal, const json& character) {
    Seat s;
    s.name = name;
    s.look = look;
    s.local = true;
    s.journal = journal;
    s.character = character;
    seats[seat_no] = std::move(s);
}

void Host::RemoveLocal(uint8_t seat_no) {
    auto it = seats.find(seat_no);
    if (it == seats.end() || !it->second.local) return;
    if (it->second.where) it->second.where->RemoveGuest(seat_no);
    seats.erase(it);
}

void Host::FeedLocal(uint8_t seat_no, const PlayerInput& hands) {
    auto it = seats.find(seat_no);
    if (it != seats.end() && it->second.local) it->second.hands = hands;
}

bool Host::IsLocal(uint8_t seat_no) const {
    auto it = seats.find(seat_no);
    return it != seats.end() && it->second.local;
}

void Host::LocalAct(uint8_t seat_no, const net::Action& a, const GameContext& ctx) {
    auto it = seats.find(seat_no);
    if (it == seats.end() || !it->second.local || !it->second.where) return;
    World& w = *it->second.where;
    Player* g = w.Guest(seat_no);
    if (!g) return;
    if (a.kind == net::Action::Interact) {
        // What E would do is what their own seat's prompt found.
        w.AsSeat(*g, ctx, [&](const GameContext& theirs) { w.TryInteract(theirs); });
        return;
    }
    Act(seat_no, it->second, a, ctx);
}

Player* Host::PlayerOf(uint8_t seat) {
    World* w = WorldOf(seat);
    return w ? w->Guest(seat) : nullptr;
}

World* Host::WorldOf(uint8_t seat) {
    auto it = seats.find(seat);
    return it == seats.end() ? nullptr : it->second.where;
}

vector<World*> Host::AllWorlds(World& home) {
    vector<World*> all{&home};
    for (auto& kv : away) all.push_back(kv.second.get());
    return all;
}

vector<Host::Member> Host::Party(World& home, net::Server& server) {
    vector<Member> out;
    for (const net::SeatInfo& info : server.Roster()) {
        Member m;
        m.seat = info.seat;
        m.name = info.name;
        if (info.host) {
            m.map = home.MapId(); m.hp = home.player.hp; m.max_hp = home.player.max_hp; m.resting = home.player.resting;
        } else if (World* w = WorldOf(info.seat)) {
            if (const Player* g = w->Guest(info.seat)) { m.map = w->MapId(); m.hp = g->hp; m.max_hp = g->max_hp; m.resting = g->resting; }
        }
        out.push_back(m);
    }
    return out;
}

void Host::Update(float dt, net::Server& server, World& home, const GameContext& ctx, bool in_world) {
    clock_ms += static_cast<double>(dt) * 1000.0;
    trail_clock += dt;
    realm_home = &home;
    in_world = in_world && home.CurrentMap().Loaded();

    // Whoever is standing in for a dropped line is let go when the grace is up.
    for (auto it = gone.begin(); it != gone.end();) {
        if ((it->second.left -= dt) > 0.0f) { ++it; continue; }
        for (World* w : AllWorlds(home)) if (w == it->second.where) w->RemoveGuest(it->second.seat);
        it = gone.erase(it);
    }

    // The host leaves friends behind on the map being left, in a world of
    // their own, and finds the ones already on the map being entered.
    home.before_unload = [this](World& w) { SplitOff(w); };
    home.after_load    = [this](World& w) { Adopt(w); };
    home.journal = true;

    // An ear in the world, for friends: what is heard between now and the next
    // frame -- the host's own world stepping included -- is kept to be told.
    ear_world = &home;
    ear_seat = -1;
    if (seats.empty()) { Audio::SetTap(nullptr); heard.clear(); }
    else Audio::SetTap([this](Sfx s, bool placed, float x, float y, float volume, float pitch) {
        if (heard.size() < 256)
            heard.push_back({static_cast<uint8_t>(s), placed, x, y, volume, pitch, ear_world, ear_seat});
    });

    SyncRoster(server, home, ctx, in_world);
    home.company = !seats.empty();
    home.player.seat = host_seat;
    Hear(server, home, ctx);
    if (!in_world) { host_outfit_sent = false; return; }

    // Where every monster is now, kept for a moment: a friend swings at what
    // they saw, which is where it was.
    for (World* w : AllWorlds(home)) {
        std::deque<Trail>& trail = trails[w];
        Trail now;
        now.t = static_cast<float>(trail_clock);
        for (const auto& e : w->enemies) now.at.push_back({e->x, e->y});
        trail.push_back(std::move(now));
        while (trail.size() > 2 && trail.front().t < trail_clock - 0.5) trail.pop_front();
    }

    StepSeats(dt, ctx);

    // The maps only friends are on. Nobody's journal is the host's there, and
    // the clock is the realm's.
    GameContext quiet = ctx;
    quiet.quests = nullptr;
    quiet.input = nullptr;
    for (auto& kv : away) {
        World& w = *kv.second;
        w.clock = home.clock;
        w.company = true;
        ear_world = &w;
        // Not the host's map, so not the host's to hear -- unless someone at
        // this machine is on it, looking at the other half of the screen.
        bool watched = false;
        for (const auto& [seat_no, s] : seats) watched |= s.local && s.where == &w;
        Audio::SetMuted(watched ? 0 : 2);
        w.Update(dt, quiet);
        Audio::SetMuted(0);
        w.clock = home.clock;
    }
    ear_world = &home;

    Doors(server, home, ctx);
    Night(home);
    for (auto& [seat_no, s] : seats) Gather(seat_no, s);
    Sounds();
    Journals(home);
    Tell(dt, server, home);

    // A map nobody is on is let go after a while; what happened there that
    // matters is in the flags, which are the realm's.
    for (auto it = away.begin(); it != away.end();) {
        bool anyone = !it->second->guests.empty();
        float& idle = empty_for[it->first];
        idle = anyone ? 0.0f : idle + dt;
        if (idle > EMPTY_WORLD_LIFE) {
            trails.erase(it->second.get());
            empty_for.erase(it->first);
            it = away.erase(it);
        } else {
            ++it;
        }
    }
}

// --- who is here ---------------------------------------------------------------------

void Host::SyncRoster(net::Server& server, World& home, const GameContext& ctx, bool in_world) {
    host_seat = 255;
    std::set<uint8_t> present;
    for (const net::SeatInfo& info : server.Roster()) {
        if (info.host) host_seat = info.seat;
        else present.insert(info.seat);
    }

    // Gone: the line dropped, or they left. Their character stands where it
    // was for a while, out of the fight, in case they come back.
    for (auto it = seats.begin(); it != seats.end();) {
        if (present.count(it->first) || it->second.local) { ++it; continue; }
        Seat& s = it->second;
        if (s.where) {
            if (Player* g = s.where->Guest(it->first)) {
                last_place[s.name] = {s.where->MapId(), g->x, g->y};
                Keep(s, g);
                // Parked under a number no seat has, so whoever sits here next
                // is not mistaken for them.
                uint8_t parked = 200;
                for (const auto& kv : gone) parked = std::max<uint8_t>(parked, static_cast<uint8_t>(kv.second.seat + 1));
                SeatState moved = std::move(s.where->SeatOf(it->first));
                moved.targeting.Clear();
                moved.transition_pending = false;
                g->seat = parked;
                g->away = true;
                g->hands = PlayerInput{};
                s.where->RemoveGuest(it->first);          // only the old seat's state: nobody has that number now
                s.where->SeatOf(parked) = std::move(moved);
                gone[s.name] = {parked, s.where, RECONNECT_GRACE};
            }
        }
        it = seats.erase(it);
    }

    // Whoever is at this machine arrives and leaves the world with the host.
    for (auto& [seat_no, s] : seats) {
        if (!s.local) continue;
        if (!in_world) {
            if (s.where) s.where->RemoveGuest(seat_no);
            s.where = nullptr;
        } else if (!s.where) {
            Arrive(seat_no, s, server, home, ctx);
        }
    }

    for (const net::SeatInfo& info : server.Roster()) {
        if (info.host) continue;
        auto found = seats.find(info.seat);
        if (found != seats.end() && found->second.local) continue;
        if (found == seats.end()) {
            Seat fresh;
            fresh.name = info.name;
            fresh.look = info.look;
            found = seats.emplace(info.seat, std::move(fresh)).first;
            // Back within the grace: the one standing in for them goes.
            auto was = gone.find(info.name);
            if (was != gone.end()) {
                for (World* w : AllWorlds(home)) if (w == was->second.where) w->RemoveGuest(was->second.seat);
                gone.erase(was);
            }
        }
        Seat& s = found->second;
        if (!in_world) {
            // No world to be in: back to the lobby with them.
            if (s.where || !s.told_lobby) {
                if (s.where) s.where->RemoveGuest(info.seat);
                s.where = nullptr;
                s.queue.clear();
                server.SendToSeat(info.seat, net::Channel::Reliable, net::Encode(net::Enter{}));
                s.told_lobby = true;
            }
            continue;
        }
        if (!s.where) Arrive(info.seat, s, server, home, ctx);
    }
    if (!in_world) { away.clear(); trails.clear(); empty_for.clear(); }
}

void Host::Arrive(uint8_t seat_no, Seat& s, net::Server& server, World& home, const GameContext& ctx) {
    // Where they were when they were last here, if that can still be stood
    // on; otherwise beside the host, or at the start where there is no host.
    World* w = nullptr;
    float x = 0.0f, y = 0.0f;
    // From a day the host has since restarted: the kept copy says where.
    if (!last_place.count(s.name) && !kept_dir.empty()) {
        std::ifstream in(kept_dir + "/" + SafeFileName(s.name) + ".json");
        if (in) {
            try {
                json j; in >> j;
                if (j.contains("place") && j["place"].is_object())
                    last_place[s.name] = {j["place"].value("map", string()), j["place"].value("x", 0.0f), j["place"].value("y", 0.0f)};
            } catch (const std::exception&) {}
        }
    }
    auto place = last_place.find(s.name);
    if (place != last_place.end()) {
        w = WorldFor(place->second.map, home, ctx, true);
        if (w) { x = place->second.x; y = place->second.y; }
    }
    if (!w) {
        w = &home;
        if (home.player.absent) {
            const SDL_FPoint p = home.CurrentMap().DefaultSpawn();
            x = p.x; y = p.y;
        } else {
            x = home.player.x; y = home.player.y;
        }
    }
    Player* g = w->AddGuest(seat_no, s.name, s.look, ctx);
    if (s.local) {
        // Their own character, whole, as it was kept; and a seat that is looked
        // through, with their real journal behind it.
        if (s.character.is_object()) {
            g->Init(ctx, s.character.value("sprite", s.look));
            g->FromJson(s.character, ctx);
            g->local = false;
            g->seat = seat_no;
            g->name = s.name;
        } else {
            for (const string& id : Player::StartingKit(g->sprite_id)) {
                g->inventory.Add(id, 1);
                if (const ItemDef* def = ctx.items ? ctx.items->Get(id) : nullptr)
                    if (def->slot != SLOT_NONE) g->equipment.Equip(def->slot, id);
            }
            g->inventory.Add("coins", 25);
            g->inventory.Add("cooked_meat", 3);
        }
        g->x = x; g->y = y;
        SeatState& state = w->SeatOf(seat_no);
        state.own_journal = s.journal;
        state.viewed = true;
        state.camera.SetBounds(w->CurrentMap().Width(), w->CurrentMap().Height());
        state.camera.SnapTo(x, y);
        s.where = w;
        return;
    }
    g->x = x; g->y = y;
    if (!s.sheet.empty()) {
        try { g->ApplySheet(json::parse(s.sheet).value("player", json::object()), ctx); } catch (const std::exception&) {}
    } else {
        // Dressed as a new character is, until their machine says otherwise.
        for (const string& id : Player::StartingKit(g->sprite_id))
            if (const ItemDef* def = ctx.items ? ctx.items->Get(id) : nullptr)
                if (def->slot != SLOT_NONE) g->equipment.Equip(def->slot, id);
    }
    s.mirror = Counts(g->inventory);
    s.where = w;
    s.told_lobby = false;
    s.queue.clear();
    SendEnter(server, seat_no, s, false, "");
    SendOutfits(server, seat_no, home);
}

World* Host::WorldFor(const string& map, World& home, const GameContext& ctx, bool in_world) {
    if (map.empty()) return nullptr;
    if (in_world && home.MapId() == map) return &home;
    auto it = away.find(map);
    if (it != away.end()) return it->second.get();

    auto w = std::make_unique<World>();
    w->player.absent = true;
    w->player.seat = Player::NO_SEAT;
    w->journal = false;               // not while it loads: "visited" is nobody's news
    w->company = true;
    w->SetFlags(home.Flags());
    w->SetPickedHerbs(home.PickedHerbs());
    w->clock = home.clock;
    GameContext quiet = ctx;
    quiet.quests = nullptr;
    quiet.input = nullptr;
    if (!w->LoadMap(map, "", quiet)) return nullptr;
    w->journal = true;
    World* raw = w.get();
    away[map] = std::move(w);
    empty_for[map] = 0.0f;
    return raw;
}

void Host::SplitOff(World& home) {
    // Friends on the map the host is leaving stay on it.
    bool anyone = false;
    for (const auto& g : home.guests) anyone |= !g->puppet;
    if (!anyone) return;
    auto w = std::make_unique<World>();
    w->player.absent = true;
    w->player.seat = Player::NO_SEAT;
    w->company = true;
    w->SetFlags(home.Flags());
    w->SetPickedHerbs(home.PickedHerbs());
    w->clock = home.clock;
    home.HandOver(*w);
    w->journal = true;
    for (auto& [seat_no, s] : seats) if (s.where == &home) s.where = w.get();
    for (auto& kv : gone) if (kv.second.where == &home) kv.second.where = w.get();
    trails.erase(&home);
    const string id = w->MapId();
    empty_for[id] = 0.0f;
    away[id] = std::move(w);
}

void Host::Adopt(World& home) {
    // And the ones already on the map the host has come to are joined: the
    // place as they have it -- the boar half dead, the coins on the ground --
    // rather than as a fresh arrival would find it.
    auto it = away.find(home.MapId());
    if (it == away.end()) return;
    World* from = it->second.get();
    from->HandOver(home);
    for (auto& [seat_no, s] : seats) if (s.where == from) s.where = &home;
    for (auto& kv : gone) if (kv.second.where == from) kv.second.where = &home;
    trails.erase(from);
    empty_for.erase(it->first);
    away.erase(it);
}

// --- what they have sent ------------------------------------------------------------------

void Host::Hear(net::Server& server, World& home, const GameContext& ctx) {
    (void)home;
    for (const net::Server::Inbound& in : server.TakeGameMessages()) {
        auto it = seats.find(in.seat);
        if (it == seats.end()) continue;
        Seat& s = it->second;
        Player* g = s.where ? s.where->Guest(in.seat) : nullptr;
        switch (static_cast<net::MsgType>(net::PeekType(in.data))) {
            case net::MsgType::InputFrames: {
                net::InputFrames frames;
                if (!g || !net::Decode(in.data, frames)) break;
                s.aim_id = frames.aim_id;
                s.aim_locked = frames.aim_locked;
                g->SetMana(frames.mana);
                // Most of every packet is steps already taken: only the new.
                for (size_t i = 0; i < frames.steps.size(); ++i) {
                    const uint32_t seq = frames.first_seq + static_cast<uint32_t>(i);
                    if (seq < s.next_seq) continue;
                    s.next_seq = seq + 1;
                    s.queue.push_back({seq, frames.steps[i]});
                    if (s.queue.size() > MAX_QUEUED_STEPS) s.queue.pop_front();
                }
                break;
            }
            case net::MsgType::Sheet: {
                net::Sheet sheet;
                if (!net::Decode(in.data, sheet)) break;
                json j;
                try { j = json::parse(sheet.json); } catch (const std::exception&) { break; }
                if (!j.is_object()) break;
                s.sheet = sheet.json;
                if (!g) break;
                {
                    // A character kept from another day may not be the look
                    // that was said at the door.
                    const string look = j.value("player", json::object()).value("sprite", string());
                    if (!look.empty() && look != g->sprite_id) {
                        net::Outfit as; as.seat = in.seat; as.look = look;
                        Wear(*g, as, ctx);
                        g->local = false;
                    }
                }
                g->ApplySheet(j.value("player", json::object()), ctx);
                SeatState& state = s.where->SeatOf(in.seat);
                state.journal.relay_active.clear();
                state.private_flags.clear();
                if (j.contains("active") && j["active"].is_array())
                    for (const json& q : j["active"]) if (q.is_string()) state.journal.relay_active.insert(q.get<string>());
                if (j.contains("flags") && j["flags"].is_array())
                    for (const json& f : j["flags"])
                        if (f.is_string() && PrivateFlag(f.get<string>())) state.private_flags.insert(f.get<string>());
                // Their bag is now what they say it is.
                s.mirror = Counts(g->inventory);
                // And if what they wear has changed, the others are told.
                const net::Outfit now = OutfitOf(*g, in.seat);
                if (!s.has_outfit || !SameOutfit(now, s.outfit)) {
                    s.outfit = now;
                    s.has_outfit = true;
                    server.SendToGuests(net::Channel::Reliable, net::Encode(now), in.seat);
                }
                break;
            }
            case net::MsgType::Action: {
                net::Action a;
                if (g && net::Decode(in.data, a)) Act(in.seat, s, a, ctx);
                break;
            }
            default: break;
        }
    }
}

void Host::Act(uint8_t seat_no, Seat& s, const net::Action& a, const GameContext& ctx) {
    World& w = *s.where;
    Player* g = w.Guest(seat_no);
    if (!g) return;
    switch (a.kind) {
        case net::Action::Interact: {
            const int kind = SDL_atoi(a.a.c_str());
            w.AsSeat(*g, ctx, [&](const GameContext& theirs) { w.InteractWith(kind, a.n, theirs); });
            break;
        }
        case net::Action::Drop:
            if (a.n > 0 && ctx.items && ctx.items->Get(a.a)) {
                const float x = g->x, y = g->y + 6.0f;
                w.AsSeat(*g, ctx, [&](const GameContext& theirs) { w.DropItem(a.a, std::min(a.n, 100000), x, y, theirs, true); });
                // It left their bag on their machine; it leaves the copy too,
                // or the difference would be sent back to them as a gift.
                if (g->inventory.Remove(a.a, std::min(a.n, g->inventory.Count(a.a)))) s.mirror = Counts(g->inventory);
            }
            break;
        case net::Action::Heal:
            if (a.n > 0 && !g->Fallen()) {
                g->Heal(std::min(a.n, g->max_hp));
                g->skills.SetCurrent(SKILL_HITPOINTS, g->hp);
            }
            s.heals = static_cast<uint16_t>(a.x);
            break;
        case net::Action::Respawn:
            if (g->Fallen()) {
                SeatState& state = w.SeatOf(seat_no);
                state.transition_pending = true;
                state.next_map = "town_havenbrook";
                state.next_spawn = "respawn";
                state.next_has_point = false;
                state.waking = -1;           // not a waking: a getting up
            }
            break;
        case net::Action::Sleep:
            w.AsSeat(*g, ctx, [&](const GameContext& theirs) {
                w.Sleep(a.n == 1 ? World::SleepChoice::Reverie : World::SleepChoice::Through, theirs);
            });
            break;
        case net::Action::ShopSold:
            // One shelf for everyone: the realm's ledger is the host's.
            if (realm_home && a.n > 0) realm_home->shops.Record(a.a, a.b, std::min(a.n, 100000));
            break;
        default: break;
    }
}

// --- their steps, by their clocks -------------------------------------------------------------

void Host::StepSeats(float dt, const GameContext& ctx) {
    for (auto& [seat_no, s] : seats) {
        if (!s.where) continue;
        World& w = *s.where;
        Player* g = w.Guest(seat_no);
        if (!g) continue;

        // Someone at this machine: their hands were read this frame, their
        // clock is ours, and what their step sounds like is for these speakers.
        if (s.local) {
            Audio::SetMuted(0);
            ear_world = &w;
            ear_seat = seat_no;
            w.StepGuest(*g, s.hands, dt, ctx);
            continue;
        }

        // What their step sounds like is theirs to hear; the host hears only
        // what has a place, and only on its own map.
        ear_world = &w;
        ear_seat = seat_no;
        Audio::SetMuted(&w == realm_home ? 1 : 2);

        // Who they are fighting is who their machine says: the same monster,
        // by its number.
        SeatState& state = w.SeatOf(seat_no);
        Enemy* aim = (s.aim_id >= 1 && s.aim_id <= w.enemies.size()) ? w.enemies[s.aim_id - 1].get() : nullptr;
        state.targeting.Force(aim && Targeting::Targetable(*aim) ? aim : nullptr, s.aim_locked);

        // A swing lands on where the monster was when they saw it.
        const bool rewind = g->Attacking() && !w.enemies.empty();
        vector<std::pair<float, float>> now;
        const Trail* then = nullptr;
        if (rewind) {
            auto found = trails.find(&w);
            if (found != trails.end())
                for (const Trail& t : found->second)
                    if (t.t <= trail_clock - REWIND_SECONDS && t.at.size() == w.enemies.size()) then = &t;
            if (then) {
                for (size_t i = 0; i < w.enemies.size(); ++i) {
                    now.push_back({w.enemies[i]->x, w.enemies[i]->y});
                    w.enemies[i]->x = then->at[i].first;
                    w.enemies[i]->y = then->at[i].second;
                }
            }
        }

        int taken = 0;
        while (!s.queue.empty() && taken < MAX_STEPS_A_FRAME) {
            const auto [number, step] = s.queue.front();
            s.queue.pop_front();
            w.StepGuest(*g, ToHands(step), StepSeconds(step), ctx);
            s.last_applied = number;
            ++taken;
        }
        if (taken > 0) s.idle = 0.0f;
        else if ((s.idle += dt) > IDLE_AFTER)
            w.StepGuest(*g, PlayerInput{}, dt, ctx);

        if (then) {
            // Back to now, keeping whatever the blow did to them.
            for (size_t i = 0; i < w.enemies.size() && i < now.size(); ++i) {
                w.enemies[i]->x = now[i].first  + (w.enemies[i]->x - then->at[i].first);
                w.enemies[i]->y = now[i].second + (w.enemies[i]->y - then->at[i].second);
            }
        }
    }
    Audio::SetMuted(0);
    ear_world = realm_home;
    ear_seat = -1;
}

void Host::Sounds() {
    // What a friend's own window already plays for its own character -- the
    // swing, the footfall, the menus -- is not sent; everything the window
    // cannot know is. A sound with a place goes to everyone on that map, and
    // their machine fades it by distance; one without goes only to whoever's
    // step made it.
    const auto own = [](uint8_t s) {
        const Sfx k = static_cast<Sfx>(s);
        return k == Sfx::Swing || k == Sfx::SwingHeavy || k == Sfx::Jump || k == Sfx::Land || k == Sfx::Footstep ||
               k == Sfx::FootstepWood || k == Sfx::FootstepStone || k == Sfx::Winded || k == Sfx::Sleep || k == Sfx::Wake ||
               k == Sfx::Equip || k == Sfx::Eat || (k >= Sfx::UiMove && k <= Sfx::QuestComplete);
    };
    for (const Heard& h : heard) {
        if (own(h.sfx)) continue;
        net::Delta::Sound out;
        out.sfx = h.sfx;
        out.volume = static_cast<uint8_t>(std::clamp(h.volume, 0.0f, 1.0f) * 255.0f);
        out.pitch = static_cast<uint8_t>(std::clamp(h.pitch * 100.0f, 10.0f, 255.0f));
        out.placed = h.placed;
        out.x = Px(h.x); out.y = Px(h.y);
        for (auto& [seat_no, s] : seats) {
            if (s.where != h.where || s.tell.sounds.size() >= 24) continue;
            if (h.placed || h.seat == static_cast<int>(seat_no)) s.tell.sounds.push_back(out);
        }
    }
    heard.clear();
}

// --- doors, dreams and getting up ------------------------------------------------------------------

void Host::Doors(net::Server& server, World& home, const GameContext& ctx) {
    for (auto& [seat_no, s] : seats) {
        if (!s.where) continue;
        SeatState& state = s.where->SeatOf(seat_no);
        if (!state.transition_pending) continue;
        const string map = state.next_map, spawn = state.next_spawn;
        const bool has_point = state.next_has_point;
        const float px = state.next_x, py = state.next_y;
        const int waking = state.waking;
        state.transition_pending = false;
        state.next_has_point = false;
        state.waking = 0;
        state.fade = 0.0f;
        state.fade_dir = 0;
        state.fade_caption.clear();
        Transfer(seat_no, s, map, spawn, has_point, px, py, waking, server, home, ctx);
    }
}

bool Host::Transfer(uint8_t seat_no, Seat& s, const string& map, const string& spawn, bool has_point,
                    float px, float py, int waking, net::Server& server, World& home, const GameContext& ctx) {
    World* from = s.where;
    World* to = WorldFor(map, home, ctx, true);
    if (!from || !to) {
        s.tell.panels.push_back(ToPanel(Toast("That path could not be opened.")));
        return false;
    }
    // Out of one world and into the other, seat and all.
    std::unique_ptr<Player> g;
    for (auto it = from->guests.begin(); it != from->guests.end(); ++it)
        if ((*it)->seat == seat_no && !(*it)->puppet) { g = std::move(*it); from->guests.erase(it); break; }
    if (!g) return false;
    SeatState state = std::move(from->SeatOf(seat_no));
    from->RemoveGuest(seat_no);
    state.targeting.Clear();
    state.gather_index = -1;
    state.portals_armed = false;          // they arrive standing on the way back
    state.arrival_released = false;

    SDL_FPoint p;
    if (has_point) p = {px, py};
    else if (spawn.empty() || !to->CurrentMap().Spawn(spawn, p)) p = to->CurrentMap().DefaultSpawn();
    g->x = p.x;
    g->y = p.y;
    g->knock_x = g->knock_y = 0.0f;
    g->StopGathering();
    g->resting = false;

    string caption;
    bool woke = false;
    if (waking != 0) {
        // Up from a dream, or up from the ground.
        woke = true;
        if (g->Fallen()) g->Respawn(p.x, p.y);
        g->Rest();
        state.dream_active = false;
        using W = World::WakeReason;
        caption = waking < 0                                  ? "You wake in Havenbrook, aching but alive."
                : waking == static_cast<int>(W::Nightmare)    ? "The nightmare jolted you awake."
                : waking == static_cast<int>(W::Stone)        ? "You wake before dawn, rested."
                                                              : "You wake at dawn, rested.";
    }
    to->guests.push_back(std::move(g));
    to->SeatOf(seat_no) = std::move(state);
    s.where = to;
    s.queue.clear();
    SendEnter(server, seat_no, s, woke, caption);
    return true;
}

void Host::Night(World& home) {
    // Dawn comes at once when everyone is abed or dreaming. Until then a
    // sleeper rests at the clock's own pace.
    if (!home.clock.IsNight() && !home.clock.CanSleep()) return;
    int abed = 0, up = 0;
    const auto count = [&](const Player& p, const World& w) {
        if (p.absent || p.away) return;
        if (p.resting) ++abed;
        else if (!w.InDream()) ++up;
    };
    count(home.player, home);
    for (World* w : AllWorlds(home)) for (const auto& g : w->guests) if (!g->puppet) count(*g, *w);
    if (abed == 0 || up > 0) return;

    home.clock.SkipToDawn();
    const auto rise = [&](Player& p) { if (p.resting) { p.resting = false; p.Rest(); return true; } return false; };
    if (rise(home.player)) home.PushRequest(Toast("Dawn breaks. You slept the night through, and wake rested."));
    for (auto& [seat_no, s] : seats)
        if (s.where)
            if (Player* g = s.where->Guest(seat_no))
                if (rise(*g)) s.tell.panels.push_back(ToPanel(Toast("Dawn breaks. You slept the night through, and wake rested.")));
}

// --- what is theirs to hear -------------------------------------------------------------------------

void Host::Gather(uint8_t seat_no, Seat& s) {
    // Someone at this machine has no copy to reconcile: their bag is the bag,
    // their journal is the journal, and the game opens their panels itself.
    if (!s.where || s.local) return;
    Player* g = s.where->Guest(seat_no);
    if (!g) return;
    SeatState& state = s.where->SeatOf(seat_no);

    // Their bag against what their machine believes it holds: the difference
    // is what the world gave or took -- a coin picked up, logs chopped, raw
    // meat cooked -- and is sent to be done to the real one.
    const std::map<string, int> now = Counts(g->inventory);
    if (now != s.mirror) {
        for (const auto& kv : now) {
            const auto had = s.mirror.find(kv.first);
            const int before = had == s.mirror.end() ? 0 : had->second;
            if (kv.second != before) s.tell.bag.push_back({kv.first, kv.second - before});
        }
        for (const auto& kv : s.mirror)
            if (!now.count(kv.first)) s.tell.bag.push_back({kv.first, -kv.second});
        s.mirror = now;
    }
    for (const auto& [skill, amount] : g->TakeXpDrops())
        s.tell.xp.push_back({static_cast<uint8_t>(skill), amount});
    g->TakeLevelUps();

    for (const QuestEvent& e : state.journal.relayed) {
        net::Delta::Quest q;
        q.type = static_cast<uint8_t>(e.type);
        q.target = e.target; q.secondary = e.secondary; q.map = e.map_id; q.amount = e.amount;
        s.tell.quests.push_back(std::move(q));
    }
    state.journal.relayed.clear();

    for (const WorldRequest& r : state.requests) s.tell.panels.push_back(ToPanel(r));
    state.requests.clear();

    // The chain counter is on their screen, but the blows land here.
    const int chain = g->ChainHits();
    if (chain > s.chain) {
        for (int i = s.chain; i < chain; ++i)
            s.tell.chain.push_back(g->ChainTrail().empty() ? string("Hit") : g->ChainTrail().back());
    } else if (chain < s.chain && chain == 0) {
        s.tell.chain.push_back("");         // broken
    }
    s.chain = chain;
}

void Host::Journals(World& home) {
    // What each world showed and marked this frame.
    const vector<World*> worlds = AllWorlds(home);
    for (World* w : worlds) {
        for (const FloatingText& t : w->text_log) {
            net::Delta::Text out;
            out.text = t.text;
            out.x = Px(t.x); out.y = Px(t.y);
            out.r = t.color.r; out.g = t.color.g; out.b = t.color.b; out.a = t.color.a;
            out.life = static_cast<uint8_t>(std::clamp(std::lround(t.max_life * 10.0f), 1l, 255l));
            for (auto& [seat_no, s] : seats) if (s.where == w) s.tell.texts.push_back(out);
        }
        w->text_log.clear();
    }
    // A chest opened or a tree felled is so everywhere, for everyone.
    for (World* w : worlds) {
        const vector<string> flags = std::move(w->flag_log);
        const auto picked = std::move(w->picked_log);
        w->flag_log.clear();
        w->picked_log.clear();
        for (const string& key : flags) {
            if (PrivateFlag(key)) continue;
            for (World* other : worlds) {
                if (other == w) continue;
                const bool was = other->journal;
                other->journal = false;           // told, not retold
                other->SetFlag(key);
                other->journal = was;
            }
            for (auto& [seat_no, s] : seats) s.tell.flags.push_back(key);
        }
        for (const auto& [key, when] : picked) {
            for (World* other : worlds) if (other != w) other->SetPicked(key, when);
            for (auto& [seat_no, s] : seats) s.tell.picked.push_back({key, when});
        }
    }
}

void Host::Tell(float dt, net::Server& server, World& home) {
    // The traders' shelves, when someone has bought from one.
    if ((since_ledger += dt) > 0.5f) {
        since_ledger = 0.0f;
        const string ledger = home.shops.ToJson().dump();
        if (ledger != ledger_told) {
            ledger_told = ledger;
            for (auto& [seat_no, s] : seats) s.tell.ledger = ledger;
        }
    }
    // What the host is wearing.
    if (!home.player.absent) {
        const net::Outfit now = OutfitOf(home.player, host_seat);
        if (!host_outfit_sent || !SameOutfit(now, host_outfit)) {
            host_outfit = now;
            host_outfit_sent = true;
            server.SendToGuests(net::Channel::Reliable, net::Encode(now));
        }
    }
    for (auto& [seat_no, s] : seats) {
        if (s.local) {
            // They saw and heard it all first-hand. What they wear is told to
            // friends across the wire, as the host's own is.
            s.tell = net::Delta();
            if (const Player* g = s.where ? s.where->Guest(seat_no) : nullptr) {
                const net::Outfit now = OutfitOf(*g, seat_no);
                if (!s.has_outfit || !SameOutfit(now, s.outfit)) {
                    s.outfit = now;
                    s.has_outfit = true;
                    server.SendToGuests(net::Channel::Reliable, net::Encode(now));
                }
            }
            continue;
        }
        if (s.tell.Empty()) continue;
        server.SendToSeat(seat_no, net::Channel::Reliable, net::Encode(s.tell));
        s.tell = net::Delta();
    }

    since_snapshot += dt;
    if (since_snapshot < SNAPSHOT_INTERVAL) return;
    since_snapshot = std::fmod(since_snapshot, SNAPSHOT_INTERVAL);

    for (auto& [seat_no, s] : seats) {
        if (!s.where || s.local) continue;
        World& w = *s.where;
        const Player* me = w.Guest(seat_no);
        if (!me) continue;
        const SeatState& state = w.SeatOf(seat_no);

        net::Snapshot snap;
        snap.time_ms  = static_cast<uint32_t>(clock_ms);
        snap.ack_seq  = s.last_applied;
        snap.day      = static_cast<uint16_t>(std::clamp(home.clock.Day(), 1, 65535));
        snap.hours    = home.clock.Hours();
        snap.map_tag  = net::MapTag(w.MapId());
        snap.heal_ack = s.heals;
        snap.resting  = me->resting;
        if (state.gather_index >= 0 && state.gather_needed > 0.0f) {
            snap.gather = static_cast<uint8_t>(std::clamp(state.gather_timer / state.gather_needed, 0.0f, 1.0f) * 255.0f);
            snap.gather_clip = me->GatherClip() + "|" + me->GatherModel();
        }
        if (&w == &home && !home.player.absent) snap.players.push_back(StateOf(home.player, host_seat));
        for (const auto& g : w.guests)
            if (!g->puppet && !g->away && snap.players.size() < static_cast<size_t>(net::MAX_SEATS))
                snap.players.push_back(StateOf(*g, g->seat));

        const auto near = [&](float x, float y) { return Length(x - me->x, y - me->y) <= RELEVANCE_RADIUS; };
        for (size_t i = 0; i < w.enemies.size() && snap.enemies.size() < net::MAX_ENEMIES_TOLD; ++i) {
            const Enemy& e = *w.enemies[i];
            if (e.CorpseGone() || !near(e.x, e.y)) continue;
            const Enemy::Posed told = e.Told();
            net::EnemyState es;
            es.id = static_cast<uint16_t>(i + 1);
            es.x = Px(told.x); es.y = Px(told.y);
            es.bits = static_cast<uint8_t>((told.facing & 3) | ((told.state & 7) << 2) | (told.hurt ? 32 : 0) | (told.bar ? 64 : 0));
            es.clip = ClipIndex(e.sprite.Def(), told.clip);
            es.frame = told.frame; es.heavy = told.heavy; es.alpha = told.alpha;
            es.hp = static_cast<uint16_t>(std::clamp(told.hp, 0, 65535));
            snap.enemies.push_back(es);
        }
        for (const Pickup& p : w.pickups) {
            if (p.collected || !near(p.x, p.y) || snap.pickups.size() >= net::MAX_PICKUPS_TOLD) continue;
            snap.pickups.push_back({p.net_id, Px(p.x), Px(p.y), static_cast<uint16_t>(std::clamp(p.qty, 0, 65535)), p.item_id});
        }
        for (const Projectile& p : w.projectiles) {
            if (p.finished || !p.def || !near(p.x, p.y) || snap.shots.size() >= net::MAX_SHOTS_TOLD) continue;
            snap.shots.push_back({p.net_id, Px(p.x), Px(p.y), Px(p.vx), Px(p.vy), p.from_player, p.def->id});
        }
        for (const GroundEffect& g : w.ground_effects) {
            if (g.finished || !near(g.x, g.y) || snap.patches.size() >= net::MAX_PATCHES_TOLD) continue;
            net::PatchState ps;
            ps.x = Px(g.x); ps.y = Px(g.y);
            ps.radius = static_cast<uint16_t>(std::clamp(g.radius, 0.0f, 65535.0f));
            ps.element = static_cast<uint8_t>(g.element);
            ps.life = static_cast<uint8_t>(std::clamp(g.life * 10.0f, 0.0f, 255.0f));
            ps.max_life = static_cast<uint8_t>(std::clamp(g.max_life * 10.0f, 0.0f, 255.0f));
            ps.active = g.Active();
            ps.from_player = g.from_player;
            ps.kind = g.rain ? 1 : 0;
            snap.patches.push_back(ps);
        }
        server.SendToSeat(seat_no, net::Channel::Unreliable, net::Encode(snap));
    }
}

void Host::SendEnter(net::Server& server, uint8_t seat_no, Seat& s, bool woke, const string& caption) {
    World& w = *s.where;
    const Player* g = w.Guest(seat_no);
    net::Enter e;
    e.map   = w.MapId();
    e.x     = g ? g->x : 0.0f;
    e.y     = g ? g->y : 0.0f;
    e.day   = static_cast<uint16_t>(std::clamp(w.clock.Day(), 1, 65535));
    e.hours = w.clock.Hours();
    for (const string& key : w.Flags()) if (!PrivateFlag(key)) e.flags.push_back(key);
    for (const auto& kv : w.PickedHerbs()) e.picked.push_back({kv.first, kv.second});
    e.ledger = ledger_told;
    e.woke = woke;
    e.caption = caption;
    server.SendToSeat(seat_no, net::Channel::Reliable, net::Encode(e));
}

void Host::SendOutfits(net::Server& server, uint8_t to_seat, World& home) {
    if (!home.player.absent)
        server.SendToSeat(to_seat, net::Channel::Reliable, net::Encode(OutfitOf(home.player, host_seat)));
    for (const auto& [seat_no, s] : seats)
        if (seat_no != to_seat && s.has_outfit)
            server.SendToSeat(to_seat, net::Channel::Reliable, net::Encode(s.outfit));
}

void Host::Keep(const Seat& s, const Player* g) {
    if (s.sheet.empty() || kept_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(kept_dir, ec);
    std::ofstream out(kept_dir + "/" + SafeFileName(s.name) + ".json", std::ios::trunc);
    if (!out) return;
    json j;
    try { j = json::parse(s.sheet); } catch (const std::exception&) { return; }
    if (g && s.where) j["place"] = {{"map", s.where->MapId()}, {"x", g->x}, {"y", g->y}};
    out << j.dump(1);
}

// -----------------------------------------------------------------------------
//  Guest
// -----------------------------------------------------------------------------

void Guest::Reset(World& world) {
    *this = Guest();
    world.guests.clear();
    world.visitor_acts.clear();
    world.shops.journal = false;
    world.shops.sales.clear();
}

void Guest::SetTheDay(World& world) const {
    if (pending_enter && !enter.map.empty()) world.clock.Set(enter.day, enter.hours);
}

void Guest::Arrived(World& world) {
    pending_enter = false;
    // A new map is a new path: nothing remembered is about this place.
    path.clear();
    beasts.clear();
    for (auto& [seat, p] : puppets) p.heard.clear();
    world.guests.clear();
    for (auto& [seat, p] : puppets) p.worn = false;
    world.player.hands_external = true;
    world.visitor_acts.clear();
    world.shops.journal = true;

    // The world as it stands: what is open, what is down, what is sold.
    for (const string& key : enter.flags) world.SetFlag(key);
    for (const auto& [key, when] : enter.picked) world.SetPicked(key, when);
    if (!enter.ledger.empty()) {
        try { world.shops.FromJson(json::parse(enter.ledger)); } catch (const std::exception&) {}
        world.shops.sales.clear();
    }
    world.clock.Set(enter.day, enter.hours);
    if (enter.woke) {
        if (world.player.Fallen()) world.player.Respawn(enter.x, enter.y);
        world.player.Rest();
        hp_applied = world.player.hp;
        if (!enter.caption.empty()) world.PushRequest(Toast(enter.caption));
    }
    resting = false;
}

void Guest::BeforeStep(World& world, const Input* device) {
    world.player.hands_external = true;
    world.player.hands = (device ? PlayerInput::FromDevice(*device) : PlayerInput{}).Quantised();
    if (world.player.input_locked) world.player.hands = PlayerInput{};
    device_hands = world.player.hands;
    // Abed, the hands are sent -- a move gets up, and that is the host's to
    // see -- but the character here stays where it lies.
    world.player.resting = resting;
}

void Guest::AfterStep(World& world, net::Client& client, float quantised_dt) {
    if (!client.Seated()) return;
    ++seq;
    path.push_back({seq, world.player.x, world.player.y});
    if (path.size() > 256) path.pop_front();

    // What is sent is what the device said. Up and about that is what was
    // used; abed the world stills the hands here, and the host must still see
    // the move that gets up.
    recent.push_back(ToStep(device_hands, quantised_dt));
    if (recent.size() > INPUT_REDUNDANCY) recent.pop_front();

    net::InputFrames frames;
    frames.first_seq = seq + 1 - static_cast<uint32_t>(recent.size());
    frames.steps.assign(recent.begin(), recent.end());
    frames.mana = static_cast<uint16_t>(std::clamp(world.player.Mana(), 0, 65535));
    if (const Enemy* t = world.targeting.Current()) {
        for (size_t i = 0; i < world.enemies.size(); ++i)
            if (world.enemies[i].get() == t) frames.aim_id = static_cast<uint16_t>(i + 1);
        frames.aim_locked = world.targeting.IsLocked();
    }
    client.SendGame(net::Channel::Unreliable, net::Encode(frames));
}

string Guest::MakeSheet(const World& world, const QuestLog* journal) {
    json player = world.player.ToJson();
    // Where they stand and how hurt they are is the host's to know.
    for (const char* volatile_key : {"x", "y", "facing", "hp", "mana"}) player.erase(volatile_key);
    json flags = json::array();
    for (const string& key : world.Flags()) if (PrivateFlag(key)) flags.push_back(key);
    json active = json::array();
    if (journal) for (const string& id : journal->Active()) active.push_back(id);
    return json{{"player", player}, {"flags", flags}, {"active", active}}.dump();
}

void Guest::Update(float dt, net::Client& client, World& world, const GameContext& ctx) {
    // Food eaten here is hit points the host has not heard of.
    Player& me = world.player;
    if (client.Seated() && hp_applied >= 0 && me.hp > hp_applied && !me.Fallen()) {
        net::Action heal;
        heal.kind = net::Action::Heal;
        heal.n = me.hp - hp_applied;
        heal.x = static_cast<float>(++heals_sent);
        client.SendGame(net::Channel::Reliable, net::Encode(heal));
        hp_applied = me.hp;
    }

    for (const net::Bytes& bytes : client.TakeGameMessages()) {
        switch (static_cast<net::MsgType>(net::PeekType(bytes))) {
            case net::MsgType::Enter: {
                net::Enter e;
                if (!net::Decode(bytes, e)) break;
                enter = e;
                pending_enter = true;
                break;
            }
            case net::MsgType::Snapshot: {
                net::Snapshot snap;
                if (net::Decode(bytes, snap) && !pending_enter) OnSnapshot(snap, client, world, ctx);
                break;
            }
            case net::MsgType::Outfit: {
                net::Outfit o;
                if (!net::Decode(bytes, o) || o.seat == client.Seat()) break;
                Puppet& p = puppets[o.seat];
                p.outfit = o;
                p.has_outfit = true;
                p.worn = false;
                break;
            }
            case net::MsgType::Delta: {
                net::Delta d;
                if (net::Decode(bytes, d)) OnDelta(d, world, ctx);
                break;
            }
            default: break;
        }
    }

    if (client.Seated() && world.CurrentMap().Loaded() && !pending_enter) {
        SendActs(client, world);
        // The character, when it has changed; not more than four times a second.
        if ((since_sheet += dt) >= 0.25f) {
            since_sheet = 0.0f;
            const string sheet = MakeSheet(world, ctx.quests);
            if (sheet != sheet_sent) {
                sheet_sent = sheet;
                client.SendGame(net::Channel::Reliable, net::Encode(net::Sheet{sheet}));
            }
        }
    }

    // Shots fly on between the host's words about them.
    for (Projectile& p : world.projectiles) { p.x += p.vx * dt; p.y += p.vy * dt; p.spin_angle += dt * 9.0f; }

    PosePuppets(dt, client, world, ctx);
    PoseBeasts(dt, world);
}

void Guest::SendActs(net::Client& client, World& world) {
    for (const World::VisitorAct& act : world.visitor_acts) {
        net::Action a;
        a.kind = static_cast<uint8_t>(act.kind);
        a.a = act.a; a.b = act.b; a.n = act.n;
        client.SendGame(net::Channel::Reliable, net::Encode(a));
    }
    world.visitor_acts.clear();
    for (const ShopLedger::Sale& sale : world.shops.sales) {
        net::Action a;
        a.kind = net::Action::ShopSold;
        a.a = sale.shop; a.b = sale.item; a.n = sale.qty;
        client.SendGame(net::Channel::Reliable, net::Encode(a));
    }
    world.shops.sales.clear();
}

void Guest::OnDelta(const net::Delta& d, World& world, const GameContext& ctx) {
    Player& me = world.player;
    for (const net::Delta::Text& t : d.texts)
        world.AddText(t.text, t.x, t.y, {t.r, t.g, t.b, t.a}, t.life / 10.0f);
    for (const string& key : d.flags) world.SetFlag(key);
    for (const auto& [key, when] : d.picked) world.SetPicked(key, when);
    for (const net::Delta::Panel& p : d.panels) world.PushRequest(FromPanel(p));
    for (const net::Delta::Item& it : d.bag) {
        if (it.qty > 0) {
            const int added = me.inventory.Add(it.id, it.qty);
            // No room after all: it goes back on the ground, at their feet.
            if (added < it.qty) world.visitor_acts.push_back({net::Action::Drop, it.id, "", it.qty - added});
        } else if (it.qty < 0) {
            me.inventory.Remove(it.id, std::min(-it.qty, me.inventory.Count(it.id)));
        }
    }
    for (const net::Delta::Xp& x : d.xp) me.GrantXp(x.skill, x.amount);
    if (ctx.quests)
        for (const net::Delta::Quest& q : d.quests) {
            QuestEvent e;
            e.type = static_cast<ObjectiveType>(q.type);
            e.target = q.target; e.secondary = q.secondary; e.map_id = q.map; e.amount = q.amount;
            ctx.quests->Notify(e, me.inventory);
        }
    for (const string& label : d.chain) { if (label.empty()) me.BreakChain(); else me.CountChainHit(label); }
    for (const net::Delta::Sound& s : d.sounds) {
        const Sfx sfx = static_cast<Sfx>(std::min<uint8_t>(s.sfx, static_cast<uint8_t>(Sfx::Count) - 1));
        if (s.placed) Audio::PlayAt(sfx, s.x, s.y, s.volume / 255.0f, s.pitch / 100.0f);
        else          Audio::Play(sfx, s.volume / 255.0f, s.pitch / 100.0f);
    }
    if (!d.ledger.empty()) {
        try { world.shops.FromJson(json::parse(d.ledger)); } catch (const std::exception&) {}
    }
}

void Guest::OnSnapshot(const net::Snapshot& snap, net::Client& client, World& world, const GameContext& ctx) {
    if (snap.map_tag != net::MapTag(world.MapId())) return;      // about a map we have left
    if (heard_any && snap.time_ms < newest_ms) return;          // newest wins
    newest_ms = snap.time_ms;
    if (!heard_any) play_ms = static_cast<double>(snap.time_ms) - INTERP_DELAY_MS;
    heard_any = true;

    world.clock.Set(snap.day, snap.hours);      // one day for everyone
    resting = snap.resting;
    {
        const size_t bar = snap.gather_clip.find('|');
        const string clip = snap.gather_clip.substr(0, bar);
        const string model = bar == string::npos ? string() : snap.gather_clip.substr(bar + 1);
        world.ShowGather(snap.gather / 255.0f, clip, model);
    }

    for (const net::PlayerState& st : snap.players) {
        if (st.seat != client.Seat()) {
            Puppet& p = puppets[st.seat];
            p.heard.push_back({snap.time_ms, st});
            if (p.heard.size() > 40) p.heard.pop_front();
            p.silent = 0.0f;
            continue;
        }

        Player& me = world.player;
        // How hurt we are is the host's to say -- once it has counted every
        // bite of food we have told it about.
        if (snap.heal_ack == heals_sent) {
            if ((st.flags & net::PlayerState::Dead) && !me.Fallen()) {
                me.Damage(me.hp + 9999);
                me.skills.SetCurrent(SKILL_HITPOINTS, 0);
            } else if (!(st.flags & net::PlayerState::Dead) && !me.Fallen() && st.hp != me.hp) {
                if (st.hp < me.hp) me.BreakChain();
                me.hp = std::clamp<int>(st.hp, 0, me.max_hp);
                me.skills.SetCurrent(SKILL_HITPOINTS, me.hp);
            }
            hp_applied = me.hp;
        }

        // Where did the host say that step ended?
        if (snap.ack_seq <= acked && acked != 0) continue;
        acked = snap.ack_seq;
        while (!path.empty() && path.front().seq < acked) path.pop_front();
        if (path.empty() || path.front().seq != acked) continue;

        float ex = st.x - path.front().x, ey = st.y - path.front().y;
        const float error = Length(ex, ey);
        last_error = error;
        if (error <= RECONCILE_THRESHOLD) continue;
        ++corrections;
        if (error > GLIDE_DISTANCE && error <= SNAP_DISTANCE) { ex *= 0.5f; ey *= 0.5f; }
        me.x += ex;
        me.y += ey;
        for (Remembered& r : path) { r.x += ex; r.y += ey; }
        if (world.map.Blocked(me.Bounds())) {
            const float bx = st.x - me.x, by = st.y - me.y;
            me.x = st.x; me.y = st.y;
            for (Remembered& r : path) { r.x += bx; r.y += by; }
        }
        if (error > SNAP_DISTANCE) world.camera.SnapTo(me.x, me.y);
    }

    // The monsters near us.
    for (const net::EnemyState& es : snap.enemies) {
        Beast& b = beasts[es.id];
        b.heard.push_back({snap.time_ms, es});
        if (b.heard.size() > 8) b.heard.pop_front();
        b.silent = 0.0f;
    }

    // What lies on the ground, what is in the air, what is burning: as the
    // host has them, whole, each time.
    world.pickups.clear();
    for (const net::PickupState& ps : snap.pickups) {
        Pickup p;
        p.net_id = ps.id;
        p.item_id = ps.item;
        p.qty = ps.qty;
        p.x = ps.x; p.y = ps.y;
        p.life = 10.0f;
        p.bob = static_cast<float>(play_ms) * 0.0034f + static_cast<float>(ps.id % 7);
        if (const ItemDef* d = ctx.items ? ctx.items->Get(ps.item) : nullptr) p.icon = d->icon;
        world.pickups.push_back(std::move(p));
    }
    world.projectiles.clear();
    for (const net::ShotState& ss : snap.shots) {
        const ProjectileDef* def = ctx.projectiles ? ctx.projectiles->Get(ss.def) : nullptr;
        if (!def) continue;
        Projectile p;
        p.def = def;
        p.net_id = ss.id;
        p.x = ss.x; p.y = ss.y;
        p.vx = ss.vx; p.vy = ss.vy;
        p.angle = atan2f(p.vy, p.vx);
        p.element = def->element;
        p.from_player = ss.from_player;
        world.projectiles.push_back(std::move(p));
    }
    world.ground_effects.clear();
    for (const net::PatchState& ps : snap.patches) {
        GroundEffect g;
        g.x = ps.x; g.y = ps.y;
        g.radius = ps.radius;
        g.element = static_cast<Element>(ps.element);
        g.life = ps.life / 10.0f;
        g.max_life = std::max(0.1f, ps.max_life / 10.0f);
        g.delay = ps.active ? 0.0f : 0.1f;
        g.from_player = ps.from_player;
        g.rain = ps.kind == 1;
        world.ground_effects.push_back(g);
    }
}

void Guest::PosePuppets(float dt, net::Client& client, World& world, const GameContext& ctx) {
    if (heard_any) {
        play_ms += static_cast<double>(dt) * 1000.0;
        const double target = static_cast<double>(newest_ms) - INTERP_DELAY_MS;
        if (std::fabs(target - play_ms) > 400.0) play_ms = target;
        else play_ms += (target - play_ms) * std::min(1.0, static_cast<double>(dt) * 2.0);
    }

    for (auto it = puppets.begin(); it != puppets.end();) {
        bool named = false;
        for (const net::SeatInfo& info : client.Roster()) named |= info.seat == it->first;
        if (!named) {
            world.RemoveGuest(it->first);
            it = puppets.erase(it);
            continue;
        }
        // Not heard of for a while: on another map. They are not here.
        if ((it->second.silent += dt) > 1.0f && !it->second.heard.empty()) {
            it->second.heard.clear();
            it->second.worn = false;
            world.RemoveGuest(it->first);
        }
        ++it;
    }

    for (auto& [seat, p] : puppets) {
        if (p.heard.empty()) continue;
        Player* g = world.Guest(seat);
        if (!g) {
            string name, look;
            for (const net::SeatInfo& info : client.Roster())
                if (info.seat == seat) { name = info.name; look = info.look; }
            g = world.AddGuest(seat, name, p.has_outfit ? p.outfit.look : look, ctx);
            g->puppet = true;
            p.worn = false;
        }
        if (p.has_outfit && !p.worn) {
            Wear(*g, p.outfit, ctx);
            g->puppet = true;
            p.worn = true;
        }

        while (p.heard.size() >= 2 && static_cast<double>(p.heard[1].time_ms) <= play_ms) p.heard.pop_front();
        const Heard& a = p.heard.front();
        float x = a.state.x, y = a.state.y, lift = a.state.lift;
        if (p.heard.size() >= 2 && play_ms > static_cast<double>(a.time_ms)) {
            const Heard& b = p.heard[1];
            const double span = static_cast<double>(b.time_ms) - static_cast<double>(a.time_ms);
            const float t = span > 0.0 ? static_cast<float>(std::clamp((play_ms - a.time_ms) / span, 0.0, 1.0)) : 1.0f;
            if (Length(b.state.x - a.state.x, b.state.y - a.state.y) < SNAP_DISTANCE) {
                x += (b.state.x - a.state.x) * t;
                y += (b.state.y - a.state.y) * t;
                lift += (b.state.lift - a.state.lift) * t;
            }
        }
        g->Pose(x, y, static_cast<Facing>(std::min<uint8_t>(a.state.facing, 3)), a.state.clip, a.state.frame, ctx.items);
        g->draw_lift = lift;
        g->hp = a.state.hp;
        g->max_hp = std::max<int>(1, a.state.max_hp);
    }
}

void Guest::PoseBeasts(float dt, World& world) {
    Enemy::Posed unseen;
    unseen.state = static_cast<uint8_t>(Enemy::State::Dead);
    unseen.alpha = 0;
    for (auto it = beasts.begin(); it != beasts.end();) {
        const uint16_t id = it->first;
        Beast& b = it->second;
        Enemy* e = (id >= 1 && id <= world.enemies.size()) ? world.enemies[id - 1].get() : nullptr;
        if (!e || !e->puppet) { it = beasts.erase(it); continue; }
        // Not spoken of lately: out of sight, or gone.
        if ((b.silent += dt) > 0.5f) {
            unseen.x = e->x; unseen.y = e->y;
            e->Pose(unseen);
            if (world.targeting.Current() == e) world.targeting.Clear();
            it = beasts.erase(it);
            continue;
        }
        while (b.heard.size() >= 2 && static_cast<double>(b.heard[1].first) <= play_ms) b.heard.pop_front();
        const net::EnemyState& a = b.heard.front().second;
        float x = a.x, y = a.y;
        if (b.heard.size() >= 2 && play_ms > static_cast<double>(b.heard.front().first)) {
            const net::EnemyState& n = b.heard[1].second;
            const double span = static_cast<double>(b.heard[1].first) - static_cast<double>(b.heard.front().first);
            const float t = span > 0.0 ? static_cast<float>(std::clamp((play_ms - b.heard.front().first) / span, 0.0, 1.0)) : 1.0f;
            if (Length(static_cast<float>(n.x - a.x), static_cast<float>(n.y - a.y)) < SNAP_DISTANCE) {
                x += (n.x - a.x) * t;
                y += (n.y - a.y) * t;
            }
        }
        Enemy::Posed p;
        p.x = x; p.y = y;
        p.facing = a.bits & 3;
        p.state = (a.bits >> 2) & 7;
        p.hurt = (a.bits & 32) != 0;
        p.bar = (a.bits & 64) != 0;
        p.frame = a.frame; p.heavy = a.heavy; p.alpha = a.alpha;
        p.hp = a.hp;
        p.clip = ClipName(e->sprite.Def(), a.clip);
        e->Pose(p);
        ++it;
    }
}

} // namespace coop
