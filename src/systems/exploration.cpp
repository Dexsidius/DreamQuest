#include "exploration.h"
#include <cstring>

uint32_t Exploration::next_stamp = 0;

namespace {

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string Base64(const vector<uint8_t>& in) {
    string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const uint32_t a = in[i];
        const uint32_t b = i + 1 < in.size() ? in[i + 1] : 0;
        const uint32_t c = i + 2 < in.size() ? in[i + 2] : 0;
        const uint32_t n = (a << 16) | (b << 8) | c;
        out += kBase64[(n >> 18) & 63];
        out += kBase64[(n >> 12) & 63];
        out += i + 1 < in.size() ? kBase64[(n >> 6) & 63] : '=';
        out += i + 2 < in.size() ? kBase64[n & 63] : '=';
    }
    return out;
}

bool Unbase64(const string& in, vector<uint8_t>& out) {
    out.clear();
    uint32_t n = 0;
    int bits = 0;
    for (const char ch : in) {
        if (ch == '=') break;
        const char* at = strchr(kBase64, ch);
        if (!at || ch == '\0') return false;
        n = (n << 6) | static_cast<uint32_t>(at - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((n >> bits) & 0xFF));
        }
    }
    return true;
}

// 0 at the edge of what lifts, 1 at the edge of what lifts all the way:
// smooth at both ends, so the fog has no ring in it.
float Lift(float d) {
    if (d <= Exploration::CLEAR) return 1.0f;
    if (d >= Exploration::RADIUS) return 0.0f;
    const float t = (Exploration::RADIUS - d) / (Exploration::RADIUS - Exploration::CLEAR);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void Exploration::Reveal(const string& map_id, float map_w, float map_h, float x, float y, float dt) {
    if (map_id.empty() || map_w <= 0.0f || map_h <= 0.0f || dt <= 0.0f) return;
    const int w = std::max(1, static_cast<int>(ceilf(map_w / CELL)));
    const int h = std::max(1, static_cast<int>(ceilf(map_h / CELL)));
    Grid& g = grids[map_id];
    if (g.w != w || g.h != h || g.seen.size() != static_cast<size_t>(w) * h) {
        g.w = w;
        g.h = h;
        g.seen.assign(static_cast<size_t>(w) * h, 0);
        g.stamp = ++next_stamp;
    }

    // A whole step of clearing at a time; a fraction is carried to the next.
    carry += 255.0f * dt / CLEAR_TIME;
    const int step = static_cast<int>(carry);
    carry -= static_cast<float>(step);
    if (step <= 0) return;

    const int x0 = std::max(0, static_cast<int>(floorf((x - RADIUS) / CELL)));
    const int x1 = std::min(w - 1, static_cast<int>(floorf((x + RADIUS) / CELL)));
    const int y0 = std::max(0, static_cast<int>(floorf((y - RADIUS) / CELL)));
    const int y1 = std::min(h - 1, static_cast<int>(floorf((y + RADIUS) / CELL)));
    bool changed = false;
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            uint8_t& v = g.seen[static_cast<size_t>(cy) * w + cx];
            if (v == 255) continue;
            const float d = Length((cx + 0.5f) * CELL - x, (cy + 0.5f) * CELL - y);
            const int target = static_cast<int>(lroundf(255.0f * Lift(d)));
            if (v >= target) continue;
            v = static_cast<uint8_t>(std::min(target, v + step));
            changed = true;
        }
    }
    if (changed) g.stamp = ++next_stamp;
}

float Exploration::SeenAt(const string& map_id, float x, float y) const {
    const Grid* g = GridFor(map_id);
    if (!g || g->w <= 0) return 0.0f;
    // Between the middles of the four squares round the point.
    const float fx = x / CELL - 0.5f, fy = y / CELL - 0.5f;
    const int cx = static_cast<int>(floorf(fx)), cy = static_cast<int>(floorf(fy));
    const float tx = fx - cx, ty = fy - cy;
    const auto at = [&](int ax, int ay) {
        return g->At(std::clamp(ax, 0, g->w - 1), std::clamp(ay, 0, g->h - 1)) / 255.0f;
    };
    const float top = at(cx, cy) * (1.0f - tx) + at(cx + 1, cy) * tx;
    const float bottom = at(cx, cy + 1) * (1.0f - tx) + at(cx + 1, cy + 1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

const Exploration::Grid* Exploration::GridFor(const string& map_id) const {
    const auto it = grids.find(map_id);
    return it == grids.end() ? nullptr : &it->second;
}

// Runs of one value, as (how many, what), at most 255 to a run.
static vector<uint8_t> Runs(const vector<uint8_t>& cells) {
    vector<uint8_t> runs;
    for (size_t i = 0; i < cells.size();) {
        size_t n = 1;
        while (i + n < cells.size() && n < 255 && cells[i + n] == cells[i]) ++n;
        runs.push_back(static_cast<uint8_t>(n));
        runs.push_back(cells[i]);
        i += n;
    }
    return runs;
}

json Exploration::ToJson() const {
    json out = json::object();
    for (const auto& [id, g] : grids) {
        // Run-length coded, either as it is or with each row as what has
        // changed from the row above -- whichever is shorter. A clearing is
        // shorter as it is; a road walked down a map is the same row over and
        // over, and shorter as changes.
        vector<uint8_t> rows(g.seen.size());
        for (size_t i = 0; i < g.seen.size(); ++i)
            rows[i] = static_cast<uint8_t>(g.seen[i] - (i >= static_cast<size_t>(g.w) ? g.seen[i - g.w] : 0));
        const vector<uint8_t> plain = Runs(g.seen), changes = Runs(rows);
        json one = {{"w", g.w}, {"h", g.h}};
        if (changes.size() < plain.size()) {
            one["seen"] = Base64(changes);
            one["rows"] = "changes";
        } else {
            one["seen"] = Base64(plain);
        }
        out[id] = std::move(one);
    }
    return out;
}

void Exploration::FromJson(const json& j) {
    Clear();
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) continue;
        const int w = it.value().value("w", 0), h = it.value().value("h", 0);
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) continue;
        vector<uint8_t> runs;
        if (!Unbase64(it.value().value("seen", string()), runs)) continue;
        Grid g;
        g.w = w;
        g.h = h;
        g.seen.reserve(static_cast<size_t>(w) * h);
        for (size_t i = 0; i + 1 < runs.size(); i += 2)
            g.seen.insert(g.seen.end(), runs[i], runs[i + 1]);
        // Anything that does not add up to the grid is nobody's map.
        if (g.seen.size() != static_cast<size_t>(w) * h) continue;
        if (it.value().value("rows", string()) == "changes")
            for (size_t i = static_cast<size_t>(w); i < g.seen.size(); ++i)
                g.seen[i] = static_cast<uint8_t>(g.seen[i] + g.seen[i - w]);
        g.stamp = ++next_stamp;
        grids[it.key()] = std::move(g);
    }
}
