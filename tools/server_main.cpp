// -----------------------------------------------------------------------------
//  DreamQuestServer - the Hollowmarch with nobody at the keyboard.
//
//  The same World, the same coop::Host and the same door as the game, with no
//  window, no renderer and no sound: for the always-on machine on the tailnet,
//  so that nobody has to be host. It has no player of its own -- its home
//  world is a map with an absent seat -- and everyone who joins is a guest,
//  with their character kept on their own machine and their place kept here.
//
//    DreamQuestServer.exe [--port 7777] [--name "The Hollowmarch"]
//                         [--password word] [--start-here]
//                         [--map town_havenbrook] [--world saves/server_world.json]
//                         [--kept saves/characters/kept]
//
//  It needs data/ and maps/ beside it (or one directory up), not assets/. The
//  world's one-shots -- chests opened, levers thrown, trees felled, the clock,
//  the traders' day -- are written to --world every two minutes and on Ctrl+C.
//
//  Build:  build.ps1 -Server
// -----------------------------------------------------------------------------

#include "../src/headers.h"
#include "../src/sprite.h"
#include "../src/world/world.h"
#include "../src/systems/items.h"
#include "../src/systems/loot.h"
#include "../src/systems/quest.h"
#include "../src/systems/dialogue.h"
#include "../src/systems/projectile.h"
#include "../src/systems/spell.h"
#include "../src/systems/talents.h"
#include "../src/coop/coop.h"

#include <csignal>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static volatile std::sig_atomic_t g_stop = 0;
static void OnSignal(int) { g_stop = 1; }

static bool FindGameRoot() {
    vector<fs::path> candidates;
    if (const char* base = SDL_GetBasePath()) {
        fs::path exe_dir(base);
        if (!exe_dir.has_filename()) exe_dir = exe_dir.parent_path();
        candidates.push_back(exe_dir);
        candidates.push_back(exe_dir.parent_path());
    }
    candidates.push_back(fs::current_path());
    std::error_code ec;
    for (const fs::path& dir : candidates) {
        if (dir.empty() || !fs::exists(dir / "data" / "sprites.json", ec)) continue;
        fs::current_path(dir, ec);
        if (!ec) return true;
    }
    return false;
}

// The realm's one-shots. A character is its player's and is not here.
static void SaveWorld(const string& path, const World& world) {
    json flags = json::array();
    for (const string& key : world.Flags()) if (!coop::PrivateFlag(key)) flags.push_back(key);
    json picked = json::object();
    for (const auto& kv : world.PickedHerbs()) picked[kv.first] = kv.second;
    const json j = {{"version", 1}, {"flags", flags}, {"picked", picked},
                    {"clock", world.clock.ToJson()}, {"shops", world.shops.ToJson()}};
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out) out << j.dump(1);
}

static void LoadWorld(const string& path, World& world) {
    std::ifstream in(path);
    if (!in) return;
    try {
        json j;
        in >> j;
        std::set<string> flags;
        if (j.contains("flags") && j["flags"].is_array())
            for (const json& f : j["flags"]) if (f.is_string()) flags.insert(f.get<string>());
        world.SetFlags(flags);
        std::map<string, double> picked;
        if (j.contains("picked") && j["picked"].is_object())
            for (auto it = j["picked"].begin(); it != j["picked"].end(); ++it)
                if (it.value().is_number()) picked[it.key()] = it.value().get<double>();
        world.SetPickedHerbs(picked);
        world.clock.FromJson(j.value("clock", json::object()));
        world.shops.FromJson(j.value("shops", json::object()));
        printf("world: %zu flags, day %d %s (%s)\n", flags.size(), world.clock.Day(), world.clock.TimeText().c_str(), path.c_str());
    } catch (const std::exception& e) {
        printf("world: '%s' could not be read (%s); starting fresh\n", path.c_str(), e.what());
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = net::DEFAULT_PORT;
    string name = "The Hollowmarch", password, start_map = "town_havenbrook", world_path = "saves/server_world.json";
    string kept_dir = "saves/characters/kept";   // friends' characters as last seen, and where they stood
    bool bring_your_own = true;
    float run_for = 0.0f;           // --for <seconds>: stop by itself, for a smoke test
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--port" && more)          { const int p = SDL_atoi(argv[++i]); if (p > 0 && p < 65536) port = static_cast<uint16_t>(p); }
        else if (arg == "--name" && more)     name = argv[++i];
        else if (arg == "--password" && more) password = argv[++i];
        else if (arg == "--map" && more)      start_map = argv[++i];
        else if (arg == "--world" && more)    world_path = argv[++i];
        else if (arg == "--kept" && more)     kept_dir = argv[++i];
        else if (arg == "--for" && more)      run_for = static_cast<float>(SDL_atof(argv[++i]));
        else if (arg == "--start-here")       bring_your_own = false;
        else if (arg == "--help" || arg == "-h") {
            printf("DreamQuestServer [--port 7777] [--name \"The Hollowmarch\"] [--password word] [--start-here]\n"
                   "                 [--map town_havenbrook] [--world saves/server_world.json]\n");
            return 0;
        }
    }

    if (!FindGameRoot()) {
        printf("Could not find data/ and maps/ beside the executable or one directory up.\n");
        return 1;
    }

    // Everything the game loads, except what is only ever drawn.
    SpriteLibrary sprites;
    ItemDatabase items;
    EnemyDatabase enemy_db;
    LootSystem loot;
    DialogueDatabase dialogue;
    ProjectileDatabase projectiles;
    SpellBook spells;
    SkillTrees trees;
    StatusDatabase statuses;
    bool ok = true;
    ok &= sprites.Load("data/sprites.json");
    ok &= items.Load("data/items.json");
    items.Load("data/items_armour.json", false);
    ok &= items.LoadTiers("data/tiers.json");
    ok &= items.LoadEnchantments("data/enchantments.json");
    ok &= enemy_db.Load("data/enemies.json");
    ok &= loot.Load("data/loot_tables.json");
    loot.Load("data/loot_tables_armour.json", false);
    ok &= dialogue.Load("data/dialogue.json");
    ok &= projectiles.Load("data/projectiles.json");
    ok &= spells.Load("data/spells.json");
    ok &= trees.Load("data/skill_trees.json");
    ok &= statuses.Load("data/statuses.json");
    if (!ok) { printf("One or more data files failed to load.\n"); return 1; }

    std::mt19937 rng(std::random_device{}());
    GameContext ctx;
    ctx.sprites = &sprites;   ctx.items = &items;   ctx.loot = &loot;
    ctx.dialogue = &dialogue; ctx.enemies = &enemy_db;
    ctx.projectiles = &projectiles; ctx.spells = &spells; ctx.trees = &trees; ctx.statuses = &statuses;
    ctx.rng = &rng;

    World home;
    home.SeedDice(std::random_device{}());
    home.player.absent = true;
    LoadWorld(world_path, home);
    if (!home.LoadMap(start_map, "", ctx)) {
        printf("The start map '%s' could not be loaded.\n", start_map.c_str());
        return 1;
    }

    const net::DataHashes hashes = net::ComputeDataHashes(".");
    net::Server::Config config;
    config.world_name = net::CleanLine(name, net::MAX_NAME * 2);
    config.data_hash = hashes.data;
    config.maps_hash = hashes.maps;
    config.password = password;
    config.bring_your_own = bring_your_own;
    net::Server server(config);
    string error;
    std::unique_ptr<net::Transport> wire = net::ListenEnet(port, net::MAX_SEATS, error);
    if (!wire) { printf("Could not listen: %s\n", error.c_str()); return 1; }
    server.Attach(std::move(wire));

    printf("%s is open on UDP %u  (protocol %u, data %s, maps %s)%s%s\n", config.world_name.c_str(), port,
           net::PROTOCOL_VERSION, net::ShortHash(hashes.data).c_str(), net::ShortHash(hashes.maps).c_str(),
           password.empty() ? "" : "  [password]", bring_your_own ? "" : "  [characters start here]");
    for (const net::LocalAddress& a : net::LocalAddresses())
        printf("  friends join: %s%s\n", a.ip.c_str(), a.tailnet ? "   (tailnet)" : "");
    fflush(stdout);

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    coop::Host host;
    host.start_map = start_map;
    host.kept_dir = kept_dir;
    Uint64 previous = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    float since_save = 0.0f, ran = 0.0f;
    size_t seated = 0;
    while (!g_stop) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>((now - previous) / freq);
        previous = now;
        if (dt > 0.05f) dt = 0.05f;

        home.Update(dt, ctx);
        server.Update(dt);
        host.Update(dt, server, home, ctx, true);

        if (server.Roster().size() != seated) {
            seated = server.Roster().size();
            printf("%s  %zu here:", home.clock.TimeText().c_str(), seated);
            for (const net::SeatInfo& s : server.Roster()) printf(" %s", s.name.c_str());
            printf("   (%zu maps running)\n", host.Worlds());
            fflush(stdout);
        }
        if ((since_save += dt) > 120.0f) { since_save = 0.0f; SaveWorld(world_path, home); }
        if (run_for > 0.0f && (ran += dt) >= run_for) break;
        SDL_Delay(8);      // about 120 steps a second: well inside a frame of anyone's
    }

    SaveWorld(world_path, home);
    server.Shutdown();
    printf("Saved %s. Goodbye.\n", world_path.c_str());
    return 0;
}
