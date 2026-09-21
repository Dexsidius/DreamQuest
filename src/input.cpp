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
    take(Ability,  Action::Ability);
    // On the keys the guard is the abilities' shift as well, as it always was.
    if (in.ActiveDevice() != InputMode::Controller) {
        if (in.Down(Action::Block))     out.down     |= Ability;
        if (in.Pressed(Action::Block))  out.pressed  |= Ability;
        if (in.Released(Action::Block)) out.released |= Ability;
    }
    return out;
}

static constexpr float STICK_DEADZONE = 0.25f;
static constexpr float REPEAT_DELAY   = 0.35f;
static constexpr float REPEAT_RATE    = 0.09f;

// -----------------------------------------------------------------------------
//  Bindings
// -----------------------------------------------------------------------------

Bindings::Bindings() {
    // The game is played on the keyboard alone: the left hand steers, sprints,
    // jumps and interacts, and the right rests on J K L for the fight, with the
    // guard on H beside them so a swing and a block are one finger apart. The
    // panels sit on the row above that, I O P, where the right hand reaches
    // them without leaving home.
    keys = {
        {Action::MoveUp, SDLK_W}, {Action::MoveDown, SDLK_S}, {Action::MoveLeft, SDLK_A}, {Action::MoveRight, SDLK_D},
        {Action::LightAttack, SDLK_J}, {Action::StrongAttack, SDLK_K}, {Action::Target, SDLK_L}, {Action::Block, SDLK_H},
        {Action::Interact, SDLK_E}, {Action::Jump, SDLK_SPACE}, {Action::Sprint, SDLK_LSHIFT},
        {Action::Inventory, SDLK_I}, {Action::Skills, SDLK_O}, {Action::QuestLog, SDLK_P}, {Action::WorldMap, SDLK_M},
        // The menu of menus, and a key for the abilities' shift under the left
        // hand's first finger -- though H does it too.
        {Action::Menu, SDLK_TAB}, {Action::Ability, SDLK_F},
        {Action::SelectFire, SDLK_1}, {Action::SelectWater, SDLK_2}, {Action::SelectEarth, SDLK_3},
        {Action::SelectAir, SDLK_4},
        // The ancient magic, once any of it is known: 5 chooses it, and 5 again
        // steps through the spells learned.
        {Action::SelectArcane, SDLK_5}, {Action::CycleSpell, SDLK_R},
        // G drops the item under the cursor in the bag. Q was the obvious
        // letter, but Q already opens the journal beside P; G sits under the
        // left hand next to the movement keys and nothing else wanted it.
        {Action::Drop, SDLK_G},
    };
    buttons = {
        {Action::LightAttack, SDL_GAMEPAD_BUTTON_WEST}, {Action::StrongAttack, SDL_GAMEPAD_BUTTON_NORTH},
        {Action::Interact, SDL_GAMEPAD_BUTTON_SOUTH},
        // The button that backs out of menus guards in the game: every other
        // one a thumb can reach was already an attack.
        {Action::Block, SDL_GAMEPAD_BUTTON_EAST},
        // RB held is the abilities' shift: RB + X, Y and the right trigger. It was
        // B + X, which is one thumb in two places. RB was the skills panel and
        // Select the journal; both are in the menu Select opens now, with the
        // rest -- a pad had run out of buttons to give each panel its own.
        {Action::Inventory, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {Action::Ability, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {Action::Menu, SDL_GAMEPAD_BUTTON_BACK}, {Action::WorldMap, SDL_GAMEPAD_BUTTON_GUIDE},
        // Clicking the right stick steps through the elements; the d-pad and
        // both sticks are already spoken for.
        {Action::CycleSpell, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
        // Left stick click, because every face button is already an attack,
        // interact or back, and a jump you have to take a thumb off the stick
        // for is one you cannot steer.
        {Action::Jump, SDL_GAMEPAD_BUTTON_LEFT_STICK},
        // Sprint is held on the left trigger and the lock on the right: what a
        // thumb-on-stick player can hold while steering.
        {Action::Sprint, PAD_LEFT_TRIGGER}, {Action::Target, PAD_RIGHT_TRIGGER},
    };
}

const vector<Action>& Bindings::Rebindable() {
    static const vector<Action> list = {
        Action::MoveUp, Action::MoveDown, Action::MoveLeft, Action::MoveRight,
        Action::LightAttack, Action::StrongAttack, Action::Target, Action::Block, Action::Ability,
        Action::Interact, Action::Jump, Action::Sprint,
        Action::Menu, Action::Inventory, Action::Skills, Action::QuestLog, Action::WorldMap, Action::Drop,
        Action::CycleSpell, Action::SelectFire, Action::SelectWater, Action::SelectEarth, Action::SelectAir,
        Action::SelectArcane,
    };
    return list;
}

const char* Bindings::Name(Action a) {
    switch (a) {
        case Action::MoveUp:       return "Move up";
        case Action::MoveDown:     return "Move down";
        case Action::MoveLeft:     return "Move left";
        case Action::MoveRight:    return "Move right";
        case Action::LightAttack:  return "Light attack";
        case Action::StrongAttack: return "Heavy attack";
        case Action::Target:       return "Lock on";
        case Action::Block:        return "Block";
        case Action::Ability:      return "Abilities (hold)";
        case Action::Menu:         return "Menu";
        case Action::Interact:     return "Interact";
        case Action::Jump:         return "Jump";
        case Action::Sprint:       return "Sprint";
        case Action::Inventory:    return "Inventory";
        case Action::Skills:       return "Skills";
        case Action::QuestLog:     return "Quest journal";
        case Action::WorldMap:     return "Map";
        case Action::Drop:         return "Drop (in the bag)";
        case Action::CycleSpell:   return "Next element";
        case Action::SelectFire:   return "Fire";
        case Action::SelectWater:  return "Water";
        case Action::SelectEarth:  return "Earth";
        case Action::SelectAir:    return "Air";
        case Action::SelectArcane: return "Ancient magic";
        default:                   return "";
    }
}

const char* Bindings::Id(Action a) {
    switch (a) {
        case Action::MoveUp:       return "move_up";
        case Action::MoveDown:     return "move_down";
        case Action::MoveLeft:     return "move_left";
        case Action::MoveRight:    return "move_right";
        case Action::LightAttack:  return "light_attack";
        case Action::StrongAttack: return "heavy_attack";
        case Action::Target:       return "lock_on";
        case Action::Block:        return "block";
        case Action::Ability:      return "abilities";
        case Action::Menu:         return "menu";
        case Action::Interact:     return "interact";
        case Action::Jump:         return "jump";
        case Action::Sprint:       return "sprint";
        case Action::Inventory:    return "inventory";
        case Action::Skills:       return "skills";
        case Action::QuestLog:     return "quest_journal";
        case Action::WorldMap:     return "map";
        case Action::Drop:         return "drop";
        case Action::CycleSpell:   return "next_element";
        case Action::SelectFire:   return "fire";
        case Action::SelectWater:  return "water";
        case Action::SelectEarth:  return "earth";
        case Action::SelectAir:    return "air";
        case Action::SelectArcane: return "ancient_magic";
        default:                   return "";
    }
}

bool Bindings::OnPad(Action a) {
    static const Bindings defaults;
    return defaults.buttons.count(a) > 0;
}

// Esc pauses, Enter confirms, Backspace backs out and the arrows steer, always:
// they are how a player gets back out of whatever else they have done here.
bool Bindings::KeyFree(SDL_Keycode key) {
    switch (key) {
        case SDLK_UNKNOWN: case SDLK_ESCAPE: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_BACKSPACE:
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
            return false;
        default:
            return true;
    }
}

bool Bindings::ButtonFree(int button) {
    if (button == PAD_LEFT_TRIGGER || button == PAD_RIGHT_TRIGGER) return true;
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        case SDL_GAMEPAD_BUTTON_START:
            return false;
        default:
            return true;
    }
}

SDL_Keycode Bindings::Key(Action a) const {
    auto it = keys.find(a);
    return it == keys.end() ? SDLK_UNKNOWN : it->second;
}

int Bindings::Button(Action a) const {
    auto it = buttons.find(a);
    return it == buttons.end() ? -1 : it->second;
}

Action Bindings::BindKey(Action a, SDL_Keycode key) {
    if (!KeyFree(key) || !keys.count(a) || keys[a] == key) return Action::COUNT;
    Action displaced = Action::COUNT;
    for (auto& kv : keys)
        if (kv.first != a && kv.second == key) { kv.second = keys[a]; displaced = kv.first; }
    keys[a] = key;
    return displaced;
}

Action Bindings::BindButton(Action a, int button) {
    if (!ButtonFree(button) || !buttons.count(a) || buttons[a] == button) return Action::COUNT;
    Action displaced = Action::COUNT;
    for (auto& kv : buttons)
        if (kv.first != a && kv.second == button) { kv.second = buttons[a]; displaced = kv.first; }
    buttons[a] = button;
    return displaced;
}

string Bindings::KeyLabel(SDL_Keycode key) {
    switch (key) {
        case SDLK_UNKNOWN:   return "";
        case SDLK_LSHIFT:    return "Shift";
        case SDLK_RSHIFT:    return "RShift";
        case SDLK_LCTRL:     return "Ctrl";
        case SDLK_RCTRL:     return "RCtrl";
        case SDLK_LALT:      return "Alt";
        case SDLK_RALT:      return "RAlt";
        case SDLK_ESCAPE:    return "Esc";
        case SDLK_RETURN:    return "Enter";
        case SDLK_SPACE:     return "Space";
        case SDLK_TAB:       return "Tab";
        case SDLK_BACKSPACE: return "Backspace";
        default: break;
    }
    const char* name = SDL_GetKeyName(key);
    return (name && *name) ? string(name) : string("?");
}

string Bindings::ButtonLabel(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:          return "(A)";
        case SDL_GAMEPAD_BUTTON_EAST:           return "(B)";
        case SDL_GAMEPAD_BUTTON_WEST:           return "(X)";
        case SDL_GAMEPAD_BUTTON_NORTH:          return "(Y)";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
        case SDL_GAMEPAD_BUTTON_BACK:           return "Back";
        case SDL_GAMEPAD_BUTTON_GUIDE:          return "Guide";
        case SDL_GAMEPAD_BUTTON_START:          return "Start";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return "LS";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return "RS";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:  return "R4";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:   return "L4";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:  return "R5";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:   return "L5";
        case SDL_GAMEPAD_BUTTON_MISC1:          return "Misc";
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:       return "Pad";
        case PAD_LEFT_TRIGGER:                  return "LT";
        case PAD_RIGHT_TRIGGER:                 return "RT";
        default:                                return button < 0 ? "" : "B" + std::to_string(button);
    }
}

// Names, not numbers, so the file can be read and survives SDL renumbering
// anything: SDL's own for keys and buttons, and its axis names for the triggers.
json Bindings::ToJson() const {
    json k = json::object(), b = json::object();
    for (const auto& kv : keys) k[Id(kv.first)] = SDL_GetKeyName(kv.second);
    for (const auto& kv : buttons) {
        if (kv.second == PAD_LEFT_TRIGGER)       b[Id(kv.first)] = "lefttrigger";
        else if (kv.second == PAD_RIGHT_TRIGGER) b[Id(kv.first)] = "righttrigger";
        else if (const char* n = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(kv.second)))
            b[Id(kv.first)] = n;
    }
    return json{{"layout", LAYOUT}, {"keys", k}, {"buttons", b}};
}

void Bindings::FromJson(const json& j) {
    *this = Bindings();
    if (!j.is_object()) return;
    // One at a time, each a swap: a whole set of distinct keys lands as it was
    // saved whatever order it comes in, and a file somebody has put the same
    // key in twice still ends with every action on a key of its own.
    const auto each = [&](const char* block, const std::function<void(Action, const string&)>& apply) {
        if (!j.contains(block) || !j[block].is_object()) return;
        for (Action a : Rebindable()) {
            const auto it = j[block].find(Id(a));
            if (it != j[block].end() && it->is_string()) apply(a, it->get<string>());
        }
    };
    each("keys", [&](Action a, const string& name) { BindKey(a, SDL_GetKeyFromName(name.c_str())); });
    // A pad laid out before the abilities moved to RB has RB on the skills
    // panel, and would take it straight back: the saved buttons are from
    // another layout, so the new one stands. Keys are as they were saved.
    if (j.value("layout", 1) < LAYOUT) {
        const std::map<Action, SDL_Keycode> mine = keys;
        *this = Bindings();
        for (const auto& kv : mine) if (keys.count(kv.first)) BindKey(kv.first, kv.second);
        return;
    }
    each("buttons", [&](Action a, const string& name) {
        if (name == "lefttrigger")       BindButton(a, PAD_LEFT_TRIGGER);
        else if (name == "righttrigger") BindButton(a, PAD_RIGHT_TRIGGER);
        else BindButton(a, static_cast<int>(SDL_GetGamepadButtonFromString(name.c_str())));
    });
}

// -----------------------------------------------------------------------------
//  Input
// -----------------------------------------------------------------------------

Input::Input() {
    RebuildMaps();
    OpenGamepad();
}

// The two tables every event is looked up in, from the bindings and the few
// things that are nobody's to move.
void Input::RebuildMaps() {
    keymap.clear();
    padmap.clear();

    // Always: the arrows steer, Esc pauses, Enter confirms, Backspace backs out.
    keymap.insert({SDLK_UP, Action::MoveUp});     keymap.insert({SDLK_DOWN, Action::MoveDown});
    keymap.insert({SDLK_LEFT, Action::MoveLeft}); keymap.insert({SDLK_RIGHT, Action::MoveRight});
    keymap.insert({SDLK_ESCAPE, Action::Pause});
    keymap.insert({SDLK_RETURN, Action::Confirm}); keymap.insert({SDLK_KP_ENTER, Action::Confirm});
    keymap.insert({SDLK_BACKSPACE, Action::Back});

    std::set<SDL_Keycode> taken;
    for (const auto& kv : bindings.keys) { keymap.insert({kv.second, kv.first}); taken.insert(kv.second); }

    // In menus the fighting keys double up, the way a controller's face
    // buttons do: the light attack's key -- or Interact's, or Jump's -- to
    // confirm, the heavy attack's to back out. Gameplay never reads Confirm or
    // Back, and menus never read the attacks, so neither meaning leaks into
    // the other.
    for (Action a : {Action::LightAttack, Action::Interact, Action::Jump})
        keymap.insert({bindings.Key(a), Action::Confirm});
    keymap.insert({bindings.Key(Action::StrongAttack), Action::Back});

    // A second key for three things, for as long as nothing else has asked for it.
    const std::pair<SDL_Keycode, Action> spares[] = {
        {SDLK_Q, Action::QuestLog}, {SDLK_RSHIFT, Action::Sprint}};
    for (const auto& spare : spares)
        if (!taken.count(spare.first)) keymap.insert(spare);

    padmap[SDL_GAMEPAD_BUTTON_DPAD_UP] = Action::MoveUp;     padmap[SDL_GAMEPAD_BUTTON_DPAD_DOWN] = Action::MoveDown;
    padmap[SDL_GAMEPAD_BUTTON_DPAD_LEFT] = Action::MoveLeft; padmap[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = Action::MoveRight;
    padmap[SDL_GAMEPAD_BUTTON_START] = Action::Pause;
    for (const auto& kv : bindings.buttons) padmap[kv.second] = kv.first;
}

void Input::ReleaseAll() {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (state[i].kb)     Set(static_cast<Action>(i), false, false);
        if (state[i].padbtn) Set(static_cast<Action>(i), false, true);
    }
    trigger_held[0] = trigger_held[1] = false;
}

void Input::SetBindings(const Bindings& b) {
    if (b == bindings) return;
    // Whatever is held is let go first: the key that was holding it may be
    // about to mean something else, and would never be seen to come up.
    ReleaseAll();
    bindings = b;
    RebuildMaps();
}

void Input::Listen(ListenFor what) {
    listening = what;
    heard = Heard{};
    // The press that asked for this is still down, and its release is about to
    // be swallowed with everything else.
    if (what != ListenFor::Nothing) ReleaseAll();
}

Input::Heard Input::TakeHeard() {
    const Heard out = heard;
    if (out.any || out.cancelled) { heard = Heard{}; listening = ListenFor::Nothing; }
    return out;
}

Input::~Input() { CloseGamepad(); }

void Input::OpenGamepad() {
    if (pad || pad_rank < 0) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        // The nth that is plugged in: alone that is the first, and in split
        // screen each player is told which is theirs.
        if (count > pad_rank) {
            pad = SDL_OpenGamepad(ids[pad_rank]);
            if (pad) {
                pad_id = ids[pad_rank];
                SDL_Log("Input: gamepad connected - %s", GamepadName());
            }
        }
        SDL_free(ids);
    }
}

int Input::ConnectedPads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) SDL_free(ids);
    return std::max(0, count);
}

void Input::SetDevices(bool keyboard, int rank, bool mine) {
    if (keyboard == use_keyboard && rank == pad_rank && mine == only_mine) return;
    use_keyboard = keyboard;
    pad_rank = rank;
    only_mine = mine;
    // Whatever was held on a device that is no longer ours is let go.
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (!use_keyboard && state[i].kb) Set(static_cast<Action>(i), false, false);
        if (state[i].padbtn) Set(static_cast<Action>(i), false, true);
    }
    stick = {0, 0};
    CloseGamepad();
    OpenGamepad();
    if (!use_keyboard) active = InputMode::Controller;
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

void Input::PadSet(Action a, bool dn) {
    Set(a, dn, true);
    // The button that interacts also confirms in menus.
    if (a == Action::Interact) Set(Action::Confirm, dn, true);
    // And the one that guards in the game backs out of them. Gameplay never
    // reads Back, so the two never collide.
    if (a == Action::Block) Set(Action::Back, dn, true);
    // And the heavy attack drops things in the bag. No menu reads the heavy
    // attack and the game never reads Drop, so it means one thing in a fight
    // and another over the bag.
    if (a == Action::StrongAttack) Set(Action::Drop, dn, true);
}

bool Input::HandleEvent(const SDL_Event& e) {
    // The Controls screen is waiting to be told what a key is: whatever comes
    // next is the answer and not a command.
    if (listening != ListenFor::Nothing) {
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat || !use_keyboard) return true;
                if (e.key.key == SDLK_ESCAPE) heard.cancelled = true;
                else if (listening == ListenFor::Key) { heard.any = true; heard.key = e.key.key; }
                return true;
            case SDL_EVENT_KEY_UP:
                return true;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (only_mine && (!pad || e.gbutton.which != pad_id)) return false;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) heard.cancelled = true;
                else if (listening == ListenFor::Button) { heard.any = true; heard.button = e.gbutton.button; }
                return true;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                return true;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (only_mine && (!pad || e.gaxis.which != pad_id)) return false;
                if (listening == ListenFor::Button && !heard.any && e.gaxis.value > 20000) {
                    if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  { heard.any = true; heard.button = PAD_LEFT_TRIGGER; }
                    if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) { heard.any = true; heard.button = PAD_RIGHT_TRIGGER; }
                }
                // The stick still has to be tracked, or it is wherever it was when this began.
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick.x = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) stick.y = e.gaxis.value / 32767.0f;
                return true;
            default: break;
        }
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (!use_keyboard) return false;
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
            return false;          // everyone hears of a controller arriving

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (e.gdevice.which == pad_id) {
                CloseGamepad();
                SDL_Log("Input: gamepad disconnected");
                if (mode == InputMode::Auto) active = InputMode::KeyboardMouse;
                OpenGamepad();
            }
            return false;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            if (only_mine && (!pad || e.gbutton.which != pad_id)) return false;      // someone else's controller
            const bool dn = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            if (mode == InputMode::Auto && dn) active = InputMode::Controller;
            auto it = padmap.find(e.gbutton.button);
            if (it != padmap.end()) {
                PadSet(it->second, dn);
                return true;
            }
            return false;
        }

        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            if (only_mine && (!pad || e.gaxis.which != pad_id)) return false;
            const float v = e.gaxis.value / 32767.0f;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick.x = v;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) stick.y = v;
            // A trigger is held like a button and bound like one -- sprint on
            // the left and the lock on the right, until somebody moves them.
            // Half-way down counts, with a little hysteresis so a trigger
            // resting on the line does not flicker.
            for (int side = 0; side < 2; ++side) {
                if (e.gaxis.axis != (side == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) continue;
                const auto it = padmap.find(side == 0 ? PAD_LEFT_TRIGGER : PAD_RIGHT_TRIGGER);
                if (it == padmap.end()) continue;
                if (!trigger_held[side] && v > 0.5f)  { trigger_held[side] = true;  PadSet(it->second, true); }
                if (trigger_held[side] && v < 0.35f)  { trigger_held[side] = false; PadSet(it->second, false); }
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
    const ActionState& s = Src().state[Index(a)];
    return s.pressed || (s.down && s.repeat_timer <= 0.0f);
}

bool Input::MenuUp() const    { return Repeated(Action::MoveUp); }
bool Input::MenuDown() const  { return Repeated(Action::MoveDown); }
bool Input::MenuLeft() const  { return Repeated(Action::MoveLeft); }
bool Input::MenuRight() const { return Repeated(Action::MoveRight); }

Vec2 Input::MoveAxis() const {
    if (proxy) return proxy->MoveAxis();
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

// What to press, as the UI should say it: the key or button an action is on
// now, not the one it shipped on. The menus' second meanings are read off the
// actions they ride on -- see Bindings.
string Input::PromptFor(Action a) const {
    const Bindings& b = Src().bindings;
    if (ActiveDevice() == InputMode::Controller) {
        switch (a) {
            case Action::Pause:   return "Start";
            case Action::Confirm: a = Action::Interact;     break;
            case Action::Back:    a = Action::Block;        break;
            case Action::Drop:    a = Action::StrongAttack; break;
            // A pad has no button for each element: it steps through them.
            case Action::SelectFire: case Action::SelectWater: case Action::SelectEarth:
            case Action::SelectAir:  case Action::SelectArcane:
                a = Action::CycleSpell; break;
            // The skills and the journal are in the menu, on a pad.
            case Action::Skills: case Action::QuestLog:
                if (!b.buttons.count(a)) a = Action::Menu;
                break;
            default: break;
        }
        return Bindings::ButtonLabel(b.Button(a));
    }
    switch (a) {
        case Action::Pause:   return "Esc";
        case Action::Confirm: a = Action::LightAttack;  break;
        case Action::Back:    a = Action::StrongAttack; break;
        default: break;
    }
    return Bindings::KeyLabel(b.Key(a));
}
