#pragma once
#include "player.h"

// -----------------------------------------------------------------------------
//  The character panel's numbers
//
//  What the panel writes beside the figure: the character's attributes, and
//  everything running on them. Worked out here rather than in the panel that
//  draws them -- the reason ItemStatLines lives in the item layer -- so the
//  self-test can hold each line to what the fight does with it.
// -----------------------------------------------------------------------------

// One attribute: what it is, what it comes to, and -- for a combat level --
// what what is worn adds to it (`extra`, "+32"). `tone` is +1 when something
// has it above where the character stands (a draught, a meal, a boon) and -1
// when something has it below (a hex, a poison); 0 otherwise.
struct AttributeLine {
    string label, value, extra;
    int    tone = 0;
};

// Two columns of ATTRIBUTE_ROWS: what keeps the character standing, and what
// they fight with. See attributes.cpp for each line.
static constexpr int ATTRIBUTE_ROWS = 7;
vector<AttributeLine> CharacterAttributes(const Player& p);

// One thing running on the character, and how long it has.
struct BoonLine {
    enum class Kind { Boon, Totem, Meal, Draught, Ward, Ability, Charm, Passive, Affliction };
    Kind   kind = Kind::Boon;
    string name, detail;
    // How long it has -- "18h 20m", "12 min" -- or, for what a piece does,
    // where it is worn ("feet", "weapon"); empty for nothing to say.
    string left;
};

// Everything running, in the order the panel lists it: the bosses' boons, the
// totem's blessing, a meal, draughts, wards, what an ability has left going,
// the charm on the weapon and what the worn pieces do -- and last, whatever a
// monster has left on them. `hours_to_dawn` is when the totem's blessing ends,
// which the world's clock knows and the character does not.
vector<BoonLine> CharacterBoons(const Player& p, const StatusDatabase* statuses, double hours_to_dawn);

// A time left as the panel says it. Hours are the world's clock's, as a boon
// counts them ("18h 20m"); seconds are play's ("12 min", "40 s").
string HoursLeftText(double hours);
string SecondsLeftText(float seconds);
