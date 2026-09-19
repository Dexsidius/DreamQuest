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

// A controller's two triggers are axes, but they are held like buttons and are
// bound like them: these stand beside SDL's own button numbers in a binding.
static constexpr int PAD_LEFT_TRIGGER  = 1000;
static constexpr int PAD_RIGHT_TRIGGER = 1001;

// -----------------------------------------------------------------------------
//  Which key and which button does what.
//
//  Every action a player might want somewhere else has one key and, if a pad
//  can do it at all, one button, and both can be moved from the Controls
//  screen. Giving an action a key that another already has **swaps** them, so
//  nothing is ever left with no key and no two actions ever share one -- which
//  is the whole of what keeps a rebinding menu from being a way to break the
//  game.
//
//  What cannot be moved is what gets a player back out of a mistake: Esc and
//  Start pause, Enter confirms and Backspace backs out, and the arrow keys and
//  the d-pad steer a menu, whatever else has been done.
//
//  The menus' second meanings follow the actions they always rode on. On the
//  keyboard the light attack's key confirms (with Interact's and Jump's) and
//  the heavy attack's backs out; on a pad Interact's button confirms, Block's
//  backs out, and the heavy attack's drops things in the bag. Move the attack
//  and the prompt under every menu moves with it.
// -----------------------------------------------------------------------------
struct Bindings {
    std::map<Action, SDL_Keycode> keys;
    std::map<Action, int>         buttons;

    Bindings();                                       // the defaults

    static const vector<Action>& Rebindable();        // in the order the menu lists them
    static const char* Name(Action a);                // "Light attack"
    static const char* Id(Action a);                  // "light_attack": its name in settings.json
    static bool OnPad(Action a);                      // whether a pad has a button of its own for it
    static bool KeyFree(SDL_Keycode key);             // not one the game keeps for itself
    static bool ButtonFree(int button);

    SDL_Keycode Key(Action a) const;
    int         Button(Action a) const;               // -1 if it has none
    // Gives `a` the key or button. Whoever had it takes what `a` had, and is
    // returned; Action::COUNT if nobody did, or if nothing changed.
    Action BindKey(Action a, SDL_Keycode key);
    Action BindButton(Action a, int button);

    static string KeyLabel(SDL_Keycode key);          // "J", "Shift", "Space"
    static string ButtonLabel(int button);            // "(A)", "LB", "LT"

    json ToJson() const;
    void FromJson(const json& j);                     // anything it does not like keeps its default
    bool operator==(const Bindings& o) const { return keys == o.keys && buttons == o.buttons; }
};

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

    // --- bindings -------------------------------------------------------------------
    void SetBindings(const Bindings& b);
    const Bindings& GetBindings() const { return Src().bindings; }

    // The Controls screen asks for "whatever is pressed next". While listening
    // nothing pressed reaches the game: the next key (or button, or trigger) is
    // kept for TakeHeard, and Esc or Start calls it off.
    enum class ListenFor { Nothing, Key, Button };
    struct Heard {
        bool any = false, cancelled = false;
        SDL_Keycode key = SDLK_UNKNOWN;
        int button = -1;
    };
    void  Listen(ListenFor what);
    bool  Listening() const { return listening != ListenFor::Nothing; }
    Heard TakeHeard();

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
    // A pad button's action, and whatever rides on it in a menu.
    void PadSet(Action a, bool value);
    void RebuildMaps();
    void ReleaseAll();
    void OpenGamepad();
    void CloseGamepad();
    bool Repeated(Action a) const;

    ActionState state[ACTION_COUNT];
    // One key can mean something in play and something else in a menu: J is
    // the light attack and also confirms, K the heavy attack and also backs out.
    std::multimap<SDL_Keycode, Action> keymap;
    map<int, Action> padmap;               // SDL_GamepadButton, or a trigger -> Action
    Bindings  bindings;
    ListenFor listening = ListenFor::Nothing;
    Heard     heard;
    bool      trigger_held[2] = {false, false};

    SDL_Gamepad* pad = nullptr;
    SDL_JoystickID pad_id = 0;

    InputMode mode = InputMode::Auto;
    InputMode active = InputMode::KeyboardMouse;

    Vec2 stick{0, 0};
};
