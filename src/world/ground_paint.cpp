#include "ground_paint.h"

#include <atomic>
#include <cstring>
#include <future>
#include <memory>
#include <thread>

#include "../camera.h"
#include "../systems/shaders.h"
#include "map.h"

namespace GroundPaint {
namespace {

// --- the same numbers for the same place, always ------------------------------------------------

inline uint32_t Mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// 0..1 for a pixel (or a lattice point) and a seed.
inline float Hash(int x, int y, int seed) {
    const uint32_t h = Mix(static_cast<uint32_t>(x) * 0x9E3779B1U ^
                           Mix(static_cast<uint32_t>(y) * 0x85EBCA77U ^ static_cast<uint32_t>(seed) * 0xC2B2AE3DU));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

inline float Ease(float t) { return t * t * (3.0f - 2.0f * t); }

// Random at the corners of a lattice `scale` px apart, eased in between.
float Noise(float x, float y, float scale, int seed) {
    const float fx = x / scale, fy = y / scale;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    const float tx = Ease(fx - static_cast<float>(ix)), ty = Ease(fy - static_cast<float>(iy));
    const float a = Hash(ix, iy, seed), b = Hash(ix + 1, iy, seed);
    const float c = Hash(ix, iy + 1, seed), d = Hash(ix + 1, iy + 1, seed);
    const float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

// Octaves of it, each half the size and half as strong.
float Fbm(float x, float y, float scale, int seed, int octaves) {
    float total = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        total += Noise(x, y, scale / static_cast<float>(1 << o), seed + o) * amp;
        norm += amp;
        amp *= 0.5f;
    }
    return total / norm;
}

struct Rgb { float r, g, b; };
inline Rgb Lerp(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// One of a few colours, by a field of 0..1, its edges roughened a little so
// the bands do not run as clean contours.
Rgb Band(float field, const Rgb* cols, int n, float jitter, float x, float y, int seed) {
    float f = field + (Noise(x, y, 2.0f, seed) - 0.5f) * jitter;
    f = std::clamp(f, 0.0f, 0.9999f);
    return cols[static_cast<int>(f * static_cast<float>(n))];
}

// --- the grounds ---------------------------------------------------------------------------

// The painters. Everything without one is painted from its own art (SAMPLE).
enum Painter : Uint8 {
    P_SAMPLE = 0, P_MEADOW, P_FOREST, P_SWAMP, P_MOSS, P_DIRT, P_DARK_EARTH, P_TILLED, P_ROAD, P_PLAZA, P_SAND,
    P_PLANK, P_COUNT
};

// A painter's colours. Cover's four greens, light to... no: dark to light.
struct Greens { Rgb band[4]; Rgb ring, blade, tip, clump, clump_tip; float flowers; };
const Greens kMeadow = {{{54, 140, 54}, {72, 162, 58}, {92, 182, 64}, {118, 202, 74}},
                        {34, 82, 36}, {40, 116, 44}, {150, 220, 90}, {38, 110, 42}, {140, 214, 86}, 1.0f};
const Greens kForest = {{{30, 92, 44}, {40, 110, 50}, {52, 128, 56}, {68, 146, 62}},
                        {20, 56, 28}, {26, 82, 36}, {104, 176, 76}, {24, 76, 34}, {98, 168, 72}, 0.25f};
// The Bayou's: deeper and lusher than the meadow's, and it keeps more of its
// colour (kSoft below).
const Greens kSwamp = {{{28, 110, 44}, {36, 130, 50}, {48, 150, 56}, {68, 172, 66}},
                       {18, 70, 30}, {26, 104, 40}, {124, 214, 88}, {22, 96, 38}, {116, 206, 82}, 0.35f};
const Greens kMoss = {{{70, 116, 44}, {86, 136, 50}, {104, 154, 56}, {126, 170, 64}},
                      {44, 72, 28}, {60, 100, 38}, {150, 190, 80}, {56, 94, 34}, {146, 184, 76}, 0.0f};
const Rgb kOlive = {170, 190, 60};
const Rgb kDirt[] = {{176, 116, 64}, {200, 142, 82}, {220, 166, 102}};
const Rgb kDarkEarth[] = {{112, 76, 48}, {130, 90, 56}, {148, 104, 64}};
const Rgb kRoad[] = {{176, 168, 152}, {162, 154, 140}, {190, 182, 166}, {150, 144, 134}};
const Rgb kRoadMortar = {86, 78, 70}, kRoadLit = {214, 208, 194}, kRoadShade = {120, 112, 102};
const Rgb kSlab[] = {{198, 186, 162}, {188, 176, 152}, {206, 194, 170}, {182, 170, 146}};
const Rgb kSand[] = {{222, 196, 132}, {236, 214, 152}, {246, 230, 178}};

// How much of its colour each keeps in the Cozy look (see Soften): the
// Bayou's grass more than the meadow's, art-painted grounds more than the
// painted ones, which are bright to begin with.
float SoftOf(Uint8 painter) {
    switch (painter) {
        case P_SWAMP:  return 0.92f;
        case P_SAMPLE: return 0.82f;
        case P_FOREST: return 0.78f;
        default:       return 0.7f;
    }
}

string StemOf(const string& path) {
    const size_t slash = path.find_last_of("/\\");
    string stem = slash == string::npos ? path : path.substr(slash + 1);
    const size_t dot = stem.rfind('.');
    if (dot != string::npos) stem.resize(dot);
    // grass_light_2 is grass_light.
    const size_t us = stem.rfind('_');
    if (us != string::npos && us + 1 < stem.size() &&
        std::all_of(stem.begin() + static_cast<long>(us) + 1, stem.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        stem.resize(us);
    return stem;
}

struct Named { const char* stem; Role role; Uint8 painter; float tone; };
// The grounds there are out of doors, and what each is.
const Named kTable[] = {
    // Cover: grass and what grows like it.
    {"grass", COVER, P_MEADOW, 0.0f},        {"grass_light", COVER, P_MEADOW, 1.0f},
    {"grass_olive", COVER, P_MEADOW, -1.0f}, {"grass_dark", COVER, P_FOREST, 0.0f},
    {"swamp_grass", COVER, P_SWAMP, 0.0f},   {"moss", COVER, P_MOSS, 0.0f},
    {"fen_sedge", COVER, P_SAMPLE, 0.0f},    {"grave_grass", COVER, P_SAMPLE, 0.0f},
    {"frost_heath", COVER, P_SAMPLE, 0.0f},  {"frozen_turf", COVER, P_SAMPLE, 0.0f},
    {"snow", COVER, P_SAMPLE, 0.0f},
    // Earth.
    {"dirt", EARTH, P_DIRT, 0.0f},           {"dirt_dark", EARTH, P_DARK_EARTH, 0.0f},
    {"sand", EARTH, P_SAND, 0.0f},
    {"peat", EARTH, P_SAMPLE, 0.0f},         {"swamp_mud", EARTH, P_SAMPLE, 0.0f},
    {"marsh_ground", EARTH, P_SAMPLE, 0.0f}, {"ash", EARTH, P_SAMPLE, 0.0f},
    {"pale_ash", EARTH, P_SAMPLE, 0.0f},     {"cinder", EARTH, P_SAMPLE, 0.0f},
    {"cursed_ground", EARTH, P_SAMPLE, 0.0f}, {"cursed_sand", EARTH, P_SAMPLE, 0.0f},
    {"bone_dust", EARTH, P_SAMPLE, 0.0f},    {"scree", EARTH, P_SAMPLE, 0.0f},
    {"crag", EARTH, P_SAMPLE, 0.0f},         {"frost_rock", EARTH, P_SAMPLE, 0.0f},
    {"drowned_loam", EARTH, P_SAMPLE, 0.0f}, {"hex_clay", EARTH, P_SAMPLE, 0.0f},
    {"temple_earth", EARTH, P_SAMPLE, 0.0f}, {"grave_earth", EARTH, P_SAMPLE, 0.0f},
    {"salt_flat", EARTH, P_SAMPLE, 0.0f},    {"salt_crust", EARTH, P_SAMPLE, 0.0f},
    {"shell_sand", EARTH, P_SAMPLE, 0.0f},   {"tide_flat", EARTH, P_SAMPLE, 0.0f},
    {"deeps_sand", EARTH, P_SAMPLE, 0.0f},   {"deeps_coral", EARTH, P_SAMPLE, 0.0f},
    {"deeps_shelf", EARTH, P_SAMPLE, 0.0f},  {"kiln_cinders", EARTH, P_SAMPLE, 0.0f},
    {"cypress_litter", EARTH, P_SAMPLE, 0.0f}, {"firm_cloud", EARTH, P_SAMPLE, 0.0f},
    // Laid stone, and ice.
    {"road", STONE, P_ROAD, 0.0f},           {"plaza", STONE, P_PLAZA, 0.0f},
    {"college_paving", STONE, P_SAMPLE, 0.0f}, {"college_inlay", STONE, P_SAMPLE, 0.0f},
    {"plateau_road", STONE, P_SAMPLE, 0.0f}, {"frost_road", STONE, P_SAMPLE, 0.0f},
    {"hex_road", STONE, P_SAMPLE, 0.0f},     {"stronghold_flag", STONE, P_SAMPLE, 0.0f},
    {"stronghold_flag_dark", STONE, P_SAMPLE, 0.0f}, {"palace_floor", STONE, P_SAMPLE, 0.0f},
    {"conflux_floor", STONE, P_SAMPLE, 0.0f}, {"conflux_road", STONE, P_SAMPLE, 0.0f},
    {"deeps_road", STONE, P_SAMPLE, 0.0f},   {"kiln_road", STONE, P_SAMPLE, 0.0f},
    {"tempest_road", STONE, P_SAMPLE, 0.0f}, {"firm_road", STONE, P_SAMPLE, 0.0f},
    {"firm_stone", STONE, P_SAMPLE, 0.0f},   {"rime_stone", STONE, P_SAMPLE, 0.0f},
    {"moss_stone", STONE, P_SAMPLE, 0.0f},   {"brine_stone", STONE, P_SAMPLE, 0.0f},
    {"bedrock_road", STONE, P_SAMPLE, 0.0f}, {"bedrock_slab", STONE, P_SAMPLE, 0.0f},
    {"bedrock_gravel", STONE, P_SAMPLE, 0.0f}, {"bedrock_crystal", STONE, P_SAMPLE, 0.0f},
    {"bedrock_chasm", STONE, P_SAMPLE, 0.0f}, {"kiln_basalt", STONE, P_SAMPLE, 0.0f},
    {"kiln_glass", STONE, P_SAMPLE, 0.0f},   {"tempest_slate", STONE, P_SAMPLE, 0.0f},
    {"tempest_glass", STONE, P_SAMPLE, 0.0f}, {"glacier_ice", EARTH, P_SAMPLE, 0.0f},
    {"lake_ice", EARTH, P_SAMPLE, 0.0f},     {"lake_ice_dark", STONE, P_SAMPLE, 0.0f},
    {"blue_ice", EARTH, P_SAMPLE, 0.0f},     {"ice", EARTH, P_SAMPLE, 0.0f},
    // Flat: keeps exactly to its edge.
    {"plank_floor", FLAT, P_PLANK, 0.0f},    {"plank_floor_dark", FLAT, P_SAMPLE, 0.0f},
    {"college_walltop", FLAT, P_SAMPLE, 0.0f}, {"college_wallface", FLAT, P_SAMPLE, 0.0f},
    {"college_carpet", FLAT, P_SAMPLE, 0.0f}, {"firm_sky", FLAT, P_SAMPLE, 0.0f},
};

// --- a tile's art, for the grounds painted from it -------------------------------------------

struct Image {
    int w = 0, h = 0;
    vector<Uint8> rgba;
    Rgb mean{128, 128, 128};
    Rgb At(int x, int y) const {
        const Uint8* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
        return {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
    }
};

Image LoadImage(const string& path) {
    Image img;
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) return img;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return img;
    img.w = c->w; img.h = c->h;
    img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(&img.rgba[static_cast<size_t>(y) * img.w * 4], static_cast<Uint8*>(c->pixels) + y * c->pitch,
                    static_cast<size_t>(img.w) * 4);
    SDL_DestroySurface(c);
    double r = 0, g = 0, b = 0;
    const size_t n = static_cast<size_t>(img.w) * img.h;
    for (size_t i = 0; i < n; ++i) { r += img.rgba[i * 4]; g += img.rgba[i * 4 + 1]; b += img.rgba[i * 4 + 2]; }
    if (n) img.mean = {static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n)};
    return img;
}

// --- a map, made ready to paint -----------------------------------------------------------------

struct Cell {
    Uint8 role = NONE;
    Uint8 painter = P_SAMPLE;
    Sint16 art = -1;              // which of Prepared::arts it is painted from
    Sint16 x0 = 0, y0 = 0;        // its tile's corner, and size, for its art's pixels
    Sint16 tw = 32, th = 32;
    Sint8 level = 0;
};

struct Ring { float x, y; };

struct Prepared {
    string id;
    size_t tiles = 0;
    float bounds_w = 0, bounds_h = 0;
    int W = 0, H = 0;
    int cols = 0, rows = 0;
    float size = 32.0f;
    vector<Cell> cells;
    vector<float> nongrass, wobble, fluid, tone;   // per cell, sampled smoothly between
    vector<Image> arts;
    vector<Ring> rings;
    bool elevated = false;
    int top_level = 0;

    int Index(int c, int r) const { return std::clamp(r, 0, rows - 1) * cols + std::clamp(c, 0, cols - 1); }
    const Cell& At(float x, float y) const {
        return cells[Index(static_cast<int>(std::floor(x / size)), static_cast<int>(std::floor(y / size)))];
    }
    // Between the cells' middles, eased: a one-cell path keeps its width, and
    // a corner comes out round.
    float Sample(const vector<float>& v, float x, float y) const {
        const float gx = x / size - 0.5f, gy = y / size - 0.5f;
        const int c = static_cast<int>(std::floor(gx)), r = static_cast<int>(std::floor(gy));
        const float tx = Ease(gx - static_cast<float>(c)), ty = Ease(gy - static_cast<float>(r));
        const float a = v[Index(c, r)], b = v[Index(c + 1, r)];
        const float d = v[Index(c, r + 1)], e = v[Index(c + 1, r + 1)];
        const float top = a + (b - a) * tx, bottom = d + (e - d) * tx;
        return top + (bottom - top) * ty;
    }
};

std::shared_ptr<Prepared> Prepare(const Map& map) {
    auto p = std::make_shared<Prepared>();
    p->id = map.Id();
    p->tiles = map.Tiles().size();
    p->bounds_w = map.Width();
    p->bounds_h = map.Height();
    p->W = static_cast<int>(std::ceil(map.Width()));
    p->H = static_cast<int>(std::ceil(map.Height()));
    p->elevated = map.HasElevation();
    const bool farm = map.Id().find("havenbrook") != string::npos;   // Havenbrook's dark earth is its tilled plots

    float smallest = 0.0f;
    for (const TileInstance& t : map.Tiles()) {
        const string& path = map.TexturePath(t);
        if (StemOf(path) == "town_well") p->rings.push_back({t.rect.x + t.rect.w * 0.5f, t.rect.y + t.rect.h - 34.0f});
        if (t.layer != LAYER_GROUND || t.overlay) continue;
        const float s = std::min(t.rect.w, t.rect.h);
        if (s >= 8.0f && (smallest == 0.0f || s < smallest)) smallest = s;
    }
    p->size = smallest > 0.0f ? smallest : 32.0f;
    p->cols = static_cast<int>(std::ceil(static_cast<float>(p->W) / p->size));
    p->rows = static_cast<int>(std::ceil(static_cast<float>(p->H) / p->size));
    const size_t n = static_cast<size_t>(p->cols) * p->rows;
    p->cells.assign(n, Cell{});
    vector<float> tone(n, 0.0f);
    unordered_map<string, Sint16> art_of;

    // The floor, then what lies on it that is a ground (a path laid over grass).
    for (int pass = 0; pass < 2; ++pass)
        for (const TileInstance& t : map.Tiles()) {
            if (t.layer != LAYER_GROUND || t.overlay != (pass == 1)) continue;
            const string& path = map.TexturePath(t);
            const Ground& g = GroundOf(path);
            if (pass == 1 && !g.known) continue;    // a rug: drawn as itself
            Sint16 art = -1;
            const auto it = art_of.find(path);
            if (it != art_of.end()) art = it->second;
            else {
                art = static_cast<Sint16>(p->arts.size());
                p->arts.push_back(LoadImage(path));
                art_of[path] = art;
            }
            const int c0 = std::max(0, static_cast<int>(std::floor(t.rect.x / p->size)));
            const int r0 = std::max(0, static_cast<int>(std::floor(t.rect.y / p->size)));
            const int c1 = std::min(p->cols - 1, static_cast<int>(std::ceil((t.rect.x + t.rect.w) / p->size)));
            const int r1 = std::min(p->rows - 1, static_cast<int>(std::ceil((t.rect.y + t.rect.h) / p->size)));
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c) {
                    const float mx = (static_cast<float>(c) + 0.5f) * p->size, my = (static_cast<float>(r) + 0.5f) * p->size;
                    if (mx < t.rect.x || mx >= t.rect.x + t.rect.w || my < t.rect.y || my >= t.rect.y + t.rect.h) continue;
                    Cell& cell = p->cells[static_cast<size_t>(r) * p->cols + c];
                    cell.role = g.role;
                    cell.painter = g.painter == P_DARK_EARTH && farm ? static_cast<Uint8>(P_TILLED) : g.painter;
                    cell.art = art;
                    cell.x0 = static_cast<Sint16>(t.rect.x); cell.y0 = static_cast<Sint16>(t.rect.y);
                    cell.tw = static_cast<Sint16>(std::max(1.0f, t.rect.w)); cell.th = static_cast<Sint16>(std::max(1.0f, t.rect.h));
                    tone[static_cast<size_t>(r) * p->cols + c] = g.painter == P_MEADOW ? g.tone : 0.0f;
                }
        }
    // Raised ground: each cell lifted as its tile is (by the level under its middle).
    if (p->elevated)
        for (int r = 0; r < p->rows; ++r)
            for (int c = 0; c < p->cols; ++c) {
                const int level = map.LevelAt((static_cast<float>(c) + 0.5f) * p->size, (static_cast<float>(r) + 0.5f) * p->size);
                p->cells[static_cast<size_t>(r) * p->cols + c].level = static_cast<Sint8>(level);
                p->top_level = std::max(p->top_level, level);
            }
    // The meadow's lighter and olive patches drift into each other over a few
    // cells rather than showing the squares they were laid in.
    for (int pass = 0; pass < 3; ++pass) {
        vector<float> soft(n, 0.0f);
        for (int r = 0; r < p->rows; ++r)
            for (int c = 0; c < p->cols; ++c) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) sum += tone[p->Index(c + dx, r + dy)];
                soft[static_cast<size_t>(r) * p->cols + c] = sum / 9.0f;
            }
        tone.swap(soft);
    }
    p->tone = std::move(tone);
    p->nongrass.resize(n); p->wobble.resize(n); p->fluid.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Cell& c = p->cells[i];
        p->nongrass[i] = c.role == COVER ? 0.0f : 1.0f;
        // Earth wanders; laid stone keeps nearly straight; boards and walls,
        // and nothing at all, keep to their edges.
        p->wobble[i] = c.role == STONE ? (c.painter == P_PLAZA ? 0.12f : 0.25f)
                     : (c.role == FLAT || c.role == NONE) ? 0.0f : 1.0f;
        p->fluid[i] = c.role == FLUID ? 1.0f : 0.0f;
    }
    return p;
}

// --- the painters ------------------------------------------------------------------------------

const Greens& GreensOf(Uint8 painter) {
    switch (painter) {
        case P_FOREST: return kForest;
        case P_SWAMP:  return kSwamp;
        case P_MOSS:   return kMoss;
        default:       return kMeadow;
    }
}

Rgb CoverAt(Uint8 painter, float x, float y, float tone) {
    const Greens& g = GreensOf(painter);
    const float t = Fbm(x, y, 170.0f, 21, 2) * 0.3f + 0.36f + tone * 0.14f;
    Rgb c = Band(std::clamp(t, 0.0f, 1.0f), g.band, 4, 0.05f, x, y, 31);
    if (painter == P_MOSS && Noise(x, y, 7.0f, 33) > 0.68f) c = g.band[3];   // cushions of it, catching the light
    if (painter == P_MEADOW) c = Lerp(c, kOlive, std::clamp(-tone, 0.0f, 1.0f) * 0.25f);
    return c;
}

Rgb EarthAt(const Rgb* cols, int x, int y) {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    Rgb c = Band(Fbm(fx, fy, 40.0f, 41, 2), cols, 3, 0.12f, fx, fy, 42);
    if (Hash(x, y, 43) < 0.03f) c = {cols[0].r * 0.85f, cols[0].g * 0.83f, cols[0].b * 0.8f};
    if (Hash(x, y, 44) < 0.02f) c = {std::min(255.0f, cols[2].r * 1.08f), std::min(255.0f, cols[2].g * 1.18f),
                                     std::min(255.0f, cols[2].b * 1.35f)};
    // Pebbles: a lit top, a shaded underside.
    const auto pebble = [&](int ax, int ay) { return Hash(ax, ay, 45) < 0.0016f; };
    if (pebble(x, y - 1) || pebble(x - 1, y - 1) || pebble(x - 2, y - 1)) c = {132, 92, 58};
    if (pebble(x, y) || pebble(x - 1, y)) c = {214, 206, 190};
    if (pebble(x - 2, y)) c = {170, 160, 146};
    return c;
}

Rgb TilledAt(int x, int y) {
    const int wobble = static_cast<int>(Noise(static_cast<float>(x), static_cast<float>(y), 18.0f, 51) * 3.0f);
    const int f = ((y + wobble) % 8 + 8) % 8;
    return f < 2 ? Rgb{106, 66, 40} : f == 2 ? Rgb{168, 116, 70} : Rgb{138, 90, 54};
}

// Cobbles: the nearest of a jittered lattice of stones, and the gap to the
// next nearest is the mortar. Lit from the upper left.
Rgb RoadAt(int x, int y) {
    constexpr float cell = 10.0f;
    const int gx0 = static_cast<int>(std::floor(static_cast<float>(x) / cell)), gy0 = static_cast<int>(std::floor(static_cast<float>(y) / cell));
    float d1 = 1e9f, d2 = 1e9f, cx = 0.0f, cy = 0.0f;
    int id_x = 0, id_y = 0;
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = gx0 + ox, gy = gy0 + oy;
            const float px = (static_cast<float>(gx) + Hash(gx, gy, 61) * 0.7f + 0.15f) * cell;
            const float py = (static_cast<float>(gy) + Hash(gx, gy, 62) * 0.7f + 0.15f) * cell;
            const float d = std::hypot(static_cast<float>(x) - px, static_cast<float>(y) - py);
            if (d < d1) { d2 = d1; d1 = d; cx = px; cy = py; id_x = gx; id_y = gy; }
            else if (d < d2) d2 = d;
        }
    constexpr float gap = 1.3f;
    if (d2 - d1 < gap) return kRoadMortar;
    if (d2 - d1 < gap + 2.2f) {
        const float lean = ((static_cast<float>(x) - cx) + (static_cast<float>(y) - cy)) / (cell * 0.5f);
        if (lean < -0.15f) return kRoadLit;
        if (lean > 0.15f) return kRoadShade;
    }
    return kRoad[static_cast<size_t>(Hash(id_x, id_y, 63) * 3.999f)];
}

// Flagstones in a running bond, bevelled, with a ring of darker stone round a well.
Rgb PlazaAt(int x, int y, const vector<Ring>& rings) {
    for (const Ring& w : rings) {
        const float dx = static_cast<float>(x) - w.x, dy = (static_cast<float>(y) - w.y) * 1.25f;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 78.0f && dist < 96.0f) {
            const float ang = (std::atan2(dy, dx) + 3.14159265f) / 6.2831853f * 40.0f;
            const bool seam = (ang - std::floor(ang)) * dist * 0.157f < 1.2f || std::fabs(dist - 78.0f) < 1.2f ||
                              std::fabs(dist - 96.0f) < 1.2f;
            if (seam) return {96, 84, 72};
            return dist < 81.0f ? Rgb{200, 178, 150} : Rgb{170, 146, 120};
        }
    }
    constexpr int SL = 16;
    const int row = y / SL, off = (row % 2) * (SL / 2);
    const int lx = (x + off) % SL, ly = y % SL;
    if (Hash(x, y, 64) < 0.004f) return {168, 152, 128};
    if (ly == 0 || lx == 0) return {104, 94, 82};
    if (ly == SL - 1 || lx == SL - 1) return {150, 136, 116};
    if (ly == 1 || lx == 1) return {220, 210, 190};
    return kSlab[static_cast<size_t>(Hash((x + off) / SL, row, 65) * 3.999f)];
}

Rgb SandAt(int x, int y) {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    return Band(Fbm(fx, fy, 30.0f, 71, 2), kSand, 3, 0.15f, fx, fy, 72);
}

Rgb PlankAt(int x, int y) {
    const int py = ((y % 10) + 10) % 10;
    if (py == 0 || (x + (y / 10) * 23) % 48 == 0) return {92, 56, 30};
    return py == 1 ? Rgb{214, 150, 88} : Rgb{184, 120, 66};
}

// Its own art, as its tile would draw it there.
Rgb ArtAt(const Prepared& p, const Cell& c, int x, int y) {
    if (c.art < 0 || c.art >= static_cast<int>(p.arts.size())) return {128, 128, 128};
    const Image& img = p.arts[static_cast<size_t>(c.art)];
    if (img.w <= 0 || img.h <= 0) return {128, 128, 128};
    const int lx = ((x - c.x0) % c.tw + c.tw) % c.tw, ly = ((y - c.y0) % c.th + c.th) % c.th;
    return img.At(std::min(img.w - 1, lx * img.w / c.tw), std::min(img.h - 1, ly * img.h / c.th));
}

Rgb Colour(const Prepared& p, const Cell& c, int x, int y) {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    switch (c.painter) {
        case P_MEADOW: case P_FOREST: case P_SWAMP: case P_MOSS:
            return c.role == COVER ? CoverAt(c.painter, fx, fy, p.Sample(p.tone, fx, fy)) : ArtAt(p, c, x, y);
        case P_DIRT:       return EarthAt(kDirt, x, y);
        case P_DARK_EARTH: return EarthAt(kDarkEarth, x, y);
        case P_TILLED:     return TilledAt(x, y);
        case P_ROAD:       return RoadAt(x, y);
        case P_PLAZA:      return PlazaAt(x, y, p.rings);
        case P_SAND:       return SandAt(x, y);
        case P_PLANK:      return PlankAt(x, y);
        default:           return ArtAt(p, c, x, y);
    }
}

// The line where a cover lips over what it meets.
Rgb RingOf(const Prepared& p, const Cell& c) {
    if (c.painter == P_SAMPLE) {
        const Rgb m = c.art >= 0 && c.art < static_cast<int>(p.arts.size()) ? p.arts[static_cast<size_t>(c.art)].mean : Rgb{90, 90, 90};
        return {m.r * 0.5f, m.g * 0.5f, m.b * 0.55f};
    }
    return GreensOf(c.painter).ring;
}

// The bank of a fluid: wet sand by water, dark mud by a bog, a black crust by lava.
Rgb ShoreAt(const Prepared& p, const Cell& c, int x, int y) {
    const float n = Noise(static_cast<float>(x), static_cast<float>(y), 6.0f, 73);
    if (c.art >= 0 && c.art < static_cast<int>(p.arts.size())) {
        const Rgb m = p.arts[static_cast<size_t>(c.art)].mean;
        if (m.r > m.b + 30.0f) return Lerp({44, 30, 28}, {62, 40, 34}, n);           // lava
        if (m.r + m.g + m.b < 230.0f) return Lerp({82, 66, 46}, {96, 78, 54}, n);   // bog, blackwater
    }
    return Lerp({178, 150, 100}, {196, 168, 116}, n);
}

inline void Put(Uint8* px, Rgb c, Uint8 a = 255) {
    px[0] = static_cast<Uint8>(std::clamp(c.r, 0.0f, 255.0f));
    px[1] = static_cast<Uint8>(std::clamp(c.g, 0.0f, 255.0f));
    px[2] = static_cast<Uint8>(std::clamp(c.b, 0.0f, 255.0f));
    px[3] = a;
}

// The Cozy look's colours: a little of the colour out, a little light in, warmer.
inline void Soften(Uint8* px, float keep) {
    const float r = px[0], g = px[1], b = px[2];
    const float grey = r * 0.3f + g * 0.59f + b * 0.11f;
    const auto one = [&](float c, float tint) {
        return static_cast<Uint8>(std::clamp((grey + (c - grey) * keep) * 1.05f * tint, 0.0f, 255.0f));
    };
    px[0] = one(r, 1.03f); px[1] = one(g, 1.0f); px[2] = one(b, 0.95f);
}

// How far each pixel is from the nearest one that is `source`, counting a
// diagonal step as one, up to `cap`.
vector<Uint8> Distance(const vector<Uint8>& source, int w, int h, Uint8 cap) {
    vector<Uint8> d(source.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = source[i] ? 0 : cap;
    const auto at = [&](int x, int y) -> int { return (x < 0 || y < 0 || x >= w || y >= h) ? cap : d[static_cast<size_t>(y) * w + x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Uint8& v = d[static_cast<size_t>(y) * w + x];
            const int m = std::min({at(x - 1, y), at(x, y - 1), at(x - 1, y - 1), at(x + 1, y - 1)}) + 1;
            if (m < v) v = static_cast<Uint8>(m);
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            Uint8& v = d[static_cast<size_t>(y) * w + x];
            const int m = std::min({at(x + 1, y), at(x, y + 1), at(x + 1, y + 1), at(x - 1, y + 1)}) + 1;
            if (m < v) v = static_cast<Uint8>(m);
        }
    return d;
}

constexpr int MARGIN = 16;     // what a piece paints past its edges, for what reaches over them
constexpr int PIECE = 256;

// What a pixel turned out to be, past its cell's role.
enum : Uint8 { PX_NONE = 0, PX_COVER, PX_GROUND, PX_FLAT, PX_FLUID, PX_SHORE };

// Paint the world's floor over `area` (world pixels, flat -- before any lifting).
// Returns RGBA for it; `cells_out` is which cell each pixel was painted as.
vector<Uint8> PaintFlat(const Prepared& p, const SDL_Rect& area, vector<int>& cells_out) {
    const int W = area.w, H = area.h;
    vector<Uint8> rgba(static_cast<size_t>(W) * H * 4, 0);
    vector<Uint8> what(static_cast<size_t>(W) * H, PX_NONE);
    cells_out.assign(static_cast<size_t>(W) * H, -1);
    const auto cell_index = [&](float x, float y) {
        return p.Index(static_cast<int>(std::floor(x / p.size)), static_cast<int>(std::floor(y / p.size)));
    };

    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const int x = area.x + i, y = area.y + j;
            if (x < 0 || y < 0 || x >= p.W || y >= p.H) continue;
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            const size_t k = static_cast<size_t>(j) * W + i;
            Uint8* px = &rgba[k * 4];
            int ci = cell_index(fx, fy);
            const Cell& own = p.cells[static_cast<size_t>(ci)];
            if (own.role == NONE) continue;
            // Where a cell's edge is: wobbling, for earth; nearly straight for stone.
            const float wob = p.Sample(p.wobble, fx, fy);
            const float edge = (Fbm(fx, fy, 14.0f, 11, 2) - 0.5f) * 0.9f + (Noise(fx, fy, 5.0f, 13) - 0.5f) * 0.25f;
            // Of the four cells whose middles the pixel lies between, the one of
            // this sort it belongs to: each weighs what it would eased between
            // their middles, and a wander and a scatter of its own -- so where
            // two grasses or two earths meet the line is round at its corners,
            // wanders, and is feathered a pixel or two.
            const auto choose = [&](bool cover) {
                const float gx = fx / p.size - 0.5f, gy = fy / p.size - 0.5f;
                const int c = static_cast<int>(std::floor(gx)), r = static_cast<int>(std::floor(gy));
                // Straight-line weights, not eased: the noise below has the whole
                // cell to wander in, not the steep middle of an ease.
                const float tx = gx - static_cast<float>(c), ty = gy - static_cast<float>(r);
                const int idx[4] = {p.Index(c, r), p.Index(c + 1, r), p.Index(c, r + 1), p.Index(c + 1, r + 1)};
                const float w[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                const auto sort_of = [&](const Cell& cl) {
                    return cover ? cl.role == COVER : (cl.role == EARTH || cl.role == STONE);
                };
                const auto group = [](const Cell& cl) { return cl.painter == P_SAMPLE ? 100 + cl.art : cl.painter; };
                int best = -1;
                float best_v = -1e9f;
                for (int a = 0; a < 4; ++a) {
                    const Cell& ca = p.cells[static_cast<size_t>(idx[a])];
                    if (!sort_of(ca)) continue;
                    const int g = group(ca);
                    float v = 0.0f;
                    for (int b = 0; b < 4; ++b) {
                        const Cell& cb = p.cells[static_cast<size_t>(idx[b])];
                        if (sort_of(cb) && group(cb) == g) v += w[b];
                    }
                    v += (Fbm(fx, fy, 22.0f, 300 + g * 3, 2) - 0.5f) * 1.5f * wob + (Hash(x, y, 400 + g) - 0.5f) * 0.12f * wob;
                    if (v > best_v) { best_v = v; best = idx[a]; }
                }
                return best;
            };
            Uint8 kind = PX_COVER;
            if (own.role == FLAT) {
                kind = PX_FLAT;
            } else if (own.role != COVER &&
                       p.Sample(p.nongrass, fx, fy) + edge * wob > 0.5f) {
                if (own.role == FLUID) {
                    kind = p.Sample(p.fluid, fx, fy) + edge * 0.8f > 0.62f ? PX_FLUID : PX_SHORE;
                } else {
                    kind = PX_GROUND;
                    const int best = choose(false);
                    if (best >= 0) ci = best;
                }
            } else {
                // Grass, or a cell of earth the grass has run over: painted as
                // the grass it belongs to.
                const int best = choose(true);
                if (best >= 0) ci = best;
                else kind = PX_GROUND;
            }
            what[k] = kind;
            cells_out[k] = ci;
            const Cell& c = p.cells[static_cast<size_t>(ci)];
            switch (kind) {
                case PX_FLUID: break;   // the tiles show
                case PX_SHORE: Put(px, ShoreAt(p, own, x, y)); break;
                default:       Put(px, Colour(p, c, x, y)); break;
            }
        }

    const auto at = [&](int i, int j) -> Uint8* { return &rgba[(static_cast<size_t>(j) * W + i) * 4]; };
    const auto is = [&](int i, int j, Uint8 kind) {
        return i >= 0 && j >= 0 && i < W && j < H && what[static_cast<size_t>(j) * W + i] == kind;
    };

    // --- fluids: a broken ring of foam just inside the bank (not by lava) -------------------------
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            if (!is(i, j, PX_FLUID)) continue;
            const int ci = cells_out[static_cast<size_t>(j) * W + i];
            const Cell& c = p.cells[static_cast<size_t>(ci)];
            if (c.art >= 0 && c.art < static_cast<int>(p.arts.size())) {
                const Rgb m = p.arts[static_cast<size_t>(c.art)].mean;
                if (m.r > m.b + 30.0f) continue;
            }
            bool bank = false;
            for (int dy = -2; dy <= 2 && !bank; ++dy)
                for (int dx = -2; dx <= 2 && !bank; ++dx) {
                    const int a = i + dx, b = j + dy;
                    if (a >= 0 && b >= 0 && a < W && b < H && !is(a, b, PX_FLUID) && !is(a, b, PX_NONE)) bank = true;
                }
            if (bank && Noise(static_cast<float>(area.x + i), static_cast<float>(area.y + j), 4.0f, 83) > 0.35f)
                Put(at(i, j), {232, 246, 255}, 230);
        }

    // --- cover over its edges: tufts over the lip, a line along it, the ground below in its shade ---
    vector<Uint8> cover(what.size());
    for (size_t k = 0; k < what.size(); ++k) cover[k] = what[k] == PX_COVER;
    const vector<Uint8> to_cover = Distance(cover, W, H, 8);
    vector<Uint8> cover2 = cover;
    vector<int> owner(what.size(), -1);   // the cover a tuft or a line belongs to
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const size_t k = static_cast<size_t>(j) * W + i;
            if (cover[k] || to_cover[k] > 3 || what[k] != PX_GROUND) continue;
            const float fx = static_cast<float>(area.x + i), fy = static_cast<float>(area.y + j);
            if (Noise(fx, fy, 3.0f, 91) <= 0.55f) continue;
            // The cover it grows from: the nearest cover pixel round it.
            int from = -1;
            for (int r = 1; r <= 3 && from < 0; ++r)
                for (int dy = -r; dy <= r && from < 0; ++dy)
                    for (int dx = -r; dx <= r && from < 0; ++dx)
                        if (is(i + dx, j + dy, PX_COVER)) from = cells_out[static_cast<size_t>(j + dy) * W + (i + dx)];
            if (from < 0) continue;
            const Cell& c = p.cells[static_cast<size_t>(from)];
            if (c.painter == P_SAMPLE && c.role == COVER && Noise(fx, fy, 5.0f, 92) < 0.5f) continue;   // snow: a softer lip
            cover2[k] = 1;
            cells_out[k] = from;
            Put(at(i, j), Colour(p, c, area.x + i, area.y + j));
        }
    const auto c2 = [&](int i, int j) { return i >= 0 && j >= 0 && i < W && j < H && cover2[static_cast<size_t>(j) * W + i]; };
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const size_t k = static_cast<size_t>(j) * W + i;
            Uint8* px = at(i, j);
            if (!c2(i, j)) {
                if (what[k] == PX_FLAT || what[k] == PX_NONE) continue;   // boards and walls keep their own edges
                int from = -1;
                for (int dy = -1; dy <= 1 && from < 0; ++dy)
                    for (int dx = -1; dx <= 1 && from < 0; ++dx)
                        if ((dx || dy) && c2(i + dx, j + dy)) from = cells_out[static_cast<size_t>(j + dy) * W + (i + dx)];
                if (from >= 0) { Put(px, RingOf(p, p.cells[static_cast<size_t>(from)])); what[k] = PX_GROUND; continue; }
                // The lower ground in the cover's shade.
                if (c2(i, j - 3)) {
                    if (px[3] > 0) { px[0] = static_cast<Uint8>(px[0] * 0.78f); px[1] = static_cast<Uint8>(px[1] * 0.78f); px[2] = static_cast<Uint8>(px[2] * 0.78f); }
                    else Put(px, {0, 0, 0}, 56);
                }
            } else if (!c2(i, j + 1) && c2(i, j - 1)) {
                // Its own lip, catching the light.
                for (int ch = 0; ch < 3; ++ch) px[ch] = static_cast<Uint8>(std::min(255.0f, px[ch] * 1.18f));
            }
        }

    // --- tufts, clumps and flowers in the open grass --------------------------------------------
    vector<Uint8> not_cover(what.size());
    for (size_t k = 0; k < what.size(); ++k) not_cover[k] = !cover[k];
    const vector<Uint8> to_edge = Distance(not_cover, W, H, 12);
    const auto open = [&](int i, int j, int r) {
        return i >= 0 && j >= 0 && i < W && j < H && to_edge[static_cast<size_t>(j) * W + i] >= r;
    };
    const auto stamp = [&](int i, int j, std::initializer_list<std::pair<int, int>> where, Rgb c) {
        for (const auto& [dx, dy] : where)
            if (open(i + dx, j + dy, 4)) Put(at(i + dx, j + dy), c);
    };
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            if (!open(i, j, 4)) continue;
            const Cell& c = p.cells[static_cast<size_t>(cells_out[static_cast<size_t>(j) * W + i])];
            if (c.painter != P_MEADOW && c.painter != P_FOREST && c.painter != P_SWAMP && c.painter != P_MOSS) continue;
            const Greens& g = GreensOf(c.painter);
            const int x = area.x + i, y = area.y + j;
            if (c.painter != P_MOSS && Hash(x, y, 101) < 0.009f) {
                stamp(i, j, {{0, 0}, {1, -1}, {2, 0}, {3, -1}, {4, 0}}, g.blade);
                stamp(i, j, {{1, -2}, {3, -2}}, g.tip);
            }
            if (open(i, j, 7) && Hash(x, y, 102) < (c.painter == P_SWAMP ? 0.0011f : 0.0005f)) {
                stamp(i, j, {{0, 0}, {1, -1}, {2, -2}, {3, -1}, {4, -2}, {5, -1}, {6, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0},
                             {5, 0}, {2, -1}, {4, -1}}, g.clump);
                stamp(i, j, {{2, -3}, {4, -3}, {3, -2}}, g.clump_tip);
            }
            if (g.flowers > 0.0f && open(i, j, 9) && Hash(x, y, 111) < 0.0001f * g.flowers) {
                static const Rgb kPetal[] = {{255, 255, 255}, {255, 226, 70}, {255, 120, 150}, {160, 190, 255}};
                const Rgb petal = c.painter == P_MEADOW ? kPetal[static_cast<size_t>(Hash(x, y, 112) * 3.999f)]
                                                       : kPetal[static_cast<size_t>(Hash(x, y, 112) * 1.999f)];
                const int count = 3 + static_cast<int>(Hash(x, y, 113) * 3.999f);
                for (int f = 0; f < count; ++f) {
                    const int a = i + static_cast<int>(Hash(x + f, y, 114) * 19.0f) - 9;
                    const int b = j + static_cast<int>(Hash(x, y + f, 115) * 13.0f) - 6;
                    if (a < 2 || b < 3 || a >= W - 2 || b >= H - 2 || !cover[static_cast<size_t>(b) * W + a]) continue;
                    Put(at(a, b + 1), {g.blade.r * 0.9f, g.blade.g * 0.86f, g.blade.b * 0.9f});
                    for (const auto& [dx, dy] : {std::pair{0, -2}, std::pair{-1, -1}, std::pair{1, -1}, std::pair{0, 0}})
                        Put(at(a + dx, b + dy), petal);
                    Put(at(a, b - 1), {255, 196, 60});
                }
            }
        }

    for (size_t k = 0; k < what.size(); ++k)
        if (rgba[k * 4 + 3] == 255 && cells_out[k] >= 0) Soften(&rgba[k * 4], SoftOf(p.cells[static_cast<size_t>(cells_out[k])].painter));
    return rgba;
}

// A region of the view, painted: flat, then each cell lifted as its tile is.
Picture PaintPiece(const Prepared& p, const SDL_Rect& region) {
    Picture pic;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    pic.x = region.x; pic.y = region.y; pic.w = region.w; pic.h = region.h;
    pic.rgba.assign(static_cast<size_t>(region.w) * region.h * 4, 0);
    const int lift = p.elevated ? static_cast<int>(std::ceil(p.top_level * ELEVATION_RISE)) : 0;
    const SDL_Rect area = {region.x - MARGIN, region.y - MARGIN, region.w + 2 * MARGIN, region.h + 2 * MARGIN + lift};
    vector<int> cells;
    const vector<Uint8> flat = PaintFlat(p, area, cells);
    if (!p.elevated) {
        for (int j = 0; j < region.h; ++j)
            std::memcpy(&pic.rgba[static_cast<size_t>(j) * region.w * 4],
                        &flat[(static_cast<size_t>(j + MARGIN) * area.w + MARGIN) * 4], static_cast<size_t>(region.w) * 4);
    } else {
        // Row by row, as the tiles are drawn: a raised cell lifted over the
        // row behind it, shaded by its height.
        const int c0 = std::max(0, static_cast<int>(std::floor(static_cast<float>(region.x) / p.size)));
        const int c1 = std::min(p.cols - 1, static_cast<int>(std::floor(static_cast<float>(region.x + region.w - 1) / p.size)));
        const int r0 = std::max(0, static_cast<int>(std::floor(static_cast<float>(region.y) / p.size)));
        const int r1 = std::min(p.rows - 1, static_cast<int>(std::floor(static_cast<float>(region.y + region.h - 1 + lift) / p.size)));
        const int cs = static_cast<int>(p.size);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) {
                const Cell& cell = p.cells[static_cast<size_t>(r) * p.cols + c];
                if (cell.role == NONE) continue;
                const int up = static_cast<int>(std::lround(cell.level * ELEVATION_RISE));
                const float shade = static_cast<float>(std::min(255, 236 + cell.level * 7)) / 255.0f;
                for (int yy = 0; yy < cs; ++yy) {
                    const int wy = r * cs + yy, oy = wy - up;
                    if (oy < region.y || oy >= region.y + region.h) continue;
                    const int fj = wy - area.y;
                    if (fj < 0 || fj >= area.h) continue;
                    for (int xx = 0; xx < cs; ++xx) {
                        const int wx = c * cs + xx;
                        if (wx < region.x || wx >= region.x + region.w) continue;
                        const Uint8* src = &flat[(static_cast<size_t>(fj) * area.w + (wx - area.x)) * 4];
                        Uint8* dst = &pic.rgba[(static_cast<size_t>(oy - region.y) * region.w + (wx - region.x)) * 4];
                        dst[0] = static_cast<Uint8>(src[0] * shade);
                        dst[1] = static_cast<Uint8>(src[1] * shade);
                        dst[2] = static_cast<Uint8>(src[2] * shade);
                        dst[3] = src[3];
                    }
                }
            }
    }
    pic.seconds = static_cast<float>(SDL_GetPerformanceCounter() - t0) / static_cast<float>(SDL_GetPerformanceFrequency());
    return pic;
}

// --- the pictures kept for drawing ---------------------------------------------------------

struct Kept {
    std::shared_ptr<Prepared> prep;
    SDL_Renderer* renderer = nullptr;
    int cols = 0, rows = 0;
    vector<SDL_Texture*> pieces;           // cols * rows
    vector<Uint8> state;                    // 0 not yet, 1 being painted, 2 done
    Uint64 used = 0;

    bool Holds(const Map& m, SDL_Renderer* r) const {
        return prep && renderer == r && prep->id == m.Id() && prep->tiles == m.Tiles().size() &&
               prep->bounds_w == m.Width() && prep->bounds_h == m.Height();
    }
    void Free() {
        for (SDL_Texture* t : pieces) if (t) SDL_DestroyTexture(t);
        *this = Kept{};
    }
};
Kept g_kept[2];
Uint64 g_uses = 0;

// Pieces being painted in the background, and those finished whose map has
// gone (kept until they are done: a future waits for its work as it goes).
struct Job {
    std::shared_ptr<Prepared> prep;
    int index = 0;
    std::future<Picture> work;
};
vector<Job> g_jobs;
constexpr size_t kJobs = 3;

SDL_Rect PieceRect(const Prepared& p, int index, int cols) {
    const int c = index % cols, r = index / cols;
    return {c * PIECE, r * PIECE, std::min(PIECE, p.W - c * PIECE), std::min(PIECE, p.H - r * PIECE)};
}

void Upload(Kept& k, int index, const Picture& pic) {
    k.state[static_cast<size_t>(index)] = 2;
    bool any = false;
    for (size_t i = 3; i < pic.rgba.size() && !any; i += 4) any = pic.rgba[i] != 0;
    if (!any) return;
    SDL_Texture* t = SDL_CreateTexture(k.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pic.w, pic.h);
    if (!t) return;
    SDL_UpdateTexture(t, nullptr, pic.rgba.data(), pic.w * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    k.pieces[static_cast<size_t>(index)] = t;
}

}  // namespace

// --- what is painted ---------------------------------------------------------------------------

const Ground& GroundOf(const string& tile_path) {
    static unordered_map<string, Ground> known;
    const auto it = known.find(tile_path);
    if (it != known.end()) return it->second;
    Ground g;
    const Shaders::Surface surface = Shaders::SurfaceOfTile(tile_path);
    if (surface == Shaders::WATER || surface == Shaders::LAVA) {
        g.role = FLUID;
        g.known = true;
    } else {
        const string stem = StemOf(tile_path);
        bool named = false;
        for (const Named& n : kTable)
            if (stem == n.stem) { g.role = n.role; g.painter = n.painter; g.tone = n.tone; g.known = true; named = true; break; }
        if (!named) {
            // Not in the table: earth, from its own art -- or flat, if it is a wall.
            g.role = stem.find("wall") != string::npos ? FLAT : EARTH;
            g.painter = P_SAMPLE;
        }
    }
    return known[tile_path] = g;
}

bool Paints(const Map& map) {
    return map.Loaded() && !map.IsInterior() && !map.IsDark() && map.Ambient() != "dungeon";
}

bool Wants(const Map& map) { return Shaders::Cozy() && Paints(map); }

bool Covers(const string& tile_path, bool overlay) {
    const Ground& g = GroundOf(tile_path);
    if (g.role == FLUID || g.role == NONE) return false;
    return !overlay || g.known;
}

Picture PaintRegion(const Map& map, const SDL_Rect& region) {
    return PaintPiece(*Prepare(map), region);
}

Picture Paint(const Map& map) {
    const std::shared_ptr<Prepared> p = Prepare(map);
    Picture whole;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    whole.w = p->W; whole.h = p->H;
    whole.rgba.assign(static_cast<size_t>(p->W) * p->H * 4, 0);
    const int cols = (p->W + PIECE - 1) / PIECE, rows = (p->H + PIECE - 1) / PIECE;
    vector<Picture> pieces(static_cast<size_t>(cols) * rows);
    std::atomic<int> next{0};
    const auto worker = [&]() {
        for (int i = next++; i < cols * rows; i = next++) pieces[static_cast<size_t>(i)] = PaintPiece(*p, PieceRect(*p, i, cols));
    };
    vector<std::thread> pool;
    const int n = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    for (int k = 0; k < n; ++k) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();
    for (const Picture& piece : pieces)
        for (int j = 0; j < piece.h; ++j)
            std::memcpy(&whole.rgba[(static_cast<size_t>(piece.y + j) * whole.w + piece.x) * 4],
                        &piece.rgba[static_cast<size_t>(j) * piece.w * 4], static_cast<size_t>(piece.w) * 4);
    whole.seconds = static_cast<float>(SDL_GetPerformanceCounter() - t0) / static_cast<float>(SDL_GetPerformanceFrequency());
    return whole;
}

// --- drawing ---------------------------------------------------------------------------------------

void Draw(SDL_Renderer* renderer, const Map& map, const Camera& camera) {
    if (!renderer || !map.Loaded()) return;
    Kept* kept = nullptr;
    for (Kept& k : g_kept)
        if (k.Holds(map, renderer)) kept = &k;
    if (!kept) {
        kept = g_kept[0].used <= g_kept[1].used ? &g_kept[0] : &g_kept[1];
        kept->Free();
        kept->prep = Prepare(map);
        kept->renderer = renderer;
        kept->cols = (kept->prep->W + PIECE - 1) / PIECE;
        kept->rows = (kept->prep->H + PIECE - 1) / PIECE;
        kept->pieces.assign(static_cast<size_t>(kept->cols) * kept->rows, nullptr);
        kept->state.assign(kept->pieces.size(), 0);
    }
    kept->used = ++g_uses;
    const Prepared& p = *kept->prep;

    // What the background has finished: up it goes (or away, if its map has gone).
    for (size_t i = 0; i < g_jobs.size();) {
        Job& j = g_jobs[i];
        if (j.work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++i; continue; }
        Picture pic = j.work.get();
        for (Kept& k : g_kept)
            if (k.prep == j.prep && k.state[static_cast<size_t>(j.index)] == 1) Upload(k, j.index, pic);
        g_jobs.erase(g_jobs.begin() + static_cast<long>(i));
    }

    const SDL_FRect view = camera.VisibleWorldRect(8.0f);
    const auto range = [&](float pad, int& c0, int& r0, int& c1, int& r1) {
        c0 = std::max(0, static_cast<int>(std::floor((view.x - pad) / PIECE)));
        r0 = std::max(0, static_cast<int>(std::floor((view.y - pad) / PIECE)));
        c1 = std::min(kept->cols - 1, static_cast<int>(std::floor((view.x + view.w + pad) / PIECE)));
        r1 = std::min(kept->rows - 1, static_cast<int>(std::floor((view.y + view.h + pad) / PIECE)));
    };
    int c0, r0, c1, r1;
    range(0.0f, c0, r0, c1, r1);

    // What the view needs now and has not got, painted now, side by side.
    vector<int> now;
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            if (kept->state[static_cast<size_t>(r) * kept->cols + c] != 2) now.push_back(r * kept->cols + c);
    if (!now.empty()) {
        vector<Picture> done(now.size());
        std::atomic<int> next{0};
        const auto worker = [&]() {
            for (int i = next++; i < static_cast<int>(now.size()); i = next++)
                done[static_cast<size_t>(i)] = PaintPiece(p, PieceRect(p, now[static_cast<size_t>(i)], kept->cols));
        };
        vector<std::thread> pool;
        const int n = std::clamp(static_cast<int>(std::min<size_t>(now.size(), std::thread::hardware_concurrency())), 1, 16);
        for (int k = 0; k < n; ++k) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
        for (size_t i = 0; i < now.size(); ++i) Upload(*kept, now[i], done[i]);
    }

    // And the ring round the view, in the background, a few at a time.
    int a0, b0, a1, b1;
    range(static_cast<float>(PIECE), a0, b0, a1, b1);
    for (int r = b0; r <= b1 && g_jobs.size() < kJobs; ++r)
        for (int c = a0; c <= a1 && g_jobs.size() < kJobs; ++c) {
            const int index = r * kept->cols + c;
            if (kept->state[static_cast<size_t>(index)] != 0) continue;
            kept->state[static_cast<size_t>(index)] = 1;
            Job j;
            j.prep = kept->prep;
            j.index = index;
            const SDL_Rect rect = PieceRect(p, index, kept->cols);
            std::shared_ptr<Prepared> keep = kept->prep;
            j.work = std::async(std::launch::async, [keep, rect]() { return PaintPiece(*keep, rect); });
            g_jobs.push_back(std::move(j));
        }

    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            SDL_Texture* t = kept->pieces[static_cast<size_t>(r) * kept->cols + c];
            if (!t) continue;
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(t, &tw, &th);
            const SDL_FRect world = {static_cast<float>(c * PIECE), static_cast<float>(r * PIECE), tw, th};
            const SDL_FRect dst = camera.ToScreenRect(world);
            SDL_RenderTexture(renderer, t, nullptr, &dst);
        }
}

void Release() {
    for (Job& j : g_jobs) j.work.wait();
    g_jobs.clear();
    for (Kept& k : g_kept) k.Free();
}

}  // namespace GroundPaint
