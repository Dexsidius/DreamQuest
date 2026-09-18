#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  The wire, and nothing else
//
//  Everything above this file talks to a Transport and never to a socket, so
//  the same server and client run over ENet on the tailnet, over an in-memory
//  loopback for the host's own client, and over the same loopback in the
//  self-test, where two clients and a server share one process and one thread.
//
//  Five things a transport does: Connect, Disconnect, Send, Poll, Peers. How a
//  listening one comes to exist is the back end's own business -- see
//  ListenEnet / DialEnet below, and LoopbackHub in transport_loopback.h.
//
//  Two channels, as ENet has them:
//    Reliable     ordered, resent until it arrives. Hello, chat, events,
//                 requests: anything that must not be lost.
//    Unreliable   sequenced, newest wins, never resent. Inputs and snapshots:
//                 anything a later one makes worthless.
//
//  This header deliberately includes nothing of the game's, so the headless
//  server can use it without SDL.
// ---------------------------------------------------------------------------

namespace net {

using PeerId = uint32_t;
static constexpr PeerId NO_PEER = 0;

enum class Channel : uint8_t { Reliable = 0, Unreliable = 1 };
static constexpr int CHANNEL_COUNT = 2;

using Bytes = std::vector<uint8_t>;

struct Packet {
    enum class Type : uint8_t { Connected, Disconnected, Data };
    Type    type = Type::Data;
    PeerId  peer = NO_PEER;
    Channel channel = Channel::Reliable;
    Bytes   data;
};

class Transport {
public:
    virtual ~Transport() = default;

    // Starts reaching for a listening transport. The answer comes later, out
    // of Poll: a Connected packet, or a Disconnected one if nobody was there.
    // False only when the attempt could not even be made (a name that does
    // not resolve, a transport that is listening rather than dialling).
    virtual bool Connect(const std::string& host, uint16_t port) = 0;
    // Lets go of one peer, after whatever has already been sent to them has
    // gone out -- so a refusal can say why before the line drops.
    virtual void Disconnect(PeerId peer) = 0;
    virtual void Send(PeerId peer, Channel channel, const Bytes& bytes) = 0;
    // Everything that has arrived since the last call, in order.
    virtual std::vector<Packet> Poll() = 0;
    // Who is on the other end right now.
    virtual std::vector<PeerId> Peers() const = 0;

    // Why the last thing that failed did.
    const std::string& Error() const { return error; }

protected:
    std::string error;
};

// --- ENet -------------------------------------------------------------------
// UDP, which a tailnet passes without port forwarding. Null with `error` set
// if the port is taken or the library would not start.
//
// `bind_host` is the address to listen on; empty is every interface, which is
// what a host wants. The self-test listens on 127.0.0.1 alone, which Windows
// Firewall does not ask about.
static constexpr uint16_t DEFAULT_PORT = 7777;
std::unique_ptr<Transport> ListenEnet(uint16_t port, int max_peers, std::string& error,
                                      const std::string& bind_host = "");
std::unique_ptr<Transport> DialEnet(std::string& error);

// "dada-pc.tailnet.ts.net:7777", "100.101.102.103" or "[fd7a::1]:7777" into a
// host and a port; the port is DEFAULT_PORT when none is written.
bool SplitAddress(const std::string& address, std::string& host, uint16_t& port);

// This machine's own IPv4 addresses, the tailnet's (100.64.0.0/10) first,
// for the host screen to tell friends what to type. Loopback is left out.
struct LocalAddress { std::string ip; bool tailnet = false; };
std::vector<LocalAddress> LocalAddresses();
std::string LocalHostName();

} // namespace net
