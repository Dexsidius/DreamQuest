// The weather on the ground, and what is left on it: a shower's splashes and
// the rings it opens on the water, the puddles it leaves, prints in soft
// ground and splashes in wet, breath in the cold, and the Frostreach's aurora.
// See Ambience, and Weather for when it rains.
#include "ambience.h"
#include "../systems/shaders.h"

namespace {
float Rand01(std::mt19937& rng) { return (rng() % 10000u) / 10000.0f; }
float Range(std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * Rand01(rng); }

float Hash01(float x, float y) {
    uint32_t h = static_cast<uint32_t>(static_cast<int>(x)) * 0x9E3779B1u ^ static_cast<uint32_t>(static_cast<int>(y)) * 0x85EBCA77u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// A flat ring on the ground or the water, a pixel at a time round it.
void Ring(SDL_Renderer* r, const Camera& cam, float x, float y, float radius, SDL_Color c) {
    const float z = cam.zoom;
    const SDL_FPoint p = cam.ToScreen(x, y);
    const float rx = radius * z, ry = rx * 0.5f;
    const int steps = std::max(10, static_cast<int>(radius * 3.0f));
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int i = 0; i < steps; ++i) {
        const float a = 6.2831853f * i / steps;
        const SDL_FRect dot = {roundf((p.x + cosf(a) * rx) / z) * z, roundf((p.y + sinf(a) * ry) / z) * z, z, z};
        SDL_RenderFillRect(r, &dot);
    }
}
}   // namespace

void Ambience::UpdateWeather(float dt, const SDL_FRect& view, const World& world) {
    // Splashes age and go.
    for (Splash& s : splashes) s.age += dt;
    splashes.erase(std::remove_if(splashes.begin(), splashes.end(), [](const Splash& s) { return s.age >= s.life; }),
                   splashes.end());
    for (Puff& p : breath) {
        p.age += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vx *= std::max(0.0f, 1.0f - 1.2f * dt);
        p.vy *= std::max(0.0f, 1.0f - 0.8f * dt);
    }
    breath.erase(std::remove_if(breath.begin(), breath.end(), [](const Puff& p) { return p.age >= p.life; }), breath.end());

    if (lively && rain_shown > 0.02f) {
        // Rings opening all over the water, and the odd one in a puddle; the
        // rain bouncing off the ground everywhere else.
        const auto rate = [&](float per_second) {
            float n = per_second * dt;
            int k = static_cast<int>(n);
            if (Rand01(rng) < n - static_cast<float>(k)) ++k;
            return k;
        };
        if (!place.water.empty())
            for (int i = rate(40.0f * rain_shown); i > 0; --i)
                for (int attempt = 0; attempt < 4; ++attempt) {
                    const SDL_FPoint& w = place.water[rng() % place.water.size()];
                    if (w.x < view.x - 24.0f || w.x > view.x + view.w + 24.0f || w.y < view.y - 24.0f || w.y > view.y + view.h + 24.0f)
                        continue;
                    splashes.push_back({w.x + Range(rng, -20.0f, 20.0f), w.y + Range(rng, -20.0f, 20.0f), 0.0f,
                                        Range(rng, 0.45f, 0.7f), Range(rng, 2.5f, 4.5f), true});
                    break;
                }
        for (int i = rate(70.0f * rain_shown); i > 0; --i)
            splashes.push_back({Range(rng, view.x, view.x + view.w), Range(rng, view.y, view.y + view.h), 0.0f,
                                Range(rng, 0.12f, 0.2f), 1.0f, false});
        for (const SDL_FPoint& p : place.puddles)
            if (p.x > view.x && p.x < view.x + view.w && p.y > view.y && p.y < view.y + view.h && Rand01(rng) < dt * 2.0f * rain_shown)
                splashes.push_back({p.x + Range(rng, -6.0f, 6.0f), p.y + Range(rng, -2.0f, 2.0f), 0.0f, 0.5f, 2.5f, true});
    }
    if (splashes.size() > 400) splashes.erase(splashes.begin(), splashes.begin() + (splashes.size() - 400));

    // Breath in the cold: a puff from everybody out in the snow every couple
    // of seconds, at the mouth, drifting off and gone.
    if (kind == Kind::Snow && lively && (breath_wait -= dt) <= 0.0f) {
        breath_wait = Range(rng, 1.8f, 2.6f);
        const SDL_FPoint wd = Shaders::WindDirection();
        for (const Walker& w : world.walkers)
            for (int k = 0; k < (w.sprinting ? 6 : 4); ++k)
                breath.push_back({w.x + Range(rng, -3.0f, 3.0f), w.y - 23.0f + Range(rng, -1.5f, 1.5f),
                                  wd.x * Range(rng, 6.0f, 14.0f) + Range(rng, -5.0f, 5.0f), Range(rng, -8.0f, -3.0f), 0.0f,
                                  Range(rng, 0.9f, 1.4f)});
    }
    if (breath.size() > 80) breath.erase(breath.begin(), breath.begin() + (breath.size() - 80));
}

void Ambience::UpdateSteps(float dt, const World& world) {
    (void)dt;
    for (Print& p : prints) p.age += dt;
    prints.erase(std::remove_if(prints.begin(), prints.end(), [](const Print& p) { return p.age >= p.life; }), prints.end());
    if (!lively || !OutdoorsKind()) return;
    if (step_left.size() != world.walkers.size()) {
        step_left.assign(world.walkers.size(), 0.0f);
        step_at.clear();
        for (const Walker& w : world.walkers) step_at.push_back({w.x, w.y});
        step_n.assign(world.walkers.size(), 0);
        return;
    }
    for (size_t i = 0; i < world.walkers.size(); ++i) {
        const Walker& w = world.walkers[i];
        const float dx = w.x - step_at[i].x, dy = w.y - step_at[i].y;
        const float moved = std::sqrt(dx * dx + dy * dy);
        if (moved > 60.0f) { step_at[i] = {w.x, w.y}; step_left[i] = 0.0f; continue; }   // a door, a waystone
        if (moved < 0.01f) continue;
        step_at[i] = {w.x, w.y};
        if ((step_left[i] -= moved) > 0.0f) continue;
        step_left[i] = w.sprinting ? 26.0f : 17.0f;
        const bool left = (step_n[i]++ % 2) == 0;
        // Across the way they are going, a foot to either side.
        const float ux = dx / moved, uy = dy / moved;
        const float side = left ? -2.5f : 2.5f;
        const float px = w.x - uy * side, py = w.y + ux * side * 0.6f;
        const Footing f = FootingAt(px, py);
        const bool soaked = wet > 0.35f && (f == FOOT_EARTH || f == FOOT_GRASS || f == FOOT_SAND);
        float life = 0.0f;
        switch (f) {
            case FOOT_SNOW: life = 45.0f; break;
            case FOOT_SAND: life = 30.0f; break;
            case FOOT_SALT: life = 30.0f; break;
            case FOOT_MUD:  life = 40.0f; break;
            case FOOT_ASH:  life = 30.0f; break;
            case FOOT_EARTH: life = soaked ? 25.0f : 0.0f; break;
            default: break;
        }
        if (life > 0.0f) {
            Print p;
            p.x = px; p.y = py; p.dx = ux; p.dy = uy;
            p.life = life;
            p.on = static_cast<Uint8>(soaked ? FOOT_MUD : f);
            p.left = left;
            prints.push_back(p);
        }
        // A splash where it is wet: in the mud, on ground the rain has soaked,
        // and in a puddle.
        bool puddle = false;
        if (wet > 0.15f)
            for (const SDL_FPoint& q : place.puddles)
                if (std::fabs(q.x - px) < 9.0f && std::fabs(q.y - py) < 4.0f) { puddle = true; break; }
        if (f == FOOT_MUD || puddle || (soaked && rain > 0.2f)) {
            splashes.push_back({px, py, 0.0f, 0.5f, puddle ? 5.0f : 3.0f, true});
            splashes.push_back({px, py, 0.0f, 0.35f, 3.0f, false});
        }
    }
    if (prints.size() > 300) prints.erase(prints.begin(), prints.begin() + (prints.size() - 300));
}

int Ambience::PuddlesShown() const {
    if (!lively || wet <= 0.02f || !RainsHere()) return 0;
    return static_cast<int>(place.puddles.size());
}

bool Ambience::Aurora() const {
    return lively && place.frost && daylight < 0.35f;
}

void Ambience::DrawWet(SDL_Renderer* r, const Camera& cam) const {
    if (!lively) return;
    const float z = cam.zoom;
    const SDL_FRect view = cam.VisibleWorldRect(24.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Puddles: dark water standing in a dip, the sky pale in it along one
    // edge, and as big as the wet is.
    if (PuddlesShown() > 0) {
        const float k = std::clamp(wet, 0.0f, 1.0f);
        for (const SDL_FPoint& q : place.puddles) {
            if (q.x < view.x || q.x > view.x + view.w || q.y < view.y || q.y > view.y + view.h) continue;
            const float size = (7.0f + 9.0f * Hash01(q.x, q.y)) * (0.4f + 0.6f * k);
            const float rx = roundf(size * z / z) * z, ry = std::max(z, roundf(size * 0.42f) * z);
            const SDL_FPoint p = cam.ToScreen(q.x, q.y);
            const int rows = std::max(2, static_cast<int>(ry * 2.0f / z));
            for (int i = 0; i < rows; ++i) {
                const float t = (i + 0.5f) / rows * 2.0f - 1.0f;
                const float half = roundf(rx * std::sqrt(std::max(0.0f, 1.0f - t * t)) / z) * z;
                const float y = roundf((p.y + t * ry) / z) * z;
                const bool sky = i == 0 || (i == 1 && rows > 3);
                SDL_SetRenderDrawColor(r, sky ? 186 : 66, sky ? 206 : 90, sky ? 232 : 120, static_cast<Uint8>((sky ? 175.0f : 145.0f) * k));
                const SDL_FRect row = {roundf((p.x - half) / z) * z, y, half * 2.0f, z};
                SDL_RenderFillRect(r, &row);
            }
        }
    }
    // Rings on the water and in the puddles; the rain bouncing off the ground.
    for (const Splash& s : splashes) {
        if (s.x < view.x || s.x > view.x + view.w || s.y < view.y || s.y > view.y + view.h) continue;
        const float t = std::clamp(s.age / s.life, 0.0f, 1.0f);
        if (s.ring) {
            Ring(r, cam, s.x, s.y, 1.5f + s.size * t, {222, 238, 250, static_cast<Uint8>(190.0f * (1.0f - t))});
        } else if (s.size <= 1.0f) {
            // A drop hitting the ground: a little crown of it, and gone.
            const SDL_FPoint p = cam.ToScreen(s.x, s.y);
            SDL_SetRenderDrawColor(r, 222, 232, 246, static_cast<Uint8>(190.0f * (1.0f - t)));
            const float x0 = roundf(p.x / z) * z, y0 = roundf(p.y / z) * z;
            const SDL_FRect a = {x0 - z, y0 - z * roundf(t), z, z};
            const SDL_FRect b = {x0 + z, y0 - z * roundf(t), z, z};
            const SDL_FRect c = {x0, y0 - z - z * roundf(t), z, z};
            SDL_RenderFillRect(r, &a);
            SDL_RenderFillRect(r, &b);
            SDL_RenderFillRect(r, &c);
        } else {
            // Spray off a foot: four drops out and down.
            const SDL_FPoint p = cam.ToScreen(s.x, s.y);
            SDL_SetRenderDrawColor(r, 200, 214, 228, static_cast<Uint8>(200.0f * (1.0f - t)));
            for (int k = 0; k < 4; ++k) {
                const float a = 0.6f + k * 0.65f;
                const float out = s.size * t * 1.6f;
                const float up = 6.0f * t * (1.0f - t) * 4.0f;
                const SDL_FRect d = {roundf((p.x + cosf(a) * out * z * (k % 2 ? 1.0f : -1.0f)) / z) * z,
                                     roundf((p.y - up * z) / z) * z, z, z};
                SDL_RenderFillRect(r, &d);
            }
        }
    }
}

void Ambience::DrawPrints(SDL_Renderer* r, const Camera& cam) const {
    if (prints.empty()) return;
    const float z = cam.zoom;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Print& p : prints) {
        // Fresh and plain, and filling in over the last third of its time.
        const float fade = std::clamp((p.life - p.age) / (p.life * 0.33f), 0.0f, 1.0f);
        SDL_Color c{0, 0, 0, 0};
        switch (p.on) {
            case FOOT_SNOW: c = {132, 156, 196, 150}; break;
            case FOOT_SAND: c = {150, 120, 78, 110}; break;
            case FOOT_SALT: c = {150, 150, 156, 120}; break;
            case FOOT_MUD:  c = {58, 44, 32, 150}; break;
            case FOOT_ASH:  c = {22, 18, 18, 190}; break;
            default:        c = {80, 64, 48, 110}; break;
        }
        const SDL_FPoint s = cam.ToScreen(p.x, p.y);
        // A print is a heel and a toe along the way they were walking, the toe
        // the next pixel on.
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, static_cast<Uint8>(c.a * fade));
        const float hx = roundf(s.x / z) * z, hy = roundf(s.y / z) * z;
        const SDL_FRect heel = {hx, hy, z, z};
        const bool across = fabsf(p.dx) >= fabsf(p.dy);
        const SDL_FRect toe = {hx + (across ? (p.dx > 0.0f ? z : -z) : 0.0f), hy + (across ? 0.0f : (p.dy > 0.0f ? z : -z)), z, z};
        SDL_RenderFillRect(r, &heel);
        SDL_RenderFillRect(r, &toe);
        // In snow, the edge the light catches.
        if (p.on == FOOT_SNOW) {
            SDL_SetRenderDrawColor(r, 252, 254, 255, static_cast<Uint8>(110.0f * fade));
            const SDL_FRect lip = {hx, hy + z, z, z};
            SDL_RenderFillRect(r, &lip);
        }
    }
}

void Ambience::DrawBreath(SDL_Renderer* r, const Camera& cam) const {
    if (breath.empty()) return;
    const float z = cam.zoom;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Puff& p : breath) {
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float size = roundf((1.5f + 2.5f * t) * z);
        const SDL_FPoint s = cam.ToScreen(p.x, p.y);
        if (p.dark) SDL_SetRenderDrawColor(r, 30, 18, 46, static_cast<Uint8>(170.0f * (1.0f - t)));
        else        SDL_SetRenderDrawColor(r, 226, 234, 246, static_cast<Uint8>(170.0f * (1.0f - t) * (1.0f - t) * (0.6f + 0.4f * daylight)));
        const SDL_FRect q = {roundf((s.x - size / 2.0f) / z) * z, roundf((s.y - size / 2.0f) / z) * z, size, size};
        SDL_RenderFillRect(r, &q);
    }
}

void Ambience::DrawAurora(SDL_Renderer* r) const {
    if (!Aurora()) return;
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    if (w <= 0 || h <= 0) return;
    const float k = std::clamp((0.35f - daylight) / 0.3f, 0.0f, 1.0f);
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    // Curtains of green and violet light hanging over the upper part of the
    // view, their lower edges rippling, drifting slowly along.
    const int strip = std::max(2, w / 240);
    struct Band { float top, length, speed, phase; SDL_Color c; };
    const Band bands[] = {{0.04f, 0.30f, 0.11f, 0.0f, {60, 210, 140, 255}},
                          {0.10f, 0.24f, -0.08f, 2.1f, {70, 190, 200, 255}},
                          {0.02f, 0.18f, 0.06f, 4.2f, {150, 90, 220, 255}}};
    for (const Band& b : bands)
        for (int x = 0; x < w; x += strip) {
            const float u = static_cast<float>(x) / w;
            const float wave = sinf(u * 9.0f + now * b.speed * 6.0f + b.phase) * 0.5f + sinf(u * 23.0f - now * 0.7f + b.phase) * 0.25f;
            const float bright = std::clamp(0.55f + 0.45f * sinf(u * 5.0f + now * b.speed * 3.0f + b.phase * 1.7f), 0.0f, 1.0f);
            const float y0 = (b.top + 0.04f * wave) * h;
            const float len = (b.length * (0.7f + 0.3f * bright)) * h;
            const int steps = 6;
            for (int s = 0; s < steps; ++s) {
                // Brightest at its lower edge, fading up into the dark.
                const float f = static_cast<float>(s + 1) / steps;
                SDL_SetRenderDrawColor(r, b.c.r, b.c.g, b.c.b, static_cast<Uint8>(52.0f * k * bright * f * f));
                const SDL_FRect seg = {static_cast<float>(x), y0 + len * s / steps, static_cast<float>(strip), len / steps + 1.0f};
                SDL_RenderFillRect(r, &seg);
            }
        }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}
