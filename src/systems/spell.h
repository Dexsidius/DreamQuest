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
};

class SpellBook {
public:
    bool Load(const string& path);

    const SpellDef* Get(const string& id) const;
    // The strongest spell of this element the given Magic level can cast.
    const SpellDef* BestFor(Element e, int magic_level) const;
    // The next tier up, for the "unlocks at" line in the UI.
    const SpellDef* NextFor(Element e, int magic_level) const;

    const map<string, SpellDef>& All() const { return defs; }

    // Mana scales with Magic so a caster gets more casts as well as bigger
    // ones; a non-caster still has a small pool for the first tier.
    static int MaxMana(int magic_level) { return 12 + magic_level * 2; }
    static float RegenPerSecond(int magic_level) { return 0.6f + magic_level * 0.05f; }

private:
    map<string, SpellDef> defs;
};
