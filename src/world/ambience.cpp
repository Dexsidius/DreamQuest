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
    birds.clear();
    land_wait = -1.0f;   // a few already about when the map is come into: see UpdateBirds
    flock_wait = 9.0f;
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
            m.vy    = (kind == Kind::Forest || kind == Kind::Grove) ? Range(rng, 16.0f, 28.0f) : Range(rng, 2.0f, 9.0f);
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
        case STREAK:
            // A breath of the wind made visible: a short pale line along it,
            // fading in and out, quickest and clearest as a gust goes by.
            m.color = {250, 252, 255, static_cast<Uint8>(Range(rng, 60.0f, 110.0f))};
            m.speed = Range(rng, 170.0f, 240.0f);
            m.size  = Range(rng, 7.0f, 13.0f);
            m.life  = Range(rng, 0.8f, 1.5f);
            m.age   = Range(rng, 0.0f, m.life);
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
    int leaves = 0, flies = 0, pollen = 0, dust = 0, streaks = 0;
    switch (kind) {
        case Kind::Forest:  leaves = 42; flies = 14; streaks = 5; break;
        case Kind::Grove:   leaves = 18; flies = 10; streaks = 10; break;
        case Kind::Field:   pollen = 18; leaves = 9; streaks = 12; break;
        case Kind::Town:    pollen = 10; leaves = 6; streaks = 8; break;
        case Kind::Dungeon: dust = 44; break;
        case Kind::Dream:   break;
        case Kind::Snow:    break;
        case Kind::Ash:     dust = 10; streaks = 6; break;
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
    for (int i = 0; i < streaks; ++i) motes.push_back(Make(STREAK, view));
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

void Ambience::Update(float dt, const Camera& cam, const World& world) {
    daylight = world.daylight;
    lively = world.lively;
    if (kind == Kind::None) return;
    const SDL_FRect view = cam.VisibleWorldRect(0.0f);
    const SDL_FPoint wd = Shaders::WindDirection();
    // In a forest the trees take the edge off it.
    const float blow = Shaders::OutdoorWind() * (kind == Kind::Forest ? 0.6f : 1.0f);
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
            case LEAF: {
                // Falling, swaying side to side as it turns over, and carried
                // along the wind -- tumbling faster as a gust reaches it.
                const float push = (20.0f + 90.0f * Shaders::GustAt(m.x, m.y)) * blow;
                m.x += (m.vx + sinf(m.phase) * 16.0f + wd.x * push) * dt;
                m.y += (m.vy + wd.y * push) * dt;
                break;
            }
            case STREAK: {
                const float g = Shaders::GustAt(m.x, m.y);
                m.x += wd.x * m.speed * (0.55f + 0.7f * g) * blow * dt;
                m.y += (wd.y * m.speed * (0.55f + 0.7f * g) * blow + sinf(m.phase * 3.0f) * 4.0f) * dt;
                if ((m.age += dt) >= m.life) {
                    m.age = 0.0f;
                    m.x = Range(rng, view.x, view.x + view.w);
                    m.y = Range(rng, view.y, view.y + view.h);
                }
                break;
            }
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

    UpdateBirds(dt, view, world);
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
                // Its shadow on the ground below it, and then the leaf: the
                // paler underside as it turns over.
                SDL_SetRenderDrawColor(r, 10, 20, 10, 46);
                const SDL_FRect sh = {roundf(p.x - w / 2.0f + z), roundf(p.y - h / 2.0f + 4.0f * z), w, h};
                SDL_RenderFillRect(r, &sh);
                const bool under = cosf(m.phase) < 0.0f;
                SDL_SetRenderDrawColor(r, under ? static_cast<Uint8>(std::min(255, m.color.r + 30)) : m.color.r,
                                       under ? static_cast<Uint8>(std::min(255, m.color.g + 30)) : m.color.g,
                                       under ? static_cast<Uint8>(std::min(255, m.color.b + 20)) : m.color.b, 225);
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
            case STREAK: {
                if (!lively || m.life <= 0.0f) break;
                const float g = Shaders::GustAt(m.x, m.y);
                const float a = m.color.a * sinf(3.14159265f * std::clamp(m.age / m.life, 0.0f, 1.0f)) *
                                (0.25f + 0.75f * g) * (0.35f + 0.65f * daylight);
                if (a < 6.0f) break;
                const SDL_FPoint wd = Shaders::WindDirection();
                const float len = (m.size + 8.0f * g) * z;
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, static_cast<Uint8>(a));
                SDL_RenderLine(r, p.x - wd.x * len, p.y - wd.y * len, p.x, p.y);
                SDL_SetRenderDrawColor(r, m.color.r, m.color.g, m.color.b, static_cast<Uint8>(a * 0.45f));
                SDL_RenderLine(r, p.x - wd.x * len * 0.7f, p.y - wd.y * len * 0.7f + 1.0f, p.x, p.y + 1.0f);
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
    // A Cozy night out of doors: the edges close in, dusk-blue.
    if (dusk > 0.0f && dusk * 0.85f > strength) { strength = dusk * 0.85f; tint = {12, 14, 34, 255}; }
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

// --- birds ------------------------------------------------------------------------------------

int Ambience::PickSpecies(bool flock) {
    const float roll = Rand01(rng);
    switch (kind) {
        case Kind::Field:  return roll < (flock ? 0.5f : 0.65f) ? SPARROW : CROW;
        case Kind::Town:   return flock ? SPARROW : (roll < 0.8f ? SPARROW : CROW);
        case Kind::Forest: return flock ? CROW : (roll < 0.5f ? SPARROW : CROW);
        case Kind::Grove:  return roll < 0.45f ? EGRET : roll < 0.8f ? SPARROW : CROW;
        default:           return CROW;   // the snow and the ash: crows, and only crows
    }
}

void Ambience::UpdateBirds(float dt, const SDL_FRect& view, const World& world) {
    int target = 0;
    switch (kind) {
        case Kind::Field:  target = 6; break;
        case Kind::Town:   target = 4; break;
        case Kind::Forest: target = 3; break;
        case Kind::Grove:  target = 5; break;
        case Kind::Snow:   target = 2; break;
        case Kind::Ash:    target = 2; break;
        default:           target = 0; break;
    }
    const bool day = daylight > 0.45f;
    const auto near_walker = [&](float x, float y, float r) {
        for (const SDL_FPoint& w : world.walkers)
            if ((w.x - x) * (w.x - x) + (w.y - y) * (w.y - y) < r * r) return true;
        return false;
    };
    const float cx = view.x + view.w * 0.5f, cy = view.y + view.h * 0.5f;
    const float reach = std::max(view.w, view.h) * 0.75f + 60.0f;

    // A new map: a few are already about on the ground, out of the way.
    if (land_wait < 0.0f) {
        land_wait = Range(rng, 2.0f, 4.0f);
        if (lively && target > 0 && day && world.stand)
            for (int attempt = 0, placed = 0; attempt < 40 && placed < (target + 1) / 2; ++attempt) {
                const float x = Range(rng, view.x + 30.0f, view.x + view.w - 30.0f);
                const float y = Range(rng, view.y + 30.0f, view.y + view.h - 30.0f);
                if (near_walker(x, y, 150.0f) || !world.stand(x, y)) continue;
                Bird b;
                b.species = PickSpecies(false);
                b.x = x; b.y = y;
                b.until = Range(rng, 0.3f, 2.0f);
                b.left = Rand01(rng) < 0.5f;
                birds.push_back(b);
                ++placed;
            }
    }

    // Down out of the sky onto open ground, a few at a time, out of sight of
    // whoever is near.
    if (lively && target > 0 && day && world.stand && (land_wait -= dt) <= 0.0f) {
        land_wait = Range(rng, 2.5f, 5.0f);
        int down = 0;
        for (const Bird& b : birds) down += b.state != Bird::PASSING && b.state != Bird::FLEEING;
        if (down < target)
            for (int attempt = 0; attempt < 12; ++attempt) {
                const float tx = Range(rng, view.x + 40.0f, view.x + view.w - 40.0f);
                const float ty = Range(rng, view.y + 40.0f, view.y + view.h - 40.0f);
                if (near_walker(tx, ty, 150.0f) || !world.stand(tx, ty)) continue;
                const int n = 1 + static_cast<int>(rng() % 3u);
                const int species = PickSpecies(false);
                const float a = Range(rng, 0.0f, 6.2831853f);
                const float from = std::max(view.w, view.h) * 0.55f + 40.0f;   // just out of sight
                const float sx = tx - cosf(a) * from, sy = ty - sinf(a) * from;
                for (int k = 0; k < n; ++k) {
                    Bird b;
                    b.state = Bird::LANDING;
                    b.species = species;
                    b.tx = tx + Range(rng, -18.0f, 18.0f);
                    b.ty = ty + Range(rng, -12.0f, 12.0f);
                    if (!world.stand(b.tx, b.ty)) { b.tx = tx; b.ty = ty; }
                    b.x = sx + Range(rng, -20.0f, 20.0f);
                    b.y = sy + Range(rng, -20.0f, 20.0f);
                    b.h = Range(rng, 70.0f, 100.0f);
                    b.left = b.tx < b.x;
                    b.flap = Range(rng, 0.0f, 1.0f);
                    birds.push_back(b);
                }
                break;
            }
    }

    // Now and then a flock going over, high, more or less with the wind.
    if (lively && target > 0 && day && (flock_wait -= dt) <= 0.0f) {
        flock_wait = Range(rng, 16.0f, 32.0f);
        const SDL_FPoint wd = Shaders::WindDirection();
        float heading = atan2f(wd.y, wd.x) + Range(rng, -1.1f, 1.1f);
        if (Rand01(rng) < 0.3f) heading += 3.14159265f;
        const float dx = cosf(heading), dy = sinf(heading);
        const float side = Range(rng, -view.h * 0.3f, view.h * 0.3f);
        const float sx = cx - dx * (reach + 80.0f) - dy * side, sy = cy - dy * (reach + 80.0f) + dx * side;
        const int n = 3 + static_cast<int>(rng() % 5u);
        const int species = PickSpecies(true);
        const float speed = Range(rng, 85.0f, 110.0f), height = Range(rng, 110.0f, 150.0f);
        for (int k = 0; k < n; ++k) {
            // A loose V behind the first.
            const float back = static_cast<float>((k + 1) / 2) * Range(rng, 12.0f, 18.0f);
            const float out = static_cast<float>((k + 1) / 2) * Range(rng, 8.0f, 12.0f) * (k % 2 ? 1.0f : -1.0f);
            Bird b;
            b.state = Bird::PASSING;
            b.species = species;
            b.x = sx - dx * back - dy * out;
            b.y = sy - dy * back + dx * out;
            b.h = height + Range(rng, -10.0f, 10.0f);
            b.vx = dx * speed;
            b.vy = dy * speed;
            b.left = dx < 0.0f;
            b.flap = Range(rng, 0.0f, 1.0f);
            b.glide = Range(rng, 0.0f, 6.2831853f);
            birds.push_back(b);
        }
    }

    // Each, by what it is doing.
    const auto take_off = [&](Bird& b, float from_x, float from_y) {
        float ax = b.x - from_x, ay = b.y - from_y;
        float len = std::sqrt(ax * ax + ay * ay);
        if (len < 1.0f) { ax = Range(rng, -1.0f, 1.0f); ay = -1.0f; len = std::sqrt(ax * ax + ay * ay); }
        const float turn = Range(rng, -0.5f, 0.5f), speed = Range(rng, 120.0f, 150.0f);
        const float ux = ax / len, uy = ay / len;
        b.vx = (ux * cosf(turn) - uy * sinf(turn)) * speed;
        b.vy = (ux * sinf(turn) + uy * cosf(turn)) * speed;
        b.left = b.vx < 0.0f;
        b.state = Bird::FLEEING;
        b.t = 0.0f;
    };
    for (Bird& b : birds) {
        b.t += dt;
        b.flap += dt;
        switch (b.state) {
            case Bird::LANDING: {
                if (near_walker(b.tx, b.ty, 90.0f) || !day) { take_off(b, b.tx, b.ty); break; }
                const float ax = b.tx - b.x, ay = b.ty - b.y, d = std::sqrt(ax * ax + ay * ay);
                const float step = 110.0f * dt;
                if (d <= step) {
                    b.x = b.tx; b.y = b.ty; b.h = 0.0f;
                    b.state = Bird::STANDING; b.t = 0.0f; b.until = Range(rng, 0.8f, 2.5f);
                } else {
                    b.x += ax / d * step; b.y += ay / d * step;
                    b.h = std::min(b.h, d * 0.28f);
                    b.left = ax < 0.0f;
                }
                break;
            }
            case Bird::STANDING:
            case Bird::PECKING:
            case Bird::HOPPING: {
                // Somebody close: up and away -- and whoever is beside it too.
                bool scare = !day;
                float fx = b.x, fy = b.y + 1.0f;
                for (const SDL_FPoint& w : world.walkers)
                    if ((w.x - b.x) * (w.x - b.x) + (w.y - b.y) * (w.y - b.y) < 80.0f * 80.0f) { scare = true; fx = w.x; fy = w.y; }
                for (const Bird& o : birds)
                    if (&o != &b && o.state == Bird::FLEEING && o.t < 0.4f && o.h < 20.0f &&
                        (o.x - b.x) * (o.x - b.x) + (o.y - b.y) * (o.y - b.y) < 50.0f * 50.0f) { scare = true; fx = o.x - o.vx; fy = o.y - o.vy; }
                if (scare) { take_off(b, fx, fy); break; }
                if (b.state == Bird::HOPPING) {
                    const float k = std::min(1.0f, b.t / 0.22f);
                    b.x += (b.tx - b.x) * k;
                    b.y += (b.ty - b.y) * k;
                    if (b.t >= 0.22f) { b.state = Bird::STANDING; b.t = 0.0f; b.until = Range(rng, 0.6f, 2.2f); }
                } else if (b.t >= b.until) {
                    const float roll = Rand01(rng);
                    b.t = 0.0f;
                    if (b.state == Bird::PECKING || roll < 0.2f) {
                        b.state = Bird::STANDING; b.until = Range(rng, 0.6f, 2.4f);
                        if (Rand01(rng) < 0.3f) b.left = !b.left;
                    } else if (roll < 0.65f) {
                        b.state = Bird::PECKING; b.until = Range(rng, 0.35f, 0.7f);
                    } else {
                        const float hx = b.x + (b.left ? -1.0f : 1.0f) * Range(rng, 6.0f, 12.0f);
                        const float hy = b.y + Range(rng, -4.0f, 4.0f);
                        if (world.stand && world.stand(hx, hy)) { b.state = Bird::HOPPING; b.tx = hx; b.ty = hy; }
                        else { b.state = Bird::STANDING; b.until = Range(rng, 0.6f, 2.0f); b.left = !b.left; }
                    }
                }
                break;
            }
            case Bird::FLEEING:
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                b.h = std::min(110.0f, b.h + 70.0f * dt);
                break;
            case Bird::PASSING:
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                break;
        }
    }
    // Gone: the far side of the view, or left far behind.
    birds.erase(std::remove_if(birds.begin(), birds.end(), [&](const Bird& b) {
                    const float far = (b.state == Bird::PASSING || b.state == Bird::LANDING) ? reach + 260.0f : reach + 120.0f;
                    return std::fabs(b.x - cx) > far || std::fabs(b.y - cy) > far;
                }),
                birds.end());
}

vector<Ambience::BirdView> Ambience::Birds() const {
    vector<BirdView> out;
    for (const Bird& b : birds) out.push_back({b.x, b.y, b.h, b.h > 0.5f, b.species});
    return out;
}

// --- clouds ------------------------------------------------------------------------------------

vector<Uint8> Ambience::CloudMask(int size) {
    // Value noise on lattices that wrap at the edge, so the picture tiles:
    // three octaves, the biggest four cells across.
    const auto hash = [](int x, int y, int seed) {
        uint32_t h = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^ static_cast<uint32_t>(seed) * 0xC2B2AE3Du;
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        return static_cast<float>(h >> 8) / 16777216.0f;
    };
    const auto ease = [](float t) { return t * t * (3.0f - 2.0f * t); };
    vector<Uint8> mask(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float total = 0.0f, amp = 1.0f, norm = 0.0f;
            for (int o = 0; o < 3; ++o) {
                const int cells = 4 << o;
                const float fx = static_cast<float>(x) / size * cells, fy = static_cast<float>(y) / size * cells;
                const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                const float tx = ease(fx - ix), ty = ease(fy - iy);
                const auto at = [&](int a, int b) { return hash(((a % cells) + cells) % cells, ((b % cells) + cells) % cells, 77 + o); };
                const float top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * tx;
                const float bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * tx;
                total += (top + (bottom - top) * ty) * amp;
                norm += amp;
                amp *= 0.5f;
            }
            const float f = total / norm;
            const float k = std::clamp((f - 0.5f) / 0.14f, 0.0f, 1.0f);
            mask[static_cast<size_t>(y) * size + x] = static_cast<Uint8>(255.0f * k * k * (3.0f - 2.0f * k));
        }
    return mask;
}

float Ambience::CloudStrength() const {
    switch (kind) {
        case Kind::Field:  return 0.22f;
        case Kind::Town:   return 0.22f;
        case Kind::Forest: return 0.16f;
        case Kind::Grove:  return 0.24f;
        case Kind::Snow:   return 0.20f;
        case Kind::Ash:    return 0.30f;
        case Kind::Storm:  return 0.34f;
        default:           return 0.0f;
    }
}

SDL_Texture* Ambience::Sheet(SDL_Renderer* r) const {
    if (made_for != r) {
        made_for = r;
        sheet = IMG_LoadTexture(r, BIRDS_SHEET);
        if (sheet) SDL_SetTextureScaleMode(sheet, SDL_SCALEMODE_NEAREST);
        constexpr int size = 128;
        const vector<Uint8> mask = CloudMask(size);
        vector<Uint8> rgba(static_cast<size_t>(size) * size * 4, 255);
        for (size_t i = 0; i < mask.size(); ++i) rgba[i * 4 + 3] = mask[i];
        clouds = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
        if (clouds) {
            SDL_UpdateTexture(clouds, nullptr, rgba.data(), size * 4);
            SDL_SetTextureScaleMode(clouds, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(clouds, SDL_BLENDMODE_BLEND);
        }
    }
    return sheet;
}

void Ambience::DrawBird(SDL_Renderer* r, const Camera& cam, const Bird& b, bool shadow) const {
    const float z = cam.zoom;
    const float scale = b.species == SPARROW ? 0.8f : 1.0f;
    if (shadow) {
        // Under it on the ground: fainter the higher it is.
        const float fade = 1.0f - std::min(b.h, 160.0f) / 220.0f;
        const SDL_FPoint p = cam.ToScreen(b.x + b.h * 0.12f, b.y + b.h * 0.08f);
        const float w = roundf(7.0f * scale * z), h = std::max(1.0f, roundf(2.0f * z));
        SDL_SetRenderDrawColor(r, 10, 16, 20, static_cast<Uint8>(70.0f * fade));
        const SDL_FRect mid = {roundf(p.x - w / 2.0f), roundf(p.y - h / 2.0f), w, h};
        SDL_RenderFillRect(r, &mid);
        const SDL_FRect thin = {roundf(p.x - w / 3.0f), roundf(p.y - h / 2.0f - z), roundf(w * 2.0f / 3.0f), z};
        SDL_RenderFillRect(r, &thin);
        return;
    }
    SDL_Texture* tex = Sheet(r);
    if (!tex) return;
    int frame = 0;
    switch (b.state) {
        case Bird::STANDING: frame = 0; break;
        case Bird::PECKING:  frame = 1; break;
        case Bird::HOPPING:  frame = 2; break;
        default: {
            static const int kFlap[4] = {3, 4, 5, 4};
            frame = kFlap[static_cast<int>(b.flap * (b.species == EGRET ? 8.0f : 12.0f)) % 4];
            // A flock flaps and glides by turns, each bird to its own beat.
            if (b.state == Bird::PASSING && sinf(b.glide + b.t * 0.8f) > 0.25f) frame = 4;
            if (b.state == Bird::LANDING && b.h < 4.0f) frame = 2;
            break;
        }
    }
    const SDL_FRect src = {static_cast<float>(frame * 16), static_cast<float>(b.species * 16), 16.0f, 16.0f};
    const SDL_FRect world = {b.x - 8.0f * scale, b.y - 13.0f * scale - b.h, 16.0f * scale, 16.0f * scale};
    const SDL_FRect dst = cam.ToScreenRect(world);
    SDL_RenderTextureRotated(r, tex, &src, &dst, 0.0, nullptr, b.left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void Ambience::RenderGround(SDL_Renderer* r, const Camera& cam) const {
    if (birds.empty()) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Bird& b : birds) DrawBird(r, cam, b, true);
    for (const Bird& b : birds)
        if (b.h <= 0.5f) DrawBird(r, cam, b, false);
}

void Ambience::RenderSky(SDL_Renderer* r, const Camera& cam) const {
    Sheet(r);
    // The clouds' shadows: one soft picture, tiled over the world a tile a
    // thousand pixels across, drifting downwind -- cooler than the light
    // round them, not just darker.
    const float strength = lively ? CloudStrength() * std::clamp(daylight, 0.0f, 1.0f) : 0.0f;
    if (clouds && strength > 0.01f) {
        constexpr float tile = 1024.0f;
        const SDL_FPoint wd = Shaders::WindDirection();
        const float drift = static_cast<float>(std::fmod(SDL_GetTicks() / 1000.0 * 14.0, 1024.0 * 64.0));
        const float ox = std::fmod(drift * wd.x, tile), oy = std::fmod(drift * wd.y, tile);
        const SDL_FRect view = cam.VisibleWorldRect(0.0f);
        SDL_SetTextureColorMod(clouds, 16, 24, 40);
        SDL_SetTextureAlphaMod(clouds, static_cast<Uint8>(255.0f * strength));
        const float x0 = std::floor((view.x - ox) / tile) * tile + ox, y0 = std::floor((view.y - oy) / tile) * tile + oy;
        for (float y = y0; y < view.y + view.h; y += tile)
            for (float x = x0; x < view.x + view.w; x += tile) {
                const SDL_FRect dst = cam.ToScreenRect({x, y, tile, tile});
                SDL_RenderTexture(r, clouds, nullptr, &dst);
            }
    }
    for (const Bird& b : birds)
        if (b.h > 0.5f) DrawBird(r, cam, b, false);
}
