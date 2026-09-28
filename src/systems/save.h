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

// Two shelves of slots: the games played alone, and the worlds played
// together -- hosted for friends, or with Player Two at this screen. Each has
// SAVE_SLOTS of its own, in a folder of its own (saves/ and saves/multiplayer/),
// so one never fills up, or writes over, the other.
enum class SaveKind { Single, Multi };

struct SlotRef {
    SaveKind kind = SaveKind::Single;
    int      number = 1;
    SlotRef() = default;
    // A bare number is a single-player slot, which is all a slot ever was.
    SlotRef(int n) : number(n) {}
    SlotRef(SaveKind k, int n) : kind(k), number(n) {}
    bool Multi() const { return kind == SaveKind::Multi; }
    bool operator==(const SlotRef&) const = default;
};

// Summary shown on the load-game screen without loading the whole save.
struct SaveSlotInfo {
    bool   exists = false;
    int    slot = 0;
    SaveKind kind = SaveKind::Single;
    string map_name;
    int    combat_level = 1;
    int    total_level = 1;
    float  playtime = 0.0f;
    string saved_at;          // human-readable local timestamp
    string character;         // sprite id, so the slot shows who you were
    // There is a file and it cannot be read, and neither can its backup. It
    // used to be reported as an empty slot, which is an invitation to start a
    // new game on top of whatever could still have been rescued from it.
    bool   damaged = false;
    // The slot's own file could not be read, and this is its backup: the save
    // before the last one.
    bool   from_backup = false;
    // Who else has played in this world, most recent first: what tells one
    // multiplayer save from another at a glance.
    vector<string> played_with;
};

class SaveSystem {
public:
    // Where saves live. "saves" unless somebody says otherwise -- which the
    // self-test does, so that checking what happens to a damaged slot never
    // goes near a real one.
    static void   SetDirectory(const string& dir);
    static const string& Directory();
    static string SlotPath(SlotRef slot);
    // The save before the last one, kept beside it. One generation: enough to
    // come back from a file that went bad, without keeping a history.
    static string BackupPath(SlotRef slot);
    // Where a deleted slot goes, so that one mis-press is not the end of a
    // character: it is overwritten by the next delete of the same slot.
    static string DeletedPath(SlotRef slot);
    // Where the host keeps its copy of every friend's character, and the spot
    // they were standing on, for the world in this slot: a multiplayer slot
    // has its own, so friends come back to where they left off in *this*
    // world. A single-player slot has the one folder they all used to share.
    static string FriendsPath(SlotRef slot);
    // A different world is taking the slot -- a new game, a game carried or
    // saved into it, a delete: the friends the last one kept are put aside as
    // "<folder>.deleted", in place of whatever was put aside there before.
    static void   SetFriendsAside(SlotRef slot);
    // True when there is anything in the slot at all, readable or not: the
    // question "would a new game here destroy something".
    static bool   Occupied(SlotRef slot);
    static bool   Exists(SlotRef slot);
    static SaveSlotInfo Peek(SlotRef slot);
    static vector<SaveSlotInfo> PeekAll(SaveKind kind = SaveKind::Single);
    static bool   Delete(SlotRef slot);
    // "slot 2", or "multiplayer slot 2": what the messages call it.
    static string Describe(SlotRef slot, bool capital = false);

    // Writes through a temporary file and renames, so an interrupted save
    // cannot leave a half-written slot behind. `played_with` is who else has
    // played in the world, for the slot list to say.
    static bool Save(SlotRef slot, const World& world, const QuestLog& quests,
                     float playtime, const vector<string>& played_with = {});
    // `from_backup`, when given, is set if the slot's own file could not be
    // read and its backup was loaded in its place.
    static bool Load(SlotRef slot, World& world, QuestLog& quests,
                     const GameContext& ctx, float& playtime, bool* from_backup = nullptr);

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
    // Experience as it is earned, beside the vitals: "+48 Strength  62%".
    bool  xp_drops = true;
    float ui_scale = 1.0f;
    float master_volume   = 0.8f;
    float sfx_volume      = 1.0f;
    float ambience_volume = 0.7f;
    // Playing together: what friends see you as, and the last five hosts
    // dialled, newest first. An empty name is filled in from the machine's
    // user name the first time the game starts.
    string player_name;
    vector<string> recent_hosts;
    // Hosting: a word friends must give at the door (empty: the tailnet is
    // the door), and whether their characters travel here with them or this
    // world keeps its own.
    string host_password;
    bool   bring_your_own = true;
    // Split screen: who Player Two is, and whether the halves are side by
    // side or one above the other.
    string p2_name = "Player Two";
    string p2_look = "player_warden";
    bool   split_stacked = false;
    // Which key and which button does what, as Bindings::ToJson wrote it; null
    // until somebody has changed one, which reads back as the defaults.
    json   controls;
    // The marker that says where the quest being followed is.
    bool   quest_waypoints = true;
    // The Visual Effects page: the shaders as a whole, and the parts of them
    // some people would rather not have -- see Shaders::Options.
    bool   visual_effects = true;
    bool   screen_shake = true;
    bool   flashes = true;
    bool   colour_fringing = true;
    bool   screen_distortion = true;

    bool Load(const string& path = "settings.json");
    bool Save(const string& path = "settings.json") const;
};
