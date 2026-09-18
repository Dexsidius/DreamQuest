#include "input.h"
#include "entity/player_input.h"

PlayerInput PlayerInput::FromDevice(const Input& in) {
    PlayerInput out;
    out.move = in.MoveAxis();
    const auto take = [&](Button b, Action a) {
        if (in.Down(a))     out.down     |= b;
        if (in.Pressed(a))  out.pressed  |= b;
        if (in.Released(a)) out.released |= b;
    };
    take(Light,    Action::LightAttack);
    take(Strong,   Action::StrongAttack);
    take(Block,    Action::Block);
    take(Sprint,   Action::Sprint);
    take(Jump,     Action::Jump);
    take(Interact, Action::Interact);
    take(Target,   Action::Target);
    return out;
}

static constexpr float STICK_DEADZONE = 0.25f;
static constexpr float REPEAT_DELAY   = 0.35f;
static constexpr float REPEAT_RATE    = 0.09f;

Input::Input() {
    keymap = {
        {SDLK_W, Action::MoveUp},    {SDLK_UP,    Action::MoveUp},
        {SDLK_S, Action::MoveDown},  {SDLK_DOWN,  Action::MoveDown},
        {SDLK_A, Action::MoveLeft},  {SDLK_LEFT,  Action::MoveLeft},
        {SDLK_D, Action::MoveRight}, {SDLK_RIGHT, Action::MoveRight},

        // The game is played on the keyboard alone: the left hand steers,
        // sprints, jumps and interacts, and the right rests on J K L for the
        // fight. The panels sit on the row above that, I O P, where the right
        // hand reaches them without leaving home.
        {SDLK_J, Action::LightAttack},
        {SDLK_K, Action::StrongAttack},
        {SDLK_L, Action::Target},
        // Guard, held. H sits under the right hand beside the attacks, so a
        // swing and a block are one finger apart.
        {SDLK_H, Action::Block},
        {SDLK_E, Action::Interact},
        {SDLK_SPACE, Action::Jump},
        {SDLK_LSHIFT, Action::Sprint}, {SDLK_RSHIFT, Action::Sprint},

        {SDLK_I, Action::Inventory},  {SDLK_TAB, Action::Inventory},
        {SDLK_O, Action::Skills},
        {SDLK_P, Action::QuestLog},   {SDLK_Q, Action::QuestLog},
        {SDLK_M, Action::WorldMap},
        {SDLK_ESCAPE, Action::Pause},

        {SDLK_1, Action::SelectFire},
        {SDLK_2, Action::SelectWater},
        {SDLK_3, Action::SelectEarth},
        {SDLK_4, Action::SelectAir},
        // The ancient magic, once any of it is known: 5 chooses it, and 5
        // again steps through the spells learned.
        {SDLK_5, Action::SelectArcane},
        {SDLK_R, Action::CycleSpell},
        // G drops the item under the cursor in the bag. Q was the obvious
        // letter, but Q already opens the journal beside P; G sits under the
        // left hand next to the movement keys and nothing else wanted it.
        {SDLK_G, Action::Drop},

        // In menus the fighting keys double up, the way a controller's face
        // buttons do: J or E or Space to confirm, K to back out. Gameplay never
        // reads Confirm or Back, and menus never read the attacks, so neither
        // meaning leaks into the other.
        {SDLK_RETURN, Action::Confirm}, {SDLK_J, Action::Confirm},
        {SDLK_E, Action::Confirm},      {SDLK_SPACE, Action::Confirm},
        {SDLK_BACKSPACE, Action::Back}, {SDLK_K, Action::Back},
    };

    padmap = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, Action::MoveUp},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Action::MoveDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Action::MoveLeft},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Action::MoveRight},
        {SDL_GAMEPAD_BUTTON_WEST, Action::LightAttack},
        {SDL_GAMEPAD_BUTTON_NORTH, Action::StrongAttack},
        {SDL_GAMEPAD_BUTTON_SOUTH, Action::Interact},
        {SDL_GAMEPAD_BUTTON_EAST, Action::Back},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Action::Inventory},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Action::Skills},
        {SDL_GAMEPAD_BUTTON_BACK, Action::QuestLog},
        {SDL_GAMEPAD_BUTTON_GUIDE, Action::WorldMap},
        {SDL_GAMEPAD_BUTTON_START, Action::Pause},
        // Clicking the right stick steps through the elements; the d-pad and
        // both sticks are already spoken for.
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, Action::CycleSpell},
        // Left stick click, because every face button is already an attack,
        // interact or back, and a jump you have to take a thumb off the stick
        // for is one you cannot steer.
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, Action::Jump},
    };

    OpenGamepad();
}

Input::~Input() { CloseGamepad(); }

void Input::OpenGamepad() {
    if (pad) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        if (count > 0) {
            pad = SDL_OpenGamepad(ids[0]);
            if (pad) {
                pad_id = ids[0];
                SDL_Log("Input: gamepad connected - %s", GamepadName());
            }
        }
        SDL_free(ids);
    }
}

void Input::CloseGamepad() {
    if (pad) { SDL_CloseGamepad(pad); pad = nullptr; pad_id = 0; }
}

const char* Input::GamepadName() const {
    if (!pad) return "none";
    const char* n = SDL_GetGamepadName(pad);
    return n ? n : "gamepad";
}

void Input::SetMode(InputMode m) {
    mode = m;
    if (m == InputMode::Controller)         active = InputMode::Controller;
    else if (m == InputMode::KeyboardMouse) active = InputMode::KeyboardMouse;
}

void Input::Set(Action a, bool value, bool from_pad) {
    ActionState& s = state[Index(a)];
    if (from_pad) s.padbtn = value;
    else          s.kb = value;

    // A device the player has switched away from stops driving actions, so a
    // stuck key or a resting stick cannot leak into the other scheme.
    //
    // Except the keyboard is never shut out when there is no controller to use
    // instead. Input Device is the first row of the options screen, one press
    // of Enter selects Controller, and the setting is saved -- so with no pad
    // plugged in, that one press used to leave nothing that responded, not
    // even the menu to change it back, on every launch after.
    const bool allow_kb  = (mode != InputMode::Controller) || !pad;
    const bool allow_pad = (mode != InputMode::KeyboardMouse);
    const bool now = (s.kb && allow_kb) || (s.padbtn && allow_pad);

    if (now && !s.down)      { s.pressed = true; s.held_time = 0.0f; s.repeat_timer = REPEAT_DELAY; }
    else if (!now && s.down) { s.released = true; }
    s.down = now;
}

bool Input::HandleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (e.key.repeat) return true;
            if (mode == InputMode::Auto && e.type == SDL_EVENT_KEY_DOWN)
                active = InputMode::KeyboardMouse;
            const auto range = keymap.equal_range(e.key.key);
            if (range.first == range.second) return false;
            for (auto it = range.first; it != range.second; ++it)
                Set(it->second, e.type == SDL_EVENT_KEY_DOWN, false);
            return true;
        }

        // The mouse does nothing. Aiming is the targeting system's job, and a
        // click on the window to focus it should not swing a sword.

        case SDL_EVENT_GAMEPAD_ADDED:
            OpenGamepad();
            return true;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (e.gdevice.which == pad_id) {
                CloseGamepad();
                SDL_Log("Input: gamepad disconnected");
                if (mode == InputMode::Auto) active = InputMode::KeyboardMouse;
                OpenGamepad();
            }
            return true;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const bool dn = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            if (mode == InputMode::Auto && dn) active = InputMode::Controller;
            auto it = padmap.find(e.gbutton.button);
            if (it != padmap.end()) {
                const Action a = it->second;
                Set(a, dn, true);
                // The face button that interacts also confirms in menus.
                if (a == Action::Interact) Set(Action::Confirm, dn, true);
                // And the one that backs out of menus guards in the game.
                // Gameplay never reads Back, so the two never collide; every
                // other button a thumb can reach was already an attack.
                if (a == Action::Back) Set(Action::Block, dn, true);
                // And the strong attack drops things in the bag. No menu
                // reads the strong attack and the game never reads Drop, so
                // Y means one thing in a fight and another over the bag.
                if (a == Action::StrongAttack) Set(Action::Drop, dn, true);
                return true;
            }
            return false;
        }

        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const float v = e.gaxis.value / 32767.0f;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick.x = v;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) stick.y = v;
            // Sprint is held on the left trigger: every button is spoken for,
            // and a trigger is what a thumb-on-stick player can hold while
            // steering. Half-way down counts, with a little hysteresis so a
            // trigger resting on the line does not flicker.
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                const bool held = state[Index(Action::Sprint)].padbtn;
                if (!held && v > 0.5f) Set(Action::Sprint, true, true);
                if (held && v < 0.35f) Set(Action::Sprint, false, true);
            }
            // And the lock on the right trigger, the same way.
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                const bool held = state[Index(Action::Target)].padbtn;
                if (!held && v > 0.5f) Set(Action::Target, true, true);
                if (held && v < 0.35f) Set(Action::Target, false, true);
            }
            if (mode == InputMode::Auto && fabsf(v) > 0.6f) active = InputMode::Controller;
            return true;
        }
    }
    return false;
}

void Input::Update(float dt) {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        ActionState& s = state[i];
        s.pressed = false;
        s.released = false;
        if (s.down) {
            s.held_time += dt;
            s.repeat_timer -= dt;
            if (s.repeat_timer <= 0.0f) s.repeat_timer += REPEAT_RATE;
        } else {
            s.held_time = 0.0f;
            s.repeat_timer = REPEAT_DELAY;
        }
    }
    if (mode != InputMode::Auto) active = mode;
}

// Fires on the initial press and then on each repeat tick while held.
bool Input::Repeated(Action a) const {
    const ActionState& s = state[Index(a)];
    return s.pressed || (s.down && s.repeat_timer <= 0.0f);
}

bool Input::MenuUp() const    { return Repeated(Action::MoveUp); }
bool Input::MenuDown() const  { return Repeated(Action::MoveDown); }
bool Input::MenuLeft() const  { return Repeated(Action::MoveLeft); }
bool Input::MenuRight() const { return Repeated(Action::MoveRight); }

Vec2 Input::MoveAxis() const {
    Vec2 v{0, 0};

    if (mode != InputMode::KeyboardMouse && pad) {
        const float mag = Length(stick.x, stick.y);
        if (mag > STICK_DEADZONE) {
            // Rescale past the dead zone so slow walking is still reachable.
            const float s = (mag - STICK_DEADZONE) / (1.0f - STICK_DEADZONE) / mag;
            v.x = stick.x * s;
            v.y = stick.y * s;
        }
    }

    if ((mode != InputMode::Controller || !pad) && Length(v.x, v.y) < 0.01f) {
        if (Down(Action::MoveLeft))  v.x -= 1.0f;
        if (Down(Action::MoveRight)) v.x += 1.0f;
        if (Down(Action::MoveUp))    v.y -= 1.0f;
        if (Down(Action::MoveDown))  v.y += 1.0f;
    }

    const float mag = Length(v.x, v.y);
    if (mag > 1.0f) { v.x /= mag; v.y /= mag; }
    return v;
}

string Input::PromptFor(Action a) const {
    if (ActiveDevice() == InputMode::Controller) {
        switch (a) {
            case Action::LightAttack:  return "(X)";
            case Action::StrongAttack: return "(Y)";
            case Action::Interact:
            case Action::Confirm:      return "(A)";
            case Action::Back:
            case Action::Block:        return "(B)";
            case Action::Inventory:    return "LB";
            case Action::Skills:       return "RB";
            case Action::QuestLog:     return "Back";
            case Action::WorldMap:     return "Guide";
            case Action::Pause:        return "Start";
            case Action::CycleSpell:   return "RS";
            case Action::Jump:         return "LS";
            case Action::Sprint:       return "LT";
            case Action::Target:       return "RT";
            case Action::Drop:         return "(Y)";
            default:                   return "";
        }
    }
    switch (a) {
        case Action::LightAttack:  return "J";
        case Action::StrongAttack: return "K";
        case Action::Target:       return "L";
        case Action::Block:        return "H";
        case Action::Interact:     return "E";
        case Action::Confirm:      return "J";
        case Action::Back:         return "K";
        case Action::Inventory:    return "I";
        case Action::Skills:       return "O";
        case Action::QuestLog:     return "P";
        case Action::WorldMap:     return "M";
        case Action::Pause:        return "Esc";
        case Action::CycleSpell:   return "R";
        case Action::Jump:         return "Space";
        case Action::Sprint:       return "Shift";
        case Action::Drop:         return "G";
        default:                   return "";
    }
}
