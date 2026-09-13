#include "game.h"

static constexpr float AUTOSAVE_INTERVAL = 120.0f;
static constexpr float MAX_FRAME_DT      = 0.05f;   // clamp after a stall

Game::Game() : rng(std::random_device{}()) {}

Game::~Game() {
    // The minimap owns a texture, so it has to let go before the renderer does.
    minimap.Forget();
    Audio::Shutdown();
    ui.Shutdown();
    delete textures;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

int Game::Start(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("DreamQuest: SDL_Init failed: %s", SDL_GetError());
        return 0;
    }
    if (!TTF_Init()) {
        SDL_Log("DreamQuest: TTF_Init failed: %s", SDL_GetError());
        return 0;
    }

    settings.Load();
    // Silence is a fine fallback: a machine with no output device still plays.
    Audio::Init();

    const SDL_WindowFlags flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    window = SDL_CreateWindow("DreamQuest", screen_w, screen_h, flags);
    if (!window) {
        SDL_Log("DreamQuest: could not create window: %s", SDL_GetError());
        return 0;
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
    ok &= enemy_db.Load("data/enemies.json");
    ok &= loot.Load("data/loot_tables.json");
    loot.Load("data/loot_tables_armour.json", false);   // optional armour drops
    ok &= quests.LoadDefinitions("data/quests.json");
    ok &= dialogue_db.Load("data/dialogue.json");
    ok &= projectile_db.Load("data/projectiles.json");
    ok &= spells.Load("data/spells.json");
    ok &= skill_trees.Load("data/skill_trees.json");

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
    world.player = Player();
    world.player.Init(ctx, character);

    // Starting kit: something to fight with, something to hide behind, and
    // something to eat when neither worked.
    world.player.inventory.Add("coins", 25);
    world.player.inventory.Add("bronze_sword", 1);
    world.player.inventory.Add("wooden_shield", 1);
    world.player.inventory.Add("cooked_meat", 4);
    // A starter bow and staff, so both other styles can be tried out from the
    // first minute rather than waiting on a drop.
    world.player.inventory.Add("training_bow", 1);
    world.player.inventory.Add("novice_staff", 1);
    // And a bedroll, so the first night can be slept through wherever it falls.
    world.player.inventory.Add("bedroll", 1);

    string why;
    for (int slot = 0; slot < world.player.inventory.SlotCount(); ++slot) {
        const string& id = world.player.inventory.Slot(slot).id;
        if (id == "bronze_sword" || id == "wooden_shield")
            world.player.EquipFromInventory(slot, why);
    }

    active_slot = slot;
    playtime = 0.0f;
    autosave_timer = 0.0f;

    if (!world.LoadMap("overworld", "start", ctx)) {
        SDL_Log("DreamQuest: could not load the starting map");
        SetState(GameState::MainMenu);
        return;
    }

    has_session = true;
    SetState(GameState::Play);
    PushToast("A new journey begins.", Palette::Highlight);
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
    SetState(GameState::Play);
    PushToast("Welcome back.", Palette::Highlight);
    return true;
}

bool Game::SaveGame(int slot) {
    if (!has_session) return false;
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
         state == GameState::CharacterSelect);

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
        case GameState::Dialogue:
        case GameState::Board:
        case GameState::Note:
        case GameState::Crafting:
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
                input.HandleEvent(event);
                break;
        }
    }
}

void Game::Update(float dt) {
    state_time += dt;
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
        case GameState::Play:            UpdatePlay(dt); break;
        case GameState::Paused:          UpdatePaused(); break;
        case GameState::Inventory:       UpdateInventory(); break;
        case GameState::SkillsPanel:     UpdateSkillsPanel(); break;
        case GameState::QuestPanel:      UpdateQuestPanel(); break;
        case GameState::Dialogue:        UpdateDialogue(dt); break;
        case GameState::Board:           UpdateBoard(); break;
        case GameState::Note:            UpdateNote(); break;
        case GameState::Crafting:        UpdateCrafting(); break;
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
    playtime += dt;

    world.Update(dt, ctx);
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
    if (input.Pressed(Action::CycleSpell))  world.player.CycleElement(1);

    // --- panel hotkeys -------------------------------------------------------
    if (input.Pressed(Action::Interact))   world.TryInteract(ctx);
    if (input.Pressed(Action::Inventory))  OpenPanel(GameState::Inventory);
    if (input.Pressed(Action::Skills))     OpenPanel(GameState::SkillsPanel);
    if (input.Pressed(Action::QuestLog))   OpenPanel(GameState::QuestPanel);
    if (input.Pressed(Action::Pause))      OpenPanel(GameState::Paused);

    // --- autosave ------------------------------------------------------------
    autosave_timer += dt;
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
                dialogue.Begin(&dialogue_db, r.text, r.id, r.title);
                HandleDialogueActions(dialogue.TakeActions());
                if (dialogue.Active()) OpenPanel(GameState::Dialogue);
                else                   for (auto& n : world.npcs) n->talking = false;
                break;
            }
            case WorldRequest::Type::Board:
                board_title  = r.title;
                board_quests = r.list;
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

        if (a.heal) {
            p.skills.ResetCurrent();
            p.SyncHitpoints();
            p.hp = p.max_hp;
            PushToast("You feel restored.", Palette::Xp);
        }
    }

    // Talking to someone is itself a quest objective.
    if (!dialogue.NpcId().empty()) {
        QuestEvent e;
        e.type   = ObjectiveType::Talk;
        e.target = dialogue.NpcId();
        quests.Notify(e, p.inventory);
    }
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
        DrawWorldText();
        DrawHud();
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
        case GameState::Paused:          DrawPaused(); break;
        case GameState::Inventory:       DrawInventory(); break;
        case GameState::SkillsPanel:     DrawSkillsPanel(); break;
        case GameState::QuestPanel:      DrawQuestPanel(); break;
        case GameState::Dialogue:        DrawDialogue(); break;
        case GameState::Board:           DrawBoard(); break;
        case GameState::Note:            DrawNote(); break;
        case GameState::Crafting:        DrawCrafting(); break;
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

    SDL_RenderPresent(renderer);
}
