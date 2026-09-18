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
        } else if (arg == "--scratch" && more) {
            launch_scratch = argv[++i];
            never_save = true;
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
    ctx.quests   = &quests;
    ctx.dialogue = &dialogue_db;
    ctx.enemies  = &enemy_db;
    ctx.projectiles = &projectile_db;
    ctx.spells   = &spells;
    ctx.trees    = &skill_trees;
    ctx.input    = &input;
    ctx.rng      = &rng;

    ApplySettings();
    SetState(GameState::MainMenu);

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
        }
    }
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
    ok &= quests.LoadDefinitions("data/quests.json");
    ok &= dialogue_db.Load("data/dialogue.json");
    ok &= projectile_db.Load("data/projectiles.json");
    ok &= spells.Load("data/spells.json");
    ok &= skill_trees.Load("data/skill_trees.json");
    ok &= shop_db.Load("data/shops.json");
    // The world map's marks; the picture itself is baked the first time it is
    // opened, from maps/overworld.mx.
    ok &= world_map.Load("data/worldmap.json", shop_db);

    if (!ok) {
        SDL_Log("DreamQuest: one or more data files failed to load. "
                "Run tools/import_assets.ps1 (or import_assets.sh) first.");
    }
    return ok;
}

void Game::ApplySettings() {
    input.SetMode(static_cast<InputMode>(settings.input_mode));
    world.camera.SetZoom(settings.zoom);
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
    quests.FromJson(json::object());
    world.SetFlags({});
    world.clock.Set(1, 9.0f);
    world.SetCamp({});
    world.SetDream({});
    world.SetPickedHerbs({});
    world.shops.Clear();
    world.player = Player();
    world.player.Init(ctx, character);

    // Starting kit: a few coins, a bit of food, the wood tier's weapon of the
    // character's affinity -- a sword for the hero, a bow for the warden, a
    // staff for the wayfarer -- and something to put between yourself and the
    // first boar. Everything else -- the rest of a set, the tools to work the
    // land, a bedroll -- is bought, found or made. Every character used to
    // start with the sword, which sent the warden and the wayfarer into their
    // first fight with the one weapon their affinity does nothing for.
    //
    // The cuirass and the shield are not generosity. Accuracy here is
    // (level + 8) x (bonus + 64) on both sides, so at level 1 the bonus from
    // gear is most of the number: with nothing worn a boar hits a new
    // character 60% of the time and an orc 65%, while they hit back at about
    // 42%. Twenty-six points of defence bonus brings that to 45% and 48%, and
    // the opening hour stops feeling arranged against you.
    const vector<string> kit = Player::StartingKit(character);
    world.player.inventory.Add("coins", 25);
    for (const string& id : kit) world.player.inventory.Add(id, 1);
    world.player.inventory.Add("cooked_meat", 3);
    // Marked, so loading this character never hands them the tools a character
    // from before gathering needed tools is given.
    world.SetFlag("starter_tools");

    // Worn straight away: a new player should not have to find the bag screen
    // before the first fight to benefit from what they were given.
    string why;
    for (const string& worn : kit)
        for (int slot = 0; slot < world.player.inventory.SlotCount(); ++slot)
            if (world.player.inventory.Slot(slot).id == worn)
                world.player.EquipFromInventory(slot, why);

    active_slot = slot;
    playtime = 0.0f;
    autosave_timer = 0.0f;

    if (!world.LoadMap("overworld", "start", ctx)) {
        SDL_Log("DreamQuest: could not load the starting map");
        SetState(GameState::MainMenu);
        return;
    }

    has_session = true;
    quests.SetDay(world.clock.QuestDay());
    world.shops.SetDay(world.clock.QuestDay());
    quest_day_seen = world.clock.QuestDay();
    SetState(GameState::Play);
    PushToast("A new journey begins.", Palette::Highlight);
    welcome_pending = true;
}

bool Game::LoadGame(int slot) {
    banner_active = false;
    banner_time = 0.0f;
    banner_zone.clear();
    banner_seen_map.clear();
    if (!SaveSystem::Load(slot, world, quests, ctx, playtime)) {
        PushToast("That save could not be loaded.", {235, 120, 120, 255});
        return false;
    }
    active_slot = slot;
    autosave_timer = 0.0f;
    has_session = true;
    quests.SetDay(world.clock.QuestDay());
    world.shops.SetDay(world.clock.QuestDay());
    quest_day_seen = world.clock.QuestDay();
    SetState(GameState::Play);
    PushToast("Welcome back.", Palette::Highlight);

    // A character from before gathering needed tools has none, and could not
    // chop the logs to make an axe with. Hand them the basic set, once.
    if (!world.Flagged("starter_tools")) {
        world.SetFlag("starter_tools");
        Inventory& bag = world.player.inventory;
        bool given = false;
        for (const char* kind : {"axe", "pickaxe", "rod"})
            if (!Gathering::BestTool(bag, world.player.equipment, items, world.player.skills, kind)) {
                const char* id = string(kind) == "axe" ? "bronze_axe" : string(kind) == "pickaxe" ? "bronze_pickaxe" : "fishing_rod";
                if (bag.Add(id, 1) > 0) given = true;
            }
        if (given) PushToast("Your pack has the tools for chopping, mining and fishing now.", Palette::Xp);
    }
    return true;
}

bool Game::SaveGame(int slot) {
    if (!has_session) return false;
    if (never_save) return false;
    if (guest_session) {
        SaveGuestCharacter();
        PushToast("Your character is saved. The world is the host's to keep.", Palette::Xp);
        return true;
    }
    if (SaveSystem::Save(slot, world, quests, playtime)) {
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
        (state == GameState::Options || state == GameState::LoadMenu ||
         state == GameState::CharacterSelect || state == GameState::Multiplayer);

    state = s;
    state_time = 0.0f;
    cursor = back_to_menu ? main_menu_cursor : 0;
    // The player only steers during actual gameplay.
    world.player.input_locked = (s != GameState::Play);
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
        case GameState::Inventory:
        case GameState::SkillsPanel:
        case GameState::QuestPanel:
        case GameState::WorldMapPage:
        case GameState::Dialogue:
        case GameState::Board:
        case GameState::Note:
        case GameState::SleepPrompt:
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
                running = false;
                return;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    running = false;
                    return;
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                screen_w = event.window.data1;
                screen_h = event.window.data2;
                ui.SetViewport(static_cast<float>(screen_w), static_cast<float>(screen_h));
                world.camera.SetViewport(static_cast<float>(screen_w),
                                         static_cast<float>(screen_h));
                break;

            default:
                // A field being typed into takes its keys before the input
                // map can read them as actions.
                if (!TextEntryEvent(event)) input.HandleEvent(event);
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
    UpdateSession(dt);
    UpdateCoop(dt);
    if (!launch_say.empty() && session.Me().Seated()) {
        session.Me().Say(launch_say);
        launch_say.clear();
    }
    for (auto& t : toasts) t.life -= dt;
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
        case GameState::Multiplayer:     UpdateMultiplayer(); break;
        case GameState::Play:            UpdatePlay(dt); break;
        case GameState::Paused:          UpdatePaused(); break;
        case GameState::Inventory:       UpdateInventory(); break;
        case GameState::SkillsPanel:     UpdateSkillsPanel(); break;
        case GameState::QuestPanel:      UpdateQuestPanel(); break;
        case GameState::WorldMapPage:    UpdateWorldMap(); break;
        case GameState::Dialogue:        UpdateDialogue(dt); break;
        case GameState::Board:           UpdateBoard(); break;
        case GameState::Note:            UpdateNote(); break;
        case GameState::SleepPrompt:     UpdateSleepPrompt(); break;
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
    if (has_session && InGameplayState() && world.CurrentMap().Loaded() &&
        world.MapId() != banner_seen_map) {
        banner_seen_map = world.MapId();
        const Map& m = world.CurrentMap();
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
        const float night = (world.InDream() || world.CurrentMap().IsInterior())
                                ? 0.0f : world.clock.Darkness();
        if (fabsf(night - audio_night) > 0.02f) {
            audio_night = night;
            Audio::SetNight(night);
        }
    }

    // Panels pause the world but still show it behind them, so keep the
    // camera settled and let floating text finish.
    if (has_session && state != GameState::Play && InGameplayState())
        world.camera.Follow(world.player.x + world.player.LookAhead().x,
                            world.player.y + world.player.LookAhead().y, dt);
}

void Game::UpdatePlay(float dt) {
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
    quests.SetDay(world.clock.QuestDay());
    world.shops.SetDay(world.clock.QuestDay());
    if (quest_day_seen >= 0 && world.clock.QuestDay() > quest_day_seen)
        PushToast("New notices are up, and the traders have restocked.", Palette::Xp);
    quest_day_seen = world.clock.QuestDay();

    // As a guest, the hands are read here, quantised, and sent as they were
    // used. Alone or hosting, the world reads the device itself.
    if (guest_session) coop_guest.BeforeStep(world, &input);
    else               world.player.hands_external = false;
    world.Update(dt, ctx);
    if (guest_session) coop_guest.AfterStep(world, session.Me(), dt);
    HandleWorldRequests();

    switch (world.TakeWake()) {
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

    // Collect objectives follow the bag, and the bag changes in more places
    // than are worth chasing individually -- eating, delivering, accepting a
    // quest for something already carried. Only a handful of quests are ever
    // active, so settling them every frame costs nothing.
    quests.RefreshCollectObjectives(world.player.inventory);

    // Progression feedback raised by the player during the update.
    const vector<LevelUp> ups = world.player.TakeLevelUps();
    if (!ups.empty()) Audio::Play(Sfx::LevelUp);
    for (const LevelUp& up : ups) {
        PushToast(string(SkillName(up.skill)) + " level " + std::to_string(up.level) + "!",
                  Palette::Highlight);
        // Every fifth level of a tree's skill is a point to spend in it.
        for (int t = 0; t < 3; ++t) {
            const TalentTree& tree = skill_trees.Tree(static_cast<AttackStyle>(t));
            if (tree.skill == up.skill && up.level % SkillTrees::LEVELS_PER_POINT == 0)
                PushToast(tree.name + " skill point  -  " + input.PromptFor(Action::Skills) +
                          " to spend it", Palette::Xp);
        }
    }
    world.player.TakeXpDrops();     // consumed; the HUD shows totals instead

    // Quests that finished this frame pay out now.
    for (const string& id : quests.TakeJustStarted())
        if (const QuestDef* d = quests.Definition(id)) {
            PushToast("Quest started: " + d->name, Palette::Xp);
            Audio::Play(Sfx::QuestStart);
        }

    for (const string& id : quests.TakeJustCompleted())
        GrantQuestRewards(id);

    // Dying in a dream only wakes you; the world handles that.
    if (world.player.IsDead() && world.player.DeathTimer() <= 0.0f && !world.InDream())
        SetState(GameState::Death);

    // --- spell selection -----------------------------------------------------
    // The element is chosen, not the spell: Magic level decides which tier of
    // that element actually comes out.
    if (input.Pressed(Action::SelectFire))  world.player.SelectElement(Element::Fire);
    if (input.Pressed(Action::SelectWater)) world.player.SelectElement(Element::Water);
    if (input.Pressed(Action::SelectEarth)) world.player.SelectElement(Element::Earth);
    if (input.Pressed(Action::SelectAir))   world.player.SelectElement(Element::Air);
    if (input.Pressed(Action::SelectArcane)) {
        const vector<string> known = world.KnownArcane(spells);
        if (known.empty()) PushToast("You know no ancient magic yet. The college in Fernhollow teaches it.", Palette::TextDim);
        else world.player.SelectArcane(known);
    }
    if (input.Pressed(Action::CycleSpell))  world.player.CycleElement(1);

    // --- panel hotkeys -------------------------------------------------------
    if (input.Pressed(Action::Interact))   world.TryInteract(ctx);
    if (input.Pressed(Action::Inventory))  OpenPanel(GameState::Inventory);
    if (input.Pressed(Action::Skills))     OpenPanel(GameState::SkillsPanel);
    if (input.Pressed(Action::QuestLog))   OpenPanel(GameState::QuestPanel);
    if (input.Pressed(Action::WorldMap))   OpenPanel(GameState::WorldMapPage);
    if (input.Pressed(Action::Pause))      OpenPanel(GameState::Paused);

    // --- autosave ------------------------------------------------------------
    // As a guest it is the character that is kept, on this machine; the world
    // is the host's to keep.
    if (!never_save) autosave_timer += dt;
    if (guest_session && autosave_timer >= AUTOSAVE_INTERVAL) {
        autosave_timer = 0.0f;
        SaveGuestCharacter();
    }
    if (autosave_timer >= AUTOSAVE_INTERVAL) {
        autosave_timer = 0.0f;
        if (SaveSystem::Save(active_slot, world, quests, playtime))
            PushToast("Autosaved.", Palette::TextDim);
    }
}

void Game::HandleWorldRequests() {
    for (const WorldRequest& r : world.TakeRequests()) {
        switch (r.type) {
            case WorldRequest::Type::Dialogue: {
                // Freeze the NPC being spoken to.
                for (auto& n : world.npcs)
                    if (n->Id() == r.id) n->talking = true;
                dialogue.Begin(&dialogue_db, r.text, r.id, r.title, MakeDialogueContext());
                HandleDialogueActions(dialogue.TakeActions());
                if (dialogue.Active()) OpenPanel(GameState::Dialogue);
                else                   for (auto& n : world.npcs) n->talking = false;
                break;
            }
            case WorldRequest::Type::Board:
                board_orders = false;
                board_title  = r.title;
                board_quests = r.list;
                // Anything whose giver is this board is pinned to it too:
                // that is how the rotating dailies get posted.
                for (const auto& kv : quests.Definitions())
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

            case WorldRequest::Type::Sleep:
                sleep_title = r.title;
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
    Player& p = world.player;

    for (const DialogueAction& a : actions) {
        if (!a.start_quest.empty()) {
            if (quests.CanStart(a.start_quest, p.skills)) quests.Start(a.start_quest);
        }

        if (!a.advance_quest.empty()) {
            QuestEvent e;
            e.type   = ObjectiveType::Talk;
            e.target = a.advance_quest;
            quests.Notify(e, p.inventory);
        }

        if (!a.give_item.empty()) {
            const int added = p.inventory.Add(a.give_item, a.give_qty);
            if (added > 0) {
                const ItemDef* d = items.Get(a.give_item);
                PushToast("Received " + std::to_string(added) + "x " +
                          (d ? d->name : a.give_item), Palette::Xp);
                quests.RefreshCollectObjectives(p.inventory);
            }
            if (added < a.give_qty) {
                world.DropItem(a.give_item, a.give_qty - added, p.x, p.y + 6.0f, ctx);
                PushToast("Your pack is full. The item is at your feet.", {235, 150, 120, 255});
            }
        }

        if (!a.take_item.empty()) {
            if (p.inventory.Remove(a.take_item, a.take_qty)) {
                QuestEvent e;
                e.type      = ObjectiveType::Deliver;
                e.target    = a.take_item;
                e.secondary = dialogue.NpcId();
                e.amount    = a.take_qty;
                quests.Notify(e, p.inventory);
            }
        }

        if (!a.skill_xp.empty() && a.xp_amount > 0) {
            const int s = SkillFromName(a.skill_xp);
            if (s >= 0) p.GrantXp(s, a.xp_amount);
        }

        // Trading waits for the conversation to close; see UpdateDialogue.
        if (!a.open_shop.empty() && shop_db.Get(a.open_shop)) pending_shop = a.open_shop;
        if (!a.open_orders.empty()) pending_orders = a.open_orders;

        if (!a.learn_recipe.empty() && !world.KnowsRecipe(a.learn_recipe)) {
            world.SetFlag("recipe:" + a.learn_recipe);
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
            for (const string& id : quests.ReadyToDeliver(dialogue.NpcId(), p.inventory)) {
                const QuestDef* d = quests.Definition(id);
                const QuestStage& st = d->stages[quests.Stage(id)];
                const int need = st.count - quests.Counter(id);
                if (need <= 0 || !p.inventory.Remove(st.target, need)) continue;
                QuestEvent e;
                e.type      = ObjectiveType::Deliver;
                e.target    = st.target;
                e.secondary = dialogue.NpcId();
                e.amount    = need;
                quests.Notify(e, p.inventory);
            }
        }

        if (a.heal) {
            p.skills.ResetCurrent();
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
    for (const auto& kv : quests.Definitions())
        if (kv.second.giver == npc_id && kv.second.daily) board_quests.push_back(kv.first);
    board_cursor = 0;
    SetState(GameState::Board);
}

DialogueContext Game::MakeDialogueContext() const {
    DialogueContext c;
    c.quests    = &quests;
    c.inventory = &world.player.inventory;
    c.skills    = &world.player.skills;
    c.flags     = &world.Flags();
    c.night     = world.clock.IsNight();
    return c;
}

void Game::GrantQuestRewards(const string& quest_id) {
    const QuestDef* d = quests.Definition(quest_id);
    if (!d) return;

    Player& p = world.player;

    for (const auto& xp : d->rewards.xp) p.GrantXp(xp.first, xp.second);
    if (d->rewards.coins > 0) p.inventory.AddCoins(d->rewards.coins);

    for (const auto& item : d->rewards.items) {
        const int added = p.inventory.Add(item.first, item.second);
        if (added < item.second) {
            // No room: drop it at the player's feet rather than losing it.
            world.DropItem(item.first, item.second - added, p.x, p.y + 6.0f, ctx);
        }
    }

    PushToast("Quest complete: " + d->name, Palette::Highlight);
    Audio::Play(Sfx::QuestComplete);
    quests.RefreshCollectObjectives(p.inventory);
}

// -----------------------------------------------------------------------------
//  Render dispatch
// -----------------------------------------------------------------------------

void Game::Render() {
    ui.SetViewport(static_cast<float>(screen_w), static_cast<float>(screen_h));

    if (InGameplayState() && has_session) {
        world.Render(renderer, *textures);
        DrawNameTags();
        DrawWorldText();
        DrawHud();
        DrawParty();
    } else if (!has_session) {
        // The front end: the cover painting and its night sky, rather than a
        // flat colour. Only without a session -- a panel opened mid-game draws
        // over the world it belongs to.
        title.Draw(renderer, *textures, ui);
    } else {
        SDL_SetRenderDrawColor(renderer, 16, 13, 18, 255);
        SDL_RenderClear(renderer);
    }

    switch (state) {
        case GameState::MainMenu:        DrawMainMenu(); break;
        case GameState::CharacterSelect: DrawCharacterSelect(); break;
        case GameState::SlotSelect:      DrawSlotSelect(); break;
        case GameState::LoadMenu:        DrawLoadMenu(); break;
        case GameState::Options:         DrawOptions(); break;
        case GameState::Multiplayer:     DrawMultiplayer(); break;
        case GameState::Paused:          DrawPaused(); break;
        case GameState::Inventory:       DrawInventory(); break;
        case GameState::SkillsPanel:     DrawSkillsPanel(); break;
        case GameState::QuestPanel:      DrawQuestPanel(); break;
        case GameState::WorldMapPage:    DrawWorldMap(); break;
        case GameState::Dialogue:        DrawDialogue(); break;
        case GameState::Board:           DrawBoard(); break;
        case GameState::Note:            DrawNote(); break;
        case GameState::SleepPrompt:     DrawSleepPrompt(); break;
        case GameState::Crafting:        DrawCrafting(); break;
        case GameState::Enchanting:      DrawEnchanting(); break;
        case GameState::Shop:            DrawShop(); break;
        case GameState::Storage:         DrawStorage(); break;
        case GameState::Death:           DrawDeath(); break;
        case GameState::Play:            break;
    }

    // Map-change wipe sits above the world but below nothing else.
    if (has_session && world.FadeAmount() > 0.0f) {
        ui.Dim(world.FadeAmount());
        // Falling asleep and waking say so while the screen is dark.
        if (!world.FadeCaption().empty()) {
            SDL_Color c = {226, 214, 255, 255};
            c.a = static_cast<Uint8>(255.0f * std::clamp((world.FadeAmount() - 0.35f) / 0.5f, 0.0f, 1.0f));
            if (c.a > 0)
                ui.TextShadowed(world.FadeCaption(), ui.ViewWidth() / 2.0f,
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
