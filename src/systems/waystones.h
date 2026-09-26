#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  The waystones: every one there is, where it stands, and what the travel
//  panel calls it.
//
//  Each is an object of type "waystone" in its map (tools/genmaps.cpp,
//  PlaceWaystone), asleep until somebody puts a hand on it; the world remembers
//  a woken one as a flag with its id. Arriving by one puts you at the spawn
//  named for it, in front of it.
//
//  The panel has two tabs. **Towns**: the three towns' own stones, and the one
//  at the door of the player's house in Mossvale. **The wilds**: the
//  checkpoints out on the overworld's own ground -- the Ashen Path, the top of
//  the climb onto Purgatory's Plateau, and the Bayou by the Hexmire's gate.
//  The self-test holds the maps to this list: every stone in it stands where it
//  says, and there is no stone anywhere that is not in it.
// -----------------------------------------------------------------------------

struct WaystoneDef {
    const char* id;      // the object's id, the flag it sets, and the spawn it arrives at
    const char* map;     // the map it stands in
    const char* name;    // what the panel calls it
    const char* note;    // and the line under that
    bool town;           // under Towns, or under the wilds
};

inline const vector<WaystoneDef>& Waystones() {
    static const vector<WaystoneDef> kAll = {
        {"waystone_havenbrook",       "town_havenbrook", "Havenbrook",          "the market town on the southern road",       true},
        {"waystone_mossvale",         "mossvale",        "Mossvale",            "the logging village under the Whisperwood",  true},
        {"waystone_mossvale_cottage", "mossvale",        "Your house",          "at your own door, in Mossvale",              true},
        {"waystone_fernhollow",       "fernhollow",      "Fernhollow",          "the hamlet on still water",                  true},
        {"waystone_ashen_path",       "ashen_path",      "The Ashen Path",      "where the palace road leaves the burnt one", false},
        {"waystone_plateau",          "plateau_ascent",  "Purgatory's Plateau", "the Pale Ascent, at the top of the climb",   false},
        {"waystone_bayou",            "bayou",           "The Bayou",           "on the spur, below the Hexmire's gate",      false},
    };
    return kAll;
}

inline const WaystoneDef* WaystoneById(const string& id) {
    for (const WaystoneDef& w : Waystones())
        if (id == w.id) return &w;
    return nullptr;
}

// The stones under one tab, in the order the panel lists them.
inline vector<const WaystoneDef*> WaystonesIn(bool towns) {
    vector<const WaystoneDef*> out;
    for (const WaystoneDef& w : Waystones())
        if (w.town == towns) out.push_back(&w);
    return out;
}
