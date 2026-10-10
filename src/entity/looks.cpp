#include "looks.h"
#include <array>
#include <fstream>

const char* LookPartName(int part) {
    switch (part) {
        case LOOK_HAIR:    return "Hair";
        case LOOK_SKIN:    return "Skin";
        case LOOK_CLOTHES: return "Clothes";
        default:           return "";
    }
}

const char* LookPartKey(int part) {
    switch (part) {
        case LOOK_HAIR:    return "hair";
        case LOOK_SKIN:    return "skin";
        case LOOK_CLOTHES: return "clothes";
        default:           return "";
    }
}

// =============================================================================
//  Looks
// =============================================================================

bool Looks::Any() const {
    for (int p = 0; p < LOOK_PARTS; ++p)
        if (!Own(p)) return true;
    return false;
}

string Looks::Key() const {
    string key;
    for (int p = 0; p < LOOK_PARTS; ++p) {
        char buf[16];
        if (Own(p)) SDL_snprintf(buf, sizeof buf, "%c-", LookPartKey(p)[0]);
        else        SDL_snprintf(buf, sizeof buf, "%c%06x", LookPartKey(p)[0], static_cast<unsigned>(part[p]));
        key += buf;
    }
    return key;
}

json Looks::ToJson() const {
    json j = json::object();
    for (int p = 0; p < LOOK_PARTS; ++p) {
        if (Own(p)) continue;
        char buf[16];
        SDL_snprintf(buf, sizeof buf, "#%06x", static_cast<unsigned>(part[p]));
        j[LookPartKey(p)] = buf;
    }
    return j;
}

Looks Looks::FromJson(const json& j) {
    Looks out;
    if (!j.is_object()) return out;
    for (int p = 0; p < LOOK_PARTS; ++p) {
        const auto it = j.find(LookPartKey(p));
        if (it == j.end()) continue;
        if (it->is_string()) {
            string s = it->get<string>();
            if (!s.empty() && s[0] == '#') s.erase(0, 1);
            if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != string::npos) continue;
            out.part[p] = static_cast<int32_t>(std::stoul(s, nullptr, 16));
        } else if (it->is_number_integer()) {
            const long long v = it->get<long long>();
            if (v >= 0 && v <= 0xFFFFFF) out.part[p] = static_cast<int32_t>(v);
        }
    }
    return out;
}

bool Looks::operator==(const Looks& o) const {
    for (int p = 0; p < LOOK_PARTS; ++p)
        if (part[p] != o.part[p]) return false;
    return true;
}

// =============================================================================
//  The colours offered
//
//  Read at the size the character is drawn, through the three bands: the shade
//  band is not much over half the colour, so the darkest offered are as dark as
//  a colour can go and still have three bands to tell apart, and the palest
//  stop short of white so the light band is not lost against the mid.
// =============================================================================

const vector<LookSwatch>& LookSwatches(int part) {
    static const vector<LookSwatch> hair = {
        {"Raven", 0x2a2321},     {"Dark brown", 0x4a3426}, {"Chestnut", 0x7a4a2c}, {"Auburn", 0x9a3f22},
        {"Copper", 0xc4632a},    {"Strawberry", 0xd98a5a}, {"Honey", 0xc79a4e},     {"Golden", 0xe8cc78},
        {"Flaxen", 0xede0b0},    {"Ash", 0x9c9a96},        {"Silver", 0xd2d4d8},    {"Snow", 0xf4f2f0},
        {"Dusk blue", 0x4a62b0}, {"Moss", 0x5e8a4a},       {"Violet", 0x7a4fa8},    {"Rose", 0xd87a9a},
    };
    static const vector<LookSwatch> skin = {
        {"Porcelain", 0xfbe3d2}, {"Fair", 0xf7cca8},    {"Rosy", 0xf0bfa0},  {"Light", 0xe8b892},
        {"Golden", 0xdda878},    {"Olive", 0xc49a6c},   {"Tan", 0xb8865e},   {"Bronze", 0x9c6b48},
        {"Brown", 0x80553a},     {"Umber", 0x664330},   {"Deep", 0x4e3326},
        {"Moonlit", 0xb8c8e8},   {"Fae", 0xa8c8a0},
    };
    static const vector<LookSwatch> clothes = {
        {"Linen", 0xe0d9c2},  {"White", 0xf2f0ea},  {"Slate", 0x8a929e},   {"Charcoal", 0x3c3c44},
        {"Scarlet", 0xc4302e}, {"Wine", 0x7a2434},  {"Rust", 0xb0582a},    {"Mustard", 0xd0a23a},
        {"Leaf", 0x6e9a44},   {"Forest", 0x3e5e3a}, {"Teal", 0x2e7a78},    {"Sky", 0x6e9ed8},
        {"Royal", 0x3450a8},  {"Navy", 0x283458},   {"Plum", 0x6a3a80},    {"Rose", 0xd87e98},
        {"Umber", 0x6a4a32},
    };
    static const vector<LookSwatch> none;
    switch (part) {
        case LOOK_HAIR:    return hair;
        case LOOK_SKIN:    return skin;
        case LOOK_CLOTHES: return clothes;
        default:           return none;
    }
}

int LookSwatchIndex(const Looks& looks, int part) {
    if (part < 0 || part >= LOOK_PARTS || looks.Own(part)) return 0;
    const vector<LookSwatch>& all = LookSwatches(part);
    for (size_t i = 0; i < all.size(); ++i)
        if (static_cast<int32_t>(all[i].rgb) == looks.part[part]) return static_cast<int>(i) + 1;
    return 0;
}

void SetLookSwatch(Looks& looks, int part, int index) {
    if (part < 0 || part >= LOOK_PARTS) return;
    const vector<LookSwatch>& all = LookSwatches(part);
    if (index <= 0 || index > static_cast<int>(all.size())) looks.part[part] = -1;
    else looks.part[part] = static_cast<int32_t>(all[static_cast<size_t>(index - 1)].rgb);
}

// =============================================================================
//  DyeTable
// =============================================================================

namespace {

uint32_t Pack(int r, int g, int b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// How far a render may land from the exact band and still be that band.
constexpr int kNear = 2;

}  // namespace

bool DyeTable::Load(const string& path) {
    materials.clear();
    hits.clear();
    std::ifstream in(path);
    if (!in) return false;
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("DyeTable: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }
    const auto three = [](const json& j, float out[3]) {
        if (!j.is_array() || j.size() != 3) return;
        for (int i = 0; i < 3; ++i)
            if (j[static_cast<size_t>(i)].is_number()) out[i] = j[static_cast<size_t>(i)].get<float>();
    };
    if (root.contains("ramp")) {
        const json& ramp = root["ramp"];
        three(ramp.value("bands", json()), bands);
        three(ramp.value("tint", json()), tint);
        three(ramp.value("tint_amount", json()), tint_amount);
    }
    if (root.contains("outline")) outline = root["outline"].value("strength", outline);

    std::set<string> flat;
    if (root.contains("flat") && root["flat"].is_array())
        for (const json& f : root["flat"]) if (f.is_string()) flat.insert(f.get<string>());
    if (!root.contains("materials") || !root["materials"].is_object()) return false;
    for (auto it = root["materials"].begin(); it != root["materials"].end(); ++it) {
        Material m;
        m.name = it.key();
        three(it.value(), m.base);
        m.flat = flat.count(m.name) > 0;
        materials.push_back(m);
    }

    // Which part recolours each material, and how far from that part's first
    // material it was drawn.
    const json dye = root.value("dye", json::object());
    for (int p = 0; p < LOOK_PARTS; ++p) {
        const auto list = dye.find(LookPartKey(p));
        if (list == dye.end() || !list->is_array() || list->empty()) continue;
        const Material* first = nullptr;
        for (const json& name : *list) {
            if (!name.is_string()) continue;
            for (Material& m : materials) {
                if (m.name != name.get<string>()) continue;
                if (!first) {
                    first = &m;
                    m.first = true;
                }
                m.part = p;
                for (int i = 0; i < 3; ++i) m.ratio[i] = m.base[i] / std::max(0.02f, first->base[i]);
            }
        }
    }

    // Every colour each band can come out as, to the nearest band.
    for (size_t i = 0; i < materials.size() && i < 256; ++i) {
        const Material& m = materials[i];
        for (int band = m.flat ? 2 : 0; band < 3; ++band) {
            const SDL_Color c = Band(m.base, band);
            for (int dr = -kNear; dr <= kNear; ++dr)
                for (int dg = -kNear; dg <= kNear; ++dg)
                    for (int db = -kNear; db <= kNear; ++db) {
                        const int r = c.r + dr, g = c.g + dg, b = c.b + db;
                        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) continue;
                        const uint8_t d = static_cast<uint8_t>(std::max({std::abs(dr), std::abs(dg), std::abs(db)}));
                        const auto at = hits.find(Pack(r, g, b));
                        if (at != hits.end() && at->second.distance <= d) continue;
                        hits[Pack(r, g, b)] = {static_cast<uint8_t>(i), static_cast<uint8_t>(band), d};
                    }
        }
    }
    return true;
}

SDL_Color DyeTable::Band(const float base[3], int band) const {
    // material() in tools/blender_character.py: the base scaled by the band's
    // brightness, then mixed with the cool tint at that brightness.
    const float k = bands[std::clamp(band, 0, 2)], amount = tint_amount[std::clamp(band, 0, 2)];
    Uint8 out[3];
    for (int i = 0; i < 3; ++i) {
        float c = std::min(1.0f, base[i] * k);
        c = c * (1.0f - amount) + tint[i] * k * amount;
        out[i] = static_cast<Uint8>(std::clamp(std::lround(c * 255.0f), 0L, 255L));
    }
    return {out[0], out[1], out[2], 255};
}

bool DyeTable::Target(const Material& m, const Looks& looks, float out[3]) const {
    if (m.part < 0 || looks.Own(m.part)) return false;
    const uint32_t rgb = static_cast<uint32_t>(looks.part[m.part]);
    const float c[3] = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f};
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(c[i] * m.ratio[i], 0.0f, 1.0f);
    return true;
}

const DyeTable::Hit* DyeTable::Find(Uint8 r, Uint8 g, Uint8 b) const {
    const auto it = hits.find(Pack(r, g, b));
    return it == hits.end() ? nullptr : &it->second;
}

uint32_t DyeTable::Own(int part) const {
    for (const Material& m : materials) {
        if (m.part != part || !m.first) continue;
        const SDL_Color c = Band(m.base, 2);
        return Pack(c.r, c.g, c.b);
    }
    return 0x808080;
}

bool DyeTable::Apply(SDL_Surface* s, const Looks& looks) const {
    if (!s || materials.empty() || !looks.Any() || s->format != SDL_PIXELFORMAT_RGBA32) return false;

    // The three bands of every material these looks recolour.
    vector<std::array<SDL_Color, 3>> to(materials.size());
    vector<char> dyed(materials.size(), 0);
    bool any = false;
    for (size_t i = 0; i < materials.size(); ++i) {
        float target[3];
        if (!Target(materials[i], looks, target)) continue;
        for (int b = 0; b < 3; ++b) to[i][static_cast<size_t>(b)] = Band(target, b);
        dyed[i] = 1;
        any = true;
    }
    if (!any || !SDL_LockSurface(s)) return false;

    const int w = s->w, h = s->h;
    Uint8* px = static_cast<Uint8*>(s->pixels);
    vector<uint8_t> known(static_cast<size_t>(w) * h, 0);
    vector<int16_t> delta(static_cast<size_t>(w) * h * 3, 0);
    bool changed = false;

    for (int y = 0; y < h; ++y) {
        Uint8* row = px + y * s->pitch;
        for (int x = 0; x < w; ++x) {
            Uint8* p = row + x * 4;
            if (p[3] != 255) continue;
            const Hit* hit = Find(p[0], p[1], p[2]);
            if (!hit) continue;
            const size_t i = static_cast<size_t>(y) * w + x;
            known[i] = 1;
            if (!dyed[hit->material]) continue;
            const SDL_Color c = to[hit->material][hit->band];
            const Uint8 now[3] = {c.r, c.g, c.b};
            for (int k = 0; k < 3; ++k) {
                delta[i * 3 + k] = static_cast<int16_t>(now[k] - p[k]);
                if (now[k] != p[k]) changed = true;
                p[k] = now[k];
            }
        }
    }

    // The outline: one pixel drawn as `outline` of the mean of the pixels it
    // borders (outline() in the Blender script), so it moves by that share of
    // their mean change. Anything opaque that is not one of the rig's colours
    // is outline; one that borders nothing recoloured is left alone.
    for (int y = 0; y < h; ++y) {
        Uint8* row = px + y * s->pitch;
        for (int x = 0; x < w; ++x) {
            Uint8* p = row + x * 4;
            const size_t i = static_cast<size_t>(y) * w + x;
            if (p[3] != 255 || known[i]) continue;
            int sum[3] = {0, 0, 0}, n = 0;
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                const size_t j = static_cast<size_t>(ny[k]) * w + nx[k];
                if (!known[j]) continue;
                ++n;
                for (int c = 0; c < 3; ++c) sum[c] += delta[j * 3 + c];
            }
            if (n == 0 || (sum[0] == 0 && sum[1] == 0 && sum[2] == 0)) continue;
            for (int c = 0; c < 3; ++c) {
                const long v = p[c] + std::lround(outline * static_cast<float>(sum[c]) / n);
                p[c] = static_cast<Uint8>(std::clamp(v, 0L, 255L));
            }
            changed = true;
        }
    }
    SDL_UnlockSurface(s);
    return changed;
}

int DyeTable::PartOf(Uint8 r, Uint8 g, Uint8 b) const {
    const Hit* hit = Find(r, g, b);
    if (!hit) return -2;
    return materials[hit->material].part;
}

void DyeTable::Count(SDL_Surface* s, int& opaque, int& known, int* per_part) const {
    opaque = known = 0;
    if (per_part) for (int p = 0; p < LOOK_PARTS; ++p) per_part[p] = 0;
    if (!s || s->format != SDL_PIXELFORMAT_RGBA32 || !SDL_LockSurface(s)) return;
    const Uint8* px = static_cast<const Uint8*>(s->pixels);
    for (int y = 0; y < s->h; ++y) {
        const Uint8* row = px + y * s->pitch;
        for (int x = 0; x < s->w; ++x) {
            const Uint8* p = row + x * 4;
            if (p[3] != 255) continue;
            ++opaque;
            const Hit* hit = Find(p[0], p[1], p[2]);
            if (!hit) continue;
            ++known;
            const int part = materials[hit->material].part;
            if (per_part && part >= 0) ++per_part[part];
        }
    }
    SDL_UnlockSurface(s);
}
