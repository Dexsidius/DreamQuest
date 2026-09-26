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

// Light is added to what is under it; a thing is laid over it. Splinters, a
// mark and a rune are things: added to grass, blood came out yellow and the
// Hunter's red reticle gold.
SDL_BlendMode StrikeBlend(Shaders::Shape shape) {
    return shape == Shaders::SHAPE_SHARDS || shape == Shaders::SHAPE_RETICLE || shape == Shaders::SHAPE_SIGIL
               ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_ADD;
}

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
        Shaders::DrawShape(r, dst, fx, StrikeBlend(s.shape));
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

// The techniques' and abilities' shapes (fx.frag 9 to 16). Each runs its first
// number 0..1 over its life, but the streak and the wave, which run their
// second and third.
World::Strike MkShape(Shaders::Shape shape, float x, float y, float radius, float p0, float p1, float p2, float p3,
                      int grows, SDL_FColor c, float life) {
    World::Strike s;
    s.shape = shape;
    s.x = x; s.y = y; s.radius = radius;
    s.p[0] = p0; s.p[1] = p1; s.p[2] = p2; s.p[3] = p3;
    s.grows = grows; s.grows_from = 0.0f; s.grows_to = 1.0f;
    s.colour = c; s.life = life;
    return s;
}
// Arms wheeling round; `arms` negative turns the other way; `flow` below
// nought draws them in rather than spreading out.
World::Strike MkVortex(float x, float y, float radius, float arms, float squash, float flow, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_VORTEX, x, y, radius, 0, arms, squash, flow, 0, c, life);
}
// The ground breaking: `heat` 0..1 is how white-hot the fissures start.
World::Strike MkCracks(float x, float y, float radius, float count, float squash, float heat, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_CRACKS, x, y, radius, 0, count, squash, heat, 0, c, life);
}
// A column of light on the ground point: the quad is lifted so the pillar's
// foot, seven tenths of the way down it, stands where it was put.
World::Strike MkPillar(float x, float y, float radius, float width, float motes, SDL_FColor c, float life) {
    World::Strike s = MkShape(Shaders::SHAPE_PILLAR, x, y, radius, 0, width, 0.45f, motes, 0, c, life);
    s.lift = radius * 0.7f;
    return s;
}
World::Strike MkSigil(float x, float y, float radius, float points, float squash, float spin, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_SIGIL, x, y, radius, 0, points, squash, spin, 0, c, life);
}
// Speed lines along `angle`, the quad centred on the middle of them.
World::Strike MkStreak(float x, float y, float radius, float angle, float width, float length, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_STREAK, x, y, radius, angle, 0, width, length, 1, c, life);
}
World::Strike MkReticle(float x, float y, float radius, float brackets, float squash, float spin, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_RETICLE, x, y, radius, 0, brackets, squash, spin, 0, c, life);
}
World::Strike MkShards(float x, float y, float radius, float count, float squash, float size, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_SHARDS, x, y, radius, 0, count, squash, size, 0, c, life);
}
// A front of force along `angle`, `half` radians either side (pi a ring).
World::Strike MkWave(float x, float y, float radius, float angle, float half, float thick, SDL_FColor c, float life) {
    return MkShape(Shaders::SHAPE_WAVE, x, y, radius, angle, half, 0, thick, 2, c, life);
}
}   // namespace

namespace {

SDL_FColor F(SDL_Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}; }
// An element's colour, or `none` for no element.
SDL_FColor ElementF(Element e, SDL_FColor none) { return e == Element::None ? none : F(ElementColor(e)); }

// What a plain blow of each family leaves: the crescent's own pale gold for a
// sword, steel for a dagger, bronze for a mace, and warmer for the big two.
const SDL_FColor kPlain[FAMILIES] = {
    Rgb(255, 244, 200), Rgb(226, 236, 250), Rgb(240, 214, 170), Rgb(255, 234, 180), Rgb(255, 180, 120),
};

const SDL_FColor kGold   = Rgb(255, 214, 110);
const SDL_FColor kEarth  = Rgb(214, 180, 120);
const SDL_FColor kBlood  = Rgb(200, 30, 36);
const SDL_FColor kSteel  = Rgb(176, 204, 236);
const SDL_FColor kViolet = Rgb(190, 170, 255);
const SDL_FColor kMana   = Rgb(130, 170, 255);
const SDL_FColor kFrenzy = Rgb(255, 110, 80);
const SDL_FColor kHunt   = Rgb(255, 96, 96);
const SDL_FColor kQuick  = Rgb(170, 240, 170);
const SDL_FColor kIron   = Rgb(170, 176, 186);
const SDL_FColor kWhite  = Rgb(240, 244, 255);
constexpr float kGround = 0.5f;          // how flat a thing lying on the ground is drawn

World::Strike Delayed(World::Strike s, float delay) { s.delay = delay; return s; }

World::Strike Lifted(World::Strike s, float lift) { s.lift += lift; return s; }

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
                AddStrike(Delayed(MkShards(fx, fy - 4.0f, heavy ? 32.0f : 22.0f, heavy ? 12.0f : 8.0f, kGround, 0.9f,
                                           Rgb(170, 150, 120), 0.45f), 0.03f));
                // The big two bring it down hard enough to break the ground open.
                if (heavy) AddStrike(Delayed(MkCracks(fx, fy, 50.0f, 8.0f, kGround, 1.0f, c, 0.7f), 0.03f));
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
            if (f == DAGGER) {
                // Flurry: two more cuts crossing the first, quick as it is.
                for (int k = 1; k <= 2; ++k)
                    AddStrike(Delayed(MkSlash(g.x, g.y - 16.0f, reach + 10.0f, face + half * (k == 1 ? 0.7f : -0.9f),
                                              (k == 1 ? -1.4f : 1.4f) * half, 0.12f, c, 0.2f), 0.07f * k));
            } else if (f == MACE) {
                // Sweeping Blow: low, and a wave of grit off the floor with it.
                AddStrike(Delayed(MkWave(g.x, g.y, reach * 1.5f, face, half, 0.11f, kEarth, 0.35f), 0.05f));
            } else if (f == GREAT) {
                // Reaping Sweep: red on the wind of it.
                AddStrike(Delayed(MkSlash(g.x, g.y - 16.0f, reach + 30.0f, face - half, 2.0f * half, 0.06f, kBlood, 0.34f), 0.1f));
            } else if (f == AXE) {
                // Felling Sweep: the blow's weight along the ground after it.
                AddStrike(Delayed(MkWave(g.x, g.y, reach * 1.6f, face, half, 0.13f, c, 0.4f), 0.06f));
            }
            break;
        }
        case ComboMove::Backhand: {
            // A snap: a flash where the swing turns back.
            Strike s = MkImpact(g.x + cosf(face) * 14.0f, g.y - 18.0f + sinf(face) * 10.0f, 16.0f, 5.0f, 1.0f, 0.0f, c, 0.16f);
            s.seed = seed;
            AddStrike(s);
            if (f == GREAT || f == AXE || f == MACE) {
                // Pommel Strike, Haft Check, Backswing: blunt, and it shoves.
                AddStrike(Delayed(MkWave(g.x, g.y, reach * 1.2f, face, 0.55f, 0.12f, c, 0.3f), 0.03f));
            } else if (f == BLADE) {
                // Backhand: the wind of the cut going back the other way.
                const float half = std::max(0.6f, player.Attack().profile.HalfAngle(reach));
                AddStrike(Delayed(MkSlash(g.x, g.y - 16.0f, reach + 14.0f, face + half, -2.0f * half, 0.08f, c, 0.22f), 0.03f));
            }
            break;
        }
        case ComboMove::CrossCut:
            if (f == DAGGER) {
                // Fan of Steel: blades flung out all round, a ring of glints.
                Strike s = MkImpact(g.x, g.y - 12.0f, reach + 16.0f, 14.0f, 0.7f, 0.08f, c, 0.35f);
                s.seed = seed;
                AddStrike(s);
                AddStrike(MkShards(g.x, g.y - 14.0f, reach + 10.0f, 16.0f, 0.7f, 0.8f, Rgb(230, 240, 255), 0.45f));
            } else if (f == MACE) {
                // Ground Slam: the floor itself goes out from under everything.
                Strike s = MkImpact(g.x, g.y, reach * 1.3f, 12.0f, 0.4f, 0.22f, c, 0.55f);
                s.seed = seed;
                AddStrike(s);
                StrikeShock(g.x, g.y, 0.6f, 0.45f);
                AddStrike(MkCracks(g.x, g.y, reach * 1.25f, 9.0f, kGround, 1.0f, c, 0.8f));
                AddStrike(MkShards(g.x, g.y - 4.0f, reach, 14.0f, kGround, 1.0f, Rgb(170, 150, 120), 0.55f));
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
                // The air dragged round with it: a quick swirl for a blade, the
                // greatsword's Whirlwind a wide one, the greataxe's Maelstrom
                // red and spitting.
                const float swirl = f == GREAT || f == AXE ? reach + 16.0f : reach;
                AddStrike(MkVortex(g.x, g.y, swirl, 3.0f, kGround, 0.6f, f == AXE ? kBlood : c, 0.45f));
                if (f == AXE) AddStrike(MkShards(g.x, g.y - 12.0f, reach + 12.0f, 12.0f, 0.6f, 0.8f, kBlood, 0.45f));
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
                AddStrike(MkShards(ex + cosf(face) * 6.0f, ey, 20.0f, 9.0f, 0.8f, 0.7f, F(blood), 0.4f));
            } else if (f == MACE) {
                // Skull Crack: stars round the head it rang.
                Strike s = MkImpact(ex, ey - 16.0f, 22.0f, 5.0f, 0.5f, 0.12f, c, 0.45f);
                s.seed = seed;
                AddStrike(s);
            } else {
                // The big two bring it down hard enough to break the ground
                // under what they hit; Hew opens it up.
                const bool big = f == GREAT || f == AXE;
                Strike s = MkImpact(ex, ey, big ? 28.0f : 22.0f, 8.0f, 1.0f, big ? 0.12f : 0.0f, c, 0.22f);
                s.seed = seed;
                AddStrike(s);
                if (big) AddStrike(MkCracks(e.x, e.y, 26.0f, 7.0f, kGround, 0.9f, c, 0.6f));
                if (f == AXE) AddStrike(MkShards(ex, ey, 22.0f, 9.0f, 0.8f, 0.8f, F(blood), 0.4f));
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
            } else if (f == MACE) {
                // Sweeping Blow: swept off its feet, not cut -- a flash and
                // grit thrown up where it was knocked.
                Strike s = MkImpact(ex, ey, 22.0f, 7.0f, 0.8f, 0.1f, c, 0.25f);
                s.seed = seed;
                AddStrike(s);
                AddStrike(MkShards(e.x, e.y - 4.0f, 22.0f, 8.0f, kGround, 0.8f, kEarth, 0.4f));
            } else {
                // A cut laid across it along the swing, and whatever it bled:
                // the Reaping Sweep a spray of it.
                Strike s = MkSlash(ex, ey, 22.0f, face - 2.2f, 2.0f, 0.24f, c, 0.2f);
                AddStrike(s);
                if (f == GREAT || f == AXE)
                    AddStrike(MkShards(ex, ey, f == GREAT ? 28.0f : 22.0f, f == GREAT ? 12.0f : 8.0f, 0.8f, 0.8f, F(blood), 0.45f));
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
            } else if (f == MACE) {
                // Backswing: the head coming back rings it too.
                Strike s = MkImpact(ex, ey - 16.0f, 20.0f, 5.0f, 0.5f, 0.12f, c, 0.4f);
                s.seed = seed;
                AddStrike(s);
                AddStrike(MkImpact(ex, ey, 16.0f, 6.0f, 1.0f, 0.0f, c, 0.15f));
            } else {
                // Backhand: a cut back across the way the blade came, and the
                // sparks off it.
                AddStrike(MkSlash(ex, ey, 18.0f, face + 2.2f, -1.8f, 0.2f, c, 0.18f));
                Strike s = MkImpact(ex, ey, 16.0f, 6.0f, 1.0f, 0.0f, c, 0.15f);
                s.seed = seed;
                AddStrike(s);
                AddStrike(MkShards(ex, ey, 18.0f, 6.0f, 1.0f, 0.6f, c, 0.3f));
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
                if (f == AXE) AddStrike(MkShards(ex, ey, 22.0f, 9.0f, 0.8f, 0.8f, F(blood), 0.4f));
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
            // The push of it, out ahead the way the bolt goes.
            AddStrike(MkWave(x, y, 48.0f, angle, 0.5f, 0.1f, c, 0.3f));
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
            // The Barbed Shot bleeds: red splinters laid over what it hit, as
            // every other blow's blood is -- Burst's blobs were added to the
            // grass behind and came out yellow.
            if (style == AttackStyle::Ranged)
                AddStrike(MkShards(x + cosf(angle) * 6.0f, y + sinf(angle) * 6.0f, 20.0f, 9.0f, 0.8f, 0.7f, Rgb(190, 20, 30), 0.4f));
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
    // The air dragged round with it, the way the spin turns, and the sparks
    // off the edge -- which were Burst's blobs, and read as coins.
    AddStrike(MkVortex(g.x, g.y, radius, 3.0f, kGround, 0.6f, c, player.Attack().turn * 1.1f));
    AddStrike(MkShards(g.x, g.y - 12.0f, radius, last ? 14.0f : 8.0f, 0.55f, 0.7f, kWhite, 0.4f));
    if (last) {
        AddStrike(Delayed(MkWave(g.x, g.y, radius * 1.4f, 0.0f, 3.2f, 0.1f, c, 0.5f), 0.05f));
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

// =============================================================================
//  The rest of a fight: plain swings, the leap, the techniques, the abilities
// =============================================================================
//
// The combos had marks of their own and nothing else did: a plain swing was
// its crescent, the Rushing Strike was nothing at all, and every ability was a
// ring of Burst's blobs and a word over the head. The shapes these use are the
// techniques' and abilities' own (fx.frag 9 to 16): a vortex, cracks, a pillar
// of light, a sigil, speed lines, a reticle, shards and a wave. Like the
// combos', they go to friends through the strike log.


// --- plain swings ------------------------------------------------------------------

void World::SwingFx(const ItemDef* weapon) {
    // The finisher, a strong and a charged blow each leave the wind of their
    // swing -- the crescent is the swinger's own screen's alone -- and a
    // charged blow a push of air along it. The first two links of a chain are
    // quick and many: they leave only what they strike.
    const AttackState& atk = player.Attack();
    const bool finisher = atk.type == AttackType::Light && atk.combo >= 2;
    if (!finisher && atk.type != AttackType::Strong && atk.type != AttackType::Charged) return;
    const Family f = FamilyOf(weapon);
    const SDL_FColor c = kPlain[f];
    const float face = FacingAngle(player.facing);
    const SDL_FPoint g = player.GroundCentre();
    const float reach = std::max(24.0f, atk.profile.reach * atk.reach_scale);
    const float half = std::max(0.5f, atk.profile.HalfAngle(reach));
    const bool charged = atk.type == AttackType::Charged;
    const float thick = charged ? 0.16f : atk.type == AttackType::Strong ? 0.12f : 0.08f;
    if (player.AttackClip() == "thrust") {
        AddStrike(Delayed(MkThrust(g.x, g.y - 14.0f, reach + 12.0f, face, thick, 0.1f, c, 0.2f), 0.03f));
    } else {
        AddStrike(Delayed(MkSlash(g.x, g.y - 16.0f, reach + 12.0f, face - half, 2.0f * half, thick, c, 0.24f), 0.04f));
    }
    if (charged) AddStrike(Delayed(MkWave(g.x, g.y, reach * 1.4f, face, half, 0.1f, c, 0.35f), 0.06f));
}

void World::SwingHitFx(const ItemDef* weapon, const Enemy& e) {
    const AttackState& atk = player.Attack();
    const Family f = FamilyOf(weapon);
    const SDL_FColor c = kPlain[f];
    const float ex = e.x, ey = e.y - 18.0f;
    const float seed = static_cast<float>(strike_seed++ % 97);
    const bool heavy = f == GREAT || f == AXE;
    World::Strike flash;
    if (atk.type == AttackType::Light && atk.combo < 2) {
        // A quick cut: a spark where it bites.
        flash = MkImpact(ex, ey, heavy ? 14.0f : 10.0f, 5.0f, 1.0f, 0.0f, c, 0.14f);
    } else if (atk.type == AttackType::Light) {
        // The finisher: a flash with a ring, and splinters off what it hit.
        flash = MkImpact(ex, ey, 18.0f, 8.0f, 1.0f, 0.1f, c, 0.22f);
        AddStrike(MkShards(ex, ey, 22.0f, 7.0f, 0.8f, 0.8f, c, 0.35f));
        StrikeShock(ex, ey, 0.15f, 0.1f);
    } else if (atk.type == AttackType::Strong) {
        flash = MkImpact(ex, ey, 22.0f, 7.0f, 1.0f, 0.12f, c, 0.26f);
        AddStrike(MkShards(ex, ey, 24.0f, 6.0f, 0.8f, 1.0f, c, 0.35f));
        StrikeShock(ex, ey, 0.2f, 0.12f);
    } else {
        // Charged: the blow drives into the ground under what it hit.
        flash = MkImpact(ex, ey, 28.0f, 10.0f, 1.0f, 0.16f, c, 0.3f);
        AddStrike(MkCracks(e.x, e.y, 24.0f, 6.0f, kGround, 0.6f, c, 0.55f));
        StrikeShock(ex, ey, 0.3f, 0.2f);
    }
    flash.seed = seed;
    AddStrike(flash);
}

// --- the leap -------------------------------------------------------------------------

void World::RushFx(const ItemDef* weapon) {
    // Speed lines down the way it came, and the blow at the end of it: the
    // Rushing Strike drew nothing at all, not even a crescent.
    const Family f = FamilyOf(weapon);
    const SDL_FColor c = kPlain[f];
    const Vec2 d = player.RushDirection();
    const float angle = atan2f(d.y, d.x);
    const SDL_FPoint g = player.GroundCentre();
    const float span = Player::RUSH_DISTANCE;
    AddStrike(MkStreak(g.x - d.x * span * 0.45f, g.y - 12.0f - d.y * span * 0.45f, span * 0.62f, angle, 0.05f, 1.7f, c, 0.32f));
    AddStrike(MkImpact(g.x + d.x * 16.0f, g.y - 16.0f + d.y * 10.0f, 26.0f, 9.0f, 0.9f, 0.14f, c, 0.3f));
    AddStrike(Delayed(MkWave(g.x, g.y, 46.0f, angle, 0.7f, 0.1f, c, 0.3f), 0.03f));
    StrikeShock(g.x + d.x * 16.0f, g.y, 0.25f, 0.18f);
}

// --- the melee techniques ------------------------------------------------------------------

void World::SlamFx(float radius) {
    // The Ground Slam: the ground breaking open round the feet, white-hot at
    // first, a ring and a wave running out over it, and the stones thrown up.
    const SDL_FPoint g = player.GroundCentre();
    AddStrike(MkCracks(g.x, g.y, radius * 1.1f, 9.0f, kGround, 1.0f, kEarth, 0.95f));
    AddStrike(MkImpact(g.x, g.y, radius, 0.0f, 0.45f, 0.14f, kEarth, 0.45f));
    AddStrike(Delayed(MkWave(g.x, g.y, radius * 1.35f, 0.0f, 3.2f, 0.1f, kEarth, 0.55f), 0.05f));
    AddStrike(MkShards(g.x, g.y - 6.0f, radius * 0.9f, 14.0f, kGround, 1.1f, Rgb(170, 150, 120), 0.6f));
    StrikeShock(g.x, g.y, 0.55f, 0.4f);
}

void World::LungeFx(const ItemDef* weapon, float reach) {
    // Speed lines out ahead along the line it carries you down, the thrust
    // at the end of it as far as it really reaches, and the blow landing there.
    const SDL_FColor c = kPlain[FamilyOf(weapon)];
    const float face = FacingAngle(player.facing);
    const float dx = cosf(face), dy = sinf(face);
    const SDL_FPoint g = player.GroundCentre();
    AddStrike(MkStreak(g.x + dx * reach * 0.5f, g.y - 12.0f + dy * reach * 0.5f, reach * 0.62f, face, 0.05f, 1.7f, c, 0.35f));
    AddStrike(MkThrust(g.x, g.y - 14.0f, reach, face, 0.12f, 0.1f, c, 0.25f));
    AddStrike(Delayed(MkImpact(g.x + dx * reach, g.y - 14.0f + dy * reach, 22.0f, 8.0f, 0.7f, 0.1f, c, 0.25f), 0.08f));
}

// --- the shots and casts of the techniques --------------------------------------------------

void World::ShotTechniqueFx(const string& technique, AttackStyle style, Element element, float mx, float my,
                            float angle, float tx, float ty, const Enemy* target) {
    const SDL_FColor c = style == AttackStyle::Ranged ? kGold : ElementF(element, kViolet);
    const SDL_FPoint g = player.GroundCentre();
    const float dx = cosf(angle), dy = sinf(angle);
    if (technique == "piercing_shot") {
        // Speed lines down the whole line it will go, the arrow's thrust, a
        // ring where it left the string.
        AddStrike(MkStreak(mx + dx * 46.0f, my + dy * 46.0f, 52.0f, angle, 0.04f, 1.8f, c, 0.3f));
        AddStrike(MkThrust(mx, my, 60.0f, angle, 0.08f, 0.1f, c, 0.2f));
        AddStrike(MkImpact(mx, my, 16.0f, 0.0f, 1.0f, 0.14f, c, 0.25f));
        StrikeShock(mx, my, 0.2f, 0.12f);
    } else if (technique == "volley") {
        // A fan of force as wide as the five arrows spread.
        AddStrike(MkWave(mx, my, 72.0f, angle, 0.4f, 0.07f, c, 0.35f));
        AddStrike(MkImpact(mx, my, 14.0f, 6.0f, 1.0f, 0.0f, c, 0.16f));
    } else if (technique == "arrow_rain") {
        // Where it will come down: a circle turning on the ground there for
        // as long as the rain lasts, and a mark closing on the middle of it.
        AddStrike(MkSigil(tx, ty, GroundEffect::RAIN_RADIUS * 1.05f, 0.0f, kGround, 1.2f, kSteel, 2.6f));
        AddStrike(MkReticle(tx, ty, GroundEffect::RAIN_RADIUS * 0.8f, 4.0f, kGround, 2.0f, kSteel, 0.6f));
        AddStrike(MkWave(mx, my, 40.0f, -1.5707963f, 0.5f, 0.1f, c, 0.3f));
    } else if (technique == "nova") {
        AddStrike(MkSigil(g.x, g.y, 34.0f, 8.0f, kGround, 2.0f, c, 0.7f));
        AddStrike(MkWave(g.x, g.y, 96.0f, 0.0f, 3.2f, 0.09f, c, 0.5f));
        AddStrike(MkVortex(g.x, g.y, 40.0f, 4.0f, kGround, 1.0f, c, 0.5f));
    } else if (technique == "barrage") {
        AddStrike(MkSigil(g.x, g.y, 26.0f, 4.0f, kGround, 3.0f, c, 0.5f));
        AddStrike(MkImpact(mx, my, 14.0f, 6.0f, 1.0f, 0.1f, c, 0.2f));
        if (target) AddStrike(Lifted(MkReticle(target->x, target->y, 22.0f, 4.0f, 1.0f, 3.0f, c, 0.9f), 20.0f));
    } else if (technique == "meteor") {
        // Where the stone will fall, for as long as it takes: a pentagram
        // turning on the ground and a mark closing on it.
        AddStrike(MkSigil(tx, ty, 62.0f, 5.0f, kGround, -1.2f, c, 1.1f));
        AddStrike(MkReticle(tx, ty, 52.0f, 4.0f, kGround, 2.0f, c, 0.9f));
        AddStrike(MkPillar(g.x, g.y, 36.0f, 0.1f, 0.6f, c, 0.45f));
    }
}

void World::TechniqueShotHitFx(uint8_t kind, Element element, float x, float y, float angle) {
    const float seed = static_cast<float>(strike_seed++ % 97);
    World::Strike s;
    switch (kind) {
        case 1: {   // Piercing Shot: a hole punched through, splinters out the far side
            const SDL_FColor c = kGold;
            s = MkImpact(x, y, 18.0f, 7.0f, 1.0f, 0.1f, c, 0.22f);
            AddStrike(MkThrust(x, y, 30.0f, angle, 0.08f, 0.0f, c, 0.2f));
            AddStrike(MkShards(x + cosf(angle) * 8.0f, y + sinf(angle) * 8.0f, 22.0f, 6.0f, 1.0f, 0.7f, c, 0.3f));
            break;
        }
        case 2: s = MkImpact(x, y, 12.0f, 5.0f, 1.0f, 0.0f, kGold, 0.16f); break;
        case 3: s = MkImpact(x, y, 18.0f, 0.0f, 1.0f, 0.12f, ElementF(element, kViolet), 0.25f); break;
        case 4: s = MkCross(x, y, 14.0f, angle, 0.1f, ElementF(element, kViolet), 0.22f); break;
        default: return;
    }
    s.seed = seed;
    AddStrike(s);
}

void World::MeteorLandFx(float x, float y, float size, Element element) {
    const SDL_FColor c = ElementF(element, Rgb(255, 150, 80));
    const float radius = size * 0.55f;
    AddStrike(MkCracks(x, y, radius * 1.2f, 11.0f, kGround, 1.0f, c, 1.1f));
    AddStrike(MkWave(x, y, radius * 1.6f, 0.0f, 3.2f, 0.12f, c, 0.6f));
    AddStrike(MkShards(x, y - 8.0f, radius, 16.0f, kGround, 1.2f, c, 0.7f));
}

// --- the abilities ------------------------------------------------------------------------

void World::AbilityFx(const string& ability, Element element, const Enemy* target) {
    const SDL_FPoint g = player.GroundCentre();
    const float px = g.x, py = g.y;
    const float face = FacingAngle(player.facing);
    const SDL_FColor el = ElementF(element, kViolet);
    if (ability == "sunder" && target) {
        // Straps and seams: the armour flying off in pieces, the blow, and the
        // ground cracking under what took it.
        const float tx = target->x, ty = target->y;
        AddStrike(MkShards(tx, ty - 20.0f, 32.0f, 12.0f, 0.8f, 1.1f, Rgb(214, 190, 150), 0.6f));
        AddStrike(MkImpact(tx, ty - 18.0f, 28.0f, 9.0f, 1.0f, 0.14f, Rgb(255, 190, 110), 0.35f));
        AddStrike(MkCracks(tx, ty, 26.0f, 6.0f, kGround, 0.8f, Rgb(255, 190, 110), 0.6f));
        StrikeShock(tx, ty, 0.3f, 0.2f);
    } else if (ability == "hunters_mark" && target) {
        // A mark that closes on it, and a thread of light straight up from it.
        AddStrike(Lifted(MkReticle(target->x, target->y, 26.0f, 4.0f, 1.0f, 2.0f, kHunt, 1.0f), 20.0f));
        AddStrike(MkPillar(target->x, target->y, 46.0f, 0.1f, 0.6f, kHunt, 0.8f));
    } else if (ability == "war_cry") {
        // The shout: rings of it going out one after another, and the light
        // of it standing up off the one who shouted.
        for (int i = 0; i < 3; ++i)
            AddStrike(Delayed(MkWave(px, py, 112.0f, 0.0f, 3.2f, 0.08f, kGold, 0.6f), 0.12f * i));
        AddStrike(MkPillar(px, py, 52.0f, 0.16f, 0.8f, kGold, 0.6f));
        StrikeShock(px, py, 0.4f, 0.25f);
    } else if (ability == "bash") {
        // A shove: a short push of air ahead, and the flash where it lands.
        const float fx = cosf(face), fy = sinf(face);
        AddStrike(MkWave(px, py, 54.0f, face, 0.55f, 0.1f, kWhite, 0.3f));
        AddStrike(MkImpact(px + fx * 24.0f, py - 14.0f + fy * 16.0f, 24.0f, 8.0f, 1.0f, 0.12f, kWhite, 0.25f));
    } else if (ability == "caltrops") {
        // Iron scattered at the feet.
        AddStrike(MkShards(px, py, 48.0f, 16.0f, kGround, 0.9f, kIron, 0.5f));
        AddStrike(MkImpact(px, py, 46.0f, 0.0f, kGround, 0.08f, kIron, 0.4f));
    } else if (ability == "arcane_pulse") {
        // Ten bolts: a ten-pointed star under the feet, ten sparks of the
        // element thrown out with them, and the ring they go out on. Not the
        // Nova's swirl drawn in first -- the two are both a ring of spells,
        // and were one look.
        AddStrike(MkSigil(px, py, 40.0f, 10.0f, kGround, 3.0f, el, 0.55f));
        AddStrike(MkShards(px, py - 12.0f, 46.0f, 10.0f, kGround, 1.0f, el, 0.45f));
        AddStrike(MkWave(px, py, 100.0f, 0.0f, 3.2f, 0.06f, el, 0.45f));
    } else if (ability == "blink") {
        // Gone from where they were -- drawn in to a point -- and there, with a
        // line of light between.
        const SDL_FPoint from = player.AbilityFrom();
        const float dx = px - from.x, dy = py - from.y;
        const float dist = std::max(12.0f, Length(dx, dy));
        const float ang = atan2f(dy, dx);
        AddStrike(MkVortex(from.x, from.y, 30.0f, 4.0f, kGround, -1.0f, kViolet, 0.45f));
        AddStrike(MkImpact(from.x, from.y - 14.0f, 24.0f, 0.0f, 1.0f, 0.14f, kViolet, 0.3f));
        AddStrike(MkStreak((from.x + px) * 0.5f, (from.y + py) * 0.5f - 14.0f, dist * 0.55f + 10.0f, ang, 0.05f, 1.8f, kViolet, 0.3f));
        AddStrike(Delayed(MkImpact(px, py - 14.0f, 26.0f, 8.0f, 1.0f, 0.12f, kViolet, 0.3f), 0.04f));
    } else if (ability == "tumble") {
        // Speed lines down the roll.
        const float kx = player.knock_x, ky = player.knock_y;
        if (Length(kx, ky) > 1.0f) {
            const float ang = atan2f(ky, kx);
            const float dist = Player::TUMBLE_SPEED / 9.0f;
            AddStrike(MkStreak(px + cosf(ang) * dist * 0.5f, py - 10.0f + sinf(ang) * dist * 0.5f, dist * 0.62f, ang,
                               0.08f, 1.6f, Rgb(236, 226, 200), 0.4f));
            // The ground kicked back where it pushed off.
            AddStrike(MkWave(px, py, 34.0f, ang + 3.14159265f, 0.8f, 0.12f, Rgb(214, 196, 160), 0.3f));
        }
    } else if (ability == "mana_shield") {
        AddStrike(MkSigil(px, py, 30.0f, 6.0f, kGround, 2.0f, kMana, 0.7f));
    } else if (ability == "frenzy") {
        AddStrike(MkVortex(px, py, 34.0f, 3.0f, kGround, 1.0f, kFrenzy, 0.6f));
        AddStrike(MkPillar(px, py, 40.0f, 0.14f, 0.7f, kFrenzy, 0.5f));
        StrikeShock(px, py, 0.2f, 0.12f);
    } else if (ability == "shockwave") {
        // A line of force down the corridor it strikes: a wave and the ground
        // breaking under it every step of the way, one after the other.
        const float fx = cosf(face), fy = sinf(face);
        AddStrike(MkImpact(px + fx * 14.0f, py + fy * 10.0f, 30.0f, 10.0f, 0.6f, 0.14f, kEarth, 0.3f));
        // The line itself, the length of it at once, and then the ground going
        // up along it a step at a time, the last the hardest.
        AddStrike(MkStreak(px + fx * 95.0f, py - 4.0f + fy * 95.0f, 104.0f, face, 0.09f, 1.8f, kEarth, 0.45f));
        for (int i = 0; i < 5; ++i) {
            const float d = 30.0f + 38.0f * i;
            const float delay = 0.05f * i;
            const bool last = i == 4;
            AddStrike(Delayed(MkWave(px + fx * d, py + fy * d, last ? 70.0f : 58.0f, face, 0.55f, 0.16f, kEarth, 0.4f), delay));
            AddStrike(Delayed(MkCracks(px + fx * d, py + fy * d, last ? 44.0f : 34.0f, 6.0f, kGround, 0.9f, kEarth, 0.7f), delay));
            AddStrike(Delayed(MkShards(px + fx * d, py - 6.0f + fy * d, 26.0f, 6.0f, kGround, 0.9f, Rgb(170, 150, 120), 0.45f), delay));
        }
        StrikeShock(px, py, 0.45f, 0.35f);
    } else if (ability == "stand_fast") {
        // Feet set: a square sigil, and the ground taking the weight.
        AddStrike(MkSigil(px, py, 32.0f, 4.0f, kGround, 0.8f, kSteel, 0.9f));
        AddStrike(MkCracks(px, py, 24.0f, 6.0f, kGround, 0.5f, kSteel, 0.6f));
        StrikeShock(px, py, 0.3f, 0.2f);
    } else if (ability == "take_aim") {
        AddStrike(Lifted(MkReticle(px, py, 18.0f, 4.0f, 1.0f, 2.0f, kGold, 0.7f), 40.0f));
        AddStrike(MkPillar(px, py, 36.0f, 0.06f, 0.4f, kGold, 0.45f));
    } else if (ability == "rapid_fire") {
        AddStrike(MkSigil(px, py, 28.0f, 3.0f, kGround, 5.0f, kQuick, 0.6f));
        AddStrike(MkWave(px, py, 60.0f, 0.0f, 3.2f, 0.07f, kQuick, 0.4f));
    } else if (ability == "snare") {
        // The trap's rune, laid where it is set.
        AddStrike(MkSigil(px, py, 24.0f, 3.0f, kGround, 1.5f, Rgb(200, 190, 160), 1.0f));
    } else if (ability == "overload") {
        // Power drawn up into the hands: in to the middle, and up.
        AddStrike(MkPillar(px, py, 44.0f, 0.14f, 1.0f, kViolet, 0.6f));
        AddStrike(MkVortex(px, py, 30.0f, 4.0f, kGround, -1.0f, kViolet, 0.6f));
    } else if (ability == "invoke") {
        AddStrike(MkVortex(px, py, 44.0f, 5.0f, kGround, -1.0f, kMana, 0.8f));
        AddStrike(MkSigil(px, py, 28.0f, 6.0f, kGround, -2.0f, kMana, 0.7f));
    } else if (ability == "repulse") {
        AddStrike(MkWave(px, py, 124.0f, 0.0f, 3.2f, 0.14f, el, 0.45f));
        AddStrike(MkImpact(px, py - 10.0f, 40.0f, 12.0f, 0.6f, 0.14f, el, 0.35f));
        StrikeShock(px, py, 0.5f, 0.35f);
    }
}

void World::DazeFx(const Enemy& e) {
    Strike stars = MkImpact(e.x, e.y - 34.0f, 20.0f, 5.0f, 0.5f, 0.12f, kWhite, 0.45f);
    stars.seed = static_cast<float>(strike_seed++ % 97);
    AddStrike(stars);
    AddStrike(MkShards(e.x, e.y - 18.0f, 20.0f, 6.0f, 0.8f, 0.7f, kWhite, 0.3f));
}

void World::SnareSprungFx(float x, float y, float radius) {
    // The jaws shutting: a cross where they bite, and the earth thrown up.
    const SDL_FColor c = Rgb(200, 190, 160);
    AddStrike(MkCross(x, y - 12.0f, radius, 0.0f, 0.14f, c, 0.3f));
    AddStrike(MkShards(x, y - 6.0f, radius * 1.2f, 9.0f, kGround, 0.9f, c, 0.45f));
    AddStrike(MkImpact(x, y, radius * 1.3f, 0.0f, kGround, 0.12f, c, 0.35f));
}

// --- the auras -----------------------------------------------------------------------------

void World::DrawAuras(SDL_Renderer* r) const {
    // Drawn every frame round whoever has one up, from the bits the character
    // carries -- a friend's from what the host said -- so it stays with them.
    if (!Shaders::Effects()) return;
    vector<const Player*> everyone{&player};
    for (const auto& g : guests) everyone.push_back(g.get());
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    for (const Player* who : everyone) {
        if (!who || who->IsDead() || who->under_ice) continue;
        const uint8_t buffs = who->Buffs();
        if (!buffs) continue;
        const float fx = who->x, fy = who->y - who->draw_lift;
        const auto draw = [&](uint8_t bit, Shaders::Shape shape, float radius, float lift, SDL_FColor c,
                              float strength, float p0, float p1, float p2, float p3) {
            if (!(buffs & bit)) return;
            const float left = who->BuffLeft(bit);
            Shaders::ShapeFx s;
            s.shape = shape;
            s.fade = left > 0.0f ? std::clamp(left / 0.5f, 0.0f, 1.0f) : 1.0f;
            s.seed = static_cast<float>(who->seat * 7 + bit);
            c.a = strength;
            s.colour = c;
            s.hit_x = p0; s.hit_y = p1; s.hit_age = p2; s.extra = p3;
            const float ly = fy - LiftAt(fx, fy) - lift;
            Shaders::DrawShape(r, camera.ToScreenRect({fx - radius, ly - radius, radius * 2.0f, radius * 2.0f}), s,
                               StrikeBlend(shape));
        };
        const float cycle = now - floorf(now);
        // Frenzy: the red swirl of it round the feet.
        draw(Player::BUFF_FRENZY, Shaders::SHAPE_VORTEX, 22.0f, 0.0f, kFrenzy, 0.55f, 1.0f, 3.0f, kGround, 0.4f);
        // Stand Fast: the square sigil it set, turning slowly.
        draw(Player::BUFF_STAND_FAST, Shaders::SHAPE_SIGIL, 24.0f, 0.0f, kSteel, 0.6f, 0.5f, 4.0f, kGround, 0.6f);
        // War Cry: the shout still going out of them, a ring at a time.
        draw(Player::BUFF_WAR_CRY, Shaders::SHAPE_WAVE, 30.0f, 0.0f, kGold, 0.5f, 0.0f, 3.2f, cycle, 0.07f);
        // Take Aim: the mark over their head, for the one shot it is for.
        draw(Player::BUFF_TAKE_AIM, Shaders::SHAPE_RETICLE, 10.0f, 46.0f, kGold, 0.8f, 1.0f, 4.0f, 1.0f, 2.0f);
        // Rapid Fire: a three-pointed sigil spinning fast.
        draw(Player::BUFF_RAPID_FIRE, Shaders::SHAPE_SIGIL, 20.0f, 0.0f, kQuick, 0.5f, 0.5f, 3.0f, kGround, 6.0f);
        // Overload: the charge held in the hands, an orb swirling at the chest.
        draw(Player::BUFF_OVERLOAD, Shaders::SHAPE_NODE, 7.0f, 20.0f, kViolet, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);
        // Invoke: mana drawn in to them from all round.
        draw(Player::BUFF_INVOKE, Shaders::SHAPE_VORTEX, 26.0f, 0.0f, kMana, 0.5f, cycle, 4.0f, kGround, -1.0f);
    }
}
