#pragma once
#include "headers.h"

// One action vocabulary for the whole game. Gameplay and UI code ask about
// actions and never about keys, so keyboard/mouse and controller stay
// interchangeable and the options menu can switch between them.
enum class Action {
    MoveUp, MoveDown, MoveLeft, MoveRight,
    LightAttack, StrongAttack, Interact, Jump, Sprint, Target, Block,
    Inventory, QuestLog, Skills, WorldMap, Pause,
    SelectFire, SelectWater, SelectEarth, SelectAir, SelectArcane, CycleSpell,
    // Drops what the bag's cursor is on. Read only by the inventory panel.
    Drop,
    MenuUp, MenuDown, MenuLeft, MenuRight, Confirm, Back,
    COUNT
};
static constexpr int ACTION_COUNT = static_cast<int>(Action::COUNT);

enum class InputMode { Auto = 0, KeyboardMouse = 1, Controller = 2 };

class Input {
public:
    Input();
    ~Input();

    // Fed every SDL event; returns true if the event was input-related.
    bool HandleEvent(const SDL_Event& e);
    // Called once per frame before gameplay reads any action.
    void Update(float dt);

    bool  Down(Action a) const     { return Src().state[Index(a)].down; }
    bool  Pressed(Action a) const  { return Src().state[Index(a)].pressed; }
    bool  Released(Action a) const { return Src().state[Index(a)].released; }
    float HeldFor(Action a) const  { return Src().state[Index(a)].held_time; }

    // --- two players at one machine ---------------------------------------------
    // Which devices are this player's. Alone, everything: the keyboard and
    // whichever controller is plugged in. In split screen each Input is given
    // its share -- the keyboard or not, and the nth controller or none -- and
    // hears nothing from anyone else's.
    // Alone, a button on any controller counts, as it always has. With
    // `only_mine` a controller that is not this Input's is somebody else's.
    void SetDevices(bool keyboard, int pad_rank, bool only_mine = false);
    bool UsesKeyboard() const { return use_keyboard; }
    int  PadRank() const { return pad_rank; }
    static int ConnectedPads();
    SDL_JoystickID PadId() const { return pad_id; }
    // While set, every question asked of this Input is answered by another:
    // how a panel written for "the input" is handed to Player Two.
    void Borrow(const Input* other) { proxy = other; }
    // A press from nowhere, for the self-test and the --hold2 flag.
    void Inject(Action a, bool down) { Set(a, down, true); }

    // Normalised movement, analog on a stick and digital on keys.
    Vec2 MoveAxis() const;

    // Menus accept d-pad, stick, WASD and arrows alike; these repeat on hold.
    bool MenuUp() const, MenuDown() const, MenuLeft() const, MenuRight() const;

    void SetMode(InputMode m);
    InputMode Mode() const { return mode; }
    // What the player is actually using right now (never Auto).
    // The device prompts should be drawn for. Controller mode with no
    // controller connected reports the keyboard, because that is what the
    // player is actually using.
    InputMode ActiveDevice() const {
        if (proxy) return proxy->ActiveDevice();
        if (!use_keyboard) return InputMode::Controller;
        return (active == InputMode::Controller && !pad) ? InputMode::KeyboardMouse : active;
    }
    bool HasGamepad() const { return Src().pad != nullptr; }
    const char* GamepadName() const;


    // Prompt glyphs for UI text, e.g. "E" vs "(A)".
    string PromptFor(Action a) const;

private:
    struct ActionState {
        bool down = false, pressed = false, released = false;
        bool kb = false, padbtn = false;   // which source is asserting it
        float held_time = 0.0f;
        float repeat_timer = 0.0f;
    };

    static int Index(Action a) { return static_cast<int>(a); }
    const Input& Src() const { return proxy ? *proxy : *this; }
    const Input* proxy = nullptr;
    bool use_keyboard = true;
    int  pad_rank = 0;                     // which connected controller is ours; -1 none
    bool only_mine = false;
    void Set(Action a, bool value, bool from_pad);
    void OpenGamepad();
    void CloseGamepad();
    bool Repeated(Action a) const;

    ActionState state[ACTION_COUNT];
    // One key can mean something in play and something else in a menu: J is
    // the light attack and also confirms, K the heavy attack and also backs out.
    std::multimap<SDL_Keycode, Action> keymap;
    map<int, Action> padmap;               // SDL_GamepadButton -> Action

    SDL_Gamepad* pad = nullptr;
    SDL_JoystickID pad_id = 0;

    InputMode mode = InputMode::Auto;
    InputMode active = InputMode::KeyboardMouse;

    Vec2 stick{0, 0};
};
