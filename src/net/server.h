#pragma once
#include "protocol.h"
#include <functional>

// ---------------------------------------------------------------------------
//  The server: the door, the seats and the chat line
//
//  Milestone 0 of the co-op plan. It simulates nothing yet -- the Realm, the
//  tick and the snapshots arrive in M1 -- but everything a later milestone
//  needs to stand on is here: who is connected, that they are running the
//  same game we are, which seat is theirs, and a reliable line to each.
//
//  A server listens on any number of transports at once. The host's own game
//  attaches two: ENet for friends, and a loopback for the host's own client,
//  which is a client like any other and is given no special path. The
//  headless server will attach ENet alone, and the self-test a loopback alone.
//
//  The door, in order: the magic number (is this DreamQuest knocking at all),
//  the protocol version, the data hash, the maps hash, then whether a seat is
//  free. The first failure is sent back as a Refuse with a sentence a player
//  can act on, and the line is dropped once that has gone out. Someone who
//  connects and never says Hello is dropped after HELLO_TIMEOUT.
// ---------------------------------------------------------------------------

namespace net {

class Server {
public:
    static constexpr float HELLO_TIMEOUT = 5.0f;

    struct Config {
        std::string world_name = "Hollowmarch";
        uint64_t    data_hash = 0, maps_hash = 0;
        int         max_seats = MAX_SEATS;
    };

    explicit Server(Config config);

    // A way in. `local` marks the transport the host's own client arrives on:
    // whoever is seated through it is the host in the roster.
    void Attach(std::unique_ptr<Transport> transport, bool local = false);

    void Update(float dt);

    // Who has a seat, in seat order.
    const std::vector<SeatInfo>& Roster() const { return roster; }
    // True once anyone has reached the door from outside -- a Hello over a
    // transport that is not the host's own. The host screen's "reachable"
    // light: if it never turns on, the firewall is eating the port.
    bool ReachedFromOutside() const { return reached; }
    // A line from the world itself, to everyone: "Oona joined."
    void Announce(const std::string& text);
    // Lets everybody go and stops listening.
    void Shutdown();

private:
    enum class Stage { Greeting, Seated, Leaving };
    struct Connection {
        size_t  transport = 0;
        PeerId  peer = NO_PEER;
        Stage   stage = Stage::Greeting;
        float   age = 0.0f;
        bool    local = false;
        SeatInfo info;
    };

    void Handle(size_t transport, const Packet& packet);
    void HandleHello(Connection& c, const Bytes& bytes);
    void HandleSay(Connection& c, const Bytes& bytes);
    void RefuseAndDrop(Connection& c, RefuseReason reason, const std::string& text);
    void Drop(size_t transport, PeerId peer);
    void SendTo(const Connection& c, const Bytes& bytes);
    void Broadcast(const Bytes& bytes);
    void RebuildRoster();
    Connection* Find(size_t transport, PeerId peer);
    int  FreeSeat() const;
    std::string UniqueName(const std::string& wanted) const;

    Config config;
    struct Way { std::unique_ptr<Transport> transport; bool local = false; };
    std::vector<Way> ways;
    std::vector<Connection> connections;
    std::vector<SeatInfo> roster;
    bool reached = false;
};

} // namespace net
