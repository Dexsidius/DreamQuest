#include "transport_loopback.h"
#include <algorithm>
#include <deque>
#include <map>

namespace net {

namespace {
class LoopbackEnd;
}

struct LoopbackHub::State {
    LoopbackEnd* server = nullptr;
    std::map<PeerId, LoopbackEnd*> clients;    // by the id the server knows them as
    PeerId next_id = 2;                        // 1 is the server, as clients see it
    int    delay = 0;
    int    drop_every = 0;
    int    unreliable_sent = 0;
};

namespace {

class LoopbackEnd final : public Transport {
public:
    LoopbackEnd(std::shared_ptr<LoopbackHub::State> s, bool listening)
        : state(std::move(s)), is_server(listening) {
        if (is_server) state->server = this;
    }

    ~LoopbackEnd() override {
        // Going away is a disconnect the other side hears about.
        if (is_server) {
            for (auto& [id, client] : state->clients)
                if (client->connected) { client->connected = false; client->Deliver({Packet::Type::Disconnected, LOOPBACK_SERVER, Channel::Reliable, {}}); }
            state->clients.clear();
            state->server = nullptr;
        } else {
            if (connected && state->server)
                state->server->Deliver({Packet::Type::Disconnected, id, Channel::Reliable, {}});
            state->clients.erase(id);
        }
    }

    bool Connect(const std::string&, uint16_t) override {
        if (is_server) { error = "a listening transport does not dial"; return false; }
        if (connected) { error = "already connected"; return false; }
        if (!state->server) {
            // Nobody home. ENet says so with a Disconnected after its timeout;
            // here it is said at once.
            Deliver({Packet::Type::Disconnected, LOOPBACK_SERVER, Channel::Reliable, {}});
            return true;
        }
        if (id == NO_PEER) id = state->next_id++;
        state->clients[id] = this;
        connected = true;
        state->server->Deliver({Packet::Type::Connected, id, Channel::Reliable, {}});
        Deliver({Packet::Type::Connected, LOOPBACK_SERVER, Channel::Reliable, {}});
        return true;
    }

    void Disconnect(PeerId peer) override {
        if (is_server) {
            auto it = state->clients.find(peer);
            if (it == state->clients.end() || !it->second->connected) return;
            it->second->connected = false;
            it->second->Deliver({Packet::Type::Disconnected, LOOPBACK_SERVER, Channel::Reliable, {}});
            state->clients.erase(it);
            Deliver({Packet::Type::Disconnected, peer, Channel::Reliable, {}});
        } else {
            if (!connected || peer != LOOPBACK_SERVER) return;
            connected = false;
            if (state->server) state->server->Deliver({Packet::Type::Disconnected, id, Channel::Reliable, {}});
            state->clients.erase(id);
            Deliver({Packet::Type::Disconnected, LOOPBACK_SERVER, Channel::Reliable, {}});
        }
    }

    void Send(PeerId peer, Channel channel, const Bytes& bytes) override {
        LoopbackEnd* to = nullptr;
        PeerId from = NO_PEER;
        if (is_server) {
            auto it = state->clients.find(peer);
            if (it == state->clients.end() || !it->second->connected) return;
            to = it->second;
            from = LOOPBACK_SERVER;
        } else {
            if (!connected || peer != LOOPBACK_SERVER || !state->server) return;
            to = state->server;
            from = id;
        }
        if (channel == Channel::Unreliable && state->drop_every > 0 &&
            ++state->unreliable_sent % state->drop_every == 0)
            return;
        to->Deliver({Packet::Type::Data, from, channel, bytes});
    }

    std::vector<Packet> Poll() override {
        ++polls;
        std::vector<Packet> out;
        // In order, always: a packet that is not due yet holds back the ones
        // behind it, the way a reliable ordered channel does.
        while (!inbox.empty() && inbox.front().due <= polls) {
            out.push_back(std::move(inbox.front().packet));
            inbox.pop_front();
        }
        return out;
    }

    std::vector<PeerId> Peers() const override {
        std::vector<PeerId> peers;
        if (is_server) {
            for (const auto& [pid, client] : state->clients)
                if (client->connected) peers.push_back(pid);
        } else if (connected) {
            peers.push_back(LOOPBACK_SERVER);
        }
        return peers;
    }

    void Deliver(Packet p) { inbox.push_back({std::move(p), polls + 1 + state->delay}); }

private:
    struct Held { Packet packet; long long due; };

    std::shared_ptr<LoopbackHub::State> state;
    std::deque<Held> inbox;
    long long polls = 0;
    PeerId id = NO_PEER;
    bool   is_server;
    bool   connected = false;
};

} // namespace

LoopbackHub::LoopbackHub() : state(std::make_shared<State>()) {}
LoopbackHub::~LoopbackHub() = default;

std::unique_ptr<Transport> LoopbackHub::Server() {
    if (state->server) return nullptr;
    return std::make_unique<LoopbackEnd>(state, true);
}

std::unique_ptr<Transport> LoopbackHub::Client() {
    return std::make_unique<LoopbackEnd>(state, false);
}

void LoopbackHub::SetDelay(int polls)          { state->delay = std::max(0, polls); }
void LoopbackHub::DropUnreliable(int every_nth) { state->drop_every = std::max(0, every_nth); }

} // namespace net
