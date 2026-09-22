#pragma once
#include "../headers.h"
#include "projectile.h"

// -----------------------------------------------------------------------------
//  Spells.
//
//  Four elements, three tiers each, gated on the Magic skill exactly the way
//  weapon requirements are gated on Attack. The player picks an element rather
//  than a spell; the book hands back the strongest spell of that element they
//  are actually high enough level to cast, so levelling Magic upgrades what
//  the same button does instead of adding another thing to remember.
//
//  Casting costs mana, which comes from the Magic level and refills over time.
// -----------------------------------------------------------------------------

struct SpellDef {
    string  id, name, description;
    Element element = Element::Fire;
    int     tier = 1;
    int     level = 1;          // Magic level required
    int     mana = 4;
    float   damage_mult = 1.0f; // multiplies the magic max hit
    int     xp = 10;            // Magic XP per cast that connects
    string  projectile;         // key into data/projectiles.json
    // The ancient magic: "arcane" spells are learned one by one -- the
    // magister teaches the first, tomes the rest -- and chosen by name rather
    // than by element. Each has a shape: "bolt" (one, at the target), "darts"
    // (three that seek), "rays" (three in a fan), "rain" (a strike from above
    // on the target), "ring" (eight, all round the caster). Elemental spells
    // are all bolts.
    bool    arcane = false;
    // Which of an element's four places it has: 1 is the bolt every staff
    // throws, and 2 to 4 are what that element's own staff adds. See
    // ItemDef::element.
    int     slot = 1;
    string  shape = "bolt";
    string  taught_by;          // where it is learned, for the panel to say
};

class SpellBook {
public:
    bool Load(const string& path);

    const SpellDef* Get(const string& id) const;
    // The strongest spell of this element the given Magic level can cast.
    const SpellDef* BestFor(Element e, int magic_level) const;
    // The next tier up, for the "unlocks at" line in the UI.
    const SpellDef* NextFor(Element e, int magic_level) const;
    // What an element actually casts. Left alone that is the strongest; the
    // spellbook's page can hold an element to a lesser spell -- an Ember for
    // four mana where a Pyre is nine -- and `held` is that spell's id. One that
    // is not this element's, or is out of the Magic level's reach, is ignored.
    const SpellDef* Chosen(Element e, int magic_level, const string& held) const;
    // Every spell of an element, weakest first.
    vector<const SpellDef*> Of(Element e) const;
    // The strongest spell on one of an element's four slots that this Magic
    // level can cast, and -- whether or not it can -- the first there is.
    const SpellDef* ForSlot(Element e, int slot, int magic_level) const;
    const SpellDef* FirstOnSlot(Element e, int slot) const;
    // What a weapon offers for an element: the best castable spell on each of
    // the slots that weapon reaches, weakest first. See ItemDef::spell_slots.
    vector<const SpellDef*> ForWeapon(Element e, const vector<int>& slots, int magic_level) const;
    // And which of those it casts: the one held to, where that is one of them
    // and within reach, and otherwise the strongest of the element's first --
    // so widening what a weapon offers never quietly changes what it throws.
    const SpellDef* ChosenFor(Element e, const vector<int>& slots, int magic_level, const string& held) const;

    const map<string, SpellDef>& All() const { return defs; }
    // The ancient spells, in the order they are learned.
    vector<const SpellDef*> Arcane() const;

    // Mana scales with Magic so a caster gets more casts as well as bigger
    // ones; a non-caster still has a small pool for the first tier.
    static int MaxMana(int magic_level) { return 12 + magic_level * 2; }
    static float RegenPerSecond(int magic_level) { return 0.6f + magic_level * 0.05f; }

private:
    map<string, SpellDef> defs;
};
