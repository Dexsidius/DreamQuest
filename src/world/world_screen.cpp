// -----------------------------------------------------------------------------
//  World, continued: the screen's own effects
//
//  What the shaders are told about the view before it is drawn (see
//  Shaders::Frame), and the passes the world draws with them: shockwaves and
//  the shake that goes with them, flashes, rings on the water, reflections,
//  the light of stained glass on a floor, and after dark what is lit from
//  inside. All of it is for show and none of it is sent to a friend: each
//  machine sees the meteor land and makes its own ring.
//
//  Everything here is a no-op with the effects off or off the GPU renderer,
//  and the world draws exactly as it did before any of it.
// -----------------------------------------------------------------------------
#include "world.h"

namespace {

bool StartsWith(const string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Where the flame of a fire is in its art: the upper part of what is drawn.
SDL_FPoint FlameOf(TextureCache& cache, const string& path, const SDL_FRect& world) {
    const SDL_FRect ob = cache.OpaqueBounds(path);
    return {world.x + (ob.x + ob.w * 0.5f) * world.w, world.y + (ob.y + ob.h * 0.3f) * world.h};
}

float Hash01(const string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    return static_cast<float>(h % 1000) / 1000.0f;
}

}  // namespace

void World::Shock(float x, float y, float strength, float shake_amount) {
    if (shocks.size() >= 4) shocks.erase(shocks.begin());
    shocks.push_back({x, y, 0.0f, std::clamp(strength, 0.0f, 1.0f)});
    shake = std::max(shake, std::clamp(shake_amount, 0.0f, 1.0f));
}

void World::Flash(SDL_Color colour, float amount) {
    if (amount < flash_amount) return;
    flash_colour = colour;
    flash_amount = std::clamp(amount, 0.0f, 1.0f);
}

SDL_FPoint World::ShakeOffset() const {
    // Not a shader: the camera is moved, and moved in whole world pixels so
    // the art stays on its grid. The plain look has none.
    const Shaders::Options& o = Shaders::GetOptions();
    if (!o.effects || !o.shake || shake <= 0.01f) return {0.0f, 0.0f};
    const float a = shake * shake * 7.0f;
    const float x = sinf(shake_clock * 53.0f) * 0.7f + sinf(shake_clock * 31.0f + 1.3f) * 0.3f;
    const float y = sinf(shake_clock * 47.0f + 0.7f) * 0.7f + sinf(shake_clock * 27.0f) * 0.3f;
    return {roundf(x * a), roundf(y * a)};
}

float World::GlowDarkness() const {
    if (!map.Loaded()) return 0.0f;
    if (map.IsDark() || InDream()) return 1.0f;
    // A lit dungeon is drawn in plain white light: a fire there is already as
    // bright as it is, and glowing on top of that it would be a lamp.
    if (map.Ambient() == "dungeon") return 0.0f;
    float dark = clock.Darkness();
    if (map.IsInterior()) dark *= 0.8f;
    return std::clamp(dark, 0.0f, 1.0f);
}

void World::UpdateScreenFx(float dt) {
    for (ShockRing& s : shocks) s.age += dt;
    shocks.erase(std::remove_if(shocks.begin(), shocks.end(),
                                [](const ShockRing& s) { return s.age >= SHOCK_TIME; }), shocks.end());
    shake = std::max(0.0f, shake - dt * 1.8f);
    shake_clock += dt;
    flash_amount = std::max(0.0f, flash_amount - dt * 2.6f);

    for (Shaders::Ripple& r : ripples) r.age += dt;
    ripples.erase(std::remove_if(ripples.begin(), ripples.end(),
                                 [](const Shaders::Ripple& r) { return r.age >= RIPPLE_TIME; }), ripples.end());
    if (!Shaders::Effects()) { ripples.clear(); swim_seen.clear(); return; }

    // A few times a second: a ring from whatever is waiting under the water,
    // one every second or so and a strong one as it comes up; and a ring left
    // behind anything swimming, which in a row is its wake.
    ripple_clock -= dt;
    if (ripple_clock > 0.0f) return;
    ripple_clock = 0.3f;
    ++ripple_tick;
    for (const auto& e : enemies) {
        if (e->CorpseGone()) continue;
        const Enemy* who = e.get();
        if (e->Hidden()) {
            const bool rising = e->Emerged() > 0.0f;
            const int phase = static_cast<int>(e->x * 0.37f + e->y * 0.61f);
            if (rising || (ripple_tick + phase) % 4 == 0)
                ripples.push_back({e->x, e->y - e->draw_lift, 0.0f, rising ? 1.0f : 0.75f});
            continue;
        }
        if (!e->Afloat() || e->CurrentState() == Enemy::State::Dead) { swim_seen.erase(who); continue; }
        auto it = swim_seen.find(who);
        if (it != swim_seen.end() && Length(e->x - it->second.x, e->y - it->second.y) > 3.0f)
            ripples.push_back({e->x, e->y - e->draw_lift, 0.0f, 0.8f});
        swim_seen[who] = {e->x, e->y};
    }
    if (ripples.size() > 48) ripples.erase(ripples.begin(), ripples.begin() + static_cast<long>(ripples.size() - 48));
}

Shaders::Frame World::ScreenFrame(TextureCache& cache) const {
    Shaders::Frame f;
    if (!map.Loaded()) return f;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const bool dreaming = InDream();
    f.night = GlowDarkness();
    // Indoors a draught; outdoors the wind rises and falls over a minute or two.
    f.wind = map.IsInterior() ? 0.22f : dreaming ? 0.5f : 0.72f + 0.28f * sinf(now * 0.07f);

    const SDL_FRect view = camera.VisibleWorldRect(48.0f);
    const float cx = view.x + view.w * 0.5f, cy = view.y + view.h * 0.5f;
    const auto inside = [&](float x, float y, float margin) {
        return x > view.x - margin && x < view.x + view.w + margin && y > view.y - margin && y < view.y + view.h + margin;
    };

    // Rings on the water: those in view, the nearest the middle first.
    for (const Shaders::Ripple& r : ripples)
        if (inside(r.x, r.y, 24.0f)) f.ripples.push_back(r);
    std::sort(f.ripples.begin(), f.ripples.end(), [&](const Shaders::Ripple& a, const Shaders::Ripple& b) {
        return Length(a.x - cx, a.y - cy) < Length(b.x - cx, b.y - cy);
    });
    if (f.ripples.size() > 12) f.ripples.resize(12);

    // Fog lies thickest in the dark: out of doors the day burns most of it off.
    f.fog = map.GroundFog();
    if (!map.IsInterior() && !dreaming) f.fog.density *= 0.6f + 0.4f * clock.Darkness();

    // Each place its own colours, and the hour its own on top outdoors: warm
    // and rich in the golden hour, the colour drained out of the dead of night.
    const auto grade = [&](float r, float g, float b, float saturation, float contrast, float lift) {
        f.grade[0] = r; f.grade[1] = g; f.grade[2] = b; f.grade[3] = saturation;
        f.contrast = contrast; f.lift = lift;
    };
    const string& ambient = map.Ambient();
    const string& id = map.Id();
    if (dreaming)
        grade(1.02f, 0.98f, 1.06f, 1.12f, 1.02f, 0.0f);
    else if (ambient == "ash" || StartsWith(id, "palace_") || StartsWith(id, "dungeon_emberfell") || id == "dungeon_infernal")
        grade(1.07f, 0.96f, 0.86f, 1.1f, 1.05f, 0.0f);
    else if (ambient == "snow")
        grade(0.93f, 0.99f, 1.08f, 0.84f, 1.03f, 0.012f);
    else if (ambient == "dungeon")
        grade(0.97f, 0.97f, 1.01f, 0.88f, 1.06f, 0.0f);
    else if (!map.IsInterior()) {
        const float dark = clock.Darkness();
        const float warm = clock.Warmth() * (1.0f - dark);
        grade(1.0f + 0.07f * warm - 0.03f * dark, 1.0f + 0.02f * warm - 0.01f * dark, 1.0f - 0.07f * warm + 0.05f * dark,
              1.0f + 0.18f * warm - 0.32f * dark, 1.0f + 0.03f * warm, 0.012f * dark);
    }

    f.flash[0] = flash_colour.r / 255.0f; f.flash[1] = flash_colour.g / 255.0f;
    f.flash[2] = flash_colour.b / 255.0f; f.flash[3] = flash_amount * 0.8f;

    // Shockwaves, in the view's own pixels: fast at first and slowing, and
    // weaker the further they have gone.
    for (const ShockRing& s : shocks) {
        const float k = std::clamp(s.age / SHOCK_TIME, 0.0f, 1.0f);
        const SDL_FPoint at = camera.ToScreen(s.x, s.y - LiftAt(s.x, s.y));
        const float out = 1.0f - (1.0f - k) * (1.0f - k);
        f.shocks.push_back({at.x, at.y, (6.0f + 190.0f * out) * camera.zoom, s.strength * powf(1.0f - k, 1.5f)});
    }

    // What is hot: the air over it wavers. Fires in the scenery, from their
    // art; fireballs and the trail behind them; burning ground.
    struct Hot { Shaders::HeatSpot h; float d; };
    vector<Hot> hot;
    const auto consider = [&](float x, float y, float radius, float strength) {
        if (!inside(x, y, radius)) return;
        hot.push_back({{x, y, radius, strength}, Length(x - cx, y - cy)});
    };
    vector<const TileInstance*> decor;
    map.CollectDecor(camera, decor);
    for (const TileInstance* t : decor) {
        if (!map.ArtOf(t->tex).hot) continue;
        SDL_FRect world = t->rect;
        world.y -= map.HeightAt(world.x + world.w * 0.5f, world.y + world.h);
        const SDL_FPoint flame = FlameOf(cache, map.TexturePath(*t), world);
        const float wide = cache.OpaqueBounds(map.TexturePath(*t)).w * world.w;
        consider(flame.x, flame.y, std::clamp(wide * 0.9f, 22.0f, 90.0f), 0.55f);
    }
    for (const MapObject& o : map.Objects()) {
        if (o.sprite.empty() || !Shaders::ArtOf(o.sprite).hot || !ObjectPresent(o)) continue;
        const SDL_Point size = cache.Size(o.sprite);
        const SDL_FRect world = {o.x - size.x / 2.0f, o.y - static_cast<float>(size.y),
                                 static_cast<float>(size.x), static_cast<float>(size.y)};
        const SDL_FPoint flame = FlameOf(cache, o.sprite, world);
        consider(flame.x, flame.y, std::clamp(cache.OpaqueBounds(o.sprite).w * world.w * 0.9f, 22.0f, 90.0f), 0.55f);
    }
    for (const Projectile& p : projectiles) {
        if (p.finished || !p.def || p.def->element != Element::Fire) continue;
        const float lift = LiftAt(p.x, p.y);
        consider(p.x, p.y - lift, 30.0f, 0.8f);
        consider(p.x - cosf(p.angle) * 18.0f, p.y - lift - sinf(p.angle) * 18.0f, 24.0f, 0.55f);
    }
    for (const GroundEffect& g : ground_effects)
        if (g.Look() == Element::Fire && g.Active()) consider(g.x, g.y - LiftAt(g.x, g.y), g.radius * 1.3f, 0.6f);
    std::sort(hot.begin(), hot.end(), [](const Hot& a, const Hot& b) { return a.d < b.d; });
    for (size_t i = 0; i < hot.size() && i < 8; ++i) f.heats.push_back(hot[i].h);

    // The Reverie: its edges swim and bright things bloom, more the deeper.
    if (dreaming) f.dream = 0.3f + 0.2f * static_cast<float>(std::clamp(map.DreamDepth(), 1, 3) - 1);
    return f;
}

void World::DrawReflections(SDL_Renderer* r, TextureCache& cache, const vector<const TileInstance*>& decor) const {
    if (!Shaders::Effects()) return;
    bool began = false;
    // Upside down under its own foot, as tall as it is: the shader keeps only
    // what lands on water or lava. Only for what has any within that reach.
    const auto reflect = [&](SDL_Texture* tex, float x, float base, float w, float h, Uint8 alpha) {
        bool wet = false;
        for (float down : {0.08f, 0.25f, 0.45f, 0.7f}) {
            for (float across : {0.3f, 0.5f, 0.7f})
                if (Shaders::FluidAt(x + w * across, base + h * down) != Shaders::PLAIN) { wet = true; break; }
            if (wet) break;
        }
        if (!wet) return;
        if (!began) {
            if (!Shaders::UseReflection(r)) return;
            began = true;
        }
        const SDL_FRect dst = camera.ToScreenRect({x, base, w, h});
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_RenderTextureRotated(r, tex, nullptr, &dst, 0.0, nullptr, SDL_FLIP_VERTICAL);
        SDL_SetTextureAlphaMod(tex, 255);
    };
    for (const TileInstance* t : decor) {
        const string& path = map.TexturePath(*t);
        // What lies flat on the water has nothing standing up to show in it.
        if (path.find("lily_pads") != string::npos) continue;
        SDL_Texture* tex = cache.Get(path);
        if (!tex) continue;
        const float lift = map.HeightAt(t->rect.x + t->rect.w * 0.5f, t->rect.y + t->rect.h);
        reflect(tex, t->rect.x, t->rect.y + t->rect.h - lift, t->rect.w, t->rect.h, 255);
    }
    const SDL_FRect view = camera.VisibleWorldRect(96.0f);
    for (const MapObject& o : map.Objects()) {
        if (o.sprite.empty() || !ObjectPresent(o)) continue;
        if (o.x < view.x || o.x > view.x + view.w || o.y < view.y || o.y > view.y + view.h) continue;
        const bool spent = ObjectSpent(o);
        const string& shown = (!o.sprite_open.empty() && spent) ? o.sprite_open : o.sprite;
        SDL_Texture* tex = cache.Get(shown);
        if (!tex) continue;
        float tw = 0, th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        reflect(tex, o.x - tw / 2.0f, o.y, tw, th, 255);
    }
    if (began) Shaders::UsePlain(r);
}

void World::DrawFloorLight(SDL_Renderer* r) const {
    if (!Shaders::Effects()) return;
    // Stained glass: the light of a high window lying on the floor, strongest
    // by day. Laid in by genmaps as "glass_light" objects.
    const float day = 1.0f - clock.Darkness();
    const SDL_FRect view = camera.VisibleWorldRect(96.0f);
    for (const MapObject& o : map.Objects()) {
        if (o.type != "glass_light") continue;
        if (o.x < view.x || o.x > view.x + view.w || o.y < view.y || o.y > view.y + view.h) continue;
        Shaders::ShapeFx fx;
        fx.shape = Shaders::SHAPE_GLASS;
        fx.seed = floorf(Hash01(o.id) * 97.0f);
        fx.colour = {1.0f, 1.0f, 1.0f, 0.22f + 0.4f * day};
        const SDL_FRect dst = camera.ToScreenRect({o.x - 34.0f, o.y - 56.0f, 68.0f, 112.0f});
        Shaders::DrawShape(r, dst, fx, SDL_BLENDMODE_ADD);
    }
}

void World::DrawGlows(SDL_Renderer* r, TextureCache& cache, const vector<const TileInstance*>& decor) const {
    if (!Shaders::Effects()) return;
    const float dark = GlowDarkness();
    if (dark <= 0.02f) return;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const SDL_FRect view = camera.VisibleWorldRect(96.0f);

    // What is lit from inside -- windows, flames, eyes of crystal -- drawn
    // again, only those pixels of it, added over the night.
    const auto glow = [&](SDL_Texture* tex, const SDL_FRect& world) {
        const SDL_FRect dst = camera.ToScreenRect(world);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        SDL_RenderTexture(r, tex, nullptr, &dst);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    };
    struct Halo { float x, y, radius; SDL_FColor c; float seed; };
    vector<Halo> halos;
    const auto halo_of = [&](const string& path, const SDL_FRect& world, const string& key) {
        const SDL_FPoint at = FlameOf(cache, path, world);
        const bool fire = Shaders::ArtOf(path).hot;
        const float wide = cache.OpaqueBounds(path).w * world.w;
        halos.push_back({at.x, at.y, std::clamp(wide * 1.6f, 44.0f, 110.0f),
                         fire ? SDL_FColor{1.0f, 0.62f, 0.3f, 1.0f} : SDL_FColor{0.85f, 0.9f, 1.0f, 1.0f}, Hash01(key)});
    };
    if (Shaders::UseGlow(r)) {
        for (const TileInstance* t : decor) {
            const Shaders::Art& art = map.ArtOf(t->tex);
            if (!art.glows && !art.halo) continue;
            SDL_Texture* tex = cache.Get(map.TexturePath(*t));
            if (!tex) continue;
            SDL_FRect world = t->rect;
            world.y -= map.HeightAt(world.x + world.w * 0.5f, world.y + world.h);
            if (art.glows) glow(tex, world);
            if (art.halo) halo_of(map.TexturePath(*t), world, map.TexturePath(*t) + std::to_string(t->rect.x));
        }
        for (const MapObject& o : map.Objects()) {
            if (o.sprite.empty() || !ObjectPresent(o)) continue;
            if (o.x < view.x || o.x > view.x + view.w || o.y < view.y || o.y > view.y + view.h) continue;
            const bool spent = ObjectSpent(o);
            const bool used = !o.sprite_open.empty() && spent;
            if (spent && o.sprite_open.empty()) continue;           // a worked-out seam is dark
            const string& shown = used ? o.sprite_open : o.sprite;
            const Shaders::Art& art = Shaders::ArtOf(shown);
            const bool lamp = o.type == "lamp" || o.type == "range" || o.type == "camp_fire";
            if (!art.glows && !art.halo && !lamp) continue;
            SDL_Texture* tex = cache.Get(shown);
            if (!tex) continue;
            float tw = 0, th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            const SDL_FRect world = {o.x - tw / 2.0f, o.y - th, tw, th};
            if (art.glows) glow(tex, world);
            if (art.halo || lamp) halo_of(shown, world, o.id);
        }
        Shaders::UsePlain(r);
    }

    // The lit air round every light: a soft pool, flickering if it is a fire.
    for (const Halo& h : halos) {
        Shaders::ShapeFx fx;
        fx.shape = Shaders::SHAPE_HALO;
        const float flicker = 0.9f + 0.1f * sinf(now * 7.3f + h.seed * 6.28f) + 0.05f * sinf(now * 13.1f + h.seed * 3.1f);
        fx.colour = {h.c.r, h.c.g, h.c.b, 0.34f * dark * flicker};
        const SDL_FRect dst = camera.ToScreenRect({h.x - h.radius, h.y - h.radius, h.radius * 2.0f, h.radius * 2.0f});
        Shaders::DrawShape(r, dst, fx, SDL_BLENDMODE_ADD);
    }

    // Spells in flight shine through the dark, where the night had dimmed them.
    if (SDL_Texture* light = cache.Get("assets/effects/glow.png")) {
        SDL_SetTextureBlendMode(light, SDL_BLENDMODE_ADD);
        for (const Projectile& p : projectiles) {
            if (p.finished || !p.def || (p.def->element == Element::None && p.def->glow <= 0.0f)) continue;
            const float across = std::max(36.0f, p.def->glow) * 1.1f;
            const float lift = LiftAt(p.x, p.y);
            const SDL_FRect dst = camera.ToScreenRect({p.x - across / 2.0f, p.y - lift - across / 2.0f, across, across});
            const SDL_Color c = ElementColor(p.def->element);
            SDL_SetTextureColorMod(light, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(light, static_cast<Uint8>(190.0f * dark));
            SDL_RenderTexture(r, light, nullptr, &dst);
        }
        SDL_SetTextureColorMod(light, 255, 255, 255);
        SDL_SetTextureAlphaMod(light, 255);
        SDL_SetTextureBlendMode(light, SDL_BLENDMODE_BLEND);
    }
}
