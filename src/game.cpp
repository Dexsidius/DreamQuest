#include "game.h"
#include "systems/gathering.h"

static constexpr float AUTOSAVE_INTERVAL = 120.0f;
static constexpr float MAX_FRAME_DT      = 0.05f;   // clamp after a stall

Game::Game() : rng(std::random_device{}()) {}

Game::~Game() {
    // Say goodbye while there is still a frame to say it in: friends are told
    // at once rather than finding out from a timeout.
    EndTextEntry();
    if (guest_session) SaveGuestCharacter();
    ServeSeat(0);
    SavePlayerTwo();
    for (SDL_Texture*& t : view_texture) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    minimap_two.Forget();
    session.Leave();
    // The minimap owns a texture, so it has to let go before the renderer does.
    minimap.Forget();
    world_map.Forget();
    Audio::Shutdown();
    ui.Shutdown();
    delete textures;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

int Game::Start(int argc, char** argv) {
    // Playing together, from a shortcut or a terminal:
    //   --host [port]        open the door as soon as the game is up
    //   --join name[:port]   dial a host
    //   --name "Oona"        what friends see you as (remembered)
    string launch_name;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        const bool more = i + 1 < argc && argv[i + 1][0] != '-';
        if (arg == "--host") {
            launch_host = true;
            if (more) {
                const int p = SDL_atoi(argv[++i]);
                if (p > 0 && p < 65536) mp_port = static_cast<uint16_t>(p);
            }
        } else if (arg == "--join" && more) {
            launch_join = argv[++i];
        } else if (arg == "--name" && more) {
            launch_name = argv[++i];
        } else if (arg == "--password" && more) {
            mp_password = argv[++i];
        } else if (arg == "--say" && more) {
            launch_say = argv[++i];
        } else if (arg == "--p2") {
            launch_p2 = true;
        } else if (arg == "--hold2" && i + 3 < argc) {
            // up, down, left, right, light, strong, interact, jump, sprint, bag
            const string what = argv[++i];
            HeldAction h;
            h.action = what == "up" ? Action::MoveUp : what == "down" ? Action::MoveDown : what == "left" ? Action::MoveLeft
                     : what == "light" ? Action::LightAttack : what == "strong" ? Action::StrongAttack
                     : what == "interact" ? Action::Interact : what == "jump" ? Action::Jump
                     : what == "sprint" ? Action::Sprint : what == "bag" ? Action::Inventory : Action::MoveRight;
            h.from = static_cast<float>(SDL_atof(argv[++i]));
            h.to   = static_cast<float>(SDL_atof(argv[++i]));
            launch_holds_two.push_back(h);
        } else if (arg == "--scratch" && more) {
            launch_scratch = argv[++i];
            never_save = true;
        } else if (arg == "--level" && more) {
            launch_level = std::clamp(SDL_atoi(argv[++i]), 1, 99);
        } else if (arg == "--audit") {
            launch_audit = true;
            launch_scratch = launch_scratch.empty() ? "hero" : launch_scratch;
            never_save = true;
        } else if (arg == "--quest" && more) {
            launch_quests = argv[++i];
        } else if (arg == "--screen" && more) {
            launch_screen = argv[++i];
        } else if (arg == "--learn" && more) {
            // With --scratch and --level: buy these nodes, in order, and switch on any that is a technique.
            // A name that starts "spell:" is an ancient spell to know instead,
            // and one that starts "zap:" is which of the lightning to hold.
            launch_learn = argv[++i];
        } else if (arg == "--charge" && more) {
            // With --scratch: start with this much in the lightning's battery,
            // 0 to 1, for looking at what it pays for.
            launch_charge = std::clamp(static_cast<float>(SDL_atof(argv[++i])), 0.0f, 1.0f);
        } else if (arg == "--hour" && more) {
            launch_hour = std::clamp(static_cast<float>(SDL_atof(argv[++i])), 0.0f, 23.99f);
        } else if (arg == "--map" && more) {
            // With --scratch: start on this map, at this spawn if one is named.
            launch_map = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') launch_spawn = argv[++i];
        } else if (arg == "--bag" && more) {
            // With --scratch: put these in the pack, comma separated, worn or not.
            launch_bag = argv[++i];
        } else if (arg == "--at" && i + 2 < argc) {
            // With --scratch and --map: stood at this point of it, in pixels,
            // rather than at a spawn -- for looking at somewhere no door leads to.
            launch_at_x = static_cast<float>(SDL_atof(argv[++i]));
            launch_at_y = static_cast<float>(SDL_atof(argv[++i]));
        } else if (arg == "--slay" && more) {
            // With --scratch: these bosses are already dead at this character's
            // hands, with the point and the boon each left.
            launch_slay = argv[++i];
        } else if (arg == "--wear" && more) {
            // With --scratch: put these on, comma separated.
            launch_wear = argv[++i];
        } else if (arg == "--hold" && i + 3 < argc) {
            HeldKey h;
            h.key  = SDL_GetKeyFromName(argv[++i]);
            h.from = static_cast<float>(SDL_atof(argv[++i]));
            h.to   = static_cast<float>(SDL_atof(argv[++i]));
            if (h.key != SDLK_UNKNOWN) launch_holds.push_back(h);
        } else if (arg == "--shot" && more) {
            shot_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shot_after = static_cast<float>(SDL_atof(argv[++i]));
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("DreamQuest: SDL_Init failed: %s", SDL_GetError());
        return 0;
    }
    if (!TTF_Init()) {
        SDL_Log("DreamQuest: TTF_Init failed: %s", SDL_GetError());
        return 0;
    }

    settings.Load();
    // A name to be known by, without being asked for one: whoever is logged in
    // to the machine. It can be changed on the Play Together screen.
    if (!launch_name.empty()) settings.player_name = launch_name;
    if (net::CleanLine(settings.player_name, net::MAX_NAME).empty()) {
        const char* user = SDL_getenv("USERNAME");
        if (!user || !*user) user = SDL_getenv("USER");
        settings.player_name = net::CleanLine(user ? user : "", net::MAX_NAME);
        if (settings.player_name.empty()) settings.player_name = "Traveller";
    }
    // Silence is a fine fallback: a machine with no output device still plays.
    Audio::Init();

    const SDL_WindowFlags flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    window = SDL_CreateWindow("DreamQuest", screen_w, screen_h, flags);
    if (!window) {
        SDL_Log("DreamQuest: could not create window: %s", SDL_GetError());
        return 0;
    }

    // The title painting's own crest, for the window and the taskbar. Windows
    // takes the .exe's embedded icon for the shortcut and the file itself;
    // this is what the running window shows, and it is what every other
    // platform has. Missing art is not worth refusing to start over.
    if (SDL_Surface* icon = IMG_Load(TitleScreen::kIconPath)) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    } else {
        SDL_Log("DreamQuest: no window icon (%s): %s", TitleScreen::kIconPath, SDL_GetError());
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("DreamQuest: could not create renderer: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    textures = new TextureCache(renderer);
    if (!ui.Init(renderer))
        SDL_Log("DreamQuest: continuing without text rendering");

    if (!LoadContent()) return 0;

    ctx.renderer = renderer;
    ctx.textures = textures;
    ctx.sprites  = &sprites;
    ctx.items    = &items;
    ctx.loot     = &loot;
    ctx.quests   = quests;
    ctx.dialogue = &dialogue_db;
    ctx.enemies  = &enemy_db;
    ctx.projectiles = &projectile_db;
    ctx.spells   = &spells;
    ctx.trees    = &skill_trees;
    ctx.statuses = &status_db;
    ctx.input    = &input;
    ctx.rng      = &rng;

    ApplySettings();
    SetState(GameState::MainMenu);
    if (launch_audit) {
        NewGame("player_hero", active_slot);
        RunAudit();
        return 0;
    }

    // --host and --join land on the Play Together screen with the thing
    // already under way, so whatever goes wrong is said where it can be read.
    if (!launch_scratch.empty()) {
        // A game nobody keeps: "hero", "warden", "wayfarer", or a full id.
        string who = launch_scratch;
        if (who.rfind("player_", 0) != 0) who = "player_" + who;
        if (!sprites.Has(who)) who = Player::kDefaultCharacter;
        pending_character = who;
        if (launch_host || launch_join.empty()) {
            NewGame(who, active_slot);
            welcome_pending = false;
            // A scratch character with some levels behind them, for looking
            // at a skill tree without playing forty hours first.
            if (launch_level > 1) {
                LevelUp up;
                Player& p = world->player;
                p.skills.AddXp(skill_trees.Tree(p.Affinity()).skill, XpForLevel(launch_level), up);
                p.skills.AddXp(SKILL_HITPOINTS, XpForLevel(std::max(10, launch_level)), up);
                p.SyncHitpoints();
                p.SyncMana();
                p.Rest();
                p.TakeLevelUps(); p.TakeXpDrops();
            }
            // Dressed for the look of it: requirements are not asked, because
            // what is being looked at is the art.
            if (launch_charge > 0.0f) world->player.AddBattery(launch_charge);
            if (!launch_learn.empty()) {
                Player& p = world->player;
                size_t from = 0;
                while (from <= launch_learn.size()) {
                    const size_t comma = launch_learn.find(',', from);
                    const string id = launch_learn.substr(from, comma == string::npos ? string::npos : comma - from);
                    // "spell:<id>" is an ancient spell, known as if its tome had been read.
                    if (id.rfind("spell:", 0) == 0) world->SetFlag("recipe:" + id);
                    // "zap:<id>" chooses which of the lightning is on the fifth
                    // key, and selects the school, for looking at one of them.
                    else if (id.rfind("zap:", 0) == 0) {
                        p.SetElectricSpell(id.substr(4));
                        p.SelectElement(Element::Electric);
                    }
                    else if (p.talents.Learn(id, p.skills)) {
                        p.talents.ToggleTechnique(id);
                        // An ability is no use learned but unslotted: put it on
                        // the first bar that is free, as the Skills screen would.
                        const TalentNode* node = p.talents.Database() ? p.talents.Database()->Find(id) : nullptr;
                        if (node && !node->ability.empty())
                            for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot)
                                if (p.talents.AbilityNode(slot).empty()) { p.talents.SetAbility(slot, id); break; }
                    }
                    if (comma == string::npos) break;
                    from = comma + 1;
                }
            }
            if (!launch_wear.empty()) {
                size_t from = 0;
                while (from <= launch_wear.size()) {
                    const size_t comma = launch_wear.find(',', from);
                    const string id = launch_wear.substr(from, comma == string::npos ? string::npos : comma - from);
                    if (const ItemDef* d = items.Get(id)) {
                        // What is worn is worn; a bag is shouldered; anything
                        // else just goes in the pack.
                        // A second dagger goes in the other hand, as it would from the bag.
                        const ItemDef* right = world->player.equipment.Weapon();
                        const bool second = d->slot == SLOT_WEAPON && d->offhand && right && right->offhand;
                        if (d->slot != SLOT_NONE) world->player.equipment.Equip(second ? SLOT_SHIELD : d->slot, id);
                        else if (world->player.inventory.Add(id, 1) > 0 && d->use == "bag") {
                            string why;
                            for (int k = 0; k < world->player.inventory.SlotCount(); ++k)
                                if (world->player.inventory.Slot(k).id == id) { world->player.WearBag(k, why); break; }
                        }
                    }
                    if (comma == string::npos) break;
                    from = comma + 1;
                }
            }
            if (!launch_bag.empty()) {
                size_t from = 0;
                while (from <= launch_bag.size()) {
                    const size_t comma = launch_bag.find(',', from);
                    const string id = launch_bag.substr(from, comma == string::npos ? string::npos : comma - from);
                    if (items.Get(id)) world->player.inventory.Add(id, 1);
                    if (comma == string::npos) break;
                    from = comma + 1;
                }
            }
            if (!launch_slay.empty()) {
                size_t from = 0;
                while (from <= launch_slay.size()) {
                    const size_t comma = launch_slay.find(',', from);
                    const string id = launch_slay.substr(from, comma == string::npos ? string::npos : comma - from);
                    if (enemy_db.Get(id)) world->AwardBoss(id, ctx);
                    if (comma == string::npos) break;
                    from = comma + 1;
                }
            }
            // The hour before the map: who keeps a dream's platforms is settled
            // by the day as it loads, and a dream walked into by daylight is over.
            if (launch_hour >= 0.0f) world->clock.Set(world->clock.Day(), launch_hour);
            if (!launch_map.empty()) world->LoadMap(launch_map, launch_spawn.empty() ? "default" : launch_spawn, ctx);
            if (launch_at_x >= 0.0f && launch_at_y >= 0.0f) {
                world->player.x = launch_at_x;
                world->player.y = launch_at_y;
                world->camera.SnapTo(launch_at_x, launch_at_y);
            }
            if (!launch_quests.empty()) {
                size_t from = 0;
                while (from <= launch_quests.size()) {
                    const size_t comma = launch_quests.find(',', from);
                    quests->Start(launch_quests.substr(from, comma == string::npos ? string::npos : comma - from));
                    if (comma == string::npos) break;
                    from = comma + 1;
                }
                quests->TakeJustStarted();
            }
            // Any panel, for looking at: "shop:<id>", "craft:<station>",
            // "orders:<npc>", "board:<object id>" and "tree" take what they open.
            {
                const size_t colon = launch_screen.find(':');
                const string what = launch_screen.substr(0, colon);
                const string arg = colon == string::npos ? string() : launch_screen.substr(colon + 1);
                if (what == "controls")       { OpenPanel(GameState::Options); SetState(GameState::Controls); }
                else if (what == "options")   OpenPanel(GameState::Options);
                else if (what == "map")       OpenPanel(GameState::WorldMapPage);
                else if (what == "journal")   OpenPanel(GameState::QuestPanel);
                else if (what == "inventory") OpenPanel(GameState::Inventory);
                else if (what == "skills")    {
                    skills_tab = 0;
                    OpenPanel(GameState::SkillsPanel);
                    // "skills:magic" opens on that skill, for looking at what
                    // it says the levels are for. After the open: SetState puts
                    // every panel's cursor back to the top.
                    for (int k = 0; !arg.empty() && k < SKILL_COUNT; ++k)
                        if (SDL_strcasecmp(SkillName(k), arg.c_str()) == 0) cursor = k;
                }
                else if (what == "menu")      { hub_cursor = 0; OpenPanel(GameState::Hub); }
                else if (what == "tree")      { OpenPanel(GameState::SkillsPanel); skills_tab = TAB_TREE; }
                else if (what == "spellbook") { OpenPanel(GameState::SkillsPanel); skills_tab = TAB_BOOK; book_row = 0; }
                else if (what == "boons")     { OpenPanel(GameState::SkillsPanel); skills_tab = TAB_BOONS; }
                else if (what == "pause")     OpenPanel(GameState::Paused);
                else if (what == "totems")    { totem_cursor = 0; OpenPanel(GameState::TotemRing); }
                else if (what == "travel")    { travel_from = "waystone_havenbrook"; travel_cursor = 0;
                                                OpenPanel(GameState::Travel); }
                else if (what == "shop")      OpenShop(arg);
                else if (what == "craft")     {
                    craft_station = CraftStationFromName(arg);
                    craft_title = arg == "range" ? "Cooking fire" : arg == "anvil" ? "Anvil"
                                : arg == "cauldron" ? "Cauldron" : arg == "loom" ? "Loom"
                                : (arg == "rack" || arg == "tanning_rack") ? "Tanning Rack" : "Workbench";
                    craft_cursor = 0;
                    OpenPanel(GameState::Crafting);
                }
                else if (what == "enchant")   { craft_title = "Enchanting table"; enchant_cursor = enchant_target = 0; OpenPanel(GameState::Enchanting); }
                else if (what == "storage")   { storage_id = "scratch"; storage_title = "Storage chest"; storage_slots = 100;
                                                storage_cursor = storage_bag_cursor = 0; storage_on_chest = false; OpenPanel(GameState::Storage); }
                else if (what == "orders")    OpenOrders(arg, arg);
                else if (what == "character") SetState(GameState::CharacterSelect);
                else if (what == "load")      SetState(GameState::LoadMenu);
                else if (what == "together")  OpenMultiplayer();
            }
        }
    }
    input_two.SetDevices(false, -1, true);
    LayoutViews();
    if (launch_p2 && has_session) JoinSplit(true);
    if (launch_host || !launch_join.empty()) {
        if (launch_host) StartHosting(mp_port);
        else { mp_address = launch_join; StartJoining(launch_join); }
        // A host already in a world stays in it; everyone else lands on the
        // Play Together screen, where whatever goes wrong can be read.
        if (!has_session) OpenMultiplayer();
    }
    return 1;
}

bool Game::LoadContent() {
    // Data files are required; the game has nothing to show without them.
    bool ok = true;
    ok &= sprites.Load("data/sprites.json");
    ok &= items.Load("data/items.json");
    // Written by tools/import_assets.ps1 when the armour icon packs are
    // present. Absent is normal, not an error.
    items.Load("data/items_armour.json", false);
    ok &= items.LoadTiers("data/tiers.json");
    ok &= items.LoadEnchantments("data/enchantments.json");
    ok &= enemy_db.Load("data/enemies.json");
    ok &= loot.Load("data/loot_tables.json");
    loot.Load("data/loot_tables_armour.json", false);   // optional armour drops
    ok &= own_quests.LoadDefinitions("data/quests.json");
    ok &= quests_two.LoadDefinitions("data/quests.json");
    ok &= dialogue_db.Load("data/dialogue.json");
    ok &= projectile_db.Load("data/projectiles.json");
    ok &= status_db.Load("data/statuses.json");
    ok &= spells.Load("data/spells.json");
    ok &= skill_trees.Load("data/skill_trees.json");
    ok &= shop_db.Load("data/shops.json");
    // The world map's marks; the picture itself is baked the first time it is
    // opened, from maps/overworld.mx.
    ok &= world_map.Load("data/worldmap.json", shop_db);
    // Not `ok &=`: without it quests are simply not pointed at.
    waypoints.Load("data/waypoints.json");

    if (!ok) {
        SDL_Log("DreamQuest: one or more data files failed to load. "
                "Run tools/import_assets.ps1 (or import_assets.sh) first.");
    }
    return ok;
}

void Game::ApplyBindings() {
    Bindings b;
    b.FromJson(settings.controls);
    input.SetBindings(b);
    // Player Two's controller is a controller: the same buttons do the same things.
    input_two.SetBindings(b);
}

void Game::ApplySettings() {
    input.SetMode(static_cast<InputMode>(settings.input_mode));
    ApplyBindings();
    world->camera.SetZoom(settings.zoom);
    SDL_SetWindowFullscreen(window, settings.fullscreen);
    SDL_SetRenderVSync(renderer, settings.vsync ? 1 : 0);
    Audio::SetVolumes(settings.master_volume, settings.sfx_volume, settings.ambience_volume);
}

// -----------------------------------------------------------------------------
//  Session lifecycle
// -----------------------------------------------------------------------------

void Game::NewGame(const string& character, int slot) {
    banner_active = false;
    banner_time = 0.0f;
    banner_zone.clear();
    banner_seen_map.clear();
    quests->FromJson(json::object());
    // Everything the last game left in the world: see World::StartAfresh.
    world->StartAfresh();
    world->SetCamp({});
    world->SetDream({});
    world->player = Player();
    world->player.Init(ctx, character);

    // Starting kit: a few coins, a bit of food, the wood tier's weapon of the
    // character's affinity -- a sword for the hero, a bow for the warden, a
    // staff for the wayfarer -- and something to put between yourself and the
    // first boar. Everything else -- the rest of a set, the tools to work the
    // land, a bedroll -- is bought, found or made. Every character used to
    // start with the sword, which sent the warden and the wayfarer into their
    // first fight with the one weapon their affinity does nothing for.
    //
    // The armour is not generosity -- the hero's cuirass and shield, the
    // warden's rawhide, the wayfarer's homespun and shield. Accuracy here is
    // (level + 8) x (bonus + 64) on both sides, so at level 1 the bonus from
    // gear is most of the number: with nothing worn a boar hits a new
    // character 60% of the time and an orc 65%, while they hit back at about
    // 42%. Twenty-six points of defence bonus brings that to 45% and 48%, and
    // the opening hour stops feeling arranged against you.
    const vector<string> kit = Player::StartingKit(character);
    world->player.inventory.Add("coins", 25);
    for (const string& id : kit) world->player.inventory.Add(id, 1);
    world->player.inventory.Add("cooked_meat", 3);
    // Marked, so loading this character never hands them the tools a character
    // from before gathering needed tools is given.
    world->SetFlag("starter_tools");

    // Worn straight away: a new player should not have to find the bag screen
    // before the first fight to benefit from what they were given.
    string why;
    for (const string& worn : kit)
        for (int slot = 0; slot < world->player.inventory.SlotCount(); ++slot)
            if (world->player.inventory.Slot(slot).id == worn)
                world->player.EquipFromInventory(slot, why);

    active_slot = slot;
    playtime = 0.0f;
    autosave_timer = 0.0f;

    if (!world->LoadMap("overworld", "start", ctx)) {
        SDL_Log("DreamQuest: could not load the starting map");
        SetState(GameState::MainMenu);
        return;
    }

    has_session = true;
    quests->SetDay(world->clock.QuestDay());
    world->shops.SetDay(world->clock.QuestDay());
    quest_day_seen = world->clock.QuestDay();
    SetState(GameState::Play);
    PushToast("A new journey begins.", Palette::Highlight);
    welcome_pending = true;
}

bool Game::LoadGame(int slot) {
    banner_active = false;
    banner_time = 0.0f;
    banner_zone.clear();
    banner_seen_map.clear();
    bool from_backup = false;
    if (!SaveSystem::Load(slot, (*world), (*quests), ctx, playtime, &from_backup)) {
        PushToast("That save could not be loaded.", {235, 120, 120, 255});
        return false;
    }
    if (from_backup)
        PushToast("Slot " + std::to_string(slot) + " could not be read. This is the save before it.",
                  {235, 200, 120, 255});
    active_slot = slot;
    autosave_timer = 0.0f;
    has_session = true;
    quests->SetDay(world->clock.QuestDay());
    world->shops.SetDay(world->clock.QuestDay());
    quest_day_seen = world->clock.QuestDay();
    SetState(GameState::Play);
    PushToast("Welcome back.", Palette::Highlight);

    // A character from before gathering needed tools has none, and could not
    // chop the logs to make an axe with. Hand them the basic set, once.
    if (!world->Flagged("starter_tools")) {
        world->SetFlag("starter_tools");
        Inventory& bag = world->player.inventory;
        bool given = false;
        for (const char* kind : {"axe", "pickaxe", "rod"})
            if (!Gathering::BestTool(bag, world->player.equipment, items, world->player.skills, kind)) {
                const char* id = string(kind) == "axe" ? "bronze_axe" : string(kind) == "pickaxe" ? "bronze_pickaxe" : "fishing_rod";
                if (bag.Add(id, 1) > 0) given = true;
            }
        if (given) PushToast("Your pack has the tools for chopping, mining and fishing now.", Palette::Xp);
    }
    return true;
}

void Game::SaveOnTheWayOut() {
    // Closing the window used to cost whatever had happened since the last
    // autosave -- up to two minutes -- where quitting from the pause menu cost
    // nothing. A guest's character is kept by the destructor; a scratch game
    // is nobody's to keep; and somebody lying dead is not written down dead:
    // the last autosave stands for them.
    if (!has_session || never_save || guest_session) return;
    if (home_world.player.IsDead()) return;
    if (WriteSlot(active_slot)) SDL_Log("DreamQuest: saved slot %d on the way out", active_slot);
}

bool Game::SaveGame(int slot) {
    if (!has_session) return false;
    if (never_save) return false;
    if (guest_session) {
        SaveGuestCharacter();
        PushToast("Your character is saved. The world is the host's to keep.", Palette::Xp);
        return true;
    }
    if (WriteSlot(slot)) {
        active_slot = slot;
        PushToast("Game saved to slot " + std::to_string(slot) + ".", Palette::Xp);
        return true;
    }
    PushToast("Could not write the save file.", {235, 120, 120, 255});
    return false;
}

// -----------------------------------------------------------------------------
//  State plumbing
// -----------------------------------------------------------------------------

void Game::SetState(GameState s) {
    // Backing out of a screen the main menu opened lands on the row that
    // opened it. Resetting to the top meant leaving Options put the cursor
    // on Continue, one Enter away from loading a game.
    if (state == GameState::MainMenu) main_menu_cursor = cursor;
    // The title screen has its own quiet wind; a session hands over to the map.
    if (s == GameState::MainMenu && !has_session) Audio::SetAmbience("menu", false);
    const bool back_to_menu = (s == GameState::MainMenu) &&
        (state == GameState::Options || state == GameState::Controls || state == GameState::LoadMenu ||
         state == GameState::CharacterSelect || state == GameState::Multiplayer);

    state = s;
    state_time = 0.0f;
    cursor = back_to_menu ? main_menu_cursor : 0;
    // The player only steers during actual gameplay.
    world->player.input_locked = (s != GameState::Play);
}

void Game::OpenPanel(GameState panel) {
    return_state = state;
    SetState(panel);
}

void Game::ClosePanel() {
    SetState(return_state);
}

bool Game::InGameplayState() const {
    switch (state) {
        case GameState::Play:
        case GameState::Paused:
        case GameState::Hub:
        case GameState::Inventory:
        case GameState::SkillsPanel:
        case GameState::QuestPanel:
        case GameState::WorldMapPage:
        case GameState::Dialogue:
        case GameState::Board:
        case GameState::Note:
        case GameState::SleepPrompt:
        case GameState::Travel:
        case GameState::TotemRing:
        case GameState::Crafting:
        case GameState::Enchanting:
        case GameState::Shop:
        case GameState::Storage:
        case GameState::Death:
            return true;
        default:
            return false;
    }
}

void Game::PushToast(const string& text, SDL_Color color) {
    Toast t;
    t.text = text;
    t.color = color;
    t.life = t.max_life = 3.2f;
    toasts.push_back(t);
    // Keep the stack short so it never covers the play area.
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}

vector<string> Game::KnownElectric() const {
    vector<string> out;
    for (const SpellDef* s : spells.Electric(world->player.skills.Level(SKILL_MAGIC))) out.push_back(s->id);
    return out;
}

void Game::MoveCursor(int& c, int count, bool wrap) {
    if (count <= 0) { c = 0; return; }
    int delta = 0;
    if (input.MenuDown()) ++delta;
    if (input.MenuUp())   --delta;
    if (delta == 0) return;

    const int before = c;
    if (wrap) c = ((c + delta) % count + count) % count;
    else      c = std::clamp(c + delta, 0, count - 1);
    if (c != before) Audio::Play(Sfx::UiMove);
}

// -----------------------------------------------------------------------------
//  Main loop
// -----------------------------------------------------------------------------

void Game::Loop() {
    Uint64 previous = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>((now - previous) / freq);
        previous = now;

        // A long stall (dragging the window, a breakpoint) must not teleport
        // anyone through a wall.
        if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
        fps = (dt > 0.0f) ? (fps * 0.9f + (1.0f / dt) * 0.1f) : fps;

        Process(dt);
        Update(dt);
        Render();

        if (!settings.vsync) SDL_Delay(1);
    }
}

void Game::Process(float dt) {
    // Clear last frame's press/release edges and advance the hold timers
    // BEFORE new events arrive, so an edge set while polling survives into
    // this frame's Update. Doing it afterwards would wipe every press.
    input.Update(dt);
    input_two.Update(dt);
    for (HeldAction& h : launch_holds_two) {
        const bool want = run_time >= h.from && run_time < h.to;
        if ((want && h.sent == 0) || (!want && h.sent == 1)) {
            input_two.Inject(h.action, want);
            h.sent = want ? 1 : 2;
        }
    }

    // --hold: a key goes down and comes up when the clock says so.
    for (HeldKey& h : launch_holds) {
        const bool want = run_time >= h.from && run_time < h.to;
        if ((want && h.sent == 0) || (!want && h.sent == 1)) {
            SDL_Event e{};
            e.type = want ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = h.key;
            input.HandleEvent(e);
            h.sent = want ? 1 : 2;
        }
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                SaveOnTheWayOut();
                running = false;
                return;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    SaveOnTheWayOut();
                    running = false;
                    return;
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                screen_w = event.window.data1;
                screen_h = event.window.data2;
                ui.SetViewport(static_cast<float>(screen_w), static_cast<float>(screen_h));
                LayoutViews();
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                // Start, on a controller that is nobody's yet: Player Two sits down.
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START && !split_active && state == GameState::Play &&
                    has_session && !guest_session && Input::ConnectedPads() >= 2 &&
                    event.gbutton.which != input.PadId()) {
                    JoinSplit();
                    break;
                }
                input.HandleEvent(event);
                input_two.HandleEvent(event);
                break;

            default:
                // A field being typed into takes its keys before the input
                // map can read them as actions.
                if (!TextEntryEvent(event)) { input.HandleEvent(event); input_two.HandleEvent(event); }
                break;
        }
    }
}

void Game::Update(float dt) {
    // A guest steps its world with the dt it will send, to the microsecond, so
    // the host steps its copy of their character with the very same number.
    if (guest_session) dt = coop::QuantiseDt(dt);
    state_time += dt;
    run_time += dt;
    // The realm is stepped with nobody being acted as: a panel of Player
    // Two's lets go of them for the moment, and takes them back.
    const int panel_seat = (state != GameState::Play) ? serving : 0;
    ServeSeat(0);
    UpdateSession(dt);
    UpdateCoop(dt);
    ServeSeat(panel_seat);
    if (!launch_say.empty() && session.Me().Seated()) {
        session.Me().Say(launch_say);
        launch_say.clear();
    }
    for (auto& t : toasts) t.life -= dt;
    // What is earned at a bench or a fire is shown as it is earned, not when
    // the panel is closed: the chores that collect these run only while the
    // world does, and a panel stops it.
    if (has_session && !split_active && state != GameState::Play && InGameplayState())
        for (const auto& drop : world->player.TakeXpDrops()) NoteXp(drop.first, drop.second);
    for (XpLine& line : xp_lines) line.age += dt;
    xp_lines.erase(std::remove_if(xp_lines.begin(), xp_lines.end(),
                                  [](const XpLine& l) { return l.age >= 2.8f; }), xp_lines.end());
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [](const Toast& t) { return t.life <= 0.0f; }),
                 toasts.end());

    // Menu feedback in one place rather than in every screen's handler: a
    // confirm or back press on any screen that is not the game itself.
    const GameState state_before = state;
    if (state != GameState::Play && state != GameState::Death) {
        if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact))
            Audio::Play(Sfx::UiConfirm);
        else if (input.Pressed(Action::Back))
            Audio::Play(Sfx::UiBack);
    }

    switch (state) {
        case GameState::MainMenu:        UpdateMainMenu(); break;
        case GameState::CharacterSelect: UpdateCharacterSelect(); break;
        case GameState::SlotSelect:      UpdateSlotSelect(); break;
        case GameState::LoadMenu:        UpdateLoadMenu(); break;
        case GameState::Options:         UpdateOptions(); break;
        case GameState::Controls:        UpdateControls(); break;
        case GameState::Multiplayer:     UpdateMultiplayer(); break;
        case GameState::Play:            UpdatePlay(dt); break;
        case GameState::Paused:          UpdatePaused(); break;
        case GameState::Inventory:       UpdateInventory(); break;
        case GameState::SkillsPanel:     UpdateSkillsPanel(); break;
        case GameState::Hub:             UpdateHub(); break;
        case GameState::QuestPanel:      UpdateQuestPanel(); break;
        case GameState::WorldMapPage:    UpdateWorldMap(); break;
        case GameState::Dialogue:        UpdateDialogue(dt); break;
        case GameState::Board:           UpdateBoard(); break;
        case GameState::Note:            UpdateNote(); break;
        case GameState::SleepPrompt:     UpdateSleepPrompt(); break;
        case GameState::Travel:          UpdateTravel(); break;
        case GameState::TotemRing:       UpdateTotemRing(); break;
        case GameState::Crafting:        UpdateCrafting(); break;
        case GameState::Enchanting:      UpdateEnchanting(); break;
        case GameState::Shop:            UpdateShop(); break;
        case GameState::Storage:         UpdateStorage(); break;
        case GameState::Death:           UpdateDeath(dt); break;
    }

    // Opening a panel from the game clicks too.
    if (state_before == GameState::Play && state != GameState::Play &&
        state != GameState::Death)
        Audio::Play(Sfx::UiConfirm, 0.8f);

    // Walking into a new zone puts its name on screen, the way DragonFable
    // and AdventureQuest Worlds do when you cross into somewhere new. Houses
    // do not announce themselves; dungeon levels, which are their own places,
    // do.
    if (has_session && InGameplayState() && world->CurrentMap().Loaded() &&
        world->MapId() != banner_seen_map) {
        banner_seen_map = world->MapId();
        const Map& m = world->CurrentMap();
        const bool zone = !m.IsInterior() || m.Ambient() == "dungeon";
        if (!zone) banner_active = false;
        if (zone && m.Id() != banner_zone) {
            banner_zone     = m.Id();
            banner_title    = m.DisplayName();
            banner_subtitle = m.Subtitle();
            banner_time     = 0.0f;
            banner_active   = true;
        }
    }
    if (banner_active) {
        banner_time += dt;
        if (banner_time > 4.2f) banner_active = false;
    }

    // The birds go quiet as it gets dark, and the crickets start.
    if (has_session && InGameplayState()) {
        const float night = (world->InDream() || world->CurrentMap().IsInterior())
                                ? 0.0f : world->clock.Darkness();
        if (fabsf(night - audio_night) > 0.02f) {
            audio_night = night;
            Audio::SetNight(night);
        }
    }

    // Panels pause the world but still show it behind them, so keep the
    // camera settled and let floating text finish.
    if (has_session && state != GameState::Play && InGameplayState())
        world->camera.Follow(world->player.x + world->player.LookAhead().x,
                            world->player.y + world->player.LookAhead().y, dt);
}

void Game::UpdatePlay(float dt) {
    ServeSeat(0);
    // The first thing a new character sees: a note of welcome, on the
    // parchment a sign is read on, saying where they are and what the keys
    // do. Once, and only on a new game -- a load puts the player back mid-story.
    if (welcome_pending) {
        welcome_pending = false;
        const string J = input.PromptFor(Action::LightAttack), K = input.PromptFor(Action::StrongAttack);
        note_title = "Welcome to the Hollowmarch";
        note_text  = "You stand on the road above Havenbrook. The town is south through the gate, "
                     "and the guild hall there has work for anyone who asks; Elder Maren's letter is the "
                     "first of it. North, the road runs to the Emberfell mine. East, the trail goes under "
                     "the trees to Mossvale.\n\n" +
                     J + " swings. " + K + " strikes hard, and held, charges. Mix the two for combos. "
                     "Hold " + input.PromptFor(Action::Block) + " behind a shield. " +
                     input.PromptFor(Action::Interact) + " talks, opens and works. " +
                     input.PromptFor(Action::Inventory) + " is your pack, " +
                     input.PromptFor(Action::Skills) + " your skills, " +
                     input.PromptFor(Action::QuestLog) + " your journal, " +
                     input.PromptFor(Action::WorldMap) + " the map.\n\n"
                     "Rest at an inn or a camp after dusk: sleep the night through, or let it take you "
                     "somewhere else. "
                     "Go carefully, and go far.";
        note_quest.clear();
        OpenPanel(GameState::Note);
        return;
    }
    playtime += dt;

    // The boards post new dailies when the quest day turns over, at dawn, and
    // the traders restock.
    quests->SetDay(world->clock.QuestDay());
    world->shops.SetDay(world->clock.QuestDay());
    if (quest_day_seen >= 0 && world->clock.QuestDay() > quest_day_seen)
        PushToast("New notices are up, and the traders have restocked.", Palette::Xp);
    quest_day_seen = world->clock.QuestDay();

    // As a guest, the hands are read here, quantised, and sent as they were
    // used. Alone or hosting, the world reads the device itself.
    if (guest_session) coop_guest.BeforeStep((*world), &input);
    else               world->player.hands_external = false;
    world->Update(dt, ctx);
    if (guest_session) coop_guest.AfterStep((*world), session.Me(), dt);
    HandleWorldRequests();

    switch (world->TakeWake()) {
        case World::WakeReason::Dawn:
            PushToast("You wake at dawn, rested.", Palette::Highlight);
            break;
        case World::WakeReason::Nightmare:
            PushToast("The nightmare jolted you awake. The night is gone.", {220, 170, 240, 255});
            break;
        case World::WakeReason::Stone:
            PushToast("You wake before dawn, rested.", Palette::Highlight);
            break;
        case World::WakeReason::Slept:
            PushToast("You slept the night through, and wake rested.", Palette::Highlight);
            break;
        default: break;
    }

    SeatChores();
    UpdatePlayerTwo(dt);

    // --- autosave ------------------------------------------------------------
    // As a guest it is the character that is kept, on this machine; the world
    // is the host's to keep.
    if (state != GameState::Play) return;       // a panel has opened: not mid-change
    if (!never_save) autosave_timer += dt;
    if (guest_session && autosave_timer >= AUTOSAVE_INTERVAL) {
        autosave_timer = 0.0f;
        SaveGuestCharacter();
    }
    if (autosave_timer >= AUTOSAVE_INTERVAL) {
        autosave_timer = 0.0f;
        if (WriteSlot(active_slot)) PushToast("Autosaved.", Palette::TextDim);
    }
}

bool Game::WriteSlot(int slot) {
    // The slot is Player One's and the home world's, whoever pressed save: a
    // panel of Player Two's has `world` and `quests` pointing at theirs.
    const int was = serving;
    ServeSeat(0);
    const bool ok = SaveSystem::Save(slot, home_world, own_quests, playtime);
    SavePlayerTwo();
    ServeSeat(was);
    return ok;
}

void Game::SeatChores() {
    // Collect objectives follow the bag, and the bag changes in more places
    // than are worth chasing individually -- eating, delivering, accepting a
    // quest for something already carried. Only a handful of quests are ever
    // active, so settling them every frame costs nothing.
    quests->RefreshCollectObjectives(world->player.inventory);

    // Progression feedback raised by the player during the update.
    const vector<LevelUp> ups = world->player.TakeLevelUps();
    if (!ups.empty()) Audio::Play(Sfx::LevelUp);
    for (const LevelUp& up : ups) {
        PushToast(string(SkillName(up.skill)) + " level " + std::to_string(up.level) + "!",
                  Palette::Highlight);
        // Every fourth level of the skill their tree grows from is a point to
        // spend in it. The other two trees are other characters'.
        for (int t = 0; t < 3; ++t) {
            const TalentTree& tree = skill_trees.Tree(static_cast<AttackStyle>(t));
            if (!world->player.talents.Open(static_cast<AttackStyle>(t))) continue;
            if (tree.skill == up.skill && up.level % SkillTrees::LEVELS_PER_POINT == 0)
                PushToast(tree.name + " skill point  -  " + input.PromptFor(Action::Skills) +
                          " to spend it", Palette::Xp);
        }
    }
    // Shown, where they used to be taken and dropped on the floor.
    for (const auto& drop : world->player.TakeXpDrops()) NoteXp(drop.first, drop.second);

    // Quests that finished this frame pay out now.
    for (const string& id : quests->TakeJustStarted())
        if (const QuestDef* d = quests->Definition(id)) {
            PushToast("Quest started: " + d->name, Palette::Xp);
            Audio::Play(Sfx::QuestStart);
        }

    for (const string& id : quests->TakeJustCompleted())
        GrantQuestRewards(id);

    // Dying in a dream only wakes you; the world handles that.
    if (world->player.IsDead() && world->player.DeathTimer() <= 0.0f && !world->InDream())
        SetState(GameState::Death);

    // --- spell selection -----------------------------------------------------
    // The element is chosen, not the spell: Magic level decides which tier of
    // that element actually comes out.
    // With an element's own staff in hand the same four keys are that
    // element's four spells: see ItemDef::element.
    {
        static const Action kKeys[4] = {Action::SelectFire, Action::SelectWater, Action::SelectEarth, Action::SelectAir};
        static const Element kElements[4] = {Element::Fire, Element::Water, Element::Earth, Element::Air};
        Player& me = world->player;
        for (int i = 0; i < 4; ++i) {
            if (!input.Pressed(kKeys[i])) continue;
            if (me.StaffElement() != Element::None) me.SelectSlot(i);
            else                                    me.SelectElement(kElements[i]);
        }
    }
    // Lightning on the fifth key, and the fifth key again for the next of it:
    // five spells and no staff of its own to put them on the number row.
    if (input.Pressed(Action::SelectElectric)) {
        const vector<string> known = KnownElectric();
        if (known.empty()) {
            const SpellDef* next = spells.Electric(MAX_SKILL_LEVEL).empty() ? nullptr : spells.Electric(MAX_SKILL_LEVEL).front();
            PushToast(next ? "Magic " + std::to_string(next->level) + " for " + next->name + ", the first of the lightning."
                           : "No lightning known.", Palette::TextDim);
        } else {
            world->player.SelectElectric(known);
            if (const SpellDef* s = spells.Get(world->player.ElectricSpell()))
                PushToast(s->name + (known.size() > 1 ? "   " + input.PromptFor(Action::SelectElectric) + " again: next" : ""),
                          ElementColor(Element::Electric));
        }
    }
    if (input.Pressed(Action::SelectArcane)) {
        const vector<string> known = world->KnownArcane(spells);
        if (known.empty()) PushToast("You know no ancient magic yet. The college in Fernhollow teaches it.", Palette::TextDim);
        else world->player.SelectArcane(known);
    }
    // With what is known of the ancient magic, so that the sixth slot is in
    // the round: see Player::CycleElement.
    if (input.Pressed(Action::CycleSpell)) {
        // The lightning is in the round only once some of it is reached, and
        // its spell has to be set for CycleElement to know that.
        if (world->player.ElectricSpell().empty())
            if (const vector<string> known = KnownElectric(); !known.empty())
                world->player.SetElectricSpell(known.front());
        world->player.CycleElement(1, world->KnownArcane(spells));
    }

    // --- what is to hand ------------------------------------------------------
    // Guard and interact eats or drinks the quick item; guard and sprint steps
    // to the next. A chord, the way the abilities are, because a pad has no
    // button left -- and because it is the one way to eat that does not open
    // the bag, which online does not stop the world for you.
    bool chorded = false;
    if (input.ShiftDown()) {
        Player& me = world->player;
        if (input.Pressed(Action::Interact)) {
            chorded = true;
            string why;
            const string id = me.QuickItem().empty() && !me.QuickChoices().empty() ? me.QuickChoices().front()
                                                                                   : me.QuickItem();
            const ItemDef* d = items.Get(id);
            if (me.UseQuickItem(why)) {
                const bool potion = d && std::find(d->tags.begin(), d->tags.end(), "potion") != d->tags.end();
                PushToast((potion ? "You drink the " : "You eat the ") + (d ? d->name : id) + ".", Palette::Xp);
                Audio::Play(Sfx::Eat);
            } else {
                PushToast(why, Palette::TextDim);
                Audio::Play(Sfx::UiError);
            }
        } else if (input.Pressed(Action::Sprint)) {
            chorded = true;
            const string id = me.CycleQuickItem();
            if (const ItemDef* d = items.Get(id))
                PushToast(d->name + " is to hand  (x" + std::to_string(me.inventory.Count(id)) + ")", Palette::Text);
            else
                PushToast("You have nothing to eat or drink.", Palette::TextDim);
            Audio::Play(Sfx::UiMove);
        }
    }

    // --- panel hotkeys -------------------------------------------------------
    if (!chorded && input.Pressed(Action::Interact)) world->TryInteract(ctx);
    if (input.Pressed(Action::Menu))       { hub_cursor = 0; OpenPanel(GameState::Hub); }
    if (input.Pressed(Action::Inventory))  OpenPanel(GameState::Inventory);
    if (input.Pressed(Action::Skills))     OpenPanel(GameState::SkillsPanel);
    if (input.Pressed(Action::QuestLog))   OpenPanel(GameState::QuestPanel);
    if (input.Pressed(Action::WorldMap))   OpenPanel(GameState::WorldMapPage);
    if (input.Pressed(Action::Pause))      OpenPanel(GameState::Paused);
}

void Game::HandleWorldRequests() {
    for (const WorldRequest& r : world->TakeRequests()) {
        switch (r.type) {
            case WorldRequest::Type::Dialogue: {
                // Freeze the NPC being spoken to.
                for (auto& n : world->npcs)
                    if (n->Id() == r.id) n->talking = true;
                dialogue.Begin(&dialogue_db, r.text, r.id, r.title, MakeDialogueContext());
                HandleDialogueActions(dialogue.TakeActions());
                if (dialogue.Active()) OpenPanel(GameState::Dialogue);
                else                   for (auto& n : world->npcs) n->talking = false;
                break;
            }
            case WorldRequest::Type::Board:
                board_orders = false;
                board_title  = r.title;
                board_quests = r.list;
                // Anything whose giver is this board is pinned to it too:
                // that is how the rotating dailies get posted.
                for (const auto& kv : quests->Definitions())
                    if (kv.second.giver == r.id &&
                        std::find(board_quests.begin(), board_quests.end(), kv.first) == board_quests.end())
                        board_quests.push_back(kv.first);
                board_cursor = 0;
                OpenPanel(GameState::Board);
                break;

            case WorldRequest::Type::Note:
                note_title = r.title;
                note_text  = r.text;
                note_quest = r.list.empty() ? "" : r.list.front();
                OpenPanel(GameState::Note);
                break;

            case WorldRequest::Type::Craft:
                craft_title   = r.title;
                craft_station = CraftStationFromName(r.text);
                craft_cursor  = 0;
                OpenPanel(GameState::Crafting);
                break;

            case WorldRequest::Type::Travel:
                travel_from = r.id;
                travel_cursor = 0;
                OpenPanel(GameState::Travel);
                break;

            case WorldRequest::Type::Totem:
                totem_cursor = 0;
                OpenPanel(GameState::TotemRing);
                break;

            case WorldRequest::Type::Sleep:
                sleep_title = r.title;
                sleep_fee   = r.count;
                OpenPanel(GameState::SleepPrompt);
                break;

            case WorldRequest::Type::Enchant:
                craft_title    = r.title;
                enchant_cursor = enchant_target = 0;
                OpenPanel(GameState::Enchanting);
                break;

            case WorldRequest::Type::Storage:
                storage_id     = r.id;
                storage_title  = r.title;
                storage_slots  = r.count > 0 ? r.count : 100;
                storage_cursor = storage_bag_cursor = 0;
                storage_on_chest = false;
                OpenPanel(GameState::Storage);
                break;

            case WorldRequest::Type::Toast:
                PushToast(r.text);
                break;

            default:
                break;
        }
    }
}

void Game::HandleDialogueActions(const vector<DialogueAction>& actions) {
    Player& p = world->player;

    for (const DialogueAction& a : actions) {
        // The journal and the bag are the dialogue layer's to change, and it
        // keeps the rule about gifts: see ApplyDialogueAction. What is left
        // here is saying so, and putting what did not fit on the ground.
        const DialogueOutcome done = ApplyDialogueAction(a, *quests, p.inventory, p.skills, dialogue.NpcId());
        for (const auto& got : done.received) {
            const ItemDef* d = items.Get(got.first);
            PushToast("Received " + std::to_string(got.second) + "x " + (d ? d->name : got.first), Palette::Xp);
        }
        for (const auto& spilt : done.overflow) {
            world->DropItem(spilt.first, spilt.second, p.x, p.y + 6.0f, ctx);
            PushToast("Your pack is full. The item is at your feet.", {235, 150, 120, 255});
        }
        for (const string& flag : done.flags) world->SetFlag(flag);

        // Trading waits for the conversation to close; see UpdateDialogue.
        if (!a.open_shop.empty() && shop_db.Get(a.open_shop)) pending_shop = a.open_shop;
        if (!a.open_orders.empty()) pending_orders = a.open_orders;

        if (!a.learn_recipe.empty() && !world->KnowsRecipe(a.learn_recipe)) {
            world->SetFlag("recipe:" + a.learn_recipe);
            // "enchant:<id>" is a charm for the enchanting table rather than
            // a brew; the flag is the same shape either way.
            if (a.learn_recipe.rfind("enchant:", 0) == 0) {
                const EnchantDef* e = items.Enchantment(a.learn_recipe.substr(8));
                PushToast("Enchantment learned: " + (e ? e->name : a.learn_recipe.substr(8)) +
                          ". Work it at an enchanting table.", Palette::Highlight);
            } else if (a.learn_recipe.rfind("spell:", 0) == 0) {
                const SpellDef* sp = spells.Get(a.learn_recipe.substr(6));
                PushToast("Spell learned: " + (sp ? sp->name : a.learn_recipe.substr(6)) + ". Press " +
                          input.PromptFor(Action::SelectArcane) + " with a staff in hand.", Palette::Highlight);
            } else {
                const ItemDef* d = items.Get(a.learn_recipe);
                PushToast("Recipe learned: " + (d ? d->name : a.learn_recipe), Palette::Highlight);
            }
            Audio::Play(Sfx::QuestStart);
        }

        // Every order the bag can fill for this NPC, at once.
        if (a.hand_in) {
            for (const string& id : quests->ReadyToDeliver(dialogue.NpcId(), p.inventory)) {
                const QuestDef* d = quests->Definition(id);
                const QuestStage& st = d->stages[quests->Stage(id)];
                const int need = st.count - quests->Counter(id);
                if (need <= 0 || !p.inventory.Remove(st.target, need)) continue;
                QuestEvent e;
                e.type      = ObjectiveType::Deliver;
                e.target    = st.target;
                e.secondary = dialogue.NpcId();
                e.amount    = need;
                quests->Notify(e, p.inventory);
            }
        }

        if (a.heal) {
            // Mended, not reset: see Skills::RestoreDrained.
            p.skills.RestoreDrained();
            p.SyncHitpoints();
            p.hp = p.max_hp;
            PushToast("You feel restored.", Palette::Xp);
        }
    }

    // Talking to someone is no longer an objective by itself. Opening a
    // conversation used to complete "return to Maren" before the hand-in line
    // was ever chosen, so the page, the totem and the letter were never taken
    // and the line that takes them vanished. Talk stages advance only through
    // an option's explicit "advance" action now.
}

void Game::OpenOrders(const string& npc_id, const string& npc_name) {
    board_orders = true;
    board_title  = npc_name + "'s Orders";
    board_quests.clear();
    for (const auto& kv : quests->Definitions())
        if (kv.second.giver == npc_id && kv.second.daily) board_quests.push_back(kv.first);
    board_cursor = 0;
    SetState(GameState::Board);
}

DialogueContext Game::MakeDialogueContext() const {
    DialogueContext c;
    c.quests    = quests;
    c.inventory = &world->player.inventory;
    c.skills    = &world->player.skills;
    c.flags     = &world->Flags();
    c.night     = world->clock.IsNight();
    return c;
}

void Game::GrantQuestRewards(const string& quest_id) {
    const QuestDef* d = quests->Definition(quest_id);
    if (!d) return;

    Player& p = world->player;

    for (const auto& xp : d->rewards.xp) p.GrantXp(xp.first, xp.second);
    if (d->rewards.coins > 0) p.inventory.AddCoins(d->rewards.coins);

    for (const auto& item : d->rewards.items) {
        const int added = p.inventory.Add(item.first, item.second);
        if (added < item.second) {
            // No room: drop it at the player's feet rather than losing it.
            world->DropItem(item.first, item.second - added, p.x, p.y + 6.0f, ctx);
        }
    }

    PushToast("Quest complete: " + d->name, Palette::Highlight);
    Audio::Play(Sfx::QuestComplete);
    quests->RefreshCollectObjectives(p.inventory);
}

// -----------------------------------------------------------------------------
//  Render dispatch
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  --audit: every menu, at three window sizes, looking for text that runs off
// -----------------------------------------------------------------------------
//
// Opens each panel in turn, draws one frame of it with UI::BeginAudit on, and
// prints whatever was drawn outside the panel it was drawn on or off the edge
// of the window. It is run by hand while working on a menu, and by the
// self-test's screen pass through bin/DreamQuest.exe --audit, so a panel that
// somebody adds a long line to says so rather than waiting to be noticed.
//
// The play HUD goes through it too, for the other thing that goes wrong with a
// screen: two pieces of it in the same place. See UI::Claim.
void Game::RunAudit() {
    // `draw`, for the few that are not a frame of Render(): half a split screen.
    struct Screen { const char* name; GameState state; std::function<void()> open; std::function<void()> draw; };
    // A character with everything, because the longest lines are the ones a
    // finished character sees: every skill at seventy, a full bag, the trees
    // learned, quests in hand.
    Player& p = world->player;
    LevelUp up;
    for (int s2 = 0; s2 < SKILL_COUNT; ++s2) p.skills.AddXp(s2, XpForLevel(70), up);
    p.SyncHitpoints(); p.hp = p.max_hp; p.SyncMana(); p.RestoreMana();
    for (const char* id : {"dragonhide_hide_head", "dragonhide_hide_body", "dragonhide_hide_legs",
                           "bag_haversack", "bag_rucksack", "greatwolf_pelt", "dream_shard", "starlily_panacea",
                           "demonite_greatsword", "wyvern_scale", "herbal_tonic", "dire_bear_hide",
                           // A piece with a passive on it and an enchanted one,
                           // so the line the stat block prints for those is
                           // swept in the shop's sell tab rather than assumed
                           // to fit.
                           "drowned_king_boots", "orichalcum_sword+might", "dragonhide_hide_body+the_wind"})
        if (items.Get(id)) p.inventory.Add(id, 99);
    // And wearing something in every slot, because the stat block in the shop
    // and at the anvil writes "Instead of <what you have on>": the longest
    // line it can draw is the longest item name in the game.
    for (const char* id : {"orichalcum_sword", "orichalcum_shield", "dragonhide_hide_head",
                           "dragonhide_hide_body", "dragonhide_hide_legs"})
        if (const ItemDef* d = items.Get(id)) if (d->slot != SLOT_NONE) p.equipment.Equip(d->slot, id);
    for (const auto& kv : quests->Definitions()) quests->Start(kv.first);
    quests->TakeJustStarted();

    // A toast is drawn over the HUD, not inside a panel, and lands on top of a
    // full-screen one: it is not a panel's text running off.
    toasts.clear();

    // The window's size, the interface's scale. 1280 by 800 is a Steam Deck,
    // and it is here twice: as it comes, and at the 125% that is the most it
    // has room for -- which is every panel laid out in 1024 by 640, close to
    // the smallest the game supports and the likeliest place for a line to run
    // off.
    //
    // This list used to be three sizes and test one. Render() sets the
    // interface's viewport from the window's size on its first line, so
    // whatever was asked for here was thrown away and every pass was the
    // window as it opened: 1280 by 720, three times. The window's size is what
    // is set now, which is what Render() reads.
    struct Pass { float w, h, scale; };
    const Pass sizes[] = {{1280.0f, 720.0f, 1.0f}, {1024.0f, 600.0f, 1.0f}, {1920.0f, 1080.0f, 1.0f},
                          {1280.0f, 800.0f, 1.0f}, {1280.0f, 800.0f, 1.1f}, {1280.0f, 800.0f, 1.25f},
                          {1920.0f, 1080.0f, 1.25f}, {1920.0f, 1080.0f, 1.5f}};
    // The play HUD is not a panel, and what goes wrong on it is not a line
    // running off: it is two things drawn in the same place, which is what
    // UI::Claim is for. It is posed with everything the foot of the screen can
    // hold at once -- a technique chosen, three abilities carried, something to
    // interact with, a chain open -- once with a sword and once with a staff,
    // whose element bar is the tallest thing there and whose line, with the
    // ancient magic chosen and a technique riding on it, is the widest.
    //
    // The chain first: one light attack at nothing, let finish. Nothing in an
    // audit steps the player again, so the window it leaves open stays open.
    p.input_locked = false;
    p.hands = {};
    p.hands.down = p.hands.pressed = PlayerInput::Light;
    p.Update(1.0f / 60.0f, *world, ctx);
    p.hands = {};
    for (int f = 0; f < 240 && !p.ComboOpen(); ++f) p.Update(1.0f / 60.0f, *world, ctx);

    const Talents talents_was = p.talents;
    const Equipment worn_was = p.equipment;
    const std::set<string> flags_was = world->Flags();
    const auto hud_pose = [&](bool staff) {
        has_session = true;
        const AttackStyle path = staff ? AttackStyle::Magic : AttackStyle::Melee;
        const TalentTree& tree = skill_trees.Tree(path);
        p.talents = talents_was;
        p.talents.SetPath(path);
        // A rank of everything the points reach, then the technique with the
        // longest name and the first three abilities.
        for (const TalentNode& n : tree.nodes) p.talents.Learn(n.id, p.skills);
        const TalentNode* longest = nullptr;
        int carried = 0;
        for (const TalentNode& n : tree.nodes) {
            if (!p.talents.Has(n.id)) continue;
            if (!n.technique.empty() && (!longest || n.name.size() > longest->name.size())) longest = &n;
            if (!n.ability.empty() && carried < SkillTrees::ABILITY_SLOTS) { p.talents.CycleAbility(n.id); ++carried; }
        }
        if (longest) p.talents.ToggleTechnique(longest->id);
        p.equipment.Equip(SLOT_WEAPON, staff ? "apprentice_staff" : "orichalcum_sword");
        p.SelectElement(Element::Fire);
        if (staff) {
            for (const SpellDef* s : spells.Arcane()) world->SetFlag("recipe:spell:" + s->id);
            p.SelectArcane(world->KnownArcane(spells));
        }
        p.interact = {};
        p.interact.kind = InteractTarget::Object;
        p.interact.label = "Search the Drowned King's reliquary";
    };
    // Half a split screen, as RenderSplit lays one out: at 100% whatever the
    // interface is set to, without the key hints, and -- side by side -- narrow
    // enough for the middle of the foot of the screen to reach the abilities.
    const auto hud_half = [&](float w, float h) {
        ui.SetScale(1.0f);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        ui.SetViewport(w, h);
        world->camera.SetViewport(w, h);
        split_active = true;
        DrawHud();
        split_active = false;
    };

    int found = 0, met = 0;
    std::set<string> said;
    const int was_w = screen_w, was_h = screen_h;
    const float was_scale = settings.ui_scale;
    // The frame counter is drawn over the corner of the screen, not on a panel:
    // once a panel reaches into that corner it reads as the panel's text
    // running off, which it is not. Like the toasts, it is put away for this.
    const bool was_fps = settings.show_fps;
    settings.show_fps = false;
    for (const Pass& size : sizes) {
        screen_w = static_cast<int>(size.w);
        screen_h = static_cast<int>(size.h);
        settings.ui_scale = size.scale;
        const float eff = UiScale();
        ui.SetViewport(size.w / eff, size.h / eff);
        world->camera.SetViewport(size.w, size.h);
        const Screen screens[] = {
            {"main menu",      GameState::MainMenu,        [&] { has_session = false; }},
            {"character",      GameState::CharacterSelect, [] {}},
            {"load",           GameState::LoadMenu,        [] {}},
            {"options",        GameState::Options,         [] {}},
            {"controls",       GameState::Controls,        [&] { controls_cursor = 6; }},
            {"play together",  GameState::Multiplayer,     [] {}},
            {"pause",          GameState::Paused,          [&] { has_session = true; }},
            {"menu",           GameState::Hub,             [&] { hub_cursor = 0; }},
            {"inventory",      GameState::Inventory,       [&] { inventory_cursor = 0; }},
            {"skills",         GameState::SkillsPanel,     [&] { skills_tab = TAB_SKILLS; cursor = SKILL_ATTACK;
                                                                 milestones_for = -1; on_milestones = false; }},
            // The milestone column, on the two skills whose lists are longest
            // and whose lines are widest -- every spell and enchantment under
            // Magic, every recipe of every station under Crafting -- with the
            // cursor in it, which is when it draws its brightest and says what
            // a level is still owed.
            {"skills milestones", GameState::SkillsPanel,  [&] { skills_tab = TAB_SKILLS; cursor = SKILL_MAGIC;
                                                                 milestones_for = -1; on_milestones = true; }},
            {"skills recipes",  GameState::SkillsPanel,    [&] { skills_tab = TAB_SKILLS; cursor = SKILL_CRAFTING;
                                                                 milestones_for = -1; on_milestones = true; }},
            {"skill tree",     GameState::SkillsPanel,     [&] { skills_tab = TAB_TREE; tree_branch = 0; tree_row = 2; }},
            // With the whole tree learned and every ancient spell known: every
            // row has its longest choice somewhere along it.
            {"spellbook",      GameState::SkillsPanel,     [&] {
                skills_tab = TAB_BOOK;
                const AttackStyle path = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
                for (int pass = 0; pass < 4; ++pass)
                    for (const TalentNode& n : skill_trees.Tree(path).nodes) p.talents.Learn(n.id, p.skills);
                for (const SpellDef* s : spells.Arcane()) world->SetFlag("recipe:spell:" + s->id);
            }},
            // With every boss in the game brought down: the fullest the page can be.
            {"boons",          GameState::SkillsPanel,     [&] {
                skills_tab = TAB_BOONS;
                for (const char* boss : {"broodmother", "lizardman_chief", "barrow_wight", "orc3", "well_warden", "den_mother",
                                         "nightmare_troll", "wyvern_matriarch", "pit_lord", "frost_dragon", "nightmare_dragon"})
                    world->player.talents.SlayBoss(boss, rng);
            }},
            {"journal",        GameState::QuestPanel,      [&] { quest_tab = 0; quest_cursor[0] = 0; }},
            {"journal side",   GameState::QuestPanel,      [&] { quest_tab = 2; quest_cursor[2] = 0; }},
            {"map",            GameState::WorldMapPage,    [&] { map_overview = false; }},
            {"map overview",   GameState::WorldMapPage,    [&] { map_overview = true; }},
            {"crafting",       GameState::Crafting,        [&] { craft_title = "Workbench"; craft_station = CraftStation::Workbench; craft_cursor = 0; }},
            {"smithing",       GameState::Crafting,        [&] { craft_title = "Anvil"; craft_station = CraftStation::Anvil; craft_cursor = 0; }},
            {"brewing",        GameState::Crafting,        [&] { craft_title = "Cauldron"; craft_station = CraftStation::Cauldron; craft_cursor = 0; }},
            {"cooking",        GameState::Crafting,        [&] { craft_title = "Cooking fire"; craft_station = CraftStation::Range; craft_cursor = 0; }},
            {"weaving",        GameState::Crafting,        [&] { craft_title = "Loom"; craft_station = CraftStation::Loom; craft_cursor = 0; }},
            {"tanning",        GameState::Crafting,        [&] { craft_title = "Tanning Rack"; craft_station = CraftStation::Rack; craft_cursor = 0; }},
            {"enchanting",     GameState::Enchanting,      [&] { craft_title = "Enchanting table"; enchant_cursor = 0; }},
            {"storage",        GameState::Storage,         [&] { storage_id = "audit"; storage_title = "Storage chest"; storage_slots = 100; }},
            {"shop",           GameState::Shop,            [&] { shop_id = "havenbrook_general"; shop_tab = 0; shop_cursor = 0; }},
            {"shop sell",      GameState::Shop,            [&] { shop_id = "havenbrook_general"; shop_tab = 1; shop_cursor = 0; }},
            // A shelf with gear on it, so the stat block under a shop's
            // description is swept too: the general store sells rope.
            {"shop gear",      GameState::Shop,            [&] { shop_id = "havenbrook_forge"; shop_tab = 0; shop_cursor = 0; }},
            {"board",          GameState::Board,           [&] { board_orders = false; board_title = "Notice board"; board_cursor = 0;
                                                                 board_quests.clear();
                                                                 for (const auto& kv : quests->Definitions()) board_quests.push_back(kv.first); }},
            {"note",           GameState::Note,            [&] { note_title = "A torn page"; note_quest.clear();
                                                                 note_text = string(40, 'M') + "\n\n" + string(400, 'a') + " and a very long unbroken word: " + string(60, 'q'); }},
            {"sleep",          GameState::SleepPrompt,     [&] { sleep_title = "A bed at the Barley and Bell"; sleep_cursor = 0; }},
            {"travel",         GameState::Travel,          [&] { travel_from = "waystone_havenbrook"; travel_cursor = 0; }},
            // With every totem there is in the bag, and one of them in the ring.
            {"totem ring",     GameState::TotemRing,       [&] {
                totem_cursor = 0;
                for (const TotemDef& t : skill_trees.Totems())
                    if (!world->player.inventory.Has(t.item) && world->player.talents.PlacedTotem() != t.item)
                        world->player.inventory.Add(t.item, 1);
                if (world->player.talents.PlacedTotem().empty() && !skill_trees.Totems().empty()) {
                    const string first = skill_trees.Totems().front().item;
                    world->player.inventory.Remove(first, 1);
                    world->player.talents.PlaceTotem(first, world->clock.QuestDay());
                }
            }},
            {"death",          GameState::Death,           [] {}},
            // Last, because they dress the character for it; it is put back
            // as it was under the loop.
            {"hud",            GameState::Play,            [&] { hud_pose(false); }},
            {"hud staff",      GameState::Play,            [&] { hud_pose(true); }},
            {"hud side",       GameState::Play,            [&] { hud_pose(true); }, [&] { hud_half(floorf(size.w / 2.0f) - 1.0f, size.h); }},
            {"hud stacked",    GameState::Play,            [&] { hud_pose(true); }, [&] { hud_half(size.w, floorf(size.h / 2.0f) - 1.0f); }},
        };
        for (const Screen& sc : screens) {
            sc.open();
            state = sc.state;
            state_time = 1.0f;
            // One frame for every row a cursor can be on: what a panel writes is
            // mostly about whatever is selected, and the longest line in the game
            // is somewhere down a list nobody scrolled to.
            int& cursor_row = cursor;
            int* target = nullptr;
            int steps = 1;
            const string name = sc.name;
            if (name == "inventory")      { target = &inventory_cursor; steps = p.inventory.SlotCount(); }
            else if (name == "skills")    { target = &cursor_row; steps = SKILL_COUNT; }
            else if (name == "menu")      { target = &hub_cursor; steps = 5; }
            else if (name == "skill tree") { target = &tree_row; steps = SkillTrees::ROWS; }
            // Nine rows, and as many choices as the longest of them has: the
            // ancient magic's, with every spell of it known.
            else if (name == "spellbook") { target = &book_row; steps = 9 * (static_cast<int>(spells.Arcane().size()) + 1); }
            else if (name == "journal" || name == "journal side") { target = &quest_cursor[quest_tab]; steps = 40; }
            // Every recipe, not the first forty: the anvil alone makes one of
            // every piece in every tier, and the longest lines -- "Instead of
            // Orichalcum Greatsword" -- are all down the far end of it.
            else if (name == "crafting" || name == "smithing" || name == "brewing" ||
                     name == "cooking" || name == "weaving" || name == "tanning") { target = &craft_cursor; steps = 130; }
            else if (name == "enchanting") { target = &enchant_cursor; steps = 12; }
            else if (name == "shop" || name == "shop sell" || name == "shop gear") { target = &shop_cursor; steps = 30; }
            else if (name == "board")     { target = &board_cursor; steps = 30; }
            else if (name == "travel")    { target = &travel_cursor; steps = 3; }
            else if (name == "totem ring") { target = &totem_cursor; steps = 12; }
            else if (name == "controls")  { target = &controls_cursor; steps = 26; }
            else if (name == "options")   { target = &cursor_row; steps = 12; }
            else if (name == "character") { target = &cursor_row; steps = 3; }
            else if (name == "storage")   { target = &storage_bag_cursor; steps = p.inventory.SlotCount(); }

            for (int step = 0; step < steps; ++step) {
                if (target) *target = step;
                // Every branch of a tree, too, and both of its tabs.
                if (name == "skill tree") tree_branch = step % SkillTrees::BRANCHES;
                // Every row of the book, with each thing that could be on it in turn.
                if (name == "spellbook") {
                    book_row = step % 9;
                    const vector<BookRow> rows = SpellbookRows();
                    if (book_row < static_cast<int>(rows.size()) && !rows[book_row].options.empty())
                        ChooseInBook(rows[book_row], (step / 9) % static_cast<int>(rows[book_row].options.size()));
                }
                ui.BeginAudit();
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                if (sc.draw) sc.draw(); else Render();
                for (const UI::Overlap& o : ui.Overlaps()) {
                    const string key = string(sc.name) + "|" + o.a + "|" + o.b;
                    if (!said.insert(key).second) continue;
                    ++met;
                    std::printf("audit %4.0fx%-4.0f @%3.0f%% %-14s the %s and the %s overlap by %.0f\n", size.w, size.h,
                                size.scale * 100.0f, sc.name, o.a.c_str(), o.b.c_str(), o.by);
                }
                for (const UI::Overflow& o : ui.EndAudit()) {
                    const string key = string(sc.name) + "|" + o.text;
                    if (!said.insert(key).second) continue;
                    ++found;
                    std::printf("audit %4.0fx%-4.0f @%3.0f%% %-14s %s%s%s%s  \"%.70s\"\n", size.w, size.h,
                                size.scale * 100.0f, sc.name,
                                o.off_window ? "off the window " : "",
                                o.over_right > 0.5f ? "right " : "", o.over_left > 0.5f ? "left " : "",
                                o.over_bottom > 0.5f ? "bottom " : "", o.text.c_str());
                }
                SDL_RenderPresent(renderer);
            }
            if (target) *target = 0;
        }
        // Out of what the HUD was posed in, so the next size opens its panels
        // on the character the last one did.
        p.talents = talents_was;
        p.equipment = worn_was;
        p.interact = {};
        p.SelectElement(Element::Fire);
        world->SetFlags(flags_was);
    }
    screen_w = was_w;
    screen_h = was_h;
    settings.ui_scale = was_scale;
    settings.show_fps = was_fps;
    std::printf("audit: %d overflowing %s\n", found, found == 1 ? "line" : "lines");
    std::printf("audit: %d overlapping HUD %s\n", met, met == 1 ? "pair" : "pairs");
}

float Game::UiScale() const {
    if (split_active) return 1.0f;
    const float room = std::min(screen_w / 1024.0f, screen_h / 600.0f);
    return std::clamp(std::min(settings.ui_scale, room), 1.0f, 2.0f);
}

SDL_FPoint Game::UiPoint(float world_x, float world_y) const {
    const SDL_FPoint p = world->camera.ToScreen(world_x, world_y);
    const float s = ui.Scale();
    return {p.x / s, p.y / s};
}

void Game::NoteXp(int skill, int amount) {
    if (amount <= 0 || skill < 0 || skill >= SKILL_COUNT) return;
    // The same skill again while its line is still up adds to it: a fight is
    // one number climbing, not a column of fours.
    for (XpLine& line : xp_lines)
        if (line.skill == skill) { line.amount += amount; line.age = 0.0f; return; }
    xp_lines.push_back({skill, amount, 0.0f});
    if (xp_lines.size() > 4) xp_lines.erase(xp_lines.begin());
}

void Game::Render() {
    // The world is drawn at the screen's own pixels and the interface over it
    // at the player's chosen size; see UI::SetScale.
    const float ui_scale = UiScale();
    ui.SetScale(ui_scale);
    ui.SetViewport(screen_w / ui_scale, screen_h / ui_scale);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    if (InGameplayState() && has_session && split_active) {
        RenderSplit();
        DrawParty();
    } else if (InGameplayState() && has_session) {
        world->Render(renderer, *textures);
        SDL_SetRenderScale(renderer, ui_scale, ui_scale);
        DrawNameTags();
        DrawWorldText();
        DrawHud();
        DrawParty();
    } else if (!has_session) {
        // The front end: the cover painting and its night sky, rather than a
        // flat colour. Only without a session -- a panel opened mid-game draws
        // over the world it belongs to.
        SDL_SetRenderScale(renderer, ui_scale, ui_scale);
        title.Draw(renderer, *textures, ui);
    } else {
        SDL_SetRenderDrawColor(renderer, 16, 13, 18, 255);
        SDL_RenderClear(renderer);
    }
    SDL_SetRenderScale(renderer, ui_scale, ui_scale);

    switch (state) {
        case GameState::MainMenu:        DrawMainMenu(); break;
        case GameState::CharacterSelect: DrawCharacterSelect(); break;
        case GameState::SlotSelect:      DrawSlotSelect(); break;
        case GameState::LoadMenu:        DrawLoadMenu(); break;
        case GameState::Options:         DrawOptions(); break;
        case GameState::Controls:        DrawControls(); break;
        case GameState::Multiplayer:     DrawMultiplayer(); break;
        case GameState::Paused:          DrawPaused(); break;
        case GameState::Inventory:       DrawInventory(); break;
        case GameState::SkillsPanel:     DrawSkillsPanel(); break;
        case GameState::Hub:             DrawHub(); break;
        case GameState::QuestPanel:      DrawQuestPanel(); break;
        case GameState::WorldMapPage:    DrawWorldMap(); break;
        case GameState::Dialogue:        DrawDialogue(); break;
        case GameState::Board:           DrawBoard(); break;
        case GameState::Note:            DrawNote(); break;
        case GameState::SleepPrompt:     DrawSleepPrompt(); break;
        case GameState::Travel:          DrawTravel(); break;
        case GameState::TotemRing:       DrawTotemRing(); break;
        case GameState::Crafting:        DrawCrafting(); break;
        case GameState::Enchanting:      DrawEnchanting(); break;
        case GameState::Shop:            DrawShop(); break;
        case GameState::Storage:         DrawStorage(); break;
        case GameState::Death:           DrawDeath(); break;
        case GameState::Play:            break;
    }

    // With two at one machine, a panel says whose it is.
    if (split_active && has_session && InGameplayState() && state != GameState::Play)
        ui.TextShadowed(serving == 1 ? settings.p2_name + "'s" : string("Player One's"), ui.ViewWidth() / 2.0f, 10.0f,
                        TextSize::Body, Palette::Highlight, Align::Center);

    // Map-change wipe sits above the world but below nothing else.
    if (has_session && world->FadeAmount() > 0.0f) {
        ui.Dim(world->FadeAmount());
        // Falling asleep and waking say so while the screen is dark.
        if (!world->FadeCaption().empty()) {
            SDL_Color c = {226, 214, 255, 255};
            c.a = static_cast<Uint8>(255.0f * std::clamp((world->FadeAmount() - 0.35f) / 0.5f, 0.0f, 1.0f));
            if (c.a > 0)
                ui.TextShadowed(world->FadeCaption(), ui.ViewWidth() / 2.0f,
                                ui.ViewHeight() / 2.0f - 12.0f, TextSize::Title, c, Align::Center);
        }
    }

    DrawToasts();

    if (settings.show_fps) {
        char buf[32];
        SDL_snprintf(buf, sizeof(buf), "%.0f fps", fps);
        // Bottom right: the top right corner belongs to the minimap now.
        ui.TextShadowed(buf, ui.ViewWidth() - 10.0f, ui.ViewHeight() - 22.0f,
                        TextSize::Small, Palette::TextDim, Align::Right);
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    if (!shot_path.empty() && run_time >= shot_after) {
        if (SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr)) {
            IMG_SavePNG(pixels, shot_path.c_str());
            SDL_DestroySurface(pixels);
        }
        shot_path.clear();
        running = false;
    }

    SDL_RenderPresent(renderer);
}
