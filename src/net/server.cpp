#include "server.h"
#include "datahash.h"
#include <algorithm>

namespace net {

Server::Server(Config c) : config(std::move(c)) {
    config.max_seats = std::clamp(config.max_seats, 1, MAX_SEATS);
}

void Server::Attach(std::unique_ptr<Transport> transport, bool local) {
    if (transport) ways.push_back({std::move(transport), local});
}

void Server::Shutdown() {
    for (Connection& c : connections)
        if (c.stage != Stage::Leaving) ways[c.transport].transport->Disconnect(c.peer);
    connections.clear();
    roster.clear();
    ways.clear();
}

void Server::Update(float dt) {
    for (size_t i = 0; i < ways.size(); ++i)
        for (const Packet& p : ways[i].transport->Poll())
            Handle(i, p);

    // Whoever connected and has said nothing is not a player.
    for (Connection& c : connections) {
        if (c.stage != Stage::Greeting) continue;
        c.age += dt;
        if (c.age >= HELLO_TIMEOUT) {
            c.stage = Stage::Leaving;
            ways[c.transport].transport->Disconnect(c.peer);
        }
    }
}

void Server::Handle(size_t transport, const Packet& packet) {
    switch (packet.type) {
        case Packet::Type::Connected: {
            Connection c;
            c.transport = transport;
            c.peer = packet.peer;
            c.local = ways[transport].local;
            connections.push_back(std::move(c));
            break;
        }
        case Packet::Type::Disconnected:
            Drop(transport, packet.peer);
            break;
        case Packet::Type::Data: {
            Connection* c = Find(transport, packet.peer);
            if (!c || c->stage == Stage::Leaving) break;
            const uint8_t type = PeekType(packet.data);
            if (c->stage == Stage::Greeting) {
                // Nothing is heard from anyone until they have said Hello.
                if (type == static_cast<uint8_t>(MsgType::Hello)) HandleHello(*c, packet.data);
                else RefuseAndDrop(*c, RefuseReason::Malformed, "The server expected a greeting first.");
            } else if (type == static_cast<uint8_t>(MsgType::Say)) {
                HandleSay(*c, packet.data);
            } else if (IsGameMessage(type)) {
                // Kept short: a client that floods is dropped from the front.
                if (inbound.size() >= 1024) inbound.erase(inbound.begin(), inbound.begin() + 256);
                inbound.push_back({c->info.seat, packet.channel, packet.data});
            }
            // Anything else from a seated player is a message from a later
            // milestone or a stray, and is let go by.
            break;
        }
    }
}

void Server::HandleHello(Connection& c, const Bytes& bytes) {
    if (!c.local) reached = true;

    Hello hello;
    if (!Decode(bytes, hello))
        return RefuseAndDrop(c, RefuseReason::Malformed, "The server could not read that greeting.");
    if (hello.magic != PROTOCOL_MAGIC)
        return RefuseAndDrop(c, RefuseReason::NotDreamQuest, "That is not a DreamQuest client.");
    if (hello.version != PROTOCOL_VERSION)
        return RefuseAndDrop(c, RefuseReason::Version,
            "This world speaks protocol " + std::to_string(PROTOCOL_VERSION) + " and your game speaks " +
            std::to_string(hello.version) + ". Whichever is older needs the newer build.");
    if (hello.data_hash != config.data_hash)
        return RefuseAndDrop(c, RefuseReason::Data,
            "Your data/ folder differs from the host's (" + ShortHash(hello.data_hash) + " against " +
            ShortHash(config.data_hash) + "). Both of you need the same build.");
    if (hello.maps_hash != config.maps_hash)
        return RefuseAndDrop(c, RefuseReason::Maps,
            "Your maps/ folder differs from the host's (" + ShortHash(hello.maps_hash) + " against " +
            ShortHash(config.maps_hash) + "). Both of you need the same build.");

    if (!config.password.empty() && hello.password != config.password)
        return RefuseAndDrop(c, RefuseReason::Password,
            hello.password.empty() ? "This world has a password. Ask the host for it."
                                   : "That is not this world's password.");

    const int seat = FreeSeat();
    if (seat < 0)
        return RefuseAndDrop(c, RefuseReason::Full,
            "All " + std::to_string(config.max_seats) + " seats in this world are taken.");

    std::string name = CleanLine(hello.name, MAX_NAME);
    if (name.empty()) name = "Traveller";
    c.info.seat = static_cast<uint8_t>(seat);
    c.info.name = UniqueName(name);
    c.info.look = CleanLine(hello.look, MAX_LOOK);
    c.info.host = c.local;
    c.stage = Stage::Seated;
    RebuildRoster();

    Welcome welcome;
    welcome.seat = c.info.seat;
    welcome.max_seats = static_cast<uint8_t>(config.max_seats);
    welcome.bring_your_own = config.bring_your_own;
    welcome.world_name = config.world_name;
    welcome.roster = roster;
    SendTo(c, Encode(welcome));

    // Everyone else learns of the newcomer; the newcomer has just been told.
    const Bytes update = Encode(net::Roster{roster});
    for (const Connection& other : connections)
        if (other.stage == Stage::Seated && &other != &c) SendTo(other, update);
    Announce(c.info.name + " joined.");
}

void Server::HandleSay(Connection& c, const Bytes& bytes) {
    Say say;
    if (!Decode(bytes, say)) return;
    const std::string text = CleanLine(say.text, MAX_CHAT);
    if (text.empty()) return;
    Broadcast(Encode(Chat{c.info.seat, text}));
}

std::vector<Server::Inbound> Server::TakeGameMessages() {
    std::vector<Inbound> out;
    out.swap(inbound);
    return out;
}

void Server::SendToSeat(uint8_t seat, Channel channel, const Bytes& bytes) {
    for (const Connection& c : connections)
        if (c.stage == Stage::Seated && c.info.seat == seat)
            ways[c.transport].transport->Send(c.peer, channel, bytes);
}

void Server::SendToGuests(Channel channel, const Bytes& bytes, int except_seat) {
    for (const Connection& c : connections)
        if (c.stage == Stage::Seated && !c.local && c.info.seat != except_seat)
            ways[c.transport].transport->Send(c.peer, channel, bytes);
}

void Server::Announce(const std::string& text) {
    Broadcast(Encode(Chat{SERVER_SEAT, CleanLine(text, MAX_CHAT)}));
}

void Server::RefuseAndDrop(Connection& c, RefuseReason reason, const std::string& text) {
    SendTo(c, Encode(Refuse{reason, text}));
    c.stage = Stage::Leaving;
    // Disconnect waits for what has been sent, so the reason arrives first.
    ways[c.transport].transport->Disconnect(c.peer);
}

void Server::Drop(size_t transport, PeerId peer) {
    auto it = std::find_if(connections.begin(), connections.end(), [&](const Connection& c) {
        return c.transport == transport && c.peer == peer;
    });
    if (it == connections.end()) return;
    const bool was_seated = it->stage == Stage::Seated;
    const std::string name = it->info.name;
    connections.erase(it);
    if (!was_seated) return;
    RebuildRoster();
    Broadcast(Encode(net::Roster{roster}));
    Announce(name + " left.");
}

void Server::SendTo(const Connection& c, const Bytes& bytes) {
    ways[c.transport].transport->Send(c.peer, Channel::Reliable, bytes);
}

void Server::Broadcast(const Bytes& bytes) {
    for (const Connection& c : connections)
        if (c.stage == Stage::Seated) SendTo(c, bytes);
}

int Server::ReserveSeat(const std::string& name, const std::string& look) {
    const int seat = FreeSeat();
    if (seat < 0) return -1;
    SeatInfo info;
    info.seat = static_cast<uint8_t>(seat);
    info.name = UniqueName(CleanLine(name, MAX_NAME).empty() ? std::string("Player Two") : CleanLine(name, MAX_NAME));
    info.look = CleanLine(look, MAX_LOOK);
    reserved.push_back(info);
    RebuildRoster();
    Broadcast(Encode(net::Roster{roster}));
    Announce(info.name + " joined.");
    return seat;
}

void Server::ReleaseSeat(uint8_t seat) {
    for (auto it = reserved.begin(); it != reserved.end(); ++it) {
        if (it->seat != seat) continue;
        const std::string name = it->name;
        reserved.erase(it);
        RebuildRoster();
        Broadcast(Encode(net::Roster{roster}));
        Announce(name + " left.");
        return;
    }
}

void Server::RebuildRoster() {
    roster.clear();
    for (const SeatInfo& r : reserved) roster.push_back(r);
    for (const Connection& c : connections)
        if (c.stage == Stage::Seated) roster.push_back(c.info);
    std::sort(roster.begin(), roster.end(),
              [](const SeatInfo& a, const SeatInfo& b) { return a.seat < b.seat; });
}

Server::Connection* Server::Find(size_t transport, PeerId peer) {
    for (Connection& c : connections)
        if (c.transport == transport && c.peer == peer) return &c;
    return nullptr;
}

int Server::FreeSeat() const {
    for (int seat = 0; seat < config.max_seats; ++seat) {
        bool taken = std::any_of(connections.begin(), connections.end(), [&](const Connection& c) {
            return c.stage == Stage::Seated && c.info.seat == seat;
        });
        for (const SeatInfo& r : reserved) taken = taken || r.seat == seat;
        if (!taken) return seat;
    }
    return -1;
}

std::string Server::UniqueName(const std::string& wanted) const {
    const auto taken = [&](const std::string& name) {
        for (const SeatInfo& r : reserved) if (r.name == name) return true;
        return std::any_of(connections.begin(), connections.end(), [&](const Connection& c) {
            return c.stage == Stage::Seated && c.info.name == name;
        });
    };
    if (!taken(wanted)) return wanted;
    // Two friends both called Sam are Sam and Sam 2.
    for (int n = 2; n < 10; ++n) {
        const std::string suffix = " " + std::to_string(n);
        std::string base = wanted;
        if (base.size() + suffix.size() > MAX_NAME) base = CleanLine(base, MAX_NAME - suffix.size());
        if (!taken(base + suffix)) return base + suffix;
    }
    return wanted;
}

} // namespace net
