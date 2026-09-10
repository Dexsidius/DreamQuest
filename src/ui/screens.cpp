#include "../game.h"

// =============================================================================
//  Shared helpers
// =============================================================================

namespace {

SDL_FRect CenteredPanel(const UI& ui, float w, float h) {
    return {(ui.ViewWidth() - w) / 2.0f, (ui.ViewHeight() - h) / 2.0f, w, h};
}

// Stand-in tile for an item whose icon art has not been imported: a coloured
// swatch keyed off the item id, with its initial on top. Keeps the inventory
// readable instead of showing empty squares.
void DrawItemPlaceholder(UI& ui, const ItemDef* def, const string& id,
                         const SDL_FRect& r) {
    size_t hash = 0;
    for (char c : id) hash = hash * 31 + static_cast<unsigned char>(c);
    const SDL_Color swatch = {static_cast<Uint8>(70 + (hash % 90)),
                              static_cast<Uint8>(58 + ((hash / 7) % 80)),
                              static_cast<Uint8>(46 + ((hash / 13) % 70)), 255};

    ui.Fill(r, swatch);
    ui.Outline(r, {20, 16, 12, 200}, 1.0f);

    const string label = def ? def->name : id;
    const string letter = label.empty() ? string("?") : label.substr(0, 1);
    ui.Text(letter, r.x + r.w / 2.0f,
            r.y + (r.h - ui.LineHeight(TextSize::Small)) / 2.0f,
            TextSize::Small, Palette::Text, Align::Center);
}

// The main menu list, built once so the update and draw passes always agree
// on what is at each index.
vector<string> MainMenuOptions(bool any_save) {
    vector<string> options;
    if (any_save) options.push_back("Continue");
    options.push_back("New Game");
    options.push_back("Load Game");
    options.push_back("Options");
    options.push_back("Quit");
    return options;
}

bool AnySaveExists(const vector<SaveSlotInfo>& slots) {
    return std::any_of(slots.begin(), slots.end(),
                       [](const SaveSlotInfo& s) { return s.exists; });
}

const char* InputModeLabel(int mode) {
    switch (mode) {
        case 1:  return "Keyboard & Mouse";
        case 2:  return "Controller";
        default: return "Automatic";
    }
}

} // namespace

// =============================================================================
//  Main menu
// =============================================================================

void Game::UpdateMainMenu() {
    const vector<SaveSlotInfo> slots = SaveSystem::PeekAll();
    const vector<string> options = MainMenuOptions(AnySaveExists(slots));

    MoveCursor(cursor, static_cast<int>(options.size()));
    cursor = std::clamp(cursor, 0, static_cast<int>(options.size()) - 1);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const string& choice = options[cursor];
        if (choice == "Continue") {
            // Resume the most recently written slot.
            int best = -1;
            string newest;
            for (const SaveSlotInfo& s : slots)
                if (s.exists && s.saved_at >= newest) { newest = s.saved_at; best = s.slot; }
            if (best > 0) LoadGame(best);
        } else if (choice == "New Game") {
            SetState(GameState::CharacterSelect);
        } else if (choice == "Load Game") {
            SetState(GameState::LoadMenu);
        } else if (choice == "Options") {
            OpenPanel(GameState::Options);
        } else if (choice == "Quit") {
            running = false;
        }
    }
}

void Game::DrawMainMenu() {
    const float cx = ui.ViewWidth() / 2.0f;

    ui.Text("DREAMQUEST", cx, ui.ViewHeight() * 0.16f, TextSize::Title,
            Palette::Highlight, Align::Center);
    ui.Text("An adventure in the Hollowmarch", cx, ui.ViewHeight() * 0.16f + 52.0f,
            TextSize::Body, Palette::TextDim, Align::Center);

    const vector<SaveSlotInfo> slots = SaveSystem::PeekAll();
    const vector<string> options = MainMenuOptions(AnySaveExists(slots));

    const float row_h = 42.0f;
    const float panel_w = 300.0f;
    const float panel_h = row_h * options.size() + 28.0f;
    const SDL_FRect panel = {cx - panel_w / 2.0f, ui.ViewHeight() * 0.42f, panel_w, panel_h};
    ui.Panel(panel);

    for (size_t i = 0; i < options.size(); ++i) {
        const SDL_FRect row = {panel.x + 12.0f, panel.y + 14.0f + i * row_h,
                               panel.w - 24.0f, row_h - 4.0f};
        ui.MenuItem(row, options[i], static_cast<int>(i) == cursor);
    }

    ui.Text(input.ActiveDevice() == InputMode::Controller
                ? "D-pad to move   (A) select"
                : "Arrow keys / WASD to move   Enter to select",
            cx, ui.ViewHeight() - 44.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// =============================================================================
//  Character select
// =============================================================================

// The choices, and the order they appear in. player_hero is this project's own
// character -- modelled and animated in tools/blender_character.py rather than
// taken from a pack -- so it leads.
const char* Game::kCharacterIds[kCharacterCount] = {
    "player_hero", "player_male", "player_female"
};
const char* Game::kCharacterLabels[kCharacterCount] = {
    "Hollow-born", "Wanderer", "Wayfarer"
};

void Game::UpdateCharacterSelect() {
    // Left actually goes left. The old version advanced the cursor for both
    // directions, which was survivable with two choices and is not with three.
    if (input.MenuLeft())  cursor = (cursor + kCharacterCount - 1) % kCharacterCount;
    if (input.MenuRight()) cursor = (cursor + 1) % kCharacterCount;
    MoveCursor(cursor, kCharacterCount);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        pending_character = kCharacterIds[std::clamp(cursor, 0, kCharacterCount - 1)];
        slot_purpose = 0;
        SetState(GameState::SlotSelect);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::MainMenu);
}

void Game::DrawCharacterSelect() {
    const float cx = ui.ViewWidth() / 2.0f;
    ui.Text("Choose your adventurer", cx, ui.ViewHeight() * 0.16f, TextSize::Large,
            Palette::Text, Align::Center);

    const float card_w = 200.0f, card_h = 280.0f, gap = 28.0f;
    const float total = card_w * kCharacterCount + gap * (kCharacterCount - 1);
    const float start_x = cx - total / 2.0f;

    for (int i = 0; i < kCharacterCount; ++i) {
        const SDL_FRect card = {start_x + i * (card_w + gap), ui.ViewHeight() * 0.3f,
                                card_w, card_h};
        ui.Panel(card, i == cursor);
        if (i == cursor) ui.Outline(card, Palette::Highlight, 2.0f);

        // Live idle animation as the preview.
        if (const SpriteDef* def = sprites.Get(kCharacterIds[i])) {
            Sprite preview;
            preview.SetDef(def);
            preview.facing = FACE_DOWN;
            preview.Play("idle");
            preview.Update(static_cast<float>(SDL_GetTicks()) / 1000.0f);
            const SDL_FRect dst = {card.x + card.w / 2.0f - 64.0f, card.y + 40.0f, 128.0f, 128.0f};
            preview.DrawAt(renderer, *textures, dst);
        }

        ui.Text(kCharacterLabels[i], card.x + card.w / 2.0f, card.y + card.h - 58.0f,
                TextSize::Body, i == cursor ? Palette::Highlight : Palette::Text,
                Align::Center);
    }

    ui.Text("Left / Right to choose   " + input.PromptFor(Action::Confirm) + " to continue   " +
            input.PromptFor(Action::Back) + " to go back",
            cx, ui.ViewHeight() - 52.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// =============================================================================
//  Slot select (used for starting a new game and for saving)
// =============================================================================

void Game::UpdateSlotSelect() {
    MoveCursor(cursor, SAVE_SLOTS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const int slot = cursor + 1;
        if (slot_purpose == 0) NewGame(pending_character, slot);
        else {
            SaveGame(slot);
            SetState(GameState::Play);
        }
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(slot_purpose == 0 ? GameState::CharacterSelect : GameState::Paused);
}

void Game::DrawSlotList(const SDL_FRect& area, const string& heading) {
    ui.Panel(area);
    ui.Text(heading, area.x + area.w / 2.0f, area.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const auto slots = SaveSystem::PeekAll();
    const float row_h = 84.0f;
    const float top = area.y + 66.0f;

    for (int i = 0; i < SAVE_SLOTS; ++i) {
        const SDL_FRect row = {area.x + 16.0f, top + i * (row_h + 10.0f),
                               area.w - 32.0f, row_h};
        const bool selected = (i == cursor);

        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, selected ? Palette::Highlight : Palette::BorderDim,
                   selected ? 2.0f : 1.0f);

        const SaveSlotInfo& s = slots[i];
        ui.Text("Slot " + std::to_string(i + 1), row.x + 16.0f, row.y + 12.0f,
                TextSize::Body, selected ? Palette::Highlight : Palette::Text);

        if (!s.exists) {
            ui.Text("Empty", row.x + 16.0f, row.y + 42.0f, TextSize::Small, Palette::TextDim);
            continue;
        }

        ui.Text(s.map_name, row.x + 120.0f, row.y + 12.0f, TextSize::Body, Palette::Text);
        char line[160];
        SDL_snprintf(line, sizeof(line), "Combat %d    Total level %d    %s",
                     s.combat_level, s.total_level,
                     SaveSystem::FormatPlaytime(s.playtime).c_str());
        ui.Text(line, row.x + 16.0f, row.y + 42.0f, TextSize::Small, Palette::TextDim);
        ui.Text(s.saved_at, row.x + row.w - 16.0f, row.y + 42.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
    }
}

void Game::DrawSlotSelect() {
    const SDL_FRect area = CenteredPanel(ui, 560.0f, 400.0f);
    DrawSlotList(area, slot_purpose == 0 ? "Start a new game in..." : "Save game to...");
    ui.Text(input.PromptFor(Action::Confirm) + " confirm     " +
            input.PromptFor(Action::Back) + " back",
            ui.ViewWidth() / 2.0f, area.y + area.h + 16.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Load menu
// =============================================================================

void Game::UpdateLoadMenu() {
    MoveCursor(cursor, SAVE_SLOTS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (SaveSystem::Exists(cursor + 1)) LoadGame(cursor + 1);
        else PushToast("That slot is empty.", Palette::TextDim);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::MainMenu);
}

void Game::DrawLoadMenu() {
    const SDL_FRect area = CenteredPanel(ui, 560.0f, 400.0f);
    DrawSlotList(area, "Load game");
    ui.Text(input.PromptFor(Action::Confirm) + " load     " +
            input.PromptFor(Action::Back) + " back",
            ui.ViewWidth() / 2.0f, area.y + area.h + 16.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Options
// =============================================================================

void Game::UpdateOptions() {
    constexpr int ROWS = 7;
    MoveCursor(cursor, ROWS);

    int delta = 0;
    if (input.MenuRight()) delta = 1;
    if (input.MenuLeft())  delta = -1;
    const bool confirm = input.Pressed(Action::Confirm) || input.Pressed(Action::Interact);

    if (delta != 0 || confirm) {
        const int step = (delta != 0) ? delta : 1;
        switch (cursor) {
            case 0:
                settings.input_mode = ((settings.input_mode + step) % 3 + 3) % 3;
                input.SetMode(static_cast<InputMode>(settings.input_mode));
                break;
            case 1:
                settings.zoom = std::clamp(settings.zoom + step * 0.25f, 1.5f, 4.0f);
                world.camera.SetZoom(settings.zoom);
                break;
            case 2:
                settings.fullscreen = !settings.fullscreen;
                SDL_SetWindowFullscreen(window, settings.fullscreen);
                break;
            case 3:
                settings.vsync = !settings.vsync;
                SDL_SetRenderVSync(renderer, settings.vsync ? 1 : 0);
                break;
            case 4: settings.show_fps = !settings.show_fps; break;
            case 5: settings.damage_numbers = !settings.damage_numbers; break;
            case 6:
                if (confirm) {
                    settings.Save();
                    SetState(return_state);
                    return;
                }
                break;
        }
        settings.Save();
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        settings.Save();
        SetState(return_state);
    }
}

void Game::DrawOptions() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 430.0f);
    ui.Panel(panel);

    ui.Text("Options", panel.x + panel.w / 2.0f, panel.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    char zoom_buf[24];
    SDL_snprintf(zoom_buf, sizeof(zoom_buf), "%.2fx", settings.zoom);

    const string pad_note = input.HasGamepad()
        ? string(input.GamepadName())
        : string("no controller detected");

    const pair<string, string> rows[] = {
        {"Input Device",   InputModeLabel(settings.input_mode)},
        {"Camera Zoom",    zoom_buf},
        {"Fullscreen",     settings.fullscreen ? "On" : "Off"},
        {"VSync",          settings.vsync ? "On" : "Off"},
        {"Show FPS",       settings.show_fps ? "On" : "Off"},
        {"Damage Numbers", settings.damage_numbers ? "On" : "Off"},
        {"Back",           ""},
    };

    const float row_h = 44.0f;
    for (int i = 0; i < 7; ++i) {
        const SDL_FRect row = {panel.x + 16.0f, panel.y + 62.0f + i * row_h,
                               panel.w - 32.0f, row_h - 4.0f};
        ui.MenuItem(row, rows[i].first, i == cursor, true, rows[i].second);
    }

    ui.Text("Controller: " + pad_note, panel.x + panel.w / 2.0f,
            panel.y + panel.h - 52.0f, TextSize::Small, Palette::TextDim, Align::Center);
    ui.Text("Left / Right to change", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 30.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// =============================================================================
//  Pause
// =============================================================================

void Game::UpdatePaused() {
    static const char* kRows[] = {"Resume", "Save Game", "Options", "Quit to Main Menu"};
    constexpr int ROWS = 4;
    MoveCursor(cursor, ROWS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        switch (cursor) {
            case 0: SetState(GameState::Play); break;
            case 1: slot_purpose = 1; SetState(GameState::SlotSelect); break;
            case 2: OpenPanel(GameState::Options); break;
            case 3:
                // Save before leaving, so quitting never costs progress.
                SaveSystem::Save(active_slot, world, quests, playtime);
                has_session = false;
                SetState(GameState::MainMenu);
                break;
        }
        (void)kRows;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawPaused() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 340.0f, 280.0f);
    ui.Panel(panel);

    ui.Text("Paused", panel.x + panel.w / 2.0f, panel.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    static const char* kRows[] = {"Resume", "Save Game", "Options", "Quit to Main Menu"};
    for (int i = 0; i < 4; ++i) {
        const SDL_FRect row = {panel.x + 16.0f, panel.y + 66.0f + i * 44.0f,
                               panel.w - 32.0f, 40.0f};
        ui.MenuItem(row, kRows[i], i == cursor);
    }

    ui.Text("Playtime " + SaveSystem::FormatPlaytime(playtime),
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  HUD
// =============================================================================

void Game::DrawWorldText() {
    if (!settings.damage_numbers) return;

    for (const FloatingText& t : world.texts) {
        const float progress = 1.0f - (t.life / t.max_life);
        const SDL_FPoint p = world.camera.ToScreen(t.x, t.y - t.rise * progress);
        SDL_Color c = t.color;
        // Fade out over the last third of the life.
        c.a = static_cast<Uint8>(255 * std::clamp((t.life / t.max_life) * 3.0f, 0.0f, 1.0f));
        ui.TextShadowed(t.text, p.x, p.y, TextSize::Small, c, Align::Center);
    }
}

void Game::DrawHud() {
    const Player& p = world.player;

    // --- health --------------------------------------------------------------
    const SDL_FRect hp_bar = {18.0f, 16.0f, 232.0f, 20.0f};
    ui.Bar(hp_bar, p.max_hp > 0 ? static_cast<float>(p.hp) / p.max_hp : 0.0f,
           Palette::Health, Palette::HealthBack);
    char hp_text[32];
    SDL_snprintf(hp_text, sizeof(hp_text), "%d / %d", p.hp, p.max_hp);
    ui.TextShadowed(hp_text, hp_bar.x + hp_bar.w / 2.0f, hp_bar.y + 1.0f,
                    TextSize::Small, Palette::Text, Align::Center);

    // --- mana ----------------------------------------------------------------
    // Only shown once the player has any, so a pure melee character is not
    // told about a resource they never spend.
    float meta_y = hp_bar.y + hp_bar.h + 8.0f;
    if (p.MaxMana() > 0) {
        const SDL_FRect mana_bar = {hp_bar.x, hp_bar.y + hp_bar.h + 4.0f, 232.0f, 12.0f};
        ui.Bar(mana_bar, static_cast<float>(p.Mana()) / p.MaxMana(),
               Palette::Mana, Palette::ManaBack);
        char mana_text[32];
        SDL_snprintf(mana_text, sizeof(mana_text), "%d / %d", p.Mana(), p.MaxMana());
        ui.TextShadowed(mana_text, mana_bar.x + mana_bar.w / 2.0f, mana_bar.y - 2.0f,
                        TextSize::Small, Palette::Text, Align::Center);
        meta_y = mana_bar.y + mana_bar.h + 6.0f;
    }

    char meta[96];
    SDL_snprintf(meta, sizeof(meta), "Combat %d    %d coins",
                 p.skills.CombatLevel(), p.inventory.Coins());
    ui.TextShadowed(meta, hp_bar.x, meta_y, TextSize::Small, Palette::TextDim);

    // Meters and prompts describe what the button does right now, so they are
    // only meaningful while the player actually has control.
    const bool live = (state == GameState::Play);

    // --- what the attack button will do ---------------------------------------
    // A staff shows the four elements with the selected one lit, and names the
    // spell that Magic level actually casts. A bow just says so.
    if (live) {
        const AttackStyle style = p.Style();
        if (style == AttackStyle::Magic) {
            static const Element kOrder[4] = {Element::Fire, Element::Water,
                                              Element::Earth, Element::Air};
            const float box = 30.0f, gap = 5.0f;
            const float total = box * 4 + gap * 3;
            const float x0 = ui.ViewWidth() / 2.0f - total / 2.0f;
            const float y0 = ui.ViewHeight() - 62.0f;

            for (int i = 0; i < 4; ++i) {
                const bool on = (kOrder[i] == p.SelectedElement());
                const SDL_FRect r = {x0 + i * (box + gap), y0, box, box};
                const SDL_Color c = ElementColor(kOrder[i]);
                const SpellDef* known = spells.BestFor(kOrder[i],
                                                       p.skills.Level(SKILL_MAGIC));

                ui.Fill(r, {static_cast<Uint8>(c.r / (on ? 2 : 5)),
                            static_cast<Uint8>(c.g / (on ? 2 : 5)),
                            static_cast<Uint8>(c.b / (on ? 2 : 5)),
                            on ? static_cast<Uint8>(235) : static_cast<Uint8>(180)});
                ui.Outline(r, on ? c : Palette::BorderDim, on ? 2.0f : 1.0f);

                // Number key that selects it.
                ui.Text(std::to_string(i + 1), r.x + r.w / 2.0f, r.y + 6.0f,
                        TextSize::Small,
                        known ? (on ? Palette::Text : Palette::TextDim)
                              : SDL_Color{110, 100, 96, 255},
                        Align::Center);
            }

            const SpellDef* current = spells.BestFor(p.SelectedElement(),
                                                     p.skills.Level(SKILL_MAGIC));
            string line;
            if (current) {
                line = current->name + "   " + std::to_string(current->mana) + " mana";
            } else {
                const SpellDef* next = spells.NextFor(p.SelectedElement(),
                                                      p.skills.Level(SKILL_MAGIC));
                line = next ? ("Magic " + std::to_string(next->level) + " for " + next->name)
                            : "Nothing known";
            }
            ui.TextShadowed(line, ui.ViewWidth() / 2.0f, y0 + box + 4.0f,
                            TextSize::Small,
                            current ? ElementColor(p.SelectedElement()) : Palette::TextDim,
                            Align::Center);
        } else if (style == AttackStyle::Ranged) {
            ui.TextShadowed("Bow drawn", ui.ViewWidth() / 2.0f,
                            ui.ViewHeight() - 46.0f, TextSize::Small,
                            Palette::TextDim, Align::Center);
        }
    }

    // --- charge meter --------------------------------------------------------
    if (live && p.IsCharging()) {
        const float t = p.ChargeProgress();
        const SDL_FPoint anchor = world.camera.ToScreen(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 34.0f, anchor.y + 8.0f, 68.0f, 8.0f};
        // Turns bright at full charge, so the release timing is readable.
        const SDL_Color fill = (t >= 1.0f) ? SDL_Color{255, 236, 150, 255} : Palette::Charge;
        ui.Bar(bar, t, fill, {30, 20, 12, 220});
        if (t >= 1.0f)
            ui.TextShadowed("READY", bar.x + bar.w / 2.0f, bar.y - 18.0f,
                            TextSize::Small, fill, Align::Center);
    }

    // --- gathering -----------------------------------------------------------
    if (live && world.Gathering()) {
        const SDL_FPoint anchor = world.camera.ToScreen(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 34.0f, anchor.y + 8.0f, 68.0f, 8.0f};
        ui.Bar(bar, world.GatherProgress(), Palette::Xp, {20, 30, 20, 220});
    }

    // --- interact prompt -----------------------------------------------------
    if (live && p.interact.kind != InteractTarget::None && !world.Gathering()) {
        const string prompt = "[" + input.PromptFor(Action::Interact) + "]  " + p.interact.label;
        const SDL_FPoint size = ui.Measure(prompt, TextSize::Body);
        const SDL_FRect box = {ui.ViewWidth() / 2.0f - size.x / 2.0f - 14.0f,
                               ui.ViewHeight() - 92.0f, size.x + 28.0f, size.y + 12.0f};
        ui.Fill(box, {20, 16, 12, 210});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        ui.Text(prompt, ui.ViewWidth() / 2.0f, box.y + 6.0f, TextSize::Body,
                Palette::Text, Align::Center);
    }

    // --- quest tracker -------------------------------------------------------
    const vector<string> active = quests.Active();
    if (!active.empty()) {
        const float right = ui.ViewWidth() - 18.0f;
        // Sits below however many toasts are currently stacked.
        float y = 74.0f + toasts.size() * 20.0f;
        ui.TextShadowed("QUESTS", right, y, TextSize::Small, Palette::Highlight, Align::Right);
        y += 22.0f;

        // Show at most three so the tracker never crowds the view.
        for (size_t i = 0; i < active.size() && i < 3; ++i) {
            const QuestDef* d = quests.Definition(active[i]);
            if (!d) continue;
            ui.TextShadowed(d->name, right, y, TextSize::Small, Palette::Text, Align::Right);
            y += 18.0f;
            ui.TextShadowed(quests.CurrentObjectiveText(active[i]), right, y,
                            TextSize::Small, Palette::TextDim, Align::Right);
            y += 24.0f;
        }
    }

    // --- controls hint -------------------------------------------------------
    if (!live) return;
    string spell_hint;
    if (p.Style() == AttackStyle::Magic)
        spell_hint = (input.ActiveDevice() == InputMode::Controller
                          ? string("RS element    ")
                          : string("1-4 element    "));

    const string hint = spell_hint + input.PromptFor(Action::Inventory) + " bag    " +
                        input.PromptFor(Action::Skills) + " skills    " +
                        input.PromptFor(Action::QuestLog) + " quests    " +
                        input.PromptFor(Action::Pause) + " menu";
    ui.TextShadowed(hint, 18.0f, ui.ViewHeight() - 28.0f, TextSize::Small, Palette::TextDim);
}

void Game::DrawToasts() {
    float y = 18.0f;
    const float right = ui.ViewWidth() - 18.0f;

    for (const Toast& t : toasts) {
        SDL_Color c = t.color;
        c.a = static_cast<Uint8>(255 * std::clamp(t.life / 0.6f, 0.0f, 1.0f));
        // Toasts sit under the quest tracker when a game is running.
        const float draw_y = InGameplayState() ? y + 0.0f : y;
        ui.TextShadowed(t.text, right, draw_y, TextSize::Small, c, Align::Right);
        y += 20.0f;
    }
}

// =============================================================================
//  Inventory
// =============================================================================

void Game::UpdateInventory() {
    Player& p = world.player;
    constexpr int COLS = 7;
    const int slots = p.inventory.SlotCount();

    if (inventory_on_equipment) {
        MoveCursor(equipment_cursor, SLOT_COUNT);
        if (input.MenuLeft()) inventory_on_equipment = false;

        if (input.Pressed(Action::Confirm)) {
            if (p.UnequipSlot(equipment_cursor)) PushToast("Unequipped.", Palette::TextDim);
            else PushToast("Nothing to remove, or your pack is full.", Palette::TextDim);
        }
    } else {
        if (input.MenuRight()) {
            if (inventory_cursor % COLS == COLS - 1) inventory_on_equipment = true;
            else inventory_cursor = std::min(inventory_cursor + 1, slots - 1);
        }
        if (input.MenuLeft())  inventory_cursor = std::max(inventory_cursor - 1, 0);
        if (input.MenuDown())  inventory_cursor = std::min(inventory_cursor + COLS, slots - 1);
        if (input.MenuUp())    inventory_cursor = std::max(inventory_cursor - COLS, 0);

        if (input.Pressed(Action::Confirm)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            if (!s.Empty()) {
                const ItemDef* def = items.Get(s.id);
                if (def && def->consumable) {
                    if (p.Eat(inventory_cursor)) PushToast("You eat the " + def->name + ".", Palette::Xp);
                    else PushToast("You are already at full health.", Palette::TextDim);
                } else if (def && def->slot != SLOT_NONE) {
                    string why;
                    if (p.EquipFromInventory(inventory_cursor, why))
                        PushToast("Equipped " + def->name + ".", Palette::Xp);
                    else
                        PushToast(why.empty() ? "You cannot equip that." : why,
                                  {235, 150, 120, 255});
                } else {
                    PushToast("Nothing happens.", Palette::TextDim);
                }
            }
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Inventory) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawInventory() {
    ui.Dim(0.5f);
    Player& p = world.player;

    const SDL_FRect panel = CenteredPanel(ui, 700.0f, 470.0f);
    ui.Panel(panel);
    ui.Text("Inventory", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight);

    char header[96];
    SDL_snprintf(header, sizeof(header), "%d coins    %d / %d slots used",
                 p.inventory.Coins(),
                 p.inventory.SlotCount() - p.inventory.FreeSlots(),
                 p.inventory.SlotCount());
    ui.Text(header, panel.x + panel.w - 24.0f, panel.y + 22.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // --- item grid -----------------------------------------------------------
    constexpr int COLS = 7;
    const float cell = 52.0f, gap = 6.0f;
    const float grid_x = panel.x + 24.0f, grid_y = panel.y + 62.0f;

    for (int i = 0; i < p.inventory.SlotCount(); ++i) {
        const int col = i % COLS, row = i / COLS;
        const SDL_FRect r = {grid_x + col * (cell + gap), grid_y + row * (cell + gap),
                             cell, cell};
        const bool selected = (!inventory_on_equipment && i == inventory_cursor);

        ui.Fill(r, {34, 27, 22, 235});
        ui.Outline(r, selected ? Palette::Highlight : Palette::BorderDim,
                   selected ? 2.0f : 1.0f);

        const ItemStack& s = p.inventory.Slot(i);
        if (s.Empty()) continue;

        const ItemDef* def = items.Get(s.id);
        SDL_Texture* tex = (def && !def->icon.empty()) ? textures->Get(def->icon) : nullptr;
        const SDL_FRect inner = {r.x + 6.0f, r.y + 6.0f, r.w - 12.0f, r.h - 12.0f};
        if (tex) SDL_RenderTexture(renderer, tex, nullptr, &inner);
        else     DrawItemPlaceholder(ui, def, s.id, inner);

        if (s.qty > 1)
            ui.TextShadowed(std::to_string(s.qty), r.x + r.w - 4.0f, r.y + r.h - 18.0f,
                            TextSize::Small, Palette::Highlight, Align::Right);
    }

    // --- equipment -----------------------------------------------------------
    const float eq_x = grid_x + COLS * (cell + gap) + 18.0f;
    ui.Text("Worn", eq_x, panel.y + 62.0f, TextSize::Body, Palette::Text);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const SDL_FRect r = {eq_x, panel.y + 92.0f + i * 30.0f, 236.0f, 26.0f};
        const bool selected = (inventory_on_equipment && i == equipment_cursor);
        ui.Fill(r, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(r, selected ? Palette::Highlight : Palette::BorderDim, 1.0f);

        const string& worn = p.equipment.InSlot(i);
        const ItemDef* def = worn.empty() ? nullptr : items.Get(worn);
        ui.Text(EquipSlotName(i), r.x + 8.0f, r.y + 4.0f, TextSize::Small, Palette::TextDim);
        ui.Text(def ? def->name : "-", r.x + r.w - 8.0f, r.y + 4.0f, TextSize::Small,
                def ? Palette::Text : Palette::TextDim, Align::Right);
    }

    // --- bonuses -------------------------------------------------------------
    // Two lines rather than one: robes and staves carry magic, and a bow
    // carries ranged, so leaving those off understated half the equipment.
    char bonus[128];
    SDL_snprintf(bonus, sizeof(bonus), "Attack +%d    Strength +%d    Defence +%d",
                 p.equipment.AttackBonus(), p.equipment.StrengthBonus(),
                 p.equipment.DefenceBonus());
    ui.Text(bonus, eq_x, panel.y + panel.h - 74.0f, TextSize::Small, Palette::Xp);

    SDL_snprintf(bonus, sizeof(bonus), "Ranged +%d    Magic +%d",
                 p.equipment.RangedBonus(), p.equipment.MagicBonus());
    ui.Text(bonus, eq_x, panel.y + panel.h - 58.0f, TextSize::Small, Palette::Xp);

    // --- selected item detail ------------------------------------------------
    const string sel_id = inventory_on_equipment
        ? p.equipment.InSlot(equipment_cursor)
        : p.inventory.Slot(inventory_cursor).id;

    if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id)) {
        const float y = grid_y + 4 * (cell + gap) + 12.0f;
        ui.Text(def->name, grid_x, y, TextSize::Body, Palette::Highlight);
        ui.TextWrapped(def->description, grid_x, y + 24.0f, COLS * (cell + gap) - 12.0f,
                       TextSize::Small, Palette::TextDim);
    }

    ui.Text(input.PromptFor(Action::Confirm) + " use / equip     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Skills
// =============================================================================

void Game::UpdateSkillsPanel() {
    MoveCursor(cursor, SKILL_COUNT);
    if (input.Pressed(Action::Back) || input.Pressed(Action::Skills) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawSkillsPanel() {
    ui.Dim(0.5f);
    const Skills& s = world.player.skills;

    const SDL_FRect panel = CenteredPanel(ui, 640.0f, 512.0f);
    ui.Panel(panel);
    ui.Text("Skills", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight);

    char header[128];
    SDL_snprintf(header, sizeof(header), "Combat %d    Total level %d    Total XP %lld",
                 s.CombatLevel(), s.TotalLevel(), s.TotalXp());
    ui.Text(header, panel.x + panel.w - 24.0f, panel.y + 22.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    const float row_h = 38.0f;
    for (int i = 0; i < SKILL_COUNT; ++i) {
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + i * row_h,
                               panel.w - 40.0f, row_h - 4.0f};
        const bool selected = (i == cursor);
        if (selected) {
            ui.Fill(row, {58, 46, 28, 200});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }

        const int level = s.Level(i);
        ui.Text(SkillName(i), row.x + 10.0f, row.y + 7.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        ui.Text(std::to_string(level), row.x + 170.0f, row.y + 7.0f, TextSize::Body,
                Palette::Text, Align::Right);

        // Progress toward the next level, the way the OSRS skill guide reads.
        const int xp = s.Xp(i);
        const int here = XpForLevel(level);
        const int next = XpForLevel(std::min(level + 1, MAX_SKILL_LEVEL));
        const float frac = (next > here) ? static_cast<float>(xp - here) / (next - here) : 1.0f;

        const SDL_FRect bar = {row.x + 190.0f, row.y + 10.0f, row.w - 320.0f, 14.0f};
        ui.Bar(bar, frac, Palette::Xp, {26, 34, 26, 235});

        char xp_text[48];
        if (level >= MAX_SKILL_LEVEL) SDL_snprintf(xp_text, sizeof(xp_text), "max");
        else SDL_snprintf(xp_text, sizeof(xp_text), "%d xp to %d", next - xp, level + 1);
        ui.Text(xp_text, row.x + row.w - 10.0f, row.y + 9.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
    }

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// =============================================================================
//  Quest log
// =============================================================================

void Game::UpdateQuestPanel() {
    vector<string> list = quests.Active();
    const vector<string> done = quests.Completed();
    list.insert(list.end(), done.begin(), done.end());

    MoveCursor(quest_cursor, static_cast<int>(list.size()));
    if (input.Pressed(Action::Back) || input.Pressed(Action::QuestLog) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawQuestPanel() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 460.0f);
    ui.Panel(panel);
    ui.Text("Quest Journal", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight);

    const vector<string> active = quests.Active();
    const vector<string> done   = quests.Completed();
    vector<string> list = active;
    list.insert(list.end(), done.begin(), done.end());

    if (list.empty()) {
        ui.Text("You have not taken on any tasks yet.",
                panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f - 20.0f,
                TextSize::Body, Palette::TextDim, Align::Center);
        ui.Text("Look for a mission board in town, or talk to the villagers.",
                panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        // Left: the list. Right: detail for whatever is highlighted.
        const float list_w = 290.0f;
        const float row_h = 34.0f;

        for (size_t i = 0; i < list.size() && i < 10; ++i) {
            const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + i * row_h,
                                   list_w, row_h - 4.0f};
            const bool selected = (static_cast<int>(i) == quest_cursor);
            const bool complete = (i >= active.size());

            if (selected) {
                ui.Fill(row, {58, 46, 28, 200});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests.Definition(list[i]);
            ui.Text(d ? d->name : list[i], row.x + 10.0f, row.y + 5.0f, TextSize::Small,
                    complete ? Palette::TextDim
                             : (selected ? Palette::Highlight : Palette::Text));
            if (complete)
                ui.Text("done", row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small,
                        Palette::Xp, Align::Right);
        }

        const int index = std::clamp(quest_cursor, 0, static_cast<int>(list.size()) - 1);
        if (const QuestDef* d = quests.Definition(list[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 28.0f;
            ui.Text("Suggested level " + std::to_string(d->recommended_level),
                    dx, y, TextSize::Small, Palette::TextDim);
            y += 24.0f;
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 12.0f;

            ui.Text("Objective", dx, y, TextSize::Small, Palette::Highlight);
            y += 20.0f;
            y += ui.TextWrapped(quests.CurrentObjectiveText(list[index]), dx, y, dw,
                                TextSize::Small, Palette::Text);
            y += 14.0f;

            if (!d->rewards.xp.empty() || d->rewards.coins > 0 || !d->rewards.items.empty()) {
                ui.Text("Rewards", dx, y, TextSize::Small, Palette::Highlight);
                y += 20.0f;
                for (const auto& xp : d->rewards.xp) {
                    ui.Text(std::to_string(xp.second) + " " + SkillName(xp.first) + " XP",
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                if (d->rewards.coins > 0) {
                    ui.Text(std::to_string(d->rewards.coins) + " coins", dx, y,
                            TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                for (const auto& it : d->rewards.items) {
                    const ItemDef* def = items.Get(it.first);
                    ui.Text(std::to_string(it.second) + "x " + (def ? def->name : it.first),
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
            }
        }
    }

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// =============================================================================
//  Dialogue
// =============================================================================

void Game::UpdateDialogue(float dt) {
    dialogue.Update(dt);

    DialogueContext dctx;
    dctx.quests    = &quests;
    dctx.inventory = &world.player.inventory;
    dctx.skills    = &world.player.skills;

    if (!dialogue.Active()) {
        for (auto& n : world.npcs) n->talking = false;
        SetState(GameState::Play);
        return;
    }

    const int option_count = static_cast<int>(dialogue.VisibleOptions().size());
    if (input.MenuDown()) dialogue.MoveSelection(1);
    if (input.MenuUp())   dialogue.MoveSelection(-1);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        // First press finishes the text reveal; the next one picks an answer.
        if (!dialogue.FullyRevealed()) {
            dialogue.SkipReveal();
        } else if (option_count == 0) {
            dialogue.End();
        } else {
            dialogue.Choose(dctx);
            HandleDialogueActions(dialogue.TakeActions());

            for (const string& id : quests.TakeJustStarted())
                if (const QuestDef* d = quests.Definition(id))
                    PushToast("Quest started: " + d->name, Palette::Xp);
            for (const string& id : quests.TakeJustCompleted())
                GrantQuestRewards(id);
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) dialogue.End();
}

void Game::DrawDialogue() {
    const DialogueNode* node = dialogue.Node();
    if (!node) return;

    const float box_h = 210.0f;
    const SDL_FRect box = {40.0f, ui.ViewHeight() - box_h - 30.0f,
                           ui.ViewWidth() - 80.0f, box_h};
    ui.Panel(box);

    // Speaker plate overlapping the top edge.
    const string speaker = dialogue.SpeakerName();
    if (!speaker.empty()) {
        const SDL_FPoint size = ui.Measure(speaker, TextSize::Body);
        const SDL_FRect plate = {box.x + 22.0f, box.y - 16.0f, size.x + 28.0f, 32.0f};
        ui.Fill(plate, Palette::PanelLight);
        ui.Outline(plate, Palette::Border, 1.0f);
        ui.Text(speaker, plate.x + 14.0f, plate.y + 5.0f, TextSize::Body, Palette::Highlight);
    }

    const float text_y = box.y + 28.0f;
    ui.TextWrapped(dialogue.RevealedText(), box.x + 24.0f, text_y, box.w - 48.0f,
                   TextSize::Body, Palette::Text);

    if (!dialogue.FullyRevealed()) {
        ui.Text(input.PromptFor(Action::Confirm) + " to skip",
                box.x + box.w - 22.0f, box.y + box.h - 26.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
        return;
    }

    const auto& options = dialogue.VisibleOptions();
    if (options.empty()) {
        ui.Text(input.PromptFor(Action::Confirm) + " to continue",
                box.x + box.w - 22.0f, box.y + box.h - 26.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
        return;
    }

    // Answers stack up from the bottom of the box.
    const float row_h = 26.0f;
    float y = box.y + box.h - 16.0f - options.size() * row_h;
    for (size_t i = 0; i < options.size(); ++i) {
        const bool selected = (static_cast<int>(i) == dialogue.Selected());
        if (selected)
            ui.Text(">", box.x + 26.0f, y, TextSize::Body, Palette::Highlight);
        ui.Text(options[i]->text, box.x + 46.0f, y, TextSize::Body,
                selected ? Palette::Highlight : Palette::TextDim);
        y += row_h;
    }
}

// =============================================================================
//  Mission board
// =============================================================================

void Game::UpdateBoard() {
    // Only offer what the player can actually take on right now.
    vector<string> available;
    for (const string& id : board_quests)
        if (quests.CanStart(id, world.player.skills)) available.push_back(id);

    MoveCursor(board_cursor, static_cast<int>(available.size()));

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !available.empty()) {
        const string& id = available[std::clamp(board_cursor, 0,
                                                static_cast<int>(available.size()) - 1)];
        if (quests.Start(id)) {
            for (const string& started : quests.TakeJustStarted())
                if (const QuestDef* d = quests.Definition(started))
                    PushToast("Quest started: " + d->name, Palette::Xp);
            board_cursor = 0;
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawBoard() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 720.0f, 440.0f);
    ui.Panel(panel);
    ui.Text(board_title.empty() ? "Mission Board" : board_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    vector<string> available;
    for (const string& id : board_quests)
        if (quests.CanStart(id, world.player.skills)) available.push_back(id);

    if (available.empty()) {
        ui.Text("Nothing new is pinned up today.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f - 20.0f, TextSize::Body, Palette::TextDim,
                Align::Center);
        ui.Text("Come back after you have finished what you already took on.",
                panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        const float row_h = 40.0f;
        const float list_w = 300.0f;

        for (size_t i = 0; i < available.size() && i < 8; ++i) {
            const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + i * row_h,
                                   list_w, row_h - 5.0f};
            const bool selected = (static_cast<int>(i) == board_cursor);
            if (selected) {
                ui.Fill(row, {58, 46, 28, 210});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests.Definition(available[i]);
            ui.Text(d ? d->name : available[i], row.x + 10.0f, row.y + 3.0f,
                    TextSize::Small, selected ? Palette::Highlight : Palette::Text);
            if (d)
                ui.Text("Lv " + std::to_string(d->recommended_level),
                        row.x + row.w - 8.0f, row.y + 3.0f, TextSize::Small,
                        Palette::TextDim, Align::Right);
        }

        const int index = std::clamp(board_cursor, 0, static_cast<int>(available.size()) - 1);
        if (const QuestDef* d = quests.Definition(available[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 30.0f;
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 14.0f;

            if (!d->rewards.xp.empty() || d->rewards.coins > 0) {
                ui.Text("Reward", dx, y, TextSize::Small, Palette::Highlight);
                y += 20.0f;
                for (const auto& xp : d->rewards.xp) {
                    ui.Text(std::to_string(xp.second) + " " + SkillName(xp.first) + " XP",
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                if (d->rewards.coins > 0)
                    ui.Text(std::to_string(d->rewards.coins) + " coins", dx, y,
                            TextSize::Small, Palette::Xp);
            }
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + " accept     " +
            input.PromptFor(Action::Back) + " leave",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Notes and signs
// =============================================================================

void Game::UpdateNote() {
    const bool offers_quest = !note_quest.empty() &&
                              quests.CanStart(note_quest, world.player.skills);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (offers_quest && quests.Start(note_quest)) {
            for (const string& started : quests.TakeJustStarted())
                if (const QuestDef* d = quests.Definition(started))
                    PushToast("Quest started: " + d->name, Palette::Xp);
        }
        SetState(GameState::Play);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawNote() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 380.0f);

    // Parchment, rather than the usual dark panel.
    ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
    ui.Fill(panel, {214, 197, 158, 250});
    ui.Outline(panel, {120, 96, 58, 255}, 2.0f);

    ui.Text(note_title, panel.x + panel.w / 2.0f, panel.y + 26.0f, TextSize::Large,
            {68, 48, 28, 255}, Align::Center);
    ui.Fill({panel.x + 40.0f, panel.y + 68.0f, panel.w - 80.0f, 1.0f}, {140, 116, 78, 255});

    ui.TextWrapped(note_text, panel.x + 40.0f, panel.y + 86.0f, panel.w - 80.0f,
                   TextSize::Body, {52, 38, 24, 255});

    const bool offers_quest = !note_quest.empty() &&
                              quests.CanStart(note_quest, world.player.skills);
    ui.Text(offers_quest ? (input.PromptFor(Action::Confirm) + " look into this")
                         : (input.PromptFor(Action::Confirm) + " put it away"),
            panel.x + panel.w / 2.0f, panel.y + panel.h - 34.0f, TextSize::Small,
            {96, 74, 44, 255}, Align::Center);
}


// =============================================================================
//  Crafting
// =============================================================================

void Game::UpdateCrafting() {
    const vector<const ItemDef*> recipes = items.Recipes();
    MoveCursor(craft_cursor, static_cast<int>(recipes.size()));

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !recipes.empty()) {
        const ItemDef* recipe = recipes[std::clamp(craft_cursor, 0,
                                                   static_cast<int>(recipes.size()) - 1)];
        Player& p = world.player;

        if (p.skills.Level(SKILL_CRAFTING) < recipe->craft_level) {
            PushToast("Needs Crafting " + std::to_string(recipe->craft_level) + ".",
                      {235, 150, 120, 255});
            return;
        }

        bool have_all = true;
        for (const auto& in : recipe->craft_inputs)
            if (!p.inventory.Has(in.first, in.second)) { have_all = false; break; }

        if (!have_all) {
            PushToast("You are missing materials.", {235, 150, 120, 255});
            return;
        }
        if (p.inventory.FreeSlots() == 0) {
            PushToast("Your pack is full.", {235, 150, 120, 255});
            return;
        }

        for (const auto& in : recipe->craft_inputs) p.inventory.Remove(in.first, in.second);
        p.inventory.Add(recipe->craft_result, recipe->craft_qty);
        p.GrantXp(SKILL_CRAFTING, recipe->craft_xp);

        const ItemDef* made = items.Get(recipe->craft_result);
        PushToast("Crafted " + (made ? made->name : recipe->craft_result) + ".", Palette::Xp);
        quests.RefreshCollectObjectives(p.inventory);
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawCrafting() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 420.0f);
    ui.Panel(panel);
    ui.Text(craft_title.empty() ? "Workbench" : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world.player;
    ui.Text("Crafting " + std::to_string(p.skills.Level(SKILL_CRAFTING)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    const vector<const ItemDef*> recipes = items.Recipes();
    if (recipes.empty()) {
        ui.Text("Nothing to make here.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f, TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    const float list_w = 260.0f, row_h = 34.0f;
    for (size_t i = 0; i < recipes.size() && i < 9; ++i) {
        const ItemDef* r = recipes[i];
        const ItemDef* made = items.Get(r->craft_result);
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + i * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (static_cast<int>(i) == craft_cursor);
        const bool unlocked = p.skills.Level(SKILL_CRAFTING) >= r->craft_level;

        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        ui.Text(made ? made->name : r->craft_result, row.x + 10.0f, row.y + 5.0f,
                TextSize::Small,
                !unlocked ? SDL_Color{120, 110, 100, 255}
                          : (selected ? Palette::Highlight : Palette::Text));
        ui.Text("Lv " + std::to_string(r->craft_level), row.x + row.w - 8.0f,
                row.y + 5.0f, TextSize::Small, Palette::TextDim, Align::Right);
    }

    // Detail for the highlighted recipe: what it needs and what you hold.
    const int index = std::clamp(craft_cursor, 0, static_cast<int>(recipes.size()) - 1);
    const ItemDef* r = recipes[index];
    const ItemDef* made = items.Get(r->craft_result);
    const float dx = panel.x + list_w + 40.0f;
    float y = panel.y + 62.0f;

    ui.Text(made ? made->name : r->craft_result, dx, y, TextSize::Body, Palette::Highlight);
    y += 28.0f;
    if (made) y += ui.TextWrapped(made->description, dx, y, panel.w - list_w - 64.0f,
                                  TextSize::Small, Palette::TextDim) + 8.0f;

    ui.Text("Materials", dx, y, TextSize::Small, Palette::Highlight);
    y += 20.0f;
    for (const auto& in : r->craft_inputs) {
        const ItemDef* mat = items.Get(in.first);
        const int held = p.inventory.Count(in.first);
        const bool enough = held >= in.second;
        char line[128];
        SDL_snprintf(line, sizeof(line), "%s  %d / %d",
                     (mat ? mat->name.c_str() : in.first.c_str()), held, in.second);
        ui.Text(line, dx, y, TextSize::Small,
                enough ? Palette::Xp : SDL_Color{225, 130, 120, 255});
        y += 18.0f;
    }

    y += 10.0f;
    ui.Text(std::to_string(r->craft_xp) + " Crafting XP", dx, y, TextSize::Small,
            Palette::TextDim);

    ui.Text(input.PromptFor(Action::Confirm) + " craft     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Death
// =============================================================================

void Game::UpdateDeath(float dt) {
    (void)dt;
    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        // Respawn at the town, keeping progress, the way a forgiving RPG does.
        world.player.Respawn(0.0f, 0.0f);
        if (!world.LoadMap("town_havenbrook", "respawn", ctx))
            world.LoadMap("overworld", "start", ctx);
        world.player.Respawn(world.player.x, world.player.y);
        SetState(GameState::Play);
        PushToast("You wake in Havenbrook, aching but alive.", Palette::TextDim);
    }
}

void Game::DrawDeath() {
    ui.Dim(0.72f);
    const float cx = ui.ViewWidth() / 2.0f;
    ui.Text("You have fallen", cx, ui.ViewHeight() * 0.38f, TextSize::Title,
            {206, 86, 76, 255}, Align::Center);
    ui.Text("Your skills and belongings remain with you.", cx,
            ui.ViewHeight() * 0.38f + 58.0f, TextSize::Body, Palette::TextDim, Align::Center);
    ui.Text(input.PromptFor(Action::Confirm) + " to return to Havenbrook", cx,
            ui.ViewHeight() * 0.38f + 96.0f, TextSize::Body, Palette::Highlight, Align::Center);
}
