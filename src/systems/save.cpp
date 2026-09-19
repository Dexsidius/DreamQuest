#include "save.h"
#include "../world/world.h"
#include "quest.h"
#include <fstream>
#include <filesystem>
#include <ctime>

namespace fs = std::filesystem;

// 2: the Hollowmarch grew twenty cells west, so everything on it moved right.
static constexpr int SAVE_VERSION = 2;

// How far a position saved before a map was widened has to move to stand on
// the same ground now. Only the overworld has ever grown.
static float LayoutShiftX(const string& map, int saved_version) {
    if (map == "overworld" && saved_version < 2) return 20.0f * 32.0f;
    return 0.0f;
}

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
    info.character    = j.value("character", string(Player::kDefaultCharacter));
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

    // The time of day, the camp, and -- for a save made in a dream -- where
    // the sleeper is lying.
    j["clock"] = world.clock.ToJson();
    const World::Camp& camp = world.PlayerCamp();
    if (camp.pitched) j["camp"] = {{"map", camp.map}, {"x", camp.x}, {"y", camp.y}};
    const World::DreamReturn& dream = world.Dream();
    if (dream.active) j["dream_return"] = {{"map", dream.map}, {"x", dream.x}, {"y", dream.y}};

    j["shops"] = world.shops.ToJson();
    j["picked"] = json::object();
    for (const auto& kv : world.PickedHerbs()) j["picked"][kv.first] = kv.second;

    j["flags"] = json::array();
    for (const auto& f : world.Flags()) j["flags"].push_back(f);

    // Storage chests, by object id. Only the ones with something in them: an
    // empty chest is rebuilt from the map the next time it is opened.
    j["storage"] = json::object();
    for (const auto& kv : world.Storages()) {
        json slots = kv.second.ToJson();
        bool any = false;
        for (const json& sl : slots) any = any || !sl.is_null();
        if (any) j["storage"][kv.first] = slots;
    }

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
    const int version = j.value("version", 1);

    if (j.contains("quests")) quests.FromJson(j["quests"]);

    std::set<string> flags;
    if (j.contains("flags"))
        for (const auto& f : j["flags"]) flags.insert(f.get<string>());
    world.SetFlags(flags);

    // A saved chest comes back without an item database and at whatever size
    // it was written; World::Storage fixes both the first time it is opened.
    map<string, Inventory> chests;
    if (j.contains("storage") && j["storage"].is_object())
        for (auto it = j["storage"].begin(); it != j["storage"].end(); ++it) {
            if (!it.value().is_array()) continue;
            Inventory inv(nullptr, static_cast<int>(it.value().size()));
            inv.FromJson(it.value());
            chests.emplace(it.key(), std::move(inv));
        }
    world.SetStorages(std::move(chests));

    // Saves from before the clock start at nine in the morning of day one.
    world.clock.FromJson(j.value("clock", json::object()));
    World::Camp camp;
    if (j.contains("camp") && j["camp"].is_object()) {
        camp.pitched = true;
        camp.map = j["camp"].value("map", string(""));
        camp.x = j["camp"].value("x", 0.0f);
        camp.y = j["camp"].value("y", 0.0f);
        camp.x += LayoutShiftX(camp.map, version);
    }
    world.SetCamp(camp);
    World::DreamReturn dream;
    if (j.contains("dream_return") && j["dream_return"].is_object()) {
        dream.active = true;
        dream.map = j["dream_return"].value("map", string("overworld"));
        dream.x = j["dream_return"].value("x", 0.0f);
        dream.y = j["dream_return"].value("y", 0.0f);
        dream.x += LayoutShiftX(dream.map, version);
    }
    world.SetDream(dream);
    world.shops.FromJson(j.value("shops", json::object()));
    map<string, double> picked;
    if (j.contains("picked") && j["picked"].is_object())
        for (auto it = j["picked"].begin(); it != j["picked"].end(); ++it)
            if (it.value().is_number()) picked[it.key()] = it.value().get<double>();
    world.SetPickedHerbs(picked);

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
        world.player.x = j["player"].value("x", world.player.x) + LayoutShiftX(map_id, version);
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
    master_volume   = std::clamp(j.value("master_volume", master_volume), 0.0f, 1.0f);
    sfx_volume      = std::clamp(j.value("sfx_volume", sfx_volume), 0.0f, 1.0f);
    ambience_volume = std::clamp(j.value("ambience_volume", ambience_volume), 0.0f, 1.0f);
    player_name     = j.value("player_name", player_name);
    host_password   = j.value("host_password", host_password);
    bring_your_own  = j.value("bring_your_own", bring_your_own);
    p2_name         = j.value("p2_name", p2_name);
    p2_look         = j.value("p2_look", p2_look);
    split_stacked   = j.value("split_stacked", split_stacked);
    quest_waypoints = j.value("quest_waypoints", quest_waypoints);
    controls        = j.contains("controls") ? j["controls"] : json();
    recent_hosts.clear();
    if (j.contains("recent_hosts") && j["recent_hosts"].is_array())
        for (const json& h : j["recent_hosts"])
            if (h.is_string() && recent_hosts.size() < 5) recent_hosts.push_back(h.get<string>());
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
        {"master_volume", master_volume},
        {"sfx_volume", sfx_volume},
        {"ambience_volume", ambience_volume},
        {"player_name", player_name},
        {"host_password", host_password},
        {"bring_your_own", bring_your_own},
        {"p2_name", p2_name},
        {"p2_look", p2_look},
        {"quest_waypoints", quest_waypoints},
        {"controls", controls},
        {"split_stacked", split_stacked},
        {"recent_hosts", recent_hosts},
    };
    out << j.dump(2);
    return out.good();
}
