#include "../game.h"
#include "screens_shared.h"

// =============================================================================
//  The looks screen: the colour of their hair, their skin and their clothes.
//
//  Between the calling and the first step -- after the character select and
//  before the slot, the door, the friend's world, or Player Two's seat. The
//  figure stands on the character panel's stage and is drawn the way the world
//  will draw it (Sprite::DrawLayers, recoloured through the rig's DyeTable), as
//  they are or in what they set out in, turned any of the eight ways they can
//  be drawn. Each part offers the calling's own colour first and then its
//  swatches (LookSwatches); left and right step through them, up and down
//  between the parts.
// =============================================================================

namespace {

// The eight ways round, in turning order: front, its right, side, back and on.
// Each is the facing and the row of an eight-row clip (Sprite::Row): down,
// left, right and up, then down-left, down-right, up-left and up-right.
struct Turn { Facing facing; int row; };
constexpr Turn kTurns[8] = {
    {FACE_DOWN, 0}, {FACE_RIGHT, 5}, {FACE_RIGHT, 2}, {FACE_RIGHT, 7},
    {FACE_UP, 3},   {FACE_LEFT, 6},  {FACE_LEFT, 1},  {FACE_LEFT, 4},
};

int SwatchCount(int part) { return static_cast<int>(LookSwatches(part).size()) + 1; }

SDL_Color Rgb(uint32_t rgb) {
    return {static_cast<Uint8>((rgb >> 16) & 255), static_cast<Uint8>((rgb >> 8) & 255),
            static_cast<Uint8>(rgb & 255), 255};
}

}  // namespace

void Game::OpenLooks(LooksFor why, const string& character) {
    looks_for = why;
    looks_character = character;
    looks_edit = why == LooksFor::PlayerTwo ? p2_looks : pending_looks;
    looks_row = 0;
    looks_turn = 0;
    looks_gear = false;
    SetState(GameState::CharacterLooks);
}

void Game::UpdateCharacterLooks() {
    if (input.MenuUp() || input.MenuDown()) {
        looks_row = (looks_row + (input.MenuDown() ? 1 : LOOK_PARTS - 1)) % LOOK_PARTS;
        Audio::Play(Sfx::UiMove);
    }
    if (input.MenuLeft() || input.MenuRight()) {
        const int n = SwatchCount(looks_row);
        const int at = LookSwatchIndex(looks_edit, looks_row);
        SetLookSwatch(looks_edit, looks_row, (at + (input.MenuRight() ? 1 : n - 1)) % n);
        Audio::Play(Sfx::UiMove);
    }
    // Turning round: the keys and the stick the character panel turns with.
    if (input.Pressed(Action::SpellNext) || input.Pressed(Action::SpellPrev)) {
        looks_turn = (looks_turn + (input.Pressed(Action::SpellNext) ? 1 : 7)) % 8;
        Audio::Play(Sfx::UiMove);
    }
    // In what they set out in, or as they are. They start the prologue with
    // nothing, so "as they are" is how they are first seen. Not the attacks'
    // buttons: in a menu the keyboard's attack keys confirm and back out.
    if (input.Pressed(Action::Inventory)) {
        looks_gear = !looks_gear;
        Audio::Play(Sfx::UiMove);
    }
    // Anything at all, the calling's own included: the bag's drop key (G, or Y).
    if (input.Pressed(Action::Drop)) {
        for (int p = 0; p < LOOK_PARTS; ++p) SetLookSwatch(looks_edit, p, static_cast<int>(SDL_rand(SwatchCount(p))));
        Audio::Play(Sfx::UiMove);
    }

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        switch (looks_for) {
            case LooksFor::NewGame:
                pending_looks = looks_edit;
                slot_purpose = SLOT_NEW;
                SetState(GameState::SlotSelect);
                break;
            case LooksFor::HostNew:
                // A new world to host, in the empty slot chosen for it: the
                // door opens as it begins.
                pending_looks = looks_edit;
                hosting_new = false;
                NewGame(pending_character, pending_slot);
                if (has_session && !StartHosting(mp_port)) PushToast(mp_error, {235, 150, 120, 255});
                break;
            case LooksFor::Guest:
                pending_looks = looks_edit;
                BackToLookRow();
                break;
            case LooksFor::PlayerTwo:
                p2_looks = looks_edit;
                if (JoinSplit(looks_p2_padless, false)) ServeSeat(0);
                SetState(GameState::Play);
                break;
        }
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        switch (looks_for) {
            case LooksFor::NewGame:
            case LooksFor::HostNew:
                // Back to the callings, on the one that was chosen; the colours
                // are kept for whichever is chosen next.
                pending_looks = looks_edit;
                SetState(GameState::CharacterSelect);
                for (int i = 0; i < kCharacterCount; ++i)
                    if (looks_character == kCharacterIds[i]) cursor = i;
                break;
            case LooksFor::Guest:
                BackToLookRow();
                break;
            case LooksFor::PlayerTwo:
                SetState(GameState::Paused);
                cursor = 3;      // the Player Two row
                break;
        }
    }
}

void Game::DrawCharacterLooks() {
    if (has_session) ui.Dim(0.55f);
    const float cx = ui.ViewWidth() / 2.0f;
    const Player::Calling calling = Player::CallingFor(looks_character);
    string calling_name = looks_character;
    for (int i = 0; i < kCharacterCount; ++i)
        if (looks_character == kCharacterIds[i]) calling_name = kCharacterLabels[i];
    const string heading = looks_for == LooksFor::PlayerTwo
        ? (settings.p2_name.empty() ? string("Player Two") : settings.p2_name) + "'s colours"
        : string("Their colours");
    ui.TextShadowed(heading, cx, ui.ViewHeight() * 0.06f, TextSize::Large, Palette::Text, Align::Center);
    ui.TextShadowed(calling_name, cx, ui.ViewHeight() * 0.06f + 40.0f, TextSize::Body, CallingColour(calling),
                    Align::Center);

    const float panel_w = std::min(920.0f, ui.ViewWidth() - 32.0f);
    const float panel_h = std::min(470.0f, ui.ViewHeight() - ui.ViewHeight() * 0.06f - 150.0f);
    const SDL_FRect panel = {cx - panel_w / 2.0f, ui.ViewHeight() * 0.06f + 78.0f, panel_w, panel_h};
    ui.Panel(panel);

    // --- the stage: as the character panel's -------------------------------------
    const SDL_FRect stage = {panel.x + 16.0f, panel.y + 16.0f, std::floor(panel.w * 0.38f), panel.h - 32.0f};
    Gradient(renderer, stage, {20, 17, 19, 245}, {50, 42, 35, 245});
    ui.Outline(stage, Palette::BorderDim, 1.0f);
    const float floor_y = stage.y + stage.h * 0.86f;
    Glow(renderer, stage.x + stage.w / 2.0f, stage.y + stage.h * 0.50f, stage.w * 0.48f, stage.h * 0.48f, {150, 130, 108, 58});
    Glow(renderer, stage.x + stage.w / 2.0f, floor_y, stage.w * 0.40f, stage.h * 0.06f, {190, 164, 120, 110});

    const SpriteDef* def = sprites.Get(looks_character);
    if (def) {
        Sprite fig;
        fig.SetDef(def);
        if (looks_gear) {
            fig.style = Player::KitStyle(looks_character, &items, looks_edit);
        } else {
            // Nothing worn and nothing in hand: the prologue's first sight of them.
            fig.style = LayerStyle();
            fig.style.show_weapon = false;
            fig.style.looks = looks_edit;
        }
        const Turn& t = kTurns[((looks_turn % 8) + 8) % 8];
        fig.facing = t.facing;
        fig.heading = t.row;
        fig.heading_for = t.facing;
        fig.Play("idle");
        fig.Update(state_time);
        // A whole multiple of the art, as big as the stage takes: the rig is
        // about 28 pixels tall in its 64-pixel frame.
        constexpr float FIGURE_PIXELS = 28.0f;
        const float frame = static_cast<float>(std::max(1, fig.FrameSize()));
        const float scale = std::clamp(std::floor(stage.h * 0.62f / FIGURE_PIXELS), 3.0f, 10.0f);
        const float side = frame * scale;
        const SDL_FRect dst = {std::floor(stage.x + (stage.w - side) / 2.0f),
                               std::floor(floor_y - (def->anchor_y - 4.0f) * scale), side, side};
        const SDL_Rect clip = {static_cast<int>(stage.x) + 1, static_cast<int>(stage.y) + 1,
                               static_cast<int>(stage.w) - 2, static_cast<int>(stage.h) - 2};
        SDL_SetRenderClipRect(renderer, &clip);
        fig.DrawAt(renderer, *textures, dst);
        SDL_SetRenderClipRect(renderer, nullptr);
    }
    const SDL_Color hint = {150, 138, 118, 255};
    ui.Text(input.PromptFor(Action::SpellPrev) + " " + input.PromptFor(Action::SpellNext) + " turn",
            stage.x + stage.w - 8.0f, stage.y + stage.h - 22.0f, TextSize::Small, hint, Align::Right);
    ui.Text(input.PromptFor(Action::Inventory) + (looks_gear ? " as they are" : " in their kit"),
            stage.x + 8.0f, stage.y + stage.h - 22.0f, TextSize::Small, hint);
    ui.Text(looks_gear ? "In what they set out in" : "As they first set out",
            stage.x + stage.w / 2.0f, stage.y + 10.0f, TextSize::Small, Palette::TextDim, Align::Center);

    // --- the parts -----------------------------------------------------------------
    const float col_x = stage.x + stage.w + 24.0f;
    const float col_w = panel.x + panel.w - 16.0f - col_x;
    const float foot_h = 40.0f;
    const float row_h = (panel.h - 32.0f - foot_h) / static_cast<float>(LOOK_PARTS);
    // As big as the rows take: a small window has short rows, and the longest
    // list has to fit in its lines under the part's name.
    float sq = 26.0f, gap = 7.0f;
    int per_line = 1;
    int most = 1;
    for (int part = 0; part < LOOK_PARTS; ++part) most = std::max(most, SwatchCount(part));
    for (;;) {
        per_line = std::max(1, static_cast<int>((col_w - 24.0f + gap) / (sq + gap)));
        const int lines = (most + per_line - 1) / per_line;
        if (40.0f + lines * (sq + gap) <= row_h - 12.0f || sq <= 14.0f) break;
        sq -= 2.0f;
        gap = std::max(4.0f, gap - 0.5f);
    }
    const float pulse = 0.5f + 0.5f * std::sin(state_time * 4.0f);

    for (int part = 0; part < LOOK_PARTS; ++part) {
        const SDL_FRect row = {col_x, panel.y + 16.0f + part * row_h, col_w, row_h - 8.0f};
        const bool on = part == looks_row;
        ui.Fill(row, on ? Palette::PanelLight : SDL_Color{32, 25, 21, 220});
        ui.Outline(row, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);

        const int chosen = LookSwatchIndex(looks_edit, part);
        const vector<LookSwatch>& swatches = LookSwatches(part);
        const string chosen_name = chosen == 0 ? "Their own" : swatches[static_cast<size_t>(chosen - 1)].name;
        ui.Text(LookPartName(part), row.x + 12.0f, row.y + 9.0f, TextSize::Body, on ? Palette::Highlight : Palette::Text);
        ui.Text(chosen_name, row.x + row.w - 12.0f, row.y + 11.0f, TextSize::Small,
                on ? Palette::Text : Palette::TextDim, Align::Right);

        const float grid_y = row.y + 40.0f;
        for (int i = 0; i < SwatchCount(part); ++i) {
            const int line = i / per_line, at = i % per_line;
            const SDL_FRect box = {row.x + 12.0f + at * (sq + gap), grid_y + line * (sq + gap), sq, sq};
            const uint32_t rgb = i == 0 ? (def && def->dyes ? def->dyes->Own(part) : 0x808080u)
                                        : swatches[static_cast<size_t>(i - 1)].rgb;
            ui.Fill(box, Rgb(rgb));
            ui.Outline(box, {20, 16, 14, 255}, 1.0f);
            // The calling's own: a corner turned down, the way a page is marked.
            if (i == 0) {
                ui.Fill({box.x + 2.0f, box.y + 2.0f, 7.0f, 3.0f}, Palette::Text);
                ui.Fill({box.x + 2.0f, box.y + 2.0f, 3.0f, 7.0f}, Palette::Text);
            }
            if (i == chosen) {
                const SDL_FRect ring = {box.x - 3.0f, box.y - 3.0f, box.w + 6.0f, box.h + 6.0f};
                SDL_Color c = on ? Palette::Highlight : Palette::Text;
                if (on) c.a = static_cast<Uint8>(160 + 95 * pulse);
                ui.Outline(ring, c, 2.0f);
            }
        }
    }

    // --- the foot of the column ------------------------------------------------------
    const char* go = looks_for == LooksFor::NewGame   ? " choose a slot"
                   : looks_for == LooksFor::HostNew   ? " begin"
                   : looks_for == LooksFor::Guest     ? " done"
                                                      : " join";
    const float foot_y = panel.y + panel.h - 16.0f - foot_h + 10.0f;
    ui.Text(input.PromptFor(Action::Drop) + " any colours", col_x + 4.0f, foot_y, TextSize::Small, hint);
    ui.Text(input.PromptFor(Action::Confirm) + go, col_x + col_w - 4.0f, foot_y, TextSize::Small, Palette::Highlight,
            Align::Right);

    ui.TextShadowed("Up / Down: hair, skin, clothes   Left / Right: colour   " +
                    input.PromptFor(Action::Back) + " back",
                    cx, ui.ViewHeight() - 40.0f, TextSize::Small, {186, 176, 158, 255}, Align::Center);
}
