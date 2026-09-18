// The wire: ENet over UDP. ENet's headers bring <winsock2.h> and <windows.h>
// with them, and those define min, max, near, far and a few hundred other
// words as macros -- so they are included here and nowhere else.
#include "transport.h"

#include <enet/enet.h>
#include <algorithm>
#include <map>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#endif

namespace net {

namespace {

// enet_initialize is WSAStartup on Windows, and is counted so that a server
// and a client in one process, or a transport and an address lookup, do not
// pull the library out from under each other.
int  g_enet_users = 0;
bool AcquireEnet() {
    if (g_enet_users == 0 && enet_initialize() != 0) return false;
    ++g_enet_users;
    return true;
}
void ReleaseEnet() {
    if (g_enet_users > 0 && --g_enet_users == 0) enet_deinitialize();
}

class EnetTransport final : public Transport {
public:
    ~EnetTransport() override {
        if (host) {
            // Tell everyone now rather than leaving them to time out. What is
            // already queued goes first -- including the goodbye of a peer
            // Disconnect() has been called on, which disconnect_now would
            // otherwise throw away unsent with the rest of its queue.
            enet_host_flush(host);
            for (auto& [id, peer] : peers) enet_peer_disconnect_now(peer, 0);
            enet_host_flush(host);
            enet_host_destroy(host);
        }
        if (acquired) ReleaseEnet();
    }

    bool Open(const ENetAddress* bind, int max_peers, std::string& why) {
        if (!AcquireEnet()) { why = "the network library would not start"; return false; }
        acquired = true;
        listening = bind != nullptr;
        host = enet_host_create(bind, static_cast<size_t>(std::max(1, max_peers)), CHANNEL_COUNT, 0, 0);
        if (!host) {
            why = listening ? "could not listen on UDP port " + std::to_string(bind->port) +
                              " (is another DreamQuest already hosting?)"
                            : "could not open a socket";
            return false;
        }
        return true;
    }

    bool Connect(const std::string& name, uint16_t port) override {
        if (listening) { error = "a listening transport does not dial"; return false; }
        if (!peers.empty()) { error = "already connected"; return false; }
        ENetAddress address{};
        if (enet_address_set_host(&address, name.c_str()) != 0) {
            error = "could not find '" + name + "'";
            return false;
        }
        address.port = port;
        ENetPeer* peer = enet_host_connect(host, &address, CHANNEL_COUNT, 0);
        if (!peer) { error = "no room for another connection"; return false; }
        Adopt(peer);
        return true;
    }

    void Disconnect(PeerId id) override {
        auto it = peers.find(id);
        if (it == peers.end()) return;
        ENetPeer* peer = it->second;
        if (peer->state == ENET_PEER_STATE_CONNECTED) {
            // After what is queued has gone; the Disconnected comes out of
            // Poll once the other end has acknowledged it.
            enet_peer_disconnect_later(peer, 0);
            enet_host_flush(host);
        } else {
            // Still dialling, or already going: ENet forgets such a peer
            // without an event, so say it ourselves.
            peer->data = nullptr;
            enet_peer_reset(peer);
            peers.erase(it);
            pending.push_back({Packet::Type::Disconnected, id, Channel::Reliable, {}});
        }
    }

    void Send(PeerId id, Channel channel, const Bytes& bytes) override {
        auto it = peers.find(id);
        if (it == peers.end() || it->second->state != ENET_PEER_STATE_CONNECTED) return;
        // Reliable is resent and ordered. Unreliable is ENet's default,
        // sequenced: a packet older than one already delivered is dropped.
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(),
                                                channel == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (!packet) return;
        if (enet_peer_send(it->second, static_cast<enet_uint8>(channel), packet) != 0) {
            enet_packet_destroy(packet);
            return;
        }
        // Out now, not on the next Poll: a frame is 16 ms, and an input that
        // waits one is an input that is late.
        enet_host_flush(host);
    }

    std::vector<Packet> Poll() override {
        std::vector<Packet> out;
        out.swap(pending);
        if (!host) return out;

        ENetEvent event;
        while (enet_host_service(host, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    // A peer we dialled already has an id; one that dialled us
                    // gets one now.
                    PeerId id = IdOf(event.peer);
                    if (id == NO_PEER) id = Adopt(event.peer);
                    // A friend on a relayed path can stall for a second or two
                    // without being gone: 5 to 15 seconds before giving up.
                    enet_peer_timeout(event.peer, 0, 5000, 15000);
                    out.push_back({Packet::Type::Connected, id, Channel::Reliable, {}});
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    const PeerId id = IdOf(event.peer);
                    if (id != NO_PEER && event.channelID < CHANNEL_COUNT) {
                        Packet p;
                        p.type = Packet::Type::Data;
                        p.peer = id;
                        p.channel = static_cast<Channel>(event.channelID);
                        p.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                        out.push_back(std::move(p));
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    const PeerId id = IdOf(event.peer);
                    event.peer->data = nullptr;
                    if (id != NO_PEER) {
                        peers.erase(id);
                        out.push_back({Packet::Type::Disconnected, id, Channel::Reliable, {}});
                    }
                    break;
                }
                default: break;
            }
        }
        return out;
    }

    std::vector<PeerId> Peers() const override {
        std::vector<PeerId> out;
        for (const auto& [id, peer] : peers)
            if (peer->state == ENET_PEER_STATE_CONNECTED) out.push_back(id);
        return out;
    }

private:
    // The id rides in the peer's user pointer, so an event finds it without a
    // search.
    static PeerId IdOf(const ENetPeer* peer) {
        return static_cast<PeerId>(reinterpret_cast<uintptr_t>(peer->data));
    }
    PeerId Adopt(ENetPeer* peer) {
        const PeerId id = next_id++;
        peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
        peers[id] = peer;
        return id;
    }

    ENetHost* host = nullptr;
    std::map<PeerId, ENetPeer*> peers;
    std::vector<Packet> pending;      // events ENet does not raise itself
    PeerId next_id = 1;
    bool   listening = false;
    bool   acquired = false;
};

} // namespace

std::unique_ptr<Transport> ListenEnet(uint16_t port, int max_peers, std::string& error,
                                      const std::string& bind_host) {
    auto t = std::make_unique<EnetTransport>();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    // On Windows a name cannot be resolved before WSAStartup, which is what
    // acquiring the library is.
    if (!bind_host.empty()) {
        if (!AcquireEnet()) { error = "the network library would not start"; return nullptr; }
        const bool found = enet_address_set_host(&address, bind_host.c_str()) == 0;
        ReleaseEnet();
        if (!found) { error = "could not find '" + bind_host + "' to listen on"; return nullptr; }
        address.port = port;
    }
    if (!t->Open(&address, max_peers, error)) return nullptr;
    return t;
}

std::unique_ptr<Transport> DialEnet(std::string& error) {
    auto t = std::make_unique<EnetTransport>();
    if (!t->Open(nullptr, 1, error)) return nullptr;
    return t;
}

bool SplitAddress(const std::string& address, std::string& host, uint16_t& port) {
    std::string text = address;
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return false;
    text = text.substr(first, text.find_last_not_of(" \t") - first + 1);

    port = DEFAULT_PORT;
    std::string port_text;
    if (text.front() == '[') {
        // "[fd7a:115c::1]:7777"
        const size_t close = text.find(']');
        if (close == std::string::npos) return false;
        host = text.substr(1, close - 1);
        if (close + 1 < text.size()) {
            if (text[close + 1] != ':') return false;
            port_text = text.substr(close + 2);
        }
    } else if (std::count(text.begin(), text.end(), ':') == 1) {
        const size_t colon = text.find(':');
        host = text.substr(0, colon);
        port_text = text.substr(colon + 1);
    } else {
        host = text;      // a bare name, a dotted quad, or an unbracketed IPv6 literal
    }
    if (host.empty()) return false;

    if (!port_text.empty() || (text.back() == ':')) {
        if (port_text.empty() || port_text.size() > 5 ||
            !std::all_of(port_text.begin(), port_text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        const int value = std::stoi(port_text);
        if (value < 1 || value > 65535) return false;
        port = static_cast<uint16_t>(value);
    }
    return true;
}

std::string LocalHostName() {
    if (!AcquireEnet()) return {};
    char name[256] = {0};
    const bool ok = gethostname(name, sizeof(name) - 1) == 0;
    ReleaseEnet();
    return ok ? std::string(name) : std::string();
}

std::vector<LocalAddress> LocalAddresses() {
    std::vector<LocalAddress> out;
    const auto add = [&](uint32_t ip_host_order) {
        const uint32_t a = (ip_host_order >> 24) & 0xFF, b = (ip_host_order >> 16) & 0xFF;
        if (a == 127 || a == 0) return;
        if (a == 169 && b == 254) return;            // link-local: nobody can dial it
        LocalAddress la;
        la.ip = std::to_string(a) + "." + std::to_string(b) + "." +
                std::to_string((ip_host_order >> 8) & 0xFF) + "." + std::to_string(ip_host_order & 0xFF);
        // Tailscale hands out 100.64.0.0/10, the carrier-grade NAT block.
        la.tailnet = (a == 100 && (b & 0xC0) == 64);
        for (const LocalAddress& have : out) if (have.ip == la.ip) return;
        out.push_back(la);
    };

    if (!AcquireEnet()) return out;
#ifdef _WIN32
    char name[256] = {0};
    if (gethostname(name, sizeof(name) - 1) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* list = nullptr;
        if (getaddrinfo(name, nullptr, &hints, &list) == 0) {
            for (addrinfo* a = list; a; a = a->ai_next)
                if (a->ai_family == AF_INET && a->ai_addr)
                    add(ntohl(reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr.s_addr));
            freeaddrinfo(list);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next)
            if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && (a->ifa_flags & IFF_UP))
                add(ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr));
        freeifaddrs(list);
    }
#endif
    ReleaseEnet();

    std::stable_sort(out.begin(), out.end(),
                     [](const LocalAddress& l, const LocalAddress& r) { return l.tailnet && !r.tailnet; });
    return out;
}

} // namespace net
