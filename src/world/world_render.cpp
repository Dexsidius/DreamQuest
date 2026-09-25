// -----------------------------------------------------------------------------
//  World, continued: everything that is drawn, and the light it is drawn in
//
//  The World class is one class in several files, cut along the sections it
//  always had: world.cpp has the map, the seats, sleep and the frame; the rest
//  is world_combat, world_projectiles, world_effects, world_interact and
//  world_render. Nothing but where a function lives changed when it was cut.
// -----------------------------------------------------------------------------
#include "world.h"
#include "../input.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include "../systems/dialogue.h"
#include "../systems/spell.h"
#include "../systems/audio.h"
#include "../systems/gathering.h"

SDL_Color World::AmbientLight() const {
    const SDL_Color white{255, 255, 255, 255};
    if (!map.Loaded()) return white;
    // A dark map is black but for what is carried into it. Not quite black:
    // at nothing at all the walls stop existing and the place reads as a bug
    // rather than as a cellar.
    if (map.IsDark()) return {26, 24, 32, 255};
    if (map.Ambient() == "dungeon") return white;
    if (InDream()) {
        // Violet where you arrive, and less of it each ladder down.
        switch (map.DreamDepth()) {
            case 2:  return {126, 100, 196, 255};
            case 3:  return {100, 76, 170, 255};
            default: return {156, 124, 214, 255};
        }
    }

    float dark = clock.Darkness();
    float warm = clock.Warmth() * (1.0f - dark * 0.7f);
    if (map.IsInterior()) { dark *= 0.5f; warm *= 0.3f; }
    if (dark <= 0.001f && warm <= 0.001f) return white;

    const SDL_Color night{84, 96, 156, 255};
    const SDL_Color sunset{255, 178, 128, 255};
    const auto mix = [&](float base, float n, float s) {
        const float c = base + (n - base) * dark;
        return static_cast<Uint8>(std::clamp(c * (1.0f + (s / 255.0f - 1.0f) * warm * 0.6f), 0.0f, 255.0f));
    };
    return {mix(255.0f, night.r, sunset.r), mix(255.0f, night.g, sunset.g),
            mix(255.0f, night.b, sunset.b), 255};
}

vector<Light> World::CollectLights() const {
    vector<Light> lights;
    if (!map.Loaded()) return lights;
    if (map.Ambient() == "dungeon" && !map.IsDark()) return lights;
    const bool dreaming = InDream();
    float dark = dreaming ? 1.0f : clock.Darkness();
    if (map.IsInterior()) dark *= 0.8f;
    if (map.IsDark()) dark = 1.0f;
    if (dark <= 0.01f) return lights;

    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    // Firelight flickers, each fire on its own rhythm.
    const auto flicker = [&](const string& id) {
        unsigned h = 2166136261u;
        for (char c : id) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        const float ph = (h % 1000) / 1000.0f * 6.2831853f;
        return 0.88f + 0.08f * sinf(t * 7.3f + ph) + 0.04f * sinf(t * 13.1f + ph * 2.0f);
    };

    for (const MapObject& o : map.Objects()) {
        if (!ObjectPresent(o)) continue;
        const bool fire = o.type == "range" || o.type == "camp_fire";
        if (fire) {
            const float f = flicker(o.id);
            const float radius = (map.IsInterior() ? 150.0f : 130.0f) * (0.96f + 0.04f * f);
            lights.push_back({o.x, o.y - 10.0f, radius, {255, 172, 96, 255}, dark * f});
        } else if (o.type == "lamp") {
            // A lamp standard: a steady cool light, as much of it as it is dark.
            lights.push_back({o.x, o.y - 44.0f, 150.0f, {196, 226, 255, 255}, dark * 0.95f});
        } else if (o.type == "dream_wake") {
            lights.push_back({o.x, o.y - 16.0f, 120.0f, {226, 214, 255, 255}, 0.85f});
        } else if (dreaming && o.yield == "dream_shard" && !o.skill.empty()) {
            const float pulse = 0.75f + 0.25f * sinf(t * 2.2f + o.x * 0.05f);
            lights.push_back({o.x, o.y - 10.0f, 84.0f, {130, 220, 255, 255}, 0.8f * pulse});
        }
    }

    // A leader winding up a heavy throws red light around it, so the warning
    // reads at night and underground as well as by day.
    for (const auto& e : enemies) {
        const float charge = e->HeavyCharge();
        if (charge <= 0.0f) continue;
        lights.push_back({e->x, e->y - 20.0f, 50.0f + 60.0f * charge, {255, 50, 30, 255}, 0.4f + 0.6f * charge});
    }

    // Below the first depth a dream is dark enough to lose a dark thing in, so
    // what lives there is lit from inside, faintly: a Gloomwing is a shape with
    // a glow round it, and not a hole in the floor. Only those near enough to
    // be on anybody's screen.
    if (dreaming && map.DreamDepth() >= 2)
        for (const auto& e : enemies) {
            if (!Targeting::Targetable(*e)) continue;
            if (fabsf(e->x - player.x) > 780.0f || fabsf(e->y - player.y) > 480.0f) continue;
            lights.push_back({e->x, e->y - 14.0f, 60.0f, {206, 176, 255, 255}, 0.5f});
        }

    // A little light of your own, so the player is never lost in the dark: a
    // warm glow outdoors, a pale one in a dream. Underground it is only what
    // is in your hand -- and with nothing in it, barely an arm's length.
    // Friends carry theirs too: in a window, on the other half of the screen,
    // or on a map the host is not on, nobody walks in the dark unlit.
    for (const auto& g : guests) {
        if (g->IsDead()) continue;
        const float lamp = g->equipment.LightRadius();
        if (map.IsDark())
            lights.push_back({g->x, g->y - 16.0f, lamp > 0.0f ? lamp : 44.0f,
                              lamp > 0.0f ? SDL_Color{255, 226, 168, 255} : SDL_Color{180, 186, 210, 255}, lamp > 0.0f ? 1.0f : 0.55f});
        else if (dreaming)
            lights.push_back({g->x, g->y - 16.0f, 120.0f, {236, 226, 255, 255}, 0.75f});
        else
            lights.push_back({g->x, g->y - 16.0f, std::max(80.0f, lamp * 0.8f), {255, 236, 200, 255},
                              (lamp > 0.0f ? 0.6f : 0.42f) * dark});
    }
    if (!player.IsDead() || player.DeathTimer() > 0.0f) {
        const float lamp = player.equipment.LightRadius();
        if (map.IsDark()) {
            const float t2 = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            const float flame = 0.94f + 0.06f * sinf(t2 * 6.1f) + 0.03f * sinf(t2 * 11.3f);
            if (lamp > 0.0f)
                lights.push_back({player.x, player.y - 16.0f, lamp * flame,
                                  {255, 226, 168, 255}, 1.0f});
            else
                lights.push_back({player.x, player.y - 16.0f, 44.0f, {180, 186, 210, 255}, 0.55f});
        } else if (dreaming) {
            lights.push_back({player.x, player.y - 16.0f, 120.0f, {236, 226, 255, 255}, 0.75f});
        } else {
            const float radius = std::max(80.0f, lamp * 0.8f);
            lights.push_back({player.x, player.y - 16.0f, radius, {255, 236, 200, 255},
                              (lamp > 0.0f ? 0.6f : 0.42f) * dark});
        }
    }

    for (const Projectile& p : projectiles) {
        if (p.finished || !p.def || p.def->element == Element::None) continue;
        lights.push_back({p.x, p.y, 48.0f, ElementColor(p.def->element), 0.9f * dark});
    }
    for (const GroundEffect& g : ground_effects) {
        if (g.Look() != Element::Fire || !g.Active()) continue;
        lights.push_back({g.x, g.y, g.radius * 2.2f, {255, 150, 70, 255}, 0.8f * dark});
    }

    if (Shaders::Effects()) {
        // Lightning lights up the night for the moment it is there: an arc
        // round its middle, a bolt from the sky wide round where it struck.
        for (const Arc& a : arcs) {
            if (a.life <= 0.0f) continue;
            const float p = a.Progress();
            const float bright = p < 0.3f ? 1.0f : 1.0f - (p - 0.3f) / 0.7f;
            if (bright <= 0.0f) continue;
            const float k = a.look == 1 ? 1.0f : 0.5f;
            lights.push_back({a.x + cosf(a.facing) * a.reach * k, a.y + sinf(a.facing) * 0.9f * a.reach * k,
                              a.look == 1 ? 260.0f : 110.0f, {222, 232, 255, 255}, bright * dark});
        }
        // Lava lights what is round it, and flickers as it churns.
        vector<SDL_FPoint> lava;
        map.SurfaceSpots(camera.VisibleWorldRect(96.0f), Shaders::LAVA, 96.0f, lava);
        for (const SDL_FPoint& at : lava) {
            const float f = 0.85f + 0.1f * sinf(t * 2.3f + at.x * 0.031f) + 0.05f * sinf(t * 5.1f + at.y * 0.047f);
            lights.push_back({at.x, at.y, 120.0f, {255, 118, 48, 255}, 0.7f * dark * f});
        }
    }
    return lights;
}

void World::RenderStars(SDL_Renderer* r) const {
    // The void under the dream's islands: stars that drift a little behind the
    // camera, so the islands read as floating over something far away.
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    if (w <= 0 || h <= 0) return;
    const SDL_FPoint origin = camera.ToScreen(0.0f, 0.0f);
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 260; ++i) {
        unsigned hsh = static_cast<unsigned>(i) * 2654435761u;
        hsh ^= hsh >> 15; hsh *= 2246822519u; hsh ^= hsh >> 13;
        const float u = (hsh & 0xFFFF) / 65535.0f;
        const float v = ((hsh >> 16) & 0xFFFF) / 65535.0f;
        const float depth = 0.08f + 0.22f * ((hsh % 97) / 96.0f);
        float sx = fmodf(u * w * 1.5f + origin.x * depth, static_cast<float>(w));
        float sy = fmodf(v * h * 1.5f + origin.y * depth, static_cast<float>(h));
        if (sx < 0.0f) sx += w;
        if (sy < 0.0f) sy += h;
        const float twinkle = 0.55f + 0.45f * sinf(t * (1.0f + (hsh % 5)) + i);
        const Uint8 a = static_cast<Uint8>(200.0f * twinkle * (0.4f + depth * 2.0f));
        const bool warm = (hsh % 7) == 0;
        SDL_SetRenderDrawColor(r, warm ? 255 : 210, warm ? 214 : 220, 255, a);
        const float s = (hsh % 11 == 0) ? 3.0f : 2.0f;
        const SDL_FRect star = {roundf(sx), roundf(sy), s, s};
        SDL_RenderFillRect(r, &star);
    }
}

// Arrow Rain, falling. Each rain keeps a few dozen arrows on the go, and every
// one of them is worked out from the clock and its own number rather than kept
// anywhere: where it lands in the circle, when it started down, how far along
// it is. It comes in steep from up and to the left, takes a fifth of a second
// over it, and then stands in the ground where it struck for half a second
// before it fades. Nothing is stored, so a guest's screen -- which is only told
// that there is a rain here, and how long it has left -- draws its own, and it
// does not matter that they are not the same arrows.
void World::DrawArrowRain(SDL_Renderer* r) const {
    bool any = false;
    for (const GroundEffect& g : ground_effects) any |= g.rain;
    if (!any) return;

    const float z = camera.zoom;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // A line of art-sized pixels from a to b, snapped to the sprite grid.
    const auto pixels = [&](float ax, float ay, float bx, float by, SDL_Color c) {
        const int n = std::max(1, static_cast<int>(std::max(fabsf(bx - ax), fabsf(by - ay)) / z));
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / n;
            const SDL_FRect px = {roundf((ax + (bx - ax) * t) / z) * z, roundf((ay + (by - ay) * t) / z) * z, z, z};
            SDL_RenderFillRect(r, &px);
        }
    };
    const auto unit = [](uint32_t h) {
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        return static_cast<float>(h & 0xffffff) / static_cast<float>(0x1000000);
    };
    // A stone of the Sedimentary Rain, `w` by `h` screen pixels about (cx, cy):
    // a dark rim, the stone, light along its top and left and dark along its
    // bottom. `which` picks its colour; no two in a rain are quite the same.
    const auto stone = [&](float cx, float cy, float w, float h, int which, Uint8 alpha) {
        const SDL_FRect rock = {roundf((cx - w / 2.0f) / z) * z, roundf((cy - h / 2.0f) / z) * z, w, h};
        const SDL_FRect rim = {rock.x - z, rock.y - z, rock.w + 2.0f * z, rock.h + 2.0f * z};
        const Uint8 warm = static_cast<Uint8>(which % 40);
        SDL_SetRenderDrawColor(r, 52, 38, 28, alpha);
        SDL_RenderFillRect(r, &rim);
        SDL_SetRenderDrawColor(r, static_cast<Uint8>(158 + warm), static_cast<Uint8>(128 + warm / 2), 90, alpha);
        SDL_RenderFillRect(r, &rock);
        if (h >= 2.0f * z) {
            SDL_SetRenderDrawColor(r, 108, 82, 58, alpha);
            const SDL_FRect under = {rock.x, rock.y + rock.h - z, rock.w, z};
            SDL_RenderFillRect(r, &under);
        }
        SDL_SetRenderDrawColor(r, 228, 206, 160, alpha);
        const SDL_FRect top = {rock.x, rock.y, std::max(z, rock.w - z), z};
        SDL_RenderFillRect(r, &top);
        if (w >= 3.0f * z && h >= 3.0f * z) {
            const SDL_FRect left = {rock.x, rock.y + z, z, rock.h - 2.0f * z};
            SDL_RenderFillRect(r, &left);
        }
    };

    constexpr int   LANES = 44;           // arrows a rain has on the go
    constexpr float CYCLE = 0.78f;         // a lane's arrow: down, stood, gone, and the next
    constexpr float FALL = 0.20f, STAND = 0.50f;
    constexpr float FROM_X = -46.0f, FROM_Y = -150.0f;   // where it comes from, off where it lands
    constexpr float SHAFT = 16.0f;

    for (const GroundEffect& g : ground_effects) {
        if (!g.rain) continue;
        // In flight before the first volley lands, and stopping as the last does.
        if (g.delay > FALL) continue;
        const float lift = LiftAt(g.x, g.y);
        const bool stones = g.Look() == Element::Earth;          // the Sedimentary Rain: the same rain, of stones
        const uint32_t seed = static_cast<uint32_t>(static_cast<int>(g.x) * 73856093) ^ static_cast<uint32_t>(static_cast<int>(g.y) * 19349663);
        for (int lane = 0; lane < LANES; ++lane) {
            const float phase = unit(seed + lane * 7919u);
            const float clock = now / CYCLE + phase;
            const uint32_t shot = static_cast<uint32_t>(clock);
            const float t = (clock - static_cast<float>(shot)) * CYCLE;          // seconds into this arrow
            if (t > FALL + STAND) continue;
            // None that set off after the rain stopped; the ones already down finish fading.
            if (g.life < GroundEffect::RAIN_LINGER && t < GroundEffect::RAIN_LINGER - g.life) continue;
            // Where in the circle, evenly by area.
            const float a = unit(seed ^ (shot * 2654435761u + lane * 40503u)) * 6.2831853f;
            const float d = sqrtf(unit(seed + shot * 97u + lane * 31337u)) * g.radius * 0.94f;
            const float lx = g.x + cosf(a) * d, ly = g.y + sinf(a) * d - lift;
            const float dirx = -FROM_X, diry = -FROM_Y;
            const float len = sqrtf(dirx * dirx + diry * diry);
            const float ux = dirx / len, uy = diry / len;
            if (t < FALL) {
                // Coming down: a pale streak with a dark head, the length of an arrow and a half.
                const float k = t / FALL;
                const float hx = lx + FROM_X * (1.0f - k), hy = ly + FROM_Y * (1.0f - k);
                const SDL_FPoint head = camera.ToScreen(hx, hy);
                if (stones) {
                    // A stone the size of a fist: dark round the edge, lit on
                    // the two sides the light is on and dark along the bottom,
                    // with the streak of its fall over it -- and under it, on
                    // the ground, its shadow closing in to where it will land.
                    const float side = (2.0f + static_cast<float>(lane % 3)) * z;
                    const SDL_FPoint land = camera.ToScreen(lx, ly);
                    const float across = side * (0.6f + 0.8f * k) + 2.0f * z;
                    SDL_SetRenderDrawColor(r, 20, 14, 10, static_cast<Uint8>(40.0f + 90.0f * k));
                    const SDL_FRect shade = {roundf((land.x - across / 2.0f) / z) * z, roundf(land.y / z) * z, roundf(across / z) * z, z};
                    SDL_RenderFillRect(r, &shade);
                    stone(head.x, head.y, side, side, lane, 255);
                    const SDL_FPoint streak = camera.ToScreen(hx - ux * 14.0f, hy - uy * 14.0f);
                    pixels(streak.x, streak.y, head.x, head.y - side, {214, 196, 160, 130});
                    continue;
                }
                const SDL_FPoint tail = camera.ToScreen(hx - ux * SHAFT * 1.5f, hy - uy * SHAFT * 1.5f);
                pixels(tail.x + z, tail.y, head.x + z, head.y, {52, 40, 34, 170});
                pixels(tail.x, tail.y, head.x, head.y, {250, 240, 208, 255});
                const SDL_FPoint tip = camera.ToScreen(hx - ux * 4.0f, hy - uy * 4.0f);
                pixels(tip.x, tip.y, head.x, head.y, {60, 54, 52, 255});
            } else {
                // Stood in the ground at the angle it came in at, fletching up, fading.
                const float k = (t - FALL) / STAND;
                const Uint8 alpha = static_cast<Uint8>(255.0f * std::clamp(1.6f - k * 1.6f, 0.0f, 1.0f));
                const SDL_FPoint foot = camera.ToScreen(lx, ly);
                if (stones) {
                    // Lying where it fell, and going: squatter than it fell, and
                    // for the first moment in the dust it knocked up -- a low
                    // pale line that spreads, and two specks thrown clear of it.
                    const float side = (2.0f + static_cast<float>(lane % 3)) * z;
                    const float squat = std::max(z, roundf(side * 0.7f / z) * z);
                    stone(foot.x, foot.y - squat / 2.0f, side, squat, lane, alpha);
                    if (k < 0.3f) {
                        const float out = k / 0.3f;
                        SDL_SetRenderDrawColor(r, 214, 196, 160, static_cast<Uint8>(170.0f * (1.0f - out)));
                        const float wide = side + (4.0f + 8.0f * out) * z;
                        const SDL_FRect puff = {roundf((foot.x - wide / 2.0f) / z) * z, roundf(foot.y / z) * z, roundf(wide / z) * z, z};
                        SDL_RenderFillRect(r, &puff);
                        for (float way : {-1.0f, 1.0f}) {
                            const SDL_FRect chip = {roundf((foot.x + way * (side * 0.5f + 5.0f * z * out)) / z) * z,
                                                    roundf((foot.y - (6.0f * out - 7.0f * out * out) * 3.0f * z) / z) * z, z, z};
                            SDL_RenderFillRect(r, &chip);
                        }
                    }
                    continue;
                }
                const SDL_FPoint top  = camera.ToScreen(lx - ux * SHAFT, ly - uy * SHAFT);
                pixels(foot.x + z, foot.y, top.x + z, top.y, {46, 34, 28, static_cast<Uint8>(alpha * 0.6f)});
                pixels(foot.x, foot.y, top.x, top.y, {168, 122, 74, alpha});
                const SDL_FPoint fl = camera.ToScreen(lx - ux * (SHAFT - 5.0f), ly - uy * (SHAFT - 5.0f));
                pixels(fl.x, fl.y, top.x, top.y, {250, 248, 240, alpha});
                // The puff it lands in, for the first moment.
                if (k < 0.18f) {
                    SDL_SetRenderDrawColor(r, 232, 220, 190, static_cast<Uint8>(170.0f * (1.0f - k / 0.18f)));
                    const SDL_FRect puff = {roundf((foot.x - 2.0f * z) / z) * z, roundf((foot.y - 0.5f * z) / z) * z, 4.0f * z, z};
                    SDL_RenderFillRect(r, &puff);
                }
            }
        }
    }
}

// A melee strike is drawn as well as animated. The character's swing is
// sixty-four pixels of arm; what the blow actually covers is the hitbox, and
// until this nothing showed it. So: a pale crescent swept through the arc the
// profile describes -- as far out as the reach, as wide as the width -- drawn
// faint through the wind-up, bright and advancing through the active frames,
// and gone with the recovery. A spear's thrust is a line driven out instead of
// a crescent; the Crushing Blow adds a streak down the middle of its arc; the
// Cross Cut's crescent is the whole circle; and each combo has its own tint,
// so what came out can be told from across the room.
void World::DrawSwing(SDL_Renderer* r) const {
    const AttackState& atk = player.Attack();
    if (!atk.Active() || player.Style() != AttackStyle::Melee || player.Rushing()) return;
    const AttackProfile& p = atk.profile;
    const float t = atk.timer;

    float alpha;
    if (t < p.windup)                 alpha = 0.30f * (t / std::max(0.01f, p.windup));
    else if (t < p.windup + p.active) alpha = 1.0f;
    else alpha = std::max(0.0f, 1.0f - (t - p.windup - p.active) / std::max(0.01f, p.recover * 0.6f));
    if (alpha <= 0.0f) return;
    // How far round the sweep has got: it starts late in the wind-up and has
    // covered the whole arc by the end of the active frames.
    const float from = p.windup * 0.5f;
    const float sweep = std::clamp((t - from) / std::max(0.01f, p.windup - from + p.active), 0.0f, 1.0f);
    if (sweep <= 0.0f) return;

    SDL_Color col = {255, 244, 200, 255};
    switch (atk.move) {
        case ComboMove::Crush:    col = {255, 200, 120, 255}; break;
        case ComboMove::Cleave:   col = {255, 168,  90, 255}; break;
        case ComboMove::Backhand: col = {214, 255, 214, 255}; break;
        case ComboMove::CrossCut: col = {196, 216, 255, 255}; break;
        default: break;
    }

    const float base = player.facing == FACE_RIGHT ? 0.0f : player.facing == FACE_DOWN ? 1.5707963f
                     : player.facing == FACE_LEFT ? 3.14159265f : -1.5707963f;
    const float reach = std::max(8.0f, p.reach * atk.reach_scale);
    float half = p.HalfAngle(reach);
    if (atk.move == ComboMove::CrossCut) half = 3.14159265f;
    const float cx = player.x, cy = player.y - 16.0f - player.draw_lift;
    const auto at = [&](float wx, float wy) {
        const SDL_FRect s = camera.ToScreenRect({wx, wy, 0.0f, 0.0f});
        return SDL_FPoint{s.x, s.y};
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const bool thrust = atk.move == ComboMove::None && player.AttackClip() == "thrust";
    // With the effects on the shader draws it, and the combo's own marks
    // (the Crushing Blow's streak falling onto the ground among them): see
    // world_strikes.cpp.
    if (DrawSwingShaded(r, cx, cy, base, half, reach, sweep, alpha, thrust)) return;
    // Every stroke is laid over a dark one two pixels wider, so the pale
    // crescent reads on the forest floor and the mine's flags as well as on
    // grass: on dark ground a light line alone was as good as invisible.
    const auto stroke = [&](SDL_FPoint a, SDL_FPoint b, float alpha_here) {
        SDL_SetRenderDrawColor(r, 12, 10, 16, static_cast<Uint8>(150.0f * std::clamp(alpha_here, 0.0f, 1.0f)));
        for (int dy = -1; dy <= 2; ++dy)
            for (int dx = -1; dx <= 2; ++dx)
                if (dx == -1 || dx == 2 || dy == -1 || dy == 2)
                    SDL_RenderLine(r, a.x + dx, a.y + dy, b.x + dx, b.y + dy);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, static_cast<Uint8>(255.0f * std::clamp(alpha_here, 0.0f, 1.0f)));
        SDL_RenderLine(r, a.x, a.y, b.x, b.y);
        SDL_RenderLine(r, a.x + 1.0f, a.y, b.x + 1.0f, b.y);
        SDL_RenderLine(r, a.x, a.y + 1.0f, b.x, b.y + 1.0f);
        SDL_RenderLine(r, a.x + 1.0f, a.y + 1.0f, b.x + 1.0f, b.y + 1.0f);
    };

    if (thrust) {
        // A line driven out along the facing.
        const float len = reach * sweep;
        const float dx = cosf(base), dy = sinf(base);
        stroke(at(cx, cy), at(cx + dx * len, cy + dy * len), alpha);
        return;
    }

    // The crescent: three rings, the middle one brightest, each a run of short
    // segments whose alpha rises toward the head of the sweep. A true arc:
    // the ground is drawn square -- a tile is as tall as it is wide -- and the
    // swing reaches as far up the screen as across it. Squashed to six tenths
    // it stopped short of what it struck above and below.
    constexpr int N = 18;
    const float a0 = base - half, a1 = base - half + 2.0f * half * sweep;
    // One bright crescent on its dark ground, rising toward the head of the
    // sweep, with a fainter ring just outside it.
    for (int ring = 0; ring <= 1; ++ring) {
        const float rad = reach + ring * 3.0f;
        const float ring_alpha = ring == 0 ? 1.0f : 0.4f;
        SDL_FPoint prev = at(cx + cosf(a0) * rad, cy + sinf(a0) * rad);
        for (int i = 1; i <= N; ++i) {
            const float a = a0 + (a1 - a0) * i / N;
            const SDL_FPoint pt = at(cx + cosf(a) * rad, cy + sinf(a) * rad);
            stroke(prev, pt, alpha * ring_alpha * (0.35f + 0.65f * i / N));
            prev = pt;
        }
    }
    if (atk.move == ComboMove::Crush) {
        // The overhead: a streak down the middle of the arc as it lands.
        stroke(at(cx + cosf(base) * reach * 0.25f, cy - 24.0f),
               at(cx + cosf(base) * reach * sweep, cy + sinf(base) * reach * sweep), alpha);
    }
}

// Taken from the body rather than from a pair of numbers, so it covers whoever
// is under it and goes on covering them if the rig ever grows.
SDL_FPoint World::ShieldDome(const Player& who) {
    const SDL_FRect body = who.BodyBox();
    const float tall = std::max(34.0f, who.y - body.y);    // crown of the head, above the feet
    const float wide = std::max(20.0f, body.w);
    const float high = tall + 12.0f;                       // and room to spare over it
    // Wide enough to read as a dome and not as an egg: never much narrower
    // than half its own height.
    return {std::max(wide * 0.5f + 11.0f, high * 0.52f), high};
}

void World::DrawMotes(SDL_Renderer* r) const {
    if (motes.empty()) return;
    const float z = camera.zoom;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Whole art pixels, on the art's own grid, like the dust: a speck that
    // slides between pixels is a smear among sprites that do not.
    const auto dot = [&](float sx, float sy, float side) {
        const SDL_FRect d = {roundf(sx / z) * z, roundf(sy / z) * z, side * z, side * z};
        SDL_RenderFillRect(r, &d);
    };
    for (const Mote& m : motes) {
        const float t = std::clamp(m.life / std::max(0.01f, m.max_life), 0.0f, 1.0f);      // one, new; nothing, gone
        const auto mix = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(b + (a - b) * t); };
        SDL_SetRenderDrawColor(r, mix(m.from.r, m.to.r), mix(m.from.g, m.to.g), mix(m.from.b, m.to.b), mix(m.from.a, m.to.a));
        const SDL_FPoint s = camera.ToScreen(m.x, m.y - m.lift);
        switch (m.kind) {
            case Mote::Kind::Speck: {
                const float side = std::max(1.0f, roundf(m.size));
                dot(s.x - side * z / 2.0f, s.y - side * z / 2.0f, side);
                if (m.tall > 0.0f) {
                    // A tongue: a narrower column standing on it, shorter as it dies.
                    const float up = roundf(m.tall * (0.35f + 0.65f * t));
                    const float narrow = std::max(1.0f, side - 1.0f);
                    const SDL_FRect column = {roundf((s.x - narrow * z / 2.0f) / z) * z,
                                              roundf((s.y - side * z / 2.0f) / z) * z - up * z, narrow * z, up * z};
                    SDL_RenderFillRect(r, &column);
                }
                break;
            }
            case Mote::Kind::Streak: {
                // A run of pixels along the way it is going.
                const float v = std::max(1.0f, Length(m.vx, m.vy));
                const int n = std::max(2, static_cast<int>(m.size));
                for (int i = 0; i < n; ++i) dot(s.x - m.vx / v * i * z, s.y - m.vy / v * i * z, 1.0f);
                break;
            }
            case Mote::Kind::Ring: {
                // Opening, a pixel at a time round it, and flat as the ground is.
                const float rx = m.size * z, ry = rx * 0.6f;
                const int steps = std::max(12, static_cast<int>(m.size * 4.0f));
                for (int i = 0; i < steps; ++i) {
                    const float a = 6.2831853f * i / steps;
                    dot(s.x + cosf(a) * rx, s.y + sinf(a) * ry, 1.0f);
                }
                break;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  Rendering
// -----------------------------------------------------------------------------

void World::Render(SDL_Renderer* r, TextureCache& cache) const {
    const SDL_Color bg = map.BackgroundColor();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);
    // What the shaders are to make of this view: see world_screen.cpp.
    if (Shaders::Effects()) Shaders::SetFrame(ScreenFrame(cache));
    if (InDream()) RenderStars(r);

    // The floor, then -- in the water, under the bridges and whatever else
    // lies on it -- what stands beside the water, upside down; then what lies
    // on the floor.
    vector<const TileInstance*> decor;
    map.CollectDecor(camera, decor);
    map.RenderLayer(r, cache, camera, LAYER_GROUND, 1);
    DrawReflections(r, cache, decor);
    map.RenderLayer(r, cache, camera, LAYER_GROUND, 2);
    // The exposed earth on the downhill side of every raised cell, drawn over
    // the ground and under everything that stands on it.
    map.RenderCliffs(r, cache, camera);
    // The light of the palace's windows, on its floor.
    DrawFloorLight(r);
    // Cracks in the ice, and the holes where it gave way.
    DrawIce(r);

    // Burning ground and pending eruptions lie on the floor, under everyone.
    // Drawn as a squashed disc rather than a rectangle: a hard-edged box reads
    // as a UI element, and this is meant to look like something on the grass.
    auto fill_disc = [&](float cx, float cy, float rx, float ry, SDL_Color c) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        const int rows = std::max(3, static_cast<int>(ry * 2));
        for (int i = 0; i < rows; ++i) {
            // Half-width of the disc at this height.
            const float t = (i + 0.5f) / rows * 2.0f - 1.0f;
            const float half = rx * sqrtf(std::max(0.0f, 1.0f - t * t));
            const SDL_FRect span = {cx - half, cy + t * ry, half * 2.0f, ry * 2.0f / rows + 1.0f};
            SDL_RenderFillRect(r, &span);
        }
    };

    for (const GroundEffect& g : ground_effects) {
        // Something else is drawing this one -- the Slabstrike's slab, falling
        // and then broken on top of whatever it landed on. A disc of earth over
        // that says nothing the chunks do not.
        if (g.quiet) continue;
        // As round as what it burns, and on the ground it lies on.
        const SDL_FPoint centre = camera.ToScreen(g.x, g.y - LiftAt(g.x, g.y));
        const float rx = g.radius * camera.zoom;
        const float ry = g.radius * camera.zoom;
        const SDL_Color c = ElementColor(g.Look());

        if (g.draw == GroundEffect::Draw::Whirlpool || g.draw == GroundEffect::Draw::Tornado ||
            g.draw == GroundEffect::Draw::Turbulence || g.draw == GroundEffect::Draw::Blades) {
            const float z = camera.zoom;
            const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            const float fade = std::clamp(g.life / 0.4f, 0.0f, 1.0f) * std::clamp((g.max_life - g.life) / 0.25f, 0.0f, 1.0f);
            const auto dotted = [&](float cx, float cy, float rx, float ry, float turn, int dots, int skip, SDL_Color col) {
                SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
                for (int i = 0; i < dots; ++i) {
                    if (skip > 0 && i % skip == skip - 1) continue;
                    const float a = 6.2831853f * i / dots + turn;
                    const SDL_FRect dot = {roundf((cx + cosf(a) * rx) / z) * z, roundf((cy + sinf(a) * ry) / z) * z, z, z};
                    SDL_RenderFillRect(r, &dot);
                }
            };
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            if (g.draw == GroundEffect::Draw::Whirlpool) {
                // Dark water, and rings of it turning inward: each goes round
                // faster than the one outside it.
                fill_disc(centre.x, centre.y, rx, ry * 0.62f, {34, 84, 168, static_cast<Uint8>(120 * fade)});
                fill_disc(centre.x, centre.y, rx * 0.45f, ry * 0.28f, {18, 46, 110, static_cast<Uint8>(150 * fade)});
                for (int ring = 0; ring < 5; ++ring) {
                    const float k = 1.0f - ring * 0.19f;
                    dotted(centre.x, centre.y, rx * k, ry * 0.62f * k, now * (1.6f + ring * 0.9f), static_cast<int>(54 * k) + 10, 4,
                           {static_cast<Uint8>(176 + ring * 16), static_cast<Uint8>(222 + ring * 6), 255, static_cast<Uint8>(230 * fade)});
                }
            } else if (g.draw == GroundEffect::Draw::Tornado) {
                // A funnel: rings stacked from the ground up, each wider than
                // the one under it and a little off to one side of it, turning.
                fill_disc(centre.x, centre.y, rx * 0.8f, ry * 0.3f, {60, 70, 80, static_cast<Uint8>(70 * fade)});
                for (int ring = 0; ring < 11; ++ring) {
                    const float up = ring * 7.0f * z;
                    const float k = 0.20f + ring * 0.085f;
                    const float sway = sinf(now * 5.0f + ring * 0.7f) * 6.0f * z * (ring / 10.0f);
                    // Two rings a level, the inner one grey: a line of white dots on
                    // grass is a sprinkle of salt, and a band with a shadow is a funnel.
                    dotted(centre.x + sway, centre.y - up + z, rx * k, ry * 0.30f * k, -now * (7.0f - ring * 0.3f) + ring,
                           static_cast<int>(64 * k) + 20, 5, {96, 112, 128, static_cast<Uint8>(200 * fade)});
                    dotted(centre.x + sway, centre.y - up, rx * k, ry * 0.30f * k, -now * (7.0f - ring * 0.3f) + ring,
                           static_cast<int>(64 * k) + 20, 5, {236, 246, 252, static_cast<Uint8>(245 * fade)});
                }
            } else if (g.draw == GroundEffect::Draw::Blades) {
                // The Hail of Blades: the tornado's turning column, made of
                // blades. Each is a short bar drawn along the way it is
                // travelling -- the tangent of the ring it is on -- with a
                // bright edge down one side and a dark spine down the other, so
                // it reads as a blade and not as a dash. They lie flat at the
                // bottom and stand up as they rise, and the whole swarm turns.
                fill_disc(centre.x, centre.y, rx * 0.85f, ry * 0.32f, {40, 36, 52, static_cast<Uint8>(80 * fade)});
                const auto blade = [&](float bx, float by, float ang, float len, Uint8 alpha) {
                    const float ca = cosf(ang), sa = sinf(ang);
                    for (float d = -len; d <= len; d += 1.0f) {
                        const float t = fabsf(d) / len;                 // 0 in the middle, 1 at the ends
                        const SDL_FRect px = {roundf((bx + ca * d * z) / z) * z,
                                              roundf((by + sa * d * z) / z) * z, z, z};
                        // Pale along the edge, dark along the back of it.
                        SDL_SetRenderDrawColor(r, static_cast<Uint8>(222 - 60 * t),
                                               static_cast<Uint8>(230 - 50 * t),
                                               static_cast<Uint8>(248 - 20 * t), alpha);
                        SDL_RenderFillRect(r, &px);
                        const SDL_FRect back = {px.x - sa * z, px.y + ca * z, z, z};
                        SDL_SetRenderDrawColor(r, 62, 58, 86, static_cast<Uint8>(alpha * 0.85f));
                        SDL_RenderFillRect(r, &back);
                    }
                };
                for (int i = 0; i < 42; ++i) {
                    const float tier = static_cast<float>(i % 7) / 6.0f;          // where up the column
                    const float k = 0.42f + 0.52f * tier;
                    const float up = tier * 24.0f * z;
                    const float turn = now * (7.4f - 3.0f * tier) + i * 1.43f;
                    const float bx = centre.x + cosf(turn) * rx * k;
                    const float by = centre.y - up + sinf(turn) * ry * 0.34f * k;
                    // Along the ring it is on, and standing up the higher it is.
                    const float ang = turn + 1.5707963f + tier * 0.9f * (i % 2 ? 1.0f : -1.0f);
                    blade(bx, by, ang, 3.5f + 2.5f * tier, static_cast<Uint8>(245 * fade));
                }
            } else {
                // Turbulence: no shape to it, only the air going every way at
                // once. Streaks of it, not dots -- each ring is a few long arcs
                // with gaps between, every other ring turning against its
                // neighbours, at two heights so it has a middle; a grey line
                // under each white one, because white specks on grass are a
                // sprinkle of salt; the ground paled under it all; and the dirt
                // and leaves it has picked up going round faster than any of it.
                const auto streaks = [&](float cx, float cy, float ax, float ay, float turn, int dots, int arcs, float fill, SDL_Color col) {
                    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
                    for (int i = 0; i < dots; ++i) {
                        const float along = static_cast<float>(i) * arcs / dots;
                        const float in_arc = along - floorf(along);
                        if (in_arc > fill) continue;
                        const float a = 6.2831853f * i / dots + turn;
                        const SDL_FRect dot = {roundf((cx + cosf(a) * ax) / z) * z, roundf((cy + sinf(a) * ay) / z) * z, z, z};
                        SDL_RenderFillRect(r, &dot);
                    }
                };
                fill_disc(centre.x, centre.y, rx, ry * 0.62f, {226, 240, 250, static_cast<Uint8>(34 * fade)});
                for (int ring = 0; ring < 6; ++ring) {
                    const float k = 1.0f - ring * 0.15f;
                    const float up = (ring % 2 ? 15.0f : 6.0f) * z;
                    const float turn = now * (ring % 2 ? 4.2f : -3.1f) + ring * 1.3f;
                    const int   dots = static_cast<int>(96 * k) + 12;
                    streaks(centre.x, centre.y - up + z, rx * k, ry * 0.62f * k, turn, dots, 3 + ring % 2, 0.56f,
                            {92, 110, 128, static_cast<Uint8>(190 * fade)});
                    streaks(centre.x, centre.y - up, rx * k, ry * 0.62f * k, turn, dots, 3 + ring % 2, 0.56f,
                            {238, 248, 255, static_cast<Uint8>(240 * fade)});
                }
                for (int bit = 0; bit < 9; ++bit) {
                    const float a = now * (5.0f + bit * 0.4f) * (bit % 2 ? 1.0f : -1.0f) + bit * 0.7f;
                    const float k = 0.35f + 0.07f * bit;
                    const float up = (4.0f + 3.0f * (bit % 5) + 3.0f * sinf(now * 6.0f + bit)) * z;
                    const SDL_FRect speck = {roundf((centre.x + cosf(a) * rx * k) / z) * z,
                                             roundf((centre.y + sinf(a) * ry * 0.62f * k - up) / z) * z, z * (1 + bit % 2), z};
                    if (bit % 3 == 0) SDL_SetRenderDrawColor(r, 96, 150, 70, static_cast<Uint8>(255 * fade));
                    else              SDL_SetRenderDrawColor(r, 122, 94, 62, static_cast<Uint8>(255 * fade));
                    SDL_RenderFillRect(r, &speck);
                }
            }
        } else if (g.delay > 0.0f) {
            // Telegraph the eruption: an outline that tightens as it arms.
            const float t = 1.0f - std::clamp(g.delay / 0.5f, 0.0f, 1.0f);
            fill_disc(centre.x, centre.y, rx * (0.55f + 0.45f * t), ry * (0.55f + 0.45f * t),
                      {c.r, c.g, c.b, static_cast<Uint8>(40 + 90 * t)});
        } else if (g.rain) {
            // Where it is raining: a faint floor and a dotted rim, steady for as
            // long as the arrows come and gone quickly once they stop. The
            // arrows themselves are drawn over the fighters: see DrawArrowRain.
            const float ending = std::clamp((g.life - GroundEffect::RAIN_LINGER) / 0.3f, 0.0f, 1.0f);
            fill_disc(centre.x, centre.y, rx, ry, {40, 30, 24, static_cast<Uint8>(46 * ending)});
            const float z = camera.zoom;
            const int dots = 72;
            const float turn = static_cast<float>(SDL_GetTicks()) / 1000.0f * 0.5f;
            SDL_SetRenderDrawColor(r, 255, 236, 170, static_cast<Uint8>(235 * ending));
            // Dashes, three dots on and one off, walking slowly round.
            for (int i = 0; i < dots; ++i) {
                if (i % 4 == 3) continue;
                const float a = 6.2831853f * i / dots + turn;
                const SDL_FRect dot = {roundf((centre.x + cosf(a) * rx) / z) * z, roundf((centre.y + sinf(a) * ry) / z) * z, z, z};
                SDL_RenderFillRect(r, &dot);
            }
        } else {
            const float t = std::clamp(g.life / std::max(0.01f, g.max_life), 0.0f, 1.0f);
            // A brighter core inside a wider glow.
            fill_disc(centre.x, centre.y, rx, ry,
                      {c.r, c.g, c.b, static_cast<Uint8>(70 * t)});
            fill_disc(centre.x, centre.y, rx * 0.6f, ry * 0.6f,
                      {c.r, c.g, c.b, static_cast<Uint8>(120 * t)});
        }
    }

    const SDL_FRect view_min = camera.VisibleWorldRect(32.0f);
    // Fishing spots: rings spreading on the water and the odd bubble, so a
    // place worth casting at can be told from the rest of the pond.
    {
        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        const float z = camera.zoom;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (const MapObject& o : map.Objects()) {
            if (o.type != "fishing_spot") continue;
            if (o.x < view_min.x || o.x > view_min.x + view_min.w || o.y < view_min.y || o.y > view_min.y + view_min.h)
                continue;
            for (int ring = 0; ring < 2; ++ring) {
                const float k = fmodf(t * 0.55f + ring * 0.5f + o.x * 0.013f, 1.0f);
                const float rx = 4.0f + k * 12.0f, ry = rx * 0.45f;
                SDL_SetRenderDrawColor(r, 220, 240, 255, static_cast<Uint8>(170.0f * (1.0f - k)));
                const int steps = 22;
                for (int i = 0; i < steps; ++i) {
                    const float a = 6.2831853f * i / steps;
                    const SDL_FPoint p = camera.ToScreen(o.x + cosf(a) * rx, o.y + sinf(a) * ry);
                    const SDL_FRect dot = {roundf(p.x / z) * z, roundf(p.y / z) * z, z, z};
                    SDL_RenderFillRect(r, &dot);
                }
            }
            const float bubble = fmodf(t * 1.3f + o.y * 0.07f, 1.0f);
            if (bubble < 0.35f) {
                const SDL_FPoint p = camera.ToScreen(o.x + 3.0f, o.y - 2.0f - bubble * 8.0f);
                SDL_SetRenderDrawColor(r, 240, 250, 255, 220);
                const SDL_FRect b = {roundf(p.x / z) * z, roundf(p.y / z) * z, z, z};
                SDL_RenderFillRect(r, &b);
            }
        }
    }

    // Sprint dust, on the ground under everything that stands on it. Square
    // puffs, snapped to the art's pixel grid so they sit with the sprites.
    for (const Dust& d : dust) {
        const float t = std::clamp(d.life / d.max_life, 0.0f, 1.0f);
        const float size = roundf(d.size * (1.6f - 0.6f * t)) * camera.zoom;
        const SDL_FPoint p = camera.ToScreen(d.x, d.y - LiftAt(d.x, d.y));
        const float px = roundf(p.x / camera.zoom) * camera.zoom - size / 2.0f;
        const float py = roundf(p.y / camera.zoom) * camera.zoom - size / 2.0f;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 214, 196, 160, static_cast<Uint8>(150 * t));
        const SDL_FRect puff = {px, py, size, size};
        SDL_RenderFillRect(r, &puff);
    }

    // A lock is a ring on the ground round the monster's feet, pulsing, under
    // everything that stands there. Rigs do not all stand on their anchor -- a
    // CraftPix orc's feet are a dozen pixels above it -- so the ring goes where
    // the feet are drawn, found from the bottom of the art in the idle sheet.
    if (const Enemy* t = targeting.Locked()) {
        float feet = 0.0f;
        const SpriteDef* def = t->sprite.Def();
        if (const AnimClip* idle = def ? def->Find("idle") : nullptr) {
            if (SDL_Texture* tex = idle->sheet.empty() ? nullptr : cache.Get(idle->sheet)) {
                float tw = 0, th = 0;
                SDL_GetTextureSize(tex, &tw, &th);
                const float fh = th / std::max(1, def->rows);
                const SDL_FRect ob = cache.OpaqueBounds(idle->sheet);
                float bottom = fmodf((ob.y + ob.h) * th, fh);
                if (bottom < 0.5f) bottom = fh;
                feet = std::max(0.0f, (def->anchor_y - bottom) * def->scale);
            }
        }
        const SDL_FRect body = t->BodyBox();
        const float rx = std::max(11.0f, body.w * 0.62f), ry = rx * 0.45f;
        const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) * 0.008f);
        const float z = camera.zoom;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, static_cast<Uint8>(200 + 55 * pulse), static_cast<Uint8>(52 + 50 * pulse),
                               static_cast<Uint8>(40 + 30 * pulse), 235);
        // One art pixel a step round the ellipse, so it reads as a line.
        const int steps = std::max(24, static_cast<int>(6.2831853f * std::max(rx, ry) * 1.2f));
        float last_x = -1e9f, last_y = -1e9f;
        for (int i = 0; i < steps; ++i) {
            const float a = 6.2831853f * i / steps;
            const SDL_FPoint s = camera.ToScreen(t->x + cosf(a) * rx,
                                                 t->y - feet - t->draw_lift + sinf(a) * ry);
            const float px = roundf(s.x / z) * z, py = roundf(s.y / z) * z;
            if (px == last_x && py == last_y) continue;
            last_x = px; last_y = py;
            const SDL_FRect dot = {px, py, z, z};
            SDL_RenderFillRect(r, &dot);
        }
    }

    // Everything at ground level draws in baseline order, so the player walks
    // behind a tree trunk and in front of the grass at its foot.
    struct Item { float sort_y; int kind; const void* ptr; };
    vector<Item> queue;

    queue.reserve(decor.size() + enemies.size() + npcs.size() + pickups.size() + 8);
    for (const TileInstance* t : decor) queue.push_back({t->sort_y, 0, t});

    const SDL_FRect view = camera.VisibleWorldRect(96.0f);

    for (const auto& o : map.Objects()) {
        // The ring is laid in the floor as an overlay, under everybody. What
        // stands in it is this character's totem, and sorts like anything
        // standing: `5`, below.
        if (o.type == "totem_circle") {
            if (!player.talents.PlacedTotem().empty()) queue.push_back({o.y, 5, &o});
            continue;
        }
        if (o.sprite.empty() || !ObjectPresent(o)) continue;
        if (o.x < view.x || o.x > view.x + view.w ||
            o.y < view.y || o.y > view.y + view.h) continue;
        queue.push_back({o.y, 3, &o});
    }
    for (const auto& p : pickups) {
        if (!RectsOverlap(p.Bounds(), view)) continue;
        queue.push_back({p.y, 2, &p});
    }
    for (const auto& p : projectiles) {
        if (!RectsOverlap(p.Bounds(), view)) continue;
        queue.push_back({p.y, 4, &p});
    }
    for (const auto& e : enemies) {
        if (e->CorpseGone()) continue;      // despawned, waiting to respawn
        if (!RectsOverlap(e->BodyBox(), view)) continue;
        queue.push_back({e->SortY(), 1, e.get()});
    }
    for (const auto& n : npcs) {
        if (n->Away() || !RectsOverlap(n->BodyBox(), view)) continue;
        queue.push_back({n->SortY(), 1, n.get()});
    }
    // Gone through the ice: under the water, a moment after the splash.
    if ((!player.IsDead() || player.DeathTimer() > 0.0f) && !player.under_ice)
        queue.push_back({player.SortY(), 1, &player});
    for (const auto& g : guests)
        if (!g->under_ice && RectsOverlap(g->BodyBox(), view)) queue.push_back({g->SortY(), 1, g.get()});

    std::stable_sort(queue.begin(), queue.end(),
                     [](const Item& a, const Item& b) { return a.sort_y < b.sort_y; });

    // Everything a piece of scenery must not be allowed to hide.
    struct Combatant { SDL_FRect box; float sort_y; };
    vector<Combatant> combatants;
    // Body boxes, not sprite frames: a 64px frame is mostly empty around a
    // figure twenty pixels wide.
    combatants.push_back({player.BodyBox(), player.SortY()});
    for (const auto& e : enemies) {
        if (e->CurrentState() == Enemy::State::Dead) continue;
        if (!RectsOverlap(e->BodyBox(), view)) continue;
        // Only what the player is actually fighting, or is about to. Every
        // grazing deer and hare used to count, so trees all over the
        // greenwood went see-through and back as the animals wandered behind
        // them, which read as a rendering fault rather than as help.
        const bool close = Length(e->x - player.x, e->y - player.y) < 140.0f;
        if (!e->Engaged() && !close) continue;
        combatants.push_back({e->BodyBox(), e->SortY()});
    }

    // True when this scenery is tall enough to swallow someone and is drawn
    // over one of them. Measured against the pixels the art actually draws:
    // tree images sit on canvases several times wider than the tree, and
    // testing the canvas faded a tree whenever the player walked past a
    // hundred pixels to one side of it.
    auto covers_someone = [&](const SDL_FRect& canvas, float sort_y, const string& path) {
        if (canvas.h <= 48.0f) return false;
        const SDL_FRect f = path.empty() ? SDL_FRect{0.0f, 0.0f, 1.0f, 1.0f}
                                         : cache.OpaqueBounds(path);
        const SDL_FRect art = {canvas.x + f.x * canvas.w, canvas.y + f.y * canvas.h,
                               f.w * canvas.w, f.h * canvas.h};
        for (const Combatant& c : combatants)
            if (sort_y > c.sort_y && RectsOverlap(art, c.box)) return true;
        return false;
    };

    // Ground fog lies over the floor and round everyone's ankles.
    Shaders::DrawFog(r);

    for (const Item& it : queue) {
        switch (it.kind) {
            case 0: {
                const TileInstance* t = static_cast<const TileInstance*>(it.ptr);
                // Decor tiles share the map texture list, so draw through the
                // map to keep that indirection in one place.
                map.RenderTile(r, cache, camera, *t,
                               covers_someone(t->rect, t->sort_y, map.TexturePath(*t)) ? 110 : 255);
                break;
            }
            case 1: {
                const Entity* e = static_cast<const Entity*>(it.ptr);
                e->Render(r, cache, camera);
                break;
            }
            case 2: {
                const Pickup* p = static_cast<const Pickup*>(it.ptr);
                const float bob = sinf(p->bob) * 2.0f;
                SDL_Texture* tex = p->icon.empty() ? nullptr : cache.Get(p->icon);
                SDL_FRect world = {p->x - 8.0f, p->y - 14.0f + bob - LiftAt(p->x, p->y), 16.0f, 16.0f};
                SDL_FRect dst = camera.ToScreenRect(world);
                if (tex) {
                    SDL_RenderTexture(r, tex, nullptr, &dst);
                } else {
                    SDL_SetRenderDrawColor(r, 240, 205, 90, 235);
                    SDL_RenderFillRect(r, &dst);
                    SDL_SetRenderDrawColor(r, 90, 70, 20, 255);
                    SDL_RenderRect(r, &dst);
                }
                break;
            }
            case 4: {
                const Projectile* p = static_cast<const Projectile*>(it.ptr);
                if (!p->def) break;
                const ProjectileDef& d = *p->def;

                // At the height it was loosed from, all the way: looked up
                // under it each frame it would drop a level crossing a bank.
                if (p->lift < 0.0f) p->lift = LiftAt(p->x, p->y);
                const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;

                // A light under what burns, by day as well. Added and not
                // painted, so the grass under a fireball is lit and not covered.
                if (d.glow > 0.0f) {
                    if (SDL_Texture* glow = cache.Get("assets/effects/glow.png")) {
                        const float across = d.glow * (0.88f + 0.12f * sinf(now * 23.0f + static_cast<float>(p->net_id)));
                        const SDL_FRect lit = camera.ToScreenRect({p->x - across / 2.0f, p->y - across / 2.0f - p->lift,
                                                                   across, across});
                        const SDL_Color c = ElementColor(d.element);
                        SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
                        SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
                        SDL_SetTextureAlphaMod(glow, 150);
                        SDL_RenderTexture(r, glow, nullptr, &lit);
                        SDL_SetTextureAlphaMod(glow, 255);
                        SDL_SetTextureColorMod(glow, 255, 255, 255);
                        SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
                    }
                }

                // One strip: the frame the clock says (the shot's own number
                // added, so a volley is not in step), at the art's own
                // proportions -- an arrow is long and thin, and forced into a
                // square it is a thrown brick -- held by its pivot on the
                // shot's position, and turned about that.
                const auto strip = [&](const string& path, int frames, float pvx, float pvy, double deg) {
                    SDL_Texture* tex = path.empty() ? nullptr : cache.Get(path);
                    float tw = 0, th = 0;
                    if (tex) SDL_GetTextureSize(tex, &tw, &th);
                    if (!tex || tw <= 0 || th <= 0) return false;
                    const float fw = tw / frames;
                    const int frame = frames > 1
                        ? (static_cast<int>(now * d.fps) + static_cast<int>(p->net_id % 64) * 3) % frames : 0;
                    if (pvx < 0.0f) pvx = fw / 2.0f;
                    if (pvy < 0.0f) pvy = th / 2.0f;
                    const SDL_FRect src = {frame * fw, 0.0f, fw, th};
                    const SDL_FRect dst = camera.ToScreenRect({p->x - pvx * d.scale, p->y - pvy * d.scale - p->lift,
                                                               fw * d.scale, th * d.scale});
                    const SDL_FPoint about = {dst.w * pvx / fw, dst.h * pvy / th};
                    SDL_SetTextureColorMod(tex, d.tint.r, d.tint.g, d.tint.b);
                    SDL_RenderTextureRotated(r, tex, &src, &dst, deg, &about, SDL_FLIP_NONE);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                    return true;
                };

                // One picture covers every direction: it is drawn turned to
                // face the way it is travelling. What is upright is not, and
                // what streams off the back of it is.
                const double along = p->angle * 57.2957795;
                if (!d.tail.empty()) strip(d.tail, d.tail_frames, d.tail_pivot_x, d.tail_pivot_y, along);
                const double deg = d.upright ? 0.0 : d.spin ? p->spin_angle * 57.2957795 : along + d.sprite_angle;
                if (!strip(d.sprite, d.frames, d.pivot_x, d.pivot_y, deg)) {
                    const SDL_FRect dst = camera.ToScreenRect({p->x - 8.0f * d.scale, p->y - 8.0f * d.scale - p->lift,
                                                               16.0f * d.scale, 16.0f * d.scale});
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, d.tint.r, d.tint.g, d.tint.b, 235);
                    SDL_RenderFillRect(r, &dst);
                }
                break;
            }
            case 5: {
                // A totem in the ring. The picture is the item's own; awake it
                // is as it is, and asleep it is dulled, the way a worked-out
                // seam is.
                const MapObject* o = static_cast<const MapObject*>(it.ptr);
                const ItemDef* thing = player.ItemDb() ? player.ItemDb()->Get(player.talents.PlacedTotem()) : nullptr;
                SDL_Texture* tex = thing ? cache.Get(thing->icon) : nullptr;
                if (!tex) break;
                float tw = 0, th = 0;
                SDL_GetTextureSize(tex, &tw, &th);
                const SDL_FRect dst = camera.ToScreenRect({o->x - tw / 2.0f, o->y + 4.0f - th, tw, th});
                const bool awake = player.talents.TotemAwake();
                if (!awake) SDL_SetTextureColorMod(tex, 128, 124, 132);
                SDL_RenderTexture(r, tex, nullptr, &dst);
                if (!awake) SDL_SetTextureColorMod(tex, 255, 255, 255);
                break;
            }
            case 3: {
                const MapObject* o = static_cast<const MapObject*>(it.ptr);
                // A strange thing in the grass: its own icon, lifted a little
                // off the ground, with a faint light under it and now and then
                // a glint off it -- so somebody looking finds it, and somebody
                // passing might.
                if (o->type == "curio") {
                    SDL_Texture* tex = cache.Get(o->sprite);
                    if (!tex) break;
                    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
                    const float seed = static_cast<float>((static_cast<int>(o->x) * 7 + static_cast<int>(o->y) * 13) % 97);
                    const float ox = o->x, oy = o->y - LiftAt(o->x, o->y);
                    const float bob = roundf(sinf(now * 2.2f + seed) * 1.5f);
                    if (SDL_Texture* glow = cache.Get("assets/effects/glow.png")) {
                        const float across = 30.0f + 4.0f * sinf(now * 1.7f + seed);
                        const SDL_FRect lit = camera.ToScreenRect({ox - across / 2.0f, oy - 9.0f - across / 2.0f, across, across});
                        SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
                        SDL_SetTextureColorMod(glow, 255, 234, 170);
                        SDL_SetTextureAlphaMod(glow, 80);
                        SDL_RenderTexture(r, glow, nullptr, &lit);
                        SDL_SetTextureAlphaMod(glow, 255);
                        SDL_SetTextureColorMod(glow, 255, 255, 255);
                        SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
                    }
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
                    const SDL_FRect shadow = camera.ToScreenRect({ox - 5.0f, oy - 2.0f, 10.0f, 2.0f});
                    SDL_RenderFillRect(r, &shadow);
                    const SDL_FRect dst = camera.ToScreenRect({ox - 8.0f, oy - 19.0f + bob, 16.0f, 16.0f});
                    SDL_RenderTexture(r, tex, nullptr, &dst);
                    // The glint: a four-pointed star on its shoulder, opening
                    // and closing again, once every few seconds.
                    const float cycle = fmodf(now + seed * 0.13f, 2.6f);
                    if (cycle < 0.45f) {
                        const float k = sinf(cycle / 0.45f * 3.14159265f);
                        const float arm = 1.0f + floorf(k * 2.6f);
                        const float z = camera.zoom;
                        const SDL_FPoint c = camera.ToScreen(ox + 5.0f, oy - 16.0f + bob);
                        const float cx = roundf(c.x / z) * z, cy = roundf(c.y / z) * z;
                        SDL_SetRenderDrawColor(r, 255, 250, 222, static_cast<Uint8>(255.0f * std::min(1.0f, k * 1.4f)));
                        const SDL_FRect across = {cx - arm * z, cy, (2.0f * arm + 1.0f) * z, z};
                        const SDL_FRect down = {cx, cy - arm * z, z, (2.0f * arm + 1.0f) * z};
                        SDL_RenderFillRect(r, &across);
                        SDL_RenderFillRect(r, &down);
                    }
                    break;
                }
                // A picked plant, a felled tree, a worked-out seam: drawn as
                // their after-picture when they have one. A seam has none --
                // it is still a rock -- so it is drawn dark and dull instead.
                const bool spent = (o->type == "herb" || o->deplete > 0.0f) ? Picked(*o) : Flagged(o->id);
                const bool used = !o->sprite_open.empty() && spent;
                const bool dulled = spent && o->sprite_open.empty();
                SDL_Texture* tex = cache.Get(used ? o->sprite_open : o->sprite);
                if (!tex) break;
                float tw = 0, th = 0;
                SDL_GetTextureSize(tex, &tw, &th);
                // Objects stand on their position, like characters do.
                const SDL_FRect world = {o->x - tw / 2.0f, o->y - th, tw, th};
                const SDL_FRect dst = camera.ToScreenRect(world);

                // Tall scenery drawn in front of someone goes translucent
                // while it overlaps them, so nobody fights behind a bush.
                const Uint8 alpha = covers_someone(world, o->y, used ? o->sprite_open : o->sprite)
                                        ? 110 : 255;

                // A tree in the wind, a woken waystone breathing (no-ops off
                // the GPU renderer).
                const Shaders::PropKind kind = dulled ? Shaders::PROP_NONE
                                                      : Shaders::ArtOf(used ? o->sprite_open : o->sprite).kind;
                if (kind != Shaders::PROP_NONE) Shaders::UseTile(r, Shaders::PLAIN, kind);
                SDL_SetTextureAlphaMod(tex, alpha);
                if (dulled) SDL_SetTextureColorMod(tex, 118, 112, 108);
                SDL_RenderTexture(r, tex, nullptr, &dst);
                if (dulled) SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                if (kind != Shaders::PROP_NONE) Shaders::UsePlain(r);
                break;
            }
        }
    }

    // The Slabstrike's square of torn-up ground: swung through an arc in front
    // of the caster, or dropped on somebody and broken on top of them.
    //
    // Seen edge-on, so what shows is the earthy side of it, with the turf still
    // along its top edge and its shadow on the ground under it. The one before
    // this was a bar four pixels wide and it read as a sawn plank -- straight
    // edges its whole length, one unbroken highlight down the leading side,
    // cracks at regular intervals across it. So: the outline is bitten into and
    // lumped out, the highlight is in pieces, and the stone is three tones
    // scattered rather than one with a stripe. What each pixel is made of comes
    // from where it is *on the slab*, not on the screen, so the stone does not
    // crawl as the slab travels.
    for (const SlabSwing& s : slabs) {
        if (s.life <= 0.0f) continue;
        const float z = camera.zoom;
        const auto grain = [](int d, int k) {
            uint32_t h = static_cast<uint32_t>(d) * 2654435761u ^ static_cast<uint32_t>(k) * 40503u;
            h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
            return static_cast<int>(h >> 28);                    // 0..15
        };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const auto dot = [&](float wx, float wy, SDL_Color c) {
            const SDL_FPoint at = camera.ToScreen(wx, wy);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            const SDL_FRect px = {roundf(at.x / z) * z, roundf(at.y / z) * z, z, z};
            SDL_RenderFillRect(r, &px);
        };
        // One block of ground, `n` across, standing `up` above the point it is
        // over. `lead` is the side that is swinging forward and catches the
        // light: -1, +1, or 0 for a chunk of debris, which catches none.
        //
        // Everything about it is worked out from `n`, because the same code
        // draws a twenty-pixel slab, a thirty-two-pixel one and the five-pixel
        // chunks it breaks into. At five pixels a knocked-off corner is a pixel
        // and the stone is one tone; at thirty-two, a corner is a proper chipped
        // wedge, the turf on top is a band rather than a line, and the face
        // wants cracks in it -- a square that big with nothing but per-pixel
        // noise on it reads as static, not stone.
        const auto block = [&](float cx, float cy, int n, float up, int seed, int alpha, int lead) {
            const int half = std::max(1, n / 2);
            const int turf_deep = std::max(1, n / 6);          // how thick the sod on top is
            const int chip = std::max(1, n / 5);               // how deep the corners are knocked off
            const int patch = n >= 10 ? 2 : 1;                 // how coarse the mottling is
            const float high = std::clamp(up / (24.0f + n * 1.6f), 0.0f, 1.0f);
            // Its shadow, which draws in and darkens as it comes down.
            const int sw = std::max(0, static_cast<int>(half * (1.25f - 0.5f * high)));
            for (int ix = -sw; ix <= sw; ++ix)
                for (int iy = 0; iy < std::max(1, n / 6); ++iy)
                    dot(cx + static_cast<float>(ix), cy + half * 0.55f + static_cast<float>(iy),
                        {24, 18, 12, static_cast<Uint8>(alpha * (0.46f - 0.24f * high))});
            for (int iy = -half; iy <= half; ++iy)
                for (int ix = -half; ix <= half; ++ix) {
                    const int in_x = half - std::abs(ix), in_y = half - std::abs(iy);   // how far in from the sides
                    // A wedge knocked off each corner, a different one each.
                    const int corner = seed * 7 + (ix < 0 ? 1 : 2) * 31 + (iy < 0 ? 3 : 5) * 17;
                    if (in_x + in_y < chip - grain(corner, 1) % (chip + 1)) continue;
                    // And bites out of the edges between them.
                    if ((in_x == 0 || in_y == 0) && grain(seed * 101 + ix * 13 + iy * 7, 2) < 3) continue;
                    const int g = grain(seed * 101 + ((iy + 64) / patch) * 19 + (ix + 64) / patch, 3);
                    const bool rim  = (ix == -half || ix == half || iy == half) && !(lead != 0 && ix == lead * half);
                    const bool turf = iy < -half + turf_deep;
                    const bool soil = iy == -half + turf_deep && n >= 10;   // the dark line under the sod
                    const bool lit  = lead != 0 && ix == lead * half && g > 5;
                    // Cracks: dashes running down and across the face, so the
                    // stone is broken rather than merely speckled.
                    const bool crack = n >= 12 && !turf && !rim &&
                                       grain(seed * 13 + ix, 9) == 0 && grain(seed * 5 + iy / 3, 10) > 9;
                    SDL_Color c;
                    if (turf)        c = {static_cast<Uint8>(58 + g * 2), static_cast<Uint8>(104 + g * 2), 48, 255};
                    else if (soil)   c = {68, 52, 40, 255};
                    else if (lit)    c = {232, 222, 196, 255};
                    else if (rim)    c = {78, 62, 50, 255};
                    else if (crack)  c = {92, 76, 60, 255};
                    else if (g < 3)  c = {116, 98, 78, 255};       // a clod of earth
                    else if (g < 10) c = {170, 152, 122, 255};
                    else             c = {202, 186, 154, 255};
                    c.a = static_cast<Uint8>(alpha);
                    dot(cx + static_cast<float>(ix), cy + static_cast<float>(iy) - up, c);
                }
            // Blades of grass hanging off the top edge.
            if (alpha > 200)
                for (int ix = -half; ix <= half; ++ix)
                    if (grain(seed * 101 + ix, 8) < 4)
                        dot(cx + static_cast<float>(ix), cy - half - 1.0f - up, {92, 146, 70, 255});
        };

        if (!s.drop) {
            // Where it has just been, fading: one small square in one place is a
            // rock sitting in the air, and three behind it are a swing.
            const int lead = s.to > s.from ? 1 : -1;
            // How far back they are set depends on how big it is: three ghosts a
            // sixteenth of the swing apart are a blur behind a small square and
            // a wall behind a big one.
            const int ghosts = s.side >= 12.0f ? 1 : 3;
            const float step = s.side >= 12.0f ? 0.11f : 0.06f;
            for (int back = ghosts; back >= 0; --back) {
                const float p = s.Progress() - static_cast<float>(back) * step;
                if (p < 0.0f) continue;
                const float a = s.AngleAt(p), out = s.OutAt(p);
                const float cx = s.x + cosf(a) * out, cy = s.y + sinf(a) * out * 0.72f - s.lift;
                static const int kFade[4] = {255, 78, 80, 50};
                block(cx, cy, static_cast<int>(s.side), 7.0f, 1, kFade[back], back ? 0 : lead);
            }
        } else if (s.Fallen() < 1.0f) {
            // Still in the air, coming down fast at the end.
            const float k = s.Fallen();
            block(s.x, s.y - s.lift, static_cast<int>(s.side), (1.0f - k * k) * (46.0f + s.side * 1.4f), 2, 255, 0);
        } else {
            // Broken on top of whatever it landed on: chunks sliding out from
            // under it, settling and fading.
            const float k = s.Broken();
            const int   n = static_cast<int>(s.side);
            for (int i = 0; i < 6; ++i) {
                const float a = 6.2831853f * i / 6.0f + 0.4f;
                const float out = (0.35f + 1.05f * (1.0f - (1.0f - k) * (1.0f - k))) * s.side *
                                  (0.7f + 0.1f * static_cast<float>(grain(i, 5) % 4));
                const int   bit = std::max(3, n / 4 - grain(i, 6) % 2);
                block(s.x + cosf(a) * out, s.y - s.lift + sinf(a) * out * 0.6f, bit,
                      std::max(0.0f, 5.0f - 10.0f * k), 3 + i,
                      static_cast<int>(255.0f * std::clamp(1.0f - (k - 0.55f) / 0.45f, 0.0f, 1.0f)), 0);
            }
        }
    }

    // A meteor on its way down, over everything it is about to land on.
    for (const Falling& f : falls) {
        if (f.life <= 0.0f) continue;
        const float z = camera.zoom;
        const float k = f.Above();                       // 1 when it is let go, 0 as it strikes
        const float rad = f.size * 0.5f;
        const SDL_Color c = ElementColor(f.element);
        // Its shadow, drawing in and darkening under it: the one thing that
        // says where it is going to land rather than where it is now.
        const SDL_FPoint down = camera.ToScreen(f.x, f.y - f.lift);
        fill_disc(down.x, down.y, rad * z * (0.45f + 0.55f * (1.0f - k)), rad * z * 0.34f * (0.45f + 0.55f * (1.0f - k)),
                  {16, 12, 10, static_cast<Uint8>(50 + 90 * (1.0f - k))});
        // Where it is: up and back along the way it came.
        const auto seat = [&](float t) {
            return camera.ToScreen(f.x - f.Lead() * t, f.y - f.Drop() * t - f.lift);
        };
        // The tail: puffs strung out behind it, hottest at the head.
        for (int i = 8; i >= 1; --i) {
            const float t = k + static_cast<float>(i) * 0.055f;
            if (t > 1.25f) continue;
            const SDL_FPoint at = seat(t);
            const float w = rad * z * (0.62f - 0.05f * i);
            if (w <= 1.0f) continue;
            const Uint8 a = static_cast<Uint8>(std::max(0.0f, 150.0f - 17.0f * i));
            fill_disc(at.x, at.y, w, w, {c.r, c.g, c.b, a});
        }
        const SDL_FPoint at = seat(k);
        // The light it throws, added rather than painted, so the ground under
        // it is lit and not covered.
        if (SDL_Texture* glow = cache.Get("assets/effects/glow.png")) {
            // Twice across, not five times: this thing is already as wide as the
            // ground it is about to cover, and a glow five times that is a sunset.
            const float across = rad * 2.2f * z;
            const SDL_FRect lit = {at.x - across / 2.0f, at.y - across / 2.0f, across, across};
            SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(glow, 150);
            SDL_RenderTexture(r, glow, nullptr, &lit);
            SDL_SetTextureAlphaMod(glow, 255);
            SDL_SetTextureColorMod(glow, 255, 255, 255);
            SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
        }
        // The fire round it, then the rock itself -- set back from the leading
        // edge, so what burns is the face that is going first.
        {
            const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            const SDL_Color hot = {static_cast<Uint8>(std::min(255, c.r + 60)),
                                   static_cast<Uint8>(std::min(255, c.g + 70)),
                                   static_cast<Uint8>(std::min(255, c.b + 40)), 255};
            for (int i = 0; i < 26; ++i) {
                const float a = 6.2831853f * i / 26.0f;
                const float puff = rad * z * (0.19f + 0.07f * sinf(now * 9.0f + i * 1.7f));
                const float d = rad * z * (0.88f + 0.09f * sinf(now * 7.0f + i * 2.3f));
                fill_disc(at.x + cosf(a) * d, at.y + sinf(a) * d, puff, puff, {c.r, c.g, c.b, 205});
            }
            for (int i = 0; i < 7; ++i) {
                const float a = 6.2831853f * i / 7.0f + now * 1.6f;
                const float puff = rad * z * (0.22f + 0.07f * sinf(now * 11.0f + i * 2.9f));
                fill_disc(at.x + cosf(a) * rad * z * 0.74f, at.y + sinf(a) * rad * z * 0.74f,
                          puff, puff, {hot.r, hot.g, hot.b, 190});
            }
        }
        const float back_x = f.Lead() / std::max(1.0f, f.Drop()) * rad * 0.22f * z;
        const float cx = at.x - back_x, cy = at.y - rad * z * 0.20f;
        // The rock itself: a lit ball with craters cut into it, worked out a
        // pixel at a time on the sprites' own grid. It was a flat disc with
        // darker discs laid on it, which is a coin with spots -- what makes a
        // crater read as a hollow rather than a stain is that the wall facing
        // the light is the dark one, the wall facing away catches the light,
        // and a rim stands proud of both.
        {
            const int R = std::max(3, static_cast<int>(rad));
            const float lx = -0.52f, ly = -0.60f, lz = 0.61f;     // the light, from the upper left
            // Where it is going, on the screen: the face that leads is the face
            // that is burning.
            const float vlen = std::max(1.0f, sqrtf(f.Lead() * f.Lead() + f.Drop() * f.Drop()));
            const float ux = f.Lead() / vlen, uy = f.Drop() / vlen;
            const auto hash = [](int a, int b) {
                uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
                h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
                return static_cast<int>(h >> 28);                 // 0..15
            };
            // The craters, in the rock's own frame so they do not crawl as it falls.
            struct Pit { float x, y, r; };
            static const Pit kPits[] = {
                {-0.34f, -0.24f, 0.32f}, {0.28f, -0.34f, 0.21f}, {0.30f, 0.26f, 0.27f},
                {-0.16f, 0.42f, 0.19f}, {-0.04f, 0.04f, 0.16f}, {0.52f, -0.04f, 0.14f},
                {-0.52f, 0.20f, 0.15f},
            };
            static const SDL_Color kStone[5] = {
                {168, 150, 132, 255}, {126, 108, 96, 255}, {94, 78, 70, 255},
                {66, 53, 48, 255}, {44, 35, 33, 255}};
            const SDL_Color hot = {static_cast<Uint8>(std::min(255, c.r + 40)),
                                   static_cast<Uint8>(std::min(255, c.g + 30)),
                                   static_cast<Uint8>(std::min(255, c.b + 20)), 255};
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            for (int iy = -R; iy <= R; ++iy) {
                // One row at a time, in runs of a colour: a fill for every pixel
                // of something this size is ten thousand draws a frame.
                int run_from = 0;
                bool running = false;
                SDL_Color run_col{0, 0, 0, 0};
                const float py = cy + static_cast<float>(iy) * z;
                const auto flush = [&](int to) {
                    if (!running) return;
                    SDL_SetRenderDrawColor(r, run_col.r, run_col.g, run_col.b, 255);
                    const SDL_FRect span = {roundf((cx + static_cast<float>(run_from) * z) / z) * z,
                                            roundf(py / z) * z,
                                            static_cast<float>(to - run_from) * z, z};
                    SDL_RenderFillRect(r, &span);
                    running = false;
                };
                for (int ix = -R; ix <= R; ++ix) {
                    const float nx = static_cast<float>(ix) / R, ny = static_cast<float>(iy) / R;
                    const float d2 = nx * nx + ny * ny;
                    // A broken edge rather than a compass circle.
                    const float edge = 1.0f - 0.06f * static_cast<float>(hash(ix / 3, iy / 3)) / 15.0f;
                    if (d2 > edge * edge) { flush(ix); continue; }
                    const float nz = sqrtf(std::max(0.0f, 1.0f - d2));
                    float lam = nx * lx + ny * ly + nz * lz;
                    for (const Pit& pit : kPits) {
                        const float ox = nx - pit.x, oy = ny - pit.y;
                        const float rr = sqrtf(ox * ox + oy * oy) / pit.r;
                        if (rr > 1.22f) continue;
                        // Inside the bowl the ground tilts toward its middle, so
                        // the side nearer the light turns away from it.
                        if (rr < 0.90f) lam -= (ox * lx + oy * ly) / pit.r * 0.80f * (1.0f - 0.45f * rr);
                        else            lam += 0.20f;                 // the rim, standing proud
                    }
                    // A little grain, so the bands are not five clean stripes.
                    const int g = hash(ix / 2 + 64, iy / 2 + 64);
                    lam += (g - 7) * 0.006f;
                    const int band = lam > 0.80f ? 0 : lam > 0.58f ? 1 : lam > 0.36f ? 2 : lam > 0.17f ? 3 : 4;
                    SDL_Color col = kStone[band];
                    // Burning up on the way in: the leading face glows, hottest
                    // at the very edge of it.
                    const float front = nx * ux + ny * uy;
                    const float heat = std::clamp((front - 0.28f) / 0.72f, 0.0f, 1.0f) *
                                       std::clamp((d2 / (edge * edge) - 0.20f) / 0.80f, 0.0f, 1.0f);
                    if (heat > 0.02f) {
                        col.r = static_cast<Uint8>(col.r + (hot.r - col.r) * heat);
                        col.g = static_cast<Uint8>(col.g + (hot.g - col.g) * heat);
                        col.b = static_cast<Uint8>(col.b + (hot.b - col.b) * heat);
                    }
                    if (running && col.r == run_col.r && col.g == run_col.g && col.b == run_col.b) continue;
                    flush(ix);
                    run_from = ix;
                    run_col = col;
                    running = true;
                }
                flush(R + 1);
            }
        }
    }

    // A claw thrown out at the end of the arm and raked across whatever is in
    // front of it: the Vampiric Touch's, and the Ice Touch's talons. Four
    // talons and a thumb, each a curve that tapers to a point, drawn from the
    // knuckle out; then the gashes they leave, which are what say the thing was
    // scratched rather than merely reached at.
    for (const ClawSwipe& cl : claws) {
        if (cl.life <= 0.0f) continue;
        const float z = camera.zoom;
        const bool ice = cl.look == 1;
        // Blood and bone, or ice: a dark body, a bright edge, and a paler point.
        const SDL_Color dark = ice ? SDL_Color{28, 74, 122, 255} : SDL_Color{74, 12, 22, 255};
        const SDL_Color mid  = ice ? SDL_Color{120, 196, 240, 255} : SDL_Color{168, 34, 50, 255};
        const SDL_Color tip  = ice ? SDL_Color{232, 250, 255, 255} : SDL_Color{238, 126, 132, 255};
        const float a0 = cl.facing + cl.Rake() * CLAW_SWEEP;
        const float out = cl.Out();
        const float fade = std::clamp(cl.life / 0.14f, 0.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const auto dot = [&](float wx, float wy, SDL_Color c, float wide) {
            const SDL_FPoint at = camera.ToScreen(wx, wy - cl.lift);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, static_cast<Uint8>(c.a * fade));
            const float w = std::max(z, roundf(wide) * z);
            const SDL_FRect px = {roundf((at.x - w / 2.0f) / z) * z, roundf((at.y - w / 2.0f) / z) * z, w, w};
            SDL_RenderFillRect(r, &px);
        };
        // One talon: walked from the knuckle to the point, turning as it goes,
        // thinning as it goes, so it is a hooked claw and not a spike.
        const auto talon = [&](float kx, float ky, float ang, float len, float curl, float thick) {
            // A step a pixel: at one every two the talon is a row of beads.
            const int steps = std::max(6, static_cast<int>(len));
            float px = kx, py = ky, a = ang;
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / steps;
                const float w = std::max(1.0f, thick * (1.0f - t * 0.88f));
                dot(px, py, dark, w + 1.6f);
                dot(px, py, t > 0.72f ? tip : mid, w);
                px += cosf(a) * (len / steps);
                py += sinf(a) * (len / steps) * 0.72f;
                a += curl / steps;
            }
        };
        // The knuckle, a little way out along the arm.
        const float hand = cl.reach * 0.30f * out;
        const float hx = cl.x + cosf(a0) * hand, hy = cl.y + sinf(a0) * hand * 0.72f;
        const float len = cl.reach * 0.62f * out;
        dot(hx, hy, dark, 7.0f);
        dot(hx - cosf(a0) * 2.0f, hy - sinf(a0) * 1.5f, mid, 4.0f);
        // Four fingers splayed across the swipe, and a thumb under them.
        static const float kSplay[4] = {-0.46f, -0.15f, 0.15f, 0.46f};
        for (int i = 0; i < 4; ++i) {
            const float side = kSplay[i];
            const float grip = 0.55f + 0.30f * cl.Rake() * (side > 0.0f ? 1.0f : -1.0f);
            talon(hx, hy, a0 + side, len * (i == 1 || i == 2 ? 1.0f : 0.82f), grip, 3.4f);
        }
        talon(hx, hy, a0 - 0.95f, len * 0.55f, 0.85f, 3.0f);
        // What it opened: gashes across the far end, once the rake is under way.
        if (cl.Progress() > 0.38f) {
            const float gash = std::clamp((cl.Progress() - 0.38f) / 0.30f, 0.0f, 1.0f);
            const float gx = cl.x + cosf(a0) * cl.reach * 0.86f;
            const float gy = cl.y + sinf(a0) * cl.reach * 0.86f * 0.72f;
            for (int i = -1; i <= 1; ++i) {
                const float across = a0 + 1.5707963f;
                const float sx = gx + cosf(across) * i * 7.0f, sy = gy + sinf(across) * i * 7.0f * 0.72f;
                const float half = 9.0f * gash;
                for (float d = -half; d <= half; d += 1.0f) {
                    const float t = 1.0f - fabsf(d) / std::max(1.0f, half);
                    dot(sx + cosf(a0) * d, sy + sinf(a0) * d * 0.72f, t > 0.55f ? tip : mid, t > 0.3f ? 2.0f : 1.0f);
                }
            }
        }
    }

    // --- the lightning ---------------------------------------------------------
    // A node standing where it was thrown: a translucent orb with the charge
    // turning inside it. Under the arcs, because the arcs come out of it.
    {
        const float z = camera.zoom;
        const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (const Node& n : nodes) {
            if (n.life <= 0.0f) continue;
            const SDL_FPoint at = camera.ToScreen(n.x, n.y - n.lift - 12.0f);
            // It fades over its last half second and shrinks a little with it,
            // so a node running out says so before it goes.
            const float left = std::clamp(n.life / 0.5f, 0.0f, 1.0f);
            const float beat = 1.0f + 0.08f * sinf(now * 7.0f + n.x * 0.05f);
            const float rad = 15.0f * z * beat * (0.7f + 0.3f * left);
            // With the shader: an orb of glass with the charge swirling
            // through it and sparking, worked out a pixel at a time.
            {
                Shaders::ShapeFx fx;
                fx.shape = Shaders::SHAPE_NODE;
                fx.fade = left;
                fx.colour = {0.74f, 0.88f, 1.0f, 1.0f};
                const SDL_FRect orb = {at.x - rad, at.y - rad * 0.92f, rad * 2.0f, rad * 1.84f};
                if (Shaders::DrawShape(r, orb, fx, SDL_BLENDMODE_BLEND)) continue;
            }
            // Glass: filled, but barely -- what is behind it is still read
            // through it, which is what makes it an orb and not a coin. Drawn
            // as rows so the fill is an ellipse and not a square.
            const auto disc = [&](float rr, SDL_Color c) {
                const int rows = std::max(4, static_cast<int>(rr * 2.0f / z));
                for (int i = 0; i < rows; ++i) {
                    const float t = -1.0f + 2.0f * (i + 0.5f) / rows;
                    const float half = rr * sqrtf(std::max(0.0f, 1.0f - t * t));
                    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
                    const SDL_FRect row = {roundf((at.x - half) / z) * z,
                                           roundf((at.y + t * rr * 0.92f) / z) * z,
                                           std::max(z, roundf(half * 2.0f / z) * z), z};
                    SDL_RenderFillRect(r, &row);
                }
            };
            // Pale and a little blue inside, where the ground it stands on is
            // earth: a yellow orb on brown dirt is a yellow patch of dirt.
            disc(rad, {206, 230, 255, static_cast<Uint8>(62 * left)});
            disc(rad * 0.62f, {236, 246, 255, static_cast<Uint8>(76 * left)});
            // The rim, brightest where the glass is seen edge-on.
            SDL_SetRenderDrawColor(r, 255, 250, 200, static_cast<Uint8>(225 * left));
            for (int i = 0; i < 30; ++i) {
                const float a = 6.2831853f * i / 30.0f;
                const SDL_FRect px = {roundf((at.x + cosf(a) * rad) / z) * z,
                                      roundf((at.y + sinf(a) * rad * 0.92f) / z) * z, z, z};
                SDL_RenderFillRect(r, &px);
            }
            // The charge turning inside it, each spark with a short tail so the
            // eye sees it going round rather than three dots jumping about.
            for (int i = 0; i < 3; ++i) {
                for (int tail = 0; tail < 4; ++tail) {
                    const float a = now * 4.5f - tail * 0.16f + 6.2831853f * i / 3.0f;
                    const float rr = rad * 0.52f;
                    SDL_SetRenderDrawColor(r, 255, 255, 235,
                                           static_cast<Uint8>((230 - tail * 52) * left));
                    const float w = tail == 0 ? z * 2.0f : z;
                    const SDL_FRect px = {roundf((at.x + cosf(a) * rr) / z) * z,
                                          roundf((at.y + sinf(a) * rr * 0.8f) / z) * z, w, w};
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
    }

    // An arc: a jagged line drawn for a fifth of a second. The jags are settled
    // from the arc's own seed rather than rolled each frame -- a bolt redrawn
    // from new numbers sixty times a second is a flicker and not a bolt -- and
    // it is drawn three times over: a wide dim body, a core, and a white spine.
    {
        const float z = camera.zoom;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (const Arc& a : arcs) {
            if (a.life <= 0.0f) continue;
            // Bright for the first third and falling away after: lightning is
            // gone before the eye has finished with it.
            const float p = a.Progress();
            const float bright = p < 0.3f ? 1.0f : 1.0f - (p - 0.3f) / 0.7f;
            if (bright <= 0.0f) continue;
            const float ax = cosf(a.facing), ay = sinf(a.facing) * 0.9f;
            const float nx = -sinf(a.facing), ny = cosf(a.facing) * 0.9f;
            // Steps of about six pixels, and a sideways wander that is widest
            // in the middle and nothing at either end -- both ends are pinned.
            const int steps = std::clamp(static_cast<int>(a.reach / 6.0f), 4, 40);
            uint32_t seed = a.seed;
            const auto noise = [&]() {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                return static_cast<float>(seed & 1023u) / 1023.0f * 2.0f - 1.0f;
            };
            const float wander = a.look == 1 ? 9.0f : 6.0f;
            SDL_FPoint prev = camera.ToScreen(a.x, a.y);
            for (int i = 1; i <= steps; ++i) {
                const float t = static_cast<float>(i) / steps;
                const float off = (i == steps ? 0.0f : noise() * wander * sinf(t * 3.14159265f));
                const float wx = a.x + ax * a.reach * t + nx * off;
                const float wy = a.y + ay * a.reach * t + ny * off;
                const SDL_FPoint now_pt = camera.ToScreen(wx, wy);
                // Three passes over the same segment, widest and dimmest first.
                for (int pass = 0; pass < 3; ++pass) {
                    const float wide = (pass == 0 ? 5.0f : pass == 1 ? 3.0f : 1.0f) * z;
                    const SDL_Color c = pass == 0 ? SDL_Color{250, 210, 60, 255}
                                      : pass == 1 ? SDL_Color{255, 244, 150, 255}
                                                  : SDL_Color{255, 255, 255, 255};
                    SDL_SetRenderDrawColor(r, c.r, c.g, c.b,
                                           static_cast<Uint8>((pass == 0 ? 90 : pass == 1 ? 190 : 255) * bright));
                    // Walked a pixel at a time: SDL_RenderLine will not thicken.
                    const float dx = now_pt.x - prev.x, dy = now_pt.y - prev.y;
                    const int n = std::max(1, static_cast<int>(Length(dx, dy) / z));
                    for (int s = 0; s <= n; ++s) {
                        const float px = prev.x + dx * s / n, py = prev.y + dy * s / n;
                        const SDL_FRect q = {roundf((px - wide / 2.0f) / z) * z,
                                             roundf((py - wide / 2.0f) / z) * z, wide, wide};
                        SDL_RenderFillRect(r, &q);
                    }
                }
                prev = now_pt;
            }
        }
    }

    // A dome of mana over anyone holding the shield up: see Player::ManaShield.
    // Translucent, so what it covers is still read through it -- the point of it
    // is to say the shield is up, not to hide the fight.
    {
        const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        vector<const Player*> everyone{&player};
        for (const auto& g : guests) everyone.push_back(g.get());
        for (const Player* who : everyone) {
            if (!who || !who->ShieldUp()) continue;
            const float z = camera.zoom;
            // It draws in over the last half second rather than blinking out.
            const float left = who->ManaShieldLeft();
            const float fade = left > 0.0f ? std::clamp(left / 0.5f, 0.0f, 1.0f) : 1.0f;
            const float beat = 1.0f + 0.035f * sinf(now * 4.2f);
            // Anchored to whoever is under it, every frame, at their feet and
            // lifted with them: it goes where they go and rises with a jump.
            const SDL_FPoint foot = camera.ToScreen(who->x, who->y - who->draw_lift);
            // Big enough to have them inside it, head and all: see ShieldDome.
            const SDL_FPoint size = ShieldDome(*who);
            const float rx = size.x * z * beat, ry = size.y * z * beat;
            // With the shader: a skin brightest where it is seen edge-on, a
            // honeycomb faint in it, and a ripple across it from a blow it
            // took -- and its ring on the ground under it.
            {
                Shaders::ShapeFx fx;
                fx.shape = Shaders::SHAPE_DOME;
                fx.fade = fade;
                fx.colour = {0.66f, 0.46f, 1.0f, 1.0f};
                const float foot_ry = rx * 0.34f;
                fx.foot = foot_ry / std::max(1.0f, ry);
                const float struck = who->ShieldStruck();
                if (struck < 0.6f) {
                    const float h = static_cast<float>(static_cast<int>((now - struck) * 7.0f) % 13);
                    fx.hit_x = sinf(h * 1.7f) * 0.7f;
                    fx.hit_y = -0.25f - 0.5f * fabsf(cosf(h * 2.3f));
                    fx.hit_age = struck / 0.6f;
                }
                const SDL_FRect dome = {foot.x - rx, foot.y - ry, rx * 2.0f, ry + foot_ry};
                if (Shaders::DrawShape(r, dome, fx, SDL_BLENDMODE_BLEND)) continue;
            }
            const int rows = std::max(6, static_cast<int>(ry));
            for (int i = 0; i < rows; ++i) {
                // 0 at the crown of the dome, 1 at the ground.
                const float t = (i + 0.5f) / rows;
                const float up = 1.0f - t;
                const float half = rx * sqrtf(std::max(0.0f, 1.0f - up * up));
                const float yy = foot.y - ry * up;
                SDL_SetRenderDrawColor(r, 196, 160, 255, static_cast<Uint8>(46 * fade));
                const SDL_FRect band = {foot.x - half, yy, half * 2.0f, ry / rows + 1.0f};
                SDL_RenderFillRect(r, &band);
                // The skin of it, brighter at the edge where it is seen edge-on.
                SDL_SetRenderDrawColor(r, 226, 206, 255, static_cast<Uint8>(150 * fade));
                const SDL_FRect lip_l = {foot.x - half, yy, z, ry / rows + 1.0f};
                const SDL_FRect lip_r = {foot.x + half - z, yy, z, ry / rows + 1.0f};
                SDL_RenderFillRect(r, &lip_l);
                SDL_RenderFillRect(r, &lip_r);
            }
            // Where it meets the ground, so it is a dome and not an arch.
            fill_disc(foot.x, foot.y, rx, rx * 0.34f, {196, 160, 255, static_cast<Uint8>(52 * fade)});
        }
    }

    // Embers, drops and the rest, over everything that stands: see Mote.
    DrawMotes(r);

    // The player's swing, over everything at ground level: it is the one
    // thing on screen that says where a blow is landing.
    DrawSwing(r);
    // What the combos leave, over it.
    DrawStrikes(r);
    DrawArrowRain(r);

    // Impact marks last a fifth of a second and are drawn over everything at
    // ground level, because the point of them is to be noticed: without one, a
    // bolt that hits a wall simply stops existing and it is not obvious whether
    // it was blocked or ran out of range.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Impact& im : impacts) {
        const float t = std::clamp(im.life / std::max(0.01f, im.max_life), 0.0f, 1.0f);
        const SDL_FPoint centre = camera.ToScreen(im.x, im.y - LiftAt(im.x, im.y));

        // A flash that opens outwards as it fades.
        const float rad = im.radius * camera.zoom * (1.0f + (1.0f - t) * 1.4f);
        fill_disc(centre.x, centre.y, rad, rad * 0.75f,
                  {im.color.r, im.color.g, im.color.b,
                   static_cast<Uint8>(190 * t)});

        // Three shards thrown back off the face it struck. Fixed rather than
        // random: a spray that reshuffles every frame reads as noise.
        if (im.nx != 0.0f || im.ny != 0.0f) {
            const float px = -im.ny, py = im.nx;      // along the surface
            const float reach = im.radius * (2.0f + (1.0f - t) * 3.0f) * camera.zoom;
            SDL_SetRenderDrawColor(r, im.color.r, im.color.g, im.color.b,
                                   static_cast<Uint8>(220 * t));
            for (float spread : {-0.6f, 0.0f, 0.6f}) {
                const float dx = im.nx + px * spread;
                const float dy = im.ny + py * spread;
                SDL_RenderLine(r, centre.x, centre.y,
                               centre.x + dx * reach, centre.y + dy * reach);
            }
        }
    }

    map.RenderLayer(r, cache, camera, LAYER_OVERHEAD);

    // Night, dusk, and the dream's violet, multiplied over everything above,
    // with fires and the player's own glow cut out of it.
    // Lava is its own light: the night leaves it, and what is on it, lit.
    vector<SDL_FRect> lit;
    if (Shaders::Effects()) map.SurfaceRects(camera.VisibleWorldRect(32.0f), Shaders::LAVA, lit);
    lighting.Render(r, camera, AmbientLight(), CollectLights(), &lit);
    // And what is lit from inside, shining through it: see world_screen.cpp.
    DrawGlows(r, cache, decor);

    // Leaves, fireflies and dust, and the vignette -- over the world, under
    // the bars and the HUD.
    ambience.Render(r, camera);

    // Health bars over anything the player has attacked. Last, above canopy
    // and roofs, because a bar hidden behind a tree is no use.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const auto& e : enemies) {
        if (!e->HealthBarVisible()) continue;
        const SDL_FRect body = e->BodyBox();
        if (!RectsOverlap(body, view)) continue;

        // Sized to the creature and hung just over its body box -- a hare's
        // bar sits low and narrow, an orc's high and wide -- and lifted with
        // the ground it stands on, like the sprite.
        const float w = std::max(20.0f, body.w + 4.0f);
        const SDL_FRect s = camera.ToScreenRect({e->x - w / 2.0f, body.y - e->draw_lift - 5.0f,
                                                 w, 3.0f});

        // Whole screen pixels, so the proportions drawn are the true ones and
        // not whatever sub-pixel scaling makes of them.
        const float bx = roundf(s.x), by = roundf(s.y);
        const int   bw = std::max(8, static_cast<int>(roundf(s.w)));
        const int   bh = std::max(5, static_cast<int>(roundf(s.h)));
        const int   inner = bw - 2;

        const int fill  = HealthBarFillPixels(e->hp, e->max_hp, inner);
        const int trail = std::clamp(static_cast<int>(std::lround(inner * e->HealthTrail())),
                                     fill, inner);

        SDL_SetRenderDrawColor(r, 14, 10, 8, 230);
        const SDL_FRect back = {bx, by, static_cast<float>(bw), static_cast<float>(bh)};
        SDL_RenderFillRect(r, &back);

        if (trail > fill) {
            SDL_SetRenderDrawColor(r, 240, 214, 160, 240);
            const SDL_FRect band = {bx + 1.0f + fill, by + 1.0f,
                                    static_cast<float>(trail - fill), bh - 2.0f};
            SDL_RenderFillRect(r, &band);
        }
        if (fill > 0) {
            SDL_SetRenderDrawColor(r, 196, 44, 40, 255);
            const SDL_FRect red = {bx + 1.0f, by + 1.0f, static_cast<float>(fill), bh - 2.0f};
            SDL_RenderFillRect(r, &red);
            // A lighter top row so it reads as a bar rather than a smear.
            SDL_SetRenderDrawColor(r, 236, 96, 84, 255);
            const SDL_FRect shine = {bx + 1.0f, by + 1.0f, static_cast<float>(fill), 1.0f};
            SDL_RenderFillRect(r, &shine);
        }

        // What is on it, as pips in a row over the bar: each status's own
        // colour in a dark frame, in the order they are declared.
        if (e->statuses.Any()) {
            static const SDL_Color kPip[STATUS_COUNT] = {
                {255, 150, 60, 255}, {110, 180, 240, 255}, {236, 214, 120, 255}, {200, 60, 70, 255},
                {140, 210, 90, 255}, {170, 220, 250, 255}, {232, 246, 255, 255}, {252, 236, 120, 255},
                {255, 140, 200, 255}, {196, 160, 255, 255}};
            const float side = static_cast<float>(std::max(6, bh + 1));
            float px = bx;
            for (int i = 0; i < STATUS_COUNT; ++i) {
                if (e->statuses.left[i] <= 0.0f) continue;
                const SDL_FRect frame = {px, by - side - 2.0f, side, side};
                SDL_SetRenderDrawColor(r, 14, 10, 8, 230);
                SDL_RenderFillRect(r, &frame);
                const SDL_FRect pip = {px + 1.0f, by - side - 1.0f, side - 2.0f, side - 2.0f};
                SDL_SetRenderDrawColor(r, kPip[i].r, kPip[i].g, kPip[i].b, 255);
                SDL_RenderFillRect(r, &pip);
                px += side + 1.0f;
            }
        }
    }

    // Heavy attacks winding up: a bar over the head that fills as the charge
    // does, amber to red, and flashes when it is about to land. Over the
    // health bar when there is one, so both can be read at once.
    for (const auto& e : enemies) {
        const float charge = e->HeavyCharge();
        if (charge <= 0.0f) continue;
        const SDL_FRect body = e->BodyBox();
        if (!RectsOverlap(body, view)) continue;
        // Wider and thicker than the health bar, with a pale frame, and
        // yellow to red as it fills: it has to read as a different thing from
        // the red health bar right under it.
        const float w = std::max(28.0f, body.w + 12.0f);
        const float lift = e->HealthBarVisible() ? 12.0f : 6.0f;
        const SDL_FRect s = camera.ToScreenRect({e->x - w / 2.0f, body.y - e->draw_lift - lift, w, 5.0f});
        const float bx = roundf(s.x), by = roundf(s.y);
        const int   bw = std::max(14, static_cast<int>(roundf(s.w)));
        const int   bh = std::max(9, static_cast<int>(roundf(s.h)));
        const int   inner = bw - 4;
        const int   fill = std::clamp(static_cast<int>(std::lround(inner * charge)), 1, inner);

        // Near full the frame flashes: it is about to land.
        const bool flash = charge > 0.8f && (SDL_GetTicks() / 80) % 2 == 0;
        SDL_SetRenderDrawColor(r, flash ? 255 : 244, flash ? 70 : 226, flash ? 50 : 190, 255);
        const SDL_FRect frame = {bx, by, static_cast<float>(bw), static_cast<float>(bh)};
        SDL_RenderFillRect(r, &frame);
        SDL_SetRenderDrawColor(r, 24, 8, 6, 255);
        const SDL_FRect back = {bx + 1.0f, by + 1.0f, bw - 2.0f, bh - 2.0f};
        SDL_RenderFillRect(r, &back);
        const Uint8 g = static_cast<Uint8>(210.0f * (1.0f - charge) + 30.0f);
        SDL_SetRenderDrawColor(r, 255, g, 36, 255);
        const SDL_FRect bar = {bx + 2.0f, by + 2.0f, static_cast<float>(fill), bh - 4.0f};
        SDL_RenderFillRect(r, &bar);
    }

    // The target marker: a small arrow hung over the head of whoever shots are
    // going to, above the health bar. Pale for the monster the fight picked,
    // red and bobbing for a lock. In art pixels, so it sits with the sprites.
    if (const Enemy* t = targeting.Current()) {
        const bool lock = targeting.IsLocked();
        const float z = camera.zoom;
        const SDL_FRect body = t->BodyBox();
        const float bob = lock ? roundf(sinf(static_cast<float>(SDL_GetTicks()) * 0.009f) * 1.5f) : 0.0f;
        // Over the health bar, and over a charging heavy's bar above that.
        const float gap = (t->HealthBarVisible() ? 9.0f : 4.0f) + (t->ChargingHeavy() ? 9.0f : 0.0f);
        const SDL_FPoint tip = camera.ToScreen(t->x, body.y - t->draw_lift - gap - bob);
        const float cx = roundf(tip.x / z) * z, by = roundf(tip.y / z) * z;
        const SDL_Color fill = lock ? SDL_Color{236, 72, 54, 255} : SDL_Color{246, 226, 160, 235};
        auto row = [&](float dy, int w, SDL_Color c) {
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            const SDL_FRect span = {cx - (w / 2) * z, by + dy * z, w * z, z};
            SDL_RenderFillRect(r, &span);
        };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color ink{20, 12, 10, 230};
        const int outline[] = {9, 9, 7, 5, 3, 1};
        for (int i = 0; i < 6; ++i) row(-5.0f + i, outline[i], ink);
        const int core[] = {7, 5, 3, 1};
        for (int i = 0; i < 4; ++i) row(-4.0f + i, core[i], fill);
    }
}

void World::DrawIce(SDL_Renderer* r) const {
    if (ice_cracks.empty() && ice_holes.empty()) return;
    const float z = camera.zoom;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // A line of whole pixels, so a crack is drawn as the ground is.
    const auto line = [&](SDL_FPoint a, SDL_FPoint b, SDL_Color c) {
        const SDL_FPoint s0 = camera.ToScreen(a.x, a.y), s1 = camera.ToScreen(b.x, b.y);
        const float len = Length(s1.x - s0.x, s1.y - s0.y);
        const int n = std::max(1, static_cast<int>(len / z));
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / n;
            const SDL_FRect px = {floorf((s0.x + (s1.x - s0.x) * t) / z) * z, floorf((s0.y + (s1.y - s0.y) * t) / z) * z, z, z};
            SDL_RenderFillRect(r, &px);
        }
    };
    for (const IceHole& h : ice_holes) {
        const float fade = std::clamp((ICE_HOLE_LIFE - h.age) / 6.0f, 0.0f, 1.0f);
        const SDL_FPoint c = camera.ToScreen(h.at.x, h.at.y);
        const float rx = 22.0f * z, ry = 13.0f * z;
        for (int ring = 0; ring < 2; ++ring) {
            const float k = ring ? 0.78f : 1.0f;
            const SDL_Color col = ring ? SDL_Color{16, 32, 52, static_cast<Uint8>(230 * fade)}
                                       : SDL_Color{226, 240, 255, static_cast<Uint8>(200 * fade)};
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
            const int rows = std::max(3, static_cast<int>(ry * k * 2.0f / z));
            for (int i = 0; i < rows; ++i) {
                const float t = (i + 0.5f) / rows * 2.0f - 1.0f;
                const float half = rx * k * sqrtf(std::max(0.0f, 1.0f - t * t));
                const SDL_FRect span = {floorf((c.x - half) / z) * z, floorf((c.y + t * ry * k) / z) * z,
                                        floorf(half * 2.0f / z) * z, z};
                SDL_RenderFillRect(r, &span);
            }
        }
    }
    for (const IceCrack& c : ice_cracks) {
        const float fade = std::clamp((ICE_CRACK_LIFE - c.age) / 8.0f, 0.0f, 1.0f);
        // A pale lip along it, and the dark of the water showing in the crack.
        line({c.a.x, c.a.y - 1.0f}, {c.b.x, c.b.y - 1.0f}, {236, 246, 255, static_cast<Uint8>(170 * fade)});
        line(c.a, c.b, {38, 70, 104, static_cast<Uint8>(220 * fade)});
    }
}
