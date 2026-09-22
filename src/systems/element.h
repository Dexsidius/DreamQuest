#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  The elements. They were in projectile.h, which is where they were first
//  wanted; statuses want them too, and a projectile wants statuses.
// -----------------------------------------------------------------------------

// Arcane is the ancient magic's school, taught at the college in Fernhollow:
// outside the four elements' cycle, so it neither beats nor is beaten by any
// of them, and untyped creatures take it as they take anything.
//
// Electric stands outside the cycle too, and for the same reason: the cycle is
// four long and a fifth thing cannot be put in it without changing what every
// one of the four already does. What electricity answers to instead is water
// on the target rather than water in the cycle -- anything soaked takes it a
// quarter harder and is twice as easy to leave arcing. That lives in
// data/statuses.json, on the soaking, where the rest of the statuses' business
// lives: see StatusDef::weak_to and StatusDef::invites.
//
// It goes before Arcane so that the five keys read 1-5 elements, 6 the ancient
// magic. Nothing writes an element's number to disk or to the wire by itself --
// saves and data files use the name -- so the renumbering costs nothing.
enum class Element { None = 0, Fire, Water, Earth, Air, Electric, Arcane, COUNT };
// The elements a player selects, in the order their keys stand: everything
// from Fire to Arcane.
static constexpr int FIRST_ELEMENT = static_cast<int>(Element::Fire);
static constexpr int LAST_ELEMENT  = static_cast<int>(Element::Arcane);
static constexpr int SELECTABLE_ELEMENTS = LAST_ELEMENT - FIRST_ELEMENT + 1;

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
