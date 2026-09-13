#pragma once
#include "headers.h"
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
#include "systems/save.h"
#include "ui/ui.h"
#include "ui/minimap.h"

enum class GameState {
    MainMenu,
    CharacterSelect,
    SlotSelect,        // used for both starting and saving
    LoadMenu,
    Options,
    Play,
    Paused,
    Inventory,
    SkillsPanel,
    QuestPanel,
    Dialogue,
    Board,
    Note,
    Crafting,
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
    void UpdatePlay(float dt);
    void UpdatePaused();
    void UpdateInventory();
    void UpdateSkillsPanel();
    void UpdateQuestPanel();
    void UpdateDialogue(float dt);
    void UpdateBoard();
    void UpdateNote();
    void UpdateCrafting();
    void UpdateDeath(float dt);

    // --- per-state drawing ---------------------------------------------------
    void DrawMainMenu();
    void DrawCharacterSelect();
    void DrawSlotSelect();
    void DrawLoadMenu();
    void DrawOptions();
    void DrawHud();
    void DrawWorldText();          // floating damage / pickup text
    void DrawPaused();
    void DrawInventory();
    void DrawSkillsPanel();
    void DrawQuestPanel();
    void DrawDialogue();
    void DrawBoard();
    void DrawNote();
    void DrawCrafting();
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
    Input            input;
    Settings         settings;
    SpriteLibrary    sprites;
    ItemDatabase     items;
    EnemyDatabase    enemy_db;
    LootSystem       loot;
    QuestLog         quests;
    DialogueDatabase dialogue_db;
    ProjectileDatabase projectile_db;
    SpellBook        spells;
    DialogueRunner   dialogue;
    std::mt19937     rng;
    GameContext      ctx;

    World world;

    // --- state ---------------------------------------------------------------
    GameState state = GameState::MainMenu;
    GameState return_state = GameState::Play;   // where a panel goes back to
    bool has_session = false;                   // a game is actually in progress

    int  cursor = 0;             // selection in the current list screen
    int  main_menu_cursor = 0;   // restored when a sub-screen backs out to the menu
    int  inventory_cursor = 0;
    int  equipment_cursor = 0;
    bool inventory_on_equipment = false;
    int  quest_cursor = 0;
    int  board_cursor = 0;
    int  craft_cursor = 0;
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
    string   board_title;
    string   craft_title;
    vector<string> board_quests;

    vector<Toast> toasts;
    float playtime = 0.0f;
    float fps = 0.0f;
    float autosave_timer = 0.0f;
};
