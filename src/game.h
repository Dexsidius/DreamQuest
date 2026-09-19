#pragma once
#include "headers.h"
#include "systems/audio.h"
#include "input.h"
#include "texturecache.h"
#include "sprite.h"
#include "world/world.h"
#include "systems/items.h"
#include "systems/loot.h"
#include "systems/quest.h"
#include "systems/dialogue.h"
#include "systems/projectile.h"
#include "systems/spell.h"
#include "systems/talents.h"
#include "systems/save.h"
#include "ui/ui.h"
#include "ui/minimap.h"
#include "systems/waypoint.h"
#include "ui/worldmap.h"
#include "ui/titlescreen.h"
#include "net/session.h"
#include "coop/coop.h"

enum class GameState {
    MainMenu,
    CharacterSelect,
    SlotSelect,        // used for both starting and saving
    LoadMenu,
    Options,
    Controls,          // which key and which button does what
    Multiplayer,       // Play Together: host, join, who is here, the chat line
    Play,
    Paused,
    Inventory,
    SkillsPanel,
    QuestPanel,
    WorldMapPage,
    Dialogue,
    Board,
    Note,
    SleepPrompt,       // a bed after dusk: sleep the night through, or dream
    Crafting,
    Enchanting,
    Shop,
    Storage,
    Death,
};

// A short-lived message in the top-right: level ups, quest updates, saves.
struct Toast {
    string text;
    SDL_Color color{255, 255, 255, 255};
    float life = 0.0f, max_life = 3.2f;
};

class Game {
public:
    Game();
    ~Game();

    int  Start(int argc, char* args[]);
    void Loop();

private:
    // --- lifecycle -----------------------------------------------------------
    bool LoadContent();
    void ApplySettings();
    void NewGame(const string& character, int slot);
    bool LoadGame(int slot);
    bool SaveGame(int slot);

    // --- frame ---------------------------------------------------------------
    void Process(float dt);
    void Update(float dt);
    void Render();

    void HandleWorldRequests();
    void HandleDialogueActions(const vector<DialogueAction>& actions);
    // What dialogue conditions are judged against, right now.
    DialogueContext MakeDialogueContext() const;
    void GrantQuestRewards(const string& quest_id);
    void PushToast(const string& text, SDL_Color color = Palette::Text);

    // --- state helpers -------------------------------------------------------
    void SetState(GameState s);
    void OpenPanel(GameState panel);   // remembers where to return to
    void ClosePanel();
    bool InGameplayState() const;

    // --- per-state input -----------------------------------------------------
    void UpdateMainMenu();
    void UpdateCharacterSelect();
    void UpdateSlotSelect();
    void UpdateLoadMenu();
    void UpdateOptions();
    void UpdateControls();
    void UpdateMultiplayer();
    void UpdatePlay(float dt);
    void UpdatePaused();
    void UpdateInventory();
    void UpdateSkillsPanel();
    void UpdateQuestPanel();
    void UpdateWorldMap();
    // The ids in one tab of the journal, active first; active_count is how
    // many of them are still going.
    void QuestList(int tab, vector<string>& out, size_t& active_count,
                   size_t& not_started_count) const;
    static const char* QuestTabName(int tab);
    void UpdateDialogue(float dt);
    void UpdateBoard();
    void UpdateNote();
    void UpdateSleepPrompt();
    void UpdateCrafting();
    void UpdateEnchanting();
    void UpdateShop();
    void UpdateStorage();
    void UpdateDeath(float dt);

    // --- per-state drawing ---------------------------------------------------
    void DrawMainMenu();
    void DrawCharacterSelect();
    void DrawSlotSelect();
    void DrawLoadMenu();
    void DrawOptions();
    void DrawControls();
    void DrawMultiplayer();
    void DrawHud();
    void DrawWorldText();          // floating damage / pickup text
    void DrawPaused();
    void DrawInventory();
    void DrawSkillsPanel();
    void DrawSkillTree(const SDL_FRect& panel);
    void DrawQuestPanel();
    void DrawWorldMap();
    void DrawDialogue();
    void DrawBoard();
    void DrawNote();
    void DrawSleepPrompt();
    void DrawCrafting();
    void DrawEnchanting();
    void DrawShop();
    void DrawStorage();
    void DrawDeath();
    void DrawToasts();
    void DrawSlotList(const SDL_FRect& area, const string& heading);

    // Shared cursor movement for every list-shaped screen.
    void MoveCursor(int& cursor, int count, bool wrap = true);

    // --- SDL -----------------------------------------------------------------
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Event     event{};
    int  screen_w = 1280, screen_h = 720;
    bool running = true;

    // --- services ------------------------------------------------------------
    TextureCache*    textures = nullptr;
    UI               ui;
    Minimap          minimap;
    WorldMapPanel    world_map;
    bool             map_overview = false;   // the map screen is turned to the Hollowmarch
    TitleBackdrop    title;
    Input            input;
    Settings         settings;
    SpriteLibrary    sprites;
    ItemDatabase     items;
    EnemyDatabase    enemy_db;
    LootSystem       loot;
    // The journal of whoever is being served: Player One's own, or in split
    // screen Player Two's. See ServeSeat.
    QuestLog         own_quests, quests_two;
    QuestLog*        quests = &own_quests;
    DialogueDatabase dialogue_db;
    ProjectileDatabase projectile_db;
    SpellBook        spells;
    SkillTrees       skill_trees;
    ShopDatabase     shop_db;
    DialogueRunner   dialogue;
    std::mt19937     rng;
    GameContext      ctx;

    // The world of whoever is being served. Alone that is always the game's
    // own; in split screen Player Two may be on another map, which is another
    // World in the realm, and while their half of the screen is drawn or their
    // bag is open this points there. See ServeSeat.
    World  home_world;
    World* world = &home_world;

    // --- state ---------------------------------------------------------------
    GameState state = GameState::MainMenu;
    GameState return_state = GameState::Play;   // where a panel goes back to
    bool has_session = false;                   // a game is actually in progress

    int  cursor = 0;             // selection in the current list screen
    int  main_menu_cursor = 0;   // restored when a sub-screen backs out to the menu
    int  inventory_cursor = 0;
    int  equipment_cursor = 0;
    bool inventory_on_equipment = false;
    // The journal's tabs: 0 the story, 1 the tutorials, 2 everything else.
    // One cursor each, so stepping between them does not lose your place.
    static constexpr int kQuestTabs = 3;
    int  quest_tab = 0;
    int  quest_cursor[kQuestTabs] = {0, 0, 0};
    int  board_cursor = 0;
    int  craft_cursor = 0;
    // The enchanting table: which enchantment, and which of the pieces in
    // the bag that take it.
    int  enchant_cursor = 0, enchant_target = 0;
    // The bag slot the drop key was pressed on once, when it held more than
    // one: a stack goes on a second press, so a whole purse of coins is not
    // one slip of a finger from the floor.
    int  drop_armed = -1;
    // The skills panel: 0 is the level list, 1..3 the melee, ranged and magic
    // trees, with a cursor on a branch and a row in whichever tree is open.
    int  skills_tab = 0;
    int  tree_branch = 0, tree_row = 0;
    bool tree_reset_armed = false;
    int  slot_purpose = 0;       // 0 = start new game, 1 = save
    int  overwrite_slot = -1;    // occupied slot a new game is waiting to replace

    string pending_character = "player_hero";

    // The playable characters, shared between the select screen's update and
    // its draw so the two can never disagree about what is on offer.
    static constexpr int kCharacterCount = 3;
    static const char* kCharacterIds[kCharacterCount];
    static const char* kCharacterLabels[kCharacterCount];
    int    active_slot = 1;

    // Board / note payloads handed over by the world.
    string   note_title, note_text, note_quest;
    // The bed's question: what is being slept on, and which of the two answers
    // the cursor is on. It stays where it was left, so someone who always
    // dreams, or never does, is one press from it.
    string   sleep_title;
    int      sleep_cursor = 0;
    string   board_title;
    string   craft_title;
    CraftStation craft_station = CraftStation::Workbench;
    vector<string> board_quests;

    // The trader being dealt with: which shop, buying (0) or selling (1), and
    // the row. pending_shop is set by a dialogue line and opened once the
    // conversation has closed.
    string shop_id, pending_shop;
    // An NPC's order book, asked for in conversation and opened once it closes.
    string pending_orders;
    bool   board_orders = false;     // the board panel is showing an order book
    void   OpenOrders(const string& npc_id, const string& npc_name);
    int    shop_tab = 0;
    int    shop_cursor = 0;
    // The storage chest standing open: which one, what it is called, how many
    // slots it has, and where the cursor is on each side of it.
    string storage_id, storage_title;
    int    storage_slots = 100;
    int    storage_cursor = 0, storage_bag_cursor = 0;
    bool   storage_on_chest = false;
    void   OpenShop(const string& id);
    // What the player could sell here: one row per item carried, in bag order.
    vector<string> ShopSellRows() const;

    vector<Toast> toasts;

    // --- playing together ------------------------------------------------------
    // The session outlives the screen that started it: a host who goes off to
    // play keeps the door open, and is pumped every frame whatever the state.
    // See ui/lobby.cpp.
    net::Session session;
    uint16_t mp_port = net::DEFAULT_PORT;
    string   mp_name, mp_address, mp_say, mp_error;
    string   mp_hostname;                       // this machine, as friends dial it
    vector<net::LocalAddress> mp_addresses;
    // What --host and --join asked for, acted on once the game has started.
    bool     launch_host = false;
    string   launch_join;
    // For checking the screens without a pair of hands: --say sends one line
    // as soon as there is a seat to say it from, and --shot writes the frame
    // to a PNG after a number of seconds and quits.
    string   launch_say, shot_path;
    float    shot_after = 3.0f, run_time = 0.0f;
    // --scratch <character> starts a game that is never written anywhere, so
    // a host can be stood in a world without a save slot being touched; and
    // --hold <key> <from> <to> holds a key down between two moments, which is
    // a pair of hands for a screenshot. While `never_save` is set nothing is
    // saved, by any route.
    string   launch_scratch;
    bool     never_save = false;
    // --map <id> [spawn] and --wear a,b,c: where a scratch game starts, and in what.
    string   launch_map, launch_spawn, launch_wear;
    // Where the quest being followed is, for whoever's HUD is being drawn:
    // worked out a few times a second, and at once when the map, the quest or
    // its stage changes. See systems/waypoint.h.
    WaypointIndex waypoints;
    Waypoint      seat_waypoint[2];
    string        seat_waypoint_key[2];
    Uint64        seat_waypoint_at[2] = {0, 0};
    const Waypoint& CurrentWaypoint();
    void     DrawWaypoint(const Waypoint& wp);

    // The Controls screen: which row, which column (0 the keyboard, 1 the
    // controller), and what it last had to say about a change.
    int      controls_cursor = 0, controls_column = 0;
    string   controls_note;
    // Pushes the saved bindings to both players' inputs.
    void     ApplyBindings();
    int      launch_level = 0;          // --level N: a scratch character starts with its path's skill here
    float    launch_hour = -1.0f;       // --hour H: and at this time of day, for looking at the night or a dream
    string   launch_quests;             // --quest a,b: with these quests taken
    string   launch_screen;             // --screen controls|options|map|journal: and this open
    // --audit: opens every menu in turn at a few window sizes, says which of
    // them draw text that runs off a panel or off the screen, and quits. See
    // Game::RunAudit.
    bool     launch_audit = false;
    void     RunAudit();
    string   launch_learn;              // --learn a,b,c: with these skill-tree nodes bought, techniques switched on
    struct HeldKey { SDL_Keycode key = 0; float from = 0.0f, to = 0.0f; int sent = 0; };
    vector<HeldKey> launch_holds;
    // The world, shared (see coop/coop.h). Hosting, friends' characters are
    // stepped in this machine's world. As a guest, this machine's world is a
    // window onto the host's: `guest_session` is a game with no save slot, no
    // autosave and no monsters of its own, begun when the host says which map
    // to load and over when the line drops or the host leaves the world.
    coop::Host  coop_host;
    coop::Guest coop_guest;
    bool guest_session = false;
    void UpdateCoop(float dt);
    void EnterAsGuest(const net::Enter& enter);
    void EndGuestSession(const string& why);
    // A guest's character is kept on their own machine, so dropping out and
    // coming back another day picks up where they left off: bag, skills,
    // journal, recipes and storage here; where they stood, with the host.
    // --- split screen: two players at this machine ------------------------------
    // See ui/splitscreen.cpp. The game serves one seat at a time: `world`,
    // `quests` and `input` are Player One's unless ServeSeat(1) has pointed
    // them at Player Two.
    Input   input_two;
    Minimap minimap_two;
    net::Server offline_server{net::Server::Config{}};   // a door nobody can reach: the realm wants one
    bool    split_active = false;
    uint8_t p2_seat = 1;
    bool    p2_arrived = false;
    vector<string> p2_flags;
    int     serving = 0;
    SDL_FRect    view_rect[2] = {{0, 0, 1280, 720}, {0, 0, 0, 0}};
    SDL_Texture* view_texture[2] = {nullptr, nullptr};
    void ServeSeat(int seat);
    net::Server& RealmServer();
    bool JoinSplit(bool without_a_controller = false);
    void LeaveSplit();
    void SavePlayerTwo();
    string PlayerTwoPath() const;
    void LayoutViews();
    void UpdatePlayerTwo(float dt);
    void RenderSplit();
    string SplitRowLabel() const;
    void CycleSplitLook(int step);
    // What the game does for a player every frame of play, whoever is being
    // served: the journal, levels gained, the buttons that open their panels.
    void SeatChores();
    // The slot is Player One's and the home world's, whoever asked.
    bool WriteSlot(int slot);
    // --p2 and --hold2, for checking the halves without a second pair of hands.
    bool launch_p2 = false;
    struct HeldAction { Action action = Action::MoveRight; float from = 0.0f, to = 0.0f; int sent = 0; };
    vector<HeldAction> launch_holds_two;

    string characters_dir = "saves/characters";
    string GuestCharacterPath() const;
    void   SaveGuestCharacter();
    string mp_password;                 // what to say at a door that asks
    void DrawNameTags();
    void DrawParty();
    void OpenMultiplayer();
    void UpdateSession(float dt);
    bool StartHosting(uint16_t port);
    bool StartJoining(const string& address);
    net::Session::Identity NetIdentity() const;

    // Typing into a field. While `text_target` is set, key presses are fed to
    // it rather than to the input map; commit, cancel and nav are what the
    // field's owner reads, once, on its next update.
    string* text_target = nullptr;
    size_t  text_limit = 0;
    bool    text_commit = false, text_cancel = false;
    int     text_nav = 0;
    void BeginTextEntry(string* target, size_t limit);
    void EndTextEntry();
    bool TextEntryEvent(const SDL_Event& e);   // true if the field took it

    // The name of a place, put on screen as you walk into it. banner_zone is
    // the last outdoor zone announced, so stepping into a house and back out
    // does not announce the town a second time.
    string banner_title, banner_subtitle, banner_zone, banner_seen_map;
    float  banner_time = 0.0f;
    bool   banner_active = false;
    float playtime = 0.0f;
    float state_time = 0.0f;      // seconds since the last state change
    // A new character has just arrived: the first frame of play opens a note
    // welcoming them, once.
    bool  welcome_pending = false;
    int   quest_day_seen = -1;    // to say so when the boards post new dailies
    float audio_night = -1.0f;    // what the sound was last told about the dark
    float fps = 0.0f;
    float autosave_timer = 0.0f;
};
