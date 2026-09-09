#pragma once

// ---------------------------------------------------------------------------
//  DreamQuest - common includes and small shared types
// ---------------------------------------------------------------------------

using namespace std;

// SDL 3 Includes
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

// Standard Includes
#include <iostream>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <random>
#include <set>
#include <filesystem>

#include "json.hpp"
using json = nlohmann::json;

// World geometry ------------------------------------------------------------
// The art is authored on a 16px grid; the world stores raw art pixels and the
// camera applies a zoom, so maps can grow far past the window size.
static constexpr int TILE = 16;

struct Vec2 {
    float x = 0, y = 0;
};

inline float Length(float x, float y) { return sqrtf(x * x + y * y); }

inline bool RectsOverlap(const SDL_FRect& a, const SDL_FRect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

// Facing order matches the CraftPix 4-direction sheets: row 0 down, 1 left,
// 2 right, 3 up.
enum Facing { FACE_DOWN = 0, FACE_LEFT = 1, FACE_RIGHT = 2, FACE_UP = 3 };
