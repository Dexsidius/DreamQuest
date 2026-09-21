#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  The elements. They were in projectile.h, which is where they were first
//  wanted; statuses want them too, and a projectile wants statuses.
// -----------------------------------------------------------------------------

// Arcane is the ancient magic's school, taught at the college in Fernhollow:
// outside the four elements' cycle, so it neither beats nor is beaten by any
// of them, and untyped creatures take it as they take anything.
enum class Element { None = 0, Fire, Water, Earth, Air, Arcane, COUNT };

const char* ElementName(Element e);
Element     ElementFromName(const string& name);
SDL_Color   ElementColor(Element e);

// The cycle is Water over Fire over Earth over Air over Water: water douses
// fire, fire scorches earth, earth smothers air, air disperses water.
Element ElementBeats(Element e);

// Damage multiplier for attacker's element against defender's.
//   1.60  the attacker's element beats the defender's
//   0.60  the defender's element beats the attacker's
//   0.75  same element, which resists itself
//   1.00  anything else, including untyped
float ElementMultiplier(Element attacker, Element defender);
