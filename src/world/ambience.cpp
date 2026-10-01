#include "ambience.h"
#include "../systems/shaders.h"

namespace {
float Rand01(std::mt19937& rng) { return (rng() % 10000u) / 10000.0f; }
float Range(std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * Rand01(rng); }
}

void Ambience::SetKind(const string& ambient, bool interior) {
    if (ambient == "dungeon")     kind = Kind::Dungeon;
    else if (ambient == "dream")  kind = Kind::Dream;
    else if (ambient == "snow")   kind = Kind::Snow;
    else if (ambient == "ash")    kind = Kind::Ash;
    else if (ambient == "deep")   kind = Kind::Deep;
    else if (ambient == "gale")   kind = Kind::Gale;
    else if (ambient == "storm")  kind = Kind::Storm;
    else if (ambient == "conflux") kind = Kind::Conflux;
    else if (interior)            kind = Kind::None;
    else if (ambient == "forest") kind = Kind::Forest;
    else if (ambient == "grove")  kind = Kind::Grove;
    else if (ambient == "town")   kind = Kind::Town;
    else                          kind = Kind::Field;

    // A new map is a new view; motes from the last one would be stranded
    // wherever the camera used to be.
    motes.clear();
    seeded = false;
    gust = 0.0f;
    gust_age = -1.0f;
    gust_wait = 7.0f;
    flash = 0.0f;
    flash_age = -1.0f;
    flash_wait = 4.0f;
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
        case SNOW:
            // Flakes on the mountain wind: slanting down, a few big and near.
            m.color = {236, 244, 255, static_cast<Uint8>(Range(rng, 170.0f, 240.0f))};
            m.vx    = Range(rng, -22.0f, -8.0f);
            m.vy    = Range(rng, 26.0f, 46.0f);
            m.speed = Range(rng, 1.0f, 2.0f);
            m.size  = Range(rng, 1.0f, 2.2f);
            break;
        case FLURRY:
            // Driven snow, low and fast: only seen while a gust is blowing.
            m.color = {240, 246, 255, static_cast<Uint8>(Range(rng, 140.0f, 220.0f))};
            m.vx    = Range(rng, -300.0f, -190.0f);
            m.vy    = Range(rng, 8.0f, 34.0f);
            m.speed = Range(rng, 1.5f, 3.0f);
            m.size  = Range(rng, 1.0f, 1.6f);
            break;
        case EMBER:
            // Sparks lifting off the burning ground, with grey ash drifting.
            m.color = (rng() % 3) ? SDL_Color{255, 150, 60, 255} : SDL_Color{150, 140, 136, 200};
            m.vx    = Range(rng, -6.0f, 6.0f);
            m.vy    = Range(rng, -24.0f, -8.0f);
            m.speed = Range(rng, 1.0f, 2.2f);
            m.size  = Range(rng, 1.0f, 1.8f);
            break;
        case WISP: {
            // Motes of dream rising out of the void: violet, rose and a pale
            // cyan, glowing and fading as they climb. In the Conflux, one of
            // each of the five: ember, amber, sea, cloud and storm.
            static const SDL_Color kWisps[] = {
                {196, 150, 255, 255}, {255, 160, 220, 255}, {150, 230, 255, 255}};
            static const SDL_Color kPrimal[] = {
                {255, 140, 50, 255}, {240, 180, 70, 255}, {90, 190, 240, 255}, {236, 242, 255, 255}, {255, 240, 130, 255}};
            m.color = kind == Kind::Conflux ? kPrimal[rng() % 5] : kWisps[rng() % 3];
            m.vx    = Range(rng, -3.0f, 3.0f);
            m.vy    = Range(rng, -14.0f, -5.0f);
            m.speed = Range(rng, 0.7f, 1.5f);
            m.size  = Range(rng, 1.0f, 2.0f);
            break;
        }
        case BUBBLE:
            // Up out of the sea-floor, wobbling as they go: a pale ring.
            m.color = {190, 236, 250, static_cast<Uint8>(Range(rng, 110.0f, 190.0f))};
            m.vx    = Range(rng, -2.0f, 2.0f);
            m.vy    = Range(rng, -26.0f, -12.0f);
            m.speed = Range(rng, 1.2f, 2.4f);
            m.size  = Range(rng, 1.0f, 2.5f);
            break;
        case WIND:
            // A thread of moving air, long and faint, across the Firmament.
            m.color = {240, 246, 255, static_cast<Uint8>(Range(rng, 50.0f, 110.0f))};
            m.vx    = Range(rng, -150.0f, -90.0f);
            m.vy    = Range(rng, -4.0f, 6.0f);
            m.speed = Range(rng, 0.6f, 1.4f);
            m.size  = Range(rng, 4.0f, 9.0f);
            break;
        case RAIN:
            // Slanting down hard.
            m.color = {176, 190, 222, static_cast<Uint8>(Range(rng, 90.0f, 160.0f))};
            m.vx    = Range(rng, -70.0f, -50.0f);
            m.vy    = Range(rng, 260.0f, 340.0f);
            m.speed = 1.0f;
            m.size  = Range(rng, 3.0f, 5.0f);
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
        case Kind::Dream:   break;
        case Kind::Snow:    break;
        case Kind::Ash:     dust = 10; break;
        case Kind::Deep:    dust = 12; break;
        case Kind::Gale:    break;
        case Kind::Storm:   break;
        case Kind::Conflux: dust = 8; break;
        case Kind::None:    break;
    }
    motes.clear();
    for (int i = 0; i < leaves; ++i) motes.push_back(Make(LEAF, view));
    for (int i = 0; i < flies; ++i)  motes.push_back(Make(FIREFLY, view));
    for (int i = 0; i < pollen; ++i) motes.push_back(Make(POLLEN, view));
    for (int i = 0; i < dust; ++i)   motes.push_back(Make(DUST, view));
    if (kind == Kind::Dream)
        for (int i = 0; i < 60; ++i) motes.push_back(Make(WISP, view));
    if (kind == Kind::Snow) {
        for (int i = 0; i < 90; ++i) motes.push_back(Make(SNOW, view));
        for (int i = 0; i < 80; ++i) motes.push_back(Make(FLURRY, view));
    }
    if (kind == Kind::Ash)
        for (int i = 0; i < 46; ++i) motes.push_back(Make(EMBER, view));
    if (kind == Kind::Deep)
        for (int i = 0; i < 54; ++i) motes.push_back(Make(BUBBLE, view));
    if (kind == Kind::Gale)
        for (int i = 0; i < 70; ++i) motes.push_back(Make(WIND, view));
    if (kind == Kind::Storm)
        for (int i = 0; i < 150; ++i) motes.push_back(Make(RAIN, view));
    if (kind == Kind::Conflux)
        for (int i = 0; i < 64; ++i) motes.push_back(Make(WISP, view));
}

void Ambience::Update(float dt, const Camera& cam) {
    if (kind == Kind::None) return;
    const SDL_FRect view = cam.VisibleWorldRect(0.0f);
    if (!seeded) {
        Populate(view);
        seeded = true;
    }

    // The wind, on the mountain and over the Firmament: a gust every so often,
    // rising and dying away.
    if (kind == Kind::Snow || kind == Kind::Gale) {
        if (gust_age < 0.0f) {
            gust_wait -= dt;
            if (gust_wait <= 0.0f) {
                gust_age = 0.0f;
                gust_len = Range(rng, 5.0f, 9.0f);
            }
        } else if ((gust_age += dt) >= gust_len) {
            gust_age = -1.0f;
            gust_wait = Range(rng, 14.0f, 30.0f);
        }
        gust = gust_age < 0.0f ? 0.0f
                               : std::clamp(std::min(gust_age / GUST_RISE, (gust_len - gust_age) / GUST_FALL), 0.0f, 1.0f);
    }

    // The Tempest's lightning: one strike every few seconds, and now and then
    // a second hard on its heels.
    if (kind == Kind::Storm) {
        if (flash_age < 0.0f) {
            flash_wait -= dt;
            if (flash_wait <= 0.0f) flash_age = 0.0f;
        } else if ((flash_age += dt) >= FLASH_TIME) {
            flash_age = -1.0f;
            flash_wait = Rand01(rng) < 0.3f ? Range(rng, 0.15f, 0.35f) : Range(rng, 5.0f, 12.0f);
        }
        flash = flash_age < 0.0f ? 0.0f : 1.0f - flash_age / FLASH_TIME;
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
            case WISP:
                m.x += (m.vx + sinf(m.phase) * 6.0f) * dt;
                m.y += m.vy * dt;
                break;
            case SNOW:
                m.x += (m.vx - 150.0f * gust + sinf(m.phase) * 9.0f) * dt;
                m.y += m.vy * (1.0f - 0.35f * gust) * dt;
                break;
            case FLURRY:
                m.x += m.vx * (0.35f + 0.65f * gust) * dt;
                m.y += (m.vy + sinf(m.phase) * 12.0f) * dt;
                break;
            case EMBER:
                m.x += (m.vx + sinf(m.phase) * 7.0f) * dt;
                m.y += m.vy * dt;
                break;
            case BUBBLE:
                m.x += (m.vx + sinf(m.phase * 2.0f) * 5.0f) * dt;
                m.y += m.vy * dt;
                break;
            case WIND:
                m.x += m.vx * (0.6f + 0.9f * gust) * dt;
                m.y += (m.vy + sinf(m.phase) * 5.0f) * dt;
                break;
            case RAIN:
                m.x += m.vx * dt;
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
            case WISP: {
                const float glow = 0.55f + 0.45f * sinf(m.phase * 1.7f);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(r, m.color.r / 3, m.color.g / 3, m.color.b / 3,
                                       static_cast<Uint8>(120.0f * glow));
                const float g = roundf((m.size + 2.0f) * z);
                const SDL_FRect halo = {roundf(p.x - g / 2.0f), roundf(p.y - g / 2.0f), g, g};
                SDL_RenderFillRect(r, &halo);
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b,
                                       static_cast<Uint8>(230.0f * glow));
                const float c = std::max(1.0f, roundf(m.size * z));
                const SDL_FRect core = {roundf(p.x - c / 2.0f), roundf(p.y - c / 2.0f), c, c};
                SDL_RenderFillRect(r, &core);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                break;
            }
            case EMBER: {
                const float glow = 0.6f + 0.4f * sinf(m.phase * 2.1f);
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, static_cast<Uint8>(m.color.a * glow));
                const float s = std::max(1.0f, roundf(m.size * z));
                const SDL_FRect q = {roundf(p.x - s / 2.0f), roundf(p.y - s / 2.0f), s, s};
                SDL_RenderFillRect(r, &q);
                break;
            }
            case FLURRY: {
                // A streak along the wind, as long as the gust is strong.
                const Uint8 a = static_cast<Uint8>(m.color.a * gust);
                if (a < 8) break;
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, a);
                const float len = roundf((3.0f + 6.0f * gust) * z);
                const float h = std::max(1.0f, roundf(m.size * z * 0.8f));
                const SDL_FRect q = {roundf(p.x - len / 2.0f), roundf(p.y), len, h};
                SDL_RenderFillRect(r, &q);
                break;
            }
            case BUBBLE: {
                // A pale fleck; the bigger ones a ring, their middles left dark.
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, m.color.a);
                const float s = std::max(1.0f, roundf(m.size * z));
                const SDL_FRect q = {roundf(p.x - s / 2.0f), roundf(p.y - s / 2.0f), s, s};
                SDL_RenderFillRect(r, &q);
                if (m.size >= 2.0f) {
                    SDL_SetRenderDrawColor(r, 20, 60, 80, 90);
                    const float c = std::max(1.0f, roundf(s / 3.0f));
                    const SDL_FRect in = {roundf(p.x - c / 2.0f), roundf(p.y - c / 2.0f), c, c};
                    SDL_RenderFillRect(r, &in);
                }
                break;
            }
            case WIND: {
                // A streak along the wind, longer as it gusts.
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b,
                                       static_cast<Uint8>(std::min(255.0f, m.color.a * (0.7f + 0.8f * gust))));
                const float len = roundf(m.size * (1.0f + gust) * z);
                const float h = std::max(1.0f, roundf(z * 0.5f));
                const SDL_FRect q = {roundf(p.x - len / 2.0f), roundf(p.y), len, h};
                SDL_RenderFillRect(r, &q);
                break;
            }
            case RAIN: {
                // A short slanted streak: three steps down its slope.
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, m.color.a);
                const float s = std::max(1.0f, roundf(z * 0.5f));
                const float step = std::max(1.0f, roundf(m.size * z * 0.34f));
                for (int k = 0; k < 3; ++k) {
                    const SDL_FRect q = {roundf(p.x - k * s), roundf(p.y + k * step), s, step};
                    SDL_RenderFillRect(r, &q);
                }
                break;
            }
            case SNOW:
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

    // A gust whitens the whole view a little: the air thick with snow.
    if (kind == Kind::Snow && gust > 0.02f) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r, &w, &h);
        SDL_SetRenderDrawColor(r, 236, 244, 255, static_cast<Uint8>(52.0f * gust));
        const SDL_FRect all = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(r, &all);
    }

    // Lightning: the whole view white for a moment, and gone -- unless the
    // player has asked for no flashes (Options, Visual Effects), whose help
    // names the lightning; or for the plain look, which has none of it.
    const Shaders::Options& look = Shaders::GetOptions();
    if (kind == Kind::Storm && flash > 0.02f && look.effects && look.flashes) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r, &w, &h);
        SDL_SetRenderDrawColor(r, 236, 240, 255, static_cast<Uint8>(120.0f * flash * flash));
        const SDL_FRect all = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(r, &all);
    }
    // A gust over the Firmament pales the view a little, as the snow does.
    if (kind == Kind::Gale && gust > 0.02f) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r, &w, &h);
        SDL_SetRenderDrawColor(r, 240, 246, 255, static_cast<Uint8>(28.0f * gust));
        const SDL_FRect all = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(r, &all);
    }

    // The vignette: the edges of the screen darken under a canopy or
    // underground, which does more for "you are in a forest" than any sprite.
    float strength = 0.0f;
    SDL_Color tint{6, 16, 8, 255};
    if (kind == Kind::Forest)       strength = 1.0f;
    else if (kind == Kind::Grove)   strength = 0.4f;
    else if (kind == Kind::Dungeon) { strength = 1.25f; tint = {0, 0, 0, 255}; }
    else if (kind == Kind::Dream)   { strength = 1.1f;  tint = {26, 8, 46, 255}; }
    else if (kind == Kind::Snow)    { strength = 0.7f;  tint = {210, 226, 240, 255}; }
    else if (kind == Kind::Ash)     { strength = 0.9f;  tint = {60, 12, 6, 255}; }
    else if (kind == Kind::Deep)    { strength = 1.0f;  tint = {4, 24, 40, 255}; }
    else if (kind == Kind::Gale)    { strength = 0.5f;  tint = {200, 214, 236, 255}; }
    else if (kind == Kind::Storm)   { strength = 1.1f;  tint = {12, 12, 26, 255}; }
    else if (kind == Kind::Conflux) { strength = 1.0f;  tint = {30, 16, 46, 255}; }
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
