#pragma once
#include "protocol.h"

// ---------------------------------------------------------------------------
//  The client: knocking, being seated, and the chat line
//
//  One transport, one server on the other end of it. Milestone 0: it says
//  Hello, is welcomed or refused, keeps the roster the server sends, and
//  passes typed lines up and chat lines down. Prediction, interpolation and
//  the snapshot buffers join it in M1.
//
//  The host's own game runs one of these over a loopback, exactly as a
//  friend's runs one over ENet; nothing here knows which it is.
// ---------------------------------------------------------------------------

namespace net {

class Client {
public:
    // Dialling gives up after this long with no answer: a wrong name, a
    // machine that is off, or a firewall eating the port all look the same
    // from here, and ENet's own patience runs to half a minute.
    static constexpr float CONNECT_TIMEOUT = 8.0f;
    static constexpr size_t CHAT_KEPT = 64;

    enum class State {
        Idle,         // not trying
        Connecting,   // dialled, no answer yet
        Greeting,     // connected, Hello sent, waiting to be seated
        Seated,       // in
        Refused,      // the door said no; Reason() says why
        Lost,         // the line dropped, or nobody answered
    };

    struct ChatLine {
        uint8_t     seat = SERVER_SEAT;
        std::string name;      // resolved when it arrived: a roster changes
        std::string text;
    };

    // Takes the transport and starts dialling. False, with Reason() set, if
    // the attempt could not be made at all.
    bool Start(std::unique_ptr<Transport> transport, const std::string& host, uint16_t port,
               const Hello& hello);
    void Update(float dt);
    // Sends a typed line. It comes back as a Chat like everyone else's; it is
    // not echoed locally, so what is on screen is what the server saw.
    void Say(const std::string& text);
    // Hangs up. The state goes to Idle and the log is kept.
    void Leave();

    State  Where() const { return state; }
    bool   Seated() const { return state == State::Seated; }
    bool   Busy() const { return state == State::Connecting || state == State::Greeting; }
    const std::string& Reason() const { return reason; }
    RefuseReason WhyRefused() const { return refused; }

    uint8_t Seat() const { return seat; }
    const std::string& WorldName() const { return world_name; }
    bool BringYourOwn() const { return bring_your_own; }
    const std::vector<SeatInfo>& Roster() const { return roster; }
    const std::vector<ChatLine>& Log() const { return log; }
    // Lines that have arrived since the last call, for toasts.
    std::vector<ChatLine> TakeNewLines();

    // The game's own messages, handed over whole and sent whole.
    std::vector<Bytes> TakeGameMessages();
    void SendGame(Channel channel, const Bytes& bytes);

private:
    void Handle(const Packet& packet);
    void Finish(State end, const std::string& why);
    std::string NameOf(uint8_t seat) const;

    std::unique_ptr<Transport> transport;
    PeerId server = NO_PEER;
    uint16_t dialled_port = DEFAULT_PORT;
    Hello  hello;
    State  state = State::Idle;
    float  waited = 0.0f;
    std::string reason;
    RefuseReason refused = RefuseReason::None;

    uint8_t seat = 0;
    bool    bring_your_own = true;
    std::string world_name;
    std::vector<SeatInfo> roster;
    std::vector<ChatLine> log, fresh;
    std::vector<Bytes> inbound;
};

} // namespace net
