#pragma once
#include "transport.h"

// ---------------------------------------------------------------------------
//  The wire with no wire in it
//
//  A hub hands out one listening end and any number of dialling ends, all in
//  this process and this thread. The host's own client reaches its server
//  through one -- so playing alone and playing as host run the same code as a
//  friend across the tailnet does -- and the self-test puts a server and two
//  clients on one and plays them against each other with no sockets at all.
//
//  It keeps the promises ENet does: reliable packets arrive, in order; a
//  Disconnect is seen by both ends, after what was sent before it; dialling a
//  hub nobody is listening on is answered with a Disconnected. And it can be
//  made worse on purpose, for tests: every packet held back a number of the
//  receiver's Polls, and every nth unreliable packet lost.
// ---------------------------------------------------------------------------

namespace net {

class LoopbackHub {
public:
    LoopbackHub();
    ~LoopbackHub();

    // The listening end. One per hub; a second call returns null.
    std::unique_ptr<Transport> Server();
    // A dialling end. Connect() on it reaches the hub's server whatever host
    // and port it is given.
    std::unique_ptr<Transport> Client();

    // A packet sent now is handed over on the receiver's nth Poll from now.
    // 0, the default, is the very next one.
    void SetDelay(int polls);
    // Loses every nth packet sent on the unreliable channel. 0 loses none.
    void DropUnreliable(int every_nth);

    struct State;

private:
    std::shared_ptr<State> state;
};

// The server, as a loopback client knows it.
static constexpr PeerId LOOPBACK_SERVER = 1;

} // namespace net
