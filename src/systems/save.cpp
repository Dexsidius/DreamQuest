#include "save.h"
#include "../world/world.h"
#include "quest.h"
#include <fstream>
#include <filesystem>
#include <ctime>

namespace fs = std::filesystem;

static constexpr int SAVE_VERSION = 1;

string SaveSystem::SlotPath(int slot) {
    return "saves/slot" + std::to_string(std::clamp(slot, 1, SAVE_SLOTS)) + ".json";
}

bool SaveSystem::Exists(int slot) {
    std::error_code ec;
    return fs::exists(SlotPath(slot), ec);
}

static string NowString() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

string SaveSystem::FormatPlaytime(float seconds) {
    const int total = static_cast<int>(seconds);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    char buf[32];
    if (h > 0) SDL_snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
    else       SDL_snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

SaveSlotInfo SaveSystem::Peek(int slot) {
    SaveSlotInfo info;
    info.slot = slot;

    std::ifstream in(SlotPath(slot));
    if (!in) return info;

    json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return info;                     // corrupt slot reads as empty
    }

    info.exists       = true;
    info.map_name     = j.value("map_name", j.value("map", string("Unknown")));
    info.combat_level = j.value("combat_level", 1);
    info.total_level  = j.value("total_level", 1);
    info.playtime     = j.value("playtime", 0.0f);
    info.saved_at     = j.value("saved_at", string(""));
    info.character    = j.value("character", string("player_male"));
    return info;
}

vector<SaveSlotInfo> SaveSystem::PeekAll() {
    vector<SaveSlotInfo> out;
    for (int i = 1; i <= SAVE_SLOTS; ++i) out.push_back(Peek(i));
    return out;
}

bool SaveSystem::Delete(int slot) {
    std::error_code ec;
    return fs::remove(SlotPath(slot), ec);
}

bool SaveSystem::Save(int slot, const World& world, const QuestLog& quests,
                      float playtime) {
    std::error_code ec;
    fs::create_directories("saves", ec);

    json j;
    j["version"]      = SAVE_VERSION;
    j["saved_at"]     = NowString();
    j["playtime"]     = playtime;
    j["map"]          = world.MapId();
    j["map_name"]     = world.CurrentMap().DisplayName();
    j["player"]       = world.player.ToJson();
    j["quests"]       = quests.ToJson();
    j["character"]    = world.player.sprite_id;
    j["combat_level"] = world.player.skills.CombatLevel();
    j["total_level"]  = world.player.skills.TotalLevel();

    j["flags"] = json::array();
    for (const auto& f : world.Flags()) j["flags"].push_back(f);

    // Write to a temp file first so a crash mid-write cannot destroy the
    // existing save.
    const string final_path = SlotPath(slot);
    const string temp_path  = final_path + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        if (!out) {
            SDL_Log("SaveSystem: cannot write '%s'", temp_path.c_str());
            return false;
        }
        out << j.dump(2);
        if (!out.good()) {
            SDL_Log("SaveSystem: write failed for '%s'", temp_path.c_str());
            return false;
        }
    }

    fs::remove(final_path, ec);
    fs::rename(temp_path, final_path, ec);
    if (ec) {
        SDL_Log("SaveSystem: could not finalise '%s': %s",
                final_path.c_str(), ec.message().c_str());
        return false;
    }

    SDL_Log("SaveSystem: saved slot %d (%s)", slot, world.MapId().c_str());
    return true;
}

bool SaveSystem::Load(int slot, World& world, QuestLog& quests,
                      const GameContext& ctx, float& playtime) {
    std::ifstream in(SlotPath(slot));
    if (!in) {
        SDL_Log("SaveSystem: slot %d is empty", slot);
        return false;
    }

    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        SDL_Log("SaveSystem: slot %d is corrupt: %s", slot, e.what());
        return false;
    }

    playtime = j.value("playtime", 0.0f);

    if (j.contains("quests")) quests.FromJson(j["quests"]);

    std::set<string> flags;
    if (j.contains("flags"))
        for (const auto& f : j["flags"]) flags.insert(f.get<string>());
    world.SetFlags(flags);

    // Restore the character before the map, so the sprite and stats are in
    // place by the time entities spawn around them.
    if (j.contains("player")) world.player.FromJson(j["player"], ctx);

    const string map_id = j.value("map", string("overworld"));
    if (!world.LoadMap(map_id, "", ctx)) {
        SDL_Log("SaveSystem: save references missing map '%s'", map_id.c_str());
        return false;
    }

    // LoadMap drops the player on a spawn point; put them back where they were.
    if (j.contains("player")) {
        world.player.x = j["player"].value("x", world.player.x);
        world.player.y = j["player"].value("y", world.player.y);
    }
    world.camera.SnapTo(world.player.x, world.player.y);

    SDL_Log("SaveSystem: loaded slot %d (%s)", slot, map_id.c_str());
    return true;
}

// --- Settings ----------------------------------------------------------------

bool Settings::Load(const string& path) {
    std::ifstream in(path);
    if (!in) return false;

    json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        SDL_Log("Settings: '%s' is malformed; using defaults", path.c_str());
        return false;
    }

    input_mode     = std::clamp(j.value("input_mode", input_mode), 0, 2);
    zoom           = std::clamp(j.value("zoom", zoom), 1.0f, 5.0f);
    fullscreen     = j.value("fullscreen", fullscreen);
    vsync          = j.value("vsync", vsync);
    show_fps       = j.value("show_fps", show_fps);
    damage_numbers = j.value("damage_numbers", damage_numbers);
    ui_scale       = std::clamp(j.value("ui_scale", ui_scale), 0.75f, 1.5f);
    return true;
}

bool Settings::Save(const string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    const json j = {
        {"input_mode", input_mode},
        {"zoom", zoom},
        {"fullscreen", fullscreen},
        {"vsync", vsync},
        {"show_fps", show_fps},
        {"damage_numbers", damage_numbers},
        {"ui_scale", ui_scale},
    };
    out << j.dump(2);
    return out.good();
}
