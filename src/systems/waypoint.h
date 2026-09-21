#pragma once
#include "../headers.h"
#include "quest.h"

class World;
class EnemyDatabase;
class LootSystem;
class Inventory;
class ItemDatabase;

// -----------------------------------------------------------------------------
//  Where a quest is.
//
//  A quest says "speak to Warden Sela", and until this the game left it there:
//  who Sela was, which of five maps she stood on and which road led to it were
//  the player's to remember. A waypoint is the answer to "where is the thing
//  this stage wants", worked out from what the stage names and from an index of
//  every map -- who stands where, what grows where, what lives where, and which
//  way out leads to which map -- that genmaps writes as it builds them
//  (data/waypoints.json), because it is the only thing that ever has all of
//  that in one place.
//
//      Talk         the NPC
//      Deliver      the NPC, once the bag holds what they want; until then,
//                   wherever that can be got
//      Collect      wherever it can be got: what yields it, or what drops it
//      Kill         what is to be killed, on the map the stage names if it
//                   names one
//      Interact     the thing
//      Reach        the way to the place
//
//  "Where" is two answers. The thing is on some map, at some spot. The player is
//  on a map too, and if it is not the same one the useful answer is the way out
//  that starts towards it -- the first step of the shortest road, counted in
//  maps -- so the marker on the screen always points at something that is
//  actually here. When there is no road at all it says why: the Reverie is not
//  reached by walking.
//
//  On the map the player is on, what is alive is looked at and not the index:
//  a villager on their round is where they are now, and "the nearest boar" is
//  the nearest one still standing.
// -----------------------------------------------------------------------------

struct Waypoint {
    bool   found = false;
    string quest;
    string what;                     // "Warden Sela", "Wild Boar", "The Whisperwood"
    string map;                      // where the thing itself is
    float  x = 0.0f, y = 0.0f;
    bool   here = false;             // on the map the player is on
    // Where to walk on this map: the thing, or the way out towards it.
    float  local_x = 0.0f, local_y = 0.0f;
    string via;                      // that way out's label, when it is one
    string place;                    // the far map's name, for "in Mossvale"
    string hint;                     // why there is nothing to point at, when there is not
    int    maps_away = 0;
};

class WaypointIndex {
public:
    struct Spot   { string map; float x = 0, y = 0; string label; };
    struct Exit   { float x = 0, y = 0; string to, label; };
    struct Post   { vector<string> types; float x = 0, y = 0; };
    // `station` is what a workbench object works as -- "anvil", "loom" -- so a
    // quest that asks for something to be made can be pointed at somewhere it
    // can be. Empty on everything that is not a place to make things.
    struct Thing  { string id, kind, yield, title; float x = 0, y = 0; string station; };
    struct Person { string id, name; float x = 0, y = 0; };
    struct Area {
        string name;
        bool   dream = false;
        vector<Exit> exits;
        vector<Person> people;
        vector<Thing> things;
        vector<Post> posts;
    };

    bool Load(const string& path);
    bool Loaded() const { return !areas.empty(); }
    const std::map<string, Area>& Areas() const { return areas; }

    // The maps from one to another, both ends included, by the fewest doors;
    // empty if no road leads there.
    vector<string> Route(const string& from, const string& to) const;

    // Everywhere a stage could be done. `holding` is how many of a Deliver's
    // item the bag has.
    vector<Spot> SpotsFor(const QuestStage& stage, int holding, const EnemyDatabase* enemies,
                          const LootSystem* loot, const ItemDatabase* items = nullptr) const;

    // The whole answer, for the player where they stand.
    Waypoint Resolve(const QuestLog& log, const string& quest_id, const World& world,
                     const EnemyDatabase* enemies, const LootSystem* loot, const ItemDatabase* items = nullptr) const;

private:
    std::map<string, Area> areas;
};
