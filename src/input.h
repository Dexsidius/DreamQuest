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

    bool  Down(Action a) const     { return state[Index(a)].down; }
    bool  Pressed(Action a) const  { return state[Index(a)].pressed; }
    bool  Released(Action a) const { return state[Index(a)].released; }
    float HeldFor(Action a) const  { return state[Index(a)].held_time; }

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
        return (active == InputMode::Controller && !pad) ? InputMode::KeyboardMouse : active;
    }
    bool HasGamepad() const { return pad != nullptr; }
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
