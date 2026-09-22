// -----------------------------------------------------------------------------
//  World, continued: what a swing, a shot, a cast and an ability do, and what a hit is
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
//  Combat resolution
// -----------------------------------------------------------------------------

// Where a shot or a cast is aimed. In a fight, at whoever the fight is with --
// the lock, or the monster targeting picked -- so nothing is aimed by hand. Out
// of one, the way the character is facing.
Vec2 World::PlayerAim() const {
    if (const Enemy* t = targeting.Current()) {
        const SDL_FPoint from = Targeting::Muzzle(player);
        const SDL_FPoint to = Targeting::AimPoint(*t);
        const float dx = to.x - from.x, dy = to.y - from.y;
        const float len = Length(dx, dy);
        if (len > 4.0f) return {dx / len, dy / len};
    }
    switch (player.facing) {
        case FACE_UP:    return {0.0f, -1.0f};
        case FACE_DOWN:  return {0.0f,  1.0f};
        case FACE_LEFT:  return {-1.0f, 0.0f};
        default:         return {1.0f,  0.0f};
    }
}

// A bow or a staff turns the same attack button into a shot or a cast. The
// swing animation and its timing are unchanged; only what leaves the character
// at the active frame is different.
void World::FirePlayerProjectile(const GameContext& ctx) {
    // Whatever is spawned between here and the way out belongs to one cast.
    struct CastScope { uint32_t& open; ~CastScope() { open = 0; } } cast_scope{casting};
    casting = 0;
    const AttackState& atk = player.Attack();
    const AttackStyle style = player.Style();
    const Vec2 aim = PlayerAim();
    const string technique = atk.type == AttackType::Charged ? player.ActiveTechnique() : string();

    string projectile_id;
    float damage_mult = atk.damage_mult * player.TalentDamage(style, atk.type);
    Element element = Element::None;
    string shape;

    if (style == AttackStyle::Ranged) {
        projectile_id = "arrow";
    } else {
        const SpellDef* spell = nullptr;
        if (ctx.spells) {
            if (player.SelectedElement() == Element::Arcane) {
                // The ancient magic: the spell chosen with 5, if it is known
                // and the Magic level is enough for it.
                spell = KnowsSpell(player.ArcaneSpell()) ? ctx.spells->Get(player.ArcaneSpell()) : nullptr;
                if (spell && spell->level > player.skills.Level(SKILL_MAGIC)) {
                    AddText("Needs Magic " + std::to_string(spell->level), player.x, player.y - 54.0f,
                            {200, 200, 210, 255});
                    Audio::Play(Sfx::UiError);
                    return;
                }
            } else {
                // The strongest the Magic level reaches, unless the
                // spellbook holds this element to a lesser one.
                spell = player.SpellOf(player.SelectedElement(), *ctx.spells);
            }
        }
        if (!spell) {
            AddText("No spell known", player.x, player.y - 54.0f, {200, 200, 210, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        // Techniques and combos cost more than a single bolt; the tree takes
        // a share off.
        const float technique_cost = technique == "meteor" ? 3.0f : technique.empty() ? 1.0f : 2.0f;
        const float combo_cost = atk.move == ComboMove::Crush ? 1.5f : atk.move == ComboMove::Cleave ? 1.6f
                               : atk.move == ComboMove::CrossCut ? 2.0f : 1.0f;
        // Overloaded: this one is already paid for.
        const int cost = atk.empowered ? 0 : std::max(1, static_cast<int>(std::lround(
            spell->mana * technique_cost * combo_cost *
            (player.equipment.Weapon() ? player.equipment.Weapon()->mana_mult : 1.0f) *
            std::max(0.1f, 1.0f - player.talents.Effect("mana_cost", AttackStyle::Magic)))));
        if (!player.SpendMana(cost)) {
            AddText("Out of mana", player.x, player.y - 54.0f, {150, 180, 235, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        projectile_id = spell->projectile;
        damage_mult *= spell->damage_mult;
        element = spell->element;
        player.NoteCast(spell->element);      // Attunement: the same element, again
        shape = spell->shape;
        // The spell's own experience is owed, not paid: it comes when the
        // spell lands on something. See OpenCast in world.h for why.
        casting = OpenCast(SKILL_MAGIC, spell->xp);
    }

    if (style == AttackStyle::Ranged) {
        const ItemDef* held = player.equipment.Weapon();
        Audio::Play(held && held->thrown ? Sfx::KnifeThrow : Sfx::BowShot);
    } else {
        // Each element is pitched a little differently.
        const Element el = player.SelectedElement();
        const float pitch = el == Element::Fire ? 0.9f : el == Element::Water ? 1.1f
                          : el == Element::Earth ? 0.75f : el == Element::Arcane ? 0.6f : 1.25f;
        Audio::Play(Sfx::SpellCast, 1.0f, pitch);
    }

    // Let go with a breath held, or overloaded: the whole of what comes out
    // hits harder, and an aimed shot strikes critically whatever the dice say.
    const bool aimed_shot = atk.empowered && style == AttackStyle::Ranged;
    if (atk.empowered) {
        damage_mult *= aimed_shot ? Player::AIM_DAMAGE : Player::OVERLOAD_DAMAGE;
        AddText(aimed_shot ? "Aimed" : "Overload", player.x, player.y - 58.0f,
                aimed_shot ? SDL_Color{255, 232, 150, 255} : SDL_Color{190, 170, 255, 255}, 0.9f);
    }

    const SDL_FPoint muzzle = Targeting::Muzzle(player);
    const Enemy* target = targeting.Current();
    // What is in the hand has its say: a crossbow throws bolts and a fan of
    // knives knives; a wand's casts are worth less than a staff's and an orb's
    // turn after what they are thrown at.
    const ItemDef* in_hand = player.equipment.Weapon();
    if (in_hand) {
        damage_mult *= in_hand->damage;
        if (style == AttackStyle::Ranged && !in_hand->shoots.empty() && ctx.projectiles && ctx.projectiles->Has(in_hand->shoots))
            projectile_id = in_hand->shoots;
    }

    // One shot along a direction, with the talents' changes applied to it.
    const auto loose = [&](float dx, float dy, float mult, bool aimed) -> Projectile* {
        const size_t before = projectiles.size();
        SpawnProjectile(projectile_id, muzzle.x + dx * 12.0f, muzzle.y + dy * 12.0f,
                        dx, dy, player.Profile(), style, mult, true, ctx);
        if (projectiles.size() == before) return nullptr;
        Projectile& p = projectiles.back();
        if (aimed) p.target = target;
        p.sure_crit = aimed_shot;
        p.knockback_mult = 1.0f + player.talents.Effect("knockback", style);
        p.extra_homing = player.talents.Effect("homing", style) + (in_hand ? in_hand->homing : 0.0f);
        if (style == AttackStyle::Ranged) {
            p.pierce_left += static_cast<int>(player.talents.Effect("pierce", style));
            const float faster = 1.0f + player.talents.Effect("projectile_speed", style);
            p.vx *= faster;
            p.vy *= faster;
        }
        return &p;
    };
    const auto turned = [&](float degrees) {
        const float a = atan2f(aim.y, aim.x) + degrees * 3.14159265f / 180.0f;
        return Vec2{cosf(a), sinf(a)};
    };
    // Where a strike from above lands: the target, or a little way ahead.
    const auto strike_point = [&]() {
        if (target) return Targeting::AimPoint(*target);
        return SDL_FPoint{player.x + aim.x * 110.0f, player.y + aim.y * 110.0f};
    };
    const auto strike = [&](float radius, float delay, float mult, Element el, bool quiet = false) {
        const SDL_FPoint at = strike_point();
        GroundEffect g;
        g.quiet = quiet;
        g.x = at.x;
        g.y = at.y + 8.0f;
        g.radius = radius;
        g.delay = delay;
        g.life = g.max_life = 0.35f;
        g.burst = true;
        g.from_player = true;
        g.owner = player.Profile();
        g.element = el;
        g.style = style;
        g.hit_mult = mult;
        g.knockback = 60.0f;
        g.sure_crit = aimed_shot;
        AddGroundEffect(g);
    };

    // --- the combos, at range ---------------------------------------------------
    // The same grammar as the sword's, with the weapon's own move at the end
    // of it. The profile's damage number is the sword's; a shot's worth is
    // what a plain one would be, times the move's own.
    if (atk.move != ComboMove::None) {
        AddText(player.ComboLabel(atk.move), player.x, player.y - 58.0f, {255, 232, 150, 255}, 0.8f);
        const float base = damage_mult / std::max(0.01f, atk.damage_mult);
        if (style == AttackStyle::Ranged) {
            switch (atk.move) {
                case ComboMove::Crush:      // Split Shot: three arrows in a narrow fan
                    for (float deg : {-9.0f, 0.0f, 9.0f}) {
                        const Vec2 d = turned(deg);
                        loose(d.x, d.y, base * 0.7f, deg == 0.0f);
                    }
                    break;
                case ComboMove::Cleave:     // Barbed Shot: one heavy arrow that passes through and throws
                    if (Projectile* p = loose(aim.x, aim.y, base * 1.6f, true)) {
                        p->pierce_left += 2;
                        p->knockback_mult *= 1.8f;
                        p->vx *= 1.3f;
                        p->vy *= 1.3f;
                    }
                    break;
                case ComboMove::Backhand:   // Snap Shot: a quick arrow, as good as a drawn one
                    loose(aim.x, aim.y, base, true);
                    break;
                case ComboMove::CrossCut:   // Twin Shot: two arrows at once
                    for (float deg : {-3.0f, 3.0f}) {
                        const Vec2 d = turned(deg);
                        loose(d.x, d.y, base * 0.9f, true);
                    }
                    break;
                default: break;
            }
        } else {
            switch (atk.move) {
                case ComboMove::Crush:      // Surge: one bolt, bigger and harder
                    if (Projectile* p = loose(aim.x, aim.y, base * 1.6f, true)) {
                        p->knockback_mult *= 1.6f;
                        p->life *= 1.2f;
                    }
                    break;
                case ComboMove::Cleave:     // Cascade: three bolts in a fan
                    for (float deg : {-14.0f, 0.0f, 14.0f}) {
                        const Vec2 d = turned(deg);
                        loose(d.x, d.y, base * 0.8f, deg == 0.0f);
                    }
                    break;
                case ComboMove::Backhand:   // Flicker: a quick bolt
                    loose(aim.x, aim.y, base, true);
                    break;
                case ComboMove::CrossCut:   // Pulse: a ring of six
                    for (int i = 0; i < 6; ++i) {
                        const float a = 6.2831853f * i / 6.0f;
                        loose(cosf(a), sinf(a), base * 0.5f, false);
                    }
                    break;
                default: break;
            }
        }
        return;
    }

    // Whatever is let off next, a crossbow has to be spanned again after it.
    struct Respan { Player& who; ~Respan() { who.StartReload(); } } respan{player};
    const bool fan = in_hand && in_hand->weapon_class == "knives" && atk.type != AttackType::Light &&
                     atk.move == ComboMove::None;
    if (fan) {
        // A heavy throw is three at once, and a charged one throws them harder.
        for (float deg : {-12.0f, 0.0f, 12.0f}) {
            const Vec2 d = turned(deg);
            loose(d.x, d.y, damage_mult * 0.7f, deg == 0.0f);
        }
    } else if (technique == "volley") {
        for (float deg : {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f}) {
            const Vec2 d = turned(deg);
            loose(d.x, d.y, damage_mult * 0.65f, deg == 0.0f);
        }
    } else if (technique == "piercing_shot") {
        if (Projectile* p = loose(aim.x, aim.y, damage_mult * 1.35f, true)) {
            p->pierce_left += 8;
            p->vx *= 1.6f;
            p->vy *= 1.6f;
            p->knockback_mult *= 1.5f;
            p->life *= 1.3f;
        }
    } else if (technique == "arrow_rain") {
        // Not one strike: a rain. It comes down on the circle for two seconds
        // and more, a volley every tick, each its own roll to hit on whatever
        // is under it then -- so something that walks out gets out, and
        // something that walks in gets wet.
        const SDL_FPoint at = strike_point();
        GroundEffect g;
        g.x = at.x;
        g.y = at.y + 8.0f;
        g.radius = GroundEffect::RAIN_RADIUS;
        g.delay = 0.35f;
        g.life = g.max_life = GroundEffect::RAIN_TIME + GroundEffect::RAIN_LINGER;
        g.tick_interval = GroundEffect::RAIN_EVERY;
        g.tick_timer = 0.0f;                  // the first volley lands as the telegraph closes
        g.rain = true;
        g.from_player = true;
        g.owner = player.Profile();
        g.element = Element::None;
        g.style = style;
        g.hit_mult = damage_mult * GroundEffect::RAIN_SHARE;
        // Arrows pin; they do not throw. A shove from the middle would push
        // everything out of the rain on the first volley.
        g.knockback = 4.0f;
        g.stagger = 0.10f;
        g.sure_crit = aimed_shot;
        AddGroundEffect(g);
    } else if (technique == "nova") {
        for (int i = 0; i < 8; ++i) {
            const float a = 6.2831853f * i / 8.0f;
            loose(cosf(a), sinf(a), damage_mult * 0.6f, false);
        }
    } else if (technique == "barrage") {
        for (float deg : {-14.0f, -5.0f, 5.0f, 14.0f})
            if (Projectile* p = loose(turned(deg).x, turned(deg).y, damage_mult * 0.5f, true))
                p->extra_homing += 4.0f;
    } else if (technique == "meteor") {
        // An actual meteor, as wide as the ground it covers, falling for as
        // long as the strike takes to arm.
        const float radius = 58.0f, wait = 0.6f;
        const SDL_FPoint at = strike_point();
        strike(radius, wait, damage_mult * 1.5f, element);
        AddFalling(at.x, at.y + 8.0f, radius * 2.0f, element, wait, LiftAt(at.x, at.y));
    // --- the ancient spells' shapes ----------------------------------------------
    } else if (shape == "darts") {
        // Three that seek: the old missile that does not miss.
        for (float deg : {-8.0f, 0.0f, 8.0f}) {
            const Vec2 d = turned(deg);
            if (Projectile* p = loose(d.x, d.y, damage_mult, true)) p->extra_homing += 6.0f;
        }
    } else if (shape == "claw") {
        // A claw conjured at the hand and raked across whatever is in front of
        // it. It was a bolt thrown a hand's reach and gone; the reach is the
        // same, but nothing leaves the hand now. What the bolt carried -- the
        // Vampiric Touch's leeching, the Ice Touch's chill -- is carried here.
        const ProjectileDef* def = ctx.projectiles ? ctx.projectiles->Get(projectile_id) : nullptr;
        const float reach = def ? std::max(40.0f, def->speed * def->life) : 88.0f;
        AttackProfile swipe;
        swipe.reach = reach; swipe.width = 44.0f; swipe.sweep_deg = 62.0f;
        const SDL_FPoint from = player.GroundCentre();
        const StrikeArc arc = ArcFor(from.x, from.y, player.facing, swipe, 1.0f);
        for (auto& e : enemies) {
            if (!Strikeable(*e)) continue;
            const SDL_FPoint a = e->GroundCentre();
            if (!ArcHits(arc, a.x, a.y, e->GroundRadius())) continue;
            if (def) { proc_next = def->status; leech_next = def->leech; }
            cast_next = casting;
            HitEnemy(*e, player.Profile(), style, element, damage_mult, def ? def->knockback : 10.0f,
                     player.x, player.y, ctx);
            proc_next = {};
            leech_next = 0.0f;
            cast_next = 0;
        }
        // Ice talons or a thing of flesh and blood: told apart by what the spell
        // leaves on whatever it rakes.
        const uint8_t look = (def && def->status.kind == Status::Chill) ? 1 : 0;
        AddClaw(player.x, player.y - 14.0f, atan2f(aim.y, aim.x), reach, look, player.draw_lift);
        Audio::PlayAt(look ? Sfx::Swing : Sfx::SwingHeavy, player.x, player.y, 0.9f, look ? 1.25f : 0.85f);
    } else if (shape == "blades") {
        // The Hail of Blades: the tornado's turning column, with a conjured
        // blade on every ring of it. One hit when they arrive, as it always was.
        const SDL_FPoint at = strike_point();
        GroundEffect g;
        g.x = at.x; g.y = at.y + 8.0f;
        g.radius = 56.0f;
        g.delay = 0.35f;
        g.life = g.max_life = 1.15f;              // long enough to be seen turning
        g.burst = true;
        g.from_player = true;
        g.owner = player.Profile();
        g.element = element;
        g.style = style;
        g.hit_mult = damage_mult;
        g.knockback = 45.0f;
        g.sure_crit = aimed_shot;
        g.draw = GroundEffect::Draw::Blades;
        AddGroundEffect(g);
    } else if (shape == "rays") {
        for (float deg : {-12.0f, 0.0f, 12.0f}) {
            const Vec2 d = turned(deg);
            loose(d.x, d.y, damage_mult, deg == 0.0f);
        }
    } else if (shape == "rain") {
        strike(56.0f, 0.45f, damage_mult, element);
    } else if (shape == "ring") {
        for (int i = 0; i < 8; ++i) {
            const float a = 6.2831853f * i / 8.0f;
            if (Projectile* p = loose(cosf(a), sinf(a), damage_mult, false)) p->knockback_mult *= 2.0f;
        }
    } else if (shape == "spray") {
        // Five gouts in a fan, at arm's length and a little more.
        for (float deg : {-26.0f, -13.0f, 0.0f, 13.0f, 26.0f}) {
            const Vec2 d = turned(deg);
            loose(d.x, d.y, damage_mult, false);
        }
    } else if (shape == "rebuke") {
        // Fire where the target stands, at once -- and half as much again if
        // it is an answer: if the caster has been hurt in the last few seconds.
        const bool answer = player.SinceHurt() <= Player::REBUKE_WINDOW;
        if (answer) AddText("Rebuke!", player.x, player.y - 58.0f, {255, 150, 80, 255}, 0.8f);
        const size_t before = ground_effects.size();
        strike(36.0f, 0.12f, damage_mult * (answer ? Player::REBUKE_DAMAGE : 1.0f), element);
        if (ground_effects.size() > before) {
            GroundEffect& g = ground_effects.back();
            g.look = Element::Fire;
            if (const ProjectileDef* d = ctx.projectiles ? ctx.projectiles->Get(projectile_id) : nullptr) g.status = d->status;
        }
    } else if (shape == "cone") {
        // The Flamethrower. Light is wide and at arm's length: five tongues
        // across sixty degrees that are gone in a quarter of a second. Heavy is
        // three, close together, that live three times as long -- a lance of it.
        //
        // And behind each, the rest of the breath: a second flight a moment
        // later, in the gaps of the first. It is what makes it a jet of fire
        // and not five darts -- and it is the same fire, shared out: what the
        // two flights are worth together is what the one was.
        const bool focused = atk.type != AttackType::Light;
        if (focused) {
            for (float deg : {-5.0f, 0.0f, 5.0f})
                if (Projectile* p = loose(turned(deg).x, turned(deg).y, damage_mult * 1.25f * 0.7f, deg == 0.0f)) p->life *= 2.9f;
            for (float deg : {-2.5f, 2.5f})
                queued_shots.push_back({0.08f, projectile_id, damage_mult * 1.25f * 0.45f, deg, casting, 2.9f});
        } else {
            for (float deg : {-30.0f, -15.0f, 0.0f, 15.0f, 30.0f}) loose(turned(deg).x, turned(deg).y, damage_mult * 0.64f, false);
            for (float deg : {-22.5f, -7.5f, 7.5f, 22.5f})
                queued_shots.push_back({0.07f, projectile_id, damage_mult * 0.45f, deg, casting, 1.0f});
        }
    } else if (shape == "fire_ring" || shape == "wall") {
        // Burning ground, laid out: a ring round the caster, or a wall across
        // the way they face, a little way off (or where the target stands).
        const bool ring = shape == "fire_ring";
        const int   count = ring ? 12 : 7;
        const float life = ring ? 3.5f : 5.0f;
        const SDL_FPoint middle = ring ? SDL_FPoint{player.x, player.y - 4.0f}
                                : target ? strike_point() : SDL_FPoint{player.x + aim.x * 92.0f, player.y + aim.y * 92.0f};
        const ProjectileDef* bolt = ctx.projectiles ? ctx.projectiles->Get(projectile_id) : nullptr;
        for (int i = 0; i < count; ++i) {
            GroundEffect g;
            if (ring) {
                const float a = 6.2831853f * i / count;
                g.x = middle.x + cosf(a) * 66.0f; g.y = middle.y + sinf(a) * 66.0f;
            } else {
                const float along = (i - (count - 1) / 2.0f) * 26.0f;
                g.x = middle.x - aim.y * along; g.y = middle.y + aim.x * along;
            }
            g.radius = ring ? 20.0f : 18.0f;
            g.life = g.max_life = life;
            g.tick_interval = 0.5f;
            g.tick_timer = 0.05f * i;                 // not all on the same frame
            g.from_player = true;
            g.owner = player.Profile();
            g.element = element;
            g.style = style;
            g.hit_mult = damage_mult * 0.45f;
            g.knockback = 6.0f;
            if (bolt) { g.status = bolt->status; g.status.chance *= 0.5f; }
            AddGroundEffect(g);
        }
        Burst(middle.x, middle.y, ring ? 66.0f : 40.0f, ElementColor(element), ring ? 16 : 8);
    } else if (shape == "wave") {
        // Seven abreast, rolling out together.
        for (int i = -3; i <= 3; ++i) {
            const size_t before = projectiles.size();
            SpawnProjectile(projectile_id, muzzle.x + aim.x * 10.0f - aim.y * i * 13.0f, muzzle.y + aim.y * 10.0f + aim.x * i * 13.0f,
                            aim.x, aim.y, player.Profile(), style, damage_mult, true, ctx);
            if (projectiles.size() > before) projectiles.back().knockback_mult = 1.0f + player.talents.Effect("knockback", style);
        }
    } else if (shape == "whirlpool" || shape == "turbulence" || shape == "tornado") {
        GroundEffect g;
        const bool held = atk.type != AttackType::Light;
        g.from_player = true;
        g.owner = player.Profile();
        g.element = element;
        g.style = style;
        g.tick_interval = 0.4f;
        g.tick_timer = 0.15f;
        if (shape == "whirlpool") {
            const SDL_FPoint at = strike_point();
            g.x = at.x; g.y = at.y + 6.0f;
            g.radius = 72.0f;
            g.life = g.max_life = 4.0f;
            g.pull = 120.0f;
            g.hit_mult = damage_mult * 0.5f;
            g.knockback = 0.0f;
            g.draw = GroundEffect::Draw::Whirlpool;
            if (const ProjectileDef* bolt = ctx.projectiles ? ctx.projectiles->Get(projectile_id) : nullptr) g.status = bolt->status;
        } else if (shape == "tornado") {
            // A light cast is a dust devil; held and let go it is the whole
            // four seconds, and it walks the way it was sent.
            g.x = player.x + aim.x * 40.0f; g.y = player.y + aim.y * 40.0f;
            g.radius = held ? 46.0f : 34.0f;
            g.life = g.max_life = held ? 4.0f : 1.4f;
            g.drift_x = aim.x * 46.0f; g.drift_y = aim.y * 46.0f;
            g.fling = held ? 300.0f : 220.0f;
            g.fling_hurt = damage_mult * 0.55f;
            g.hit_mult = 0.0f;
            g.draw = GroundEffect::Draw::Tornado;
        } else {
            g.x = player.x; g.y = player.y;
            g.radius = 92.0f;
            g.life = g.max_life = 3.0f;
            g.tick_interval = 0.3f;
            g.fling = 200.0f;
            g.fling_hurt = damage_mult * 0.5f;
            g.hit_mult = 0.0f;
            g.follows = true;
            g.draw = GroundEffect::Draw::Turbulence;
        }
        AddGroundEffect(g);
    } else if (shape == "slab") {
        // The Slabstrike. A square of the ground is torn up and swung through
        // whatever is in front: five pixels of it on a light, eight on a heavy
        // -- the same swing, but the heavy's own slowness carries it. Held and
        // let go, the big one is carried over whoever is being fought instead
        // and dropped on them, and breaks on top of them.
        const ProjectileDef* stone = ctx.projectiles ? ctx.projectiles->Get(projectile_id) : nullptr;
        const StatusProc leaves = stone ? StatusProc{stone->status.kind, 0.4f} : StatusProc{};
        if (atk.type == AttackType::Charged) {
            const SDL_FPoint at = strike_point();
            proc_next = leaves;
            strike(26.0f, World::SLAB_DROP_TIME * World::SLAB_DROP_FALL, damage_mult * 1.35f, element, true);
            proc_next = {};
            AddSlabDrop(at.x, at.y + 8.0f, World::SLAB_HEAVY, LiftAt(at.x, at.y));
            Audio::PlayAt(Sfx::SwingHeavy, player.x, player.y, 1.0f, 0.62f);
        } else {
            const bool heavy = atk.type != AttackType::Light;
            const float side = heavy ? World::SLAB_HEAVY : World::SLAB_LIGHT;
            AttackProfile swing;
            swing.reach = heavy ? 84.0f : 70.0f;
            swing.width = heavy ? 60.0f : 44.0f;
            swing.sweep_deg = heavy ? 62.0f : 54.0f;
            const SDL_FPoint from = player.GroundCentre();
            const StrikeArc arc = ArcFor(from.x, from.y, player.facing, swing, 1.0f);
            for (auto& e : enemies) {
                if (!Strikeable(*e)) continue;
                const SDL_FPoint a = e->GroundCentre();
                if (!ArcHits(arc, a.x, a.y, e->GroundRadius())) continue;
                proc_next = leaves;
                cast_next = casting;
                HitEnemy(*e, player.Profile(), style, element, damage_mult, heavy ? 230.0f : 150.0f,
                         player.x, player.y, ctx);
                proc_next = {};
                cast_next = 0;
            }
            // It rides round the middle of what it strikes, not the far edge:
            // out at the rim it reads as a rock flying past rather than as
            // something swung at what is in front of you.
            AddSlabSwing(player.x, player.y - 14.0f, atan2f(aim.y, aim.x), swing.reach * 0.62f, side, player.draw_lift);
            Audio::PlayAt(Sfx::SwingHeavy, player.x, player.y, 1.0f, heavy ? 0.7f : 0.95f);
        }
    } else if (shape == "stone_rain") {
        // The Arrow Rain's numbers, in stone: see GroundEffect::RAIN_TIME.
        const SDL_FPoint at = strike_point();
        GroundEffect g;
        g.x = at.x; g.y = at.y + 8.0f;
        g.radius = GroundEffect::RAIN_RADIUS;
        g.delay = 0.35f;
        g.life = g.max_life = GroundEffect::RAIN_TIME + GroundEffect::RAIN_LINGER;
        g.tick_interval = GroundEffect::RAIN_EVERY;
        g.tick_timer = 0.0f;
        g.rain = true;
        g.from_player = true;
        g.owner = player.Profile();
        g.element = element;
        g.style = style;
        g.hit_mult = damage_mult * GroundEffect::RAIN_SHARE * 1.15f;
        g.knockback = 4.0f;
        g.stagger = 0.10f;
        g.status = {Status::Concussed, 0.12f};
        AddGroundEffect(g);
    } else if (shape == "burst") {
        // Eight, one after another: the first now and the rest owed.
        loose(aim.x, aim.y, damage_mult, true);
        for (int i = 1; i < 8; ++i) queued_shots.push_back({0.07f * i, projectile_id, damage_mult, (i % 2 ? 1.0f : -1.0f) * 2.5f * ((i + 1) / 2), casting});
    } else {
        loose(aim.x, aim.y, damage_mult, true);
        // Spell Echo: a plain bolt is sometimes followed by a second, for
        // nothing, a little off the line of the first.
        const float echo = style == AttackStyle::Magic ? player.talents.Effect("echo", style) : 0.0f;
        if (echo > 0.0f && ctx.rng && std::uniform_real_distribution<float>(0.0f, 1.0f)(*ctx.rng) < echo) {
            const Vec2 d = turned(7.0f);
            loose(d.x, d.y, damage_mult, true);
            AddText("Echo", player.x, player.y - 58.0f, {190, 170, 255, 255}, 0.7f);
        }
    }
}

vector<string> World::KnownArcane(const SpellBook& book) const {
    vector<string> out;
    for (const SpellDef* s : book.Arcane())
        if (KnowsSpell(s->id)) out.push_back(s->id);
    return out;
}

void World::Burst(float x, float y, float radius, SDL_Color color, int count, float turn) {
    for (int i = 0; i < count; ++i) {
        const float a = 6.2831853f * i / count + turn;
        Impact im;
        im.x = x + cosf(a) * radius;
        im.y = y + sinf(a) * radius;
        im.nx = cosf(a);
        im.ny = sinf(a);
        im.radius = 4.0f;
        im.max_life = 0.3f;
        im.life = im.max_life;
        im.color = color;
        impacts.push_back(im);
        if (!map.IsInterior() && i % 2 == 0) AddDust(im.x, im.y + 4.0f, -cosf(a), -sinf(a));
    }
}

bool World::Strikeable(const Enemy& e) const {
    if (e.Dead() || e.CurrentState() == Enemy::State::Dead) return false;
    return std::abs(map.LevelAt(e.x, e.y) - map.LevelAt(player.x, player.y)) <= 1;
}

int World::HitAround(float radius, float damage_mult, float knockback, const GameContext& ctx) {
    // A circle on the ground round the player's feet. It used to be measured
    // to the middle of the monster's body, which is half its height north of
    // where it stands, so a turn reached further south than north.
    const SDL_FPoint c = player.GroundCentre();
    int struck = 0;
    for (auto& e : enemies) {
        if (!Strikeable(*e)) continue;
        const SDL_FPoint a = e->GroundCentre();
        if (!CircleHits(c.x, c.y, radius, a.x, a.y, e->GroundRadius())) continue;
        // A turn on the spot is whatever swing the player is in the middle of:
        // a charged technique, or the Cross Cut, which is a heavy one.
        HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None, damage_mult,
                 knockback, player.x, player.y, ctx, player.Attack().type);
        ++struck;
    }
    return struck;
}

string World::SwingLabel(const GameContext& ctx) const {
    const AttackState& atk = player.Attack();
    if (atk.move != ComboMove::None) return player.ComboLabel(atk.move);
    if (atk.type == AttackType::Light) return player.Rushing() ? "Rushing Strike" : "Light";
    if (atk.type == AttackType::Strong) return "Strong";
    if (atk.type == AttackType::Charged) {
        const TalentNode* t = (ctx.trees && !player.ActiveTechnique().empty())
                                  ? ctx.trees->Find(player.ActiveTechnique()) : nullptr;
        return t ? t->name : "Charged";
    }
    return "";
}

bool World::MeleeTechnique(const string& technique, const GameContext& ctx) {
    const AttackState& atk = player.Attack();
    const float mult = atk.damage_mult * player.TalentDamage(AttackStyle::Melee, atk.type);
    const float knock = 1.0f + player.talents.Effect("knockback", AttackStyle::Melee);
    int struck = 0;
    const auto hit_round = [&](float radius, float damage, float knockback) {
        struck += HitAround(radius, damage, knockback * knock, ctx);
        return struck > 0;
    };
    // The chain counter, for whichever technique this turns out to be. Set
    // on every way out below, once the technique has struck or not.
    struct Count {
        World& w; const GameContext& c; int& n; bool handled = false;
        ~Count() { if (handled) { if (n > 0) w.player.CountChainHit(w.SwingLabel(c)); else w.player.BreakChain(); } }
    } count{*this, ctx, struck};

    if (technique == "whirlwind") {
        count.handled = true;
        const float radius = 42.0f * atk.reach_scale;
        hit_round(radius, mult * 0.9f, atk.profile.knockback);
        Burst(player.x, player.y - 10.0f, radius, {236, 236, 255, 255}, 10);
        Audio::Play(Sfx::SwingHeavy, 1.0f, 1.25f);
        return true;
    }
    if (technique == "ground_slam") {
        count.handled = true;
        const float radius = 58.0f * atk.reach_scale;
        hit_round(radius, mult * 0.8f, 170.0f);
        Burst(player.x, player.y, radius, {214, 180, 120, 255}, 14);
        Audio::Play(Sfx::Impact, 1.0f, 0.6f);
        return true;
    }
    if (technique == "lunge") {
        count.handled = true;
        // A burst of speed along the facing, riding the knockback the player
        // already slides on, and a long strike down the path it covers.
        const float fx = player.facing == FACE_LEFT ? -1.0f : player.facing == FACE_RIGHT ? 1.0f : 0.0f;
        const float fy = player.facing == FACE_UP   ? -1.0f : player.facing == FACE_DOWN  ? 1.0f : 0.0f;
        player.knock_x += fx * 560.0f;
        player.knock_y += fy * 560.0f;
        AttackProfile long_reach = atk.profile;
        long_reach.reach = 82.0f;
        long_reach.width = atk.profile.width + 10.0f;
        const SDL_FPoint from = player.GroundCentre();
        const StrikeArc hit = ArcFor(from.x, from.y, player.facing, long_reach, 1.0f);
        for (auto& e : enemies) {
            if (!Strikeable(*e)) continue;
            const SDL_FPoint a = e->GroundCentre();
            if (!ArcHits(hit, a.x, a.y, e->GroundRadius())) continue;
            HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None, mult,
                     atk.profile.knockback * knock, player.x, player.y, ctx, atk.type);
            ++struck;
        }
        for (int i = 0; i < 4; ++i) AddDust(player.x - fx * i * 8.0f, player.y - fy * i * 8.0f, fx, fy);
        Audio::Play(Sfx::SwingHeavy, 1.0f, 1.1f);
        return true;
    }
    return false;
}

void World::ApplyPlayerAbility(const GameContext& ctx) {
    const string ability = player.TakeAbility();
    if (ability.empty()) return;
    const float px = player.x, py = player.y;
    const auto say = [&](const string& text, SDL_Color c) { AddText(text, px, py - 60.0f, c, 1.2f); };

    if (ability == "sunder" || ability == "hunters_mark") {
        // On what is being fought; failing that, the nearest thing in reach.
        Enemy* target = targeting.Current();
        const float reach = ability == "sunder" ? 78.0f : 520.0f;
        if (!target || Length(target->x - px, target->y - py) > reach) {
            target = nullptr;
            float best = reach;
            for (auto& e : enemies) {
                if (!Targeting::Targetable(*e)) continue;
                const float d = Length(e->x - px, e->y - py);
                if (d < best) { best = d; target = e.get(); }
            }
        }
        if (!target) { say("Nothing in reach", {200, 200, 210, 255}); return; }
        if (ability == "sunder") {
            target->Sunder(10.0f);
            HitEnemy(*target, player.Profile(), AttackStyle::Melee, Element::None,
                     1.5f * player.TalentDamage(AttackStyle::Melee, AttackType::Strong), 70.0f, px, py, ctx,
                     AttackType::Strong);
            AddText("Sundered", target->x, target->y - 64.0f, {255, 190, 110, 255}, 1.4f);
            Burst(target->x, target->y - 16.0f, 26.0f, {255, 190, 110, 255}, 10);
        } else {
            target->Mark(12.0f);
            target->RevealHealthBar();
            AddText("Marked", target->x, target->y - 64.0f, {255, 120, 120, 255}, 1.4f);
            Burst(target->x, target->y - 16.0f, 30.0f, {255, 120, 120, 255}, 12);
        }
    } else if (ability == "war_cry") {
        say("War Cry!", {255, 210, 120, 255});
        Burst(px, py - 16.0f, 96.0f, {255, 210, 120, 255}, 22);
        for (auto& e : enemies)
            if (Targeting::Targetable(*e) && Length(e->x - px, e->y - py) < 120.0f) e->Stagger(0.8f);
    } else if (ability == "bash") {
        int struck = 0;
        for (auto& e : enemies) {
            if (!Targeting::Targetable(*e)) continue;
            const float dx = e->x - px, dy = e->y - py;
            if (Length(dx, dy) > 60.0f || !InFrontOf(player.facing, dx, dy)) continue;
            HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None,
                     0.6f * player.TalentDamage(AttackStyle::Melee, AttackType::Light), 110.0f, px, py, ctx,
                     AttackType::Light);
            e->Stagger(1.2f);
            ++struck;
        }
        Burst(px, py - 16.0f, 34.0f, {230, 230, 240, 255}, struck > 0 ? 12 : 5);
    } else if (ability == "caltrops") {
        GroundEffect g;
        g.x = px; g.y = py;
        g.radius = 46.0f;
        g.life = g.max_life = 6.0f;
        g.tick_interval = 0.5f;
        g.damage = std::max(1, player.skills.Level(SKILL_RANGED) / 8);
        g.element = Element::Earth;
        g.owner = player.Profile();
        g.from_player = true;
        g.style = AttackStyle::Ranged;
        g.hit_mult = 0.3f * player.TalentDamage(AttackStyle::Ranged, AttackType::Light);
        g.knockback = 0.0f;
        g.stagger = 0.45f;
        AddGroundEffect(g);
        say("Caltrops", {200, 190, 160, 255});
    } else if (ability == "arcane_pulse") {
        // Ten bolts of the chosen element, in a ring.
        const SpellDef* spell = ctx.spells
            ? ctx.spells->BestFor(player.SelectedElement() == Element::Arcane ? Element::Fire : player.SelectedElement(),
                                  player.skills.Level(SKILL_MAGIC))
            : nullptr;
        if (!spell) return;
        const float mult = 0.7f * spell->damage_mult * player.TalentDamage(AttackStyle::Magic, AttackType::Light);
        for (int i = 0; i < 10; ++i) {
            const float a = 6.2831853f * (static_cast<float>(i) / 10.0f);
            SpawnProjectile(spell->projectile, px, py - 14.0f, cosf(a), sinf(a), player.Profile(),
                            AttackStyle::Magic, mult, true, ctx);
        }
        player.NoteCast(spell->element);
    } else if (ability == "blink") {
        Burst(px, py - 16.0f, 30.0f, {190, 170, 255, 255}, 14);
    } else if (ability == "tumble") {
        if (!map.IsInterior()) AddDust(px, py, -player.knock_x, -player.knock_y);
    } else if (ability == "mana_shield") {
        say("Mana Shield", {130, 170, 255, 255});
        Burst(px, py - 16.0f, 36.0f, {130, 170, 255, 255}, 16);
    } else if (ability == "frenzy") {
        say("Frenzy!", {255, 150, 110, 255});
        Burst(px, py - 16.0f, 34.0f, {255, 150, 110, 255}, 14);
    } else if (ability == "shockwave") {
        // A corridor straight ahead, as wide as a swing and three times as long.
        const float fx = player.facing == FACE_LEFT ? -1.0f : player.facing == FACE_RIGHT ? 1.0f : 0.0f;
        const float fy = player.facing == FACE_UP ? -1.0f : player.facing == FACE_DOWN ? 1.0f : 0.0f;
        constexpr float LENGTH = 190.0f, HALF_WIDTH = 38.0f;
        const float mult = 1.3f * player.TalentDamage(AttackStyle::Melee, AttackType::Strong);
        for (auto& e : enemies) {
            if (!Targeting::Targetable(*e)) continue;
            const float dx = e->x - px, dy = e->y - py;
            const float along = dx * fx + dy * fy;
            const float across = fabsf(dx * fy - dy * fx);
            if (along < 0.0f || along > LENGTH || across > HALF_WIDTH) continue;
            // The blow can miss; the ground going out from under it cannot.
            HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None, mult, 0.0f, px, py, ctx,
                     AttackType::Strong);
            e->knock_x += fx * 240.0f;
            e->knock_y += fy * 240.0f;
            e->Stagger(0.7f);
        }
        for (int i = 1; i <= 5; ++i) {
            const float d = LENGTH * static_cast<float>(i) / 5.0f;
            Burst(px + fx * d, py + fy * d, 22.0f, {225, 205, 170, 255}, 6);
            if (!map.IsInterior()) AddDust(px + fx * d, py + fy * d, fx, fy);
        }
        Audio::PlayAt(Sfx::SwingHeavy, px, py, 1.0f, 0.7f);
    } else if (ability == "stand_fast") {
        say("Stand Fast", {170, 200, 240, 255});
        Burst(px, py - 16.0f, 40.0f, {170, 200, 240, 255}, 16);
        // Everything near turns on whoever set their feet, and leaves their
        // friends alone for as long as it lasts.
        for (auto& e : enemies)
            if (Targeting::Targetable(*e) && Length(e->x - px, e->y - py) < 260.0f)
                e->Taunt(static_cast<int>(player.seat), Player::STAND_FAST_TIME);
    } else if (ability == "take_aim") {
        say("Take Aim", {255, 232, 150, 255});
    } else if (ability == "rapid_fire") {
        say("Rapid Fire", {190, 230, 190, 255});
        Burst(px, py - 16.0f, 30.0f, {190, 230, 190, 255}, 12);
    } else if (ability == "snare") {
        GroundEffect g;
        g.x = px; g.y = py;
        g.radius = 22.0f;
        g.life = g.max_life = 20.0f;
        g.tick_interval = 0.1f;
        g.damage = 1;
        g.element = Element::Earth;
        g.owner = player.Profile();
        g.from_player = true;
        g.style = AttackStyle::Ranged;
        g.hit_mult = 1.2f * player.TalentDamage(AttackStyle::Ranged, AttackType::Light);
        g.knockback = 0.0f;
        g.stagger = 3.0f;
        g.once = true;
        AddGroundEffect(g);
        say("Snare set", {200, 190, 160, 255});
    } else if (ability == "overload") {
        say("Overload", {190, 170, 255, 255});
        Burst(px, py - 16.0f, 32.0f, {190, 170, 255, 255}, 14);
    } else if (ability == "invoke") {
        say("Invoke", {130, 170, 255, 255});
        Burst(px, py - 16.0f, 44.0f, {130, 170, 255, 255}, 18);
    } else if (ability == "repulse") {
        const SpellDef* spell = ctx.spells
            ? ctx.spells->BestFor(player.SelectedElement() == Element::Arcane ? Element::Fire : player.SelectedElement(),
                                  player.skills.Level(SKILL_MAGIC))
            : nullptr;
        const Element element = spell ? spell->element : Element::None;
        const float mult = 0.6f * (spell ? spell->damage_mult : 1.0f) * player.TalentDamage(AttackStyle::Magic, AttackType::Light);
        for (auto& e : enemies) {
            if (!Targeting::Targetable(*e) || Length(e->x - px, e->y - py) > 116.0f) continue;
            // The bolt in it can miss; the wall cannot.
            HitEnemy(*e, player.Profile(), AttackStyle::Magic, element, mult, 0.0f, px, py, ctx);
            const float away = std::max(1.0f, Length(e->x - px, e->y - py));
            e->knock_x += (e->x - px) / away * 300.0f;
            e->knock_y += (e->y - py) / away * 300.0f;
            e->Stagger(0.7f);
        }
        if (spell) player.NoteCast(spell->element);
        Burst(px, py - 16.0f, 116.0f, element == Element::None ? SDL_Color{190, 170, 255, 255} : ElementColor(element), 26);
    }
}

void World::ApplyPlayerAttack(const GameContext& ctx) {
    // One swing lands once, on every enemy inside the arc.
    if (!player.AttackPending()) return;
    player.MarkAttackConsumed();
    const AttackState& atk = player.Attack();

    if (player.Style() != AttackStyle::Melee) {
        FirePlayerProjectile(ctx);
        return;
    }
    if (atk.type == AttackType::Charged && MeleeTechnique(player.ActiveTechnique(), ctx))
        return;

    const ItemDef* in_hand = player.equipment.Weapon();
    const float mult  = atk.damage_mult * player.TalentDamage(AttackStyle::Melee, atk.type) * (in_hand ? in_hand->damage : 1.0f);
    const float knock = atk.profile.knockback * (1.0f + player.talents.Effect("knockback", AttackStyle::Melee));

    // A combo says its name over the player as it comes out -- the weapon's
    // own name for it, where it has one.
    if (atk.move != ComboMove::None)
        AddText(player.ComboLabel(atk.move), player.x, player.y - 58.0f, {255, 232, 150, 255}, 0.8f);

    // The Cross Cut is a turn on the spot: it strikes everything round the
    // player as far as the blade reaches, the way Whirlwind does.
    if (atk.move == ComboMove::CrossCut) {
        const float radius = atk.profile.reach;
        if (HitAround(radius, mult, knock, ctx) > 0) player.CountChainHit(SwingLabel(ctx));
        else player.BreakChain();
        Burst(player.x, player.y - 10.0f, radius, {255, 236, 190, 255}, 8);
        return;
    }

    // The sector DrawSwing draws, on the ground: see StrikeArc.
    const SDL_FPoint from = player.GroundCentre();
    const StrikeArc hit = ArcFor(from.x, from.y, player.facing, atk.profile, atk.reach_scale);
    bool connected = false;

    for (auto& e : enemies) {
        if (!Strikeable(*e)) continue;
        const SDL_FPoint a = e->GroundCentre();
        if (!ArcHits(hit, a.x, a.y, e->GroundRadius())) continue;

        connected = true;
        const int before = e->hp;
        HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None, mult, knock,
                 player.x, player.y, ctx, atk.type);
        // The Crushing Blow leaves what it lands on reeling.
        if (atk.move == ComboMove::Crush && e->hp < before) e->Stagger(CRUSH_STAGGER);
    }

    // The chain: one more for a swing that met something, and over for one
    // that met nothing.
    if (connected) player.CountChainHit(SwingLabel(ctx));
    else player.BreakChain();

    if (!connected && atk.type == AttackType::Charged)
        AddText("whiff", player.x, player.y - 52.0f, {150, 150, 160, 200});
}

// One place where a hit lands, whether it came from a sword, an arrow or a
// bolt of fire, so the element matchup and the XP are applied consistently.
void World::TryAfflict(Enemy& e, const StatusProc& proc, int blow, const GameContext& ctx) {
    if (!proc.Any() || !ctx.statuses || !ctx.rng || e.hp <= 0) return;
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(*ctx.rng) >= proc.chance) return;
    const bool had = e.Afflicted(proc.kind);
    const Status left = e.Afflict(proc.kind, blow, *ctx.statuses);
    if (left == Status::COUNT) return;
    // Said once, as it takes: a fire kept burning by a second bolt says nothing.
    if (had && left == proc.kind) return;
    if (const StatusDef* d = ctx.statuses->Get(left))
        AddText(d->name, e.x, e.y - 60.0f, d->color, 0.9f);
}

void World::HitEnemy(Enemy& e, const CombatProfile& owner, AttackStyle style,
                     Element element, float damage_mult, float knockback,
                     float from_x, float from_y, const GameContext& ctx, AttackType swing) {
    // The player's talents. Everything that reaches this function is the
    // player hitting something, so they apply to all of it.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    bool crit = ctx.rng && unit(*ctx.rng) < player.talents.Effect("crit", style);
    if (crit_next) crit = true;             // loosed with Take Aim
    // Executioner: what is nearly down is always struck critically.
    const float execute = player.talents.Effect("execute", style);
    if (execute > 0.0f && e.HealthFraction() < execute) crit = true;
    if (crit) damage_mult *= 1.5f + player.talents.Effect("crit_damage", style);

    // The passives that ask where, when and on what. Each is its tree's, so a
    // hero's Momentum does nothing for a bow in the hero's hand.
    if (style == AttackStyle::Melee) {
        damage_mult *= 1.0f + player.talents.Effect("momentum", style) *
                              static_cast<float>(std::min(Player::MOMENTUM_MAX, player.ChainHits()));
        if (player.RiposteReady()) damage_mult *= 1.0f + player.talents.Effect("riposte", style);
    }
    if (style == AttackStyle::Ranged) {
        if (Length(e.x - player.x, e.y - player.y) > 180.0f) damage_mult *= 1.0f + player.talents.Effect("long_shot", style);
        if (e.hp >= e.max_hp) damage_mult *= 1.0f + player.talents.Effect("first_blood", style);
        // Weak Point: the same place, again. Counted whether or not it is
        // learned, so learning it mid-fight starts from where the fight is.
        const float weak = player.talents.Effect("weak_point", style);
        const int shots = player.NoteShotOn(&e);
        if (weak > 0.0f) damage_mult *= 1.0f + weak * static_cast<float>(shots);
    }
    // Punish, and the Trapper: what is reeling cannot brace.
    if (e.Staggered()) damage_mult *= 1.0f + player.talents.Effect("punish", style);
    if (style == AttackStyle::Magic)
        damage_mult *= 1.0f + player.talents.Effect("attunement", style) * static_cast<float>(player.AttuneStacks());
    // A mark is on the monster, not on whoever made it: a friend's blow too.
    if (e.Marked()) damage_mult *= 1.0f + Enemy::MARK_DAMAGE;
    if (style == AttackStyle::Magic && ElementMultiplier(element, e.ElementOf()) > 1.05f)
        damage_mult *= 1.0f + player.talents.Effect("elemental", style);

    // Everything the player does to a monster comes through here, so this is
    // the one place the damage floor is asked for: a swing of theirs that
    // connects always takes something off.
    // Against its Defence -- less what the point of the thing goes past: a
    // dagger's, a crossbow bolt's, a combo that finds the gap.
    CombatProfile guard = e.Profile();
    float past = pierce_next;
    const ItemDef::ComboTwist* twist = nullptr;
    if (style == AttackStyle::Melee) {
        if (const ItemDef* blade = player.equipment.Weapon()) past = std::max(past, blade->armour_pierce);
        twist = player.Twist(player.Attack().move);
        if (twist) { past = std::max(past, twist->pierce); damage_mult *= twist->damage; }
        // A pair of daggers lands twice as often, and each of the two is worth
        // less for it -- every blow of theirs, a technique's as much as a stab.
        damage_mult *= player.equipment.DualDamage();
    }
    if (past > 0.0f) {
        guard.defence_level = std::max(1, static_cast<int>(guard.defence_level * (1.0f - past)));
        guard.defence_bonus = static_cast<int>(guard.defence_bonus * (1.0f - past));
    }
    DamageResult r = RollAttack(owner, guard, style, damage_mult, *ctx.rng, true);

    // Every way the player can hurt something -- swing, arrow, bolt, burning
    // ground -- comes through here, so this is where the bar first appears.
    // Before the miss check: a swing that misses has still started the fight.
    e.RevealHealthBar();

    if (!r.hit) {
        AddText("miss", e.x, e.y - 46.0f, {150, 150, 168, 235});
        return;
    }

    // Elements only matter when both sides have one -- and what is on it can
    // make one bite harder: the wind, on something soaked.
    const float matchup = ElementMultiplier(element, e.ElementOf()) * e.StatusWeakness(element);
    int damage = static_cast<int>(roundf(r.damage * matchup));
    if (r.damage > 0 && damage <= 0) damage = 1;

    if (damage <= 0) {
        AddText("0", e.x, e.y - 46.0f, {120, 160, 220, 255});
        Audio::PlayAt(Sfx::Block, e.x, e.y);
        return;
    }
    // A killing blow is heard as the death, not as a hit on top of it.
    if (damage < e.hp)
        Audio::PlayAt(r.max_hit ? Sfx::HitCrit : Sfx::Hit, e.x, e.y);

    e.Damage(damage);
    // It comes for whoever did that, from wherever they did it.
    e.Provoke(static_cast<int>(player.seat));
    // What it trains is decided by the swing that did it. This line used to
    // say every blow was a light one, so a heavy swing fed Attack and nothing
    // in combat ever fed Strength -- the skill that sets how hard a blow can
    // land sat at level 1 for the whole of the game.
    player.AwardCombatXp(damage, swing, e.Def() ? e.Def()->xp_multiplier : 1.0f);
    // And if this is the first thing a spell has hurt, the spell's own.
    PayCast(cast_next);

    // What a blow that landed pays back.
    if (style == AttackStyle::Melee && player.RiposteReady()) player.SpendRiposte();
    if (style == AttackStyle::Ranged && player.talents.Effect("hit_run", style) > 0.0f) player.NoteRangedHit();
    if (crit) player.GainMana(static_cast<int>(player.talents.Effect("crit_mana", style)));
    if (e.hp <= 0) player.GainStamina(player.talents.Effect("kill_stamina", style));
    // Open Wounds: a chain three deep leaves them open. The chain is counted
    // after the swing has landed on everything, so this is the hits before it.
    if (style == AttackStyle::Melee && e.hp > 0 && player.ChainHits() + 1 >= Player::BLEED_CHAIN)
        e.Bleed(static_cast<float>(damage) * player.talents.Effect("bleed", style));

    // What the blow leaves on it, if it is still standing to have it: whatever
    // threw it says (a projectile's, the ground's), and a swing is asked its
    // weapon -- a sword's edge opens a wound some of the time.
    if (e.hp > 0) {
        StatusProc proc = proc_next;
        if (!proc.Any() && style == AttackStyle::Melee)
            if (const ItemDef* blade = player.equipment.Weapon()) proc = blade->on_hit;
        // A combo that always leaves its mark: the mace's Skull Crack.
        if (twist && twist->status != Status::COUNT) proc = {twist->status, 1.0f};
        TryAfflict(e, proc, damage, ctx);
    }

    // The Vampiric Touch's share comes back with the talent's.
    const float steal = player.talents.Effect("lifesteal", style) + leech_next;
    if (steal > 0.0f && !player.IsDead()) {
        lifesteal_bank += damage * steal;
        const int whole = static_cast<int>(lifesteal_bank);
        if (whole > 0 && player.hp < player.max_hp) {
            lifesteal_bank -= whole;
            player.Heal(whole);
            player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
        }
    }

    SDL_Color color = (r.max_hit || crit) ? SDL_Color{255, 220, 90, 255}
                                          : SDL_Color{255, 245, 235, 255};
    string label = std::to_string(damage);
    if (crit) label += "*";
    if (matchup > 1.05f) {
        color = ElementColor(element);
        label += "!";                      // strong against this creature
    } else if (matchup < 0.95f) {
        color = {150, 150, 170, 255};      // resisted
    }
    AddText(label, e.x, e.y - 46.0f, color);

    const float dx = e.x - from_x, dy = e.y - from_y;
    const float len = std::max(1.0f, Length(dx, dy));
    e.knock_x += (dx / len) * knockback;
    e.knock_y += (dy / len) * knockback;
}

int World::HitPlayer(int damage, const CombatProfile& attacker, float from_x, float from_y,
                     float knock_x, float knock_y) {
    if (damage <= 0 || player.IsDead() || player.resting || player.Untouchable()) return 0;
    // Slippery: on the move, some of them simply miss.
    const float evade = player.talents.Global("evade");
    if (evade > 0.0f && player.Moving() && !player.Blocking() &&
        std::uniform_real_distribution<float>(0.0f, 1.0f)(evade_dice) < evade) {
        AddText("slipped", player.x, player.y - 58.0f, {190, 230, 190, 255});
        return 0;
    }
    // Stand Fast: feet set, less of it gets through and none of it moves you.
    if (player.StandingFast()) {
        damage = std::max(1, static_cast<int>(std::lround(damage * Player::STAND_FAST_SHARE)));
        knock_x = knock_y = 0.0f;
    }
    const BlockOutcome b = player.TryBlock(damage, CombatLevelOf(attacker), from_x, from_y);

    if (b.blocked > 0) {
        AddText("blocked " + std::to_string(b.blocked), player.x, player.y - 58.0f,
                {150, 196, 240, 255});
        Audio::PlayAt(Sfx::Block, player.x, player.y);
        player.NoteBlock();          // a blow caught is a blow owed: Riposte
    }
    if (b.broke)
        AddText("Guard broken!", player.x, player.y - 72.0f, {255, 176, 96, 255}, 1.6f);
    if (b.taken > 0) player.BreakChain();
    if (b.taken > 0) {
        // A mana shield pays half of it in mana, while there is mana to pay.
        const int in_blood = player.AbsorbWithMana(b.taken);
        if (in_blood < b.taken)
            AddText("-" + std::to_string((b.taken - in_blood) * Player::MANA_PER_HP) + " mana", player.x, player.y - 58.0f,
                    {130, 170, 255, 255});
        player.Damage(in_blood);
        player.NoteHurt();
        player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
        AddText(std::to_string(in_blood), player.x, player.y - 44.0f, {235, 70, 70, 255});
        // Resolve: pain is a kind of fuel.
        if (in_blood > 0 && !player.IsDead()) player.GainMana(static_cast<int>(player.talents.Global("hurt_mana")));
        // Taking a hit trains Defence, as it does in OSRS.
        player.GrantXp(SKILL_DEFENCE, std::max(1, b.taken));
    }
    // A blow on the shield still shoves, only less.
    const float push = b.taken > 0 ? 1.0f : 0.35f;
    player.knock_x += knock_x * push;
    player.knock_y += knock_y * push;
    return b.taken;
}

int World::HeavyHitPlayer(int damage, float from_x, float from_y, float knock_x, float knock_y) {
    if (player.resting || player.Untouchable()) return 0;
    player.BreakChain();
    if (damage <= 0 || player.IsDead()) return 0;
    // What is worn takes its share first: see HeavySoak. Before the guard is
    // asked about, so a shield raised to it is still the mistake it always
    // was -- half as much again of whatever the armour let through.
    {
        const CombatProfile mine = player.Profile();
        damage = SoakHeavy(damage, mine.defence_level, mine.defence_bonus);
    }
    float push = 1.0f;
    if (player.StandingFast()) {
        damage = std::max(1, static_cast<int>(std::lround(damage * Player::STAND_FAST_SHARE)));
        push = 0.0f;
    }
    if (player.GuardFacing(from_x, from_y)) {
        // Met with a shield: it goes straight through, and takes the guard
        // and the breath with it.
        damage = static_cast<int>(std::lround(damage * HEAVY_BLOCK_PUNISH));
        player.ShatterGuard();
        push = player.StandingFast() ? 0.0f : 1.6f;
        AddText("Guard shattered!", player.x, player.y - 72.0f, {255, 120, 80, 255}, 1.8f);
        Audio::PlayAt(Sfx::Block, player.x, player.y, 1.0f, 0.6f);
    }
    player.Damage(damage);
    player.NoteHurt();
    player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
    AddText(std::to_string(damage), player.x, player.y - 44.0f, {255, 60, 40, 255}, 1.2f);
    player.GrantXp(SKILL_DEFENCE, std::max(1, damage));
    player.knock_x += knock_x * push;
    player.knock_y += knock_y * push;
    return damage;
}
