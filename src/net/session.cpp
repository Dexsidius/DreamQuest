#include "session.h"

namespace net {

const DataHashes& Session::Hashes(const std::string& root) {
    if (!hashed) {
        hashes = ComputeDataHashes(root);
        hashed = true;
    }
    return hashes;
}

Hello Session::MakeHello(const Identity& who) {
    const DataHashes& h = Hashes();
    Hello hello;
    hello.data_hash = h.data;
    hello.maps_hash = h.maps;
    hello.name = CleanLine(who.name, MAX_NAME);
    hello.look = CleanLine(who.look, MAX_LOOK);
    hello.password = who.password;
    return hello;
}

bool Session::Host(uint16_t on_port, const Identity& who, const std::string& world_name,
                   std::string& error, const std::string& bind_host) {
    Leave();
    const DataHashes& h = Hashes();
    if (h.data_files == 0 || h.map_files == 0) {
        error = "The game's data/ and maps/ folders could not be read.";
        return false;
    }

    std::unique_ptr<Transport> wire = ListenEnet(on_port, MAX_SEATS, error, bind_host);
    if (!wire) return false;

    Server::Config config;
    config.world_name = CleanLine(world_name, MAX_NAME * 2);
    config.data_hash = h.data;
    config.maps_hash = h.maps;
    config.password = who.password;
    config.bring_your_own = bring_your_own;
    server = std::make_unique<Server>(config);
    server->Attach(std::move(wire));

    // The host's own client, through the front door like anyone else.
    hub = std::make_unique<LoopbackHub>();
    server->Attach(hub->Server(), true);
    if (!client.Start(hub->Client(), "loopback", 0, MakeHello(who))) {
        error = client.Reason();
        Leave();
        return false;
    }

    role = Role::Host;
    port = on_port;
    address.clear();
    return true;
}

bool Session::Join(const std::string& where, const Identity& who, std::string& error) {
    Leave();
    std::string host;
    uint16_t to_port = DEFAULT_PORT;
    if (!SplitAddress(where, host, to_port)) {
        error = "That is not an address. Try a name like dada-pc, or dada-pc:7777.";
        return false;
    }
    std::unique_ptr<Transport> wire = DialEnet(error);
    if (!wire) return false;
    if (!client.Start(std::move(wire), host, to_port, MakeHello(who))) {
        error = client.Reason();
        return false;
    }
    role = Role::Guest;
    port = to_port;
    address = where;
    return true;
}

void Session::Leave() {
    client.Leave();
    if (server) server->Shutdown();
    server.reset();
    hub.reset();
    role = Role::Offline;
}

void Session::Update(float dt) {
    // Server first, so what the host's client said last frame is answered
    // before it asks again. The client is pumped even when offline: a refused
    // one still has a line to see closed.
    if (server) server->Update(dt);
    client.Update(dt);
    // A guest whose line has dropped is offline again; what happened stays
    // readable in Me().Where() and Me().Reason().
    if (role == Role::Guest && !client.Busy() && !client.Seated()) role = Role::Offline;
}

} // namespace net
