#include "ambience.h"

namespace {
float Rand01(std::mt19937& rng) { return (rng() % 10000u) / 10000.0f; }
float Range(std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * Rand01(rng); }
}

void Ambience::SetKind(const string& ambient, bool interior) {
    if (ambient == "dungeon")     kind = Kind::Dungeon;
    else if (interior)            kind = Kind::None;
    else if (ambient == "forest") kind = Kind::Forest;
    else if (ambient == "grove")  kind = Kind::Grove;
    else if (ambient == "town")   kind = Kind::Town;
    else                          kind = Kind::Field;

    // A new map is a new view; motes from the last one would be stranded
    // wherever the camera used to be.
    motes.clear();
    seeded = false;
}

Ambience::Mote Ambience::Make(MoteKind k, const SDL_FRect& view) {
    Mote m;
    m.kind  = k;
    m.x     = Range(rng, view.x, view.x + view.w);
    m.y     = Range(rng, view.y, view.y + view.h);
    m.phase = Range(rng, 0.0f, 6.2831853f);

    switch (k) {
        case LEAF: {
            static const SDL_Color kLeaves[] = {
                {206, 122, 52, 255}, {196, 164, 64, 255}, {120, 150, 62, 255},
                {150, 92, 50, 255},  {176, 72, 44, 255}};
            m.color = kLeaves[rng() % 5];
            m.vx    = Range(rng, 4.0f, 14.0f);
            m.vy    = Range(rng, 16.0f, 28.0f);
            m.speed = Range(rng, 1.4f, 2.4f);
            m.size  = Range(rng, 2.0f, 3.0f);
            break;
        }
        case FIREFLY:
            m.speed = Range(rng, 0.8f, 1.6f);
            m.size  = 1.0f;
            break;
        case POLLEN:
            m.color = {244, 238, 204, static_cast<Uint8>(Range(rng, 110.0f, 180.0f))};
            m.vx    = Range(rng, 3.0f, 9.0f);
            m.vy    = Range(rng, -2.0f, 2.0f);
            m.speed = Range(rng, 0.6f, 1.2f);
            m.size  = 1.0f;
            break;
        case DUST:
            m.color = {196, 188, 176, static_cast<Uint8>(Range(rng, 40.0f, 90.0f))};
            m.vy    = Range(rng, -5.0f, -1.5f);
            m.speed = Range(rng, 0.3f, 0.8f);
            m.size  = 1.0f;
            break;
    }
    return m;
}

void Ambience::Populate(const SDL_FRect& view) {
    int leaves = 0, flies = 0, pollen = 0, dust = 0;
    switch (kind) {
        case Kind::Forest:  leaves = 42; flies = 14; break;
        case Kind::Grove:   leaves = 16; flies = 10; break;
        case Kind::Field:   pollen = 18; break;
        case Kind::Town:    pollen = 10; break;
        case Kind::Dungeon: dust = 44; break;
        case Kind::None:    break;
    }
    motes.clear();
    for (int i = 0; i < leaves; ++i) motes.push_back(Make(LEAF, view));
    for (int i = 0; i < flies; ++i)  motes.push_back(Make(FIREFLY, view));
    for (int i = 0; i < pollen; ++i) motes.push_back(Make(POLLEN, view));
    for (int i = 0; i < dust; ++i)   motes.push_back(Make(DUST, view));
}

void Ambience::Update(float dt, const Camera& cam) {
    if (kind == Kind::None) return;
    const SDL_FRect view = cam.VisibleWorldRect(0.0f);
    if (!seeded) {
        Populate(view);
        seeded = true;
    }

    const float margin = 40.0f;
    for (Mote& m : motes) {
        m.phase += dt * m.speed;
        switch (m.kind) {
            case LEAF:
                // Falling, and swaying side to side as it turns over.
                m.x += (m.vx + sinf(m.phase) * 16.0f) * dt;
                m.y += m.vy * dt;
                break;
            case FIREFLY:
                m.x += cosf(m.phase * 0.7f) * 10.0f * dt;
                m.y += sinf(m.phase * 0.9f) * 8.0f * dt;
                break;
            case POLLEN:
                m.x += (m.vx + sinf(m.phase) * 4.0f) * dt;
                m.y += (m.vy + cosf(m.phase * 0.8f) * 3.0f) * dt;
                break;
            case DUST:
                m.x += sinf(m.phase) * 3.0f * dt;
                m.y += m.vy * dt;
                break;
        }

        // Anything that leaves the view comes back in on the opposite side,
        // just out of sight -- so walking along a trail keeps the air full
        // instead of leaving the leaves behind, and nothing pops into being
        // in the middle of the screen.
        const float left = view.x - margin, right = view.x + view.w + margin;
        const float top = view.y - margin, bottom = view.y + view.h + margin;
        if (m.x < left)   { m.x = right - Range(rng, 0.0f, margin); m.y = Range(rng, view.y, view.y + view.h); }
        if (m.x > right)  { m.x = left + Range(rng, 0.0f, margin);  m.y = Range(rng, view.y, view.y + view.h); }
        if (m.y < top)    { m.y = bottom - Range(rng, 0.0f, margin); m.x = Range(rng, view.x, view.x + view.w); }
        if (m.y > bottom) { m.y = top + Range(rng, 0.0f, margin);    m.x = Range(rng, view.x, view.x + view.w); }
    }
}

void Ambience::Render(SDL_Renderer* r, const Camera& cam) const {
    if (kind == Kind::None) return;
    const float z = cam.zoom;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Mote& m : motes) {
        const SDL_FPoint p = cam.ToScreen(m.x, m.y);
        switch (m.kind) {
            case LEAF: {
                // Its width breathes with the sway, which is what reads as a
                // leaf turning in the air rather than a square falling.
                const float w = std::max(1.0f, roundf(m.size * z * (0.45f + 0.55f * fabsf(cosf(m.phase)))));
                const float h = std::max(1.0f, roundf(m.size * z * 0.7f));
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, 225);
                const SDL_FRect q = {roundf(p.x - w / 2.0f), roundf(p.y - h / 2.0f), w, h};
                SDL_RenderFillRect(r, &q);
                break;
            }
            case FIREFLY: {
                const float glow = 0.5f + 0.5f * sinf(m.phase * 2.3f);
                if (glow < 0.2f) break;
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(r, 110, 140, 40, static_cast<Uint8>(80.0f * glow));
                const float g = roundf(3.0f * z);
                const SDL_FRect halo = {roundf(p.x - g / 2.0f), roundf(p.y - g / 2.0f), g, g};
                SDL_RenderFillRect(r, &halo);
                SDL_SetRenderDrawColor(r, 230, 250, 150, static_cast<Uint8>(255.0f * glow));
                const float c = std::max(1.0f, roundf(z));
                const SDL_FRect core = {roundf(p.x - c / 2.0f), roundf(p.y - c / 2.0f), c, c};
                SDL_RenderFillRect(r, &core);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                break;
            }
            case POLLEN:
            case DUST: {
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, m.color.a);
                const float s = std::max(1.0f, roundf(m.size * z));
                const SDL_FRect q = {roundf(p.x - s / 2.0f), roundf(p.y - s / 2.0f), s, s};
                SDL_RenderFillRect(r, &q);
                break;
            }
        }
    }

    // The vignette: the edges of the screen darken under a canopy or
    // underground, which does more for "you are in a forest" than any sprite.
    float strength = 0.0f;
    SDL_Color tint{6, 16, 8, 255};
    if (kind == Kind::Forest)       strength = 1.0f;
    else if (kind == Kind::Grove)   strength = 0.4f;
    else if (kind == Kind::Dungeon) { strength = 1.25f; tint = {0, 0, 0, 255}; }
    if (strength <= 0.0f) return;

    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    if (w <= 0 || h <= 0) return;

    const float band = std::min(w, h) * 0.22f;
    const int steps = 14;
    const float step = band / steps;
    for (int i = 0; i < steps; ++i) {
        const float t = 1.0f - static_cast<float>(i) / steps;         // 1 at the very edge
        const Uint8 a = static_cast<Uint8>(std::min(255.0f, 110.0f * strength * t * t));
        SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, a);
        const float d = i * step;
        const SDL_FRect top    = {0.0f, d, static_cast<float>(w), step};
        const SDL_FRect bottom = {0.0f, h - d - step, static_cast<float>(w), step};
        const SDL_FRect left   = {d, 0.0f, step, static_cast<float>(h)};
        const SDL_FRect right  = {w - d - step, 0.0f, step, static_cast<float>(h)};
        SDL_RenderFillRect(r, &top);
        SDL_RenderFillRect(r, &bottom);
        SDL_RenderFillRect(r, &left);
        SDL_RenderFillRect(r, &right);
    }
}
