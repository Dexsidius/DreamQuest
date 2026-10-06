#pragma once
#include "../game.h"

// -----------------------------------------------------------------------------
//  What more than one of the Game class's screen files uses. The screens were
//  one file, and these were in the unnamed namespace at the top of it.
// -----------------------------------------------------------------------------

// The button an ability slot is on, with the guard held: light, heavy, lock on.
inline Action AbilityButton(int slot) {
    return slot == 0 ? Action::LightAttack : slot == 1 ? Action::StrongAttack : Action::Target;
}

// Each calling's colour: the bow's green, the staff's violet, the blade's
// ember and the lantern's own gold. The select screen's cards, the character
// panel's title, and a reward card that names whose it is.
inline SDL_Color CallingColour(Player::Calling c) {
    switch (c) {
        case Player::Calling::Ranger: return {150, 210, 130, 255};
        case Player::Calling::Weaver: return {170, 150, 240, 255};
        case Player::Calling::Warden: return {246, 204, 110, 255};
        default:                      return {236, 160, 96, 255};
    }
}

inline SDL_FRect CenteredPanel(const UI& ui, float w, float h) {
    return {(ui.ViewWidth() - w) / 2.0f, (ui.ViewHeight() - h) / 2.0f, w, h};
}

// Stand-in tile for an item whose icon art has not been imported: a coloured
// swatch keyed off the item id, with its initial on top. Keeps the inventory
// readable instead of showing empty squares. The bag's, and the reward panel's.
inline void DrawItemPlaceholder(UI& ui, const ItemDef* def, const string& id,
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
