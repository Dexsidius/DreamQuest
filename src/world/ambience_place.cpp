// What a map has for the air's life to use, surveyed once on arrival -- and
// what each ground is to walk on. See Ambience::SetPlace.
#include "ambience.h"
#include "map.h"
#include "../systems/shaders.h"

namespace {

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

bool Starts(const string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool Has(const string& s, const char* p) { return s.find(p) != string::npos; }

// Where in its picture a thing's pixels are, 0..1 of it: tree pictures sit on
// canvases several times wider than the tree. Read once a picture.
SDL_FRect Opaque(const string& path) {
    static unordered_map<string, SDL_FRect> known;
    const auto it = known.find(path);
    if (it != known.end()) return it->second;
    SDL_FRect f{0.0f, 0.0f, 1.0f, 1.0f};
    if (SDL_Surface* s = IMG_Load(path.c_str())) {
        if (SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32)) {
            int x0 = c->w, y0 = c->h, x1 = -1, y1 = -1;
            for (int y = 0; y < c->h; ++y) {
                const Uint8* row = static_cast<const Uint8*>(c->pixels) + y * c->pitch;
                for (int x = 0; x < c->w; ++x)
                    if (row[x * 4 + 3] > 96) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
            }
            if (x1 >= x0 && y1 >= y0)
                f = {static_cast<float>(x0) / c->w, static_cast<float>(y0) / c->h, static_cast<float>(x1 - x0 + 1) / c->w,
                     static_cast<float>(y1 - y0 + 1) / c->h};
            SDL_DestroySurface(c);
        }
        SDL_DestroySurface(s);
    }
    known[path] = f;
    return f;
}

float Hash01(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^
                 static_cast<uint32_t>(seed) * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

}   // namespace

Ambience::Footing Ambience::FootingOf(const string& tile_path) {
    const string stem = StemOf(tile_path);
    if (stem.empty()) return FOOT_NONE;
    if (stem == "snow") return FOOT_SNOW;
    if (stem == "salt_flat" || stem == "salt_crust") return FOOT_SALT;
    if (stem == "ash" || stem == "pale_ash" || stem == "cinder" || stem == "kiln_cinders" || stem == "bone_dust")
        return FOOT_ASH;
    if (stem == "sand" || stem == "shell_sand" || stem == "cursed_sand" || stem == "deeps_sand") return FOOT_SAND;
    if (stem == "swamp_mud" || stem == "marsh_ground" || stem == "tide_flat" || stem == "peat") return FOOT_MUD;
    if (Has(stem, "ice") && !Has(stem, "price")) return FOOT_ICE;
    if (Starts(stem, "plank")) return FOOT_WOOD;
    if (Has(stem, "grass") || stem == "moss" || stem == "fen_sedge" || stem == "frost_heath" || stem == "frozen_turf")
        return FOOT_GRASS;
    if (stem == "dirt" || stem == "dirt_dark" || stem == "cursed_ground" || stem == "scree" || stem == "drowned_loam" ||
        stem == "hex_clay" || stem == "temple_earth" || stem == "grave_earth" || stem == "cypress_litter")
        return FOOT_EARTH;
    if (Has(stem, "road") || Has(stem, "plaza") || Has(stem, "flag") || Has(stem, "paving") || Has(stem, "stone") ||
        Has(stem, "slab") || Has(stem, "floor") || Has(stem, "crag") || Has(stem, "rock") || Starts(stem, "bedrock") ||
        Starts(stem, "kiln_") || Starts(stem, "tempest_") || Starts(stem, "college_") || Starts(stem, "firm_"))
        return FOOT_STONE;
    return FOOT_NONE;
}

Ambience::Footing Ambience::FootingAt(float x, float y) const {
    if (place.cols <= 0 || place.rows <= 0 || x < 0.0f || y < 0.0f) return FOOT_NONE;
    const int c = static_cast<int>(x / place.cell), r = static_cast<int>(y / place.cell);
    if (c >= place.cols || r >= place.rows) return FOOT_NONE;
    return static_cast<Footing>(place.footing[static_cast<size_t>(r) * place.cols + c]);
}

SDL_Color Ambience::KickedUp(float x, float y) const {
    const Footing f = FootingAt(x, y);
    // Wet ground throws water and mud, whatever it is under the wet.
    if (wet > 0.3f && (f == FOOT_EARTH || f == FOOT_GRASS || f == FOOT_STONE || f == FOOT_SAND))
        return {176, 196, 214, 170};
    switch (f) {
        case FOOT_SNOW:  return {242, 246, 252, 200};
        case FOOT_SALT:  return {236, 234, 226, 190};
        case FOOT_ASH:   return {118, 112, 108, 170};
        case FOOT_SAND:  return {226, 206, 158, 160};
        case FOOT_MUD:   return {104, 86, 62, 190};
        case FOOT_ICE:   return {214, 232, 246, 150};
        case FOOT_GRASS: return {120, 162, 82, 150};
        case FOOT_STONE: return {184, 178, 168, 110};
        case FOOT_WOOD:  return {0, 0, 0, 0};
        default:         return {214, 196, 160, 150};
    }
}

void Ambience::SetPlace(const Map& map) {
    place = Survey{};
    if (!map.Loaded()) return;
    place.id = map.Id();
    place.width = map.Width();
    place.height = map.Height();
    place.cols = static_cast<int>(std::ceil(place.width / place.cell));
    place.rows = static_cast<int>(std::ceil(place.height / place.cell));
    place.footing.assign(static_cast<size_t>(place.cols) * place.rows, FOOT_NONE);

    // The ground: the floor, and then what is laid over it that is a ground
    // of its own (a path over the grass). A rug or a bridge is not.
    for (int pass = 0; pass < 2; ++pass)
        for (const TileInstance& t : map.Tiles()) {
            if (t.layer != LAYER_GROUND || t.overlay != (pass == 1)) continue;
            const Footing f = FootingOf(map.TexturePath(t));
            if (f == FOOT_NONE && pass == 1) continue;
            const int c0 = std::max(0, static_cast<int>(t.rect.x / place.cell));
            const int r0 = std::max(0, static_cast<int>(t.rect.y / place.cell));
            const int c1 = std::min(place.cols - 1, static_cast<int>((t.rect.x + t.rect.w - 1.0f) / place.cell));
            const int r1 = std::min(place.rows - 1, static_cast<int>((t.rect.y + t.rect.h - 1.0f) / place.cell));
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c) {
                    const float mx = (c + 0.5f) * place.cell, my = (r + 0.5f) * place.cell;
                    if (mx < t.rect.x || mx >= t.rect.x + t.rect.w || my < t.rect.y || my >= t.rect.y + t.rect.h) continue;
                    place.footing[static_cast<size_t>(r) * place.cols + c] = f;
                }
        }

    // The water: a point in every 48 px square of it, for things that jump out
    // of it and rings that open on it, and the whole of it as it is drawn.
    const SDL_FRect all{0.0f, 0.0f, place.width, place.height};
    map.SurfaceSpots(all, Shaders::WATER, 48.0f, place.water);
    map.SurfaceRects(all, Shaders::WATER, place.water_rects);

    // What stands about: the trees (shade, and a trunk to run up), bushes
    // (cover), lily pads, gravestones, rocks, windows.
    const auto sort = [&](const string& path, float rx, float ry, float rw, float rh) {
        const string stem = StemOf(path);
        const SDL_FRect o = Opaque(path);
        Spot s;
        s.w = o.w * rw;
        s.h = o.h * rh;
        s.x = rx + (o.x + o.w * 0.5f) * rw;
        s.y = ry + (o.y + o.h) * rh;                     // where its pixels meet the ground
        const Shaders::Art& art = Shaders::ArtOf(path);
        if (stem == "bush" || stem == "bushsmall" || Starts(stem, "bush_") || Starts(stem, "bushsmall_") || stem == "hedge" ||
            stem == "topiary") {
            place.bushes.push_back(s);
        } else if (art.kind == Shaders::PROP_TREE || Starts(stem, "dead_tree") || stem == "birch_tree" ||
                   stem == "snow_pine") {
            place.trees.push_back(s);
        } else if (stem == "lily_pads") {
            s.y = ry + (o.y + o.h * 0.5f) * rh;          // on the pads, not their foot
            place.lilies.push_back(s);
        } else if (stem == "gravestone" || stem == "gravestone_cross" || stem == "grave_mound" || stem == "barrow_mound") {
            place.graves.push_back(s);
        } else if (stem == "obsidian_rock" || stem == "cinder_heap" || Starts(stem, "rock") || Starts(stem, "boulder") ||
                   stem == "scree_pile") {
            place.rocks.push_back(s);
        } else if (map.IsInterior() && (stem == "inn_window" || stem == "window_curtained" || stem == "stained_glass")) {
            place.windows.push_back(s);
        }
    };
    for (const TileInstance& t : map.Tiles()) {
        if (t.layer == LAYER_GROUND) continue;
        sort(map.TexturePath(t), t.rect.x, t.rect.y, t.rect.w, t.rect.h);
    }
    for (const MapObject& o : map.Objects()) {
        if (o.type == "critters") {
            Nest n;
            n.x = o.x;
            n.y = o.y;
            n.radius = o.radius > 0.0f ? o.radius : 60.0f;
            n.count = std::max(1, o.count);
            if (o.species == "hen")       n.kind = HEN;
            else if (o.species == "cat")  n.kind = CAT;
            else if (o.species == "gull") { place.gulls = true; continue; }
            else continue;
            place.nests.push_back(n);
            continue;
        }
        if (o.sprite.empty()) continue;
        float tw = 0.0f, th = 0.0f;
        if (SDL_Surface* s = IMG_Load(o.sprite.c_str())) {
            tw = static_cast<float>(s->w);
            th = static_cast<float>(s->h);
            SDL_DestroySurface(s);
        }
        if (tw <= 0.0f) continue;
        sort(o.sprite, o.x - tw / 2.0f, o.y - th - o.lift, tw, th);
    }

    // Where the rain stands after a shower: a dip here and there in the earth,
    // the mud and the roads, where nothing is in the way.
    for (int r = 0; r < place.rows; ++r)
        for (int c = 0; c < place.cols; ++c) {
            const Uint8 f = place.footing[static_cast<size_t>(r) * place.cols + c];
            if (f != FOOT_EARTH && f != FOOT_MUD && f != FOOT_STONE) continue;
            if (Hash01(c, r, 41) > (f == FOOT_STONE ? 0.035f : 0.07f)) continue;
            const float x = (c + 0.25f + 0.5f * Hash01(c, r, 42)) * place.cell;
            const float y = (r + 0.25f + 0.5f * Hash01(c, r, 43)) * place.cell;
            const SDL_FRect box{x - 9.0f, y - 4.0f, 18.0f, 8.0f};
            if (map.Blocked(box) || map.HazardAt(box) || map.HeightAt(x, y) > 0.0f) continue;
            place.puddles.push_back({x, y});
        }

    // Animals the map already has to hunt are not about them as scenery too:
    // a hen nobody can catch beside one that drops a feather is a puzzle.
    for (const EnemySpawnDef& e : map.Enemies()) {
        const auto twin = [&](const string& type) {
            int k = -1;
            if (type == "deer")    k = DEER;
            if (type == "chicken") k = HEN;
            if (type == "frog")    k = FROG;
            if (type == "rat")     k = RAT;
            if (type == "bat")     k = BAT;
            if (k >= 0) place.twins[k].push_back({e.x, e.y});
        };
        twin(e.type);
        for (const string& p : e.pool) twin(p);
    }

    place.frost = Starts(place.id, "frost_");
    place.reverie = map.DreamDepth() > 0;
    place.crypt = Starts(place.id, "crypt_");
}

bool Ambience::TwinNear(int critter, float x, float y) const {
    if (critter < 0 || critter >= CRITTER_KINDS) return false;
    for (const SDL_FPoint& p : place.twins[critter])
        if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) < 520.0f * 520.0f) return true;
    return false;
}

Ambience::PlaceView Ambience::Place() const {
    PlaceView v;
    v.trees = static_cast<int>(place.trees.size());
    v.bushes = static_cast<int>(place.bushes.size());
    v.lilies = static_cast<int>(place.lilies.size());
    v.graves = static_cast<int>(place.graves.size());
    v.rocks = static_cast<int>(place.rocks.size());
    v.nests = static_cast<int>(place.nests.size()) + (place.gulls ? 1 : 0);
    v.water = static_cast<int>(place.water.size());
    v.puddles = static_cast<int>(place.puddles.size());
    return v;
}

SDL_Rect Ambience::CritterCell(int kind, int frame) {
    frame = std::clamp(frame, 0, CritterFrames(kind) - 1);
    if (kind == DEER) return {frame * 32, 144, 32, 32};
    if (kind == WATCHER) return {0, 176, 16, 32};
    return {frame * 16, std::clamp(kind, 0, static_cast<int>(LIZARD)) * 16, 16, 16};
}

int Ambience::CritterFrames(int kind) {
    static const int kFrames[CRITTER_KINDS] = {5, 4, 4, 5, 3, 3, 3, 3, 4, 5, 1};
    return kind >= 0 && kind < CRITTER_KINDS ? kFrames[kind] : 1;
}

vector<Ambience::CritterView> Ambience::Critters() const {
    vector<CritterView> out;
    for (const Critter& c : critters)
        out.push_back({c.x, c.y, c.h, c.kind, c.state == Critter::FLEE || c.state == Critter::LEAVING,
                       c.state == Critter::HIDING || c.fade <= 0.05f});
    return out;
}

vector<Ambience::PrintView> Ambience::Prints() const {
    vector<PrintView> out;
    for (const Print& p : prints) out.push_back({p.x, p.y, static_cast<Footing>(p.on), p.age});
    return out;
}

vector<Uint8> Ambience::DappleMask(int size, int frame) {
    // A soft oval of shade, with holes of sun in it where the leaves part:
    // value noise thresholded, each frame's holes moved a little from the last.
    const auto hash = [](int x, int y, int seed) {
        uint32_t h = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^
                     static_cast<uint32_t>(seed) * 0xC2B2AE3Du;
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        return static_cast<float>(h >> 8) / 16777216.0f;
    };
    const auto noise = [&](float x, float y, int seed) {
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        const float tx = x - ix, ty = y - iy;
        const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
        const float a = hash(ix, iy, seed), b = hash(ix + 1, iy, seed), c = hash(ix, iy + 1, seed), d = hash(ix + 1, iy + 1, seed);
        return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
    };
    vector<Uint8> mask(static_cast<size_t>(size) * size);
    const float shift = static_cast<float>(frame) * 0.45f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = (x + 0.5f) / size * 2.0f - 1.0f, v = (y + 0.5f) / size * 2.0f - 1.0f;
            const float d = u * u + v * v;
            const float body = std::clamp((1.0f - d) / 0.35f, 0.0f, 1.0f);
            const float n = noise(x * 0.22f + shift, y * 0.22f + shift * 0.4f, 9) * 0.65f +
                            noise(x * 0.5f - shift * 0.7f, y * 0.5f, 13) * 0.35f;
            const float leaf = n > 0.62f ? 0.25f : 1.0f;        // a hole of sun
            mask[static_cast<size_t>(y) * size + x] = static_cast<Uint8>(255.0f * body * leaf);
        }
    return mask;
}
