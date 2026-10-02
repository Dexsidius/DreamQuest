#pragma once
#include "../headers.h"

// -----------------------------------------------------------------------------
//  What a character has seen of each map: the fog of war on the minimap.
//
//  Every map is a grid of CELL-pixel squares, each kept as how clearly it has
//  been seen, 0 (never) to 255 (clear). Wherever the character goes the fog
//  lifts round them -- all of it close by (CLEAR), less and less out to
//  RADIUS, which is about what the screen shows at the usual zoom -- and not
//  all at once: a square comes clear over CLEAR_TIME, so walking into new
//  ground the fog draws back as you go, and the edge of what you have walked
//  past stays soft. Nothing seen is ever forgotten.
//
//  It is the character's, like what they carry: saved with them (Player::
//  ToJson), so Player Two's minimap is from Player Two's travels, and a
//  friend's is worked out on their own machine, from where they walk, and kept
//  in their own character file. The host's copy of a friend keeps none, and
//  a sheet does not carry it (coop::Guest::MakeSheet).
// -----------------------------------------------------------------------------

class Exploration {
public:
    static constexpr float CELL       = 32.0f;    // world pixels a side: a tile
    static constexpr float RADIUS     = 320.0f;   // how far round you the fog lifts at all
    static constexpr float CLEAR      = 192.0f;   // and inside this, all the way
    static constexpr float CLEAR_TIME = 0.8f;     // seconds for a square to come clear

    struct Grid {
        int w = 0, h = 0;
        vector<uint8_t> seen;                     // row by row, w * h
        // Different every time `seen` changes, across every grid there is:
        // what the minimap compares to know its picture of it is stale.
        uint32_t stamp = 0;
        uint8_t At(int cx, int cy) const {
            return (cx < 0 || cy < 0 || cx >= w || cy >= h) ? 0 : seen[static_cast<size_t>(cy) * w + cx];
        }
    };

    // Lifts the fog round (x, y) on `map_id`, over `dt` seconds. The map's
    // size makes its grid the first time; a map that has been made a different
    // size since is begun again.
    void Reveal(const string& map_id, float map_w, float map_h, float x, float y, float dt);
    // How clearly a point has been seen, 0..1, blended between the squares
    // round it the way the minimap draws them.
    float SeenAt(const string& map_id, float x, float y) const;
    const Grid* GridFor(const string& map_id) const;
    size_t Maps() const { return grids.size(); }
    void Clear() { grids.clear(); carry = 0.0f; }

    // { map: {"w", "h", "seen"} }, each grid run-length coded -- as it is, or
    // as each row's changes from the row above, whichever is shorter ("rows":
    // "changes") -- in base64: a road across the largest map is about a
    // kilobyte, a map seen from end to end a few hundred bytes.
    json ToJson() const;
    void FromJson(const json& j);

private:
    std::map<string, Grid> grids;
    float carry = 0.0f;                           // a part-step of clearing, owed to the next frame
    static uint32_t next_stamp;
};
