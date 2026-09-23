#include "../systems/gathering.h"
#include "../game.h"
#include "screens_shared.h"

// =============================================================================
//  Shared helpers
// =============================================================================

namespace {

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
    OPT_INPUT, OPT_CONTROLS, OPT_EFFECTS, OPT_ZOOM, OPT_UI_SCALE, OPT_FULLSCREEN, OPT_VSYNC, OPT_FPS, OPT_DAMAGE, OPT_XP,
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
            case OPT_EFFECTS:
                if (confirm) {
                    SetState(GameState::VisualEffects);
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
    // Fifteen rows now, so they are a little shorter, and shorter again in a
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
        {"Visual Effects", string(Shaders::Enabled() ? (settings.visual_effects ? "On" : "Off") : "no Vulkan") + "  >"},
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
//  Visual Effects: the shaders, and the parts of them that can be turned off
// =============================================================================
//
// The first row is all of it: off, the game draws as it did before it had any
// shaders at all. The rest are the parts some people would rather not have --
// the screen moving under them, lights flashing at them, colours coming apart
// -- each on its own, and each greyed out while the whole is off.

namespace {
enum FxRow { FX_ALL, FX_SHAKE, FX_FLASHES, FX_FRINGING, FX_DISTORTION, FX_BACK, FX_ROWS };
}

void Game::ApplyVisualEffects() {
    Shaders::Options o;
    o.effects = settings.visual_effects;
    o.shake = settings.screen_shake;
    o.flashes = settings.flashes;
    o.fringing = settings.colour_fringing;
    o.distortion = settings.screen_distortion;
    Shaders::SetOptions(o);
}

void Game::UpdateVisualEffects() {
    MoveCursor(cursor, FX_ROWS);
    const bool confirm = input.Pressed(Action::Confirm) || input.Pressed(Action::Interact);
    const bool change = confirm || input.MenuRight() || input.MenuLeft();
    if (change) {
        bool* flag = nullptr;
        switch (cursor) {
            case FX_ALL:        flag = &settings.visual_effects; break;
            case FX_SHAKE:      flag = &settings.screen_shake; break;
            case FX_FLASHES:    flag = &settings.flashes; break;
            case FX_FRINGING:   flag = &settings.colour_fringing; break;
            case FX_DISTORTION: flag = &settings.screen_distortion; break;
            default: break;
        }
        if (flag) {
            *flag = !*flag;
            ApplyVisualEffects();
            settings.Save();
            Audio::Play(Sfx::UiConfirm, 0.7f);
        } else if (confirm) {
            SetState(GameState::Options);
            cursor = OPT_EFFECTS;
            return;
        }
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Options);
        cursor = OPT_EFFECTS;
    }
}

void Game::DrawVisualEffects() {
    ui.Dim(0.55f);
    const float row_h = 40.0f;
    const SDL_FRect panel = CenteredPanel(ui, 600.0f, 170.0f + static_cast<float>(FX_ROWS) * row_h + 60.0f);
    ui.Panel(panel);
    ui.Text("Visual Effects", panel.x + panel.w / 2.0f, panel.y + 18.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const bool gpu = Shaders::Enabled();
    const bool all = settings.visual_effects && gpu;
    const auto onoff = [](bool v) { return string(v ? "On" : "Off"); };
    const pair<string, string> rows[FX_ROWS] = {
        {"Visual Effects",    gpu ? onoff(settings.visual_effects) : string("needs Vulkan")},
        {"Screen Shake",      onoff(settings.screen_shake)},
        {"Flashes",           onoff(settings.flashes)},
        {"Colour Fringing",   onoff(settings.colour_fringing)},
        {"Screen Distortion", onoff(settings.screen_distortion)},
        {"Back",              ""},
    };
    // What each row does, under the list, for the one the cursor is on.
    static const char* kWhat[FX_ROWS] = {
        "Wind in the grass, water that ripples, lit windows, fog, glows and the rest. Off draws the plain look.",
        "The screen shakes when a meteor lands, a slab is dropped or a boss's blow strikes the ground.",
        "Struck things flash white or the colour of the spell; the screen flashes with lightning.",
        "Colours come apart a little at the edges in a dream and in a shockwave.",
        "Heat over lava and fires, shockwaves and the dream's edges bend the picture.",
        "",
    };
    for (int i = 0; i < FX_ROWS; ++i) {
        const SDL_FRect row = {panel.x + 16.0f, panel.y + 62.0f + i * row_h, panel.w - 32.0f, row_h - 4.0f};
        const bool enabled = i == FX_ALL ? gpu : i == FX_BACK ? true : all;
        ui.MenuItem(row, rows[i].first, i == cursor, enabled, rows[i].second);
    }
    const float note_y = panel.y + 62.0f + static_cast<float>(FX_ROWS) * row_h + 14.0f;
    if (!gpu)
        ui.Text("The GPU renderer could not be started, so the game is drawn plain.", panel.x + panel.w / 2.0f,
                note_y, TextSize::Small, Palette::TextDim, Align::Center);
    else if (cursor >= 0 && cursor < FX_ROWS && kWhat[cursor][0])
        ui.TextWrapped(kWhat[cursor], panel.x + 30.0f, note_y, panel.w - 60.0f, TextSize::Small, Palette::TextDim);
    ui.Text("Left / Right to change     " + input.PromptFor(Action::Back) + " back",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small, Palette::TextDim, Align::Center);
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
//  The menu of menus
//
//  Every panel had a button of its own, and a pad ran out: the abilities wanted
//  RB, which was the skills panel's. So Select (Tab, on the keys) opens one
//  list with all of them on it, and the panels that lost their button are
//  here. The keys still have I, O, P and M as well.
// =============================================================================

namespace {
struct HubEntry { const char* name; const char* what; GameState opens; int tab; Action own; };
}

void Game::UpdateHub() {
    static const HubEntry kEntries[5] = {
        {"Inventory", "", GameState::Inventory, -1, Action::Inventory},
        {"Skills", "", GameState::SkillsPanel, TAB_SKILLS, Action::Skills},
        {"Spellbook", "", GameState::SkillsPanel, TAB_BOOK, Action::COUNT},
        {"Quests", "", GameState::QuestPanel, -1, Action::QuestLog},
        {"Map", "", GameState::WorldMapPage, -1, Action::WorldMap},
    };
    const int was = hub_cursor;
    if (input.MenuUp())   hub_cursor = (hub_cursor + 4) % 5;
    if (input.MenuDown()) hub_cursor = (hub_cursor + 1) % 5;
    if (was != hub_cursor) Audio::Play(Sfx::UiMove);
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause) || (state_time > 0.0f && input.Pressed(Action::Menu))) {
        SetState(GameState::Play);
        return;
    }
    if (input.Pressed(Action::Confirm)) {
        const HubEntry& e = kEntries[std::clamp(hub_cursor, 0, 4)];
        Audio::Play(Sfx::UiMove);
        // Opened from the game, not from here: closing it goes back to the game.
        return_state = GameState::Play;
        SetState(e.opens);
        if (e.tab >= 0) { skills_tab = e.tab; book_row = 0; }
    }
}

void Game::DrawHub() {
    ui.Dim(0.5f);
    const Player& p = world->player;
    const AttackStyle path = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const int free = p.talents.PointsFree(path, p.skills);
    const size_t quests_on = quests ? quests->Active().size() : 0;
    const string names[5] = {"Inventory", "Skills", path == AttackStyle::Magic ? "Spellbook" : "Abilities", "Quests", "Map"};
    const string notes[5] = {
        std::to_string(p.inventory.Coins()) + " coins",
        free > 0 ? std::to_string(free) + (free == 1 ? " point to spend" : " points to spend") : "levels, and the " + skill_trees.Tree(path).name + " tree",
        "what every button does",
        quests_on ? std::to_string(quests_on) + " in hand" : string("nothing in hand"),
        world->CurrentMap().DisplayName(),
    };
    const Action own[5] = {Action::Inventory, Action::Skills, Action::COUNT, Action::QuestLog, Action::WorldMap};

    const float row_h = 46.0f;
    const SDL_FRect panel = CenteredPanel(ui, std::min(420.0f, ui.ViewWidth() - 16.0f),
                                          std::min(64.0f + row_h * 5 + 44.0f, ui.ViewHeight() - 16.0f));
    ui.Panel(panel);
    ui.Text("Menu", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Title, Palette::Highlight, Align::Center);
    const float step = std::min(row_h, (panel.h - 64.0f - 44.0f) / 5.0f);
    for (int i = 0; i < 5; ++i) {
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 64.0f + i * step, panel.w - 40.0f, step - 6.0f};
        const bool on = i == hub_cursor;
        ui.Fill(row, on ? SDL_Color{58, 46, 28, 220} : SDL_Color{30, 24, 20, 190});
        ui.Outline(row, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
        const float ty = row.y + (row.h - 20.0f) / 2.0f;
        ui.Text(names[i], row.x + 14.0f, ty, TextSize::Body, on ? Palette::Highlight : Palette::Text);
        // Its own key, where it still has one: nothing on a pad, for the two
        // that gave their buttons up.
        const bool keyed = own[i] != Action::COUNT &&
                           (input.ActiveDevice() != InputMode::Controller || input.GetBindings().buttons.count(own[i]));
        const string right = notes[i] + (keyed ? "   [" + input.PromptFor(own[i]) + "]" : string());
        ui.Text(right, row.x + row.w - 12.0f, ty + 3.0f, TextSize::Small,
                (i == 1 && free > 0) ? Palette::Xp : Palette::TextDim, Align::Right);
    }
    ui.Text(input.PromptFor(Action::Confirm) + " open     " + input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
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
