#pragma once
#include "../headers.h"

class World;
class QuestLog;
struct GameContext;

// -----------------------------------------------------------------------------
//  Saves and settings.
//
//  A save captures everything needed to put the player back exactly where they
//  left off: which map, where on it, the full skill and inventory state, quest
//  progress, and the one-shot world flags (chests looted, notes read).
// -----------------------------------------------------------------------------

static constexpr int SAVE_SLOTS = 3;

// Summary shown on the load-game screen without loading the whole save.
struct SaveSlotInfo {
    bool   exists = false;
    int    slot = 0;
    string map_name;
    int    combat_level = 1;
    int    total_level = 1;
    float  playtime = 0.0f;
    string saved_at;          // human-readable local timestamp
    string character;         // sprite id, so the slot shows who you were
};

class SaveSystem {
public:
    static string SlotPath(int slot);
    static bool   Exists(int slot);
    static SaveSlotInfo Peek(int slot);
    static vector<SaveSlotInfo> PeekAll();
    static bool   Delete(int slot);

    // Writes through a temporary file and renames, so an interrupted save
    // cannot leave a half-written slot behind.
    static bool Save(int slot, const World& world, const QuestLog& quests,
                     float playtime);
    static bool Load(int slot, World& world, QuestLog& quests,
                     const GameContext& ctx, float& playtime);

    static string FormatPlaytime(float seconds);
};

// Persisted between runs in settings.json, next to the executable.
struct Settings {
    int   input_mode = 0;        // 0 auto, 1 keyboard/mouse, 2 controller
    float zoom = 2.0f;
    bool  fullscreen = false;
    bool  vsync = true;
    bool  show_fps = false;
    bool  damage_numbers = true;
    float ui_scale = 1.0f;
    float master_volume   = 0.8f;
    float sfx_volume      = 1.0f;
    float ambience_volume = 0.7f;

    bool Load(const string& path = "settings.json");
    bool Save(const string& path = "settings.json") const;
};
