#include "coop.h"

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
    if (p.IsDead())     s.flags |= net::PlayerState::Dead;
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
        // Only what exists, and only where it goes: the host believes a friend
        // about what they are wearing, not about what an item is.
        const ItemDef* def = (ctx.items && !id.empty()) ? ctx.items->Get(id) : nullptr;
        if (def && def->slot == static_cast<int>(slot)) p.equipment.Equip(static_cast<int>(slot), id);
    }
}

// -----------------------------------------------------------------------------
//  Host
// -----------------------------------------------------------------------------

void Host::Reset(World& world) {
    seats.clear();
    world.guests.clear();
    host_outfit_sent = false;
    since_snapshot = 0.0f;
}

uint32_t Host::LastApplied(uint8_t seat) const {
    auto it = seats.find(seat);
    return it == seats.end() ? 0 : it->second.last_applied;
}

size_t Host::Queued(uint8_t seat) const {
    auto it = seats.find(seat);
    return it == seats.end() ? 0 : it->second.queue.size();
}

void Host::SendEnter(net::Server& server, uint8_t seat, Seat& s, World& world) {
    Player* g = world.Guest(seat);
    net::Enter e;
    e.map   = world.MapId();
    e.x     = g ? g->x : world.player.x;
    e.y     = g ? g->y : world.player.y;
    e.day   = static_cast<uint16_t>(std::clamp(world.clock.Day(), 1, 65535));
    e.hours = world.clock.Hours();
    server.SendToSeat(seat, net::Channel::Reliable, net::Encode(e));
    s.entered_map = e.map;
    // Steps taken on the old map are about somewhere else.
    s.queue.clear();
}

void Host::SendOutfits(net::Server& server, uint8_t to_seat, World& world) {
    // The host's own seat number is whoever the roster marks as host.
    for (const net::SeatInfo& info : server.Roster()) {
        if (info.seat == to_seat) continue;
        if (info.host) {
            server.SendToSeat(to_seat, net::Channel::Reliable, net::Encode(OutfitOf(world.player, info.seat)));
        } else {
            auto it = seats.find(info.seat);
            if (it != seats.end() && it->second.has_outfit)
                server.SendToSeat(to_seat, net::Channel::Reliable, net::Encode(it->second.outfit));
        }
    }
}

void Host::Update(float dt, net::Server& server, World& world, const GameContext& ctx, bool in_world) {
    clock_ms += static_cast<double>(dt) * 1000.0;
    in_world = in_world && world.CurrentMap().Loaded();

    // --- who is here --------------------------------------------------------------
    uint8_t host_seat = 0;
    std::set<uint8_t> present;
    for (const net::SeatInfo& info : server.Roster()) {
        if (info.host) { host_seat = info.seat; continue; }
        present.insert(info.seat);
    }
    for (auto it = seats.begin(); it != seats.end();) {
        if (present.count(it->first)) { ++it; continue; }
        world.RemoveGuest(it->first);
        it = seats.erase(it);
    }
    for (const net::SeatInfo& info : server.Roster()) {
        if (info.host) continue;
        Seat& s = seats[info.seat];
        if (!in_world) {
            // No world to be in: back to the lobby with them.
            if (!s.entered_map.empty()) {
                server.SendToSeat(info.seat, net::Channel::Reliable, net::Encode(net::Enter{}));
                s.entered_map.clear();
                s.queue.clear();
            }
            world.RemoveGuest(info.seat);
            continue;
        }
        Player* g = world.Guest(info.seat);
        if (!g) {
            g = world.AddGuest(info.seat, info.name, info.look, ctx);
            // Dressed as a new character is until they say otherwise.
            if (s.has_outfit) Wear(*g, s.outfit, ctx);
            else for (const string& id : Player::StartingKit(g->sprite_id))
                if (const ItemDef* def = ctx.items ? ctx.items->Get(id) : nullptr)
                    if (def->slot != SLOT_NONE) g->equipment.Equip(def->slot, id);
            s.entered_map.clear();
        }
        if (s.entered_map != world.MapId() && !world.TransitionPending()) {
            SendEnter(server, info.seat, s, world);
            SendOutfits(server, info.seat, world);
        }
    }
    // --- what they have sent -------------------------------------------------------
    // Read even with no world to act in, so nothing piles up at the door; an
    // outfit is remembered for when there is one.
    for (const net::Server::Inbound& in : server.TakeGameMessages()) {
        auto it = seats.find(in.seat);
        if (it == seats.end()) continue;
        Seat& s = it->second;
        switch (static_cast<net::MsgType>(net::PeekType(in.data))) {
            case net::MsgType::InputFrames: {
                net::InputFrames frames;
                if (!in_world || s.entered_map != world.MapId() || !net::Decode(in.data, frames)) break;
                // Most of every packet is steps already taken: only the new.
                for (size_t i = 0; i < frames.steps.size(); ++i) {
                    const uint32_t seq = frames.first_seq + static_cast<uint32_t>(i);
                    if (seq < s.next_seq) continue;
                    // A gap is steps lost past the redundancy: they never
                    // happened here, and the client is put right by snapshot.
                    s.next_seq = seq + 1;
                    s.queue.push_back({seq, frames.steps[i]});
                    if (s.queue.size() > MAX_QUEUED_STEPS) s.queue.pop_front();
                }
                break;
            }
            case net::MsgType::Outfit: {
                net::Outfit o;
                if (!net::Decode(in.data, o)) break;
                o.seat = in.seat;                      // a seat speaks only for itself
                s.outfit = o;
                s.has_outfit = true;
                if (Player* g = world.Guest(in.seat)) Wear(*g, o, ctx);
                server.SendToGuests(net::Channel::Reliable, net::Encode(o), in.seat);
                break;
            }
            default: break;
        }
    }

    if (!in_world) { host_outfit_sent = false; return; }

    // --- their steps, by their clocks ------------------------------------------------
    for (auto& [seat, s] : seats) {
        Player* g = world.Guest(seat);
        if (!g || s.entered_map != world.MapId()) continue;
        int taken = 0;
        while (!s.queue.empty() && taken < MAX_STEPS_A_FRAME) {
            const auto [number, step] = s.queue.front();
            s.queue.pop_front();
            world.StepGuest(*g, ToHands(step), StepSeconds(step), ctx);
            s.last_applied = number;
            ++taken;
        }
        if (taken > 0) s.idle = 0.0f;
        else if ((s.idle += dt) > IDLE_AFTER)
            // Nothing from them -- a panel open, a hitch. Hands empty, the
            // swing they started finishes and the idle clip keeps breathing.
            world.StepGuest(*g, PlayerInput{}, dt, ctx);
    }

    // --- what the host is wearing ------------------------------------------------------
    {
        const net::Outfit now = OutfitOf(world.player, host_seat);
        if (!host_outfit_sent || !SameOutfit(now, host_outfit)) {
            host_outfit = now;
            host_outfit_sent = true;
            server.SendToGuests(net::Channel::Reliable, net::Encode(now));
        }
    }

    // --- where everyone is ---------------------------------------------------------------
    since_snapshot += dt;
    if (since_snapshot < SNAPSHOT_INTERVAL) return;
    since_snapshot = std::fmod(since_snapshot, SNAPSHOT_INTERVAL);

    net::Snapshot snap;
    snap.time_ms = static_cast<uint32_t>(clock_ms);
    snap.day     = static_cast<uint16_t>(std::clamp(world.clock.Day(), 1, 65535));
    snap.hours   = world.clock.Hours();
    snap.players.push_back(StateOf(world.player, host_seat));
    for (const auto& g : world.guests) snap.players.push_back(StateOf(*g, g->seat));
    for (auto& [seat, s] : seats) {
        if (s.entered_map != world.MapId()) continue;
        snap.ack_seq = s.last_applied;
        server.SendToSeat(seat, net::Channel::Unreliable, net::Encode(snap));
    }
}

// -----------------------------------------------------------------------------
//  Guest
// -----------------------------------------------------------------------------

void Guest::Reset(World& world) {
    *this = Guest();
    world.guests.clear();
}

void Guest::Arrived(World& world) {
    pending_enter = false;
    // A new map is a new path: nothing remembered is about this place.
    path.clear();
    for (auto& [seat, p] : puppets) p.heard.clear();
    world.player.hands_external = true;
}

void Guest::BeforeStep(World& world, const Input* device) {
    // Quantised before it is used, so the step taken here and the step the
    // host takes are taken with the same axis.
    world.player.hands_external = true;
    world.player.hands = (device ? PlayerInput::FromDevice(*device) : PlayerInput{}).Quantised();
    // A panel owns the hands: what is sent is what is done.
    if (world.player.input_locked) world.player.hands = PlayerInput{};
}

void Guest::AfterStep(World& world, net::Client& client, float quantised_dt) {
    if (!client.Seated()) return;
    ++seq;
    path.push_back({seq, world.player.x, world.player.y});
    if (path.size() > 256) path.pop_front();

    recent.push_back(ToStep(world.player.hands, quantised_dt));
    if (recent.size() > INPUT_REDUNDANCY) recent.pop_front();

    net::InputFrames frames;
    frames.first_seq = seq + 1 - static_cast<uint32_t>(recent.size());
    frames.steps.assign(recent.begin(), recent.end());
    client.SendGame(net::Channel::Unreliable, net::Encode(frames));
}

void Guest::Update(float dt, net::Client& client, World& world, const GameContext& ctx) {
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
                if (net::Decode(bytes, snap) && !pending_enter) OnSnapshot(snap, client, world);
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
            default: break;
        }
    }

    // What we are wearing, whenever it changes.
    if (client.Seated()) {
        const net::Outfit now = OutfitOf(world.player, client.Seat());
        if (!outfit_sent || !SameOutfit(now, my_outfit)) {
            my_outfit = now;
            outfit_sent = true;
            client.SendGame(net::Channel::Reliable, net::Encode(now));
        }
    }

    PosePuppets(dt, client, world, ctx);
}

void Guest::OnSnapshot(const net::Snapshot& snap, net::Client& client, World& world) {
    // Newest wins: ENet's unreliable channel already drops the out-of-order,
    // and this makes it true of any transport.
    if (heard_any && snap.time_ms < newest_ms) return;
    newest_ms = snap.time_ms;
    if (!heard_any) play_ms = static_cast<double>(snap.time_ms) - INTERP_DELAY_MS;
    heard_any = true;

    // One day for everyone.
    world.clock.Set(snap.day, snap.hours);

    for (const net::PlayerState& st : snap.players) {
        if (st.seat != client.Seat()) {
            Puppet& p = puppets[st.seat];
            p.heard.push_back({snap.time_ms, st});
            if (p.heard.size() > 40) p.heard.pop_front();
            p.silent = 0.0f;
            continue;
        }

        // --- our own: where did the server say that step ended? ----------------
        if (snap.ack_seq <= acked && acked != 0) continue;      // nothing new about us
        acked = snap.ack_seq;
        while (!path.empty() && path.front().seq < acked) path.pop_front();
        if (path.empty() || path.front().seq != acked) continue;  // older than we remember

        float ex = st.x - path.front().x, ey = st.y - path.front().y;
        const float error = Length(ex, ey);
        last_error = error;
        if (error <= RECONCILE_THRESHOLD) continue;
        ++corrections;

        Player& me = world.player;
        if (error > GLIDE_DISTANCE && error <= SNAP_DISTANCE) {
            // Visible if done at once: half now, the rest as it is measured
            // again in a twentieth of a second.
            ex *= 0.5f; ey *= 0.5f;
        }
        me.x += ex;
        me.y += ey;
        for (Remembered& r : path) { r.x += ex; r.y += ey; }
        // Never into a wall: where the server has us is somewhere we can stand.
        if (world.map.Blocked(me.Bounds())) {
            const float bx = st.x - me.x, by = st.y - me.y;
            me.x = st.x; me.y = st.y;
            for (Remembered& r : path) { r.x += bx; r.y += by; }
        }
        if (error > SNAP_DISTANCE) world.camera.SnapTo(me.x, me.y);
    }
}

void Guest::PosePuppets(float dt, net::Client& client, World& world, const GameContext& ctx) {
    // The moment being drawn: a tenth of a second behind the newest word,
    // moving at our own clock's pace and leaning toward where it should be.
    if (heard_any) {
        play_ms += static_cast<double>(dt) * 1000.0;
        const double target = static_cast<double>(newest_ms) - INTERP_DELAY_MS;
        if (std::fabs(target - play_ms) > 400.0) play_ms = target;
        else play_ms += (target - play_ms) * std::min(1.0, static_cast<double>(dt) * 2.0);
    }

    // Whoever the roster no longer names, or who has not been heard of for a
    // while (another map, from M4 on), is not here.
    for (auto it = puppets.begin(); it != puppets.end();) {
        bool named = false;
        for (const net::SeatInfo& info : client.Roster()) named |= info.seat == it->first;
        if (!named) {
            world.RemoveGuest(it->first);
            it = puppets.erase(it);
            continue;
        }
        if ((it->second.silent += dt) > 2.0f && !it->second.heard.empty()) {
            it->second.heard.clear();
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

        // The two words that bracket the moment.
        while (p.heard.size() >= 2 && static_cast<double>(p.heard[1].time_ms) <= play_ms) p.heard.pop_front();
        const Heard& a = p.heard.front();
        float x = a.state.x, y = a.state.y, lift = a.state.lift;
        if (p.heard.size() >= 2 && play_ms > static_cast<double>(a.time_ms)) {
            const Heard& b = p.heard[1];
            const double span = static_cast<double>(b.time_ms) - static_cast<double>(a.time_ms);
            const float t = span > 0.0 ? static_cast<float>(std::clamp((play_ms - a.time_ms) / span, 0.0, 1.0)) : 1.0f;
            // A gap too wide to be a walk is a door: do not slide across the map.
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

} // namespace coop
