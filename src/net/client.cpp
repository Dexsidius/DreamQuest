#include "client.h"

namespace net {

bool Client::Start(std::unique_ptr<Transport> t, const std::string& host, uint16_t port,
                   const Hello& greeting) {
    Leave();
    roster.clear();
    reason.clear();
    refused = RefuseReason::None;
    hello = greeting;
    if (!t) { Finish(State::Lost, "No way to reach the network."); return false; }
    transport = std::move(t);
    if (!transport->Connect(host, port)) {
        const std::string why = transport->Error();
        transport.reset();
        Finish(State::Lost, why.empty() ? "Could not start connecting." : "Could not connect: " + why + ".");
        return false;
    }
    state = State::Connecting;
    waited = 0.0f;
    dialled_port = port ? port : DEFAULT_PORT;
    return true;
}

void Client::Update(float dt) {
    if (!transport) return;
    for (const Packet& p : transport->Poll()) {
        Handle(p);
        if (!transport) return;      // a refusal or a drop has hung up
    }
    if (Busy()) {
        waited += dt;
        if (waited >= CONNECT_TIMEOUT)
            Finish(State::Lost, state == State::Connecting
                       ? "Nobody answered. Check the name, that they are hosting, and that their "
                         "firewall allows UDP " + std::to_string(dialled_port) + "."
                       : "The host answered but never offered a seat.");
    }
}

void Client::Handle(const Packet& packet) {
    switch (packet.type) {
        case Packet::Type::Connected:
            server = packet.peer;
            state = State::Greeting;
            waited = 0.0f;
            transport->Send(server, Channel::Reliable, Encode(hello));
            break;

        case Packet::Type::Disconnected:
            // A refusal has already said why; do not overwrite it.
            if (state == State::Refused) { transport.reset(); server = NO_PEER; break; }
            Finish(State::Lost, state == State::Seated ? "The connection to the host was lost."
                              : state == State::Connecting ? "Nobody answered at that address."
                                                           : "The host hung up.");
            break;

        case Packet::Type::Data: {
            switch (static_cast<MsgType>(PeekType(packet.data))) {
                case MsgType::Welcome: {
                    Welcome w;
                    if (!Decode(packet.data, w)) break;
                    seat = w.seat;
                    world_name = w.world_name;
                    bring_your_own = w.bring_your_own;
                    roster = w.roster;
                    state = State::Seated;
                    break;
                }
                case MsgType::Refuse: {
                    Refuse r;
                    if (!Decode(packet.data, r)) break;
                    refused = r.reason;
                    reason = r.text.empty() ? "The host refused the connection." : r.text;
                    state = State::Refused;
                    roster.clear();
                    // The server hangs up next; the Disconnected tidies away.
                    break;
                }
                case MsgType::Roster: {
                    net::Roster r;     // the message, not the member of the same name
                    if (Decode(packet.data, r)) roster = r.seats;
                    break;
                }
                case MsgType::Chat: {
                    Chat c;
                    if (!Decode(packet.data, c)) break;
                    ChatLine line{c.seat, NameOf(c.seat), c.text};
                    log.push_back(line);
                    if (log.size() > CHAT_KEPT) log.erase(log.begin());
                    fresh.push_back(std::move(line));
                    break;
                }
                default:
                    if (Seated() && IsGameMessage(PeekType(packet.data))) {
                        if (inbound.size() >= 1024) inbound.erase(inbound.begin(), inbound.begin() + 256);
                        inbound.push_back(packet.data);
                    }
                    break;
            }
            break;
        }
    }
}

void Client::Say(const std::string& text) {
    if (!Seated() || !transport) return;
    const std::string line = CleanLine(text, MAX_CHAT);
    if (line.empty()) return;
    transport->Send(server, Channel::Reliable, Encode(net::Say{line}));
}

void Client::Leave() {
    if (transport && server != NO_PEER) transport->Disconnect(server);
    transport.reset();
    server = NO_PEER;
    state = State::Idle;
    roster.clear();
}

void Client::Finish(State end, const std::string& why) {
    transport.reset();
    server = NO_PEER;
    state = end;
    reason = why;
    roster.clear();
}

std::string Client::NameOf(uint8_t s) const {
    if (s == SERVER_SEAT) return {};
    for (const SeatInfo& info : roster)
        if (info.seat == s) return info.name;
    return "Seat " + std::to_string(static_cast<int>(s) + 1);
}

std::vector<Bytes> Client::TakeGameMessages() {
    std::vector<Bytes> out;
    out.swap(inbound);
    return out;
}

void Client::SendGame(Channel channel, const Bytes& bytes) {
    if (Seated() && transport) transport->Send(server, channel, bytes);
}

std::vector<Client::ChatLine> Client::TakeNewLines() {
    std::vector<ChatLine> out;
    out.swap(fresh);
    return out;
}

} // namespace net
