#include "titlescreen.h"

namespace {

// How far down the square painting the 16:9 band is taken from, as a fraction
// of what has to be thrown away. Centred would cut the moon.
constexpr float kCropBias = 0.10f;

float Frac(std::mt19937& rng, float lo, float hi) {
    return lo + (hi - lo) * (static_cast<float>(rng() % 10000) / 10000.0f);
}

} // namespace

void TitleBackdrop::Seed() {
    // A fixed seed, so the sky is the same sky every time the game is opened:
    // it is part of the picture rather than a different scatter each run.
    rng.seed(0xD6EA33u);
    stars.clear();

    // Colours off the painting: cold white, the moon's gold, and the blade's
    // blue. Nothing here is a hue the art does not already use.
    const SDL_Color palette[] = {
        {255, 252, 240, 255}, {255, 252, 240, 255},
        {252, 222, 138, 255}, {246, 208, 108, 255},
        {188, 226, 255, 255}, {150, 206, 255, 255},
    };

    const int count = 120;
    for (int i = 0; i < count; ++i) {
        Star s;
        // Stars belong in the sky and nowhere else. The middle column is the
        // moon, the blade and the menu panel; the outer sixth is the ruins,
        // whose tops come up about a third of the way. What is left over is
        // sky, and that is where they go.
        for (int tries = 0; tries < 24; ++tries) {
            s.x = Frac(rng, 0.02f, 0.98f);
            s.y = Frac(rng, 0.02f, 0.62f);
            const bool centre_column = s.x > 0.30f && s.x < 0.70f && s.y > 0.10f;
            const bool over_ruins    = (s.x < 0.16f || s.x > 0.84f) && s.y > 0.30f;
            if (!centre_column && !over_ruins) break;
        }
        s.size    = Frac(rng, 1.0f, 2.6f);
        s.base    = Frac(rng, 0.25f, 0.85f);
        s.speed   = Frac(rng, 0.25f, 1.1f);
        s.phase   = Frac(rng, 0.0f, 6.28f);
        s.sparkle = Frac(rng, 0.0f, 1.0f) < 0.22f;
        s.colour  = palette[rng() % (sizeof(palette) / sizeof(palette[0]))];
        if (s.sparkle) s.size *= 1.6f;
        stars.push_back(s);
    }
    seeded = true;
}

void TitleBackdrop::Advance(float dt, float aspect) {
    clock += dt;

    next_meteor -= dt;
    if (next_meteor <= 0.0f) {
        next_meteor = Frac(rng, 5.0f, 12.0f);
        for (Meteor& m : meteors) {
            if (m.alive) continue;
            // They fall the way the moon leans, across the upper sky, and are
            // gone in under a second: a menu is looked at for a long time and
            // anything busier than this becomes wallpaper noise.
            m.x  = Frac(rng, 0.05f, 0.85f);
            m.y  = Frac(rng, 0.02f, 0.30f);
            m.vx = Frac(rng, 0.22f, 0.40f) * (Frac(rng, 0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f);
            m.vy = Frac(rng, 0.10f, 0.20f);
            m.span = Frac(rng, 0.55f, 0.95f);
            m.life = 0.0f;
            m.alive = true;
            break;
        }
    }

    for (Meteor& m : meteors) {
        if (!m.alive) continue;
        m.life += dt;
        m.x += m.vx * dt / std::max(0.1f, aspect);
        m.y += m.vy * dt;
        if (m.life >= m.span || m.y > 0.8f || m.x < -0.1f || m.x > 1.1f) m.alive = false;
    }
}

void TitleBackdrop::Draw(SDL_Renderer* renderer, TextureCache& textures, UI& ui) {
    if (!seeded) Seed();

    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    if (last_ticks < 0.0f) last_ticks = now;
    // Clamped, so a long load or a dragged window does not teleport a meteor
    // across the sky in one frame.
    Advance(std::clamp(now - last_ticks, 0.0f, 0.1f), ui.ViewWidth() / std::max(1.0f, ui.ViewHeight()));
    last_ticks = now;

    const float vw = ui.ViewWidth(), vh = ui.ViewHeight();
    const SDL_FRect screen{0.0f, 0.0f, vw, vh};

    // --- the painting ---------------------------------------------------------
    SDL_Texture* art = textures.Get(TitleScreen::kArtPath);
    if (art) {
        // Smooth rather than nearest: the source is a painting at its own
        // resolution, not a tile that has to land on whole pixels, and a
        // non-integer scale with nearest would crawl as the window resizes.
        SDL_SetTextureScaleMode(art, SDL_SCALEMODE_LINEAR);
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(art, &tw, &th);

        SDL_FRect src{0.0f, 0.0f, tw, th};
        const float want = vw / std::max(1.0f, vh);
        if (tw / th > want) {
            // Wider than the window: take a column out of the middle.
            src.w = th * want;
            src.x = (tw - src.w) * 0.5f;
        } else {
            // Taller: take a band, biased up so the moon survives the crop.
            src.h = tw / want;
            src.y = (th - src.h) * kCropBias;
        }
        SDL_RenderTexture(renderer, art, &src, &screen);
    } else {
        ui.Fill(screen, {16, 13, 18, 255});
    }

    // --- settle it back -------------------------------------------------------
    // The painting is bright and the menu has to be read over it. A flat wash
    // plus a heavier gradient into the bottom third, where the prompts sit.
    ui.Fill(screen, {8, 6, 20, 70});
    const int bands = 10;
    for (int i = 0; i < bands; ++i) {
        const float t = static_cast<float>(i) / bands;
        const SDL_FRect band{0.0f, vh * (0.55f + 0.45f * t), vw, vh * 0.45f / bands + 1.0f};
        ui.Fill(band, {6, 5, 14, static_cast<Uint8>(10 + 90 * t)});
    }

    // --- the sky --------------------------------------------------------------
    const float scale = std::max(1.0f, vh / 720.0f);
    for (const Star& s : stars) {
        const float pulse = 0.5f + 0.5f * sinf(clock * s.speed * 2.0f + s.phase);
        const float a = std::clamp(s.base * (0.35f + 0.65f * pulse), 0.0f, 1.0f);
        if (a < 0.03f) continue;

        SDL_Color c = s.colour;
        c.a = static_cast<Uint8>(255.0f * a);
        const float px = s.x * vw, py = s.y * vh;
        const float r = s.size * scale;

        if (s.sparkle) {
            // The four-pointed star the painting itself is full of: a bright
            // core with two thin arms, the arms growing as it brightens.
            const float arm = r * (1.4f + 1.2f * pulse);
            SDL_Color faint = c;
            faint.a = static_cast<Uint8>(c.a * 0.75f);
            ui.Fill({px - arm, py - r * 0.22f, arm * 2.0f, r * 0.44f}, faint);
            ui.Fill({px - r * 0.22f, py - arm, r * 0.44f, arm * 2.0f}, faint);
            ui.Fill({px - r * 0.5f, py - r * 0.5f, r, r}, c);
        } else {
            ui.Fill({px - r * 0.5f, py - r * 0.5f, r, r}, c);
        }
    }

    for (const Meteor& m : meteors) {
        if (!m.alive) continue;
        // Fades in over the first fifth of its life and out over the rest, so
        // it never simply appears.
        const float t = m.life / m.span;
        const float fade = t < 0.2f ? t / 0.2f : 1.0f - (t - 0.2f) / 0.8f;
        const int   segments = 14;
        const float len = 0.10f;
        for (int i = 0; i < segments; ++i) {
            const float k = static_cast<float>(i) / segments;
            const float x = (m.x - m.vx * len * k / std::max(0.1f, vw / vh)) * vw;
            const float y = (m.y - m.vy * len * k) * vh;
            const float w = (2.6f - 2.0f * k) * scale;
            SDL_Color c{226, 240, 255, static_cast<Uint8>(std::clamp(235.0f * fade * (1.0f - k), 0.0f, 255.0f))};
            ui.Fill({x - w * 0.5f, y - w * 0.5f, w, w}, c);
        }
    }
}
