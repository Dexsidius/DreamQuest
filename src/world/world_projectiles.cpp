// -----------------------------------------------------------------------------
//  World, continued: what flies, what it strikes, and what it leaves on the ground
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

void World::SpawnProjectile(const string& def_id, float x, float y,
                            float dir_x, float dir_y,
                            const CombatProfile& owner, AttackStyle style,
                            float damage_mult, bool from_player,
                            const GameContext& ctx) {
    const ProjectileDef* def = ctx.projectiles ? ctx.projectiles->Get(def_id) : nullptr;
    if (!def) {
        SDL_Log("World: unknown projectile '%s'", def_id.c_str());
        return;
    }

    const float len = Length(dir_x, dir_y);
    if (len < 0.001f) return;

    Projectile p;
    p.def = def;
    p.x = x;
    p.y = y;
    p.from_x = x;
    p.from_y = y;
    p.vx = (dir_x / len) * def->speed;
    p.vy = (dir_y / len) * def->speed;
    p.angle = atan2f(p.vy, p.vx);
    p.life = def->life;
    p.owner = owner;
    p.style = style;
    p.element = def->element;
    p.damage_mult = damage_mult;
    p.from_player = from_player;
    p.owner_local = player.local;
    p.owner_seat = player.seat;
    p.cast_id = from_player ? casting : 0;
    p.net_id = next_net_id++;
    p.pierce_left  = def->pierce;
    p.bounces_left = def->bounces;
    projectiles.push_back(p);
}

void World::ThrowPracticeBolt(const string& bolt, float x, float y, float tx, float ty, const GameContext& ctx) {
    // A friend's machine is shown the host's, like any other shot.
    if (visiting) return;
    const float dx = tx - x, dy = ty - y;
    const float far = Length(dx, dy);
    if (far < 8.0f) return;
    const size_t before = projectiles.size();
    SpawnProjectile(bolt, x + dx / far * 10.0f, y + dy / far * 10.0f, dx, dy, CombatProfile{}, AttackStyle::Magic,
                    0.0f, false, ctx);
    if (projectiles.size() == before) return;
    Projectile& p = projectiles.back();
    p.show = true;
    p.show_left = std::max(8.0f, far - 14.0f);
    p.life = std::max(p.life, far / std::max(40.0f, Length(p.vx, p.vy)) + 0.2f);
    Audio::PlayAt(Sfx::SpellCast, x, y, 0.45f, 1.1f);
}

void World::AddGroundEffect(const GroundEffect& effect) {
    ground_effects.push_back(effect);
    ground_effects.back().owner_local = player.local;
    ground_effects.back().owner_seat = player.seat;
    // A meteor is the cast's own; what a bolt leaves burning says so itself.
    if (ground_effects.back().cast_id == 0 && effect.from_player) ground_effects.back().cast_id = casting;
}

uint32_t World::OpenCast(int skill, int xp) {
    if (xp <= 0) return 0;
    OwedCast c;
    c.id = next_cast_id++;
    if (next_cast_id == 0) next_cast_id = 1;      // nothing is ever cast number nothing
    c.skill = skill;
    c.xp = xp;
    owed_casts.push_back(c);
    return c.id;
}

// Called with the caster acting, as everything in HitEnemy is.
void World::PayCast(uint32_t id) {
    if (id == 0) return;
    for (size_t i = 0; i < owed_casts.size(); ++i) {
        if (owed_casts[i].id != id) continue;
        player.GrantXp(owed_casts[i].skill, owed_casts[i].xp);
        owed_casts.erase(owed_casts.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

// A cast nothing is left of will never land: the bolt met a wall, or ran out
// of air. The list is a handful long, so asking everything in flight is cheap.
void World::ForgetSpentCasts() {
    if (owed_casts.empty()) return;
    owed_casts.erase(std::remove_if(owed_casts.begin(), owed_casts.end(), [&](const OwedCast& c) {
        for (const Projectile& p : projectiles) if (!p.finished && p.cast_id == c.id) return false;
        for (const GroundEffect& g : ground_effects) if (!g.finished && g.cast_id == c.id) return false;
        return true;
    }), owed_casts.end());
}

void World::UpdateProjectiles(float dt, const GameContext& ctx) {
    for (Projectile& p : projectiles) {
        if (p.finished || !p.def) continue;

        p.life -= dt;
        if (p.life <= 0.0f) {
            // Out of air: it goes out, a little, rather than blinking off.
            p.finished = true;
            BurstOf(p.def->shed, p.x, p.y, p.lift >= 0.0f ? p.lift : LiftAt(p.x, p.y),
                    std::max(0.75f, p.def->radius / 6.0f) * 0.5f, 0.0f, 0.0f);
            // A thrown knife that struck nothing is heard going by, once. It
            // is the sound of its whole flight, so it is placed halfway along
            // it: at the end of a knife's range (some 240 px) a sound fades to
            // two-fifths, and the thrower is the one it is for. Halfway it is
            // still heard off to the side the knife went. What pierced
            // something on the way was heard going in, and is not a miss.
            if (p.from_player && p.def->thrown && !p.show && p.already_hit.empty())
                Audio::PlayAt(Sfx::Whiff, (p.x + p.from_x) * 0.5f, (p.y + p.from_y) * 0.5f);
        }

        // Homing: turn toward the monster it was loosed at, no faster than the
        // projectile allows. Once that monster is dead, gone, or already behind
        // it, the shot flies on straight -- it never circles back.
        const float homing = p.def->homing + p.extra_homing;
        if (p.target && homing > 0.0f) {
            const Enemy* t = nullptr;
            for (const auto& e : enemies) if (e.get() == p.target) { t = e.get(); break; }
            if (!t || !Targeting::Targetable(*t)) {
                p.target = nullptr;
            } else {
                const SDL_FPoint a = Targeting::AimPoint(*t);
                const float have = atan2f(p.vy, p.vx);
                const float want = atan2f(a.y - p.y, a.x - p.x);
                const float diff = remainderf(want - have, 6.2831853f);
                if (fabsf(diff) > 1.75f) {
                    p.target = nullptr;
                } else {
                    const float turn = std::clamp(diff, -homing * dt, homing * dt);
                    const float speed = Length(p.vx, p.vy);
                    p.vx = cosf(have + turn) * speed;
                    p.vy = sinf(have + turn) * speed;
                    p.angle = have + turn;
                }
            }
        }

        // Step in slices no longer than half the projectile's own radius, so a
        // fast arrow cannot pass through a wall or a thin target between one
        // frame and the next. Radius rather than a fixed distance: a small
        // fast bolt needs finer steps than a large slow one.
        const float travel = Length(p.vx, p.vy) * dt;
        const float max_step = std::max(2.0f, p.def->radius * 0.5f);
        const int steps = std::clamp(static_cast<int>(travel / max_step) + 1, 1, 32);
        const float step_dt = dt / steps;

        for (int i = 0; i < steps && !p.finished; ++i) {
            const float dx = p.vx * step_dt;
            const float dy = p.vy * step_dt;

            // Walls: resolve to the surface rather than stopping wherever the
            // step happened to land, so an impact is drawn on the wall and a
            // fire patch burns in front of it instead of inside it.
            const Map::Contact c = map.SweepPoint(p.x, p.y, dx, dy, p.def->radius);
            if (c.hit) {
                p.x = c.x;
                p.y = c.y;

                const bool can_bounce = p.bounces_left > 0 &&
                                        (c.nx != 0.0f || c.ny != 0.0f);
                AddImpact(p, c.nx, c.ny);
                Audio::PlayAt(Sfx::Impact, p.x, p.y, 0.7f);

                if (!can_bounce) {
                    p.hit_wall = true;
                    p.finished = true;
                    break;
                }

                --p.bounces_left;
                // Reflect about the surface: v' = v - 2(v.n)n.
                const float vn = p.vx * c.nx + p.vy * c.ny;
                p.vx -= 2.0f * vn * c.nx;
                p.vy -= 2.0f * vn * c.ny;

                const float keep = 1.0f - std::clamp(p.def->bounce_damping, 0.0f, 1.0f);
                p.vx *= keep;
                p.vy *= keep;
                p.angle = atan2f(p.vy, p.vx);

                // Ease off the surface so the next step does not immediately
                // find the same wall it just left.
                p.x += c.nx * (p.def->radius * 0.5f + 0.5f);
                p.y += c.ny * (p.def->radius * 0.5f + 0.5f);

                // A bounce that has lost almost all its speed is spent.
                if (Length(p.vx, p.vy) < 40.0f) { p.finished = true; break; }
                continue;
            }

            p.x += dx;
            p.y += dy;
            if (p.def->spin) p.spin_angle += 14.0f * step_dt;

            // Practice: it has so far to go, and bursts when it has gone it.
            if (p.show) {
                p.show_left -= Length(dx, dy);
                if (p.show_left <= 0.0f) {
                    const float speed = std::max(1.0f, Length(p.vx, p.vy));
                    AddImpact(p, -p.vx / speed, -p.vy / speed);
                    Burst(p.x, p.y, 26.0f, {255, 236, 200, 255}, 6);
                    Audio::PlayAt(Sfx::Impact, p.x, p.y, 0.4f);
                    p.finished = true;
                }
                continue;
            }

            const SDL_FRect box = {p.x - p.def->radius, p.y - p.def->radius,
                                   p.def->radius * 2, p.def->radius * 2};

            if (p.from_player) {
                for (auto& e : enemies) {
                    if (p.finished) break;
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead || e->Hidden()) continue;
                    if (!RectsOverlap(box, e->BodyBox())) continue;

                    const void* key = e.get();
                    if (std::find(p.already_hit.begin(), p.already_hit.end(), key) !=
                        p.already_hit.end())
                        continue;
                    p.already_hit.push_back(key);

                    ActAs(OwnerOf(p.owner_local, p.owner_seat), [&] {
                        crit_next = p.sure_crit;
                        cast_next = p.cast_id;
                        proc_next = p.def->status;
                        // An arrow, a bolt or a knife leaves nothing of its
                        // own; a Brand on the bow that loosed it does.
                        if (!proc_next.Any() && p.style == AttackStyle::Ranged)
                            if (const ItemDef* bow = player.equipment.Weapon()) proc_next = bow->on_hit;
                        leech_next = p.def->leech;
                        pierce_next = p.def->armour_pierce;
                        knife_next = p.def->thrown;
                        HitEnemy(*e, p.owner, p.style, p.element, p.damage_mult,
                                 p.def->knockback * p.knockback_mult, p.x - p.vx, p.y - p.vy, ctx);
                        if (p.combo != ComboMove::None)
                            ComboShotHitFx(p.combo, p.style, p.element, e->x, e->y - 20.0f, atan2f(p.vy, p.vx));
                        else if (p.technique_fx)
                            TechniqueShotHitFx(p.technique_fx, p.element, e->x, e->y - 20.0f, atan2f(p.vy, p.vx));
                        crit_next = false;
                        cast_next = 0;
                        proc_next = {};
                        leech_next = 0.0f;
                        pierce_next = 0.0f;
                        knife_next = false;
                    });
                    // It breaks on what it strikes, back the way it came --
                    // and on everything it goes through, which is how a bolt
                    // that pierces is seen to.
                    {
                        const float speed = std::max(1.0f, Length(p.vx, p.vy));
                        BurstOf(p.def->shed, p.x, p.y, p.lift >= 0.0f ? p.lift : LiftAt(p.x, p.y),
                                std::max(0.75f, p.def->radius / 6.0f), -p.vx / speed, -p.vy / speed);
                    }

                    if (p.pierce_left > 0) --p.pierce_left;
                    else                    p.finished = true;
                }
            } else if (Player* struck = PlayerTouching(box)) {
                ActAs(*struck, [&] {
                    // A shot that reaches them lands, as a swing does: see
                    // RollMonsterBlow. Where it came from is back along its
                    // flight; what it leaves draws them to where it was loosed.
                    const DamageResult r = RollMonsterBlow(p.owner, player.Profile(), p.style,
                                                           p.damage_mult, *ctx.rng);
                    HitPlayer(r.damage, p.owner, p.x - p.vx, p.y - p.vy, 0.0f, 0.0f,
                              p.def->status, p.from_x, p.from_y);
                });
                {
                    const float speed = std::max(1.0f, Length(p.vx, p.vy));
                    BurstOf(p.def->shed, p.x, p.y, p.lift >= 0.0f ? p.lift : LiftAt(p.x, p.y),
                            std::max(0.75f, p.def->radius / 6.0f), -p.vx / speed, -p.vy / speed);
                }
                p.finished = true;
            }
        }

        // What it leaves behind when it stops. Against a wall the contact
        // point is flush with the surface, so nudge the effect back along the
        // direction of travel -- burning ground should lie in front of the
        // wall where someone can be standing in it, not half inside it.
        if (p.finished && !p.show) {
            float ex = p.x, ey = p.y;
            if (p.hit_wall) {
                const float len = Length(p.vx, p.vy);
                if (len > 0.0f) {
                    const float back = std::max(p.def->patch_radius,
                                                p.def->erupt_radius) * 0.5f + 2.0f;
                    ex -= p.vx / len * back;
                    ey -= p.vy / len * back;
                }
            }
            if (p.def->patch_time > 0.0f) {
                GroundEffect g;
                g.x = ex;
                g.y = ey;
                g.radius = p.def->patch_radius;
                g.life = g.max_life = p.def->patch_time;
                g.tick_interval = p.def->patch_tick;
                g.tick_timer = 0.0f;
                g.damage = p.def->patch_damage;
                g.element = p.def->element;
                g.owner = p.owner;
                g.from_player = p.from_player;
                g.cast_id = p.cast_id;
                // Standing in it can leave what the bolt could -- at a third of
                // the chance a tick: it ticks four or five times, and at the
                // bolt's own odds an Ember left nine in ten of what stood in
                // it burning, which is not "some of the time".
                g.status = p.def->status;
                g.status.chance *= 0.35f;
                AddGroundEffect(g);
            }
            if (p.def->erupts) {
                GroundEffect g;
                g.x = ex;
                g.y = ey;
                g.radius = p.def->erupt_radius;
                g.delay = p.def->erupt_delay;
                g.life = g.max_life = p.def->erupt_delay + 0.28f;
                g.damage = p.def->erupt_damage;
                g.element = p.def->element;
                g.owner = p.owner;
                g.from_player = p.from_player;
                g.cast_id = p.cast_id;
                g.status = p.def->status;
                g.burst = true;
                AddGroundEffect(g);
            }
        }
    }

    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
                                     [](const Projectile& p) { return p.finished; }),
                      projectiles.end());
}

void World::AddImpact(const Projectile& p, float nx, float ny) {
    if (!p.def) return;
    // What it was made of, thrown back off the face it struck.
    BurstOf(p.def->shed, p.x, p.y, p.lift >= 0.0f ? p.lift : LiftAt(p.x, p.y),
            std::max(0.75f, p.def->radius / 6.0f), nx, ny);
    if (p.def->impact_size <= 0.0f) return;

    Impact im;
    im.x = p.x;
    im.y = p.y;
    im.nx = nx;
    im.ny = ny;
    im.radius = p.def->impact_size;
    im.life = im.max_life = 0.22f;
    // An elemental bolt splashes in its own colour; an untyped arrow throws
    // dust, so it takes the tint of the projectile art instead.
    im.color = (p.element != Element::None) ? ElementColor(p.element)
                                            : SDL_Color{214, 200, 176, 255};
    impacts.push_back(im);
}

void World::UpdateImpacts(float dt) {
    for (Impact& im : impacts) {
        im.life -= dt;
        if (im.life <= 0.0f) im.finished = true;
    }
    impacts.erase(std::remove_if(impacts.begin(), impacts.end(),
                                 [](const Impact& i) { return i.finished; }),
                  impacts.end());
}

void World::AddSlabSwing(float x, float y, float facing, float radius, float side, float lift) {
    SlabSwing s;
    s.x = x; s.y = y;
    s.facing = facing;
    s.from = facing - SLAB_SWEEP; s.to = facing + SLAB_SWEEP;
    s.radius = radius;
    s.side = side;
    s.life = s.max_life = SLAB_TIME;
    s.lift = lift;
    slabs.push_back(s);
    // The hole it came out of, and what came up with it.
    BurstOf(Element::Earth, x + cosf(s.from) * radius * 0.6f, y + 14.0f + sinf(s.from) * radius * 0.45f, lift,
            1.0f + side / 30.0f, 0.0f, 0.0f);
}

void World::AddSlabDrop(float x, float y, float side, float lift) {
    SlabSwing s;
    s.x = x; s.y = y;
    s.facing = 0.0f;
    s.radius = 0.0f;
    s.side = side;
    s.drop = true;
    s.life = s.max_life = SLAB_DROP_TIME;
    s.lift = lift;
    slabs.push_back(s);
}

void World::HearOfSlab(float x, float y, float facing, float radius, float side, bool drop) {
    // One is told of in every snapshot for as long as it lasts, and is one
    // slab: the same place and the same way, heard of again, is the one already
    // being drawn. It is kept a moment past its end for that.
    for (SlabSwing& s : slabs)
        if (s.drop == drop && fabsf(s.x - x) < 3.0f && fabsf(s.y - y) < 3.0f &&
            fabsf(s.facing - facing) < 0.06f) { s.told = 0.0f; return; }
    if (drop) AddSlabDrop(x, y, side, LiftAt(x, y));
    else      AddSlabSwing(x, y, facing, radius, side, LiftAt(x, y));
}

void World::UpdateSlabs(float dt) {
    for (SlabSwing& s : slabs) {
        const bool swinging = s.life > 0.0f;
        s.life -= dt;
        s.told += dt;
        if (!swinging) continue;
        if (s.drop) {
            // It lands: a thump, a ring of broken ground, and the dust of it.
            if (s.Fallen() >= 1.0f && s.dust <= 0.0f) {
                s.dust = 1.0f;
                Audio::PlayAt(Sfx::Impact, s.x, s.y, 0.9f, 0.62f);
                // Enough to say it struck, and no more: the chunks of it
                // sliding off are what the eye should be on.
                Burst(s.x, s.y, s.side * 0.7f, {198, 180, 146, 255}, 3, 0.8f);
                Shock(s.x, s.y, 0.35f + s.side / 80.0f, 0.25f + s.side / 120.0f);
                BurstOf(Element::Earth, s.x, s.y, s.lift, 1.0f + s.side / 24.0f, 0.0f, 0.0f);
                for (float w : {-1.0f, 1.0f})
                    AddDust(s.x + w * s.side, s.y + 2.0f, w, 0.0f);
            }
            continue;
        }
        // The ground under it: dust dragged along behind as it goes round, and
        // what is left of it thrown down where it stops.
        const float a = s.Angle(), out = s.Out();
        const float tx = s.x + cosf(a) * out, ty = s.y + 14.0f + sinf(a) * out * 0.72f;
        s.dust -= dt;
        if (s.dust <= 0.0f) {
            s.dust = 0.025f;
            AddDust(tx, ty, -sinf(a), cosf(a));
        }
        if (s.life <= 0.0f) BurstOf(Element::Earth, tx, ty, s.lift, 1.3f, 0.0f, 0.0f);
    }
    slabs.erase(std::remove_if(slabs.begin(), slabs.end(),
                               [](const SlabSwing& s) { return s.life <= 0.0f && s.told > 0.3f; }), slabs.end());
}

void World::AddFalling(float x, float y, float size, Element element, float seconds, float lift) {
    Falling f;
    f.x = x; f.y = y;
    f.size = size;
    f.element = element;
    f.life = f.max_life = std::max(0.05f, seconds);
    f.lift = lift;
    falls.push_back(f);
}

void World::HearOfFalling(float x, float y, float size, Element element) {
    // Told of in every snapshot until it lands, and is one meteor: the same
    // place and the same size, heard of again, is the one already in the air.
    for (Falling& f : falls)
        if (fabsf(f.x - x) < 3.0f && fabsf(f.y - y) < 3.0f) { f.told = 0.0f; return; }
    AddFalling(x, y, size, element, 0.6f, LiftAt(x, y));
}

void World::UpdateFalling(float dt) {
    for (Falling& f : falls) {
        const bool flying = f.life > 0.0f;
        f.life -= dt;
        f.told += dt;
        // The ground it throws up where it strikes. What it does to anyone
        // standing there is the ground effect's, which goes off at the same
        // moment: see "meteor" in FirePlayerProjectile.
        if (flying && f.life <= 0.0f) {
            Audio::PlayAt(Sfx::Impact, f.x, f.y, 1.0f, 0.5f);
            BurstOf(f.element, f.x, f.y, f.lift, 2.0f + f.size / 40.0f, 0.0f, 0.0f);
            Burst(f.x, f.y, f.size * 0.35f, ElementColor(f.element), 6, 0.6f);
            Shock(f.x, f.y, std::min(1.0f, 0.6f + f.size / 160.0f), std::min(0.8f, 0.4f + f.size / 200.0f));
            const SDL_Color c = ElementColor(f.element);
            Flash(c, 0.10f);
            for (float w : {-1.0f, 1.0f}) AddDust(f.x + w * f.size * 0.4f, f.y + 2.0f, w, 0.0f);
            // The ground it broke, the wave and the stones: marks, so the
            // host's alone -- a friend's window, which lands it too, is told.
            if (!visiting) MeteorLandFx(f.x, f.y, f.size, f.element);
        }
    }
    falls.erase(std::remove_if(falls.begin(), falls.end(),
                               [](const Falling& f) { return f.life <= 0.0f && f.told > 0.3f; }), falls.end());
}

void World::AddClaw(float x, float y, float facing, float reach, uint8_t look, float lift) {
    ClawSwipe c;
    c.x = x; c.y = y;
    c.facing = facing;
    c.reach = reach;
    c.look = look;
    c.life = c.max_life = CLAW_TIME;
    c.lift = lift;
    claws.push_back(c);
}

void World::HearOfClaw(float x, float y, float facing, float reach, uint8_t look) {
    for (ClawSwipe& c : claws)
        if (c.look == look && fabsf(c.x - x) < 3.0f && fabsf(c.y - y) < 3.0f &&
            fabsf(c.facing - facing) < 0.06f) { c.told = 0.0f; return; }
    AddClaw(x, y, facing, reach, look, LiftAt(x, y));
}

void World::UpdateClaws(float dt) {
    for (ClawSwipe& c : claws) { c.life -= dt; c.told += dt; }
    claws.erase(std::remove_if(claws.begin(), claws.end(),
                               [](const ClawSwipe& c) { return c.life <= 0.0f && c.told > 0.3f; }), claws.end());
}

// --- lightning -----------------------------------------------------------------------------

void World::AddArc(float x, float y, float to_x, float to_y, uint8_t look) {
    Arc a;
    a.x = x; a.y = y;
    a.facing = atan2f(to_y - y, to_x - x);
    a.reach = std::max(4.0f, Length(to_x - x, to_y - y));
    a.look = look;
    a.life = a.max_life = ARC_TIME;
    // Its own jags, settled once. Anything will do so long as two arcs in the
    // same frame differ and one arc does not change under the eye.
    a.seed = static_cast<uint32_t>(static_cast<int>(x * 7.0f) * 73856093 ^
                                   static_cast<int>(y * 7.0f) * 19349663 ^
                                   static_cast<int>(a.facing * 512.0f) * 83492791) | 1u;
    arcs.push_back(a);
    // A bolt out of the sky: the ground it strikes rings, and the whole view
    // goes white for a moment.
    if (look == 1) {
        Shock(to_x, to_y, 0.55f, 0.35f);
        Flash({226, 234, 255, 255}, 0.32f);
    }
}

void World::HearOfArc(float x, float y, float facing, float reach, uint8_t look) {
    // The same one twice is one arc: a snapshot says so for as long as it is
    // in the host's list, and a bolt drawn again every frame is a strobe.
    for (Arc& a : arcs)
        if (a.look == look && fabsf(a.x - x) < 3.0f && fabsf(a.y - y) < 3.0f &&
            fabsf(a.facing - facing) < 0.06f) { a.told = 0.0f; return; }
    AddArc(x, y, x + cosf(facing) * reach, y + sinf(facing) * reach, look);
}

void World::UpdateArcs(float dt) {
    for (Arc& a : arcs) { a.life -= dt; a.told += dt; }
    arcs.erase(std::remove_if(arcs.begin(), arcs.end(),
                              [](const Arc& a) { return a.life <= 0.0f && a.told > 0.3f; }), arcs.end());
}

void World::AddNode(const Node& n) {
    nodes.push_back(n);
    // It arcs as it lands rather than waiting out its first interval: a thing
    // thrown at a monster should do something to the monster it was thrown at.
    nodes.back().tick = 0.0f;
}

void World::HearOfNode(float x, float y, float life, float max_life) {
    for (Node& n : nodes)
        if (fabsf(n.x - x) < 3.0f && fabsf(n.y - y) < 3.0f) { n.told = 0.0f; n.life = life; return; }
    Node n;
    n.x = x; n.y = y;
    n.lift = LiftAt(x, y);
    n.life = life;
    n.max_life = std::max(0.1f, max_life);
    n.mine = false;               // a guest draws it; the host says what it hits
    n.tick = NODE_EVERY;
    nodes.push_back(n);
}

void World::UpdateNodes(float dt, const GameContext& ctx) {
    for (Node& n : nodes) {
        n.life -= dt;
        n.told += dt;
        if (!n.mine || n.life <= 0.0f) continue;
        n.tick -= dt;
        if (n.tick > 0.0f) continue;
        n.tick = NODE_EVERY;
        // It chains: the nearest few it can reach, one arc each. The node is
        // where the lightning comes from, so the first is not special -- a
        // second monster walking past is chained as readily as the one it was
        // thrown at.
        vector<Enemy*> near;
        for (auto& e : enemies) {
            if (!Strikeable(*e)) continue;
            const SDL_FPoint at = e->GroundCentre();
            if (Length(at.x - n.x, at.y - n.y) > NODE_REACH + e->GroundRadius()) continue;
            near.push_back(e.get());
        }
        std::sort(near.begin(), near.end(), [&](const Enemy* a, const Enemy* b) {
            return Length(a->x - n.x, a->y - n.y) < Length(b->x - n.x, b->y - n.y);
        });
        if (near.size() > static_cast<size_t>(std::max(1, n.chains))) near.resize(static_cast<size_t>(n.chains));
        for (Enemy* e : near) {
            const SDL_FPoint at = e->GroundCentre();
            AddArc(n.x, n.y - n.lift - 10.0f, at.x, at.y - 22.0f, 0);
            proc_next = n.status;
            HitEnemy(*e, n.owner, AttackStyle::Magic, Element::Electric, n.hit_mult, 8.0f,
                     n.x, n.y, ctx);
            proc_next = {};
        }
        if (!near.empty()) Audio::PlayAt(Sfx::SpellCast, n.x, n.y, 0.35f, 1.9f);
    }
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [](const Node& n) { return n.life <= 0.0f && n.told > 0.4f; }), nodes.end());
}

void World::UpdateGroundEffects(float dt, const GameContext& ctx) {
    // Shots that were owed: let go from where the caster is now, at what they
    // are fighting now.
    for (QueuedShot& q : queued_shots) q.in -= dt;
    for (size_t i = 0; i < queued_shots.size();) {
        if (queued_shots[i].in > 0.0f) { ++i; continue; }
        const QueuedShot q = queued_shots[i];
        queued_shots.erase(queued_shots.begin() + static_cast<std::ptrdiff_t>(i));
        const Vec2 aim = PlayerAim();
        const float a = atan2f(aim.y, aim.x) + q.spread * 3.14159265f / 180.0f;
        const SDL_FPoint muzzle = Targeting::Muzzle(player);
        const size_t before = projectiles.size();
        const uint32_t was = casting;
        casting = q.cast;
        SpawnProjectile(q.projectile, muzzle.x + cosf(a) * 12.0f, muzzle.y + sinf(a) * 12.0f, cosf(a), sinf(a),
                        player.Profile(), AttackStyle::Magic, q.mult, true, ctx);
        casting = was;
        if (projectiles.size() > before) {
            projectiles.back().target = targeting.Current();
            projectiles.back().life *= q.life;
        }
    }
    UpdateSlabs(dt);
    UpdateFalling(dt);
    UpdateClaws(dt);
    UpdateArcs(dt);
    UpdateNodes(dt, ctx);

    for (GroundEffect& g : ground_effects) {
        if (g.finished) continue;

        // A tornado walks, the turbulence goes where its caster goes, and a
        // whirlpool drags what is in it to the middle -- all the time, not by
        // the tick.
        if (g.Active()) {
            g.x += g.drift_x * dt;
            g.y += g.drift_y * dt;
            if (g.follows && g.from_player) { g.x = player.x; g.y = player.y; }
            if (g.pull > 0.0f)
                for (auto& e : enemies) {
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead || e->Hidden() || e->Def() == nullptr || e->Def()->is_boss) continue;
                    const SDL_FPoint at = e->GroundCentre();
                    const float dx = g.x - at.x, dy = g.y - at.y, far = Length(dx, dy);
                    if (far > g.radius + e->GroundRadius() || far < 6.0f) continue;
                    e->knock_x += dx / far * g.pull * dt * 5.0f;
                    e->knock_y += dy / far * g.pull * dt * 5.0f;
                }
        }

        if (g.delay > 0.0f) {
            g.delay -= dt;
            if (g.delay > 0.0f) continue;
        }

        g.life -= dt;
        if (g.life <= 0.0f) g.finished = true;

        bool apply = false;
        if (g.burst) {
            // An eruption hits once, the moment it goes off -- and only then.
            // What is left of its life is for show: the tick timer starts at
            // nothing, so it used to land a second time a frame later, and
            // every Meteor and Arrow Rain was two.
            apply = true;
            g.burst = false;
            g.finished = false;
            g.tick_timer = 1.0e9f;
        } else {
            g.tick_timer -= dt;
            if (g.tick_timer <= 0.0f) {
                g.tick_timer += g.tick_interval;
                apply = true;
            }
        }
        // The end of a rain is its arrows standing in the ground: nothing lands.
        if (g.rain && g.life < GroundEffect::RAIN_LINGER - 0.1f) apply = false;
        if (!apply) continue;
        if (g.rain) {
            ++g.volleys;
            Audio::PlayAt(Sfx::Impact, g.x, g.y, 0.45f, 1.30f + 0.06f * (g.volleys % 3));
            // Where this volley struck: somewhere else each time, near the middle
            // and out by the rim by turns. Always the same four points, a rain
            // looked like it was falling on four pegs.
            Burst(g.x, g.y, g.radius * (0.30f + 0.17f * static_cast<float>(g.volleys % 4)), {226, 210, 172, 255}, 4,
                  static_cast<float>(g.volleys) * 1.13f);
        }

        // A circle on the ground, against where things stand. It was a square
        // against the box a sprite fills, drawn as a flattened disc: a Meteor
        // fifty-eight across caught what stood eighty pixels south of it.
        const auto inside = [&](const Entity& who) {
            const SDL_FPoint at = who.GroundCentre();
            return CircleHits(g.x, g.y, g.radius, at.x, at.y, who.GroundRadius());
        };

        if (g.from_player) {
            ActAs(OwnerOf(g.owner_local, g.owner_seat), [&] {
                for (auto& e : enemies) {
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead || e->Hidden()) continue;
                    if (!inside(*e)) continue;
                    if (g.finished && g.once) break;      // a snare holds one thing
                    crit_next = g.sure_crit;
                    cast_next = g.cast_id;
                    proc_next = g.status;
                    if (g.hit_mult == 0.0f && g.fling > 0.0f) {
                        // It does not strike: it throws. See below.
                    } else if (g.hit_mult >= 0.0f)
                        HitEnemy(*e, g.owner, g.style, g.element, g.hit_mult, g.knockback, g.x, g.y, ctx);
                    else
                        HitEnemy(*e, g.owner, AttackStyle::Magic, g.element,
                                 static_cast<float>(g.damage) * 0.5f, 8.0f, g.x, g.y, ctx);
                    crit_next = false;
                    cast_next = 0;
                    proc_next = {};
                    // Thrown, in whatever direction the dice say, and hurt by
                    // how far: a tornado's, and the turbulence's.
                    if (g.fling > 0.0f && ctx.rng && !e->Dead()) {
                        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
                        const float a = unit(*ctx.rng) * 6.2831853f;
                        const float hard = g.fling * (0.45f + 0.55f * unit(*ctx.rng)) * (e->Def() && e->Def()->is_boss ? 0.25f : 1.0f);
                        e->knock_x += cosf(a) * hard;
                        e->knock_y += sinf(a) * hard;
                        cast_next = g.cast_id;
                        HitEnemy(*e, g.owner, g.style, g.element, g.fling_hurt * hard / 100.0f, 0.0f, g.x, g.y, ctx);
                        cast_next = 0;
                    }
                    if (g.stagger > 0.0f) e->Stagger(g.stagger);
                    if (g.once) {
                        g.finished = true;
                        AddText("Snared", e->x, e->y - 64.0f, {200, 190, 160, 255}, 1.4f);
                        SnareSprungFx(e->x, e->y, g.radius);
                    }
                }
            });
            // Take Aim is one sure shot, and a rain of them is seven: the first
            // volley has it and the rest are arrows.
            if (g.rain) g.sure_crit = false;
        } else {
            // Everyone standing in it, not only the first.
            for (Player* who : Players()) {
                if (who->IsDead() || who->puppet || who->resting || !inside(*who)) continue;
                ActAs(*who, [&] {
                    player.Damage(std::max(1, g.damage));
                    player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
                    AddText(std::to_string(g.damage), player.x, player.y - 44.0f,
                            {235, 90, 70, 255});
                });
            }
        }
    }

    ground_effects.erase(std::remove_if(ground_effects.begin(), ground_effects.end(),
                                        [](const GroundEffect& g) { return g.finished; }),
                         ground_effects.end());
}
