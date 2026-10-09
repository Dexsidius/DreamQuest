// The small animals about the world: see Ambience (the "critters"). Each
// keeps to what its kind does -- a rabbit nibbles at the grass and bolts for
// a bush, a squirrel runs up its tree, a hen pecks about its yard -- and all
// of them keep away from whoever is walking. They are seen and not touched:
// no blow lands on one and none of them is told to a friend's machine.
#include "ambience.h"
#include "../systems/shaders.h"

namespace {
float Rand01(std::mt19937& rng) { return (rng() % 10000u) / 10000.0f; }
float Range(std::mt19937& rng, float lo, float hi) { return lo + (hi - lo) * Rand01(rng); }
float Dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

// The nearest walker, and how far: a long way off if there is nobody.
float Nearest(const vector<Ambience::Walker>& walkers, float x, float y, float& wx, float& wy, bool& sprinting) {
    float best = 1e9f;
    wx = x; wy = y + 1.0f; sprinting = false;
    for (const Ambience::Walker& w : walkers) {
        const float d = Dist(w.x, w.y, x, y);
        if (d < best) { best = d; wx = w.x; wy = w.y; sprinting = w.sprinting; }
    }
    return best;
}

bool InView(const SDL_FRect& v, float x, float y, float margin) {
    return x > v.x - margin && x < v.x + v.w + margin && y > v.y - margin && y < v.y + v.h + margin;
}
}   // namespace

bool Ambience::Stands(float x, float y, const World& world) const {
    return world.stand && world.stand(x, y);
}

const Ambience::Spot* Ambience::NearestCover(float x, float y, float reach, bool trees_only) const {
    const Spot* best = nullptr;
    float d = reach;
    const auto look = [&](const vector<Spot>& spots) {
        for (const Spot& s : spots) {
            const float e = Dist(s.x, s.y, x, y);
            if (e < d) { d = e; best = &s; }
        }
    };
    look(place.trees);
    if (!trees_only) look(place.bushes);
    return best;
}

void Ambience::UpdateCritters(float dt, const SDL_FRect& view, const World& world) {
    const bool arriving = critter_wait < 0.0f;
    if (!lively) {
        critters.clear();
        return;
    }
    const float cx = view.x + view.w * 0.5f, cy = view.y + view.h * 0.5f;
    const bool outdoors = OutdoorsKind();
    const bool day = daylight > 0.45f;
    const bool dawn = hour >= 4.8f && hour < 8.5f, dusk_hours = hour >= 17.5f && hour < 21.0f;
    const bool night = daylight < 0.3f;
    const bool green = kind == Kind::Field || kind == Kind::Grove || kind == Kind::Forest;

    // --- who comes ---------------------------------------------------------------------
    const auto count = [&](int k) {
        int n = 0;
        for (const Critter& c : critters) n += c.kind == k && c.state != Critter::HIDING;
        return n;
    };
    // A spot on open ground in view and out of anybody's way, within `reach` of
    // (x, y) (or anywhere in view when reach is 0).
    const auto find_spot = [&](float x, float y, float reach, float keep_off, float& ox, float& oy) {
        for (int attempt = 0; attempt < 14; ++attempt) {
            const float px = reach > 0.0f ? x + Range(rng, -reach, reach) : Range(rng, view.x + 24.0f, view.x + view.w - 24.0f);
            const float py = reach > 0.0f ? y + Range(rng, -reach * 0.6f, reach * 0.6f)
                                          : Range(rng, view.y + 24.0f, view.y + view.h - 24.0f);
            float wx, wy;
            bool sp;
            if (Nearest(world.walkers, px, py, wx, wy, sp) < keep_off) continue;
            if (!Stands(px, py, world)) continue;
            ox = px; oy = py;
            return true;
        }
        return false;
    };
    const auto add = [&](int k, float x, float y) -> Critter& {
        Critter c;
        c.kind = k;
        c.x = c.home_x = x;
        c.y = c.home_y = y;
        c.left = Rand01(rng) < 0.5f;
        c.until = Range(rng, 0.6f, 2.5f);
        c.anim = Range(rng, 0.0f, 6.0f);
        c.fade = arriving ? 1.0f : 0.0f;     // fades in, unless it was there all along
        critters.push_back(c);
        return critters.back();
    };

    if ((critter_wait -= dt) <= 0.0f) {
        critter_wait = Range(rng, 1.2f, 2.6f);
        const int tries = arriving ? 4 : 1;
        for (int i = 0; i < tries; ++i) {
            float x = 0.0f, y = 0.0f;
            // Rabbits on the grass, by day and at either end of it.
            if (green && (day || dawn || dusk_hours) && rain < 0.5f && count(RABBIT) < (kind == Kind::Field ? 3 : 2)) {
                const Spot* cover = nullptr;
                for (int a = 0; a < 6 && !cover; ++a) {
                    if (!find_spot(0, 0, 0, 150.0f, x, y)) break;
                    if (FootingAt(x, y) != FOOT_GRASS && FootingAt(x, y) != FOOT_NONE) continue;
                    cover = NearestCover(x, y, 220.0f, false);
                }
                if (cover) add(RABBIT, x, y);
            }
            // Squirrels at the foot of a tree in the woods.
            if ((kind == Kind::Forest || kind == Kind::Grove) && day && rain < 0.4f && count(SQUIRREL) < 2 &&
                !place.trees.empty()) {
                const Spot& t = place.trees[rng() % place.trees.size()];
                if (InView(view, t.x, t.y, -20.0f) && find_spot(t.x, t.y + 8.0f, 26.0f, 140.0f, x, y)) add(SQUIRREL, x, y);
            }
            // Deer at the edge of the woods at dawn and at dusk, a few together,
            // well away from anybody.
            if (green && (dawn || dusk_hours) && count(DEER) == 0 && !place.trees.empty() &&
                Rand01(rng) < (arriving ? 0.8f : 0.08f)) {
                const Spot& t = place.trees[rng() % place.trees.size()];
                if (InView(view, t.x, t.y, 60.0f) && !TwinNear(DEER, t.x, t.y) &&
                    find_spot(t.x, t.y + 24.0f, 70.0f, 240.0f, x, y)) {
                    const int herd = 1 + static_cast<int>(rng() % 3u);
                    for (int k = 0; k < herd; ++k) {
                        float hx = x, hy = y;
                        if (k == 0 || find_spot(x, y, 36.0f, 220.0f, hx, hy)) add(DEER, hx, hy).state = Critter::BUSY;
                    }
                }
            }
            // Lizards basking on the burnt land, beside its rocks.
            if (kind == Kind::Ash && day && count(LIZARD) < 3) {
                bool ok = false;
                if (!place.rocks.empty() && Rand01(rng) < 0.7f) {
                    const Spot& rock = place.rocks[rng() % place.rocks.size()];
                    ok = InView(view, rock.x, rock.y, -10.0f) && find_spot(rock.x, rock.y + 6.0f, 22.0f, 120.0f, x, y);
                }
                if (!ok) ok = find_spot(0, 0, 0, 140.0f, x, y);
                if (ok) add(LIZARD, x, y);
            }
            // Rats along the walls under the ground.
            if (kind == Kind::Dungeon && count(RAT) < 2) {
                for (int a = 0; a < 10; ++a) {
                    if (!find_spot(0, 0, 0, 130.0f, x, y)) break;
                    if (TwinNear(RAT, x, y)) continue;
                    // Against a wall: solid a step above or below, so it runs along it.
                    const bool above = !Stands(x, y - 14.0f, world), below = !Stands(x, y + 14.0f, world);
                    if (!above && !below) continue;
                    Critter& c = add(RAT, x, y);
                    c.tx = above ? 1.0f : -1.0f;     // which side the wall is: kept to it
                    break;
                }
            }
            // Bats at dusk and after over the graves, and down in the crypt.
            if (count(BAT) < 3 && (place.crypt || ((dusk_hours || night) && outdoors))) {
                float ax = 0.0f, ay = 0.0f;
                bool ok = false;
                if (place.crypt) { ok = find_spot(0, 0, 0, 100.0f, ax, ay); }
                else if (!place.graves.empty()) {
                    const Spot& g = place.graves[rng() % place.graves.size()];
                    ok = InView(view, g.x, g.y, 120.0f);
                    ax = g.x; ay = g.y;
                }
                if (ok && !TwinNear(BAT, ax, ay)) {
                    Critter& c = add(BAT, ax + Range(rng, -40.0f, 40.0f), ay + Range(rng, -30.0f, 30.0f));
                    c.home_x = ax; c.home_y = ay;
                    c.h = Range(rng, 34.0f, 60.0f);
                }
            }
        }

        // The hens about their yards and the cats on their steps: each nest
        // keeps its own, by day; the hens go in at night.
        // (Not in a dream of the place: the dream has its own life.)
        for (size_t n = 0; outdoors && n < place.nests.size(); ++n) {
            const Nest& nest = place.nests[n];
            if (nest.kind == HEN && (TwinNear(HEN, nest.x, nest.y) || night)) continue;
            if (!InView(view, nest.x, nest.y, 200.0f)) continue;
            int have = 0;
            for (const Critter& c : critters) have += c.nest == static_cast<int>(n);
            if (have >= nest.count) continue;
            float wx, wy;
            bool sp;
            if (Nearest(world.walkers, nest.x, nest.y, wx, wy, sp) < (nest.kind == CAT ? 220.0f : 90.0f)) continue;
            float x = nest.x, y = nest.y;
            if (nest.kind == HEN && !find_spot(nest.x, nest.y, nest.radius, 60.0f, x, y)) continue;
            Critter& c = add(nest.kind, x, y);
            c.nest = static_cast<int>(n);
            c.home_x = nest.x; c.home_y = nest.y; c.radius = nest.radius;
            // Each its own colouring, the same every time it is seen.
            const uint32_t h = static_cast<uint32_t>(n * 7 + have * 13 + 5);
            if (nest.kind == HEN) {
                static const SDL_Color kHens[] = {{255, 255, 255, 255}, {214, 150, 96, 255}, {176, 112, 70, 255},
                                                  {236, 214, 170, 255}};
                c.tint = kHens[h % 4];
            } else {
                static const SDL_Color kCats[] = {{255, 176, 106, 255}, {70, 68, 74, 255}, {176, 176, 180, 255},
                                                  {236, 222, 196, 255}};
                c.tint = kCats[h % 4];
                c.state = night ? Critter::IDLE : Critter::BUSY;    // asleep by day
            }
        }

        // Frogs on the lily pads, while nobody is close.
        if (outdoors && !place.lilies.empty() && count(FROG) < 4)
            for (size_t p = 0; p < place.lilies.size(); ++p) {
                const Spot& pad = place.lilies[p];
                if (!InView(view, pad.x, pad.y, 30.0f) || TwinNear(FROG, pad.x, pad.y)) continue;
                bool taken = false;
                for (const Critter& c : critters) taken |= c.kind == FROG && c.nest == static_cast<int>(1000 + p);
                if (taken || Rand01(rng) > (arriving ? 0.6f : 0.35f)) continue;
                float wx, wy;
                bool sp;
                if (Nearest(world.walkers, pad.x, pad.y, wx, wy, sp) < 140.0f) continue;
                Critter& c = add(FROG, pad.x + Range(rng, -pad.w * 0.25f, pad.w * 0.25f), pad.y + Range(rng, -2.0f, 2.0f));
                c.nest = static_cast<int>(1000 + p);
            }
    }

    // A fish now and then, out of the water and back in.
    if (!place.water.empty() && outdoors && (fish_wait -= dt) <= 0.0f) {
        fish_wait = Range(rng, 3.0f, 9.0f);
        for (int attempt = 0; attempt < 8; ++attempt) {
            const SDL_FPoint& w = place.water[rng() % place.water.size()];
            if (!InView(view, w.x, w.y, -30.0f)) continue;
            float wx, wy;
            bool sp;
            if (Nearest(world.walkers, w.x, w.y, wx, wy, sp) < 70.0f) continue;
            Critter& c = add(FISH, w.x + Range(rng, -12.0f, 12.0f), w.y + Range(rng, -10.0f, 10.0f));
            c.fade = 1.0f;
            c.vx = (Rand01(rng) < 0.5f ? -1.0f : 1.0f) * Range(rng, 20.0f, 30.0f);
            c.left = c.vx < 0.0f;
            c.until = Range(rng, 0.6f, 0.8f);
            c.state = Critter::BUSY;
            splashes.push_back({c.x, c.y, 0.0f, 0.7f, 5.0f, true});
            break;
        }
    }

    // The figure in the Reverie: somewhere out at the edge of what can be seen,
    // standing, looking back -- and not there when you get close.
    if (place.reverie && !world.walkers.empty() && count(WATCHER) == 0 && (watcher_wait -= dt) <= 0.0f) {
        watcher_wait = Range(rng, 35.0f, 80.0f);
        const Walker& w = world.walkers.front();
        for (int attempt = 0; attempt < 20; ++attempt) {
            const float a = Range(rng, 0.0f, 6.2831853f), d = Range(rng, 230.0f, 300.0f);
            const float x = w.x + cosf(a) * d, y = w.y + sinf(a) * d * 0.7f;
            if (!InView(view, x, y, -24.0f) || !Stands(x, y, world)) continue;
            Critter& c = add(WATCHER, x, y);
            c.until = Range(rng, 12.0f, 22.0f);
            c.left = w.x < x;
            break;
        }
    }

    // --- what each does ----------------------------------------------------------------
    for (Critter& c : critters) {
        c.t += dt;
        c.anim += dt;
        float wx, wy;
        bool sprint;
        const float near = Nearest(world.walkers, c.x, c.y, wx, wy, sprint);
        const auto away = [&](float speed, float& ux, float& uy) {
            float ax = c.x - wx, ay = c.y - wy;
            const float len = std::max(1.0f, std::sqrt(ax * ax + ay * ay));
            ux = ax / len * speed;
            uy = ay / len * speed;
        };
        // Coming into sight, and going out of it.
        if (c.state == Critter::HIDING) c.fade = std::max(0.0f, c.fade - dt * 4.0f);
        else if (c.state == Critter::LEAVING) c.fade = std::max(0.0f, c.fade - dt * 0.8f);
        else c.fade = std::min(1.0f, c.fade + dt * 1.5f);

        switch (c.kind) {
            case RABBIT:
            case SQUIRREL:
            case LIZARD: {
                const float alert_at = c.kind == RABBIT ? 130.0f : c.kind == SQUIRREL ? 110.0f : 90.0f;
                const float bolt_at = c.kind == RABBIT ? 95.0f : c.kind == SQUIRREL ? 80.0f : 70.0f;
                if (c.state != Critter::FLEE && c.state != Critter::HIDING &&
                    (near < bolt_at || (sprint && near < bolt_at * 1.8f) || (c.kind != LIZARD && rain > 0.6f))) {
                    // Off, for cover: a rabbit to the nearest bush or tree, a
                    // squirrel to its tree, a lizard to its rock -- whichever is
                    // not past whoever frightened it.
                    c.state = Critter::FLEE;
                    c.t = 0.0f;
                    c.until = 0.0f;
                    const Spot* cover = nullptr;
                    if (c.kind == LIZARD) {
                        float best = 160.0f;
                        for (const Spot& s : place.rocks) {
                            const float d = Dist(s.x, s.y, c.x, c.y);
                            if (d < best && Dist(s.x, s.y, wx, wy) > d) { best = d; cover = &s; }
                        }
                    } else {
                        cover = NearestCover(c.x, c.y, 240.0f, c.kind == SQUIRREL);
                        if (cover && Dist(cover->x, cover->y, wx, wy) < Dist(cover->x, cover->y, c.x, c.y)) cover = nullptr;
                    }
                    if (cover) { c.tx = cover->x; c.ty = cover->y + 2.0f; }
                    else { float ux, uy; away(1.0f, ux, uy); c.tx = c.x + ux * 220.0f; c.ty = c.y + uy * 220.0f; }
                    break;
                }
                if (c.state == Critter::FLEE) {
                    // (A hop is the same legs, unhurried, and lands where it was going.)
                    const bool hop = c.until < 0.0f;
                    const float speed = hop ? 40.0f : c.kind == RABBIT ? 150.0f : c.kind == SQUIRREL ? 130.0f : 170.0f;
                    const float ax = c.tx - c.x, ay = c.ty - c.y, d = std::sqrt(ax * ax + ay * ay);
                    if (hop && (d <= speed * dt || c.t > 1.0f)) {
                        c.x = c.tx; c.y = c.ty;
                        c.state = Critter::IDLE; c.t = 0.0f; c.until = Range(rng, 0.6f, 2.0f);
                        break;
                    }
                    if (d < 6.0f || c.t > 2.4f) {
                        // In under the bush, up the tree, under the rock.
                        c.state = Critter::HIDING;
                        c.t = 0.0f;
                        if (c.kind == SQUIRREL) c.vy = -60.0f;
                        break;
                    }
                    c.left = ax < 0.0f;
                    const float nx = c.x + ax / d * speed * dt, ny = c.y + ay / d * speed * dt;
                    if (!Stands(nx, ny, world) && d > 30.0f) { c.state = Critter::HIDING; c.t = 0.0f; break; }
                    c.x = nx; c.y = ny;
                    break;
                }
                if (c.state == Critter::HIDING) {
                    if (c.kind == SQUIRREL) c.h += 60.0f * dt;    // up the trunk
                    break;
                }
                if (near < alert_at) { c.state = Critter::ALERT; c.left = wx < c.x; break; }
                // At its ease: sitting, nibbling, a hop or a scurry now and then.
                if (c.t >= c.until) {
                    c.t = 0.0f;
                    const float roll = Rand01(rng);
                    if (c.state == Critter::ALERT || roll < 0.35f) { c.state = Critter::IDLE; c.until = Range(rng, 0.8f, 2.6f); }
                    else if (roll < 0.75f) { c.state = Critter::BUSY; c.until = Range(rng, 1.0f, 3.0f); }
                    else {
                        // A short hop or run, and settle again.
                        const float hx = c.x + (Rand01(rng) < 0.5f ? -1.0f : 1.0f) * Range(rng, 8.0f, 18.0f);
                        const float hy = c.y + Range(rng, -6.0f, 6.0f);
                        if (Stands(hx, hy, world) && Dist(hx, hy, c.home_x, c.home_y) < 60.0f) {
                            c.left = hx < c.x;
                            c.tx = hx; c.ty = hy;
                            c.state = Critter::FLEE;    // the same legs, unhurried: see FLEE
                            c.until = -1.0f;            // marks it as only a hop
                        }
                    }
                }
                break;
            }
            case DEER: {
                if (c.state == Critter::FLEE) {
                    c.x += c.vx * dt;
                    c.y += c.vy * dt;
                    if (c.t > 1.6f) c.state = Critter::LEAVING;
                    break;
                }
                if (c.state == Critter::LEAVING) { c.x += c.vx * dt; c.y += c.vy * dt; break; }
                if (near < 170.0f || (sprint && near < 260.0f)) {
                    // Bound away, the whole herd with the first.
                    float ux, uy;
                    away(175.0f, ux, uy);
                    for (Critter& o : critters)
                        if (o.kind == DEER && o.state != Critter::FLEE && o.state != Critter::LEAVING &&
                            Dist(o.x, o.y, c.x, c.y) < 90.0f) {
                            o.state = Critter::FLEE; o.t = Range(rng, -0.25f, 0.0f);
                            o.vx = ux * Range(rng, 0.9f, 1.1f); o.vy = uy * Range(rng, 0.9f, 1.1f);
                            o.left = ux < 0.0f;
                        }
                    break;
                }
                if (near < 240.0f) { c.state = Critter::ALERT; c.left = wx < c.x; break; }
                if (c.t >= c.until) {
                    // Grazing, and lifting its head to look round between.
                    c.t = 0.0f;
                    c.state = c.state == Critter::BUSY ? Critter::IDLE : Critter::BUSY;
                    c.until = c.state == Critter::BUSY ? Range(rng, 2.0f, 5.0f) : Range(rng, 1.0f, 2.4f);
                    if (Rand01(rng) < 0.2f) c.left = !c.left;
                }
                // Gone with the morning, or the evening.
                if (!(dawn || dusk_hours) && near > 260.0f) c.state = Critter::LEAVING;
                break;
            }
            case HEN: {
                if (night) { c.state = Critter::LEAVING; break; }
                if (c.state == Critter::FLEE) {
                    c.x += c.vx * dt; c.y += c.vy * dt;
                    if (!Stands(c.x, c.y, world)) { c.x -= c.vx * dt; c.y -= c.vy * dt; c.vx = -c.vx; }
                    if (c.t > 0.7f) { c.state = Critter::IDLE; c.t = 0.0f; c.until = Range(rng, 0.6f, 1.6f); }
                    break;
                }
                if (near < 55.0f || (sprint && near < 90.0f)) {
                    c.state = Critter::FLEE; c.t = 0.0f;
                    away(95.0f, c.vx, c.vy);
                    c.left = c.vx < 0.0f;
                    break;
                }
                if (c.state == Critter::ALERT) {    // walking somewhere
                    const float ax = c.tx - c.x, ay = c.ty - c.y, d = std::sqrt(ax * ax + ay * ay);
                    if (d < 2.0f || c.t > 2.0f) { c.state = Critter::IDLE; c.t = 0.0f; c.until = Range(rng, 0.4f, 1.4f); break; }
                    c.x += ax / d * 22.0f * dt; c.y += ay / d * 22.0f * dt;
                    break;
                }
                if (c.t >= c.until) {
                    c.t = 0.0f;
                    const float roll = Rand01(rng);
                    if (roll < 0.45f) { c.state = Critter::BUSY; c.until = Range(rng, 0.3f, 0.6f); }   // a peck
                    else if (roll < 0.75f) { c.state = Critter::IDLE; c.until = Range(rng, 0.5f, 1.5f); }
                    else {
                        // A few steps, back towards home if it has strayed.
                        float tx = c.x + Range(rng, -24.0f, 24.0f), ty = c.y + Range(rng, -14.0f, 14.0f);
                        if (Dist(tx, ty, c.home_x, c.home_y) > c.radius) { tx = c.home_x + Range(rng, -10.0f, 10.0f); ty = c.home_y; }
                        if (Stands(tx, ty, world)) { c.state = Critter::ALERT; c.tx = tx; c.ty = ty; c.left = tx < c.x; }
                    }
                }
                break;
            }
            case CAT: {
                if (c.state == Critter::LEAVING) {
                    c.x += (c.left ? -1.0f : 1.0f) * 26.0f * dt;
                    break;
                }
                if (near < 34.0f || (sprint && near < 70.0f)) {
                    // Too close: up, a stretch, and off along the street.
                    c.state = Critter::LEAVING; c.t = 0.0f;
                    c.left = wx > c.x;
                    break;
                }
                // Asleep in the sun by day, sitting up and watching by night --
                // and awake and watching whoever comes near.
                c.state = (night || near < 70.0f) ? Critter::IDLE : Critter::BUSY;
                if (c.state == Critter::IDLE && near < 200.0f) c.left = wx < c.x;
                break;
            }
            case FROG: {
                if (c.state == Critter::FLEE) {
                    // A leap off the pad and into the water: an arc, then a plop.
                    c.x += c.vx * dt;
                    c.h = std::max(0.0f, 9.0f * sinf(std::min(1.0f, c.t / 0.45f) * 3.14159265f));
                    if (c.t >= 0.45f) {
                        splashes.push_back({c.x, c.y, 0.0f, 0.8f, 6.0f, true});
                        c.state = Critter::HIDING; c.fade = 0.0f;
                    }
                    break;
                }
                if (near < 75.0f || (sprint && near < 120.0f)) {
                    c.state = Critter::FLEE; c.t = 0.0f;
                    c.left = wx > c.x;
                    c.vx = (c.left ? -1.0f : 1.0f) * 40.0f;
                    break;
                }
                // Sitting; croaking now and then -- more of an evening.
                if (c.t >= c.until) {
                    c.t = 0.0f;
                    c.state = c.state == Critter::BUSY ? Critter::IDLE : (Rand01(rng) < (night || dusk_hours ? 0.6f : 0.25f)
                                                                              ? Critter::BUSY : Critter::IDLE);
                    c.until = c.state == Critter::BUSY ? Range(rng, 0.4f, 0.9f) : Range(rng, 1.5f, 4.0f);
                }
                break;
            }
            case FISH: {
                c.x += c.vx * dt;
                const float k = std::clamp(c.t / c.until, 0.0f, 1.0f);
                c.h = 14.0f * sinf(k * 3.14159265f);
                if (k >= 1.0f) {
                    splashes.push_back({c.x, c.y, 0.0f, 0.7f, 5.0f, true});
                    c.state = Critter::HIDING; c.fade = 0.0f;
                }
                break;
            }
            case BAT: {
                // Flitting: never in a straight line for long, round and round
                // whatever it is keeping to; gone by daylight.
                const float a = c.anim * 1.7f + c.home_x * 0.01f;
                const float tx = c.home_x + cosf(a) * 70.0f + sinf(c.anim * 3.1f) * 26.0f;
                const float ty = c.home_y + sinf(a * 1.3f) * 40.0f + cosf(c.anim * 2.3f) * 18.0f;
                c.left = tx < c.x;
                c.x += (tx - c.x) * std::min(1.0f, dt * 3.0f);
                c.y += (ty - c.y) * std::min(1.0f, dt * 3.0f);
                c.h = 40.0f + 14.0f * sinf(c.anim * 1.9f);
                if (!place.crypt && !(dusk_hours || night)) c.state = Critter::LEAVING;
                if (c.t > 40.0f) c.state = Critter::LEAVING;
                break;
            }
            case RAT: {
                // Along the wall: a scurry, a stop, a scurry.
                if (near < 90.0f && c.state != Critter::FLEE && c.state != Critter::LEAVING) {
                    c.state = Critter::FLEE; c.t = 0.0f;
                    c.left = wx > c.x;
                }
                if (c.state == Critter::FLEE) {
                    const float nx = c.x + (c.left ? -1.0f : 1.0f) * 150.0f * dt;
                    if (Stands(nx, c.y, world)) c.x = nx;
                    else c.left = !c.left;
                    if (c.t > 1.0f) c.state = Critter::LEAVING;
                    break;
                }
                if (c.state == Critter::LEAVING) break;
                if (c.state == Critter::ALERT) {
                    const float nx = c.x + (c.left ? -1.0f : 1.0f) * 60.0f * dt;
                    if (Stands(nx, c.y, world)) c.x = nx;
                    else { c.left = !c.left; c.state = Critter::IDLE; }
                }
                if (c.t >= c.until) {
                    c.t = 0.0f;
                    c.state = c.state == Critter::ALERT ? Critter::IDLE : Critter::ALERT;
                    c.until = c.state == Critter::ALERT ? Range(rng, 0.6f, 1.6f) : Range(rng, 0.5f, 2.0f);
                    if (Rand01(rng) < 0.3f) c.left = !c.left;
                }
                break;
            }
            case WATCHER: {
                if (near < 170.0f || c.t > c.until) {
                    if (c.state != Critter::HIDING) {
                        // Gone, the way the dream takes things: a breath of dark.
                        for (int k = 0; k < 6; ++k)
                            breath.push_back({c.x + Range(rng, -5.0f, 5.0f), c.y - Range(rng, 6.0f, 28.0f),
                                              Range(rng, -10.0f, 10.0f), Range(rng, -18.0f, -8.0f), 0.0f, 1.0f, true});
                    }
                    c.state = Critter::HIDING;
                }
                else c.left = wx < c.x;
                break;
            }
            default: break;
        }
    }
    // Gone: hidden away, faded out, or left far behind.
    critters.erase(std::remove_if(critters.begin(), critters.end(), [&](const Critter& c) {
                       if ((c.state == Critter::HIDING || c.state == Critter::LEAVING) && c.fade <= 0.0f) return true;
                       return std::fabs(c.x - cx) > view.w * 0.5f + 320.0f || std::fabs(c.y - cy) > view.h * 0.5f + 320.0f;
                   }),
                   critters.end());
    critter_wait = std::max(critter_wait, 0.0f);
}

void Ambience::CollectStanding(const SDL_FRect& view, vector<Standing>& out) const {
    for (size_t i = 0; i < critters.size(); ++i) {
        const Critter& c = critters[i];
        if (c.kind == BAT || c.kind == FISH || c.fade <= 0.01f) continue;
        if (!InView(view, c.x, c.y, 40.0f)) continue;
        out.push_back({c.y, static_cast<int>(i)});
    }
}

void Ambience::DrawStanding(SDL_Renderer* r, const Camera& cam, int index) const {
    if (index < 0 || index >= static_cast<int>(critters.size())) return;
    Sheet(r);
    DrawCritter(r, cam, critters[static_cast<size_t>(index)], false);
}

void Ambience::DrawCritter(SDL_Renderer* r, const Camera& cam, const Critter& c, bool shadow) const {
    if (c.fade <= 0.01f) return;
    const float z = cam.zoom;
    if (shadow) {
        if (c.kind == FISH || c.kind == WATCHER) return;
        // A soft dark oval under it, smaller the higher it is.
        const float wide = c.kind == DEER ? 18.0f : c.kind == BAT ? 7.0f : 9.0f;
        const float lift = c.kind == BAT ? c.h : 0.0f;
        const SDL_FPoint p = cam.ToScreen(c.x + lift * 0.12f, c.y + lift * 0.08f);
        const float w = roundf(wide * z * (1.0f - std::min(lift, 80.0f) / 200.0f)), h = std::max(1.0f, roundf(2.0f * z));
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 10, 14, 12, static_cast<Uint8>(60.0f * c.fade));
        const SDL_FRect mid = {roundf(p.x - w / 2.0f), roundf(p.y - h / 2.0f), w, h};
        SDL_RenderFillRect(r, &mid);
        return;
    }
    SDL_Texture* tex = critter_sheet;
    if (!tex) return;
    int frame = 0;
    const bool fleeing = c.state == Critter::FLEE;
    switch (c.kind) {
        case RABBIT:
            frame = fleeing ? 3 + static_cast<int>(c.anim * 11.0f) % 2 : c.state == Critter::BUSY ? 1 : c.state == Critter::ALERT ? 2 : 0;
            break;
        case SQUIRREL:
            frame = (fleeing || c.state == Critter::HIDING) ? 2 + static_cast<int>(c.anim * 12.0f) % 2
                                                            : c.state == Critter::BUSY ? 1 : 0;
            break;
        case HEN:
            frame = fleeing ? 3 : c.state == Critter::BUSY ? 1 : c.state == Critter::ALERT ? (static_cast<int>(c.anim * 6.0f) % 2 ? 2 : 0) : 0;
            break;
        case CAT:
            frame = c.state == Critter::LEAVING ? (c.t < 0.6f ? 4 : 2 + static_cast<int>(c.anim * 5.0f) % 2)
                                                : c.state == Critter::BUSY ? 0 : 1;
            break;
        case FROG:  frame = fleeing ? 2 : c.state == Critter::BUSY ? 1 : 0; break;
        case FISH:  frame = c.h > 0.0f && c.t < c.until * 0.35f ? 0 : c.t < c.until * 0.65f ? 1 : 2; break;
        case BAT:   { static const int kFlap[4] = {0, 1, 2, 1}; frame = kFlap[static_cast<int>(c.anim * 14.0f) % 4]; break; }
        case RAT:   frame = (fleeing || c.state == Critter::ALERT) ? 1 + static_cast<int>(c.anim * 12.0f) % 2 : 0; break;
        case LIZARD:
            frame = fleeing ? 2 + static_cast<int>(c.anim * 16.0f) % 2 : (static_cast<int>(c.anim * 0.8f) % 4 == 0 ? 1 : 0);
            break;
        case DEER:
            frame = (fleeing || c.state == Critter::LEAVING) ? 3 + static_cast<int>(c.anim * 6.0f) % 2
                  : c.state == Critter::BUSY ? 0 : c.state == Critter::ALERT ? 2 : 1;
            break;
        default: frame = 0; break;
    }
    const SDL_Rect cell = CritterCell(c.kind, frame);
    const SDL_FRect src = {static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.w), static_cast<float>(cell.h)};
    // Each stands on the bottom of its picture, two pixels up for its outline.
    const float foot = static_cast<float>(cell.h) - 2.0f;
    float lift = c.h;
    if (c.kind == DEER && fleeing) lift += 3.0f * fabsf(sinf(c.anim * 9.0f));
    if (c.kind == RABBIT && fleeing && c.until >= 0.0f) lift += 3.0f * fabsf(sinf(c.anim * 18.0f));
    const SDL_FRect world = {c.x - cell.w / 2.0f, c.y - foot - lift, static_cast<float>(cell.w), static_cast<float>(cell.h)};
    const SDL_FRect dst = cam.ToScreenRect(world);
    SDL_SetTextureColorMod(tex, static_cast<Uint8>(c.tint.r * critter_light), static_cast<Uint8>(c.tint.g * critter_light),
                           static_cast<Uint8>(c.tint.b * critter_light));
    float alpha = c.fade;
    // The figure is never quite solid, and fades as you look.
    if (c.kind == WATCHER) alpha *= 0.75f + 0.15f * sinf(c.anim * 1.3f);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(255.0f * std::clamp(alpha, 0.0f, 1.0f)));
    SDL_RenderTextureRotated(r, tex, &src, &dst, 0.0, nullptr, c.left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}
