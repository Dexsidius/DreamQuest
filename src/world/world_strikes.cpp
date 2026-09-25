// =============================================================================
//  world_strikes.cpp - what a combo looks like, and a parry, and a riposte.
//
//  Every combo strike has marks of its own, drawn by the fx shader's strike
//  shapes (fx.frag, 4 to 8): a slash that sweeps round, a flash with rays and a
//  ring where a blow lands, a thrust, a cross cut, a circle cast on the ground.
//  What each combo gets is one table per weapon family below -- the sword's,
//  the dagger's, the mace's, the greatsword's, the greataxe's, the bow's and the
//  staff's -- so a Gut Stab is told from a Crushing Blow across the room.
//
//  All of it is for show: nothing here touches a hit, a number or the dice,
//  and with the visual effects off (or off the GPU) none of it is drawn --
//  the plain swing crescent is (World::DrawSwing).
// =============================================================================

#include "world.h"
#include "../systems/shaders.h"
#include "../systems/audio.h"

namespace {

constexpr float kPi = 3.14159265f;

// A melee weapon's combos, by family: what they look like.
enum Family { BLADE = 0, DAGGER, MACE, GREAT, AXE, FAMILIES };

Family FamilyOf(const ItemDef* w) {
    if (!w) return BLADE;
    if (w->weapon_class == "dagger")     return DAGGER;
    if (w->weapon_class == "mace")       return MACE;
    if (w->weapon_class == "greatsword") return GREAT;
    if (w->weapon_class == "greataxe")   return AXE;
    return BLADE;
}

int MoveIndex(ComboMove m) {
    switch (m) {
        case ComboMove::Crush:    return 0;
        case ComboMove::Cleave:   return 1;
        case ComboMove::Backhand: return 2;
        case ComboMove::CrossCut: return 3;
        default:                  return -1;
    }
}

SDL_FColor Rgb(int r, int g, int b) { return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }

// Each family's four colours, Crush to Cross Cut. The sword keeps the tints its
// crescents always had.
const SDL_FColor kColour[FAMILIES][4] = {
    {Rgb(255, 200, 120), Rgb(255, 168, 90),  Rgb(214, 255, 214), Rgb(196, 216, 255)},   // sword
    {Rgb(255, 86, 86),   Rgb(232, 238, 248), Rgb(176, 118, 255), Rgb(206, 228, 255)},   // dagger
    {Rgb(255, 224, 120), Rgb(232, 172, 96),  Rgb(240, 214, 170), Rgb(214, 176, 118)},   // mace
    {Rgb(255, 238, 190), Rgb(255, 92, 80),   Rgb(236, 236, 236), Rgb(190, 222, 255)},   // greatsword
    {Rgb(255, 96, 70),   Rgb(255, 150, 72),  Rgb(236, 226, 206), Rgb(220, 60, 60)},     // greataxe
};

float FacingAngle(Facing f) {
    return f == FACE_RIGHT ? 0.0f : f == FACE_DOWN ? kPi * 0.5f : f == FACE_LEFT ? kPi : -kPi * 0.5f;
}

}   // namespace

// --- the marks themselves ----------------------------------------------------------

void World::AddStrike(const Strike& s) {
    if (strikes.size() >= 96) strikes.erase(strikes.begin());
    strikes.push_back(s);
    if (journal) {
        StrikeNote n;
        n.kind = StrikeNote::MARK;
        n.mark = s;
        strike_log.push_back(n);
    }
}

void World::StrikeShock(float x, float y, float strength, float shake_amount) {
    Shock(x, y, strength, SeenHere() ? shake_amount : 0.0f);
    if (journal) {
        StrikeNote n;
        n.kind = StrikeNote::SHOCK;
        n.x = x; n.y = y; n.size = strength; n.amount = shake_amount;
        n.seat = player.seat;
        strike_log.push_back(n);
    }
}

void World::StrikeBurst(float x, float y, float radius, SDL_Color colour, int count, float turn) {
    Burst(x, y, radius, colour, count, turn);
    if (journal) {
        StrikeNote n;
        n.kind = StrikeNote::BURST;
        n.x = x; n.y = y; n.size = radius; n.amount = turn;
        n.colour = colour;
        n.count = count;
        strike_log.push_back(n);
    }
}

void World::ReplayStrike(const StrikeNote& note, uint8_t my_seat) {
    // In a friend's window, as the host drew it; the shake only if the blow
    // was theirs.
    switch (note.kind) {
        case StrikeNote::MARK:  AddStrike(note.mark); break;
        case StrikeNote::SHOCK: Shock(note.x, note.y, note.size, note.seat == my_seat ? note.amount : 0.0f); break;
        case StrikeNote::BURST: Burst(note.x, note.y, note.size, note.colour, note.count, note.amount); break;
    }
}

void World::UpdateStrikes(float dt) {
    for (Strike& s : strikes) {
        if (s.delay > 0.0f) { s.delay -= dt; continue; }
        s.age += dt;
    }
    strikes.erase(std::remove_if(strikes.begin(), strikes.end(), [](const Strike& s) { return s.age >= s.life; }),
                  strikes.end());
}

void World::DrawStrikes(SDL_Renderer* r) const {
    if (strikes.empty() || !Shaders::Effects()) return;
    for (const Strike& s : strikes) {
        if (s.delay > 0.0f) continue;
        const float k = std::clamp(s.age / std::max(0.01f, s.life), 0.0f, 1.0f);
        Shaders::ShapeFx fx;
        fx.shape = s.shape;
        fx.seed = s.seed;
        fx.colour = s.colour;
        // Gone in the last third of its life.
        fx.fade = 1.0f - std::clamp((k - 0.66f) / 0.34f, 0.0f, 1.0f);
        float p[4] = {s.p[0], s.p[1], s.p[2], s.p[3]};
        if (s.grows >= 0 && s.grows < 4) p[s.grows] = s.grows_from + (s.grows_to - s.grows_from) * k;
        fx.hit_x = p[0];
        fx.hit_y = p[1];
        fx.hit_age = p[2];
        fx.extra = p[3];
        const float lift = LiftAt(s.x, s.y) + s.lift;
        const SDL_FRect dst = camera.ToScreenRect({s.x - s.radius, s.y - lift - s.radius, s.radius * 2.0f, s.radius * 2.0f});
        Shaders::DrawShape(r, dst, fx, SDL_BLENDMODE_ADD);
    }
}

namespace {
World::Strike MkSlash(float x, float y, float radius, float a0, float sweep, float thick, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = Shaders::SHAPE_SLASH;
    s.x = x; s.y = y; s.radius = radius;
    s.p[0] = a0; s.p[1] = sweep; s.p[3] = thick;
    s.grows = 2; s.grows_from = 0.0f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
World::Strike MkImpact(float x, float y, float radius, float rays, float squash, float ring, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = Shaders::SHAPE_IMPACT;
    s.x = x; s.y = y; s.radius = radius;
    s.p[1] = rays; s.p[2] = squash; s.p[3] = ring;
    s.grows = 0; s.grows_from = 0.0f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
World::Strike MkThrust(float x, float y, float radius, float angle, float width, float from, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = Shaders::SHAPE_THRUST;
    s.x = x; s.y = y; s.radius = radius;
    s.p[0] = angle; s.p[2] = width; s.p[3] = from;
    s.grows = 1; s.grows_from = from + 0.05f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
World::Strike MkCross(float x, float y, float radius, float angle, float width, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = Shaders::SHAPE_CROSS;
    s.x = x; s.y = y; s.radius = radius;
    s.p[0] = angle; s.p[2] = width;
    s.grows = 1; s.grows_from = 0.0f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
World::Strike MkCircle(float x, float y, float radius, float squash, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = Shaders::SHAPE_CIRCLE;
    s.x = x; s.y = y; s.radius = radius;
    s.p[2] = squash;
    s.grows = 0; s.grows_from = 0.0f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
}   // namespace

// --- the melee combos ----------------------------------------------------------------

void World::ComboSwingFx(ComboMove move, const ItemDef* weapon) {
    const int m = MoveIndex(move);
    if (m < 0) return;
    const Family f = FamilyOf(weapon);
    const SDL_FColor c = kColour[f][m];
    const float face = FacingAngle(player.facing);
    const SDL_FPoint g = player.GroundCentre();
    const float reach = std::max(24.0f, player.Attack().profile.reach * player.Attack().reach_scale);
    // Where a blow brought down in front lands.
    const float fx = g.x + cosf(face) * reach * 0.8f, fy = g.y + sinf(face) * reach * 0.8f;
    const float seed = static_cast<float>(strike_seed++ % 97);

    switch (move) {
        case ComboMove::Crush:
            if (f == DAGGER) {
                // Gut Stab: driven straight in, a red line out along the facing.
                Strike s = MkThrust(g.x, g.y - 14.0f, reach + 18.0f, face, 0.16f, 0.05f, c, 0.26f);
                s.seed = seed;
                AddStrike(s);
            } else {
                // Brought down from above: a streak falling onto the spot, and
                // the ground breaking under it -- wider and harder for the
                // heavy weapons, a ring of stars for the mace.
                const bool heavy = f == GREAT || f == AXE;
                Strike fall = MkThrust(fx, fy - 26.0f, 30.0f, kPi * 0.5f, 0.12f, -0.9f, c, 0.18f);
                AddStrike(fall);
                Strike ground = MkImpact(fx, fy, heavy ? 56.0f : 40.0f, f == MACE ? 6.0f : 11.0f, 0.42f, 0.16f, c,
                                       heavy ? 0.5f : 0.4f);
                ground.seed = seed;
                ground.delay = 0.03f;
                AddStrike(ground);
                StrikeShock(fx, fy, heavy ? 0.45f : 0.3f, heavy ? 0.35f : 0.2f);
                StrikeBurst(fx, fy - 4.0f, heavy ? 30.0f : 20.0f, {170, 150, 120, 255}, heavy ? 14 : 9);
            }
            break;
        case ComboMove::Cleave: {
            // The crescent is DrawSwing's; what it leaves behind is the wind of
            // it -- a wider, thinner sweep just outside it, a moment later --
            // and a spark thrown off where the edge finishes. (A ring of
            // Burst's blobs read as orange coins on the floor, not sparks.)
            const float half = std::max(0.6f, player.Attack().profile.HalfAngle(reach));
            Strike wind = MkSlash(g.x, g.y - 16.0f, reach + 20.0f, face - half, 2.0f * half, 0.1f, c, 0.28f);
            wind.delay = 0.05f;
            AddStrike(wind);
            const float end = face + half;
            Strike spark = MkImpact(g.x + cosf(end) * reach, g.y - 16.0f + sinf(end) * reach, f == DAGGER ? 10.0f : 14.0f,
                                    6.0f, 1.0f, 0.0f, c, 0.16f);
            spark.seed = seed;
            spark.delay = 0.08f;
            AddStrike(spark);
            break;
        }
        case ComboMove::Backhand: {
            // A snap: a flash where the swing turns back.
            Strike s = MkImpact(g.x + cosf(face) * 14.0f, g.y - 18.0f + sinf(face) * 10.0f, 16.0f, 5.0f, 1.0f, 0.0f, c, 0.16f);
            s.seed = seed;
            AddStrike(s);
            break;
        }
        case ComboMove::CrossCut:
            if (f == DAGGER) {
                // Fan of Steel: blades flung out all round, a ring of glints.
                Strike s = MkImpact(g.x, g.y - 12.0f, reach + 16.0f, 14.0f, 0.7f, 0.08f, c, 0.35f);
                s.seed = seed;
                AddStrike(s);
                StrikeBurst(g.x, g.y - 14.0f, reach, {230, 240, 255, 255}, 16);
            } else if (f == MACE) {
                // Ground Slam: the floor itself goes out from under everything.
                Strike s = MkImpact(g.x, g.y, reach * 1.3f, 12.0f, 0.4f, 0.22f, c, 0.55f);
                s.seed = seed;
                AddStrike(s);
                StrikeShock(g.x, g.y, 0.6f, 0.45f);
                StrikeBurst(g.x, g.y - 4.0f, reach, {170, 150, 120, 255}, 18);
            } else {
                // A turn on the spot: the ring it cuts, lying on the ground
                // round the feet as the crescent goes round above it.
                Strike s = MkImpact(g.x, g.y, reach + 10.0f, 0.0f, 0.45f, 0.1f, c, 0.4f);
                AddStrike(s);
                if (f == GREAT || f == AXE) {
                    Strike again = MkImpact(g.x, g.y, reach + 24.0f, 0.0f, 0.45f, 0.08f, c, 0.5f);
                    again.delay = 0.1f;
                    AddStrike(again);
                }
            }
            break;
        default: break;
    }
}

void World::ComboHitFx(ComboMove move, const ItemDef* weapon, const Enemy& e) {
    const int m = MoveIndex(move);
    if (m < 0) return;
    const Family f = FamilyOf(weapon);
    const SDL_FColor c = kColour[f][m];
    const float face = FacingAngle(player.facing);
    const float ex = e.x, ey = e.y - 18.0f;
    const float seed = static_cast<float>(strike_seed++ % 97);
    const SDL_Color blood{190, 20, 30, 255};

    switch (move) {
        case ComboMove::Crush:
            if (f == DAGGER) {
                // Gut Stab: it goes in, and comes out red.
                Strike s = MkImpact(ex, ey, 20.0f, 7.0f, 1.0f, 0.0f, c, 0.25f);
                s.seed = seed;
                AddStrike(s);
                StrikeBurst(ex, ey, 18.0f, blood, 10, face);
            } else if (f == MACE) {
                // Skull Crack: stars round the head it rang.
                Strike s = MkImpact(ex, ey - 16.0f, 22.0f, 5.0f, 0.5f, 0.12f, c, 0.45f);
                s.seed = seed;
                AddStrike(s);
            } else {
                Strike s = MkImpact(ex, ey, 22.0f, 8.0f, 1.0f, 0.0f, c, 0.22f);
                s.seed = seed;
                AddStrike(s);
                if (f == AXE) StrikeBurst(ex, ey, 16.0f, blood, 8, face);
            }
            break;
        case ComboMove::Cleave:
            if (f == DAGGER) {
                // Flurry: three quick cuts across it, one after another.
                for (int k = 0; k < 3; ++k) {
                    const float a = face + kPi * 0.5f + (k - 1) * 0.9f + static_cast<float>(strike_seed % 7) * 0.1f;
                    Strike s = MkSlash(ex + (k - 1) * 4.0f, ey + (k - 1) * 3.0f, 16.0f, a, 1.6f, 0.28f, c, 0.16f);
                    s.delay = k * 0.06f;
                    AddStrike(s);
                }
            } else {
                // A cut laid across it along the swing, and whatever it bled.
                Strike s = MkSlash(ex, ey, 22.0f, face - 2.2f, 2.0f, 0.24f, c, 0.2f);
                AddStrike(s);
                if (f == GREAT || f == AXE) StrikeBurst(ex, ey, 16.0f, blood, 8, face);
            }
            break;
        case ComboMove::Backhand:
            if (f == DAGGER) {
                // Backstab: in from the shadow, violet, and it lands deep.
                Strike s = MkThrust(ex, ey, 26.0f, face + kPi, 0.12f, -1.0f, c, 0.2f);
                AddStrike(s);
                Strike star = MkImpact(ex, ey, 18.0f, 4.0f, 1.0f, 0.0f, c, 0.22f);
                star.seed = seed;
                star.delay = 0.04f;
                AddStrike(star);
            } else if (f == GREAT || f == AXE) {
                // Pommel Strike, Haft Check: a knock that leaves it seeing stars.
                Strike s = MkImpact(ex, ey - 16.0f, 20.0f, 5.0f, 0.5f, 0.12f, c, 0.4f);
                s.seed = seed;
                AddStrike(s);
            } else {
                Strike s = MkImpact(ex, ey, 16.0f, 6.0f, 1.0f, 0.0f, c, 0.15f);
                s.seed = seed;
                AddStrike(s);
            }
            break;
        case ComboMove::CrossCut:
            if (f == DAGGER) {
                // A glint where each of the fan's blades went in.
                Strike s = MkThrust(ex, ey, 18.0f, atan2f(ey - player.y, ex - player.x), 0.1f, -1.0f, c, 0.15f);
                AddStrike(s);
            } else if (f == MACE) {
                Strike s = MkImpact(ex, ey - 16.0f, 20.0f, 5.0f, 0.5f, 0.12f, c, 0.4f);
                s.seed = seed;
                AddStrike(s);
            } else {
                // The Cross Cut's own mark: an X on everything it went round.
                Strike s = MkCross(ex, ey, 22.0f, atan2f(ey - player.y, ex - player.x), 0.1f, c, 0.3f);
                AddStrike(s);
                if (f == AXE) StrikeBurst(ex, ey, 16.0f, blood, 8);
            }
            break;
        default: break;
    }
}

// --- the bow's and the staff's -------------------------------------------------------

void World::ComboShotFx(ComboMove move, AttackStyle style, Element element, float x, float y, float angle) {
    const int m = MoveIndex(move);
    if (m < 0) return;
    const float seed = static_cast<float>(strike_seed++ % 97);
    if (style == AttackStyle::Ranged) {
        static const SDL_FColor kBow[4] = {Rgb(255, 214, 120), Rgb(255, 90, 80), Rgb(240, 248, 255), Rgb(150, 200, 255)};
        const SDL_FColor c = kBow[m];
        switch (move) {
            case ComboMove::Crush:      // Split Shot: three lines out from the string
                for (float d : {-0.16f, 0.0f, 0.16f}) AddStrike(MkThrust(x, y, 34.0f, angle + d, 0.07f, 0.1f, c, 0.16f));
                break;
            case ComboMove::Cleave: {   // Barbed Shot: a heavy line, and the kick of it
                AddStrike(MkThrust(x, y, 44.0f, angle, 0.14f, 0.05f, c, 0.2f));
                Strike s = MkImpact(x, y, 18.0f, 6.0f, 1.0f, 0.1f, c, 0.2f);
                s.seed = seed;
                AddStrike(s);
                StrikeShock(x, y, 0.2f, 0.15f);
                break;
            }
            case ComboMove::Backhand:   // Snap Shot: gone before you saw it drawn
                AddStrike(MkThrust(x, y, 30.0f, angle, 0.05f, 0.2f, c, 0.1f));
                AddStrike(MkImpact(x, y, 10.0f, 4.0f, 1.0f, 0.0f, c, 0.12f));
                break;
            case ComboMove::CrossCut: { // Twin Shot: two lines side by side
                const float nx = -sinf(angle) * 5.0f, ny = cosf(angle) * 5.0f;
                AddStrike(MkThrust(x + nx, y + ny, 40.0f, angle, 0.08f, 0.1f, c, 0.2f));
                AddStrike(MkThrust(x - nx, y - ny, 40.0f, angle, 0.08f, 0.1f, c, 0.2f));
                break;
            }
            default: break;
        }
        return;
    }
    // The staff's, in the colour of what it casts.
    const SDL_Color ec = element == Element::None ? SDL_Color{190, 170, 255, 255} : ElementColor(element);
    const SDL_FColor c = {ec.r / 255.0f, ec.g / 255.0f, ec.b / 255.0f, 1.0f};
    const SDL_FPoint g = player.GroundCentre();
    switch (move) {
        case ComboMove::Crush: {        // Surge: a circle under the caster, and the air thrown back
            Strike s = MkCircle(g.x, g.y, 30.0f, 0.45f, c, 0.55f);
            AddStrike(s);
            StrikeShock(x, y, 0.3f, 0.2f);
            break;
        }
        case ComboMove::Cleave:         // Cascade: three lines fanned out from the staff
            for (float d : {-0.25f, 0.0f, 0.25f}) AddStrike(MkThrust(x, y, 34.0f, angle + d, 0.08f, 0.1f, c, 0.2f));
            break;
        case ComboMove::Backhand: {     // Flicker: a flash at the staff's head
            Strike s = MkImpact(x, y, 14.0f, 6.0f, 1.0f, 0.0f, c, 0.14f);
            s.seed = seed;
            AddStrike(s);
            break;
        }
        case ComboMove::CrossCut: {     // Pulse: a ring going out all round
            Strike s = MkImpact(g.x, g.y - 8.0f, 72.0f, 0.0f, 0.55f, 0.1f, c, 0.45f);
            AddStrike(s);
            AddStrike(MkCircle(g.x, g.y, 26.0f, 0.45f, c, 0.4f));
            StrikeShock(g.x, g.y, 0.35f, 0.2f);
            break;
        }
        default: break;
    }
}

void World::ComboShotHitFx(ComboMove move, AttackStyle style, Element element, float x, float y, float angle) {
    const int m = MoveIndex(move);
    if (m < 0) return;
    const float seed = static_cast<float>(strike_seed++ % 97);
    SDL_FColor c;
    if (style == AttackStyle::Ranged) {
        static const SDL_FColor kBow[4] = {Rgb(255, 214, 120), Rgb(255, 90, 80), Rgb(240, 248, 255), Rgb(150, 200, 255)};
        c = kBow[m];
    } else {
        const SDL_Color ec = element == Element::None ? SDL_Color{190, 170, 255, 255} : ElementColor(element);
        c = {ec.r / 255.0f, ec.g / 255.0f, ec.b / 255.0f, 1.0f};
    }
    switch (move) {
        case ComboMove::Crush: {
            Strike s = MkImpact(x, y, style == AttackStyle::Magic ? 34.0f : 16.0f, 8.0f, 1.0f,
                              style == AttackStyle::Magic ? 0.14f : 0.0f, c, 0.3f);
            s.seed = seed;
            AddStrike(s);
            if (style == AttackStyle::Magic) StrikeShock(x, y, 0.3f, 0.15f);
            break;
        }
        case ComboMove::Cleave: {
            Strike s = MkImpact(x, y, 20.0f, 7.0f, 1.0f, 0.0f, c, 0.25f);
            s.seed = seed;
            AddStrike(s);
            if (style == AttackStyle::Ranged) StrikeBurst(x, y, 14.0f, {190, 20, 30, 255}, 8, angle);
            break;
        }
        case ComboMove::Backhand:
            AddStrike(MkImpact(x, y, 12.0f, 5.0f, 1.0f, 0.0f, c, 0.14f));
            break;
        case ComboMove::CrossCut:
            if (style == AttackStyle::Ranged) AddStrike(MkCross(x, y, 16.0f, angle, 0.1f, c, 0.22f));
            else AddStrike(MkImpact(x, y, 16.0f, 6.0f, 1.0f, 0.1f, c, 0.22f));
            break;
        default: break;
    }
}

// --- a parry, and a riposte ----------------------------------------------------------

void World::ParryFx(float x, float y, float angle) {
    // Steel on steel: a white flash with rays, and sparks thrown back.
    Strike s = MkImpact(x, y, 22.0f, 9.0f, 1.0f, 0.12f, Rgb(236, 244, 255), 0.26f);
    s.seed = static_cast<float>(strike_seed++ % 97);
    AddStrike(s);
    AddStrike(MkCross(x, y, 16.0f, angle, 0.08f, Rgb(255, 250, 220), 0.18f));
    StrikeBurst(x, y, 16.0f, {255, 236, 170, 255}, 12, angle);
    StrikeShock(x, y, 0.18f, 0.12f);
}

void World::RiposteFx(float x, float y, float angle) {
    // The lunge home: a gold cross and a flash, bigger than any plain blow.
    AddStrike(MkCross(x, y, 30.0f, angle, 0.12f, Rgb(255, 214, 110), 0.34f));
    Strike s = MkImpact(x, y, 30.0f, 10.0f, 1.0f, 0.14f, Rgb(255, 228, 150), 0.34f);
    s.seed = static_cast<float>(strike_seed++ % 97);
    AddStrike(s);
    StrikeShock(x, y, 0.3f, 0.25f);
}

// --- the Whirlwind ----------------------------------------------------------------------

void World::WhirlFx(const ItemDef* weapon, float radius, bool last) {
    // In the Cross Cut's colour: the other turn on the spot.
    const SDL_FColor c = kColour[FamilyOf(weapon)][3];
    const SDL_FPoint g = player.GroundCentre();
    AddStrike(MkImpact(g.x, g.y, radius + 8.0f, 0.0f, 0.45f, 0.1f, c, 0.35f));
    StrikeBurst(g.x, g.y - 12.0f, radius, {230, 236, 255, 255}, last ? 14 : 7,
                static_cast<float>(strike_seed++ % 7) * 0.4f);
    if (last) {
        Strike again = MkImpact(g.x, g.y, radius + 22.0f, 0.0f, 0.45f, 0.08f, c, 0.45f);
        again.delay = 0.08f;
        AddStrike(again);
        StrikeShock(g.x, g.y, 0.3f, 0.2f);
    }
}

// --- the swing itself, drawn by the shader -----------------------------------------------

bool World::DrawSwingShaded(SDL_Renderer* r, float cx, float cy, float base, float half, float reach, float sweep,
                            float alpha, bool thrust) const {
    if (!Shaders::Effects()) return false;
    const AttackState& atk = player.Attack();
    // A Whirlwind is drawn in the Cross Cut's colour, the other turn on the spot.
    const int m = atk.Whirling() ? 3 : MoveIndex(atk.move);
    const Family f = FamilyOf(player.equipment.Weapon());
    SDL_FColor c = m >= 0 ? kColour[f][m] : SDL_FColor{1.0f, 0.96f, 0.8f, 1.0f};
    if (atk.riposte) c = Rgb(255, 214, 110);
    c.a = std::clamp(alpha, 0.0f, 1.0f);
    Shaders::ShapeFx fx;
    fx.colour = c;
    fx.seed = static_cast<float>(static_cast<int>(atk.timer * 10.0f) % 7);
    if (thrust || (f == DAGGER && atk.move == ComboMove::Crush) || atk.riposte) {
        // A thrust: a line out along the facing, as far as the swing has got.
        fx.shape = Shaders::SHAPE_THRUST;
        fx.hit_x = base;
        fx.hit_y = std::max(0.08f, sweep);
        fx.hit_age = atk.riposte ? 0.14f : 0.09f;
        fx.extra = 0.05f;
    } else {
        fx.shape = Shaders::SHAPE_SLASH;
        // A backhand comes back the other way, and so does a Whirlwind: the
        // spin turns anticlockwise on the screen (see World::DrawSwing).
        const bool back = atk.move == ComboMove::Backhand || atk.Whirling();
        fx.hit_x = back ? base + half : base - half;
        fx.hit_y = back ? -2.0f * half : 2.0f * half;
        fx.hit_age = sweep;
        // Thicker for the heavy blows, thinnest for the quick ones.
        float thick = 0.16f;
        switch (atk.move) {
            case ComboMove::Crush:    thick = 0.26f; break;
            case ComboMove::Cleave:   thick = 0.32f; break;
            case ComboMove::Backhand: thick = 0.14f; break;
            case ComboMove::CrossCut: thick = 0.24f; break;
            default: break;
        }
        if (atk.Whirling()) thick = 0.24f;
        if (f == GREAT || f == AXE) thick *= 1.2f;
        fx.extra = thick;
    }
    const float rad = reach + 6.0f;
    const SDL_FRect dst = camera.ToScreenRect({cx - rad, cy - rad, rad * 2.0f, rad * 2.0f});
    return Shaders::DrawShape(r, dst, fx, SDL_BLENDMODE_ADD);
}
