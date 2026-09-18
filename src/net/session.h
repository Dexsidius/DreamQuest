#pragma once
#include "client.h"
#include "server.h"
#include "datahash.h"
#include "transport_loopback.h"

// ---------------------------------------------------------------------------
//  A session: what the game holds
//
//  Offline, hosting, or joined to someone else's world. Hosting is a Server
//  listening on ENet with the host's own Client seated through a loopback;
//  joining is a Client over ENet and no server at all. The game asks the same
//  questions of both -- who is here, what has been said, am I in -- and never
//  needs to know which it is.
//
//  The session outlives the screen that started it: a host who goes off to
//  play keeps listening, and chat that arrives meanwhile is handed to the game
//  as toasts.
// ---------------------------------------------------------------------------

namespace net {

class Session {
public:
    enum class Role { Offline, Host, Guest };

    // Who the player is, for the door.
    struct Identity {
        std::string name = "Traveller";
        std::string look = "player_hero";
    };

    // Hashes data/ and maps/ under `root`, once; every Host and Join after
    // that uses the answer. Cheap to call again.
    const DataHashes& Hashes(const std::string& root = ".");

    // Starts listening on `port` and seats the host. False with `error` set
    // if the port is taken. `bind_host` as for ListenEnet.
    bool Host(uint16_t port, const Identity& who, const std::string& world_name, std::string& error,
              const std::string& bind_host = "");
    // Starts dialling "name", "name:port" or a dotted quad. The answer arrives
    // through Me().Where(). False with `error` set if the address is no good.
    bool Join(const std::string& address, const Identity& who, std::string& error);
    // Hangs up, or stops hosting and lets everyone go.
    void Leave();

    void Update(float dt);

    Role   As() const { return role; }
    bool   Active() const { return role != Role::Offline; }
    bool   Hosting() const { return role == Role::Host; }
    uint16_t Port() const { return port; }
    const std::string& Address() const { return address; }   // what was dialled

    Client&       Me()       { return client; }
    const Client& Me() const { return client; }
    // The server being hosted, or null.
    const Server* Hosted() const { return server.get(); }

private:
    Hello MakeHello(const Identity& who);

    Role role = Role::Offline;
    uint16_t port = DEFAULT_PORT;
    std::string address;
    bool hashed = false;
    DataHashes hashes;

    // Declared in the order they must die in reverse: the client's loopback
    // end and the server's both point into the hub's shared state, which
    // outlives them all by being shared.
    std::unique_ptr<LoopbackHub> hub;
    std::unique_ptr<Server> server;
    Client client;
};

} // namespace net
