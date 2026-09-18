#pragma once
#include "../headers.h"

class Input;

// ---------------------------------------------------------------------------
//  What a player's hands are doing, this step
//
//  Player::Update reads this and never the keyboard. The seat this machine's
//  hands drive has it filled from the Input each frame; a friend's character
//  on the host has it filled from the InputFrame their machine sent. That is
//  what lets one World hold four players, and what lets a client run exactly
//  the code the server will run on the same inputs a few milliseconds later.
//
//  The edges travel with the state rather than being worked out from it. A
//  tap shorter than a frame is pressed and released with nothing held in
//  between, and a receiver looking only at what is held would never see it.
//
//  The move axis is whatever the device says until it has to cross the wire;
//  Quantised() is what survives that, and a client predicts with the
//  quantised value so both ends step with the same numbers.
// ---------------------------------------------------------------------------

struct PlayerInput {
    enum Button : uint8_t {
        Light    = 1 << 0,
        Strong   = 1 << 1,
        Block    = 1 << 2,
        Sprint   = 1 << 3,
        Jump     = 1 << 4,
        Interact = 1 << 5,
        Target   = 1 << 6,
    };

    Vec2    move{0.0f, 0.0f};
    uint8_t down = 0, pressed = 0, released = 0;

    bool Down(Button b) const     { return (down & b) != 0; }
    bool Pressed(Button b) const  { return (pressed & b) != 0; }
    bool Released(Button b) const { return (released & b) != 0; }

    static PlayerInput FromDevice(const Input& in);

    // The axis as two signed bytes, and back.
    static int8_t ToWire(float axis) {
        return static_cast<int8_t>(std::lround(std::clamp(axis, -1.0f, 1.0f) * 127.0f));
    }
    static float FromWire(int8_t v) { return static_cast<float>(v) / 127.0f; }
    PlayerInput Quantised() const {
        PlayerInput q = *this;
        q.move = {FromWire(ToWire(move.x)), FromWire(ToWire(move.y))};
        return q;
    }
};
