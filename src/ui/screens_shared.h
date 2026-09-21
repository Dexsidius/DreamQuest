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

inline SDL_FRect CenteredPanel(const UI& ui, float w, float h) {
    return {(ui.ViewWidth() - w) / 2.0f, (ui.ViewHeight() - h) / 2.0f, w, h};
}
