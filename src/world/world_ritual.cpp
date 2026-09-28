// -----------------------------------------------------------------------------
//  World, continued: a ritual at a witch table
//
//  Oona's poppet -- the reed doll the Mother of the Fen hung in the reeds,
//  bound again in new red thread -- laid on one of the witch tables out in the
//  Bayou. The candles catch on their own and a ring of fire comes up out of
//  the mud round the table: nothing crosses it, in or out, while it burns
//  (Map::SetRing). Then the fen comes for the doll through the flames, in
//  three waves of ten -- the third with the Mother of the Fen herself at its
//  head -- out of the map's ritual posts, which lie still until they are
//  called. When the last of the third is down the ring sinks into the mud and
//  the poppet is given back with one more of its five knots pulled tight. Its
//  owner falling, or leaving, breaks the ritual: the fire gutters, whatever
//  was still standing goes back into the fen, and the waves it had got
//  through are taken back.
//
//  The tables answer once a day (RitualDayFlag), and one at a time on a map:
//  the ritual posts are the map's, not a table's, and the ritual puts them
//  round whichever table has been lit.
//
//  The poppet never leaves its owner's bag. What is on the table is drawn
//  there, and a knot is added by changing which poppet is in the bag -- so a
//  save made halfway through a ritual, a death, a dropped line, a crash, can
//  none of them lose it.
//
//  The host's alone. A friend's window is told the ring as a RING patch and
//  draws it, and is held to it, from that (HearOfRing); the monsters it sees
//  are the map's own posts, posed from the host like any others.
// -----------------------------------------------------------------------------
#include "world.h"
#include "../systems/quest.h"
#include "../systems/audio.h"

namespace {

const SDL_Color kRitualFire{255, 150, 64, 255};
const SDL_Color kRitualSmoke{120, 120, 110, 255};

// A monster just inside the flames, at `k` of `n` round the ring: spread so
// they come from every side, and turned a little further each wave.
float ComeFrom(int k, int n, int wave) {
    const float step = 6.2831853f / std::max(1, n);
    return step * k + wave * 0.9f + 0.35f * static_cast<float>((k * 7 + wave * 3) % 5) / 5.0f;
}

} // namespace

string World::IdolId(int knots) {
    return knots <= 0 ? string("poppet_idol") : "poppet_idol_" + std::to_string(std::min(knots, IDOL_KNOTS));
}

int World::IdolKnots(const Inventory& bag) {
    for (int k = IDOL_KNOTS; k >= 0; --k)
        if (bag.Has(IdolId(k))) return k;
    return -1;
}

Player* World::RitualOwner() {
    if (!ritual.active) return nullptr;
    if (ritual.owner_host) return player.absent ? nullptr : &player;
    Player* g = Guest(ritual.owner_seat);
    return g && !g->puppet ? g : nullptr;
}

void World::CreditTo(Player& who, const QuestEvent& e) {
    if (&who == &player && !Acting()) {
        if (host_quests && !player.absent) host_quests->Notify(e, player.inventory);
        return;
    }
    SeatState& seat = seat_states[who.seat];
    (seat.own_journal ? *seat.own_journal : seat.journal).Notify(e, who.inventory);
}

bool World::CanStartRitual(const MapObject& table, string& why) const {
    why.clear();
    if (visiting) return false;
    if (ritual.active) {
        why = ritual.table == table.id ? "The candles are already burning."
                                       : "The fen is listening to another table. One at a time.";
        return false;
    }
    const int knots = IdolKnots(player.inventory);
    if (knots < 0) return false;                       // the table is only a table
    if (knots >= IDOL_KNOTS) {
        why = "The poppet is full: all five knots are tight. It wants nothing more of the fen. Take it to Oona.";
        return false;
    }
    if (Flagged(RitualDayFlag(clock.QuestDay()))) {
        why = "The table has fed once today. The candles will not catch again until tomorrow.";
        return false;
    }
    if (InDream()) return false;
    // A map with nothing to call is a map with no ritual.
    bool any = false;
    for (const EnemySpawnDef& d : map.Enemies()) any |= !d.ritual.empty();
    if (!any) {
        why = "Nothing answers.";
        return false;
    }
    return true;
}

bool World::StartRitual(const MapObject& table, const GameContext& ctx) {
    string why;
    if (!CanStartRitual(table, why)) {
        if (!why.empty()) {
            WorldRequest r;
            r.type = WorldRequest::Type::Toast;
            r.text = why;
            requests.push_back(r);
            Audio::Play(Sfx::UiError);
        }
        return false;
    }
    ritual = Ritual{};
    ritual.active = true;
    ritual.table = table.id;
    // Round the middle of the table, which is a little above where it stands.
    ritual.x = table.x;
    ritual.y = table.y - 10.0f;
    ritual.radius = RITUAL_RADIUS;
    // Whoever is being acted as is who lit it: a friend, or the world's own.
    ritual.owner_host = !Acting();
    ritual.owner_seat = player.seat;
    ritual.idol = IdolId(IdolKnots(player.inventory));
    ritual.wave = 0;
    ritual.timer = RITUAL_LEAD;
    ritual.between = true;
    map.SetRing(ritual.x, ritual.y, ritual.radius);

    // Laid: a quest may have asked for exactly this.
    if (ctx.quests) {
        QuestEvent e;
        e.type = ObjectiveType::Interact;
        e.target = "witch_table";
        e.map_id = map_id;
        ctx.quests->Notify(e, player.inventory);
    }
    WorldRequest r;
    r.type = WorldRequest::Type::Toast;
    r.text = "The candles catch by themselves, and fire comes up out of the mud in a ring. The fen has heard the poppet.";
    requests.push_back(r);
    AddText("The fen is coming", ritual.x, ritual.y - 64.0f, kRitualFire, 3.0f);
    Burst(ritual.x, ritual.y - 12.0f, 60.0f, kRitualFire, 26);
    Audio::PlayAt(Sfx::Breath, ritual.x, ritual.y, 0.9f, 0.8f);
    Audio::PlayAt(Sfx::Burn, ritual.x, ritual.y);
    return true;
}

void World::UpdateRitual(float dt, const GameContext& ctx) {
    if (!ritual.active || visiting) return;
    ritual.age += dt;

    // Broken: its owner down, gone off the map or somehow outside the ring --
    // judged where the ring judges everyone, the middle of their feet: the
    // point a step is held to. Their x and y is their heels, a few pixels
    // lower, and a fighter pressed up against the flames used to be "out".
    Player* owner = RitualOwner();
    const SDL_FPoint stood = owner ? owner->GroundCentre() : SDL_FPoint{};
    if (!owner || owner->IsDead() || !map.InsideRing(stood.x, stood.y)) {
        EndRitual(false, ctx);
        return;
    }
    ritual.timer -= dt;

    // Before the first wave, and between the rest, the fen gathers itself.
    if (ritual.between) {
        if (ritual.timer > 0.0f) return;
        ritual.between = false;
        ++ritual.wave;
        ritual.called.clear();
        ritual.out = 0;
        ritual.timer = 0.0f;
        for (size_t i = 0; i < enemies.size(); ++i) {
            const Enemy& e = *enemies[i];
            if (e.post < 0 || e.post >= static_cast<int>(map.Enemies().size())) continue;
            const EnemySpawnDef& d = map.Enemies()[e.post];
            if (!d.ritual.empty() && d.wave == ritual.wave) ritual.called.push_back(static_cast<int>(i));
        }
        // The Mother first: she comes at the head of hers.
        std::stable_sort(ritual.called.begin(), ritual.called.end(), [&](int a, int b) {
            const bool ba = enemies[a]->Def() && enemies[a]->Def()->is_boss;
            const bool bb = enemies[b]->Def() && enemies[b]->Def()->is_boss;
            return ba && !bb;
        });
        static const char* kCalls[RITUAL_WAVES] = {
            "The first of the fen come through the fire.",
            "More of them, and bigger. The poppet is warm in the flames.",
            "The Mother of the Fen comes for her doll.",
        };
        ActAs(*owner, [&] {
            WorldRequest r;
            r.type = WorldRequest::Type::Toast;
            r.text = kCalls[std::clamp(ritual.wave, 1, RITUAL_WAVES) - 1];
            requests.push_back(r);
        });
        return;
    }

    // Through the flames one after another, from all round the ring.
    const int n = static_cast<int>(ritual.called.size());
    if (ritual.out < n) {
        if (ritual.timer > 0.0f) return;
        Enemy& e = *enemies[ritual.called[ritual.out]];
        const float d = ritual.radius - 34.0f;
        float a = ComeFrom(ritual.out, n, ritual.wave);
        float sx = ritual.x + cosf(a) * d, sy = ritual.y + sinf(a) * d;
        // Not into the table, a stump or a wall: round the ring until there is room.
        const SDL_FRect body = e.Bounds();
        for (int t = 0; t < 16; ++t) {
            const SDL_FRect there = {body.x + (sx - e.x), body.y + (sy - e.y), body.w, body.h};
            if (!map.Blocked(there)) break;
            a += 0.39f;
            sx = ritual.x + cosf(a) * d;
            sy = ritual.y + sinf(a) * d;
        }
        e.home_x = sx;
        e.home_y = sy;
        e.Revive();
        e.Provoke(ritual.owner_host ? -1 : ritual.owner_seat);
        Burst(sx, sy - 14.0f, 26.0f, kRitualFire, 12);
        const bool boss = e.Def() && e.Def()->is_boss;
        Audio::PlayAt(boss ? Sfx::Roar : Sfx::Burn, sx, sy, boss ? 1.0f : 0.5f, boss ? 0.7f : 1.2f);
        if (boss) AddText(e.Def()->name, sx, sy - 70.0f, {236, 150, 120, 255}, 3.0f);
        ++ritual.out;
        ritual.timer = RITUAL_EVERY;
        return;
    }

    // All come: the wave is broken when every one of them is down.
    for (int i : ritual.called) {
        const Enemy& e = *enemies[i];
        if (!e.Dead() && e.CurrentState() != Enemy::State::Dead) return;
    }
    if (ritual.wave >= RITUAL_WAVES) {
        EndRitual(true, ctx);
        return;
    }
    QuestEvent wave;
    wave.type = ObjectiveType::Interact;
    wave.target = "poppet_wave";
    wave.map_id = map_id;
    CreditTo(*owner, wave);
    AddText("Wave " + std::to_string(ritual.wave) + " of " + std::to_string(RITUAL_WAVES) + " broken",
            ritual.x, ritual.y - 64.0f, kRitualFire, 2.6f);
    ritual.between = true;
    ritual.timer = RITUAL_BREATH;
}

void World::EndRitual(bool fed, const GameContext& ctx) {
    (void)ctx;
    if (!ritual.active) return;
    Player* owner = RitualOwner();
    map.ClearRing();

    // Whatever the fen sent and is still standing goes back into it.
    for (auto& ep : enemies) {
        Enemy& e = *ep;
        if (e.post < 0 || e.post >= static_cast<int>(map.Enemies().size())) continue;
        if (map.Enemies()[e.post].ritual.empty()) continue;
        if (e.Dead() || e.CurrentState() == Enemy::State::Dead) continue;
        Burst(e.x, e.y - 14.0f, 22.0f, kRitualSmoke, 10);
        e.LieDead();
    }

    QuestEvent e;
    e.type = ObjectiveType::Interact;
    e.map_id = map_id;
    WorldRequest said;
    said.type = WorldRequest::Type::Toast;
    if (fed && owner) {
        // The poppet's count first, then the last wave: finishing the waves
        // can begin the quest that counts the poppet's rituals, and that one
        // starts from the ritual that began it rather than counting it twice.
        e.target = "poppet_ritual";
        CreditTo(*owner, e);
        e.target = "poppet_wave";
        CreditTo(*owner, e);
        // A knot tighter.
        const int knots = IdolKnots(owner->inventory);
        if (knots >= 0 && knots < IDOL_KNOTS && owner->inventory.Remove(IdolId(knots), 1)) {
            owner->inventory.Add(IdolId(knots + 1), 1);
            said.text = "The fire sinks into the mud as if it had never been lit. The poppet is yours again: " +
                        std::to_string(knots + 1) + " of its " + std::to_string(IDOL_KNOTS) + " knots are tight.";
        }
        SetFlag(RitualDayFlag(clock.QuestDay()));
        AddText("The poppet is fed", ritual.x, ritual.y - 64.0f, {255, 214, 120, 255}, 3.2f);
        Burst(ritual.x, ritual.y - 12.0f, 70.0f, {255, 214, 120, 255}, 30);
        Audio::PlayAt(Sfx::QuestComplete, ritual.x, ritual.y);
    } else {
        // Broken: whatever waves it had got through are undone with it.
        if (owner) {
            e.target = "poppet_wave";
            e.amount = -RITUAL_WAVES;
            CreditTo(*owner, e);
        }
        said.text = "The ring gutters out, and the poppet on the table is cold. The ritual is broken: "
                    "lay it on a table again.";
        Burst(ritual.x, ritual.y - 12.0f, 50.0f, kRitualSmoke, 18);
        Audio::PlayAt(Sfx::Burn, ritual.x, ritual.y, 0.8f, 0.6f);
    }
    if (owner && !said.text.empty()) ActAs(*owner, [&] { requests.push_back(said); });
    ritual = Ritual{};
}

bool World::RingBurning(float& x, float& y, float& radius, int& wave) const {
    if (ritual.active) {
        x = ritual.x; y = ritual.y; radius = ritual.radius; wave = ritual.wave;
        return true;
    }
    if (ring_heard.on) {
        x = ring_heard.x; y = ring_heard.y; radius = ring_heard.radius; wave = ring_heard.wave;
        return true;
    }
    return false;
}

void World::HearOfRing(float x, float y, float radius, int wave) {
    if (!ring_heard.on) ring_heard.age = 0.0f;
    ring_heard.on = true;
    ring_heard.x = x;
    ring_heard.y = y;
    ring_heard.radius = radius;
    ring_heard.wave = wave;
    map.SetRing(x, y, radius);
}

void World::RingNotHeard() {
    if (!ring_heard.on) return;
    ring_heard = RingHeard{};
    map.ClearRing();
}

// --- drawing ----------------------------------------------------------------------

void World::DrawRingGround(SDL_Renderer* r) const {
    float cx = 0.0f, cy = 0.0f, radius = 0.0f;
    int wave = 0;
    if (!RingBurning(cx, cy, radius, wave)) return;
    const float age = ritual.active ? ritual.age : ring_heard.age;
    const float up = std::clamp(age / 1.0f, 0.0f, 1.0f);
    const float z = camera.zoom;
    const SDL_FPoint c = camera.ToScreen(cx, cy - LiftAt(cx, cy));
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Burnt into the mud: a band of scorch, and in the middle of it the glow
    // the flames stand on, flickering. In squares the size of the art's own
    // pixels, so it lies on the ground like the rest of the ground.
    const int around = std::max(24, static_cast<int>(6.2831853f * radius / 2.5f));
    for (int i = 0; i < around; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(around);
        const float flick = 0.7f + 0.3f * sinf(now * 9.0f + i * 1.7f) * sinf(now * 5.3f + i * 0.9f);
        for (int band = -3; band <= 3; ++band) {
            const int far = band < 0 ? -band : band;
            SDL_Color col;
            if (far == 0)      col = {255, 196, 96, static_cast<Uint8>(230 * flick * up)};
            else if (far == 1) col = {236, 104, 34, static_cast<Uint8>(170 * flick * up)};
            else if (far == 2) col = {120, 40, 20, static_cast<Uint8>(120 * up)};
            else               col = {26, 16, 12, static_cast<Uint8>(90 * up)};
            const float d = (radius + static_cast<float>(band) * 2.5f) * z;
            const SDL_FRect dot = {roundf((c.x + cosf(a) * d) / z) * z, roundf((c.y + sinf(a) * d) / z) * z, z * 2.0f, z * 2.0f};
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(r, &dot);
        }
    }
}

void World::DrawRitual(SDL_Renderer* r, TextureCache& cache) const {
    float cx = 0.0f, cy = 0.0f, radius = 0.0f;
    int wave = 0;
    if (!RingBurning(cx, cy, radius, wave)) return;
    // The poppet, lying on the table where it was laid, stirring a little.
    SDL_Texture* tex = cache.Get("assets/icons/poppet_idol.png");
    if (!tex) return;
    const float z = camera.zoom;
    const float stir = sinf(static_cast<float>(SDL_GetTicks()) / 1000.0f * 2.4f) * 0.8f;
    const SDL_FPoint p = camera.ToScreen(cx, cy - LiftAt(cx, cy) - 14.0f + stir);
    const float side = 18.0f * z;
    const SDL_FRect dst = {roundf(p.x - side * 0.5f), roundf(p.y - side * 0.5f), side, side};
    SDL_RenderTexture(r, tex, nullptr, &dst);
}
