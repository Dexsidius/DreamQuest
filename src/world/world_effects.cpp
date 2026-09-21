// -----------------------------------------------------------------------------
//  World, continued: what spells shed, and the dust at a runner's heels -- only ever for show
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

// -----------------------------------------------------------------------------
//  What spells shed
//
//  A bolt in the air used to be a picture moving: nothing came off it, and where
//  it landed there was a disc in its colour and three lines. So each element
//  sheds what it is made of as it flies -- embers and smoke, drops, dust and
//  chips of stone, streaks of air, sparks -- throws the same up where it lands,
//  and keeps doing it on the ground it leaves burning or breaks open.
//
//  None of it is the game's business. Nothing asks where an ember is; a friend's
//  machine makes its own from the shots it is told about; and the dice are its
//  own, so that what is only for show never moves the ones the game is played
//  with.
// -----------------------------------------------------------------------------

static std::mt19937& ShowDice() { static std::mt19937 dice(0x5EED5); return dice; }
static float Between(float a, float b) {
    return a + (b - a) * static_cast<float>(ShowDice()() & 0xFFFF) / 65535.0f;
}
static bool Chance(float p) { return Between(0.0f, 1.0f) < p; }
static constexpr size_t MOTES_MAX = 700;

static const SDL_Color EMBER_HOT{255, 228, 120, 255}, EMBER_COLD{206, 52, 20, 0};
static const SDL_Color SMOKE_NEW{84, 70, 64, 140},    SMOKE_OLD{40, 36, 36, 0};
static const SDL_Color DROP_NEW{206, 238, 255, 255},  DROP_OLD{56, 124, 214, 0};
static const SDL_Color CHIP_NEW{150, 118, 80, 255},   CHIP_OLD{84, 62, 44, 0};
static const SDL_Color DUST_NEW{214, 192, 150, 205},  DUST_OLD{160, 140, 110, 0};
static const SDL_Color AIR_NEW{240, 250, 255, 235},   AIR_OLD{190, 225, 245, 0};
static const SDL_Color SPARK_NEW{246, 232, 255, 255}, SPARK_OLD{150, 96, 255, 0};

static Mote Speck(float x, float y, float vx, float vy, float life, float size, SDL_Color from, SDL_Color to) {
    Mote m;
    m.x = x; m.y = y; m.vx = vx; m.vy = vy;
    m.life = m.max_life = life;
    m.size = size;
    m.from = from; m.to = to;
    return m;
}

// What one bolt leaves behind it over one step of its flight: (dx, dy) is the
// way it is going, `size` one for an apprentice's bolt.
static void ShedTrail(vector<Mote>& motes, Element e, float x, float y, float dx, float dy,
                      float speed, float size, float lift, SDL_Color own = {0, 0, 0, 0}) {
    const float sx = -dy, sy = dx;                  // across its path
    const auto behind = [&](float back, float across, float& px, float& py) {
        px = x - dx * back + sx * across;
        py = y - dy * back + sy * across;
    };
    float px = 0, py = 0;
    switch (e) {
        case Element::Fire: {
            behind(Between(4.0f, 14.0f) * size, Between(-4.0f, 4.0f) * size, px, py);
            Mote m = Speck(px, py, -dx * Between(10, 40) + sx * Between(-18, 18),
                           -dy * Between(10, 40) + sy * Between(-18, 18) - Between(8, 26),
                           Between(0.25f, 0.55f), Chance(0.25f * size) ? 2.0f : 1.0f, EMBER_HOT, EMBER_COLD);
            m.gravity = -30.0f; m.drag = 2.0f; m.lift = lift;
            motes.push_back(m);
            if (Chance(0.3f)) {
                behind(Between(10.0f, 20.0f) * size, Between(-3.0f, 3.0f), px, py);
                Mote s = Speck(px, py, -dx * 10.0f, -dy * 10.0f - 14.0f, Between(0.5f, 0.9f), 2.0f, SMOKE_NEW, SMOKE_OLD);
                s.grow = 3.0f; s.drag = 1.0f; s.lift = lift;
                motes.push_back(s);
            }
            break;
        }
        case Element::Water: {
            behind(Between(4.0f, 12.0f) * size, Between(-4.0f, 4.0f) * size, px, py);
            Mote m = Speck(px, py, -dx * Between(10, 30) + sx * Between(-14, 14),
                           -dy * Between(10, 30) + sy * Between(-14, 14),
                           Between(0.3f, 0.5f), Chance(0.25f * size) ? 2.0f : 1.0f, DROP_NEW, DROP_OLD);
            m.gravity = 160.0f; m.lift = lift;
            motes.push_back(m);
            break;
        }
        case Element::Earth: {
            behind(Between(3.0f, 9.0f) * size, Between(-3.0f, 3.0f) * size, px, py);
            Mote d = Speck(px, py, -dx * Between(5, 20), -dy * Between(5, 20) - 6.0f,
                           Between(0.35f, 0.6f), 2.0f, DUST_NEW, DUST_OLD);
            d.grow = 2.5f; d.drag = 2.0f; d.lift = lift;
            motes.push_back(d);
            if (Chance(0.5f)) {
                Mote c = Speck(px, py, -dx * Between(10, 40) + sx * Between(-30, 30),
                               -dy * Between(10, 40) + sy * Between(-30, 30) - Between(10, 40),
                               Between(0.3f, 0.5f), 1.0f, CHIP_NEW, CHIP_OLD);
                c.gravity = 220.0f; c.lift = lift;
                motes.push_back(c);
            }
            break;
        }
        case Element::Air: {
            // Lines of it left hanging either side, carried along a little.
            behind(Between(6.0f, 18.0f), Between(-10.0f, 10.0f) * size, px, py);
            Mote m = Speck(px, py, dx * speed * 0.35f, dy * speed * 0.35f,
                           Between(0.14f, 0.26f), Between(5.0f, 10.0f), AIR_NEW, AIR_OLD);
            m.kind = Mote::Kind::Streak; m.lift = lift;
            motes.push_back(m);
            break;
        }
        case Element::Arcane: {
            behind(Between(2.0f, 10.0f), Between(-5.0f, 5.0f) * size, px, py);
            // Violet, the school's own -- or the spell's: acid is green.
            const SDL_Color pale = own.a ? SDL_Color{static_cast<Uint8>((own.r + 510) / 3), static_cast<Uint8>((own.g + 510) / 3),
                                                     static_cast<Uint8>((own.b + 510) / 3), 255} : SPARK_NEW;
            const SDL_Color gone = own.a ? SDL_Color{own.r, own.g, own.b, 0} : SPARK_OLD;
            Mote m = Speck(px, py, Between(-8, 8), Between(-8, 8), Between(0.2f, 0.4f), 1.0f, pale, gone);
            m.lift = lift;
            motes.push_back(m);
            break;
        }
        default: break;
    }
}

void World::BurstOf(Element e, float x, float y, float lift, float size, float nx, float ny) {
    if (e == Element::None) return;
    // All round, leaning off the face it struck.
    const auto thrown = [&](float slow, float fast, float& vx, float& vy) {
        const float a = Between(0.0f, 6.2831853f), v = Between(slow, fast);
        vx = (cosf(a) + nx * 0.9f) * v;
        vy = (sinf(a) + ny * 0.9f) * v;
    };
    const auto count = [&](int base) { return std::max(2, static_cast<int>(base * size + 0.5f)); };
    const auto ring = [&](float grow, float life, SDL_Color from, SDL_Color to) {
        Mote m = Speck(x, y, 0.0f, 0.0f, life, 3.0f * size, from, to);
        m.kind = Mote::Kind::Ring; m.grow = grow * size; m.lift = lift;
        motes.push_back(m);
    };
    float vx = 0, vy = 0;
    switch (e) {
        case Element::Fire:
            for (int i = count(12); i-- > 0;) {
                thrown(30, 95, vx, vy);
                Mote m = Speck(x, y, vx, vy - Between(0, 30), Between(0.28f, 0.6f), Chance(0.3f) ? 2.0f : 1.0f,
                               EMBER_HOT, EMBER_COLD);
                m.gravity = -40.0f; m.drag = 2.2f; m.lift = lift;
                motes.push_back(m);
            }
            for (int i = count(3); i-- > 0;) {
                thrown(6, 20, vx, vy);
                Mote s = Speck(x, y, vx, vy - 16.0f, Between(0.5f, 0.9f), 2.0f, SMOKE_NEW, SMOKE_OLD);
                s.grow = 4.0f; s.drag = 1.5f; s.lift = lift;
                motes.push_back(s);
            }
            ring(40.0f, 0.22f, {255, 196, 96, 230}, {255, 120, 40, 0});
            break;
        case Element::Water:
            // A splash: up and out, and down again.
            for (int i = count(12); i-- > 0;) {
                thrown(30, 85, vx, vy);
                Mote m = Speck(x, y, vx, vy - Between(20, 70), Between(0.3f, 0.55f), Chance(0.3f) ? 2.0f : 1.0f,
                               DROP_NEW, DROP_OLD);
                m.gravity = 300.0f; m.lift = lift;
                motes.push_back(m);
            }
            ring(46.0f, 0.32f, {214, 240, 255, 235}, {110, 170, 235, 0});
            break;
        case Element::Earth:
            for (int i = count(9); i-- > 0;) {
                thrown(30, 90, vx, vy);
                Mote m = Speck(x, y, vx, vy - Between(20, 60), Between(0.3f, 0.55f), Chance(0.4f) ? 2.0f : 1.0f,
                               CHIP_NEW, CHIP_OLD);
                m.gravity = 340.0f; m.lift = lift;
                motes.push_back(m);
            }
            for (int i = count(5); i-- > 0;) {
                thrown(8, 26, vx, vy);
                Mote d = Speck(x, y, vx, vy, Between(0.35f, 0.65f), 2.0f, DUST_NEW, DUST_OLD);
                d.grow = 5.0f; d.drag = 2.0f; d.lift = lift;
                motes.push_back(d);
            }
            break;
        case Element::Air:
            for (int i = count(8); i-- > 0;) {
                thrown(70, 140, vx, vy);
                Mote m = Speck(x, y, vx, vy, Between(0.16f, 0.3f), Between(4.0f, 8.0f), AIR_NEW, AIR_OLD);
                m.kind = Mote::Kind::Streak; m.drag = 3.0f; m.lift = lift;
                motes.push_back(m);
            }
            ring(60.0f, 0.26f, {236, 248, 255, 220}, {200, 230, 245, 0});
            break;
        case Element::Arcane:
            for (int i = count(10); i-- > 0;) {
                thrown(20, 70, vx, vy);
                Mote m = Speck(x, y, vx, vy, Between(0.25f, 0.5f), Chance(0.25f) ? 2.0f : 1.0f, SPARK_NEW, SPARK_OLD);
                m.drag = 2.5f; m.lift = lift;
                motes.push_back(m);
            }
            break;
        default: break;
    }
}

void World::ShedFromShots() {
    for (auto& [id, seen] : shots_seen) seen.here = false;

    for (const Projectile& p : projectiles) {
        if (p.finished || !p.def) continue;
        const auto [it, fresh] = shots_seen.try_emplace(p.net_id);
        ShotSeen& seen = it->second;
        if (fresh) { seen.x = p.x; seen.y = p.y; }
        seen.here = true;
        seen.shed = p.def->shed;
        seen.size = std::max(0.75f, p.def->radius / 6.0f);
        seen.lift = p.lift >= 0.0f ? p.lift : LiftAt(p.x, p.y);
        const float moved = Length(p.x - seen.x, p.y - seen.y);
        seen.x = p.x; seen.y = p.y;
        if (seen.shed == Element::None) continue;
        // Put somewhere else altogether -- the host's word about it, arriving
        // late -- it sheds nothing over the gap.
        if (moved > 60.0f) continue;
        seen.owed += moved;

        const float speed = Length(p.vx, p.vy);
        if (speed < 1.0f) continue;
        const float every = seen.shed == Element::Fire ? 3.5f : seen.shed == Element::Air ? 7.0f
                          : seen.shed == Element::Earth ? 6.0f : 5.0f;
        while (seen.owed >= every) {
            seen.owed -= every;
            ShedTrail(motes, seen.shed, p.x, p.y, p.vx / speed, p.vy / speed, speed, seen.size, seen.lift,
                      p.def->shed_color);
        }
    }

    for (auto it = shots_seen.begin(); it != shots_seen.end();) {
        if (it->second.here) { ++it; continue; }
        // The host knows what its shots met, and says so as it happens. A
        // friend's machine is only ever told where the shots are: one that is
        // no longer spoken of has met something, and met it where it last was.
        if (visiting) BurstOf(it->second.shed, it->second.x, it->second.y, it->second.lift, it->second.size, 0.0f, 0.0f);
        it = shots_seen.erase(it);
    }
}

void World::ShedFromGround(float dt) {
    for (const GroundEffect& g : ground_effects) {
        if (g.finished || !g.Active() || g.rain || g.once || g.Look() == Element::None) continue;
        const float lift = LiftAt(g.x, g.y);
        const Element look = g.Look();
        float rate = g.radius * (look == Element::Earth ? 4.0f : look == Element::Fire ? 3.2f : 1.6f);
        // What turns sheds along the turn: water dragged round and in, air
        // round and up.
        if (g.draw == GroundEffect::Draw::Whirlpool || g.draw == GroundEffect::Draw::Tornado ||
            g.draw == GroundEffect::Draw::Turbulence) {
            int spun = static_cast<int>(g.radius * 0.9f * dt);
            if (Chance(g.radius * 0.9f * dt - spun)) ++spun;
            for (; spun > 0; --spun) {
                const float a = Between(0.0f, 6.2831853f), d = Between(0.35f, 1.0f) * g.radius;
                const float x = g.x + cosf(a) * d, y = g.y + sinf(a) * d * 0.62f;
                Mote m;
                if (g.draw == GroundEffect::Draw::Whirlpool) {
                    m = Speck(x, y, -sinf(a) * 70.0f - cosf(a) * 40.0f, (cosf(a) * 70.0f - sinf(a) * 40.0f) * 0.62f,
                              Between(0.3f, 0.55f), Chance(0.3f) ? 2.0f : 1.0f, DROP_NEW, DROP_OLD);
                } else {
                    m = Speck(x, y - Between(0.0f, 50.0f), -sinf(a) * 130.0f, cosf(a) * 60.0f - 40.0f, Between(0.16f, 0.3f),
                              Between(4.0f, 8.0f), AIR_NEW, AIR_OLD);
                    m.kind = Mote::Kind::Streak;
                }
                m.lift = lift;
                motes.push_back(m);
            }
            continue;
        }
        int n = static_cast<int>(rate * dt);
        if (Chance(rate * dt - n)) ++n;
        for (; n > 0; --n) {
            // Anywhere in the circle, evenly.
            const float a = Between(0.0f, 6.2831853f), d = sqrtf(Between(0.0f, 1.0f)) * g.radius * 0.92f;
            const float x = g.x + cosf(a) * d, y = g.y + sinf(a) * d;
            Mote m;
            switch (look) {
                case Element::Fire:
                    // Flames standing on it: the disc under them is the glow.
                    // Two in three are tongues, and the rest sparks going up.
                    if (Chance(0.66f)) {
                        m = Speck(x, y, Between(-4, 4), -Between(6, 16), Between(0.3f, 0.55f), Chance(0.4f) ? 3.0f : 2.0f,
                                  EMBER_HOT, EMBER_COLD);
                        m.tall = Between(2.0f, 5.0f);
                    } else {
                        m = Speck(x, y, Between(-6, 6), -Between(20, 46), Between(0.3f, 0.6f), 1.0f, EMBER_HOT, EMBER_COLD);
                        m.gravity = -30.0f;
                    }
                    break;
                case Element::Earth:
                    // The ground coming up: thrown high and outward, and down again.
                    m = Speck(x, y, cosf(a) * Between(20, 70), sinf(a) * Between(10, 40) - Between(40, 120),
                              Between(0.3f, 0.6f), Chance(0.45f) ? 2.0f : 1.0f, CHIP_NEW, CHIP_OLD);
                    m.gravity = 380.0f;
                    break;
                case Element::Water:
                    m = Speck(x, y, cosf(a) * Between(10, 40), -Between(30, 90), Between(0.3f, 0.5f),
                              Chance(0.3f) ? 2.0f : 1.0f, DROP_NEW, DROP_OLD);
                    m.gravity = 300.0f;
                    break;
                case Element::Air:
                    // Round and round.
                    m = Speck(x, y, -sinf(a) * Between(60, 120), cosf(a) * Between(60, 120), Between(0.16f, 0.3f),
                              Between(4.0f, 8.0f), AIR_NEW, AIR_OLD);
                    m.kind = Mote::Kind::Streak;
                    break;
                default:
                    m = Speck(x, y, Between(-10, 10), -Between(4, 20), Between(0.25f, 0.5f), 1.0f, SPARK_NEW, SPARK_OLD);
                    break;
            }
            m.lift = lift;
            motes.push_back(m);
        }
    }
}

// What is on a monster shows on it: embers off what burns, drops off what is
// soaked or bleeding, bubbles off what is poisoned, frost off the cold, and
// stars going round a head that has been rung. From the bits alone, so a
// friend's machine -- which is told the bits and nothing else -- does the same.
void World::ShedFromStatuses(float dt) {
    for (const auto& e : enemies) {
        if (!e->statuses.Any() || e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
        const SDL_FRect body = e->BodyBox();
        const float lift = e->draw_lift;
        const auto somewhere = [&](float& x, float& y) {
            x = body.x + Between(0.15f, 0.85f) * body.w;
            y = body.y + Between(0.10f, 0.80f) * body.h;
        };
        const auto sometimes = [&](float a_second) { return Chance(a_second * dt); };
        float x = 0, y = 0;
        if (e->Afflicted(Status::Burn) && sometimes(16.0f)) {
            somewhere(x, y);
            Mote m = Speck(x, y, Between(-6, 6), -Between(16, 40), Between(0.3f, 0.55f), Chance(0.4f) ? 2.0f : 1.0f,
                           EMBER_HOT, EMBER_COLD);
            if (Chance(0.5f)) m.tall = Between(2.0f, 4.0f);
            m.gravity = -30.0f; m.lift = lift;
            motes.push_back(m);
        }
        if (e->Afflicted(Status::Wet) && sometimes(9.0f)) {
            somewhere(x, y);
            Mote m = Speck(x, y, Between(-4, 4), Between(4, 16), Between(0.3f, 0.5f), 1.0f, DROP_NEW, DROP_OLD);
            m.gravity = 200.0f; m.lift = lift;
            motes.push_back(m);
        }
        if (e->Afflicted(Status::Bleed) && sometimes(8.0f)) {
            somewhere(x, y);
            Mote m = Speck(x, y, Between(-5, 5), Between(2, 10), Between(0.35f, 0.55f), 1.0f,
                           {226, 60, 70, 255}, {120, 16, 28, 0});
            m.gravity = 220.0f; m.lift = lift;
            motes.push_back(m);
        }
        if (e->Afflicted(Status::Poison) && sometimes(9.0f)) {
            somewhere(x, y);
            Mote m = Speck(x, y, Between(-5, 5), -Between(8, 20), Between(0.4f, 0.7f), Chance(0.35f) ? 2.0f : 1.0f,
                           {190, 240, 120, 235}, {70, 140, 40, 0});
            m.drag = 1.0f; m.lift = lift;
            motes.push_back(m);
        }
        if ((e->Afflicted(Status::Chill) || e->Afflicted(Status::Frozen)) && sometimes(e->Afflicted(Status::Frozen) ? 14.0f : 8.0f)) {
            somewhere(x, y);
            Mote m = Speck(x, y, Between(-8, 8), Between(2, 12), Between(0.4f, 0.7f), 1.0f,
                           {244, 252, 255, 245}, {150, 200, 240, 0});
            m.drag = 1.5f; m.lift = lift;
            motes.push_back(m);
        }
        if (e->Afflicted(Status::Concussed) && sometimes(14.0f)) {
            // Round the head, and carried on round it for the moment each lasts.
            const float a = Between(0.0f, 6.2831853f);
            const float hx = body.x + body.w / 2.0f, hy = body.y + 2.0f;
            Mote m = Speck(hx + cosf(a) * 9.0f, hy + sinf(a) * 3.5f, -sinf(a) * 30.0f, cosf(a) * 11.0f, 0.28f, 1.0f,
                           {255, 240, 150, 255}, {236, 200, 90, 0});
            m.lift = lift;
            motes.push_back(m);
        }
    }
}

void World::UpdateMotes(float dt) {
    for (Mote& m : motes) {
        m.life -= dt;
        m.vy += m.gravity * dt;
        const float keep = std::max(0.0f, 1.0f - m.drag * dt);
        m.vx *= keep;
        m.vy *= keep;
        m.x += m.vx * dt;
        m.y += m.vy * dt;
        m.size += m.grow * dt;
    }
    motes.erase(std::remove_if(motes.begin(), motes.end(), [](const Mote& m) { return m.life <= 0.0f; }),
                motes.end());
    // A room full of mages is a great many embers: the oldest go first.
    if (motes.size() > MOTES_MAX) motes.erase(motes.begin(), motes.begin() + (motes.size() - MOTES_MAX));
}

void World::AddDust(float x, float y, float dir_x, float dir_y) {
    // Two or three puffs at the heel, thrown back against the direction of
    // travel and spreading as they fade.
    static std::mt19937 rng(0xD057);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    const int n = 2 + static_cast<int>(rng() % 2);
    for (int i = 0; i < n; ++i) {
        Dust d;
        d.x = x - dir_x * 4.0f + u(rng) * 3.0f;
        d.y = y - 1.0f + u(rng) * 1.5f;
        d.vx = -dir_x * 16.0f + u(rng) * 10.0f;
        d.vy = -dir_y * 10.0f - 5.0f + u(rng) * 3.0f;
        d.life = d.max_life = 0.38f + 0.12f * u(rng);
        d.size = 2.2f + 0.8f * u(rng);
        dust.push_back(d);
    }
    if (dust.size() > 64) dust.erase(dust.begin(), dust.begin() + (dust.size() - 64));
}

void World::UpdateDust(float dt) {
    for (Dust& d : dust) {
        d.life -= dt;
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        d.vx *= std::max(0.0f, 1.0f - 3.0f * dt);
        d.vy *= std::max(0.0f, 1.0f - 3.0f * dt);
    }
    dust.erase(std::remove_if(dust.begin(), dust.end(),
                              [](const Dust& d) { return d.life <= 0.0f; }),
               dust.end());
}
