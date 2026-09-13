#include "../systems/gathering.h"
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
        case 1:  return "Keyboard";
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
            // 64px frames with a figure about twenty pixels wide in the middle,
            // so this needs to be large for the three to be told apart.
            const SDL_FRect dst = {card.x + card.w / 2.0f - 96.0f, card.y + 16.0f, 192.0f, 192.0f};
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
    // Starting a new game on top of a save throws that save away, and the
    // slot list's cursor starts on slot 1 -- which is where the first game
    // always went. One Enter too many used to be enough to lose it.
    if (overwrite_slot >= 0) {
        if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
            const int slot = overwrite_slot;
            overwrite_slot = -1;
            NewGame(pending_character, slot);
        } else if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
            overwrite_slot = -1;
        }
        return;
    }

    MoveCursor(cursor, SAVE_SLOTS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const int slot = cursor + 1;
        if (slot_purpose == 0) {
            if (SaveSystem::Exists(slot)) overwrite_slot = slot;
            else                          NewGame(pending_character, slot);
        } else {
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

    if (overwrite_slot >= 0) {
        ui.Dim(0.6f);
        const SDL_FRect box = CenteredPanel(ui, 440.0f, 150.0f);
        ui.Panel(box);
        const float cx = box.x + box.w / 2.0f;
        ui.Text("Slot " + std::to_string(overwrite_slot) + " already holds a saved game.",
                cx, box.y + 24.0f, TextSize::Body, Palette::Highlight, Align::Center);
        ui.Text("Starting over here will erase it.", cx, box.y + 56.0f, TextSize::Small,
                Palette::Text, Align::Center);
        ui.Text(input.PromptFor(Action::Confirm) + " erase and start     " +
                input.PromptFor(Action::Back) + " keep it",
                cx, box.y + box.h - 36.0f, TextSize::Small, Palette::TextDim, Align::Center);
    }
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
    constexpr int ROWS = 10;
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
            case 6: case 7: case 8: {
                float& v = (cursor == 6) ? settings.master_volume
                         : (cursor == 7) ? settings.sfx_volume : settings.ambience_volume;
                // Confirm cycles, wrapping back to silent after full.
                if (delta != 0) v = std::clamp(roundf((v + delta * 0.1f) * 10.0f) / 10.0f, 0.0f, 1.0f);
                else            v = (v >= 0.95f) ? 0.0f : roundf((v + 0.1f) * 10.0f) / 10.0f;
                Audio::SetVolumes(settings.master_volume, settings.sfx_volume,
                                  settings.ambience_volume);
                Audio::Play(cursor == 8 ? Sfx::Pickup : Sfx::Hit, 0.8f);
                break;
            }
            case 9:
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

static string VolumeLabel(float v) {
    const int pct = static_cast<int>(roundf(v * 100.0f));
    return pct == 0 ? string("Off") : std::to_string(pct) + "%";
}

void Game::DrawOptions() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 552.0f);
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
        {"Master Volume",  VolumeLabel(settings.master_volume)},
        {"Effects Volume", VolumeLabel(settings.sfx_volume)},
        {"Ambience Volume", VolumeLabel(settings.ambience_volume)},
        {"Back",           ""},
    };

    const float row_h = 42.0f;
    for (int i = 0; i < 10; ++i) {
        const SDL_FRect row = {panel.x + 16.0f, panel.y + 62.0f + i * row_h,
                               panel.w - 32.0f, row_h - 4.0f};
        ui.MenuItem(row, rows[i].first, i == cursor, true, rows[i].second);
    }

    ui.Text("Controller: " + pad_note, panel.x + panel.w / 2.0f,
            panel.y + panel.h - 52.0f, TextSize::Small, Palette::TextDim, Align::Center);
    ui.Text("Left / Right to change     " + input.PromptFor(Action::Back) + " back",
            panel.x + panel.w / 2.0f,
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

// The minimap bezel in assets/ui is 144 across and its glass 124, and the
// right-hand column of the HUD -- toasts, then the quest tracker -- starts
// below the whole dial and the map name under it.
static constexpr float kHudMargin    = 16.0f;
static constexpr float kMinimapRing  = 144.0f;
static constexpr float kMinimapGlass = 62.0f;
static constexpr float kHudRightTop  = kHudMargin + kMinimapRing + 24.0f;

void Game::DrawHud() {
    const Player& p = world.player;

    // --- vitals --------------------------------------------------------------
    // Health and mana in brass plates, each with a glyph at the left end, so
    // the two are told apart at a glance rather than by colour alone.
    const float glyph = 22.0f;
    const float line_h = ui.LineHeight(TextSize::Small);

    auto glyph_plate = [&](const SDL_FRect& box, const char* icon) {
        ui.Fill(box, {30, 22, 15, 255});
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w - 2.0f, box.h - 2.0f}, {124, 98, 52, 255});
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w - 2.0f, 1.0f}, {186, 156, 92, 255});
        ui.Fill({box.x + 1.0f, box.y + box.h - 2.0f, box.w - 2.0f, 1.0f}, {74, 56, 28, 255});
        ui.Fill({box.x + 2.0f, box.y + 2.0f, box.w - 4.0f, box.h - 4.0f}, {24, 18, 15, 255});
        if (SDL_Texture* tex = textures->Get(icon)) {
            const float s = box.h - 6.0f;
            const SDL_FRect dst = {roundf(box.x + (box.w - s) / 2.0f),
                                   roundf(box.y + (box.h - s) / 2.0f), s, s};
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    };

    const SDL_FRect hp_bar = {18.0f + glyph + 4.0f, 16.0f, 232.0f, glyph};
    glyph_plate({18.0f, hp_bar.y, glyph, glyph}, "assets/icons/hud_heart.png");
    ui.FramedBar(hp_bar, p.max_hp > 0 ? static_cast<float>(p.hp) / p.max_hp : 0.0f,
                 Palette::Health, Palette::HealthBack);
    char hp_text[32];
    SDL_snprintf(hp_text, sizeof(hp_text), "%d / %d", p.hp, p.max_hp);
    ui.TextShadowed(hp_text, hp_bar.x + hp_bar.w / 2.0f,
                    hp_bar.y + (hp_bar.h - line_h) / 2.0f,
                    TextSize::Small, Palette::Text, Align::Center);

    // --- mana ----------------------------------------------------------------
    // Only shown once the player has any, so a pure melee character is not
    // told about a resource they never spend.
    float meta_y = hp_bar.y + hp_bar.h + 8.0f;
    if (p.MaxMana() > 0) {
        const float mana_h = 16.0f;
        const SDL_FRect mana_bar = {hp_bar.x, hp_bar.y + hp_bar.h + 4.0f, 232.0f, mana_h};
        glyph_plate({18.0f, mana_bar.y, glyph, mana_h}, "assets/icons/hud_drop.png");
        ui.FramedBar(mana_bar, static_cast<float>(p.Mana()) / p.MaxMana(),
                     Palette::Mana, Palette::ManaBack);
        char mana_text[32];
        SDL_snprintf(mana_text, sizeof(mana_text), "%d / %d", p.Mana(), p.MaxMana());
        ui.TextShadowed(mana_text, mana_bar.x + mana_bar.w / 2.0f,
                        mana_bar.y + (mana_bar.h - line_h) / 2.0f,
                        TextSize::Small, Palette::Text, Align::Center);
        meta_y = mana_bar.y + mana_bar.h + 6.0f;
    }

    // --- stamina -------------------------------------------------------------
    // Under whichever bar is last, and slimmer, since it changes all the time
    // and wants to be glanced at rather than read. Winded, the bar pulses red
    // until it has refilled far enough to sprint again.
    {
        const float st_h = 14.0f;
        const float top = meta_y - 2.0f;
        const SDL_FRect st_bar = {hp_bar.x, top, 232.0f, st_h};
        glyph_plate({18.0f, st_bar.y, glyph, st_h}, "assets/icons/hud_stamina.png");
        SDL_Color fill = Palette::Stamina;
        if (p.Winded()) {
            const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) * 0.012f);
            fill = {static_cast<Uint8>(196 + 40 * pulse), static_cast<Uint8>(70 + 30 * pulse),
                    static_cast<Uint8>(48), 255};
        }
        ui.FramedBar(st_bar, p.Stamina() / p.MaxStamina(), fill, Palette::StaminaBack);
        if (p.Winded())
            ui.TextShadowed("winded", st_bar.x + st_bar.w / 2.0f,
                            st_bar.y + (st_bar.h - line_h) / 2.0f,
                            TextSize::Small, Palette::Text, Align::Center);
        meta_y = st_bar.y + st_bar.h + 6.0f;
    }

    // --- minimap -------------------------------------------------------------
    // Top right, with the bezel hung on the corner; everything else that used
    // to live in that corner now stacks below it.
    minimap.Draw(renderer, *textures, ui, world,
                 ui.ViewWidth() - kHudMargin - kMinimapRing / 2.0f,
                 kHudMargin + kMinimapRing / 2.0f, kMinimapGlass);

    char meta[96];
    SDL_snprintf(meta, sizeof(meta), "Combat %d    %d coins",
                 p.skills.CombatLevel(), p.inventory.Coins());
    ui.TextShadowed(meta, hp_bar.x, meta_y, TextSize::Small, Palette::TextDim);

    // --- the time ------------------------------------------------------------
    // A sun or a moon and the hour, under the vitals. In a dream it counts
    // down to dawn instead, which is when the dream ends.
    {
        const WorldClock& c = world.clock;
        const float y = meta_y + line_h + 4.0f;
        const bool moon = world.InDream() || c.IsNight() || string(c.Phase()) == "Dusk";
        glyph_plate({18.0f, y - 2.0f, glyph, line_h + 4.0f},
                    moon ? "assets/icons/hud_moon.png" : "assets/icons/hud_sun.png");
        char line[96];
        SDL_Color col = Palette::TextDim;
        if (world.InDream()) {
            const int s = static_cast<int>(c.SecondsToDawn());
            SDL_snprintf(line, sizeof(line), "Dreaming    dawn in %d:%02d", s / 60, s % 60);
            col = {206, 186, 250, 255};
        } else {
            SDL_snprintf(line, sizeof(line), "Day %d    %s    %s", c.Day(), c.TimeText().c_str(), c.Phase());
            if (c.CanSleep()) col = {176, 186, 236, 255};
        }
        ui.TextShadowed(line, hp_bar.x, y, TextSize::Small, col);
    }

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
                // A staff's technique rides on the same line as the spell.
                if (const TalentNode* t = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique()))
                    line += "     hold " + input.PromptFor(Action::StrongAttack) + ": " + t->name;
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
        }

        // The technique a held heavy attack will come out as, from the tree --
        // on the line under the prompts, where the bow says it is drawn.
        const TalentNode* tech = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique());
        if (tech && style != AttackStyle::Magic) {
            ui.TextShadowed("Hold " + input.PromptFor(Action::StrongAttack) + ": " + tech->name,
                            ui.ViewWidth() / 2.0f, ui.ViewHeight() - 46.0f, TextSize::Small,
                            {236, 150, 110, 255}, Align::Center);
        } else if (style == AttackStyle::Ranged) {
            ui.TextShadowed("Bow drawn", ui.ViewWidth() / 2.0f,
                            ui.ViewHeight() - 46.0f, TextSize::Small,
                            Palette::TextDim, Align::Center);
        }
    }

    // --- target frame ----------------------------------------------------------
    // Who the fight is with, at the top of the screen: name, level and health,
    // with a red frame and a LOCKED tag while the lock is on. Out of combat
    // there is no target, and nothing is drawn.
    if (live) {
        if (const Enemy* t = world.targeting.Current(); t && t->Def()) {
            const bool lock = world.targeting.IsLocked();
            const float w = 280.0f, cx = ui.ViewWidth() / 2.0f;
            const SDL_FRect box = {roundf(cx - w / 2.0f), 12.0f, w, 48.0f};
            ui.Fill(box, {14, 11, 9, 200});
            ui.Outline(box, lock ? SDL_Color{206, 70, 56, 255} : Palette::BorderDim, lock ? 2.0f : 1.0f);
            ui.TextShadowed(t->Def()->name, box.x + 12.0f, box.y + 5.0f, TextSize::Small,
                            lock ? Palette::Highlight : Palette::Text);
            ui.TextShadowed(lock ? "LOCKED   Lv " + std::to_string(t->level)
                                 : input.PromptFor(Action::Target) + " lock   Lv " + std::to_string(t->level),
                            box.x + box.w - 12.0f, box.y + 5.0f, TextSize::Small,
                            lock ? SDL_Color{236, 110, 90, 255} : Palette::TextDim, Align::Right);
            ui.FramedBar({box.x + 12.0f, box.y + 27.0f, box.w - 24.0f, 12.0f}, t->HealthFraction(),
                         {196, 44, 40, 255}, {40, 16, 14, 255});
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

    // --- attack cooldown -----------------------------------------------------
    // A gate the player cannot see is just an unresponsive button. This is
    // deliberately small and quiet -- it drains rather than fills, sits under
    // the feet, and is gone inside a fifth of a second between light attacks --
    // because the point is to make the rhythm legible, not to put a cooldown
    // bar in the middle of a fight.
    if (live && !p.IsCharging() && p.CooldownProgress() > 0.0f) {
        const float t = p.CooldownProgress();
        const SDL_FPoint anchor = world.camera.ToScreen(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 20.0f, anchor.y + 9.0f, 40.0f, 3.0f};
        ui.Bar(bar, t, SDL_Color{188, 170, 140, 190}, {26, 22, 18, 150});
    }

    // --- ledge prompt --------------------------------------------------------
    // A climbable ledge is otherwise indistinguishable from a wall you cannot
    // pass: you walk into it and stop either way. Pushing against one says what
    // the jump button will do there, which is the only way anyone would guess.
    if (live && !p.ClimbHint().empty() && p.interact.kind == InteractTarget::None) {
        const string prompt = "[" + input.PromptFor(Action::Jump) + "]  " + p.ClimbHint();
        const SDL_FPoint size = ui.Measure(prompt, TextSize::Body);
        const SDL_FRect box = {ui.ViewWidth() / 2.0f - size.x / 2.0f - 14.0f,
                               ui.ViewHeight() - 92.0f, size.x + 28.0f, size.y + 12.0f};
        ui.Panel(box);
        ui.Text(prompt, box.x + 14.0f, box.y + 6.0f, TextSize::Body, Palette::Highlight);
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

    // --- the ways out ---------------------------------------------------------
    // An exit at the edge of an outdoor map is a gap in the trees, and easy to
    // walk straight past. Near one, say where it goes and which way.
    if (live && !world.CurrentMap().IsInterior()) {
        const Map& mp = world.CurrentMap();
        for (const Portal& portal : mp.Portals()) {
            if (portal.requires_interact) continue;
            const float px = portal.rect.x + portal.rect.w / 2.0f;
            const float py = portal.rect.y + portal.rect.h / 2.0f;
            const float d = Length(px - p.x, py - p.y);
            if (d > 260.0f) continue;
            const float fade = std::clamp((260.0f - d) / 100.0f, 0.0f, 1.0f);

            // Which edge the exit is on decides the arrow.
            const float to_l = px, to_r = mp.Width() - px, to_t = py, to_b = mp.Height() - py;
            const float nearest = std::min({to_l, to_r, to_t, to_b});
            string text;
            float nx = 0.0f, ny = 0.0f;
            if (nearest == to_l)      { text = "< " + portal.label; nx = -1.0f; }
            else if (nearest == to_r) { text = portal.label + " >"; nx =  1.0f; }
            else if (nearest == to_t) { text = "^ " + portal.label; ny = -1.0f; }
            else                      { text = "v " + portal.label; ny =  1.0f; }

            SDL_FPoint s = world.camera.ToScreen(px, py);
            const SDL_FPoint size = ui.Measure(text, TextSize::Small);
            s.x = std::clamp(s.x - nx * 90.0f, size.x / 2.0f + 12.0f, ui.ViewWidth() - size.x / 2.0f - 12.0f);
            s.y = std::clamp(s.y - ny * 70.0f, 40.0f, ui.ViewHeight() - 60.0f);

            ui.Fill({roundf(s.x - size.x / 2.0f - 8.0f), roundf(s.y - 4.0f), size.x + 16.0f, size.y + 8.0f},
                    {14, 11, 9, static_cast<Uint8>(170.0f * fade)});
            SDL_Color c = Palette::Highlight;
            c.a = static_cast<Uint8>(255.0f * fade);
            ui.TextShadowed(text, s.x, s.y, TextSize::Small, c, Align::Center);
        }
    }

    // --- zone banner -----------------------------------------------------------
    if (banner_active) {
        const float in  = std::clamp(banner_time / 0.6f, 0.0f, 1.0f);
        const float out = std::clamp((4.2f - banner_time) / 1.0f, 0.0f, 1.0f);
        const float a   = std::min(in, out);
        const float cx  = ui.ViewWidth() / 2.0f;
        const float y   = 96.0f - (1.0f - in) * 12.0f;

        const float tw = ui.Measure(banner_title, TextSize::Title).x;
        const float rule_y = roundf(y + ui.LineHeight(TextSize::Title) + 6.0f);

        // A dark band behind the words, feathered at both ends. Gold on the
        // foothills' sand and on sunlit grass was close to unreadable, and a
        // shadow alone did not carry a line of text that size.
        {
            const float sw = banner_subtitle.empty() ? 0.0f
                                                     : ui.Measure(banner_subtitle, TextSize::Body).x;
            const float core = std::max(tw, sw) + 60.0f;
            const float top = roundf(y - 12.0f);
            const float bottom = rule_y + (banner_subtitle.empty() ? 14.0f
                                                                    : 16.0f + ui.LineHeight(TextSize::Body));
            const Uint8 band = static_cast<Uint8>(130.0f * a);
            ui.Fill({roundf(cx - core / 2.0f), top, roundf(core), bottom - top}, {12, 10, 8, band});
            constexpr int FEATHER = 16;
            constexpr float STEP = 7.0f;
            for (int i = 0; i < FEATHER; ++i) {
                const Uint8 fa = static_cast<Uint8>(band * (1.0f - (i + 1.0f) / (FEATHER + 1.0f)));
                const float off = core / 2.0f + i * STEP;
                ui.Fill({roundf(cx - off - STEP), top, STEP, bottom - top}, {12, 10, 8, fa});
                ui.Fill({roundf(cx + off), top, STEP, bottom - top}, {12, 10, 8, fa});
            }
        }

        SDL_Color title = Palette::Highlight;
        title.a = static_cast<Uint8>(255.0f * a);
        ui.TextShadowed(banner_title, cx, y, TextSize::Title, title, Align::Center);

        const Uint8 ra = static_cast<Uint8>(170.0f * a);
        ui.Fill({roundf(cx - tw / 2.0f - 28.0f), rule_y, tw + 56.0f, 1.0f}, {242, 200, 96, ra});
        ui.Fill({roundf(cx - 5.0f), rule_y - 2.0f, 10.0f, 5.0f}, {242, 200, 96, static_cast<Uint8>(230.0f * a)});
        if (!banner_subtitle.empty()) {
            SDL_Color sub_c = Palette::Text;
            sub_c.a = static_cast<Uint8>(235.0f * a);
            ui.TextShadowed(banner_subtitle, cx, rule_y + 10.0f, TextSize::Body, sub_c, Align::Center);
        }
    }

    // --- quest tracker -------------------------------------------------------
    const vector<string> active = quests.Active();
    if (!active.empty()) {
        const float right = ui.ViewWidth() - 18.0f;
        // Sits below the minimap, and below however many toasts are stacked.
        float y = kHudRightTop + toasts.size() * 20.0f;
        const size_t shown = std::min<size_t>(active.size(), 3);

        // A dark backing sized to the text. The dim objective lines were
        // unreadable over the pale olive grass and the road, shadow or not.
        {
            float widest = ui.Measure("QUESTS", TextSize::Small).x;
            for (size_t i = 0; i < shown; ++i) {
                if (const QuestDef* d = quests.Definition(active[i])) {
                    widest = std::max(widest, ui.Measure(d->name, TextSize::Small).x);
                    widest = std::max(widest, ui.Measure(quests.CurrentObjectiveText(active[i]),
                                                         TextSize::Small).x);
                }
            }
            const SDL_FRect back = {right - widest - 10.0f, y - 6.0f, widest + 20.0f,
                                    22.0f + shown * 42.0f + 2.0f};
            ui.Fill(back, {14, 11, 9, 150});
        }

        ui.TextShadowed("QUESTS", right, y, TextSize::Small, Palette::Highlight, Align::Right);
        y += 22.0f;

        // Show at most three so the tracker never crowds the view.
        for (size_t i = 0; i < shown; ++i) {
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

    const string hint = spell_hint +
                        input.PromptFor(Action::LightAttack) + " attack    " +
                        input.PromptFor(Action::StrongAttack) + " heavy    " +
                        input.PromptFor(Action::Target) + " target    " +
                        input.PromptFor(Action::Sprint) + " sprint    " +
                        input.PromptFor(Action::Inventory) + " bag    " +
                        input.PromptFor(Action::Skills) + " skills    " +
                        input.PromptFor(Action::QuestLog) + " quests    " +
                        input.PromptFor(Action::Pause) + " menu";
    ui.TextShadowed(hint, 18.0f, ui.ViewHeight() - 28.0f, TextSize::Small, Palette::TextDim);
}

void Game::DrawToasts() {
    // Under the minimap while a game is running; at the top on the menus,
    // where there is no minimap to clear.
    float y = InGameplayState() ? kHudRightTop : 18.0f;
    const float right = ui.ViewWidth() - 18.0f;

    for (const Toast& t : toasts) {
        SDL_Color c = t.color;
        c.a = static_cast<Uint8>(255 * std::clamp(t.life / 0.6f, 0.0f, 1.0f));
        ui.TextShadowed(t.text, right, y, TextSize::Small, c, Align::Right);
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
            if (p.UnequipSlot(equipment_cursor)) { PushToast("Unequipped.", Palette::TextDim); Audio::Play(Sfx::Equip, 0.7f); }
            else PushToast("Nothing to remove, or your pack is full.", Palette::TextDim);
        }
    } else {
        const int grid_before = inventory_cursor;
        if (input.MenuRight()) {
            if (inventory_cursor % COLS == COLS - 1) inventory_on_equipment = true;
            else inventory_cursor = std::min(inventory_cursor + 1, slots - 1);
        }
        if (input.MenuLeft())  inventory_cursor = std::max(inventory_cursor - 1, 0);
        if (input.MenuDown())  inventory_cursor = std::min(inventory_cursor + COLS, slots - 1);
        if (input.MenuUp())    inventory_cursor = std::max(inventory_cursor - COLS, 0);
        if (inventory_cursor != grid_before || inventory_on_equipment) Audio::Play(Sfx::UiMove);

        if (input.Pressed(Action::Confirm)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            if (!s.Empty()) {
                const ItemDef* def = items.Get(s.id);
                if (def && def->consumable) {
                    if (p.Eat(inventory_cursor)) {
                        PushToast("You eat the " + def->name + ".", Palette::Xp);
                        Audio::Play(Sfx::Eat);
                    } else {
                        PushToast("You are already at full health.", Palette::TextDim);
                    }
                } else if (def && def->use == "camp") {
                    const string why = world.PitchCamp(inventory_cursor, ctx);
                    if (why.empty()) {
                        PushToast("You pitch camp. After dusk you can sleep here.", Palette::Xp);
                        SetState(GameState::Play);
                        return;
                    }
                    PushToast(why, {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                } else if (def && def->slot != SLOT_NONE) {
                    string why;
                    if (p.EquipFromInventory(inventory_cursor, why)) {
                        PushToast("Equipped " + def->name + ".", Palette::Xp);
                        Audio::Play(Sfx::Equip);
                    } else {
                        PushToast(why.empty() ? "You cannot equip that." : why,
                                  {235, 150, 120, 255});
                        Audio::Play(Sfx::UiError);
                    }
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
    //
    // Laid out as two columns under the worn list. All three melee bonuses on
    // one line ran past the right edge of the panel once any of them reached
    // two digits, which the starting sword and shield already do.
    const float bonus_y = panel.y + 92.0f + static_cast<float>(SLOT_COUNT) * 30.0f + 2.0f;
    const float col2_x  = eq_x + 118.0f;
    const auto bonus_cell = [&](const char* label, int value, float x, float y) {
        char buf[48];
        SDL_snprintf(buf, sizeof(buf), "%s +%d", label, value);
        ui.Text(buf, x, y, TextSize::Small, Palette::Xp);
    };
    bonus_cell("Attack",   p.equipment.AttackBonus(),   eq_x,   bonus_y);
    bonus_cell("Strength", p.equipment.StrengthBonus(), col2_x, bonus_y);
    bonus_cell("Defence",  p.equipment.DefenceBonus(),  eq_x,   bonus_y + 16.0f);
    bonus_cell("Ranged",   p.equipment.RangedBonus(),   col2_x, bonus_y + 16.0f);
    bonus_cell("Magic",    p.equipment.MagicBonus(),    eq_x,   bonus_y + 32.0f);

    char bonus[128];

    // Attack speed is stated as a rate rather than as the raw multiplier: the
    // underlying number is a multiplier on swing time, so lower is faster,
    // and a stat where smaller is better wants explaining every time it is
    // read. "Swings 1.25x" does not.
    {
        const float sp = p.WeaponSpeed();
        // Bands chosen against the actual spread in data/items.json: bows sit
        // at 0.80-0.88 and daggers at 0.70-0.80, so a threshold of 0.85 called
        // a shortbow "even". Anything a tenth either side of the baseline is
        // worth naming.
        const char* word = sp < 0.92f ? "fast" : (sp > 1.08f ? "slow" : "even");
        SDL_snprintf(bonus, sizeof(bonus), "Attack speed  %.2fx  (%s)", 1.0f / sp, word);
        ui.Text(bonus, eq_x, bonus_y + 50.0f, TextSize::Small, Palette::TextDim);
    }

    // --- selected item detail ------------------------------------------------
    const string sel_id = inventory_on_equipment
        ? p.equipment.InSlot(equipment_cursor)
        : p.inventory.Slot(inventory_cursor).id;

    if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id)) {
        const float y = grid_y + 4 * (cell + gap) + 12.0f;
        ui.Text(def->name, grid_x, y, TextSize::Body, Palette::Highlight);
        // The tier and what it needs, on the right of the name.
        string tag;
        if (const TierDef* t = def->tier.empty() ? nullptr : items.Tier(def->tier)) tag = t->name + " tier";
        for (const auto& rq : def->requirements) {
            const bool met = p.skills.Level(rq.first) >= rq.second;
            tag += (tag.empty() ? "" : "   ") + string(met ? "" : "needs ") + SkillName(rq.first) +
                   " " + std::to_string(rq.second);
        }
        if (!tag.empty())
            ui.Text(tag, grid_x + COLS * (cell + gap) - 12.0f, y + 4.0f, TextSize::Small, Palette::TextDim, Align::Right);
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
    if (state_time <= 0.0f) tree_reset_armed = false;

    // I and O (the shoulder buttons on a pad) step between the level list and
    // the three trees; the panel closes with Back.
    const int tabs = 4;
    if (input.Pressed(Action::Inventory)) { skills_tab = (skills_tab + tabs - 1) % tabs; tree_reset_armed = false; Audio::Play(Sfx::UiMove); }
    if (input.Pressed(Action::Skills))    { skills_tab = (skills_tab + 1) % tabs;        tree_reset_armed = false; Audio::Play(Sfx::UiMove); }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Play);
        return;
    }

    if (skills_tab == 0) {
        MoveCursor(cursor, SKILL_COUNT);
        return;
    }

    const AttackStyle style = static_cast<AttackStyle>(skills_tab - 1);
    const TalentTree& tree = skill_trees.Tree(style);
    Player& p = world.player;

    const int b0 = tree_branch, r0 = tree_row;
    if (input.MenuLeft())  tree_branch = (tree_branch + SkillTrees::BRANCHES - 1) % SkillTrees::BRANCHES;
    if (input.MenuRight()) tree_branch = (tree_branch + 1) % SkillTrees::BRANCHES;
    if (input.MenuUp())    tree_row = std::max(0, tree_row - 1);
    if (input.MenuDown())  tree_row = std::min(SkillTrees::ROWS - 1, tree_row + 1);
    if (b0 != tree_branch || r0 != tree_row) Audio::Play(Sfx::UiMove);

    const TalentNode* node = tree.At(tree_branch, tree_row);
    if (node && input.Pressed(Action::Confirm)) {
        tree_reset_armed = false;
        if (p.talents.Has(node->id)) {
            if (!node->technique.empty() && p.talents.ToggleTechnique(node->id)) {
                const bool on = p.talents.Technique(style) == node->technique;
                PushToast(on ? node->name + " is your charged attack now."
                             : "Back to a plain charged attack.", Palette::Xp);
                Audio::Play(Sfx::Equip);
            }
        } else {
            switch (p.talents.CanLearn(node->id, p.skills)) {
                case Talents::Why::Ok:
                    p.talents.Learn(node->id, p.skills);
                    PushToast("Learned " + node->name + ".", Palette::Highlight);
                    Audio::Play(Sfx::QuestStart);
                    break;
                case Talents::Why::Level:
                    PushToast("Needs " + string(SkillName(tree.skill)) + " " +
                              std::to_string(node->level) + ".", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                case Talents::Why::Prerequisite:
                    PushToast("Learn the one above it first.", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                case Talents::Why::NoPoints:
                    PushToast("No points left. One comes every " +
                              std::to_string(SkillTrees::LEVELS_PER_POINT) + " " +
                              SkillName(tree.skill) + " levels.", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                default: break;
            }
        }
    }

    // Unlearning a tree takes two presses, so it cannot happen by accident.
    if (input.Pressed(Action::Target)) {
        if (!tree_reset_armed) {
            tree_reset_armed = true;
            PushToast("Press " + input.PromptFor(Action::Target) + " again to unlearn the " +
                      tree.name + " tree.", {235, 190, 120, 255});
        } else {
            p.talents.Reset(style);
            tree_reset_armed = false;
            PushToast(tree.name + " tree unlearned. Its points are free again.", Palette::TextDim);
            Audio::Play(Sfx::UiBack);
        }
    }
}

void Game::DrawSkillsPanel() {
    ui.Dim(0.5f);
    const Skills& s = world.player.skills;

    const SDL_FRect panel = CenteredPanel(ui, skills_tab == 0 ? 640.0f : 860.0f, 560.0f);
    ui.Panel(panel);

    // --- tabs ------------------------------------------------------------------
    {
        static const char* kTabs[4] = {"Skills", "Melee", "Ranged", "Magic"};
        float tx = panel.x + 24.0f;
        for (int t = 0; t < 4; ++t) {
            const float w = ui.Measure(kTabs[t], TextSize::Body).x + 24.0f;
            const SDL_FRect tab = {tx, panel.y + 14.0f, w, 30.0f};
            const bool on = (t == skills_tab);
            ui.Fill(tab, on ? SDL_Color{70, 54, 30, 235} : SDL_Color{30, 24, 20, 200});
            ui.Outline(tab, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
            int free = 0;
            if (t > 0) free = world.player.talents.PointsFree(static_cast<AttackStyle>(t - 1), s);
            ui.Text(kTabs[t], tab.x + 12.0f, tab.y + 5.0f, TextSize::Body,
                    on ? Palette::Highlight : (free > 0 ? Palette::Xp : Palette::Text));
            tx += w + 6.0f;
        }
        ui.Text(input.PromptFor(Action::Inventory) + " / " + input.PromptFor(Action::Skills) + " switch",
                tx + 10.0f, panel.y + 22.0f, TextSize::Small, Palette::TextDim);
    }

    if (skills_tab > 0) {
        DrawSkillTree(panel);
        return;
    }

    char header[128];
    SDL_snprintf(header, sizeof(header), "Combat %d    Total level %d    Total XP %lld",
                 s.CombatLevel(), s.TotalLevel(), s.TotalXp());
    ui.Text(header, panel.x + panel.w - 24.0f, panel.y + panel.h - 52.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // With Fishing selected, its milestones: the chance of more than one fish.
    if (cursor == SKILL_FISHING) {
        string line = "Catch more than one:";
        for (const auto& m : Gathering::FishingMilestones()) {
            char buf[64];
            if (m.three > 0.0f) SDL_snprintf(buf, sizeof(buf), "  %d: 3 fish %d%%", m.level, static_cast<int>(m.three * 100 + 0.5f));
            else                SDL_snprintf(buf, sizeof(buf), "  %d: 2 fish %d%%", m.level, static_cast<int>(m.two * 100 + 0.5f));
            line += buf;
        }
        ui.Text(line, panel.x + 24.0f, panel.y + panel.h - 76.0f, TextSize::Small,
                s.Level(SKILL_FISHING) >= 20 ? Palette::Xp : Palette::TextDim);
    }

    const float row_h = 38.0f;
    for (int i = 0; i < SKILL_COUNT; ++i) {
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 58.0f + i * row_h,
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

void Game::DrawSkillTree(const SDL_FRect& panel) {
    const AttackStyle style = static_cast<AttackStyle>(skills_tab - 1);
    const TalentTree& tree = skill_trees.Tree(style);
    const Player& p = world.player;
    const int level = p.skills.Level(tree.skill);
    const int earned = p.talents.PointsEarned(style, p.skills);
    const int free = p.talents.PointsFree(style, p.skills);

    char head[160];
    SDL_snprintf(head, sizeof(head), "%s %d     %d of %d points free     a point every %d levels",
                 SkillName(tree.skill), level, free, earned, SkillTrees::LEVELS_PER_POINT);
    ui.Text(head, panel.x + 24.0f, panel.y + 56.0f, TextSize::Small, free > 0 ? Palette::Xp : Palette::TextDim);

    // --- the grid ----------------------------------------------------------------
    const float gx = panel.x + 78.0f, gy = panel.y + 112.0f;
    const float col_w = 172.0f, row_h = 78.0f, box_w = 150.0f, box_h = 48.0f;

    for (int b = 0; b < SkillTrees::BRANCHES; ++b) {
        const string name = b < static_cast<int>(tree.branches.size()) ? tree.branches[b] : "";
        ui.Text(name, gx + b * col_w + box_w / 2.0f, gy - 26.0f, TextSize::Body, Palette::Highlight, Align::Center);
    }

    for (int row = 0; row < SkillTrees::ROWS; ++row) {
        const TalentNode* first = tree.At(0, row);
        if (first)
            ui.Text("Lv " + std::to_string(first->level), panel.x + 24.0f, gy + row * row_h + 15.0f,
                    TextSize::Small, level >= first->level ? Palette::Text : Palette::TextDim);
    }

    for (const TalentNode& n : tree.nodes) {
        const SDL_FRect box = {gx + n.branch * col_w, gy + n.row * row_h, box_w, box_h};
        const bool learned = p.talents.Has(n.id);
        const Talents::Why why = p.talents.CanLearn(n.id, p.skills);
        const bool available = why == Talents::Why::Ok;
        const bool selected = n.branch == tree_branch && n.row == tree_row;
        const bool active = !n.technique.empty() && p.talents.Technique(style) == n.technique;

        // The line down to the next node in the branch, lit once both ends are.
        if (n.row + 1 < SkillTrees::ROWS) {
            const TalentNode* below = tree.At(n.branch, n.row + 1);
            const bool lit = learned && below && p.talents.Has(below->id);
            ui.Fill({box.x + box_w / 2.0f - 1.0f, box.y + box_h, 3.0f, row_h - box_h},
                    lit ? SDL_Color{232, 190, 96, 255} : SDL_Color{70, 60, 50, 255});
        }

        SDL_Color fill = {26, 21, 18, 235}, text = {120, 110, 100, 255}, edge = Palette::BorderDim;
        if (learned)        { fill = {92, 70, 30, 240}; text = Palette::Highlight; edge = {232, 190, 96, 255}; }
        else if (available) { fill = {40, 50, 30, 240}; text = Palette::Text; edge = Palette::Xp; }
        if (active)         { fill = {120, 50, 36, 245}; edge = {255, 160, 110, 255}; }
        ui.Fill(box, fill);
        ui.Outline(box, selected ? SDL_Color{255, 255, 255, 255} : edge, selected ? 3.0f : 1.0f);

        ui.Text(n.name, box.x + box_w / 2.0f, box.y + 7.0f, TextSize::Small, text, Align::Center);
        const char* kind = !n.technique.empty() ? (active ? "technique - active" : "technique") : "passive";
        ui.Text(kind, box.x + box_w / 2.0f, box.y + 26.0f, TextSize::Small,
                !n.technique.empty() ? SDL_Color{236, 150, 110, 255} : Palette::TextDim, Align::Center);
    }

    // --- the chosen node -------------------------------------------------------------
    const TalentNode* n = tree.At(tree_branch, tree_row);
    const float dx = gx + SkillTrees::BRANCHES * col_w + 12.0f;
    const float dw = panel.x + panel.w - dx - 24.0f;
    float y = gy - 26.0f;
    if (n) {
        ui.Text(n->name, dx, y, TextSize::Body, Palette::Highlight);
        y += 28.0f;
        ui.Text(string(SkillName(tree.skill)) + " " + std::to_string(n->level) + ", " +
                (n->row == 0 ? string("one point") : "one point, after " + tree.At(n->branch, n->row - 1)->name),
                dx, y, TextSize::Small, level >= n->level ? Palette::TextDim : SDL_Color{225, 130, 120, 255});
        y += 24.0f;
        y += ui.TextWrapped(n->description, dx, y, dw, TextSize::Small, Palette::Text) + 14.0f;

        string status, action;
        const Talents::Why why = p.talents.CanLearn(n->id, p.skills);
        if (p.talents.Has(n->id)) {
            status = "Learned.";
            if (!n->technique.empty()) {
                const bool active = p.talents.Technique(style) == n->technique;
                status = active ? "Your charged attack with this style." : "Learned, not in use.";
                action = input.PromptFor(Action::Confirm) + (active ? " stop using it" : " use as charged attack");
            }
        } else if (why == Talents::Why::Ok) {
            status = "Ready to learn.";
            action = input.PromptFor(Action::Confirm) + " learn";
        } else if (why == Talents::Why::Level) {
            status = "Needs " + string(SkillName(tree.skill)) + " " + std::to_string(n->level) + ".";
        } else if (why == Talents::Why::Prerequisite) {
            status = "Learn the node above it first.";
        } else if (why == Talents::Why::NoPoints) {
            status = "No points to spend.";
        }
        ui.Text(status, dx, y, TextSize::Small, p.talents.Has(n->id) ? Palette::Highlight : Palette::TextDim);
        y += 22.0f;
        if (!action.empty()) ui.Text(action, dx, y, TextSize::Small, Palette::Xp);
    }

    const string technique = p.talents.Technique(style);
    string shown = technique;
    for (char& c : shown) if (c == '_') c = ' ';
    if (!shown.empty()) shown[0] = static_cast<char>(toupper(static_cast<unsigned char>(shown[0])));
    ui.Text("Charged attack: " + (shown.empty() ? string("plain") : shown),
            dx, panel.y + panel.h - 96.0f, TextSize::Small, Palette::TextDim);

    ui.Text(input.PromptFor(Action::Target) + " twice unlearn tree     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            tree_reset_armed ? SDL_Color{235, 190, 120, 255} : Palette::TextDim, Align::Center);
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
            ui.Text("Suggested level " + std::to_string(d->recommended_level) +
                        (d->daily ? "     daily, done " + std::to_string(quests.Completions(list[index])) + "x" : string("")),
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

    const DialogueContext dctx = MakeDialogueContext();

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
                ui.Text((d->daily ? string("daily   ") : string("")) + "Lv " + std::to_string(d->recommended_level),
                        row.x + row.w - 8.0f, row.y + 3.0f, TextSize::Small,
                        d->daily ? Palette::Xp : Palette::TextDim, Align::Right);
        }

        const int index = std::clamp(board_cursor, 0, static_cast<int>(available.size()) - 1);
        if (const QuestDef* d = quests.Definition(available[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 30.0f;
            if (d->daily) {
                ui.Text("Daily: new notices at dawn", dx, y, TextSize::Small, Palette::Xp);
                y += 22.0f;
            }
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
    const vector<const ItemDef*> recipes = items.Recipes(craft_station);
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
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 450.0f);
    ui.Panel(panel);
    const bool anvil = (craft_station == CraftStation::Anvil);
    ui.Text(craft_title.empty() ? (anvil ? "Anvil" : "Workbench") : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world.player;
    ui.Text("Crafting " + std::to_string(p.skills.Level(SKILL_CRAFTING)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // Say where the rest is made, so a missing recipe reads as "elsewhere"
    // rather than "gone".
    ui.Text(anvil ? "Smithing: anything made from metal. Wood and leather are worked at a workbench."
                  : "Wood, leather and thread. Anything made from metal is smithed at an anvil.",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 50.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    const vector<const ItemDef*> recipes = items.Recipes(craft_station);
    if (recipes.empty()) {
        ui.Text("Nothing to make here.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f, TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    const float list_w = 280.0f, row_h = 34.0f;
    // A window of nine rows that follows the cursor: the anvil alone makes
    // sixty-odd things now, one of every piece in every tier.
    constexpr int SHOWN = 9;
    const int count = static_cast<int>(recipes.size());
    const int first = std::clamp(craft_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, panel.y + 46.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, panel.y + 62.0f + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const ItemDef* r = recipes[i];
        const ItemDef* made = items.Get(r->craft_result);
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + (i - first) * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (i == craft_cursor);
        const bool unlocked = p.skills.Level(SKILL_CRAFTING) >= r->craft_level;

        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        if (made && !made->icon.empty())
            if (SDL_Texture* tex = textures->Get(made->icon)) {
                const SDL_FRect ic = {row.x + 4.0f, row.y + 3.0f, 24.0f, 24.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(made ? made->name : r->craft_result, row.x + 34.0f, row.y + 5.0f,
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
    if (made && !made->requirements.empty()) {
        string req = "To use: ";
        for (const auto& rq : made->requirements)
            req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
        ui.Text(req, dx, y, TextSize::Small, Palette::Text);
        y += 22.0f;
    }

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
    // J confirms now, and J is also what a player dying mid-fight is mashing.
    // Give the screen a moment so it is read, not skipped by the last swing.
    if (state_time < 0.8f) return;
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
