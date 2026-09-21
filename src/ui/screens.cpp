#include "../systems/gathering.h"
#include "../game.h"

// =============================================================================
//  Shared helpers
// =============================================================================

namespace {

// The button an ability slot is on, with the guard held: light, heavy, lock on.
Action AbilityButton(int slot) {
    return slot == 0 ? Action::LightAttack : slot == 1 ? Action::StrongAttack : Action::Target;
}

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
    options.push_back("Play Together");
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
        } else if (choice == "Play Together") {
            OpenMultiplayer();
        } else if (choice == "Options") {
            OpenPanel(GameState::Options);
        } else if (choice == "Quit") {
            running = false;
        }
    }
}

void Game::DrawMainMenu() {
    const float cx = ui.ViewWidth() / 2.0f;

    // Shadowed, and higher up than it used to sit: the title stands on the
    // painting now rather than on a flat colour, and the moon is behind it.
    ui.TextShadowed("DREAMQUEST", cx, ui.ViewHeight() * 0.09f, TextSize::Title,
                    Palette::Highlight, Align::Center);
    ui.TextShadowed("An adventure in the Hollowmarch", cx, ui.ViewHeight() * 0.09f + 52.0f,
                    TextSize::Body, {214, 200, 176, 255}, Align::Center);

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

    ui.TextShadowed(input.ActiveDevice() == InputMode::Controller
                        ? "D-pad to move   (A) select"
                        : "Arrow keys / WASD to move   Enter to select",
                    cx, ui.ViewHeight() - 44.0f, TextSize::Small,
                    {186, 176, 158, 255}, Align::Center);
}

// =============================================================================
//  Character select
// =============================================================================

// The choices, and the order they appear in. All three are this project's own
// character -- one rig in three sets of clothes, modelled and animated in
// tools/blender_character.py. The two that used to sit beside the first were
// from a CraftPix pack, whose licence covers using the art but not passing the
// files on, which made the game undistributable as a repository.
const char* Game::kCharacterIds[kCharacterCount] = {
    "player_hero", "player_warden", "player_wayfarer"
};
const char* Game::kCharacterLabels[kCharacterCount] = {
    "Hollow-born", "Greenwarden", "Wayfarer"
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
    // Shadowed: the front end stands on the title painting, and a heading in
    // flat text over a moonlit sky is hard to read.
    ui.TextShadowed("Choose your adventurer", cx, ui.ViewHeight() * 0.16f, TextSize::Large,
                    Palette::Text, Align::Center);

    const float card_w = 200.0f, card_h = 280.0f, gap = 28.0f;
    const float total = card_w * kCharacterCount + gap * (kCharacterCount - 1);
    const float start_x = cx - total / 2.0f;

    for (int i = 0; i < kCharacterCount; ++i) {
        const SDL_FRect card = {start_x + i * (card_w + gap), ui.ViewHeight() * 0.3f,
                                card_w, card_h};
        ui.Panel(card, i == cursor);
        if (i == cursor) ui.Outline(card, Palette::Highlight, 2.0f);

        // Live idle animation as the preview, holding what they set out with:
        // the warden's bow and the wayfarer's staff, not the rig's own sword,
        // which is what all three used to be shown with whatever they fight
        // with. The armour they start in comes with it.
        if (const SpriteDef* def = sprites.Get(kCharacterIds[i])) {
            Sprite preview;
            preview.SetDef(def);
            preview.style = Player::KitStyle(kCharacterIds[i], &items);
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
        // What each favours: the one thing that tells the three apart in a
        // fight, said before the choice is made.
        const AttackStyle aff = Player::AffinityFor(kCharacterIds[i]);
        ui.Text(string("Affinity: ") + Player::AffinityName(aff), card.x + card.w / 2.0f,
                card.y + card.h - 36.0f, TextSize::Small,
                aff == AttackStyle::Ranged ? SDL_Color{150, 210, 130, 255}
                : aff == AttackStyle::Magic ? SDL_Color{170, 150, 240, 255}
                                            : SDL_Color{236, 176, 96, 255}, Align::Center);
        // And what they set out with, since it is the weapon of that affinity.
        const vector<string> kit = Player::StartingKit(kCharacterIds[i]);
        const ItemDef* first = kit.empty() ? nullptr : items.Get(kit.front());
        const bool vowel = first && string("AEIOUaeiou").find(first->name[0]) != string::npos;
        ui.Text(first ? "starts: " + first->name : string("hits harder and truer with it"),
                card.x + card.w / 2.0f, card.y + card.h - 20.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
        (void)vowel;
    }

    ui.TextShadowed("Left / Right to choose   " + input.PromptFor(Action::Confirm) +
                    " to continue   " + input.PromptFor(Action::Back) + " to go back",
                    cx, ui.ViewHeight() - 52.0f, TextSize::Small,
                    {186, 176, 158, 255}, Align::Center);
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
            if (slot_purpose == 0) {
                NewGame(pending_character, slot);
            } else {
                SaveGame(slot);
                SetState(GameState::Play);
            }
        } else if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
            overwrite_slot = -1;
        }
        return;
    }

    MoveCursor(cursor, SAVE_SLOTS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const int slot = cursor + 1;
        // Anything in the slot at all is something to ask about -- a save that
        // cannot be read included, which used to look like an empty slot.
        if (slot_purpose == 0) {
            if (SaveSystem::Occupied(slot)) overwrite_slot = slot;
            else                            NewGame(pending_character, slot);
        } else if (SaveSystem::Occupied(slot) && slot != active_slot) {
            // Saving over your own slot is what saving is. Saving over
            // somebody else's is a different character gone, and used to take
            // one press where starting a new game there took two.
            overwrite_slot = slot;
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

        if (s.damaged) {
            ui.Text("Damaged", row.x + 120.0f, row.y + 12.0f, TextSize::Body, {235, 150, 120, 255});
            ui.Text("There is a save here that cannot be read. It has not been touched.",
                    row.x + 16.0f, row.y + 42.0f, TextSize::Small, {235, 150, 120, 255});
            continue;
        }
        if (!s.exists) {
            ui.Text("Empty", row.x + 16.0f, row.y + 42.0f, TextSize::Small, Palette::TextDim);
            continue;
        }

        ui.Text(s.map_name + (s.from_backup ? "  (backup)" : ""), row.x + 120.0f, row.y + 12.0f,
                TextSize::Body, s.from_backup ? SDL_Color{235, 200, 120, 255} : Palette::Text);
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
    ui.TextShadowed(input.PromptFor(Action::Confirm) + " confirm     " +
                    input.PromptFor(Action::Back) + " back",
                    ui.ViewWidth() / 2.0f, area.y + area.h + 16.0f, TextSize::Small,
                    {186, 176, 158, 255}, Align::Center);

    if (overwrite_slot >= 0) {
        ui.Dim(0.6f);
        const SDL_FRect box = CenteredPanel(ui, 440.0f, 150.0f);
        ui.Panel(box);
        const float cx = box.x + box.w / 2.0f;
        ui.Text("Slot " + std::to_string(overwrite_slot) + " already holds a saved game.",
                cx, box.y + 24.0f, TextSize::Body, Palette::Highlight, Align::Center);
        ui.Text(slot_purpose == 0 ? "Starting over here will erase it."
                                  : "It is not the one you are playing. Saving here will replace it.",
                cx, box.y + 56.0f, TextSize::Small, Palette::Text, Align::Center);
        ui.Text(input.PromptFor(Action::Confirm) + (slot_purpose == 0 ? " erase and start     " : " save over it     ") +
                input.PromptFor(Action::Back) + " keep it",
                cx, box.y + box.h - 36.0f, TextSize::Small, Palette::TextDim, Align::Center);
    }
}

// =============================================================================
//  Load menu
// =============================================================================

void Game::UpdateLoadMenu() {
    // Deleting asks first, on a panel of its own, the way starting over does.
    if (delete_slot >= 0) {
        if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
            const int slot = delete_slot;
            delete_slot = -1;
            if (SaveSystem::Delete(slot))
                PushToast("Slot " + std::to_string(slot) + " deleted.", Palette::TextDim);
            else
                PushToast("That slot could not be deleted.", {235, 120, 120, 255});
        } else if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
            delete_slot = -1;
        }
        return;
    }

    MoveCursor(cursor, SAVE_SLOTS);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const SaveSlotInfo info = SaveSystem::Peek(cursor + 1);
        if (info.damaged)      PushToast("That save cannot be read. It has been left as it is.", {235, 150, 120, 255});
        else if (info.exists)  LoadGame(cursor + 1);
        else                   PushToast("That slot is empty.", Palette::TextDim);
    }
    // There was a function for deleting a slot and no way to reach it, so the
    // only way to make room for a fourth character was to write over a third.
    if (input.Pressed(Action::Drop)) {
        if (SaveSystem::Occupied(cursor + 1)) delete_slot = cursor + 1;
        else PushToast("That slot is empty.", Palette::TextDim);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::MainMenu);
}

void Game::DrawLoadMenu() {
    const SDL_FRect area = CenteredPanel(ui, 560.0f, 400.0f);
    DrawSlotList(area, "Load game");
    ui.TextShadowed(input.PromptFor(Action::Confirm) + " load     " +
                    input.PromptFor(Action::Drop) + " delete     " +
                    input.PromptFor(Action::Back) + " back",
                    ui.ViewWidth() / 2.0f, area.y + area.h + 16.0f, TextSize::Small,
                    {186, 176, 158, 255}, Align::Center);

    if (delete_slot >= 0) {
        ui.Dim(0.6f);
        const SDL_FRect box = CenteredPanel(ui, 440.0f, 150.0f);
        ui.Panel(box);
        const float cx = box.x + box.w / 2.0f;
        ui.Text("Delete the save in slot " + std::to_string(delete_slot) + "?",
                cx, box.y + 24.0f, TextSize::Body, Palette::Highlight, Align::Center);
        ui.Text("The character in it will be gone from this list.", cx, box.y + 56.0f, TextSize::Small,
                Palette::Text, Align::Center);
        ui.Text(input.PromptFor(Action::Confirm) + " delete it     " +
                input.PromptFor(Action::Back) + " keep it",
                cx, box.y + box.h - 36.0f, TextSize::Small, Palette::TextDim, Align::Center);
    }
}

// =============================================================================
//  Options
// =============================================================================

// The rows of the Options screen, by name, so adding one is not a renumbering.
namespace {
enum OptionRow {
    OPT_INPUT, OPT_CONTROLS, OPT_ZOOM, OPT_UI_SCALE, OPT_FULLSCREEN, OPT_VSYNC, OPT_FPS, OPT_DAMAGE, OPT_XP,
    OPT_WAYPOINTS,
    OPT_MASTER, OPT_SFX, OPT_AMBIENCE, OPT_BACK, OPT_ROWS
};
}

void Game::UpdateOptions() {
    constexpr int ROWS = OPT_ROWS;
    MoveCursor(cursor, ROWS);

    int delta = 0;
    if (input.MenuRight()) delta = 1;
    if (input.MenuLeft())  delta = -1;
    const bool confirm = input.Pressed(Action::Confirm) || input.Pressed(Action::Interact);

    if (delta != 0 || confirm) {
        const int step = (delta != 0) ? delta : 1;
        switch (cursor) {
            case OPT_INPUT:
                settings.input_mode = ((settings.input_mode + step) % 3 + 3) % 3;
                input.SetMode(static_cast<InputMode>(settings.input_mode));
                break;
            case OPT_CONTROLS:
                if (confirm) {
                    // Not OpenPanel: Options still goes back to wherever it came from.
                    SetState(GameState::Controls);
                    controls_cursor = 0;
                    controls_column = input.ActiveDevice() == InputMode::Controller ? 1 : 0;
                    controls_note.clear();
                    return;
                }
                break;
            case OPT_ZOOM:
                settings.zoom = std::clamp(settings.zoom + step * 0.25f, 1.5f, 4.0f);
                world->camera.SetZoom(settings.zoom);
                break;
            case OPT_UI_SCALE: {
                // The four sizes worth having, stepped through: text is set at
                // a whole number of points, so the ones in between would only
                // be one of these with the panels drawn a hair larger.
                static const float kSteps[] = {1.0f, 1.1f, 1.25f, 1.5f};
                int at = 0;
                for (int k = 0; k < 4; ++k) if (fabsf(kSteps[k] - settings.ui_scale) < 0.03f) at = k;
                at = (delta != 0) ? std::clamp(at + delta, 0, 3) : (at + 1) % 4;
                settings.ui_scale = kSteps[at];
                break;
            }
            case OPT_FULLSCREEN:
                settings.fullscreen = !settings.fullscreen;
                SDL_SetWindowFullscreen(window, settings.fullscreen);
                break;
            case OPT_VSYNC:
                settings.vsync = !settings.vsync;
                SDL_SetRenderVSync(renderer, settings.vsync ? 1 : 0);
                break;
            case OPT_FPS: settings.show_fps = !settings.show_fps; break;
            case OPT_DAMAGE: settings.damage_numbers = !settings.damage_numbers; break;
            case OPT_XP: settings.xp_drops = !settings.xp_drops; break;
            case OPT_WAYPOINTS: settings.quest_waypoints = !settings.quest_waypoints; break;
            case OPT_MASTER: case OPT_SFX: case OPT_AMBIENCE: {
                float& v = (cursor == OPT_MASTER) ? settings.master_volume
                         : (cursor == OPT_SFX) ? settings.sfx_volume : settings.ambience_volume;
                // Confirm cycles, wrapping back to silent after full.
                if (delta != 0) v = std::clamp(roundf((v + delta * 0.1f) * 10.0f) / 10.0f, 0.0f, 1.0f);
                else            v = (v >= 0.95f) ? 0.0f : roundf((v + 0.1f) * 10.0f) / 10.0f;
                Audio::SetVolumes(settings.master_volume, settings.sfx_volume,
                                  settings.ambience_volume);
                Audio::Play(cursor == OPT_AMBIENCE ? Sfx::Pickup : Sfx::Hit, 0.8f);
                break;
            }
            case OPT_BACK:
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
    // Twelve rows now, so they are a little shorter, and shorter again in a
    // window that is.
    const float row_count = static_cast<float>(OPT_ROWS);
    const float row_h = std::clamp(floorf((ui.ViewHeight() - 40.0f - 130.0f) / row_count), 30.0f, 40.0f);
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 130.0f + row_count * row_h);
    ui.Panel(panel);

    ui.Text("Options", panel.x + panel.w / 2.0f, panel.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    char zoom_buf[24];
    SDL_snprintf(zoom_buf, sizeof(zoom_buf), "%.2fx", settings.zoom);
    // What was asked for, and what the window has room for when that is less.
    char scale_buf[48];
    {
        const int asked = static_cast<int>(lroundf(settings.ui_scale * 100.0f));
        const int shown = static_cast<int>(lroundf(UiScale() * 100.0f));
        if (shown < asked) SDL_snprintf(scale_buf, sizeof(scale_buf), "%d%%  (%d%% fits)", asked, shown);
        else               SDL_snprintf(scale_buf, sizeof(scale_buf), "%d%%", asked);
    }

    const string pad_note = input.HasGamepad()
        ? string(input.GamepadName())
        : string("no controller detected");

    const pair<string, string> rows[OPT_ROWS] = {
        {"Input Device",   InputModeLabel(settings.input_mode)},
        {"Controls",       "keys and buttons  >"},
        {"Camera Zoom",    zoom_buf},
        {"Interface Size", scale_buf},
        {"Fullscreen",     settings.fullscreen ? "On" : "Off"},
        {"VSync",          settings.vsync ? "On" : "Off"},
        {"Show FPS",       settings.show_fps ? "On" : "Off"},
        {"Damage Numbers", settings.damage_numbers ? "On" : "Off"},
        {"Experience Gains", settings.xp_drops ? "On" : "Off"},
        {"Quest Waypoints", settings.quest_waypoints ? "On" : "Off"},
        {"Master Volume",  VolumeLabel(settings.master_volume)},
        {"Effects Volume", VolumeLabel(settings.sfx_volume)},
        {"Ambience Volume", VolumeLabel(settings.ambience_volume)},
        {"Back",           ""},
    };

    for (int i = 0; i < OPT_ROWS; ++i) {
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
//  Controls: which key and which button does what
// =============================================================================
//
// A row an action, a column a device. Confirm on a cell listens for whatever is
// pressed next and gives the action that; whoever had it takes the one this
// had, so nothing is ever left without a key and no key ever does two things.
// Under the actions: put either column back as it shipped, and back.

namespace {
constexpr int CONTROLS_EXTRA = 3;      // reset keyboard, reset controller, back
int ControlsRows() { return static_cast<int>(Bindings::Rebindable().size()) + CONTROLS_EXTRA; }
}

void Game::UpdateControls() {
    const vector<Action>& actions = Bindings::Rebindable();
    const int count = static_cast<int>(actions.size());
    const auto keep = [&](const Bindings& b) {
        input.SetBindings(b);
        input_two.SetBindings(b);
        settings.controls = b.ToJson();
        settings.Save();
    };

    // Waiting to be told: everything pressed is an answer, and not a command.
    if (input.Listening()) {
        const Input::Heard heard = input.TakeHeard();
        if (heard.cancelled) {
            controls_note = "Left as it was.";
            Audio::Play(Sfx::UiBack);
        } else if (heard.any) {
            const Action a = actions[std::clamp(controls_cursor, 0, count - 1)];
            Bindings b = input.GetBindings();
            const bool key = heard.button < 0;
            if (key ? !Bindings::KeyFree(heard.key) : !Bindings::ButtonFree(heard.button)) {
                controls_note = (key ? Bindings::KeyLabel(heard.key) : Bindings::ButtonLabel(heard.button)) +
                                " is kept for the menus, and cannot be given away.";
                Audio::Play(Sfx::UiError);
            } else {
                const string label = key ? Bindings::KeyLabel(heard.key) : Bindings::ButtonLabel(heard.button);
                const Action other = key ? b.BindKey(a, heard.key) : b.BindButton(a, heard.button);
                controls_note = string(Bindings::Name(a)) + " is on " + label + ".";
                if (other != Action::COUNT)
                    controls_note += "  " + string(Bindings::Name(other)) + " takes " +
                                     (key ? Bindings::KeyLabel(b.Key(other)) : Bindings::ButtonLabel(b.Button(other))) + ".";
                keep(b);
                Audio::Play(Sfx::UiConfirm);
            }
        }
        return;
    }

    MoveCursor(controls_cursor, ControlsRows());
    if (input.MenuLeft() || input.MenuRight()) {
        controls_column = 1 - controls_column;
        Audio::Play(Sfx::UiMove);
    }

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (controls_cursor < count) {
            const Action a = actions[controls_cursor];
            if (controls_column == 1 && !Bindings::OnPad(a)) {
                const bool steering = a == Action::MoveUp || a == Action::MoveDown || a == Action::MoveLeft || a == Action::MoveRight;
                controls_note = steering ? "A controller steers with its left stick and its d-pad, and those stay put."
                              : a == Action::Drop ? "On a controller, the heavy attack's button drops things in the bag."
                                                  : "On a controller the elements are stepped through with Next element.";
                Audio::Play(Sfx::UiError);
            } else if (controls_column == 1 && !input.HasGamepad()) {
                controls_note = "No controller is plugged in to press a button on.";
                Audio::Play(Sfx::UiError);
            } else {
                controls_note.clear();
                input.Listen(controls_column == 0 ? Input::ListenFor::Key : Input::ListenFor::Button);
                Audio::Play(Sfx::UiConfirm);
            }
        } else if (controls_cursor == count || controls_cursor == count + 1) {
            Bindings b = input.GetBindings();
            const Bindings fresh;
            if (controls_cursor == count) b.keys = fresh.keys; else b.buttons = fresh.buttons;
            keep(b);
            controls_note = controls_cursor == count ? "The keyboard is as it shipped." : "The controller is as it shipped.";
            Audio::Play(Sfx::UiConfirm);
        } else {
            SetState(GameState::Options);
            cursor = OPT_CONTROLS;
            return;
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Options);
        cursor = OPT_CONTROLS;
    }
}

void Game::DrawControls() {
    ui.Dim(0.55f);
    const vector<Action>& actions = Bindings::Rebindable();
    const int count = static_cast<int>(actions.size());
    const int rows = ControlsRows();
    const Bindings& b = input.GetBindings();

    // As many rows as the window has room for, scrolled to keep the cursor in.
    const float row_h = 24.0f;
    const float chrome = 150.0f;
    const int fit = std::clamp(static_cast<int>((ui.ViewHeight() - 40.0f - chrome) / row_h), 6, rows);
    const SDL_FRect panel = CenteredPanel(ui, 640.0f, chrome + fit * row_h);
    ui.Panel(panel);
    ui.Text("Controls", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight, Align::Center);

    const float name_x = panel.x + 32.0f;
    const float col_x[2] = {panel.x + 330.0f, panel.x + 490.0f};
    const float head_y = panel.y + 58.0f;
    const char* heads[2] = {"Keyboard", "Controller"};
    for (int c = 0; c < 2; ++c) {
        const bool here = c == controls_column;
        ui.Text(heads[c], col_x[c], head_y, TextSize::Body, here ? Palette::Highlight : Palette::TextDim, Align::Center);
        if (here) ui.Fill({col_x[c] - 46.0f, head_y + 22.0f, 92.0f, 2.0f}, Palette::Highlight);
    }

    const int first = std::clamp(controls_cursor - fit / 2, 0, std::max(0, rows - fit));
    const float top = panel.y + 92.0f;
    for (int i = first; i < first + fit && i < rows; ++i) {
        const float y = top + (i - first) * row_h;
        const bool on_row = i == controls_cursor;
        const SDL_FRect row = {panel.x + 16.0f, y - 2.0f, panel.w - 32.0f, row_h - 2.0f};
        if (on_row) ui.Fill(row, {58, 46, 28, 235});
        if (i < count) {
            const Action a = actions[i];
            ui.Text(Bindings::Name(a), name_x, y, TextSize::Small, on_row ? Palette::Text : Palette::TextDim);
            for (int c = 0; c < 2; ++c) {
                const bool cell = on_row && c == controls_column;
                const bool steering = a == Action::MoveUp || a == Action::MoveDown || a == Action::MoveLeft || a == Action::MoveRight;
                string label = c == 0 ? Bindings::KeyLabel(b.Key(a))
                             : Bindings::OnPad(a) ? Bindings::ButtonLabel(b.Button(a))
                             : steering ? string("stick, d-pad") : string("-");
                if (cell && input.Listening()) label = c == 0 ? "press a key..." : "press a button...";
                if (cell) {
                    const SDL_FRect box = {col_x[c] - 66.0f, y - 2.0f, 132.0f, row_h - 2.0f};
                    ui.Outline(box, Palette::Highlight, input.Listening() ? 2.0f : 1.0f);
                }
                ui.Text(label, col_x[c], y, TextSize::Small,
                        cell ? Palette::Highlight : (on_row ? Palette::Text : Palette::TextDim), Align::Center);
            }
        } else {
            const char* labels[CONTROLS_EXTRA] = {"Put the keyboard back as it shipped", "Put the controller back as it shipped", "Back"};
            ui.Text(labels[i - count], name_x, y, TextSize::Small, on_row ? Palette::Highlight : Palette::TextDim);
        }
    }
    // More above or below than is showing.
    if (first > 0) ui.Text("^", panel.x + panel.w - 28.0f, top - 4.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + fit < rows)
        ui.Text("v", panel.x + panel.w - 28.0f, top + (fit - 1) * row_h, TextSize::Small, Palette::TextDim, Align::Center);

    const float foot = panel.y + panel.h - 50.0f;
    if (!controls_note.empty())
        ui.Text(controls_note, panel.x + panel.w / 2.0f, foot, TextSize::Small, Palette::Xp, Align::Center);
    const string help = input.Listening()
        ? string("Esc, or Start, leaves it as it was")
        : input.PromptFor(Action::Confirm) + " change     Left / Right keyboard or controller     " +
          input.PromptFor(Action::Back) + " back";
    ui.Text(help, panel.x + panel.w / 2.0f, foot + 22.0f, TextSize::Small, Palette::TextDim, Align::Center);
    ui.Text("Esc and Start pause, Enter and Backspace, the arrows and the d-pad work the menus: those stay put.",
            panel.x + panel.w / 2.0f, panel.y + panel.h + 8.0f, TextSize::Small, {186, 176, 158, 255}, Align::Center);
}

// =============================================================================
//  Pause
// =============================================================================

void Game::UpdatePaused() {
    static const char* kRows[] = {"Resume", "Save Game", "Play Together", "Player Two", "Options", "Quit to Main Menu"};
    constexpr int ROWS = 6;
    MoveCursor(cursor, ROWS);
    // Who Player Two arrives as, the first time: left and right on their row.
    if (cursor == 3 && !split_active && (input.MenuLeft() || input.MenuRight()))
        CycleSplitLook(input.MenuRight() ? 1 : -1);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        switch (cursor) {
            case 0: SetState(GameState::Play); break;
            case 1: slot_purpose = 1; SetState(GameState::SlotSelect); break;
            case 2: OpenMultiplayer(); break;
            case 3:
                if (split_active) LeaveSplit(); else JoinSplit();
                ServeSeat(0);
                SetState(GameState::Play);
                break;
            case 4: OpenPanel(GameState::Options); break;
            case 5:
                if (guest_session) {
                    // The character is kept; the world is the host's.
                    SaveGuestCharacter();
                    session.Leave();
                    EndGuestSession("");
                    break;
                }
                // Save before leaving, so quitting never costs progress.
                if (!never_save) WriteSlot(active_slot);
                LeaveSplit();
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
    const SDL_FRect panel = CenteredPanel(ui, 420.0f, 368.0f);
    ui.Panel(panel);

    ui.Text(serving == 1 ? "Paused  -  Player Two" : "Paused", panel.x + panel.w / 2.0f, panel.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const string split_row = SplitRowLabel();
    const string kRows[] = {"Resume", "Save Game", "Play Together", split_row, "Options", "Quit to Main Menu"};
    for (int i = 0; i < 6; ++i) {
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

    for (const FloatingText& t : world->texts) {
        const float progress = 1.0f - (t.life / t.max_life);
        const SDL_FPoint p = UiPoint(t.x, t.y - t.rise * progress);
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

const Waypoint& Game::CurrentWaypoint() {
    const int seat = serving == 1 ? 1 : 0;
    Waypoint& wp = seat_waypoint[seat];
    const string quest = (settings.quest_waypoints && quests) ? quests->Followed() : string();
    string key;
    if (!quest.empty()) {
        const QuestDef* d = quests->Definition(quest);
        const int stage = quests->Stage(quest);
        // A delivery points at the source until the bag holds enough, and at
        // whoever wants it after: how much is held is part of the question.
        int held = 0;
        if (d && stage >= 0 && stage < static_cast<int>(d->stages.size()) && d->stages[stage].type == ObjectiveType::Deliver)
            held = std::min(world->player.inventory.Count(d->stages[stage].target), d->stages[stage].count);
        key = quest + "|" + std::to_string(stage) + "|" + world->MapId() + "|" + std::to_string(held);
    }
    const Uint64 now = SDL_GetTicks();
    if (key != seat_waypoint_key[seat] || now - seat_waypoint_at[seat] > 250) {
        seat_waypoint_key[seat] = key;
        seat_waypoint_at[seat] = now;
        wp = quest.empty() ? Waypoint{} : waypoints.Resolve(*quests, quest, *world, &enemy_db, &loot, &items);
    }
    return wp;
}

// The way to the quest, in the world: a gold chevron bobbing over the thing when
// it is in sight, and when it is not, an arrow at the edge of the view pointing
// at it with how many paces off it is.
void Game::DrawWaypoint(const Waypoint& wp) {
    if (!wp.found) return;
    const float lift = world->LiftAt(wp.local_x, wp.local_y);
    // Over a head if it is somebody; over a way out, clear of the prompt that
    // names it, which is drawn after this and used to sit on top of it.
    const float up = wp.here ? 54.0f : 50.0f;
    const SDL_FPoint p = UiPoint(wp.local_x, wp.local_y - lift - up);
    const float w = ui.ViewWidth(), h = ui.ViewHeight();
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const auto tri = [&](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_Color col) {
        const SDL_FColor fc = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, col.a / 255.0f};
        const SDL_Vertex v[3] = {{a, fc, {0, 0}}, {b, fc, {0, 0}}, {c, fc, {0, 0}}};
        SDL_RenderGeometry(renderer, nullptr, v, 3, nullptr, 0);
    };
    const SDL_Color gold = {255, 214, 96, 255}, edge = {40, 28, 12, 235};
    const int paces = static_cast<int>(Length(wp.local_x - world->player.x, wp.local_y - world->player.y) / 32.0f);

    const float inset_x = 56.0f, inset_top = 96.0f, inset_bottom = 76.0f;
    const bool in_view = p.x > inset_x && p.x < w - inset_x && p.y > inset_top && p.y < h - inset_bottom;
    if (in_view) {
        // Not over somebody the player is already standing beside: it would sit on their name.
        if (paces < 2) return;
        const float bob = roundf(sinf(t * 4.0f) * 4.0f);
        const SDL_FPoint tip = {roundf(p.x), roundf(p.y + bob)};
        tri({tip.x - 12.0f, tip.y - 19.0f}, {tip.x + 12.0f, tip.y - 19.0f}, {tip.x, tip.y + 3.0f}, edge);
        tri({tip.x - 8.0f, tip.y - 16.0f}, {tip.x + 8.0f, tip.y - 16.0f}, {tip.x, tip.y - 1.0f}, gold);
        return;
    }
    // Out of sight: on the line from the middle of the view to it, as far out as the insets allow.
    const float cx = w / 2.0f, cy = h / 2.0f;
    float dx = p.x - cx, dy = p.y - cy;
    const float len = std::max(1.0f, Length(dx, dy));
    dx /= len; dy /= len;
    const float reach_x = dx > 0 ? (w - inset_x - cx) : (cx - inset_x);
    const float reach_y = dy > 0 ? (h - inset_bottom - cy) : (cy - inset_top);
    const float k = std::min(fabsf(dx) > 0.001f ? reach_x / fabsf(dx) : 1.0e9f, fabsf(dy) > 0.001f ? reach_y / fabsf(dy) : 1.0e9f);
    const SDL_FPoint at = {roundf(cx + dx * k), roundf(cy + dy * k)};
    const float nx = -dy, ny = dx;
    const float grow = 1.0f + 0.12f * sinf(t * 5.0f);
    const auto arrow = [&](float size, SDL_Color col) {
        tri({at.x + dx * size, at.y + dy * size},
            {at.x - dx * size * 0.7f + nx * size * 0.75f, at.y - dy * size * 0.7f + ny * size * 0.75f},
            {at.x - dx * size * 0.7f - nx * size * 0.75f, at.y - dy * size * 0.7f - ny * size * 0.75f}, col);
    };
    arrow(18.0f * grow, edge);
    arrow(13.0f * grow, gold);
    ui.TextShadowed(std::to_string(paces), at.x - dx * 30.0f, at.y - dy * 30.0f - 8.0f, TextSize::Small, gold, Align::Center);
}

void Game::DrawXpLines(float x, float y) {
    const Player& p = world->player;
    for (const XpLine& line : xp_lines) {
        // How far through the level this leaves you: the number that says
        // whether the next one is a swing away or an evening.
        const int level = p.skills.Level(line.skill);
        const int have = p.skills.Xp(line.skill);
        const int from = XpForLevel(level), to = XpForLevel(level + 1);
        char buf[96];
        if (level >= MAX_SKILL_LEVEL || to <= from)
            SDL_snprintf(buf, sizeof(buf), "+%d %s", line.amount, SkillName(line.skill));
        else
            SDL_snprintf(buf, sizeof(buf), "+%d %s   %d%%", line.amount, SkillName(line.skill),
                         std::clamp(static_cast<int>(100.0f * (have - from) / (to - from)), 0, 99));
        SDL_Color c = Palette::Xp;
        c.a = static_cast<Uint8>(255.0f * std::clamp((2.8f - line.age) / 0.6f, 0.0f, 1.0f));
        ui.TextShadowed(buf, x, y, TextSize::Small, c);
        y += ui.LineHeight(TextSize::Small) + 2.0f;
    }
}

void Game::DrawHud() {
    const Player& p = world->player;
    const Waypoint& waypoint = CurrentWaypoint();

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
        // Out of breath and out of guard pulse the same red: both are the bar
        // run dry, and both lift at the same point. A raised guard turns the
        // bar steel blue, because it is what every blow on the shield is paid
        // out of and that is worth seeing at a glance.
        const bool spent = p.Winded() || p.GuardBroken();
        if (spent) {
            const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) * 0.012f);
            fill = {static_cast<Uint8>(196 + 40 * pulse), static_cast<Uint8>(70 + 30 * pulse),
                    static_cast<Uint8>(48), 255};
        } else if (p.Blocking()) {
            fill = {122, 166, 214, 255};
        }
        ui.FramedBar(st_bar, p.Stamina() / p.MaxStamina(), fill, Palette::StaminaBack);
        const char* label = p.GuardBroken() ? "guard broken" : p.Winded() ? "winded"
                          : p.Blocking() ? "guard" : nullptr;
        if (label)
            ui.TextShadowed(label, st_bar.x + st_bar.w / 2.0f,
                            st_bar.y + (st_bar.h - line_h) / 2.0f,
                            TextSize::Small, Palette::Text, Align::Center);
        meta_y = st_bar.y + st_bar.h + 6.0f;
    }

    // --- Rushing Strike ------------------------------------------------------
    // A hairline under the stamina bar, only for a character who has the move
    // and a melee weapon to make it with: amber and full when a running light
    // attack will leap, filling back up over the three seconds after one.
    if (p.talents.Effect("rushing_strike", AttackStyle::Melee) > 0.0f && p.Style() == AttackStyle::Melee) {
        const float ready = 1.0f - std::clamp(p.RushCooldown() / Player::RUSH_COOLDOWN, 0.0f, 1.0f);
        const SDL_FRect rush_bar = {hp_bar.x, meta_y - 3.0f, 232.0f, 5.0f};
        ui.Fill(rush_bar, {24, 20, 18, 220});
        ui.Fill({rush_bar.x, rush_bar.y, rush_bar.w * ready, rush_bar.h},
                ready >= 1.0f ? SDL_Color{236, 176, 72, 255} : SDL_Color{120, 100, 70, 255});
        meta_y = rush_bar.y + rush_bar.h + 6.0f;
    }

    // --- minimap -------------------------------------------------------------
    // Top right, with the bezel hung on the corner; everything else that used
    // to live in that corner now stacks below it.
    (serving == 1 ? minimap_two : minimap).Draw(renderer, *textures, ui, (*world),
                 ui.ViewWidth() - kHudMargin - kMinimapRing / 2.0f,
                 kHudMargin + kMinimapRing / 2.0f, kMinimapGlass, &waypoint);

    char meta[96];
    SDL_snprintf(meta, sizeof(meta), "Combat %d    %d coins",
                 p.skills.CombatLevel(), p.inventory.Coins());
    ui.TextShadowed(meta, hp_bar.x, meta_y, TextSize::Small, Palette::TextDim);

    // --- the time ------------------------------------------------------------
    // A sun or a moon and the hour, under the vitals. In a dream it counts
    // down to dawn instead, which is when the dream ends.
    {
        const WorldClock& c = world->clock;
        const float y = meta_y + line_h + 4.0f;
        const bool moon = world->InDream() || c.IsNight() || string(c.Phase()) == "Dusk";
        glyph_plate({18.0f, y - 2.0f, glyph, line_h + 4.0f},
                    moon ? "assets/icons/hud_moon.png" : "assets/icons/hud_sun.png");
        char line[96];
        SDL_Color col = Palette::TextDim;
        if (world->InDream()) {
            const int s = static_cast<int>(c.SecondsToDawn());
            SDL_snprintf(line, sizeof(line), "Dreaming    dawn in %d:%02d", s / 60, s % 60);
            col = {206, 186, 250, 255};
        } else {
            SDL_snprintf(line, sizeof(line), "Day %d    %s    %s", c.Day(), c.TimeText().c_str(), c.Phase());
            if (c.CanSleep()) col = {176, 186, 236, 255};
        }
        ui.TextShadowed(line, hp_bar.x, y, TextSize::Small, col);
    }

    // Down the left edge, under everything that lives in the top corner.
    if (settings.xp_drops) DrawXpLines(hp_bar.x, floorf(ui.ViewHeight() * 0.40f));

    // Meters and prompts describe what the button does right now, so they are
    // only meaningful while the player actually has control.
    const bool live = (state == GameState::Play);

    // --- abilities -----------------------------------------------------------
    // The ones carried, bottom left, stacked up from the hint line: the keys,
    // the name, and a bar that refills as it comes back. What is running -- a
    // war cry, a mana shield -- beside. Drawn ahead of the middle of the foot
    // of the screen, which has to know how much of this corner is taken: the
    // boxes, and the line beside them.
    SDL_FRect corner[2] = {};
    {
        const Player& me = world->player;
        int carrying = 0;
        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) carrying += me.talents.Ability(slot) ? 1 : 0;
        // What is to hand goes at the foot of the same stack, when there is
        // anything that could be: a box like an ability's, with how many are
        // left where the cooldown would be.
        string quick = me.QuickItem();
        if (quick.empty() && !me.QuickChoices().empty()) quick = me.QuickChoices().front();
        const ItemDef* quick_def = quick.empty() ? nullptr : items.Get(quick);
        const int rows_up = carrying + (quick_def ? 1 : 0);
        const float ay = ui.ViewHeight() - 40.0f - 30.0f * static_cast<float>(std::max(1, rows_up));
        if (quick_def && live) {
            const int have = me.inventory.Count(quick);
            const SDL_FRect box = {18.0f, ay + static_cast<float>(carrying) * 30.0f, 214.0f, 26.0f};
            const float chewing = std::clamp(me.EatCooldown() / Player::EAT_COOLDOWN, 0.0f, 1.0f);
            ui.Fill(box, {18, 15, 13, 190});
            if (have > 0)
                ui.Fill({box.x, box.y, box.w * (1.0f - chewing), box.h},
                        chewing > 0.0f ? SDL_Color{58, 70, 48, 210} : SDL_Color{52, 92, 56, 225});
            ui.Outline(box, have > 0 && chewing <= 0.0f ? Palette::Xp : Palette::BorderDim, 1.0f);
            ui.Text(input.PromptFor(Action::Block) + "+" + input.PromptFor(Action::Interact),
                    box.x + 6.0f, box.y + 4.0f, TextSize::Small, Palette::TextDim);
            if (!quick_def->icon.empty())
                if (SDL_Texture* tex = textures->Get(quick_def->icon)) {
                    const SDL_FRect ic = {box.x + 60.0f, box.y + 3.0f, 20.0f, 20.0f};
                    SDL_RenderTexture(renderer, tex, nullptr, &ic);
                }
            ui.Text(quick_def->name, box.x + 84.0f, box.y + 4.0f, TextSize::Small,
                    have > 0 ? Palette::Text : SDL_Color{150, 110, 100, 255});
            ui.Text("x" + std::to_string(have), box.x + box.w - 8.0f, box.y + 4.0f, TextSize::Small,
                    have > 0 ? Palette::TextDim : SDL_Color{200, 110, 100, 255}, Align::Right);
        }
        int drawn = 0;
        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
            const TalentNode* carried = me.talents.Ability(slot);
            if (!carried) continue;
            const SDL_FRect box = {18.0f, ay + static_cast<float>(drawn++) * 30.0f, 214.0f, 26.0f};
            const float left = me.AbilityCooldown(slot);
            const float ready = carried->cooldown > 0.0f ? 1.0f - std::clamp(left / carried->cooldown, 0.0f, 1.0f) : 1.0f;
            const bool afford = (carried->stamina_cost <= 0 || me.Stamina() >= carried->stamina_cost) &&
                                (carried->mana_cost <= 0 || me.Mana() >= carried->mana_cost);
            ui.Fill(box, {18, 15, 13, 190});
            ui.Fill({box.x, box.y, box.w * ready, box.h}, left > 0.0f ? SDL_Color{52, 66, 88, 210}
                                                         : afford ? SDL_Color{46, 84, 120, 225} : SDL_Color{88, 52, 46, 215});
            ui.Outline(box, left > 0.0f ? Palette::BorderDim : SDL_Color{130, 190, 240, 255}, 1.0f);
            ui.Text(input.PromptFor(Action::Block) + "+" + input.PromptFor(AbilityButton(slot)),
                    box.x + 6.0f, box.y + 4.0f, TextSize::Small, Palette::TextDim);
            ui.Text(carried->name, box.x + 62.0f, box.y + 4.0f, TextSize::Small, left > 0.0f ? Palette::TextDim : Palette::Text);
            if (left > 0.0f)
                ui.Text(std::to_string(static_cast<int>(std::ceil(left))), box.x + box.w - 8.0f, box.y + 4.0f,
                        TextSize::Small, Palette::TextDim, Align::Right);
        }
        string running;
        if (me.WarCry())     running += "War Cry " + std::to_string(static_cast<int>(std::ceil(me.WarCryLeft()))) + "   ";
        if (me.ManaShield()) running += "Mana Shield " + std::to_string(static_cast<int>(std::ceil(me.ManaShieldLeft()))) + "   ";
        if (me.Frenzied())     running += "Frenzy " + std::to_string(static_cast<int>(std::ceil(me.FrenzyLeft()))) + "   ";
        if (me.StandingFast()) running += "Stand Fast " + std::to_string(static_cast<int>(std::ceil(me.StandFastLeft()))) + "   ";
        if (me.RapidFire())    running += "Rapid Fire " + std::to_string(static_cast<int>(std::ceil(me.RapidFireLeft()))) + "   ";
        if (me.Aiming())       running += "Aim held   ";
        if (me.Overloaded())   running += "Overloaded   ";
        if (me.Invoking())     running += "Invoking   ";
        if (me.RiposteReady()) running += "Riposte ready   ";
        if (!running.empty()) ui.TextShadowed(running, 240.0f, ay + 6.0f, TextSize::Small, {255, 214, 140, 255});

        if (rows_up > 0) corner[0] = {18.0f, ay, 214.0f, 30.0f * static_cast<float>(rows_up) - 4.0f};
        if (!running.empty()) corner[1] = {240.0f, ay + 6.0f, ui.Measure(running, TextSize::Small).x, line_h};
        ui.Claim("abilities", corner[0]);
        ui.Claim("what is running", corner[1]);
    }

    // --- the foot of the screen -------------------------------------------------
    // The key hints run along the bottom edge, and what the buttons will do
    // right now is stacked up from them in the middle: the element bar with its
    // spell named under it, the line for a combo or a technique, the prompt.
    // Each goes on top of the last and is measured, not counted up from the
    // bottom on its own -- which is how the spell's name and the key hints came
    // to be written on the same line, twenty-eight up, and a combo across the
    // element bar. At no size of window or of interface is there a line here
    // with two things on it.
    //
    // In half a screen the middle reaches over the corner the abilities are
    // in. Whatever would land on them there goes above them instead, and the
    // rest of the stack on top of that.
    const float foot_y = ui.ViewHeight() - 28.0f;
    float stack_y = foot_y;         // the top of whatever was stacked last
    float stack_half = 0.0f;        // and half the width of the widest of it
    const auto stack = [&](const char* what, float w, float h, float gap) {
        const float left = roundf(ui.ViewWidth() / 2.0f - w / 2.0f);
        float top = stack_y - gap - h;
        for (const SDL_FRect& c : corner)
            if (c.w > 0.0f && left < c.x + c.w + 8.0f && top < c.y + c.h + 4.0f && top + h > c.y - 4.0f)
                top = c.y - 4.0f - h;
        stack_y = top;
        stack_half = std::max(stack_half, w / 2.0f);
        ui.Claim(what, {left, top, w, h});
        return top;
    };

    // --- what the attack button will do ---------------------------------------
    // A staff shows the four elements with the selected one lit, and names the
    // spell that Magic level actually casts. A bow just says so.
    if (live) {
        const AttackStyle style = p.Style();
        if (style == AttackStyle::Magic) {
            static const Element kOrder[5] = {Element::Fire, Element::Water,
                                              Element::Earth, Element::Air, Element::Arcane};
            const float box = 30.0f, gap = 5.0f;
            const float total = box * 5 + gap * 4;
            const float x0 = ui.ViewWidth() / 2.0f - total / 2.0f;
            const vector<string> arcane_known = world->KnownArcane(spells);

            // The line under the boxes first: the bar and its name are one
            // piece of the stack, as wide as the wider of them.
            const bool arcane_on = p.SelectedElement() == Element::Arcane;
            const SpellDef* current = arcane_on ? spells.Get(p.ArcaneSpell())
                                                : spells.BestFor(p.SelectedElement(), p.skills.Level(SKILL_MAGIC));
            string line;
            if (current && arcane_on && current->level > p.skills.Level(SKILL_MAGIC)) {
                line = current->name + "   needs Magic " + std::to_string(current->level);
            } else if (current) {
                line = current->name + "   " + std::to_string(current->mana) + " mana";
                if (arcane_on && arcane_known.size() > 1)
                    line += "   " + input.PromptFor(Action::SelectArcane) + " again: next";
                // A staff's technique rides on the same line as the spell.
                if (const TalentNode* t = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique()))
                    line += "     hold " + input.PromptFor(Action::StrongAttack) + ": " + t->name;
            } else {
                const SpellDef* next = arcane_on ? nullptr
                                     : spells.NextFor(p.SelectedElement(), p.skills.Level(SKILL_MAGIC));
                line = next ? ("Magic " + std::to_string(next->level) + " for " + next->name)
                            : arcane_on ? "No ancient magic known" : "Nothing known";
            }
            const float y0 = stack("element bar", std::max(total, ui.Measure(line, TextSize::Small).x),
                                   box + 4.0f + line_h, 5.0f);

            for (int i = 0; i < 5; ++i) {
                const bool on = (kOrder[i] == p.SelectedElement());
                const SDL_FRect r = {x0 + i * (box + gap), y0, box, box};
                const SDL_Color c = ElementColor(kOrder[i]);
                // The fifth box is lit once any ancient spell is known.
                const SpellDef* known = kOrder[i] == Element::Arcane
                    ? (arcane_known.empty() ? nullptr : spells.Get(arcane_known.front()))
                    : spells.BestFor(kOrder[i], p.skills.Level(SKILL_MAGIC));

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

            ui.TextShadowed(line, ui.ViewWidth() / 2.0f, y0 + box + 4.0f,
                            TextSize::Small,
                            current ? ElementColor(p.SelectedElement()) : Palette::TextDim,
                            Align::Center);
        }

        // The technique a held heavy attack will come out as, from the tree --
        // on the line under the prompts, where the bow says it is drawn. The
        // line keeps its place in the stack with nothing on it, so a prompt
        // does not hop up for the two fifths of a second a chain is open.
        const TalentNode* tech = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique());
        const ComboMove next_light = p.NextCombo(true), next_heavy = p.NextCombo(false);
        string line;
        SDL_Color line_col = Palette::TextDim;
        if (next_light != ComboMove::None || next_heavy != ComboMove::None) {
            // The chain is open: what each button would come out as, for the
            // moment the window lasts.
            if (next_light != ComboMove::None)
                line += input.PromptFor(Action::LightAttack) + ": " + ComboNameFor(next_light, style);
            if (next_heavy != ComboMove::None)
                line += (line.empty() ? string("") : string("     ")) +
                        input.PromptFor(Action::StrongAttack) + ": " + ComboNameFor(next_heavy, style);
            line_col = {255, 224, 140, 255};
        } else if (tech && style != AttackStyle::Magic) {
            line = "Hold " + input.PromptFor(Action::StrongAttack) + ": " + tech->name;
            line_col = {236, 150, 110, 255};
        } else if (style == AttackStyle::Ranged) {
            line = "Bow drawn";
        }
        const float line_y = stack("attack line", line.empty() ? 0.0f : ui.Measure(line, TextSize::Small).x, line_h, 5.0f);
        ui.TextShadowed(line, ui.ViewWidth() / 2.0f, line_y, TextSize::Small, line_col, Align::Center);
    }

    // --- the chain -------------------------------------------------------------
    // Melee swings landed one after another, under the target frame: the
    // count large, and what each swing was on a line beneath it. It holds
    // for a moment after the last hit and fades; a swing that lands on
    // nothing or a blow taken ends it. From two, so a single hit is not an
    // announcement.
    if (live && p.ChainHits() >= 2 && p.ChainFade() > 0.0f) {
        const float fade = p.ChainFade();
        const int hits = p.ChainHits();
        // Pale, then amber, then ember as the run grows.
        SDL_Color col = hits >= 8 ? SDL_Color{255, 128, 72, 255}
                      : hits >= 5 ? SDL_Color{255, 204, 96, 255}
                                  : SDL_Color{240, 236, 220, 255};
        col.a = static_cast<Uint8>(255.0f * fade);
        const float cx = ui.ViewWidth() / 2.0f, y = 70.0f;
        ui.TextShadowed(std::to_string(hits), cx - 6.0f, y, TextSize::Large, col, Align::Right);
        ui.TextShadowed("HITS", cx + 4.0f, y + ui.LineHeight(TextSize::Large) - ui.LineHeight(TextSize::Small) - 2.0f,
                        TextSize::Small, col, Align::Left);
        const auto& t = p.ChainTrail();
        string trail;
        for (size_t i = 0; i < t.size(); ++i) trail += (i ? "  >  " : "") + t[i];
        if (hits > static_cast<int>(t.size())) trail = "...  >  " + trail;
        const SDL_Color dim = {col.r, col.g, col.b, static_cast<Uint8>(215.0f * fade)};
        ui.TextShadowed(trail, cx, y + ui.LineHeight(TextSize::Large) + 2.0f, TextSize::Small, dim, Align::Center);
    }

    // --- target frame ----------------------------------------------------------
    // Who the fight is with, at the top of the screen: name, level and health,
    // with a red frame and a LOCKED tag while the lock is on. Out of combat
    // there is no target, and nothing is drawn.
    if (live) {
        if (const Enemy* t = world->targeting.Current(); t && t->Def()) {
            const bool lock = world->targeting.IsLocked();
            const float w = 280.0f, cx = ui.ViewWidth() / 2.0f;
            const SDL_FRect box = {roundf(cx - w / 2.0f), 12.0f, w, 48.0f};
            ui.Fill(box, {14, 11, 9, 200});
            ui.Outline(box, lock ? SDL_Color{206, 70, 56, 255} : Palette::BorderDim, lock ? 2.0f : 1.0f);
            // `boss` in enemies.json was read into the definition and then by
            // nothing. It is what tells a player the thing in front of them is
            // not one of a pack, so the frame says so, in the gold a rare
            // thing is written in.
            const bool boss = t->Def()->is_boss;
            ui.TextShadowed(t->Def()->name, box.x + 12.0f, box.y + 5.0f, TextSize::Small,
                            boss ? SDL_Color{255, 214, 96, 255} : (lock ? Palette::Highlight : Palette::Text));
            const string level_tag = string(boss ? "Boss   Lv " : "Lv ") + std::to_string(t->ShownLevel());
            ui.TextShadowed(lock ? "LOCKED   " + level_tag
                                 : input.PromptFor(Action::Target) + " lock   " + level_tag,
                            box.x + box.w - 12.0f, box.y + 5.0f, TextSize::Small,
                            lock ? SDL_Color{236, 110, 90, 255} : Palette::TextDim, Align::Right);
            ui.FramedBar({box.x + 12.0f, box.y + 27.0f, box.w - 24.0f, 12.0f}, t->HealthFraction(),
                         {196, 44, 40, 255}, {40, 16, 14, 255});
        }
    }

    // --- charge meter --------------------------------------------------------
    if (live && p.IsCharging()) {
        const float t = p.ChargeProgress();
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
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
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
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
                               stack("prompt", size.x + 28.0f, size.y + 12.0f, 17.0f),
                               size.x + 28.0f, size.y + 12.0f};
        ui.Panel(box);
        ui.Text(prompt, box.x + 14.0f, box.y + 6.0f, TextSize::Body, Palette::Highlight);
    }

    // --- gathering -----------------------------------------------------------
    if (live && world->Gathering()) {
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 34.0f, anchor.y + 8.0f, 68.0f, 8.0f};
        ui.Bar(bar, world->GatherProgress(), Palette::Xp, {20, 30, 20, 220});
    }

    // --- interact prompt -----------------------------------------------------
    // The top of the stack at the foot of the screen, like the ledge's: there
    // is only ever one of the two.
    if (live && p.interact.kind != InteractTarget::None && !world->Gathering()) {
        const string prompt = "[" + input.PromptFor(Action::Interact) + "]  " + p.interact.label;
        const SDL_FPoint size = ui.Measure(prompt, TextSize::Body);
        const SDL_FRect box = {ui.ViewWidth() / 2.0f - size.x / 2.0f - 14.0f,
                               stack("prompt", size.x + 28.0f, size.y + 12.0f, 17.0f),
                               size.x + 28.0f, size.y + 12.0f};
        ui.Fill(box, {20, 16, 12, 210});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        ui.Text(prompt, ui.ViewWidth() / 2.0f, box.y + 6.0f, TextSize::Body,
                Palette::Text, Align::Center);
    }

    // --- the ways out ---------------------------------------------------------
    // An exit at the edge of an outdoor map is a gap in the trees, and easy to
    // walk straight past. Near one, say where it goes and which way.
    if (live && !world->CurrentMap().IsInterior()) {
        const Map& mp = world->CurrentMap();
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

            SDL_FPoint s = UiPoint(px, py);
            const SDL_FPoint size = ui.Measure(text, TextSize::Small);
            s.x = std::clamp(s.x - nx * 90.0f, size.x / 2.0f + 12.0f, ui.ViewWidth() - size.x / 2.0f - 12.0f);
            // A way out to the south is under the player's feet, which is
            // under the middle of the screen: over the stack at its foot, the
            // label sits on top of that rather than across it.
            const bool over_stack = fabsf(s.x - ui.ViewWidth() / 2.0f) < size.x / 2.0f + 8.0f + stack_half + 8.0f;
            const float lowest = over_stack ? std::min(ui.ViewHeight() - 60.0f, stack_y - size.y - 12.0f)
                                            : ui.ViewHeight() - 60.0f;
            s.y = std::clamp(s.y - ny * 70.0f, 40.0f, std::max(40.0f, lowest));

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

    // Where the quest is: over the world and the arrival banner, which dimmed it,
    // and under the tracker and the bars.
    if (state == GameState::Play) DrawWaypoint(waypoint);

    // --- quest tracker -------------------------------------------------------
    vector<string> active = quests->Active();
    const string followed = quests->Followed();
    // The one being followed leads, and says where it is.
    {
        const auto it = std::find(active.begin(), active.end(), followed);
        if (it != active.end()) std::rotate(active.begin(), it, it + 1);
    }
    string where_line;
    if (settings.quest_waypoints && !followed.empty()) {
        if (waypoint.found && waypoint.here) {
            const float wx = waypoint.local_x - p.x, wy = waypoint.local_y - p.y;
            const int paces = static_cast<int>(Length(wx, wy) / 32.0f);
            static const char* kRose[8] = {"east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east"};
            const int point = ((static_cast<int>(roundf(atan2f(wy, wx) / 0.7853982f)) % 8) + 8) % 8;
            where_line = waypoint.what + (paces < 3 ? string(", right here") : ", " + std::to_string(paces) + " paces " + kRose[point]);
            if (!waypoint.hint.empty()) where_line = waypoint.hint;
        } else if (waypoint.found) {
            where_line = "in " + waypoint.place + "  -  " + (waypoint.via.empty() ? string("this way") : waypoint.via);
        } else if (!waypoint.hint.empty()) {
            where_line = waypoint.hint;
        }
    }
    // Sits below the minimap, and below however many toasts are stacked. Three
    // at most, and fewer where three would run off the bottom: half a split
    // screen, one above the other, in the smallest window has room under the
    // minimap for one -- and drew three, the last of them off its half.
    const float tracker_y = kHudRightTop + toasts.size() * 20.0f;
    const float tracker_room = ui.ViewHeight() - 8.0f - (tracker_y + 18.0f) - (where_line.empty() ? 0.0f : 18.0f);
    const size_t shown = std::min<size_t>({active.size(), size_t{3},
                                           static_cast<size_t>(std::max(0.0f, tracker_room) / 42.0f)});
    if (shown > 0) {
        const float right = ui.ViewWidth() - 18.0f;
        float y = tracker_y;

        // A dark backing sized to the text. The dim objective lines were
        // unreadable over the pale olive grass and the road, shadow or not.
        {
            float widest = ui.Measure("QUESTS", TextSize::Small).x;
            for (size_t i = 0; i < shown; ++i) {
                if (const QuestDef* d = quests->Definition(active[i])) {
                    widest = std::max(widest, ui.Measure(d->name, TextSize::Small).x);
                    widest = std::max(widest, ui.Measure(quests->CurrentObjectiveText(active[i]),
                                                         TextSize::Small).x);
                }
            }
            if (!where_line.empty()) widest = std::max(widest, ui.Measure(where_line, TextSize::Small).x);
            const SDL_FRect back = {right - widest - 10.0f, y - 6.0f, widest + 20.0f,
                                    22.0f + shown * 42.0f + 2.0f + (where_line.empty() ? 0.0f : 18.0f)};
            ui.Fill(back, {14, 11, 9, 150});
        }

        ui.TextShadowed("QUESTS", right, y, TextSize::Small, Palette::Highlight, Align::Right);
        y += 22.0f;

        // Show at most three so the tracker never crowds the view.
        for (size_t i = 0; i < shown; ++i) {
            const QuestDef* d = quests->Definition(active[i]);
            if (!d) continue;
            const bool leading = active[i] == followed && settings.quest_waypoints;
            ui.TextShadowed(d->name, right, y, TextSize::Small,
                            leading ? SDL_Color{255, 214, 96, 255} : Palette::Text, Align::Right);
            y += 18.0f;
            ui.TextShadowed(quests->CurrentObjectiveText(active[i]), right, y,
                            TextSize::Small, Palette::TextDim, Align::Right);
            if (leading && !where_line.empty()) {
                y += 18.0f;
                ui.TextShadowed(where_line, right, y, TextSize::Small, {226, 196, 120, 255}, Align::Right);
            }
            y += 24.0f;
        }
    }

    // What was last eaten, under the vitals, while it lasts: a meal is worth
    // knowing about, and worth knowing the end of.
    if (const ItemDef* dish = p.Meal()) {
        const int left = static_cast<int>(p.MealLeft());
        char fed[96];
        SDL_snprintf(fed, sizeof(fed), "%s  %d:%02d", dish->name.c_str(), left / 60, left % 60);
        ui.TextShadowed(fed, 18.0f, 16.0f + 3.0f * 22.0f + 26.0f, TextSize::Small, {186, 226, 150, 255});
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
                        // Only worth a word when there is a shield to raise.
                        (p.Shield() ? input.PromptFor(Action::Block) + " block    " : string()) +
                        input.PromptFor(Action::Sprint) + " sprint    " +
                        input.PromptFor(Action::Inventory) + " bag    " +
                        input.PromptFor(Action::Skills) + " skills    " +
                        input.PromptFor(Action::QuestLog) + " quests    " +
                        input.PromptFor(Action::WorldMap) + " map    " +
                        input.PromptFor(Action::Pause) + " menu";
    // Half a screen has no room for the line, and two players know the keys.
    // Its place is kept all the same, so the stack above it is where it was.
    if (!split_active) {
        ui.TextShadowed(hint, 18.0f, foot_y, TextSize::Small, Palette::TextDim);
        if (ui.Auditing()) ui.Claim("key hints", {18.0f, foot_y, ui.Measure(hint, TextSize::Small).x, line_h});
    }
}

void Game::DrawToasts() {
    // Under the minimap while a game is running; at the top on the menus,
    // where there is no minimap to clear.
    float y = InGameplayState() ? kHudRightTop : 18.0f;
    float right = ui.ViewWidth() - 18.0f;
    // With the skill tree open they are about what was just bought, and where
    // they usually go is where the tree says what a node does. Inside the
    // panel instead, in the gap under the description.
    if (state == GameState::SkillsPanel && skills_tab == 1) {
        const AttackStyle mine = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        const float tree_w = 860.0f + 172.0f * (skill_trees.Tree(mine).BranchCount() - SkillTrees::BRANCHES);
        y = ui.ViewHeight() / 2.0f + 8.0f;
        right = ui.ViewWidth() / 2.0f + tree_w / 2.0f - 24.0f;
    }

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
    Player& p = world->player;
    constexpr int COLS = 7;
    const int slots = p.inventory.SlotCount();
    inventory_cursor = std::clamp(inventory_cursor, 0, slots - 1);
    if (state_time <= 0.0f) drop_armed = -1;

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
        if (inventory_cursor != grid_before) drop_armed = -1;

        if (input.Pressed(Action::Confirm)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            if (!s.Empty()) {
                const ItemDef* def = items.Get(s.id);
                if (def && def->consumable) {
                    const bool potion = !def->boosts.empty() || def->mana > 0 || def->stamina ||
                                        std::find(def->tags.begin(), def->tags.end(), "potion") != def->tags.end();
                    string why;
                    if (p.Consume(inventory_cursor, why)) {
                        PushToast((potion ? "You drink the " : "You eat the ") + def->name + ".", Palette::Xp);
                        Audio::Play(Sfx::Eat);
                    } else {
                        PushToast(why, Palette::TextDim);
                    }
                } else if (def && !def->learn.empty()) {
                    // A recipe scroll: a brew, or -- when it reads
                    // "enchant:<id>" -- a charm for the enchanting table.
                    const bool charm = def->learn.rfind("enchant:", 0) == 0;
                    const bool tome  = def->learn.rfind("spell:", 0) == 0;
                    const EnchantDef* ench = charm ? items.Enchantment(def->learn.substr(8)) : nullptr;
                    const SpellDef* sp = tome ? spells.Get(def->learn.substr(6)) : nullptr;
                    const ItemDef* brew = (charm || tome) ? nullptr : items.Get(def->learn);
                    const string name = ench ? ench->name : sp ? sp->name : brew ? brew->name : def->learn;
                    if (world->KnowsRecipe(def->learn)) {
                        PushToast(charm ? "You already know the enchantment " + name + "."
                                  : tome ? "You already know " + name + "."
                                         : "You already know how to brew " + name + ".", Palette::TextDim);
                    } else {
                        world->SetFlag("recipe:" + def->learn);
                        p.inventory.RemoveSlot(inventory_cursor, 1);
                        PushToast(charm ? "Enchantment learned: " + name + ". Work it at an enchanting table."
                                  : tome ? "Spell learned: " + name + ". Press " + input.PromptFor(Action::SelectArcane) +
                                           " with a staff in hand."
                                         : "Recipe learned: " + name + ". Brew it at a cauldron.", Palette::Highlight);
                        Audio::Play(Sfx::QuestStart);
                    }
                } else if (def && def->use == "light") {
                    // A firestarter is no use on its own: it lights whatever
                    // in the bag says what it becomes when lit, and it is not
                    // used up doing it.
                    int slot = -1;
                    for (int k = 0; k < p.inventory.SlotCount() && slot < 0; ++k) {
                        const ItemDef* carried = items.Get(p.inventory.Slot(k).id);
                        if (carried && !carried->lights.empty()) slot = k;
                    }
                    const ItemDef* unlit = slot < 0 ? nullptr : items.Get(p.inventory.Slot(slot).id);
                    const ItemDef* lit = unlit ? items.Get(unlit->lights) : nullptr;
                    if (!lit) {
                        PushToast("You have nothing to light.", Palette::TextDim);
                        Audio::Play(Sfx::UiError);
                    } else {
                        p.inventory.RemoveSlot(slot, 1);
                        p.inventory.Add(lit->id, 1);
                        // Both names read badly in the sentence ("You light the
                        // Lit Lantern", "You light the Unlit Lantern"), so the
                        // state word is dropped and the rest lower-cased: what
                        // you lit is a lantern.
                        string what = lit->name;
                        if (what.rfind("Lit ", 0) == 0) what = what.substr(4);
                        for (char& c : what) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                        PushToast("You light the " + what + ".", Palette::Highlight);
                        Audio::Play(Sfx::QuestStart);
                        quests->RefreshCollectObjectives(p.inventory);
                    }
                } else if (def && def->use == "bag") {
                    string why;
                    const string name = def->name;
                    if (p.WearBag(inventory_cursor, why)) {
                        PushToast("You shoulder the " + name + ". Your bag holds " +
                                  std::to_string(p.inventory.SlotCount()) + " now.", Palette::Highlight);
                        Audio::Play(Sfx::Equip);
                    } else {
                        PushToast(why, Palette::TextDim);
                        Audio::Play(Sfx::UiError);
                    }
                } else if (def && def->use == "camp") {
                    const string why = world->PitchCamp(inventory_cursor, ctx);
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

        // The drop key: what the cursor is on goes on the ground at the
        // player's feet, where it can be picked back up once they have
        // stepped off it, and lies for a few minutes. A stack asks to be
        // pressed twice, so a purse of coins is not one slip from the floor.
        // Leaving the map loses it, which is as close to destroying a thing
        // as the game gets.
        // The lock-on button, which has nothing to lock on to in a bag, puts
        // what the cursor is on to hand.
        if (input.Pressed(Action::Target)) {
            const ItemStack& under = p.inventory.Slot(inventory_cursor);
            const ItemDef* ud = under.Empty() ? nullptr : items.Get(under.id);
            if (ud && ud->consumable) {
                p.SetQuickItem(under.id);
                PushToast(ud->name + " is to hand: " + input.PromptFor(Action::Block) + " + " +
                          input.PromptFor(Action::Interact) + " uses it.", Palette::Xp);
                Audio::Play(Sfx::Equip);
            } else if (ud) {
                PushToast("Only something to eat or drink can be kept to hand.", Palette::TextDim);
            }
        }
        if (input.Pressed(Action::Drop)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            const ItemDef* def = s.Empty() ? nullptr : items.Get(s.id);
            if (s.Empty()) {
                drop_armed = -1;
            } else if (def && def->keep) {
                PushToast("You had better hold on to that.", Palette::TextDim);
                Audio::Play(Sfx::UiError);
            } else if (s.qty > 1 && drop_armed != inventory_cursor) {
                drop_armed = inventory_cursor;
                PushToast("Press " + input.PromptFor(Action::Drop) + " again to drop all " +
                          std::to_string(s.qty) + " " + (def ? def->name : s.id) + ".", Palette::Highlight);
            } else {
                const string id = s.id;
                const string name = def ? def->name : id;
                const int qty = s.qty;
                p.inventory.RemoveSlot(inventory_cursor, qty);
                world->DropItem(id, qty, p.x, p.y + 4.0f, ctx, true);
                PushToast("Dropped " + (qty > 1 ? std::to_string(qty) + " " : string("")) + name + ".",
                          Palette::TextDim);
                Audio::Play(Sfx::Pickup, 0.8f, 0.8f);
                quests->RefreshCollectObjectives(p.inventory);
                drop_armed = -1;
            }
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Inventory) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawInventory() {
    ui.Dim(0.5f);
    Player& p = world->player;

    // Four rows of seven, and a row more for every bag put on, up to eight.
    // The squares stay the size they were for as long as they fit, and the
    // grid keeps its width, so the worn list beside it does not move.
    constexpr int COLS = 7;
    const int rows = std::max(4, (p.inventory.SlotCount() + COLS - 1) / COLS);
    const float pitch = 58.0f;                                   // a square and its gap, at four rows
    const float chrome = 470.0f - 4.0f * pitch;                  // the panel, less its grid
    const float step = std::clamp(std::floor((ui.ViewHeight() - 40.0f - chrome) / rows), 40.0f, pitch);
    const float gap  = step >= 56.0f ? 6.0f : step >= 48.0f ? 5.0f : 4.0f;
    const float cell = step - gap;
    const float grid_h = rows * step;
    const SDL_FRect panel = CenteredPanel(ui, 700.0f, chrome + grid_h);
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
    // Centred in the width the four-row grid has, so smaller squares do not
    // drift left of the text under them.
    const float grid_x = panel.x + 24.0f + (COLS * pitch - COLS * (cell + gap)) * 0.5f;
    const float grid_y = panel.y + 62.0f;

    // Where the lit square is, so the card can be put beside it once the rest
    // of the panel has been drawn and cannot paint over it.
    SDL_FRect lit{0, 0, 0, 0};
    for (int i = 0; i < p.inventory.SlotCount(); ++i) {
        const int col = i % COLS, row = i / COLS;
        const SDL_FRect r = {grid_x + col * (cell + gap), grid_y + row * (cell + gap),
                             cell, cell};
        const bool selected = (!inventory_on_equipment && i == inventory_cursor);
        if (selected) lit = r;

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
        // What is to hand wears a gold corner.
        if (!p.QuickItem().empty() && s.id == p.QuickItem())
            ui.Fill({r.x + 2.0f, r.y + 2.0f, 8.0f, 8.0f}, {255, 214, 96, 255});
    }

    // --- equipment -----------------------------------------------------------
    const float eq_x = panel.x + 24.0f + COLS * pitch + 18.0f;
    ui.Text("Worn", eq_x, panel.y + 62.0f, TextSize::Body, Palette::Text);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const SDL_FRect r = {eq_x, panel.y + 92.0f + i * 30.0f, 236.0f, 26.0f};
        const bool selected = (inventory_on_equipment && i == equipment_cursor);
        if (selected) lit = r;
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
    // Boots and charms that quicken the step, as the percentage they add.
    if (p.equipment.MoveSpeed() != 0.0f) {
        char walk[48];
        SDL_snprintf(walk, sizeof(walk), "Walk +%d%%",
                     static_cast<int>(std::lround(p.equipment.MoveSpeed() * 100.0f)));
        ui.Text(walk, col2_x, bonus_y + 32.0f, TextSize::Small, Palette::Xp);
    }

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
        const float reach = p.WeaponReach();
        if (reach > 1.05f)
            SDL_snprintf(bonus, sizeof(bonus), "Attack speed  %.2fx  (%s)   Reach  %.2fx", 1.0f / sp, word, reach);
        else
            SDL_snprintf(bonus, sizeof(bonus), "Attack speed  %.2fx  (%s)", 1.0f / sp, word);
        ui.Text(bonus, eq_x, bonus_y + 50.0f, TextSize::Small, Palette::TextDim);
    }

    // --- selected item detail ------------------------------------------------
    const string sel_id = inventory_on_equipment
        ? p.equipment.InSlot(equipment_cursor)
        : p.inventory.Slot(inventory_cursor).id;

    if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id)) {
        const float y = grid_y + grid_h + 12.0f;
        const float text_x = panel.x + 24.0f;
        ui.Text(def->name, text_x, y, TextSize::Body, Palette::Highlight);
        // The tier and what it needs, on the right of the name.
        string tag;
        if (const TierDef* t = def->tier.empty() ? nullptr : items.Tier(def->tier)) tag = t->name + " tier";
        for (const auto& rq : def->requirements) {
            const bool met = p.skills.Level(rq.first) >= rq.second;
            tag += (tag.empty() ? "" : "   ") + string(met ? "" : "needs ") + SkillName(rq.first) +
                   " " + std::to_string(rq.second);
        }
        if (!tag.empty())
            ui.Text(tag, text_x + COLS * pitch - 12.0f, y + 4.0f, TextSize::Small, Palette::TextDim, Align::Right);
        const float desc_h = ui.TextWrapped(def->description, text_x, y + 24.0f,
                                            COLS * pitch - 12.0f, TextSize::Small, Palette::TextDim);
        // What it does beyond its numbers, in the colour of something rare.
        if (!def->passive_text.empty())
            ui.TextWrapped(def->passive_text, text_x, y + 28.0f + desc_h,
                           COLS * pitch - 12.0f, TextSize::Small, Palette::Highlight);
    }

    const ItemDef* under_cursor = sel_id.empty() ? nullptr : items.Get(sel_id);
    const bool can_hand = !inventory_on_equipment && under_cursor && under_cursor->consumable;
    ui.Text(input.PromptFor(Action::Confirm) + " use / equip     " +
            (can_hand ? input.PromptFor(Action::Target) + " keep to hand     " : string()) +
            input.PromptFor(Action::Drop) + " drop     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    // Drawn over everything else: a card under the panel it belongs to would
    // be a card nobody can read.
    if (lit.w > 0.0f)
        if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id))
            DrawItemCard(*def, lit);
}

// =============================================================================
//  Skills
// =============================================================================

void Game::UpdateSkillsPanel() {
    if (state_time <= 0.0f) tree_reset_armed = false;

    // I and O (the shoulder buttons on a pad) step between the level list and
    // the character's tree -- their path's, the only one they have; the panel
    // closes with Back.
    // ...and what the bosses have left them.
    const int tabs = 3;
    skills_tab = std::clamp(skills_tab, 0, tabs - 1);
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
    if (skills_tab == 2) return;         // the boons are read, not chosen

    Player& p = world->player;
    const AttackStyle style = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(style);

    // Stepping from the melee tree's fourth column to a tree with three.
    const int branches = tree.BranchCount();
    tree_branch = std::min(tree_branch, branches - 1);
    const int b0 = tree_branch, r0 = tree_row;
    if (input.MenuLeft())  tree_branch = (tree_branch + branches - 1) % branches;
    if (input.MenuRight()) tree_branch = (tree_branch + 1) % branches;
    if (input.MenuUp())    tree_row = std::max(0, tree_row - 1);
    if (input.MenuDown())  tree_row = std::min(SkillTrees::ROWS - 1, tree_row + 1);
    if (b0 != tree_branch || r0 != tree_row) Audio::Play(Sfx::UiMove);

    const TalentNode* node = tree.At(tree_branch, tree_row);
    if (node && input.Pressed(Action::Confirm)) {
        tree_reset_armed = false;
        const Talents::Why why = p.talents.CanLearn(node->id, p.skills);
        if (p.talents.Has(node->id) && !node->technique.empty()) {
            if (p.talents.ToggleTechnique(node->id)) {
                const bool on = p.talents.Technique(style) == node->technique;
                PushToast(on ? node->name + " is your charged attack now."
                             : "Back to a plain charged attack.", Palette::Xp);
                Audio::Play(Sfx::Equip);
            }
        } else if (p.talents.Has(node->id) && !node->ability.empty()) {
            // The first slot with room, then on down them, then put away.
            const int slot = p.talents.CycleAbility(node->id);
            const string keys = input.PromptFor(Action::Block) + " + " + input.PromptFor(AbilityButton(std::max(0, slot)));
            PushToast(slot >= 0 ? node->name + " is on " + keys + "." : node->name + " is put away.", Palette::Xp);
            Audio::Play(Sfx::Equip);
        } else {
            switch (why) {
                case Talents::Why::Ok: {
                    p.talents.Learn(node->id, p.skills);
                    const int rank = p.talents.Rank(node->id);
                    PushToast(node->ranks > 1 ? node->name + ", rank " + std::to_string(rank) + " of " + std::to_string(node->ranks) + "."
                                              : "Learned " + node->name + ".", Palette::Highlight);
                    Audio::Play(Sfx::QuestStart);
                    p.SyncMana();          // Deep Well is felt at once
                    // A first ability goes straight into a free slot, so it
                    // can be used without finding out how first.
                    if (!node->ability.empty() && rank == 1 && p.talents.SlotOf(node->id) < 0)
                        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot)
                            if (!p.talents.Ability(slot)) {
                                while (p.talents.SlotOf(node->id) != slot) p.talents.CycleAbility(node->id);
                                PushToast(node->name + " is on " + input.PromptFor(Action::Block) + " + " +
                                          input.PromptFor(AbilityButton(slot)) + ".", Palette::Xp);
                                break;
                            }
                    break;
                }
                case Talents::Why::Learned:
                    PushToast(node->name + " has every rank it can.", Palette::TextDim);
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
        // It costs: sixty coins for every point being taken back. Free and
        // unlimited, a build was whatever the next fight wanted -- thirty-three
        // points against forty-two ranks is only a choice if changing your
        // mind has a price. The first few points cost next to nothing to
        // rethink, and a finished tree costs about two thousand.
        const int spent = p.talents.PointsEarned(style, p.skills) - p.talents.PointsFree(style, p.skills);
        const int fee = Talents::RESPEC_FEE * std::max(0, spent);
        if (spent <= 0) {
            PushToast("There is nothing in the " + tree.name + " tree to unlearn.", Palette::TextDim);
        } else if (p.inventory.Coins() < fee) {
            tree_reset_armed = false;
            PushToast("Unlearning " + std::to_string(spent) + " points costs " + std::to_string(fee) +
                      " coins. You have " + std::to_string(p.inventory.Coins()) + ".", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
        } else if (!tree_reset_armed) {
            tree_reset_armed = true;
            PushToast("Press " + input.PromptFor(Action::Target) + " again to unlearn the " +
                      tree.name + " tree for " + std::to_string(fee) + " coins.", {235, 190, 120, 255});
        } else {
            p.inventory.SpendCoins(fee);
            p.talents.Reset(style);
            tree_reset_armed = false;
            PushToast(tree.name + " tree unlearned for " + std::to_string(fee) + " coins. Its points are free again.",
                      Palette::TextDim);
            Audio::Play(Sfx::UiBack);
        }
    }
}

void Game::DrawSkillsPanel() {
    ui.Dim(0.5f);
    const Skills& s = world->player.skills;

    // A tree with a fourth branch -- melee has Footwork -- widens the panel by
    // a column, so the node descriptions keep the room they had.
    float tree_w = 860.0f;
    if (skills_tab == 1) {
        const AttackStyle mine = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        tree_w += 172.0f * (skill_trees.Tree(mine).BranchCount() - SkillTrees::BRANCHES);
    }
    // As big as it wants to be, in a window with room for it; and in one
    // without, as big as the window. It was 640 and 690 tall whatever it was
    // drawn in, which is taller than a 1024 by 600 window and taller than a
    // Steam Deck's screen at the 125% its text wants -- the title and the
    // close prompt were both off the glass. (The overflow audit had a size for
    // exactly this and had never actually tested it: see Game::RunAudit.)
    const float fit_w = ui.ViewWidth() - 16.0f, fit_h = ui.ViewHeight() - 16.0f;
    const SDL_FRect panel = CenteredPanel(ui, std::min(skills_tab == 1 ? tree_w : 640.0f, fit_w),
                                          std::min(skills_tab == 1 ? 690.0f : 640.0f, fit_h));
    ui.Panel(panel);

    // --- tabs ------------------------------------------------------------------
    {
        // One tree a character: their path's. The hero's is the blade's, the
        // warden's the bow's, the wayfarer's the staff's.
        const AttackStyle path = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        const string tree_tab = skill_trees.Tree(path).name + " tree";
        const size_t boons = world->player.talents.Boons().size();
        const string kTabs[3] = {"Skills", tree_tab, boons ? "Boons (" + std::to_string(boons) + ")" : string("Boons")};
        float tx = panel.x + 24.0f;
        for (int t = 0; t < 3; ++t) {
            const float w = ui.Measure(kTabs[t], TextSize::Body).x + 24.0f;
            const SDL_FRect tab = {tx, panel.y + 14.0f, w, 30.0f};
            const bool on = (t == skills_tab);
            ui.Fill(tab, on ? SDL_Color{70, 54, 30, 235} : SDL_Color{30, 24, 20, 200});
            ui.Outline(tab, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
            int free = 0;
            if (t == 1) free = world->player.talents.PointsFree(path, s);
            ui.Text(kTabs[t], tab.x + 12.0f, tab.y + 5.0f, TextSize::Body,
                    on ? Palette::Highlight : (free > 0 ? Palette::Xp : Palette::Text));
            tx += w + 6.0f;
        }
        ui.Text(input.PromptFor(Action::Inventory) + " / " + input.PromptFor(Action::Skills) + " switch",
                tx + 10.0f, panel.y + 22.0f, TextSize::Small, Palette::TextDim);
    }

    if (skills_tab == 1) {
        DrawSkillTree(panel);
        return;
    }
    if (skills_tab == 2) {
        DrawBoons(panel);
        return;
    }

    char header[128];
    SDL_snprintf(header, sizeof(header), "Combat %d    Total level %d    Total XP %lld",
                 s.CombatLevel(), s.TotalLevel(), s.TotalXp());
    ui.Text(header, panel.x + panel.w - 24.0f, panel.y + panel.h - 52.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // With Fishing selected, its milestones: the chance of more than one fish.
    if (cursor == SKILL_FISHING) {
        // One line if it fits and two if it does not: the six milestones ran
        // off the side of the panel written out in a row.
        string line = "Catch more than one:";
        for (const auto& m : Gathering::FishingMilestones()) {
            char buf[64];
            if (m.three > 0.0f) SDL_snprintf(buf, sizeof(buf), "  %d: 3 fish %d%%", m.level, static_cast<int>(m.three * 100 + 0.5f));
            else                SDL_snprintf(buf, sizeof(buf), "  %d: 2 fish %d%%", m.level, static_cast<int>(m.two * 100 + 0.5f));
            line += buf;
        }
        const float wrap = panel.w - 48.0f;
        const float lines = ui.WrappedHeight(line, wrap, TextSize::Small);
        ui.TextWrapped(line, panel.x + 24.0f, panel.y + panel.h - 56.0f - lines, wrap, TextSize::Small,
                       s.Level(SKILL_FISHING) >= 20 ? Palette::Xp : Palette::TextDim);
    }

    // Fourteen rows in whatever is left between the tabs and the footer.
    const float row_h = std::clamp(floorf((panel.h - 58.0f - 134.0f) / static_cast<float>(SKILL_COUNT)), 24.0f, 32.0f);
    for (int i = 0; i < SKILL_COUNT; ++i) {
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 58.0f + i * row_h,
                               panel.w - 40.0f, row_h - 4.0f};
        const bool selected = (i == cursor);
        if (selected) {
            ui.Fill(row, {58, 46, 28, 200});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }

        const int level = s.Level(i);
        ui.Text(SkillName(i), row.x + 10.0f, row.y + 4.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        // A boosted level shows what it is working at right now.
        const int now = s.Current(i);
        const bool boosted = i != SKILL_HITPOINTS && now != level;
        ui.Text(boosted ? std::to_string(now) + "/" + std::to_string(level) : std::to_string(level),
                row.x + 170.0f, row.y + 4.0f, TextSize::Body,
                boosted ? (now > level ? Palette::Xp : SDL_Color{235, 150, 120, 255}) : Palette::Text, Align::Right);

        // Progress toward the next level, the way the OSRS skill guide reads.
        const int xp = s.Xp(i);
        const int here = XpForLevel(level);
        const int next = XpForLevel(std::min(level + 1, MAX_SKILL_LEVEL));
        const float frac = (next > here) ? static_cast<float>(xp - here) / (next - here) : 1.0f;

        const SDL_FRect bar = {row.x + 190.0f, row.y + 8.0f, row.w - 320.0f, 14.0f};
        ui.Bar(bar, frac, Palette::Xp, {26, 34, 26, 235});

        char xp_text[48];
        if (level >= MAX_SKILL_LEVEL) SDL_snprintf(xp_text, sizeof(xp_text), "max");
        else SDL_snprintf(xp_text, sizeof(xp_text), "%d xp to %d", next - xp, level + 1);
        ui.Text(xp_text, row.x + row.w - 10.0f, row.y + 7.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
    }

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// What the bosses have left this character: see Talents::SlayBoss. Nothing to
// choose here -- a boon is given, not bought -- so it is a page to read.
void Game::DrawBoons(const SDL_FRect& panel) {
    const Player& p = world->player;
    const float x = panel.x + 24.0f, w = panel.w - 48.0f;
    float y = panel.y + 58.0f;
    y += ui.TextWrapped("The first time you bring down one of the great ones it leaves you two things, for good: "
                        "a skill point for your tree, and a boon. The fifteenth time, its totem, for the ring in your house.",
                        x, y, w, TextSize::Small, Palette::TextDim) + 14.0f;

    const vector<string>& mine = p.talents.Boons();
    if (mine.empty()) {
        ui.Text("Nothing yet.", panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f - 30.0f, TextSize::Body,
                Palette::TextDim, Align::Center);
        ui.TextWrapped("There is one under the Barley and Bell, if Bess has not mentioned it.",
                       x + 60.0f, panel.y + panel.h / 2.0f, w - 120.0f, TextSize::Small, Palette::TextDim);
    }
    // As many rows as there are bosses in the game would need, in the room there is.
    const float room = panel.y + panel.h - 112.0f - y;
    const float row_h = std::clamp(floorf(room / std::max<size_t>(1, mine.size())), 26.0f, 40.0f);
    for (const string& id : mine) {
        const BoonDef* b = skill_trees.Boon(id);
        if (!b || y + row_h > panel.y + panel.h - 108.0f) continue;
        const SDL_FRect row = {x, y, w, row_h - 4.0f};
        ui.Fill(row, {40, 32, 22, 220});
        ui.Outline(row, {232, 190, 96, 255}, 1.0f);
        const float ty = row.y + (row.h - 20.0f) / 2.0f;
        ui.Text(b->name, row.x + 12.0f, ty, TextSize::Body, Palette::Highlight);
        ui.Text(b->text, row.x + row.w - 12.0f, ty + 3.0f, TextSize::Small, Palette::Text, Align::Right);
        y += row_h;
    }

    // Who has been killed, by name, so that it is plain which are still to do.
    // And how many times, because the fifteenth is the one that leaves a totem.
    string fallen;
    for (const string& id : p.talents.BossesSlain()) {
        const EnemyDef* d = enemy_db.Get(id);
        const int n = p.talents.Kills(id);
        fallen += (fallen.empty() ? "" : ", ") + (d ? d->name : id) + " " +
                  (n >= Talents::TOTEM_KILLS ? string("(totem)") : std::to_string(n) + "/" + std::to_string(Talents::TOTEM_KILLS));
    }
    if (!fallen.empty())
        ui.TextWrapped("Brought down: " + fallen + ".", x, panel.y + panel.h - 104.0f, w, TextSize::Small, Palette::TextDim);

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

void Game::DrawSkillTree(const SDL_FRect& panel) {
    const Player& p = world->player;
    const AttackStyle style = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(style);
    const int level = p.skills.Level(tree.skill);
    const int earned = p.talents.PointsEarned(style, p.skills);
    const int free = p.talents.PointsFree(style, p.skills);

    char head[200];
    const int from_bosses = p.talents.BonusPoints();
    if (from_bosses > 0)
        SDL_snprintf(head, sizeof(head), "%s %d     %d of %d points free     a point every %d levels, and %d from bosses",
                     SkillName(tree.skill), level, free, earned, SkillTrees::LEVELS_PER_POINT, from_bosses);
    else
        SDL_snprintf(head, sizeof(head), "%s %d     %d of %d points free     a point every %d levels, and one for each boss",
                     SkillName(tree.skill), level, free, earned, SkillTrees::LEVELS_PER_POINT);
    ui.Text(head, panel.x + 24.0f, panel.y + 56.0f, TextSize::Small, free > 0 ? Palette::Xp : Palette::TextDim);

    // --- the grid ----------------------------------------------------------------
    const float gx = panel.x + 78.0f, gy = panel.y + 112.0f;
    // The grid is cut to the panel, which is cut to the window: the melee
    // tree's four columns are 1032 wide at their full size, and eight rows of
    // 62 are 496 before the headings. Columns give up width and rows give up
    // the gap between boxes before either gives up the box itself, which has
    // two lines of text to hold.
    const int   columns = std::max(1, tree.BranchCount());
    const float col_w = std::min(172.0f, floorf((panel.w - 78.0f - 36.0f - 230.0f) / columns));
    const float row_h = std::clamp(floorf((panel.h - 112.0f - 82.0f) / SkillTrees::ROWS), 46.0f, 62.0f);
    const float box_w = col_w - 22.0f, box_h = std::min(44.0f, row_h - 6.0f);

    for (int b = 0; b < tree.BranchCount(); ++b) {
        const string name = b < static_cast<int>(tree.branches.size()) ? tree.branches[b] : "";
        ui.Text(name, gx + b * col_w + box_w / 2.0f, gy - 26.0f, TextSize::Body, Palette::Highlight, Align::Center);
    }

    for (int row = 0; row < SkillTrees::ROWS; ++row) {
        const TalentNode* first = tree.At(0, row);
        if (first)
            ui.Text("Lv " + std::to_string(first->level), panel.x + 24.0f, gy + row * row_h + 13.0f,
                    TextSize::Small, level >= first->level ? Palette::Text : Palette::TextDim);
    }

    for (const TalentNode& n : tree.nodes) {
        const SDL_FRect box = {gx + n.branch * col_w, gy + n.row * row_h, box_w, box_h};
        const bool learned = p.talents.Has(n.id);
        const int  rank = p.talents.Rank(n.id);
        const Talents::Why why = p.talents.CanLearn(n.id, p.skills);
        const bool available = why == Talents::Why::Ok;
        const bool selected = n.branch == tree_branch && n.row == tree_row;
        const int  slot = p.talents.SlotOf(n.id);
        const bool active = (!n.technique.empty() && p.talents.Technique(style) == n.technique) || slot >= 0;

        // The line down to the next node in the branch, lit once both ends are.
        if (const TalentNode* below = n.row + 1 < SkillTrees::ROWS ? tree.At(n.branch, n.row + 1) : nullptr) {
            const bool lit = learned && p.talents.Has(below->id);
            ui.Fill({box.x + box_w / 2.0f - 1.0f, box.y + box_h, 3.0f, row_h - box_h},
                    lit ? SDL_Color{232, 190, 96, 255} : SDL_Color{70, 60, 50, 255});
        }

        SDL_Color fill = {26, 21, 18, 235}, text = {120, 110, 100, 255}, edge = Palette::BorderDim;
        if (learned)        { fill = {92, 70, 30, 240}; text = Palette::Highlight; edge = {232, 190, 96, 255}; }
        else if (available) { fill = {40, 50, 30, 240}; text = Palette::Text; edge = Palette::Xp; }
        if (active)         { fill = {120, 50, 36, 245}; edge = {255, 160, 110, 255}; }
        ui.Fill(box, fill);
        ui.Outline(box, selected ? SDL_Color{255, 255, 255, 255} : edge, selected ? 3.0f : 1.0f);

        ui.Text(n.name, box.x + box_w / 2.0f, box.y + (box_h >= 44.0f ? 5.0f : 3.0f), TextSize::Small, text, Align::Center);
        // Rushing Strike is neither a charged technique nor a passive: it is a
        // move of its own, made on its own button.
        string kind = !n.technique.empty() ? string(active ? "technique - active" : "technique")
                    : !n.ability.empty() ? (slot >= 0 ? "ability - slot " + std::to_string(slot + 1) : string("ability"))
                    : n.effects.count("rushing_strike") ? string("move")
                    : n.row == SkillTrees::ROWS - 1 ? string("capstone") : string("passive");
        if (n.ranks > 1) kind += "  " + std::to_string(rank) + "/" + std::to_string(n.ranks);
        ui.Text(kind, box.x + box_w / 2.0f, box.y + box_h - 21.0f, TextSize::Small,
                !n.technique.empty() ? SDL_Color{236, 150, 110, 255}
                : !n.ability.empty() ? SDL_Color{130, 190, 240, 255} : Palette::TextDim, Align::Center);
    }

    // --- the chosen node -------------------------------------------------------------
    const TalentNode* n = tree.At(tree_branch, tree_row);
    const float dx = gx + tree.BranchCount() * col_w + 12.0f;
    const float dw = panel.x + panel.w - dx - 24.0f;
    float y = gy - 26.0f;
    if (n) {
        ui.Text(n->name, dx, y, TextSize::Body, Palette::Highlight);
        y += 28.0f;
        const string each = n->ranks > 1 ? "a point a rank" : "one point";
        // Wrapped, not written straight out: "Attack 47, a point a rank, after
        // War Cry" is wider than the column the rest of this is written in, and
        // ran off the side of the panel.
        y += ui.TextWrapped(string(SkillName(tree.skill)) + " " + std::to_string(n->level) + ", " +
                            (n->row == 0 || !tree.At(n->branch, n->row - 1)
                                 ? each
                                 : each + ", after " + tree.At(n->branch, n->row - 1)->name),
                            dx, y, dw, TextSize::Small,
                            level >= n->level ? Palette::TextDim : SDL_Color{225, 130, 120, 255}) + 6.0f;
        y += ui.TextWrapped(n->description, dx, y, dw, TextSize::Small, Palette::Text) + 10.0f;
        if (!n->ability.empty()) {
            string cost = "Every " + std::to_string(static_cast<int>(n->cooldown)) + " seconds";
            if (n->stamina_cost > 0) cost += ", " + std::to_string(n->stamina_cost) + " stamina";
            if (n->mana_cost > 0)    cost += ", " + std::to_string(n->mana_cost) + " mana";
            y += ui.TextWrapped(cost + ". Hold " + input.PromptFor(Action::Block) + " and press " +
                                input.PromptFor(Action::LightAttack) + ", " + input.PromptFor(Action::StrongAttack) +
                                " or " + input.PromptFor(Action::Target) + ", whichever slot it is in. Three are carried at once.",
                                dx, y, dw, TextSize::Small, {130, 190, 240, 255}) + 10.0f;
        }

        string status, action;
        const Talents::Why why = p.talents.CanLearn(n->id, p.skills);
        const int rank = p.talents.Rank(n->id);
        if (p.talents.Has(n->id) && !n->technique.empty()) {
            const bool active = p.talents.Technique(style) == n->technique;
            status = active ? "Your charged attack with this style." : "Learned, not in use.";
            action = input.PromptFor(Action::Confirm) + (active ? " stop using it" : " use as charged attack");
        } else if (p.talents.Has(n->id) && !n->ability.empty()) {
            const int slot = p.talents.SlotOf(n->id);
            status = slot >= 0 ? "Carried in slot " + std::to_string(slot + 1) + ": " + input.PromptFor(Action::Block) + " + " +
                                 input.PromptFor(AbilityButton(slot)) + "."
                               : "Learned, not carried.";
            action = input.PromptFor(Action::Confirm) +
                     (slot < 0 ? string(" carry it")
                      : slot + 1 < SkillTrees::ABILITY_SLOTS ? " move to slot " + std::to_string(slot + 2) : string(" put it away"));
        } else if (p.talents.Has(n->id) && why != Talents::Why::Ok) {
            status = n->ranks > 1 ? "Rank " + std::to_string(rank) + " of " + std::to_string(n->ranks) + "." : "Learned.";
            if (rank < n->ranks && why == Talents::Why::NoPoints) status += " No points for the next.";
        } else if (why == Talents::Why::Ok && rank > 0) {
            status = "Rank " + std::to_string(rank) + " of " + std::to_string(n->ranks) + ".";
            action = input.PromptFor(Action::Confirm) + " learn the next rank";
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
        y += ui.TextWrapped(status, dx, y, dw, TextSize::Small,
                            p.talents.Has(n->id) ? Palette::Highlight : Palette::TextDim) + 4.0f;
        if (!action.empty()) ui.TextWrapped(action, dx, y, dw, TextSize::Small, Palette::Xp);
    }

    const string technique = p.talents.Technique(style);
    string shown = technique;
    for (char& c : shown) if (c == '_') c = ' ';
    if (!shown.empty()) shown[0] = static_cast<char>(toupper(static_cast<unsigned char>(shown[0])));
    ui.Text("Charged attack: " + (shown.empty() ? string("plain") : shown),
            dx, panel.y + panel.h - 136.0f, TextSize::Small, Palette::TextDim);
    for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
        const TalentNode* carried = p.talents.Ability(slot);
        ui.Text(input.PromptFor(Action::Block) + " + " + input.PromptFor(AbilityButton(slot)) +
                ": " + (carried ? carried->name : string("nothing carried")),
                dx, panel.y + panel.h - 114.0f + slot * 20.0f, TextSize::Small, carried ? SDL_Color{130, 190, 240, 255} : Palette::TextDim);
    }

    ui.Text(input.PromptFor(Action::Target) + " twice unlearn tree     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            tree_reset_armed ? SDL_Color{235, 190, 120, 255} : Palette::TextDim, Align::Center);
}

// =============================================================================
//  Quest log
// =============================================================================

const char* Game::QuestTabName(int tab) {
    switch (tab) {
        case 0:  return "Story";
        case 1:  return "Tutorials";
        default: return "Side quests";
    }
}

// What each tab holds, in the order it is listed: what you are doing, then what
// is still ahead of you, then what is done. A story quest is one marked
// "major" in data/quests.json and a tutorial one marked "tutorial"; everything
// else -- board contracts, daily orders, the favours people ask -- is a side
// quest, so no list is buried under another.
//
// Quests not yet taken are listed too, which is the point of the story tab:
// what is coming is as much a part of a journal as what is in hand. Dailies
// are the exception -- there are dozens, they come back every day, and listing
// every one of them unasked-for is the clutter the tabs exist to stop.
void Game::QuestList(int tab, vector<string>& out, size_t& active_count,
                     size_t& not_started_count) const {
    const auto belongs = [&](const string& id) {
        const QuestDef* d = quests->Definition(id);
        if (!d) return false;
        const int home = d->major ? 0 : (d->tutorial ? 1 : 2);
        return home == tab;
    };
    out.clear();
    for (const string& id : quests->Active()) if (belongs(id)) out.push_back(id);
    active_count = out.size();

    vector<const QuestDef*> ahead;
    for (const auto& kv : quests->Definitions()) {
        const QuestDef& d = kv.second;
        if (d.daily || !belongs(kv.first)) continue;
        if (quests->Status(kv.first) != QuestStatus::NotStarted) continue;
        ahead.push_back(&d);
    }
    // In the order they are meant to be met.
    std::sort(ahead.begin(), ahead.end(), [](const QuestDef* a, const QuestDef* b) {
        if (a->recommended_level != b->recommended_level)
            return a->recommended_level < b->recommended_level;
        return a->id < b->id;
    });
    for (const QuestDef* d : ahead) out.push_back(d->id);
    not_started_count = ahead.size();

    for (const string& id : quests->Completed()) if (belongs(id)) out.push_back(id);
}

// Red for what has not been started, blue for what is in hand, green for what
// is done -- the three states a journal line can be in, told apart at a glance
// rather than by reading.
namespace {
constexpr SDL_Color kQuestNotStarted{216, 108, 100, 255};
constexpr SDL_Color kQuestActive    {120, 172, 238, 255};
constexpr SDL_Color kQuestDone      {120, 202, 118, 255};
}

void Game::UpdateQuestPanel() {
    vector<string> list;
    size_t active_count = 0, ahead = 0;

    // Left and right step between the tabs; the cursor of each is its own.
    const int before = quest_tab;
    if (input.MenuLeft())  quest_tab = (quest_tab + kQuestTabs - 1) % kQuestTabs;
    if (input.MenuRight()) quest_tab = (quest_tab + 1) % kQuestTabs;
    if (quest_tab != before) Audio::Play(Sfx::UiMove);

    QuestList(quest_tab, list, active_count, ahead);
    MoveCursor(quest_cursor[quest_tab], static_cast<int>(list.size()));
    // Confirm on a quest in hand follows it: the waypoint is that one's until
    // it is done, or until this is pressed on it again.
    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && !list.empty()) {
        const size_t at = static_cast<size_t>(std::clamp(quest_cursor[quest_tab], 0, static_cast<int>(list.size()) - 1));
        if (at < active_count) {
            quests->Follow(list[at]);
            Audio::Play(Sfx::UiConfirm);
        } else {
            Audio::Play(Sfx::UiError);
        }
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::QuestLog) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

// =============================================================================
//  The world map
// =============================================================================

void Game::UpdateWorldMap() {
    // It opens on where you are. The Hollowmarch is the other side of the page.
    if (state_time <= 0.0f) map_overview = false;
    if (world_map.HasOverview(world->MapId()) &&
        (input.Pressed(Action::Confirm) || input.MenuLeft() || input.MenuRight())) {
        map_overview = !map_overview;
        Audio::Play(Sfx::UiMove);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::WorldMap) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawWorldMap() {
    const Waypoint& waypoint = CurrentWaypoint();
    const QuestDef* following = quests ? quests->Definition(quests->Followed()) : nullptr;
    world_map.SetRoads(&waypoints);
    world_map.Draw(renderer, *textures, ui, (*world),
                   input.PromptFor(Action::WorldMap) + " or " + input.PromptFor(Action::Back) + " close",
                   input.PromptFor(Action::Confirm) + " turn to", map_overview,
                   settings.quest_waypoints ? &waypoint : nullptr, following ? following->name : string());
}

void Game::DrawQuestPanel() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 460.0f);
    ui.Panel(panel);
    ui.Text("Quest Journal", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight);

    vector<string> list;
    size_t active_size = 0, ahead_size = 0;
    QuestList(quest_tab, list, active_size, ahead_size);
    const int cursor_here = std::clamp(quest_cursor[quest_tab], 0,
                                       std::max(0, static_cast<int>(list.size()) - 1));
    // What each row is: in hand, still ahead, or finished.
    const auto state_of = [&](size_t i) {
        if (i < active_size) return 1;
        return i < active_size + ahead_size ? 0 : 2;
    };
    const SDL_Color kStateColour[3] = {kQuestNotStarted, kQuestActive, kQuestDone};

    // The two tabs, drawn as headings with the count each holds. The one you
    // are in is lit and underlined.
    {
        float tx = panel.x + 24.0f;
        for (int tab = 0; tab < kQuestTabs; ++tab) {
            vector<string> in_tab;
            size_t tab_active = 0, tab_ahead = 0;
            QuestList(tab, in_tab, tab_active, tab_ahead);
            // In hand out of everything the tab knows about.
            const string label = string(QuestTabName(tab)) + "  " +
                                 std::to_string(tab_active) + "/" + std::to_string(in_tab.size());
            const bool on = tab == quest_tab;
            const float w = ui.Measure(label, TextSize::Small).x + 24.0f;
            const SDL_FRect box = {tx, panel.y + 46.0f, w, 24.0f};
            if (on) {
                ui.Fill(box, {58, 46, 28, 200});
                ui.Outline(box, Palette::Highlight, 1.0f);
            }
            ui.Text(label, tx + 12.0f, box.y + 5.0f, TextSize::Small,
                    on ? Palette::Highlight : Palette::TextDim);
            tx += w + 10.0f;
        }
    }

    if (list.empty()) {
        const string what = quest_tab == 0 ? "The story has not found you yet."
                          : quest_tab == 1 ? "Nobody has offered to teach you a trade."
                                           : "No contracts, orders or favours in hand.";
        const string where = quest_tab == 0 ? "Talk to the people who have been here longest."
                           : quest_tab == 1 ? "The sawpit, the gravel pit and the mill pond are in Havenbrook."
                                            : "Look for a mission board in town, or ask at a forge.";
        ui.Text(what, panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f - 20.0f,
                TextSize::Body, Palette::TextDim, Align::Center);
        ui.Text(where, panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        const size_t active_count = active_size;
        // Left: the list. Right: detail for whatever is highlighted.
        const float list_w = 290.0f;
        const float row_h = 34.0f;

        // The window of rows the cursor is inside, so a long list scrolls
        // rather than running off the panel.
        const size_t visible = 9;
        size_t first = 0;
        if (list.size() > visible)
            first = std::min(list.size() - visible,
                             static_cast<size_t>(std::max(0, cursor_here - static_cast<int>(visible) / 2)));

        for (size_t k = 0; k < visible && first + k < list.size(); ++k) {
            const size_t i = first + k;
            const SDL_FRect row = {panel.x + 20.0f, panel.y + 82.0f + k * row_h,
                                   list_w, row_h - 4.0f};
            const bool selected = (static_cast<int>(i) == cursor_here);
            const int state = state_of(i);

            if (selected) {
                ui.Fill(row, {58, 46, 28, 200});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests->Definition(list[i]);
            ui.Text(d ? d->name : list[i], row.x + 10.0f, row.y + 5.0f, TextSize::Small,
                    kStateColour[state]);
            if (state == 2)
                ui.Text("done", row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small,
                        kQuestDone, Align::Right);
            else if (state == 1 && list[i] == quests->Followed()) {
                // The tag gives way to the name. "Cooking: Meat and Fire" and
                // "following (newest)" do not both fit on a row, and drawn
                // anyway they were one unreadable word. A gold bar down the
                // row's edge marks it whatever happens; the words are added
                // when there is room for them -- the long form, the short one,
                // or none.
                const SDL_Color gold{255, 214, 96, 255};
                ui.Fill({row.x, row.y, 3.0f, row.h}, gold);
                const float name_w = ui.Measure(d ? d->name : list[i], TextSize::Small).x;
                const float room = row.w - 18.0f - name_w - 14.0f;
                string tag = quests->Chosen() ? "following" : "following (newest)";
                if (ui.Measure(tag, TextSize::Small).x > room) tag = "following";
                if (ui.Measure(tag, TextSize::Small).x <= room)
                    ui.Text(tag, row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small, gold, Align::Right);
            }
        }
        if (list.size() > visible)
            ui.Text(std::to_string(cursor_here + 1) + "/" + std::to_string(list.size()),
                    panel.x + 20.0f + list_w, panel.y + 82.0f + visible * row_h, TextSize::Small,
                    Palette::TextDim, Align::Right);

        const int index = cursor_here;
        if (const QuestDef* d = quests->Definition(list[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 82.0f;

            const int state = state_of(static_cast<size_t>(index));
            ui.Text(d->name, dx, y, TextSize::Body, kStateColour[state]);
            y += 28.0f;
            const char* word = state == 0 ? "Not started" : (state == 1 ? "In progress" : "Completed");
            ui.Text(word, dx, y, TextSize::Small, kStateColour[state]);
            ui.Text("Suggested level " + std::to_string(d->recommended_level) +
                        (d->daily ? "     daily, done " + std::to_string(quests->Completions(list[index])) + "x" : string("")),
                    dx + dw, y, TextSize::Small, Palette::TextDim, Align::Right);
            y += 24.0f;
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 12.0f;

            ui.Text(state == 0 ? "First step" : "Objective", dx, y, TextSize::Small, Palette::Highlight);
            y += 20.0f;
            // A quest not yet taken has no progress to report, so its own
            // first stage stands in for the objective line.
            const string objective = state == 0
                ? (d->stages.empty() ? string("Nobody has offered this yet.") : d->stages.front().description)
                : quests->CurrentObjectiveText(list[index]);
            y += ui.TextWrapped(objective, dx, y, dw, TextSize::Small,
                                state == 0 ? Palette::TextDim : Palette::Text);
            y += 14.0f;
            if (state == 0 && d->combat_level > 0) {
                ui.Text("Needs Combat " + std::to_string(d->combat_level), dx, y, TextSize::Small,
                        world->player.skills.CombatLevel() >= d->combat_level ? kQuestDone : kQuestNotStarted);
                y += 20.0f;
            }

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

    ui.Text(input.PromptFor(Action::Confirm) + " follow: its waypoint is shown     Left / Right  switch tab     " +
                input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Dialogue
// =============================================================================

void Game::UpdateDialogue(float dt) {
    dialogue.Update(dt);

    const DialogueContext dctx = MakeDialogueContext();

    if (!dialogue.Active()) {
        for (auto& n : world->npcs) n->talking = false;
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

            // So does asking for the day's orders.
            if (!pending_orders.empty()) {
                const string npc = pending_orders;
                string name = npc;
                for (auto& n : world->npcs) if (n->Id() == npc) name = n->Name();
                pending_orders.clear();
                dialogue.End();
                for (auto& n : world->npcs) n->talking = false;
                OpenOrders(npc, name);
                return;
            }

            // "Show me your wares" closes the conversation and opens the shop.
            if (!pending_shop.empty()) {
                const string id = pending_shop;
                pending_shop.clear();
                dialogue.End();
                for (auto& n : world->npcs) n->talking = false;
                OpenShop(id);
                return;
            }

            for (const string& id : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(id))
                    PushToast("Quest started: " + d->name, Palette::Xp);
            for (const string& id : quests->TakeJustCompleted())
                GrantQuestRewards(id);
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) dialogue.End();
}

void Game::DrawDialogue() {
    const DialogueNode* node = dialogue.Node();
    if (!node) return;

    // Tall enough for the whole line and every answer under it: a smith with
    // orders, a shop and a lesson to offer had her answers printed over her
    // own words. Measured on the full text, so the box does not grow as it types.
    const float box_w = ui.ViewWidth() - 80.0f;
    const float text_h = ui.WrappedHeight(node->text, box_w - 48.0f, TextSize::Body);
    const float needed = 28.0f + text_h + 14.0f + dialogue.VisibleOptions().size() * 26.0f + 16.0f;
    const float box_h = std::max(210.0f, needed);
    const SDL_FRect box = {40.0f, ui.ViewHeight() - box_h - 30.0f, box_w, box_h};
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
        if (quests->CanStart(id, world->player.skills)) available.push_back(id);

    MoveCursor(board_cursor, static_cast<int>(available.size()));

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !available.empty()) {
        const string& id = available[std::clamp(board_cursor, 0,
                                                static_cast<int>(available.size()) - 1)];
        if (quests->Start(id)) {
            for (const string& started : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(started))
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
        if (quests->CanStart(id, world->player.skills)) available.push_back(id);

    if (available.empty()) {
        ui.Text(board_orders ? "No orders for you today." : "Nothing new is pinned up today.",
                panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f - 20.0f, TextSize::Body, Palette::TextDim,
                Align::Center);
        ui.Text(board_orders ? "New orders come in at dawn. Anything you have taken is in your journal."
                             : "Come back after you have finished what you already took on.",
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
            const QuestDef* d = quests->Definition(available[i]);
            ui.Text(d ? d->name : available[i], row.x + 10.0f, row.y + 3.0f,
                    TextSize::Small, selected ? Palette::Highlight : Palette::Text);
            if (d)
                ui.Text((d->daily ? string(board_orders ? "order   " : "daily   ") : string("")) +
                        "Lv " + std::to_string(d->recommended_level),
                        row.x + row.w - 8.0f, row.y + 3.0f, TextSize::Small,
                        d->daily ? Palette::Xp : Palette::TextDim, Align::Right);
        }

        const int index = std::clamp(board_cursor, 0, static_cast<int>(available.size()) - 1);
        if (const QuestDef* d = quests->Definition(available[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 30.0f;
            if (d->daily) {
                ui.Text(board_orders ? "Order: new orders at dawn" : "Daily: new notices at dawn",
                        dx, y, TextSize::Small, Palette::Xp);
                y += 22.0f;
            }
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 14.0f;
            // What it asks for against what is in the pack.
            if (!d->stages.empty() && (d->stages[0].type == ObjectiveType::Deliver ||
                                       d->stages[0].type == ObjectiveType::Collect)) {
                const QuestStage& st = d->stages[0];
                const ItemDef* want = items.Get(st.target);
                const int held = world->player.inventory.Count(st.target);
                ui.Text((want ? want->name : st.target) + ": you carry " + std::to_string(held) +
                        " of " + std::to_string(st.count), dx, y, TextSize::Small,
                        held >= st.count ? Palette::Xp : Palette::TextDim);
                y += 22.0f;
            }
            if (!d->requirements.empty()) {
                string req = "Needs ";
                for (const auto& rq : d->requirements)
                    req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
                ui.Text(req, dx, y, TextSize::Small, Palette::TextDim);
                y += 22.0f;
            }

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
                              quests->CanStart(note_quest, world->player.skills);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (offers_quest && quests->Start(note_quest)) {
            for (const string& started : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(started))
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
                              quests->CanStart(note_quest, world->player.skills);
    ui.Text(offers_quest ? (input.PromptFor(Action::Confirm) + " look into this")
                         : (input.PromptFor(Action::Confirm) + " put it away"),
            panel.x + panel.w / 2.0f, panel.y + panel.h - 34.0f, TextSize::Small,
            {96, 74, 44, 255}, Align::Center);
}

// =============================================================================
//  The bed's question
// =============================================================================
//
// Two rows on the parchment a note is read on. The wording is the same alone
// and in company: in co-op each player answers for themselves.

namespace {
struct SleepRow { const char* name; const char* what; };
constexpr SleepRow kSleepRows[2] = {
    {"Sleep through the night", "Wake here at dawn, rested. No dream."},
    {"Go into the Reverie",     "Dream until dawn. What you find there, you keep."},
};
}

// =============================================================================
//  Waystones
//
//  Three old stones, one in each town and none anywhere else. Each is asleep
//  until somebody puts a hand on it, and a woken one is a door to every other
//  woken one -- so the road to a town is walked once, and the wilds and the
//  dungeons are always walked. There is no fare: the price of a waystone is
//  having got there.
// =============================================================================

namespace {
struct Waystone { const char* id; const char* map; const char* name; const char* note; };
const Waystone kWaystones[] = {
    {"waystone_havenbrook", "town_havenbrook", "Havenbrook", "the market town on the southern road"},
    {"waystone_mossvale",   "mossvale",        "Mossvale",   "the logging village under the Whisperwood"},
    {"waystone_fernhollow", "fernhollow",      "Fernhollow", "the hamlet on still water"},
};
constexpr int kWaystoneCount = 3;
}

void Game::UpdateTravel() {
    MoveCursor(travel_cursor, kWaystoneCount);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const Waystone& to = kWaystones[std::clamp(travel_cursor, 0, kWaystoneCount - 1)];
        if (travel_from == to.id) {
            PushToast("You are standing at it.", Palette::TextDim);
            Audio::Play(Sfx::UiError);
        } else if (!world->Flagged(to.id)) {
            PushToast("The stone at " + string(to.name) + " is still asleep. It has to be woken by hand.",
                      {235, 190, 120, 255});
            Audio::Play(Sfx::UiError);
        } else {
            SetState(GameState::Play);
            if (world->RequestTransition(to.map, "waystone")) Audio::Play(Sfx::QuestStart);
        }
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawTravel() {
    ui.Dim(0.55f);
    const float row_h = 58.0f;
    const SDL_FRect panel = CenteredPanel(ui, 520.0f, 150.0f + kWaystoneCount * row_h);
    ui.Panel(panel);
    ui.Text("Waystone", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);
    ui.Text("A woken stone opens on every other you have woken.", panel.x + panel.w / 2.0f,
            panel.y + 52.0f, TextSize::Small, Palette::TextDim, Align::Center);

    const SDL_Color cold{150, 220, 255, 255};
    for (int i = 0; i < kWaystoneCount; ++i) {
        const Waystone& w = kWaystones[i];
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 84.0f + i * row_h, panel.w - 40.0f, row_h - 8.0f};
        const bool selected = (i == travel_cursor);
        const bool here = travel_from == w.id;
        const bool awake = world->Flagged(w.id);
        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, selected ? Palette::Highlight : Palette::BorderDim, selected ? 2.0f : 1.0f);
        // A lit or a dark eye, the way the stone itself shows it.
        ui.Fill({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, awake ? cold : SDL_Color{52, 50, 56, 255});
        ui.Outline({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, Palette::BorderDim, 1.0f);
        ui.Text(w.name, row.x + 44.0f, row.y + 6.0f, TextSize::Body,
                !awake ? SDL_Color{120, 110, 100, 255} : (selected ? Palette::Highlight : Palette::Text));
        ui.Text(w.note, row.x + 44.0f, row.y + 28.0f, TextSize::Small, Palette::TextDim);
        ui.Text(here ? "you are here" : (awake ? "awake" : "asleep"), row.x + row.w - 12.0f, row.y + 8.0f,
                TextSize::Small, here ? Palette::Highlight : (awake ? cold : SDL_Color{150, 110, 100, 255}),
                Align::Right);
    }

    ui.Text(input.PromptFor(Action::Confirm) + " go     " + input.PromptFor(Action::Back) + " stay",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  The ring in the house at Mossvale
//
//  What a boss leaves the fifteenth time is a totem, and the ring in the floor
//  of the player's own house is where one is stood. Touched, it gives its
//  blessing for the rest of that day -- wherever they go, and through a death --
//  and the next day it is a carving in a ring until it is touched again.
//  One at a time: standing another in the ring puts the first back in the bag.
// =============================================================================

vector<string> Game::TotemChoices() const {
    vector<string> out;
    const Player& p = world->player;
    if (!p.talents.PlacedTotem().empty()) out.push_back(p.talents.PlacedTotem());
    for (const TotemDef& t : skill_trees.Totems())
        if (p.inventory.Has(t.item) && std::find(out.begin(), out.end(), t.item) == out.end()) out.push_back(t.item);
    return out;
}

void Game::UpdateTotemRing() {
    Player& p = world->player;
    const vector<string> have = TotemChoices();
    if (!have.empty()) MoveCursor(totem_cursor, static_cast<int>(have.size()));
    totem_cursor = std::clamp(totem_cursor, 0, std::max(0, static_cast<int>(have.size()) - 1));
    const int today = world->clock.QuestDay();

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && !have.empty()) {
        const string pick = have[totem_cursor];
        const TotemDef* def = skill_trees.Totem(pick);
        const ItemDef* thing = items.Get(pick);
        const string what = thing ? thing->name : pick;
        if (pick == p.talents.PlacedTotem() && p.talents.TotemAwake()) {
            PushToast("It is awake, and will be until dawn.", Palette::TextDim);
            Audio::Play(Sfx::UiError);
            return;
        }
        if (pick == p.talents.PlacedTotem()) {
            // Standing there, asleep: a hand on it is all it wants.
            p.talents.PlaceTotem(pick, today);
        } else {
            // Out of the bag and into the ring; what was in the ring, into the
            // bag, which has the room the other one just left.
            if (!p.inventory.Remove(pick, 1)) return;
            const string was = p.talents.PlaceTotem(pick, today);
            if (!was.empty()) p.inventory.Add(was, 1);
            totem_cursor = 0;
        }
        // Some blessings are health, or mana: the pools are what they now are.
        p.SyncHitpoints();
        p.SyncMana();
        // Two lines: a toast is one line from the right-hand edge, and the
        // Pit Lord's blessing alone is most of a narrow window.
        PushToast(what + " wakes, until dawn.", {255, 214, 120, 255});
        if (def) PushToast(def->name + ": " + def->text + ".", {255, 214, 120, 255});
        Audio::Play(Sfx::QuestStart);
        return;
    }
    if (input.Pressed(Action::Drop) && !p.talents.PlacedTotem().empty()) {
        if (p.inventory.Full()) {
            PushToast("There is no room in your pack to carry it.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        const string was = p.talents.TakeTotem();
        p.inventory.Add(was, 1);
        p.SyncHitpoints();
        p.SyncMana();
        const ItemDef* thing = items.Get(was);
        PushToast("You lift " + (thing ? thing->name : was) + " out of the ring. Its blessing goes with it.", Palette::TextDim);
        Audio::Play(Sfx::UiBack);
        totem_cursor = 0;
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawTotemRing() {
    ui.Dim(0.55f);
    const Player& p = world->player;
    const vector<string> have = TotemChoices();
    const float row_h = 46.0f;
    // Room for as many rows as the window has, and no fewer than three so the
    // panel is not a letterbox with one totem in it.
    const float fit_h = ui.ViewHeight() - 16.0f;
    const int   rows = std::max(3, static_cast<int>(have.size()));
    const float want_h = 176.0f + rows * row_h;
    const SDL_FRect panel = CenteredPanel(ui, std::min(620.0f, ui.ViewWidth() - 16.0f), std::min(want_h, fit_h));
    ui.Panel(panel);
    ui.Text("The Ring", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight, Align::Center);
    const float x = panel.x + 24.0f, w = panel.w - 48.0f;
    float y = panel.y + 52.0f;
    y += ui.TextWrapped("One totem stands here at a time. Touched, it gives its blessing until dawn, wherever you go.",
                        x, y, w, TextSize::Small, Palette::TextDim) + 10.0f;

    if (have.empty()) {
        ui.TextWrapped("You have no totems. A boss leaves its totem the " + std::to_string(Talents::TOTEM_KILLS) +
                       "th time you bring it down -- it is back every dawn. The Boons page of your skills keeps count.",
                       x + 30.0f, y + 26.0f, w - 60.0f, TextSize::Small, Palette::Text);
    }
    // The cursor kept in view, in a window that may be shorter than the list.
    const int fit = std::max(1, static_cast<int>((panel.y + panel.h - 56.0f - y) / row_h));
    const int first = std::clamp(totem_cursor - fit + 1, 0, std::max(0, static_cast<int>(have.size()) - fit));
    for (int i = first; i < static_cast<int>(have.size()) && i < first + fit; ++i) {
        const TotemDef* def = skill_trees.Totem(have[i]);
        const ItemDef* thing = items.Get(have[i]);
        const SDL_FRect row = {x, y, w, row_h - 6.0f};
        const bool selected = i == totem_cursor;
        const bool standing = have[i] == p.talents.PlacedTotem();
        const bool awake = standing && p.talents.TotemAwake();
        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, awake ? SDL_Color{255, 214, 120, 255} : (selected ? Palette::Highlight : Palette::BorderDim),
                   selected || awake ? 2.0f : 1.0f);
        if (thing && !thing->icon.empty())
            if (SDL_Texture* tex = textures->Get(thing->icon)) {
                const SDL_FRect ic = {row.x + 6.0f, row.y + 4.0f, 32.0f, 32.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(thing ? thing->name : have[i], row.x + 46.0f, row.y + 3.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        ui.Text(standing ? (awake ? "awake until dawn" : "in the ring, asleep") : "in your pack",
                row.x + row.w - 10.0f, row.y + 5.0f, TextSize::Small,
                awake ? SDL_Color{255, 214, 120, 255} : (standing ? SDL_Color{170, 160, 190, 255} : Palette::TextDim),
                Align::Right);
        if (def) ui.Text(def->name + ": " + def->text, row.x + 46.0f, row.y + 22.0f, TextSize::Small, Palette::TextDim);
        y += row_h;
    }

    string foot;
    if (!have.empty()) {
        const bool standing = have[std::clamp(totem_cursor, 0, static_cast<int>(have.size()) - 1)] == p.talents.PlacedTotem();
        foot = input.PromptFor(Action::Confirm) + (standing ? " wake it" : (p.talents.PlacedTotem().empty() ? " stand it here" : " stand it here instead")) + "     ";
        if (!p.talents.PlacedTotem().empty()) foot += input.PromptFor(Action::Drop) + " lift it out     ";
    }
    foot += input.PromptFor(Action::Back) + " leave";
    ui.Text(foot, panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

void Game::UpdateSleepPrompt() {
    MoveCursor(sleep_cursor, 2);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const World::SleepChoice how = sleep_cursor == 0 ? World::SleepChoice::Through
                                                         : World::SleepChoice::Reverie;
        // An inn's bed is paid for. Your own, and the ground, are not.
        Player& sleeper = world->player;
        if (sleep_fee > 0 && sleeper.inventory.Coins() < sleep_fee) {
            PushToast("A bed here is " + std::to_string(sleep_fee) + " coins, and you have " +
                      std::to_string(sleeper.inventory.Coins()) + ". A camp costs nothing.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        SetState(GameState::Play);
        if (world->Sleep(how, ctx) && sleep_fee > 0) {
            sleeper.inventory.SpendCoins(sleep_fee);
            PushToast("Paid " + std::to_string(sleep_fee) + " coins for the bed.", Palette::TextDim);
        }
        sleep_fee = 0;
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawSleepPrompt() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 520.0f, 300.0f);

    ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
    ui.Fill(panel, {214, 197, 158, 250});
    ui.Outline(panel, {120, 96, 58, 255}, 2.0f);

    ui.Text(sleep_title.empty() ? string("A bed for the night") : sleep_title,
            panel.x + panel.w / 2.0f, panel.y + 24.0f, TextSize::Large, {68, 48, 28, 255}, Align::Center);
    ui.Fill({panel.x + 40.0f, panel.y + 64.0f, panel.w - 80.0f, 1.0f}, {140, 116, 78, 255});

    // How much night is left to spend, which is what the choice is about.
    const float to_dawn = world->clock.SecondsToDawn() / WorldClock::SECONDS_PER_HOUR;
    const int hours_left = std::max(1, static_cast<int>(to_dawn + 0.5f));
    ui.Text("It is " + world->clock.TimeText() + ". Dawn is " + std::to_string(hours_left) +
            (hours_left == 1 ? " hour off." : " hours off."),
            panel.x + panel.w / 2.0f, panel.y + 76.0f, TextSize::Small, {96, 74, 44, 255}, Align::Center);

    for (int i = 0; i < 2; ++i) {
        const SDL_FRect row = {panel.x + 40.0f, panel.y + 108.0f + i * 66.0f, panel.w - 80.0f, 56.0f};
        const bool on = (i == sleep_cursor);
        if (on) {
            ui.Fill(row, {236, 222, 184, 255});
            ui.Outline(row, {120, 96, 58, 255}, 2.0f);
        } else {
            ui.Outline(row, {170, 148, 108, 255}, 1.0f);
        }
        ui.Text(kSleepRows[i].name, row.x + 16.0f, row.y + 8.0f, TextSize::Body,
                on ? SDL_Color{52, 34, 16, 255} : SDL_Color{88, 68, 42, 255});
        ui.Text(kSleepRows[i].what, row.x + 16.0f, row.y + 31.0f, TextSize::Small,
                on ? SDL_Color{96, 74, 44, 255} : SDL_Color{128, 106, 74, 255});
    }

    ui.Text(input.PromptFor(Action::Confirm) + " lie down     " + input.PromptFor(Action::Back) + " stay up",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small,
            {96, 74, 44, 255}, Align::Center);
}


// =============================================================================
//  Crafting
// =============================================================================

void Game::UpdateCrafting() {
    const vector<const ItemDef*> recipes = items.Recipes(craft_station);
    // Opened on the row it was left on. state_time is nothing on the frame a
    // panel opens, which is the one frame the cursor is put back rather than
    // remembered.
    int& kept = craft_cursor_at[std::clamp(static_cast<int>(craft_station), 0, 7)];
    if (state_time <= 0.0f) craft_cursor = std::clamp(kept, 0, std::max(0, static_cast<int>(recipes.size()) - 1));
    MoveCursor(craft_cursor, static_cast<int>(recipes.size()));
    kept = craft_cursor;

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !recipes.empty()) {
        const ItemDef* recipe = recipes[std::clamp(craft_cursor, 0,
                                                   static_cast<int>(recipes.size()) - 1)];
        Player& p = world->player;
        const int skill = CraftSkill(craft_station);

        const ItemDef* result = items.Get(recipe->craft_result);
        // A dye is brewed but never taught: see ItemDef::untaught.
        const bool brew_taught = craft_station == CraftStation::Cauldron && !(result && result->untaught);
        if ((brew_taught || (result && result->needs_recipe)) &&
            !world->KnowsRecipe(recipe->craft_result)) {
            PushToast("You have not learned that recipe yet.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        if (p.skills.Level(skill) < recipe->craft_level) {
            PushToast("Needs " + string(SkillName(skill)) + " " + std::to_string(recipe->craft_level) + ".",
                      {235, 150, 120, 255});
            return;
        }

        // One, or with sprint held as many as there is the stuff for: the same
        // hand the shop uses for ten. Twenty bars used to be twenty presses.
        // Each is made exactly as one is -- its own materials, its own room in
        // the pack, its own chance of burning -- so holding the button is a
        // convenience and never a better deal.
        const int wanted = input.Down(Action::Sprint) ? 99 : 1;
        int made_n = 0, burnt_n = 0;
        string stopped;
        for (int n = 0; n < wanted; ++n) {
            bool have_all = true;
            for (const auto& in : recipe->craft_inputs)
                if (!p.inventory.Has(in.first, in.second)) { have_all = false; break; }
            if (!have_all) { stopped = "You are missing materials."; break; }
            if (p.inventory.FreeSlots() == 0) { stopped = "Your pack is full."; break; }

            for (const auto& in : recipe->craft_inputs) p.inventory.Remove(in.first, in.second);

            // At a fire it can still be ruined, the way it always could: the
            // chance falls away as the cook's level climbs past the dish's.
            if (craft_station == CraftStation::Range) {
                const float burn = std::max(0.0f, 0.34f - (p.skills.Level(skill) - recipe->craft_level) * 0.03f);
                if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < burn) {
                    p.GrantXp(skill, std::max(1, recipe->craft_xp / 8));
                    ++burnt_n;
                    continue;
                }
            }

            p.inventory.Add(recipe->craft_result, recipe->craft_qty);
            p.GrantXp(skill, recipe->craft_xp);
            // Tell the journal something was made. After the burn roll, so a
            // ruined dinner does not count, and by how many came off -- a
            // fleece spins into two bolts, and an order for two is filled by
            // one of them.
            QuestEvent made_it;
            made_it.type   = ObjectiveType::Craft;
            made_it.target = recipe->craft_result;
            made_it.amount = std::max(1, recipe->craft_qty);
            made_it.map_id = world->MapId();
            quests->Notify(made_it, p.inventory);
            ++made_n;
        }

        const ItemDef* made = items.Get(recipe->craft_result);
        const string what = made ? made->name : recipe->craft_result;
        if (made_n > 0) {
            const int total = made_n * std::max(1, recipe->craft_qty);
            PushToast(string(craft_station == CraftStation::Cauldron ? "Brewed " :
                             craft_station == CraftStation::Anvil ? "Smithed " :
                             craft_station == CraftStation::Range ? "Cooked " :
                             craft_station == CraftStation::Loom ? "Wove " : "Crafted ") +
                      (total > 1 ? std::to_string(total) + "x " : string()) + what + ".", Palette::Xp);
        }
        if (burnt_n > 0) {
            PushToast(burnt_n > 1 ? "Burnt " + std::to_string(burnt_n) + " of them." : string("Burnt it."),
                      {200, 110, 90, 255});
            Audio::Play(Sfx::Burn);
        }
        // Said when nothing at all could be made; a run that simply came to
        // the end of the iron has nothing to apologise for.
        if (made_n == 0 && burnt_n == 0 && !stopped.empty()) PushToast(stopped, {235, 150, 120, 255});
        quests->RefreshCollectObjectives(p.inventory);
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

// =============================================================================
//  What an item is worth
//
//  Every panel that shows an item used to show its name, its description and
//  what it needed to be worn -- and not one of its numbers. A sword on a
//  smith's shelf said nothing about being better than the one in your hand,
//  which is the only question anybody was asking of it.
//
//  This draws the numbers, and next to each the difference it would make
//  against what is worn in that slot now. Green is better, red is worse, and a
//  stat that is nothing on both pieces is left off entirely: a helmet's line
//  is one line, not five zeroes. The comparison is what makes it worth
//  reading, so a piece that goes in an empty slot compares against nothing and
//  shows the whole of its bonus as the gain it is.
// =============================================================================

namespace {

const SDL_Color kWorse{225, 130, 120, 255};

// The one thing a panel decides for itself: what a verdict looks like. Which
// rows there are is ItemStatLines' business, in the item layer, where it can
// be checked by the self-test.
SDL_Color DeltaColour(int verdict) {
    return verdict > 0 ? Palette::Xp : (verdict < 0 ? kWorse : Palette::TextDim);
}

} // namespace

// What is in the same slot now, which is what a piece is weighed against.
const ItemDef* Game::WornAgainst(const ItemDef& d) const {
    if (d.slot == SLOT_NONE) return nullptr;
    const string& in_slot = world->player.equipment.InSlot(d.slot);
    return in_slot.empty() ? nullptr : items.Get(in_slot);
}

// The line above the numbers, saying what they are being measured against.
string Game::WornAgainstLine(const ItemDef& d) const {
    if (d.slot == SLOT_NONE) return string();
    const string& in_slot = world->player.equipment.InSlot(d.slot);
    if (!in_slot.empty() && in_slot == d.id) return "Worn now";
    if (const ItemDef* worn = in_slot.empty() ? nullptr : items.Get(in_slot))
        return "Instead of " + worn->name;
    return "Nothing worn there";
}

float Game::DrawItemStats(const ItemDef& d, float x, float y, float w) {
    const Player& p = world->player;
    const float top = y;
    const float row = 16.0f;
    // Three columns: what the stat is, what this piece gives, and the change.
    // The two numbers sit near each other rather than at opposite ends of the
    // pane, because they are the pair being compared.
    const float value_x = x + w * 0.58f;
    const float delta_x = x + w * 0.88f;

    const ItemDef* worn = WornAgainst(d);
    const bool compare = (d.slot != SLOT_NONE);

    if (compare) {
        ui.Text(WornAgainstLine(d), x, y, TextSize::Small, Palette::TextDim);
        y += row + 2.0f;
    }
    for (const ItemStat& r : ItemStatLines(d, worn, compare)) {
        ui.Text(r.label, x, y, TextSize::Small, Palette::Text);
        ui.Text(r.value, value_x, y, TextSize::Small,
                r.value == "+0" ? Palette::TextDim : Palette::Xp, Align::Right);
        if (!r.delta.empty()) ui.Text(r.delta, delta_x, y, TextSize::Small, DeltaColour(r.verdict), Align::Right);
        y += row;
    }

    // What drinking or eating it does. A dish says the rest of its piece
    // itself, in the panel that cooks it.
    if (d.consumable && !d.IsDish()) {
        string line;
        if (d.heal > 0)  line += "Heals " + std::to_string(d.heal) + "   ";
        if (d.mana > 0)  line += "+" + std::to_string(d.mana) + " mana   ";
        if (d.stamina)   line += "full breath   ";
        // A boost is a flat amount plus a share of the level, so what it is
        // worth depends on who drinks it. Worked out for this character rather
        // than printed as the two numbers it is stored as: "+3 and 10%" is a
        // recipe, "+10 Strength" is the answer.
        for (const auto& b : d.boosts) {
            const int gain = ItemDef::BoostGain(b.second, p.skills.Level(b.first));
            char buf[64];
            SDL_snprintf(buf, sizeof(buf), "+%d %s   ", gain, SkillName(b.first));
            line += buf;
        }
        if (!line.empty()) y += ui.TextWrapped(line, x, y, w, TextSize::Small, Palette::Xp);
    }

    // What it does that no stat block can say. The bag has always printed
    // this; the shop and the anvil never did, which meant the one line that
    // makes a legendary piece worth having was the one line you could not
    // read until you owned it.
    if (!d.passive_text.empty())
        y += ui.TextWrapped(d.passive_text, x, y + 2.0f, w, TextSize::Small, Palette::Highlight) + 2.0f;

    return y > top ? y - top + 4.0f : 0.0f;
}

// =============================================================================
//  The card beside the cursor
//
//  The bag and the storage chest have no room under their grids for a stat
//  block: their lower half is already the name, the tier and the description.
//  So the numbers come to the cursor instead -- a small card beside whatever
//  square is highlighted, for the things where the numbers are the question.
//
//  Only for what can be worn. A card over every rock and bar would be noise
//  covering the grid it is trying to explain.
// =============================================================================

void Game::DrawItemCard(const ItemDef& d, const SDL_FRect& slot) {
    if (d.slot == SLOT_NONE) return;
    const ItemDef* worn = WornAgainst(d);
    const vector<ItemStat> rows = ItemStatLines(d, worn, true);
    if (rows.empty()) return;

    const string head = WornAgainstLine(d);
    const float pad = 10.0f, row_h = 16.0f;

    // Wide enough for its own longest line, measured rather than guessed: item
    // names run from "Cap" to "Orichalcum Greatsword".
    float label_w = 0.0f, value_w = 0.0f, delta_w = 0.0f;
    for (const ItemStat& r : rows) {
        label_w = std::max(label_w, ui.Measure(r.label, TextSize::Small).x);
        value_w = std::max(value_w, ui.Measure(r.value, TextSize::Small).x);
        delta_w = std::max(delta_w, ui.Measure(r.delta, TextSize::Small).x);
    }
    const float gap = 10.0f;
    const float widest_head = std::max(ui.Measure(d.name, TextSize::Small).x,
                                       ui.Measure(head, TextSize::Small).x);
    const float card_w = std::max(widest_head, label_w + gap + value_w + gap + delta_w) + pad * 2.0f;
    const float card_h = pad * 2.0f + row_h * 2.0f + 4.0f + row_h * static_cast<float>(rows.size());

    // Beside the square, on whichever side it fits; nudged back on-screen
    // rather than allowed to hang off an edge.
    SDL_FRect card = {slot.x + slot.w + 10.0f, slot.y - 6.0f, card_w, card_h};
    if (card.x + card.w > ui.ViewWidth() - 8.0f) card.x = slot.x - card.w - 10.0f;
    card.x = std::clamp(card.x, 8.0f, std::max(8.0f, ui.ViewWidth() - card.w - 8.0f));
    card.y = std::clamp(card.y, 8.0f, std::max(8.0f, ui.ViewHeight() - card.h - 8.0f));

    ui.Panel(card);
    float y = card.y + pad;
    ui.Text(d.name, card.x + pad, y, TextSize::Small, Palette::Highlight);
    y += row_h;
    ui.Text(head, card.x + pad, y, TextSize::Small, Palette::TextDim);
    y += row_h + 4.0f;
    const float value_x = card.x + card.w - pad - delta_w - gap;
    const float delta_x = card.x + card.w - pad;
    for (const ItemStat& r : rows) {
        ui.Text(r.label, card.x + pad, y, TextSize::Small, Palette::Text);
        ui.Text(r.value, value_x, y, TextSize::Small,
                r.value == "+0" ? Palette::TextDim : Palette::Xp, Align::Right);
        if (!r.delta.empty()) ui.Text(r.delta, delta_x, y, TextSize::Small, DeltaColour(r.verdict), Align::Right);
        y += row_h;
    }
}

void Game::DrawCrafting() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 450.0f);
    ui.Panel(panel);
    const bool anvil = (craft_station == CraftStation::Anvil);
    const bool cauldron = (craft_station == CraftStation::Cauldron);
    const bool fire = (craft_station == CraftStation::Range);
    const bool loom = (craft_station == CraftStation::Loom);
    const int skill = CraftSkill(craft_station);
    ui.Text(craft_title.empty() ? (cauldron ? "Cauldron" : anvil ? "Anvil" : fire ? "Cooking fire"
                                 : loom ? "Loom" : "Workbench") : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world->player;
    ui.Text(string(SkillName(skill)) + " " + std::to_string(p.skills.Level(skill)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // Say where the rest is made, so a missing recipe reads as "elsewhere"
    // rather than "gone".
    ui.Text(cauldron ? "Brewing: herbs and a vial. A recipe has to be learned before it can be brewed."
            : anvil  ? "Smithing: anything made from metal. Wood and leather are worked at a workbench."
            // One line, and it has to fit the panel at 1280x720: --audit counts
            // anything wider as a runoff, and it is right to.
            : fire   ? "Cooking: plain food, and dishes that sit with you a while."
            : loom   ? "Weaving: cloth from any fibre, and the robes. Dyes are boiled at a cauldron."
                     : "Wood, leather and thread. Cloth is woven at a loom, metal smithed at an anvil.",
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
        // A brew, or anything else marked as taught rather than worked out.
        const bool taught = (cauldron && !(made && made->untaught)) || (made && made->needs_recipe);
        const bool known = !taught || world->KnowsRecipe(r->craft_result);
        const bool unlocked = known && p.skills.Level(skill) >= r->craft_level;

        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        if (made && !made->icon.empty())
            if (SDL_Texture* tex = textures->Get(made->icon)) {
                const SDL_FRect ic = {row.x + 4.0f, row.y + 3.0f, 24.0f, 24.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(known ? (made ? made->name : r->craft_result) : string("Unknown recipe"), row.x + 34.0f, row.y + 5.0f,
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
    const bool taught_here = (cauldron && !(made && made->untaught)) || (made && made->needs_recipe);
    if (taught_here && !world->KnowsRecipe(r->craft_result)) {
        y += ui.TextWrapped(string(cauldron ? "You have not learned to brew this yet."
                                            : "You have not been shown how to make this yet.") +
                            (made && !made->recipe_from.empty() ? " " + made->recipe_from : string("")),
                            dx, y, panel.w - list_w - 64.0f, TextSize::Small, {235, 150, 120, 255}) + 8.0f;
    }
    if (made) y += ui.TextWrapped(made->description, dx, y, panel.w - list_w - 64.0f,
                                  TextSize::Small, Palette::TextDim) + 8.0f;
    // A dish says what it is worth eating for, and for how long.
    if (made && made->IsDish()) {
        const auto share = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; };
        string line;
        if (made->dish_max_hp > 0.0f)      line += "+" + share(made->dish_max_hp) + " max health   ";
        if (made->dish_max_mana > 0.0f)    line += "+" + share(made->dish_max_mana) + " max mana   ";
        if (made->dish_max_stamina > 0.0f) line += "+" + share(made->dish_max_stamina) + " max breath   ";
        for (const auto& b : made->dish_levels)
            line += "+" + std::to_string(b.second) + " " + SkillName(b.first) + "   ";
        y += ui.TextWrapped(line, dx, y, panel.w - list_w - 64.0f, TextSize::Small, Palette::Xp) + 2.0f;
        const int mins = static_cast<int>(made->dish_minutes);
        ui.Text("for " + std::to_string(mins) + (mins == 1 ? " minute" : " minutes") + ", and one dish at a time",
                dx, y, TextSize::Small, Palette::TextDim);
        y += 22.0f;
    }
    if (made && !made->requirements.empty()) {
        string req = "To use: ";
        for (const auto& rq : made->requirements)
            req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
        ui.Text(req, dx, y, TextSize::Small, Palette::Text);
        y += 22.0f;
    }
    // What it would be worth wearing, against what is being worn. The reason
    // to stand at an anvil is that the thing on it is better than the thing in
    // your hand, and until now the panel never said so.
    if (made) y += DrawItemStats(*made, dx, y, panel.w - list_w - 64.0f);

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
    ui.Text(std::to_string(r->craft_xp) + " " + SkillName(skill) + " XP", dx, y, TextSize::Small,
            Palette::TextDim);

    const string verb = cauldron ? " brew" : anvil ? " smith" : fire ? " cook" : loom ? " weave" : " craft";
    ui.Text(input.PromptFor(Action::Confirm) + verb + "     " +
            input.PromptFor(Action::Sprint) + " + " + input.PromptFor(Action::Confirm) + verb + " all     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Enchanting
//
//  Like the crafting panel, but the thing made is not on a list: it is one of
//  the player's own pieces, with a charm worked into it. So the left-hand list
//  is the charms, and the right-hand side says what the chosen one does, what
//  it costs, and which piece in the bag it would go into -- stepped through
//  with left and right, since a bag can hold three rings.
// =============================================================================

void Game::UpdateEnchanting() {
    const vector<const EnchantDef*> list = items.Enchantments();
    MoveCursor(enchant_cursor, static_cast<int>(list.size()));
    Player& p = world->player;
    const EnchantDef* e = list.empty() ? nullptr
        : list[std::clamp(enchant_cursor, 0, static_cast<int>(list.size()) - 1)];
    const vector<int> targets = e ? ::Enchanting::Targets(items, *e, p.inventory) : vector<int>{};
    const int n = static_cast<int>(targets.size());
    if (n > 0) {
        if (input.MenuRight()) { enchant_target = (enchant_target + 1) % n; Audio::Play(Sfx::UiMove); }
        if (input.MenuLeft())  { enchant_target = (enchant_target + n - 1) % n; Audio::Play(Sfx::UiMove); }
        enchant_target = std::clamp(enchant_target, 0, n - 1);
    } else {
        enchant_target = 0;
    }

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && e) {
        const SDL_Color bad = {235, 150, 120, 255};
        if (!world->KnowsEnchantment(e->id)) {
            PushToast("You have not learned that enchantment yet.", bad);
            Audio::Play(Sfx::UiError);
        } else if (p.skills.Level(SKILL_MAGIC) < e->level) {
            PushToast("Needs Magic " + std::to_string(e->level) + ".", bad);
            Audio::Play(Sfx::UiError);
        } else {
            string why;
            const int slot = n > 0 ? targets[enchant_target] : -1;
            const string piece = slot >= 0 ? p.inventory.Slot(slot).id : string();
            if (::Enchanting::Work(items, *e, p.inventory, slot, why)) {
                p.GrantXp(SKILL_MAGIC, e->xp);
                const ItemDef* made = items.Get(items.EnchantedId(piece, e->id));
                PushToast("Enchanted: " + (made ? made->name : piece) + ".", Palette::Xp);
                Audio::Play(Sfx::SpellCast);
                quests->RefreshCollectObjectives(p.inventory);
            } else {
                PushToast(why, bad);
                Audio::Play(Sfx::UiError);
            }
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawEnchanting() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 450.0f);
    ui.Panel(panel);
    ui.Text(craft_title.empty() ? "Enchanting Table" : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world->player;
    ui.Text("Magic " + std::to_string(p.skills.Level(SKILL_MAGIC)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);
    ui.Text("A charm worked into a worn piece, for Magic. Each is learned before it is worked.",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 50.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    const vector<const EnchantDef*> list = items.Enchantments();
    if (list.empty()) {
        ui.Text("Nothing to work here.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f, TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    const float list_w = 250.0f, row_h = 34.0f;
    constexpr int SHOWN = 9;
    const int count = static_cast<int>(list.size());
    const int first = std::clamp(enchant_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, panel.y + 46.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, panel.y + 62.0f + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const EnchantDef* e = list[i];
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + (i - first) * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (i == enchant_cursor);
        const bool known = world->KnowsEnchantment(e->id);
        const bool unlocked = known && p.skills.Level(SKILL_MAGIC) >= e->level;
        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        ui.Text(known ? e->name : string("Unknown enchantment"), row.x + 10.0f, row.y + 5.0f,
                TextSize::Small,
                !unlocked ? SDL_Color{120, 110, 100, 255}
                          : (selected ? Palette::Highlight : Palette::Text));
        ui.Text("Lv " + std::to_string(e->level), row.x + row.w - 8.0f,
                row.y + 5.0f, TextSize::Small, Palette::TextDim, Align::Right);
    }

    // The chosen charm: what it does, what it fits, what it costs, and the
    // piece it would go into.
    const EnchantDef* e = list[std::clamp(enchant_cursor, 0, count - 1)];
    const float dx = panel.x + list_w + 40.0f;
    const float dw = panel.w - list_w - 64.0f;
    float y = panel.y + 62.0f;
    const bool known = world->KnowsEnchantment(e->id);

    ui.Text(known ? e->name : string("Unknown enchantment"), dx, y, TextSize::Body, Palette::Highlight);
    y += 28.0f;
    if (!known)
        y += ui.TextWrapped("You have not learned this yet." + (e->from.empty() ? string("") : " " + e->from),
                            dx, y, dw, TextSize::Small, {235, 150, 120, 255}) + 8.0f;
    if (!e->text.empty())
        y += ui.TextWrapped(e->text, dx, y, dw, TextSize::Small, Palette::TextDim) + 8.0f;
    {
        string fits = "Fits: ";
        for (size_t i = 0; i < e->slots.size(); ++i)
            fits += string(i ? ", " : "") + EquipSlotName(e->slots[i]);
        ui.Text(fits, dx, y, TextSize::Small, Palette::Text);
        y += 22.0f;
    }

    ui.Text("Materials", dx, y, TextSize::Small, Palette::Highlight);
    y += 20.0f;
    for (const auto& in : e->inputs) {
        const ItemDef* mat = items.Get(in.first);
        const int held = p.inventory.Count(in.first);
        char line[128];
        SDL_snprintf(line, sizeof(line), "%s  %d / %d",
                     (mat ? mat->name.c_str() : in.first.c_str()), held, in.second);
        ui.Text(line, dx, y, TextSize::Small,
                held >= in.second ? Palette::Xp : SDL_Color{225, 130, 120, 255});
        y += 18.0f;
    }
    y += 10.0f;
    ui.Text(std::to_string(e->xp) + " Magic XP", dx, y, TextSize::Small, Palette::TextDim);
    y += 26.0f;

    const vector<int> targets = ::Enchanting::Targets(items, *e, p.inventory);
    if (targets.empty()) {
        ui.TextWrapped("Nothing in your pack takes this enchantment.", dx, y, dw, TextSize::Small, Palette::TextDim);
    } else {
        const int n = static_cast<int>(targets.size());
        const int which = std::clamp(enchant_target, 0, n - 1);
        const ItemStack& s = p.inventory.Slot(targets[which]);
        const ItemDef* d = items.Get(s.id);
        ui.Text("Work it into", dx, y, TextSize::Small, Palette::Highlight);
        y += 20.0f;
        char line[160];
        if (n > 1)
            SDL_snprintf(line, sizeof(line), "<  %s  >   %d of %d", d ? d->name.c_str() : s.id.c_str(), which + 1, n);
        else
            SDL_snprintf(line, sizeof(line), "%s", d ? d->name.c_str() : s.id.c_str());
        if (d && !d->icon.empty())
            if (SDL_Texture* tex = textures->Get(d->icon)) {
                const SDL_FRect ic = {dx, y - 2.0f, 22.0f, 22.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(line, dx + 28.0f, y, TextSize::Small, Palette::Xp);
    }

    ui.Text(input.PromptFor(Action::Confirm) + " enchant     left / right choose the piece     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Shops
// =============================================================================

void Game::OpenShop(const string& id) {
    if (!shop_db.Get(id)) return;
    shop_id = id;
    shop_tab = 0;
    shop_cursor = 0;
    world->shops.SetDay(world->clock.QuestDay());
    SetState(GameState::Shop);
}

vector<string> Game::ShopSellRows() const {
    vector<string> rows;
    const Inventory& bag = world->player.inventory;
    for (int i = 0; i < bag.SlotCount(); ++i) {
        const ItemStack& s = bag.Slot(i);
        if (s.Empty() || s.id == "coins") continue;
        if (std::find(rows.begin(), rows.end(), s.id) == rows.end()) rows.push_back(s.id);
    }
    return rows;
}

void Game::UpdateShop() {
    const ShopDef* shop = shop_db.Get(shop_id);
    if (!shop || input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Play);
        return;
    }

    if (input.MenuLeft() || input.MenuRight()) {
        shop_tab = 1 - shop_tab;
        shop_cursor = 0;
        Audio::Play(Sfx::UiMove);
    }

    Player& p = world->player;
    const vector<const ShopStock*> shelf = Trade::Shelf(*shop, quests);
    const vector<string> sell_rows = ShopSellRows();
    const int count = shop_tab == 0 ? static_cast<int>(shelf.size()) : static_cast<int>(sell_rows.size());
    MoveCursor(shop_cursor, count);
    shop_cursor = std::clamp(shop_cursor, 0, std::max(0, count - 1));

    if (!(input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) || count == 0) return;

    // One at a time, or ten -- or the whole stack when selling -- with sprint held.
    const bool many = input.Down(Action::Sprint);
    const string item = shop_tab == 0 ? shelf[shop_cursor]->item : sell_rows[shop_cursor];
    const ItemDef* d = items.Get(item);
    const string name = d ? d->name : item;

    TradeOutcome t;
    if (shop_tab == 0) {
        t = Trade::Buy(*shop, world->shops, p.inventory, items, quests, item, many ? 10 : 1);
        if (t.result == TradeResult::Ok)
            PushToast("Bought " + std::to_string(t.qty) + "x " + name + " for " +
                      std::to_string(t.coins) + " coins.", Palette::Xp);
    } else {
        t = Trade::Sell(*shop, p.inventory, items, item, many ? p.inventory.Count(item) : 1);
        if (t.result == TradeResult::Ok)
            PushToast("Sold " + std::to_string(t.qty) + "x " + name + " for " +
                      std::to_string(t.coins) + " coins.", Palette::Highlight);
    }

    if (t.result == TradeResult::Ok) {
        Audio::Play(Sfx::Coins);
        quests->RefreshCollectObjectives(p.inventory);
    } else {
        Audio::Play(Sfx::UiError);
        PushToast(Trade::Message(t.result), {235, 150, 120, 255});
    }
}

void Game::DrawShop() {
    const ShopDef* shop = shop_db.Get(shop_id);
    if (!shop) return;
    const Player& p = world->player;

    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 480.0f);
    ui.Panel(panel);
    ui.Text(shop->name, panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);
    ui.Text(std::to_string(p.inventory.Coins()) + " coins", panel.x + panel.w - 24.0f,
            panel.y + 24.0f, TextSize::Small, Palette::Highlight, Align::Right);

    // The two tabs.
    const float list_w = 330.0f;
    for (int t = 0; t < 2; ++t) {
        const SDL_FRect tab = {panel.x + 20.0f + t * 110.0f, panel.y + 52.0f, 100.0f, 26.0f};
        const bool on = (t == shop_tab);
        ui.Fill(tab, on ? SDL_Color{58, 46, 28, 230} : SDL_Color{30, 26, 22, 200});
        ui.Outline(tab, on ? Palette::Highlight : Palette::Border, 1.0f);
        ui.Text(t == 0 ? "Buy" : "Sell", tab.x + tab.w / 2.0f, tab.y + 4.0f, TextSize::Small,
                on ? Palette::Highlight : Palette::TextDim, Align::Center);
    }
    ui.Text("< " + string(shop_tab == 0 ? "What is on the shelf" : "What you carry") + " >",
            panel.x + 250.0f, panel.y + 56.0f, TextSize::Small, Palette::TextDim);

    const vector<const ShopStock*> shelf = Trade::Shelf(*shop, quests);
    const vector<string> sell_rows = ShopSellRows();
    const int count = shop_tab == 0 ? static_cast<int>(shelf.size()) : static_cast<int>(sell_rows.size());

    if (count == 0) {
        ui.Text(shop_tab == 0 ? "Nothing on the shelf for you yet." : "You have nothing to sell.",
                panel.x + 20.0f + list_w / 2.0f, panel.y + 200.0f, TextSize::Body,
                Palette::TextDim, Align::Center);
    }

    const float row_h = 34.0f, top = panel.y + 92.0f;
    constexpr int SHOWN = 9;
    const int first = std::clamp(shop_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, top - 16.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, top + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);

    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const string id = shop_tab == 0 ? shelf[i]->item : sell_rows[i];
        const ItemDef* d = items.Get(id);
        const SDL_FRect row = {panel.x + 20.0f, top + (i - first) * row_h, list_w, row_h - 4.0f};
        const bool selected = (i == shop_cursor);
        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        if (d && !d->icon.empty())
            if (SDL_Texture* tex = textures->Get(d->icon)) {
                const SDL_FRect ic = {row.x + 4.0f, row.y + 3.0f, 24.0f, 24.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }

        string right;
        SDL_Color right_c = Palette::Text;
        bool dim = false;
        if (shop_tab == 0) {
            const int left = world->shops.Remaining(*shop, id);
            right = d ? std::to_string(Trade::BuyPrice(*shop, *d)) + "c" : "";
            right_c = (d && p.inventory.Coins() >= Trade::BuyPrice(*shop, *d)) ? Palette::Highlight
                                                                                : SDL_Color{225, 130, 120, 255};
            dim = (left == 0);
            ui.Text(left == 0 ? "sold out" : ("x" + std::to_string(left)),
                    row.x + row.w - 64.0f, row.y + 6.0f, TextSize::Small, Palette::TextDim, Align::Right);
        } else {
            const int offer = d ? Trade::SellPrice(*shop, items, *d) : 0;
            right = offer > 0 ? std::to_string(offer) + "c" : "--";
            right_c = offer > 0 ? Palette::Xp : Palette::TextDim;
            dim = (offer == 0);
            ui.Text("x" + std::to_string(p.inventory.Count(id)), row.x + row.w - 64.0f, row.y + 6.0f,
                    TextSize::Small, Palette::TextDim, Align::Right);
        }
        ui.Text(d ? d->name : id, row.x + 34.0f, row.y + 6.0f, TextSize::Small,
                dim ? SDL_Color{120, 110, 100, 255} : (selected ? Palette::Highlight : Palette::Text));
        ui.Text(right, row.x + row.w - 8.0f, row.y + 6.0f, TextSize::Small, right_c, Align::Right);
    }

    // Detail for the highlighted row.
    if (count > 0) {
        const int index = std::clamp(shop_cursor, 0, count - 1);
        const string id = shop_tab == 0 ? shelf[index]->item : sell_rows[index];
        if (const ItemDef* d = items.Get(id)) {
            const float dx = panel.x + list_w + 44.0f;
            const float dw = panel.w - list_w - 68.0f;
            float y = top;
            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 28.0f;
            y += ui.TextWrapped(d->description, dx, y, dw, TextSize::Small, Palette::TextDim) + 8.0f;
            if (!d->requirements.empty()) {
                string req = "To use: ";
                for (const auto& rq : d->requirements)
                    req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
                ui.Text(req, dx, y, TextSize::Small, Palette::Text);
                y += 22.0f;
            }
            // The numbers, and what they would be instead of what is on: a
            // shelf of swords is a row of names until it says which is better
            // than the one you came in with.
            y += DrawItemStats(*d, dx, y, dw);
            ui.Text("You carry " + std::to_string(p.inventory.Count(id)), dx, y, TextSize::Small, Palette::Text);
            y += 20.0f;

            if (shop_tab == 0) {
                ui.Text("Price " + std::to_string(Trade::BuyPrice(*shop, *d)) + " coins", dx, y,
                        TextSize::Small, Palette::Highlight);
                y += 20.0f;
                ui.Text(std::to_string(world->shops.Remaining(*shop, id)) + " left today; restocks at dawn",
                        dx, y, TextSize::Small, Palette::TextDim);
            } else {
                const int offer = Trade::SellPrice(*shop, items, *d);
                if (offer > 0) {
                    ui.Text("They pay " + std::to_string(offer) + " coins each", dx, y, TextSize::Small, Palette::Xp);
                    y += 20.0f;
                    // Where it would fetch more, so selling is a choice.
                    int best = offer;
                    string where;
                    for (const auto& kv : shop_db.All()) {
                        const int o = Trade::SellPrice(kv.second, items, *d);
                        if (o > best) { best = o; where = kv.second.name; }
                    }
                    if (!where.empty())
                        y += ui.TextWrapped(where + " would pay " + std::to_string(best) + ".",
                                            dx, y, dw, TextSize::Small, Palette::TextDim);
                } else {
                    ui.TextWrapped("This trader does not deal in that.", dx, y, dw, TextSize::Small,
                                   {225, 130, 120, 255});
                }
            }
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + (shop_tab == 0 ? " buy     " : " sell     ") +
            input.PromptFor(Action::Sprint) + " + " + input.PromptFor(Action::Confirm) +
            (shop_tab == 0 ? " buy 10     " : " sell all     ") +
            string(input.ActiveDevice() == InputMode::Controller ? "Left/Right" : "A/D") + " buy / sell     " +
            input.PromptFor(Action::Back) + " leave",
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
        if (guest_session) {
            // The host gets them up, in Havenbrook, and says so: the screen
            // stays until it has.
            world->visitor_acts.push_back({4, "", "", 0});
            return;
        }
        if (serving == 1) {
            // Player Two: the realm gets them up, in Havenbrook, on its next
            // step. Player One is wherever they were.
            net::Action up;
            up.kind = net::Action::Respawn;
            ServeSeat(0);
            coop_host.LocalAct(p2_seat, up, ctx);
            SetState(GameState::Play);
            PushToast(settings.p2_name + " wakes in Havenbrook, aching but alive.", Palette::TextDim);
            return;
        }
        // Respawn at the town, keeping progress, the way a forgiving RPG does.
        world->player.Respawn(0.0f, 0.0f);
        if (!world->LoadMap("town_havenbrook", "respawn", ctx))
            world->LoadMap("overworld", "start", ctx);
        world->player.Respawn(world->player.x, world->player.y);
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

// =============================================================================
//  Storage
//
//  A chest you keep things in, standing open beside your pack. Two grids, the
//  same squares as the inventory, and the button moves a stack from whichever
//  side the cursor is on to the other. Nothing is ever destroyed: a move that
//  will not fit moves what fits and says so.
// =============================================================================

namespace {
constexpr int kStorageCols = 10;   // a ten by ten chest
constexpr int kBagCols     = 7;    // the same shape as the inventory screen
}

void Game::UpdateStorage() {
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause) ||
        input.Pressed(Action::Inventory)) {
        ClosePanel();
        return;
    }

    Player& p = world->player;
    Inventory& chest = world->Storage(storage_id, storage_slots, &items);
    const int bag_slots = p.inventory.SlotCount();
    const int box_slots = chest.SlotCount();
    storage_bag_cursor = std::clamp(storage_bag_cursor, 0, bag_slots - 1);

    int& cursor = storage_on_chest ? storage_cursor : storage_bag_cursor;
    const int cols  = storage_on_chest ? kStorageCols : kBagCols;
    const int count = storage_on_chest ? box_slots : bag_slots;
    const int before = cursor;
    const bool side_before = storage_on_chest;

    if (input.MenuRight()) {
        // The right-hand edge of the pack steps across into the chest, and the
        // chest's right-hand edge stops: there is nothing further right.
        if (!storage_on_chest && cursor % cols == cols - 1) storage_on_chest = true;
        else cursor = std::min(cursor + 1, count - 1);
    }
    if (input.MenuLeft()) {
        if (storage_on_chest && cursor % cols == 0) storage_on_chest = false;
        else cursor = std::max(cursor - 1, 0);
    }
    if (input.MenuDown()) cursor = std::min(cursor + cols, count - 1);
    if (input.MenuUp())   cursor = std::max(cursor - cols, 0);

    // Whichever side the cursor lands on, keep it inside that side's grid.
    storage_cursor     = std::clamp(storage_cursor, 0, std::max(0, box_slots - 1));
    storage_bag_cursor = std::clamp(storage_bag_cursor, 0, std::max(0, bag_slots - 1));
    if (cursor != before || storage_on_chest != side_before) Audio::Play(Sfx::UiMove);

    // --- by the armful ---------------------------------------------------------
    // The drop button, which in a chest has nothing to drop. From the pack it
    // stows everything the chest already has some of -- the ore goes with the
    // ore -- or, with sprint held, everything but the purse and what cannot be
    // parted with. From the chest it takes the lot. Emptying a full pack into
    // a chest was fifty-six presses.
    if (input.Pressed(Action::Drop)) {
        const bool everything = input.Down(Action::Sprint);
        Inventory& from = storage_on_chest ? chest : p.inventory;
        Inventory& to   = storage_on_chest ? p.inventory : chest;
        int moved_total = 0, kinds = 0;
        bool no_room = false;
        for (int i = 0; i < from.SlotCount(); ++i) {
            const ItemStack stack = from.Slot(i);
            if (stack.Empty()) continue;
            if (!storage_on_chest) {
                const ItemDef* d = items.Get(stack.id);
                if (stack.id == "coins" || (d && d->keep)) continue;
                if (!everything && !to.Has(stack.id, 1)) continue;
            }
            const int moved = to.Add(stack.id, stack.qty);
            if (moved <= 0) { no_room = true; continue; }
            from.RemoveSlot(i, moved);
            moved_total += moved;
            ++kinds;
            if (moved < stack.qty) no_room = true;
        }
        if (moved_total > 0) {
            PushToast((storage_on_chest ? "Took " : "Stowed ") + std::to_string(moved_total) + " things, " +
                          std::to_string(kinds) + (kinds == 1 ? " kind" : " kinds") +
                          (no_room ? "  -  no room for the rest" : ""),
                      no_room ? SDL_Color{235, 200, 120, 255} : Palette::Text);
            Audio::Play(Sfx::Pickup, 0.7f);
            quests->RefreshCollectObjectives(p.inventory);
        } else {
            PushToast(no_room ? (storage_on_chest ? "Your pack is full." : "The chest is full.")
                      : storage_on_chest ? "The chest is empty."
                      : everything ? "There is nothing to stow."
                                   : "Nothing you carry is already in here. Hold " +
                                         input.PromptFor(Action::Sprint) + " to stow everything.",
                      Palette::TextDim);
            Audio::Play(Sfx::UiError);
        }
        return;
    }

    if (!(input.Pressed(Action::Confirm) || input.Pressed(Action::Interact))) return;

    // One at a time, or the whole stack with sprint held: the same hand as the
    // shop screen, so the two panels are not opposites of each other.
    const bool all = input.Down(Action::Sprint);
    Inventory& from = storage_on_chest ? chest : p.inventory;
    Inventory& to   = storage_on_chest ? p.inventory : chest;
    const int slot  = storage_on_chest ? storage_cursor : storage_bag_cursor;

    const ItemStack stack = from.Slot(slot);
    if (stack.Empty()) { Audio::Play(Sfx::UiError); return; }

    const int want = all ? stack.qty : 1;
    const int moved = to.Add(stack.id, want);
    if (moved <= 0) {
        PushToast(storage_on_chest ? "Your pack is full." : "The chest is full.",
                  {235, 150, 120, 255});
        Audio::Play(Sfx::UiError);
        return;
    }
    from.RemoveSlot(slot, moved);

    const ItemDef* d = items.Get(stack.id);
    const string name = d ? d->name : stack.id;
    PushToast((storage_on_chest ? "Took " : "Stored ") + std::to_string(moved) + "x " + name +
                  (moved < want ? "  -  no room for the rest" : ""),
              moved < want ? SDL_Color{235, 200, 120, 255} : Palette::Text);
    Audio::Play(Sfx::Pickup, 0.7f);
    quests->RefreshCollectObjectives(p.inventory);
}

void Game::DrawStorage() {
    ui.Dim(0.5f);
    Player& p = world->player;
    const Inventory& chest = world->Storage(storage_id, storage_slots, &items);

    const float cell = 34.0f, gap = 4.0f;
    const float bag_w   = kBagCols * (cell + gap) - gap;
    const float box_w   = kStorageCols * (cell + gap) - gap;
    const int   box_rows = (chest.SlotCount() + kStorageCols - 1) / kStorageCols;
    const int   bag_rows = (p.inventory.SlotCount() + kBagCols - 1) / kBagCols;
    const float grid_h  = std::max(box_rows, bag_rows) * (cell + gap) - gap;

    const SDL_FRect panel = CenteredPanel(ui, bag_w + box_w + 96.0f, grid_h + 168.0f);
    ui.Panel(panel);
    ui.Text(storage_title, panel.x + 24.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight);

    const float grid_y = panel.y + 76.0f;
    const float bag_x  = panel.x + 24.0f;
    const float box_x  = bag_x + bag_w + 48.0f;

    // One square, drawn the same on either side so a stack does not change
    // appearance when it crosses over.
    SDL_FRect lit{0, 0, 0, 0};
    const auto square = [&](const Inventory& inv, int i, float x, float y, bool selected) {
        const SDL_FRect r = {x, y, cell, cell};
        if (selected) lit = r;
        ui.Fill(r, {34, 27, 22, 235});
        ui.Outline(r, selected ? Palette::Highlight : Palette::BorderDim, selected ? 2.0f : 1.0f);
        const ItemStack& s = inv.Slot(i);
        if (s.Empty()) return;
        const ItemDef* def = items.Get(s.id);
        SDL_Texture* tex = (def && !def->icon.empty()) ? textures->Get(def->icon) : nullptr;
        const SDL_FRect inner = {r.x + 4.0f, r.y + 4.0f, r.w - 8.0f, r.h - 8.0f};
        if (tex) SDL_RenderTexture(renderer, tex, nullptr, &inner);
        else     DrawItemPlaceholder(ui, def, s.id, inner);
        if (s.qty > 1)
            ui.TextShadowed(std::to_string(s.qty), r.x + r.w - 2.0f, r.y + r.h - 15.0f,
                            TextSize::Small, Palette::Highlight, Align::Right);
    };

    char head[96];
    SDL_snprintf(head, sizeof(head), "Your pack   %d / %d",
                 p.inventory.SlotCount() - p.inventory.FreeSlots(), p.inventory.SlotCount());
    ui.Text(head, bag_x, panel.y + 52.0f, TextSize::Small, Palette::TextDim);
    for (int i = 0; i < p.inventory.SlotCount(); ++i)
        square(p.inventory, i, bag_x + (i % kBagCols) * (cell + gap),
               grid_y + (i / kBagCols) * (cell + gap),
               !storage_on_chest && i == storage_bag_cursor);

    SDL_snprintf(head, sizeof(head), "The chest   %d / %d",
                 chest.SlotCount() - chest.FreeSlots(), chest.SlotCount());
    ui.Text(head, box_x, panel.y + 52.0f, TextSize::Small, Palette::TextDim);
    for (int i = 0; i < chest.SlotCount(); ++i)
        square(chest, i, box_x + (i % kStorageCols) * (cell + gap),
               grid_y + (i / kStorageCols) * (cell + gap),
               storage_on_chest && i == storage_cursor);

    // What is under the cursor, and how to move it.
    const ItemStack& sel = storage_on_chest ? chest.Slot(storage_cursor)
                                            : p.inventory.Slot(storage_bag_cursor);
    const float foot_y = grid_y + grid_h + 16.0f;
    if (const ItemDef* def = sel.Empty() ? nullptr : items.Get(sel.id)) {
        ui.Text(def->name, bag_x, foot_y, TextSize::Body, Palette::Highlight);
        // Wrapped to the panel: a bag's description is four lines of prose and
        // went off the side of the screen.
        ui.TextWrapped(def->description, bag_x, foot_y + 20.0f, panel.w - 48.0f, TextSize::Small, Palette::TextDim);
    }
    ui.Text(input.PromptFor(Action::Confirm) + " move one   -   hold " +
                input.PromptFor(Action::Sprint) + " for the stack   -   " +
                input.PromptFor(Action::Drop) + (storage_on_chest ? " take all" : " stow alike") + "   -   " +
                input.PromptFor(Action::Back) + " close",
            panel.x + panel.w - 24.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // The card last, over the top of the grids: moving armour between a chest
    // and a pack is the other time you want to know which of two coifs is the
    // better one.
    if (lit.w > 0.0f && !sel.Empty())
        if (const ItemDef* def = items.Get(sel.id)) DrawItemCard(*def, lit);
}
