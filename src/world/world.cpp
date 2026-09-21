#include "world.h"
#include "../input.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include "../systems/dialogue.h"
#include "../systems/spell.h"
#include "../systems/audio.h"
#include "../systems/gathering.h"

static constexpr float FADE_SPEED     = 3.2f;
// Generous enough to reach anything the player can stand next to: a wide prop
// such as the mission board keeps them ~45px from its centre, so a tighter
// radius would leave them unable to use something they are leaning on.
static constexpr float INTERACT_RANGE = 58.0f;
static constexpr float HAZARD_TICK = 0.5f;
static constexpr float PICKUP_RANGE   = 18.0f;
static constexpr float PICKUP_ARM     = 0.35f;   // no instant re-collect
// A dropped item is left alone until the player is this far from it.
static constexpr float DROP_CLEAR     = PICKUP_RANGE + 10.0f;

// -----------------------------------------------------------------------------
//  Map loading and transitions
// -----------------------------------------------------------------------------

bool World::LoadMap(const string& id, const string& spawn, const GameContext& ctx) {
    const string path = "maps/" + id + ".mx";
    // Load into a candidate first. A missing or malformed destination must not
    // unload the area the player is still standing in.
    Map arriving;
    try {
        if (!arriving.Load(path)) return false;
    } catch (const std::exception& e) {
        SDL_Log("World: invalid destination '%s': %s", id.c_str(), e.what());
        return false;
    }
    // Friends on the map being left stay on it, in a world of their own.
    if (before_unload && map.Loaded()) before_unload(*this);
    map = std::move(arriving);

    map_id = id;
    // Remembered, so dialogue can know where the player has been.
    SetFlag("visited:" + id);
    enemies.clear();
    npcs.clear();
    pickups.clear();
    texts.clear();
    projectiles.clear();
    ground_effects.clear();
    impacts.clear();
    dust.clear();
    motes.clear();
    shots_seen.clear();
    targeting.Clear();
    gather_index = -1;
    player.StopGathering();

    SpawnEntitiesFromMap(ctx);
    if (visiting) {
        // Every monster the map has, as a puppet nobody has spoken of yet:
        // out of sight until the host says where it is. The nth is number n.
        Enemy::Posed unseen;
        unseen.state = static_cast<uint8_t>(Enemy::State::Dead);
        unseen.alpha = 0;
        for (auto& e : enemies) { e->puppet = true; unseen.x = e->x; unseen.y = e->y; e->Pose(unseen); }
    }
    PlaceCampObjects();

    SDL_FPoint p;
    if (spawn.empty() || !map.Spawn(spawn, p)) p = map.DefaultSpawn();
    player.x = p.x;
    player.y = p.y;
    player.knock_x = player.knock_y = 0.0f;
    // Everyone arrives where the host does.
    for (auto& g : guests) {
        g->x = p.x;
        g->y = p.y;
        g->knock_x = g->knock_y = 0.0f;
    }
    portals_armed = false;
    arrival_released = false;

    camera.SetBounds(map.Width(), map.Height());
    camera.SnapTo(player.x, player.y);
    ambience.SetKind(map.Ambient(), map.IsInterior());
    Audio::SetAmbience(map.Ambient(), map.IsInterior());
    Audio::SetListener(player.x, player.y);
    if (ctx.quests) {
        QuestEvent e;
        e.type = ObjectiveType::Reach;
        e.target = map_id;
        e.map_id = map_id;
        ctx.quests->Notify(e, player.inventory);
    }
    if (after_load) after_load(*this);
    return true;
}

EnemySpawnDef World::ResolveSpawn(const EnemySpawnDef& def, const string& map_id, int day, int index) {
    EnemySpawnDef out = def;
    if (def.pool.empty() && def.spread <= 0) return out;
    // FNV-1a over the things that should change the answer, and nothing else.
    auto mix = [](uint32_t h, const string& s) {
        for (char c : s) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        return h;
    };
    auto stir = [](uint32_t h) { h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16; return h; };
    uint32_t h = mix(2166136261u, map_id);
    h = (h ^ static_cast<uint32_t>(day)) * 16777619u;
    // Who: by the group, so a platform agrees with itself.
    const uint32_t who = stir(def.group.empty() ? (h ^ static_cast<uint32_t>(index)) * 16777619u : mix(h, def.group));
    if (!def.pool.empty()) out.type = def.pool[who % def.pool.size()];
    // How strong: by the post, so a pack is not all one size.
    const uint32_t how = stir((h ^ (static_cast<uint32_t>(index) + 977u)) * 16777619u);
    if (def.spread > 0) out.level = def.level + static_cast<int>(how % static_cast<uint32_t>(def.spread + 1));
    return out;
}

void World::StartAfresh() {
    flags.clear();
    slain.clear();
    storage.clear();
    picked.clear();
    shops.Clear();
    clock.Set(1, 9.0f);
    camp = {};
    dream = {};
    told_day = -999999;
}

bool World::KeptTonight(const string& map_id, int day, int index, const string& group, float chance) {
    if (chance >= 1.0f) return true;
    if (chance <= 0.0f) return false;
    // The same stirring ResolveSpawn does, salted, so that which thing comes
    // and whether it comes are two questions.
    uint32_t h = 2166136261u;
    for (char c : map_id) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    h = (h ^ static_cast<uint32_t>(day)) * 16777619u;
    h = (h ^ 0x6e696768u) * 16777619u;                         // "nigh"
    if (group.empty()) h = (h ^ static_cast<uint32_t>(index)) * 16777619u;
    else for (char c : group) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFu) / 65536.0f < chance;
}

bool World::Abroad(const Enemy& e) const {
    return e.night && clock.IsNight() && !InDream() &&
           KeptTonight(map_id, clock.QuestDay(), e.post, e.night_group, e.night_chance) &&
           !SlainToday(map_id, e.post);
}

bool World::HasNightPosts() const {
    for (const auto& e : enemies) if (e->night) return true;
    return false;
}

void World::TellTheDay() {
    told_day = clock.QuestDay();
    for (Player* p : Players()) {
        const bool was = p->talents.TotemAwake();
        p->talents.SetToday(told_day);
        if (was != p->talents.TotemAwake()) {
            p->SyncHitpoints();
            p->SyncMana();
        }
    }
}

void World::AwardBoss(const string& boss_id, const GameContext& ctx) {
    if (boss_id.empty()) return;
    std::mt19937 spare(0xb055u);
    const Talents::Trophy won = player.talents.SlayBoss(boss_id, ctx.rng ? *ctx.rng : spare);
    if (won.totem) {
        // Into the bag if there is room, and at their feet if there is not:
        // it cannot be sold or thrown away, and it is not going to be lost to
        // a full pack either.
        const EnemyDef* whose = ctx.enemies ? ctx.enemies->Get(boss_id) : nullptr;
        const ItemDef* thing = ctx.items ? ctx.items->Get(won.totem->item) : nullptr;
        if (player.inventory.Add(won.totem->item, 1) <= 0) DropItem(won.totem->item, 1, player.x, player.y, ctx);
        WorldRequest t;
        t.type = WorldRequest::Type::Toast;
        t.text = string(whose ? whose->name : boss_id) + ", " + std::to_string(Talents::TOTEM_KILLS) +
                 " times: it leaves you its totem.";
        requests.push_back(t);
        t.text = (thing ? thing->name : won.totem->item) + ". Stand it in the ring in your house at Mossvale.";
        requests.push_back(t);
        AddText("A totem", player.x, player.y - 94.0f, {255, 214, 120, 255}, 3.2f);
        Burst(player.x, player.y - 30.0f, 80.0f, {255, 190, 90, 255}, 26);
        Audio::PlayAt(Sfx::QuestComplete, player.x, player.y);
    }
    if (!won.first) return;
    // Health or mana may be the boon: the pools are what they now are.
    player.SyncHitpoints();
    player.SyncMana();

    const EnemyDef* def = ctx.enemies ? ctx.enemies->Get(boss_id) : nullptr;
    // "The Hollowrest Wight" has its own article; "Broodmother" wants one.
    string who = def ? def->name : boss_id;
    if (who.rfind("The ", 0) != 0 && who.rfind("the ", 0) != 0) who = "The " + who;
    // Two short lines rather than one long one: a toast is a single line, set
    // from the right-hand edge, and a long one runs off a narrow window.
    WorldRequest r;
    r.type = WorldRequest::Type::Toast;
    r.text = who + " is down, for the first time: +1 skill point.";
    requests.push_back(r);
    if (won.boon) {
        r.text = "And a boon -- " + won.boon->name + ": " + won.boon->text + ".";
        requests.push_back(r);
    }
    AddText(won.boon ? "Boon: " + won.boon->name : string("A skill point"), player.x, player.y - 78.0f,
            {255, 214, 120, 255}, 3.2f);
    AddText("+1 skill point", player.x, player.y - 62.0f, {190, 236, 160, 255}, 3.2f);
    Burst(player.x, player.y - 30.0f, 70.0f, {255, 214, 120, 255}, 22);
    Audio::PlayAt(Sfx::QuestComplete, player.x, player.y);
}

int World::DreamBonus(const string& item_id) const {
    if (item_id != "dream_shard" || !InDream()) return 0;
    return std::max(0, map.DreamDepth() - 1);
}

void World::NoteSlain(int post) {
    if (post < 0) return;
    slain[map_id + ":" + std::to_string(post)] = clock.QuestDay();
}

bool World::SlainToday(const string& map, int post) const {
    const auto it = slain.find(map + ":" + std::to_string(post));
    return it != slain.end() && it->second >= clock.QuestDay();
}

void World::SpawnEntitiesFromMap(const GameContext& ctx) {
    int post = 0;
    for (const auto& written : map.Enemies()) {
        const EnemySpawnDef def = ResolveSpawn(written, map_id, clock.QuestDay(), post++);
        // A monster already killed this session stays dead until its timer
        // brings it back; flags cover the permanent ones.
        const EnemyDef* stats = ctx.enemies ? ctx.enemies->Get(def.type) : nullptr;
        if (!stats) {
            SDL_Log("World: unknown enemy type '%s'", def.type.c_str());
            continue;
        }
        auto e = std::make_unique<Enemy>();
        e->Init(stats, def, ctx);
        e->post = post - 1;
        // Killed already today: it keeps its place in the list, which friends
        // count monsters by, and is not there.
        if (stats->is_boss && SlainToday(map_id, e->post)) e->LieDead();
        // A night visitor by day, or on a night that is not one of its own.
        if (e->night && !Abroad(*e)) e->LieDead();
        enemies.push_back(std::move(e));
    }

    for (const auto& def : map.Npcs()) {
        auto n = std::make_unique<Npc>();
        n->Init(def, ctx);
        npcs.push_back(std::move(n));
    }
}

bool World::RequestTransition(const string& id, const string& spawn) {
    if (transition_pending) return false;
    if (visiting) {
        // Until the co-op plan's M4 there is one world on the host, the
        // host's own map: a guest cannot go where it is not.
        if (gate_note_timer <= 0.0f) {
            AddText("The host leads the way, for now.", player.x, player.y - 52.0f, {214, 232, 255, 255}, 2.2f);
            gate_note_timer = 2.5f;
        }
        return false;
    }
    transition_pending = true;
    next_map   = id;
    next_spawn = spawn;
    next_has_point = false;
    fade_speed = FADE_SPEED;
    fade_caption.clear();
    fade_dir   = 1;
    return true;
}

// -----------------------------------------------------------------------------
//  Other players
// -----------------------------------------------------------------------------

Player* World::AddGuest(uint8_t seat, const string& name, const string& look, const GameContext& ctx) {
    RemoveGuest(seat);
    auto g = std::make_unique<Player>();
    g->Init(ctx, look.empty() ? string(Player::kDefaultCharacter) : look);
    g->local = false;
    g->seat = seat;
    g->name = name;
    g->x = player.x;
    g->y = player.y;
    guests.push_back(std::move(g));
    return guests.back().get();
}

void World::RemoveGuest(uint8_t seat) {
    // Nothing may go on pointing at who is about to be gone.
    auto it = seat_states.find(seat);
    if (it != seat_states.end()) { it->second.targeting.Clear(); seat_states.erase(it); }
    guests.erase(std::remove_if(guests.begin(), guests.end(),
                                [&](const std::unique_ptr<Player>& g) { return g->seat == seat; }),
                 guests.end());
}

Player* World::Guest(uint8_t seat) {
    for (auto& g : guests) if (g->seat == seat) return g.get();
    return nullptr;
}

void World::StepGuest(Player& guest, const PlayerInput& hands, float dt, const GameContext& ctx) {
    if (guest.puppet || guest.away || !map.Loaded()) return;
    guest.hands = hands;
    guest.hands_external = true;
    AsSeat(guest, ctx, [&](const GameContext& theirs) {
        UpdateSeat(dt, theirs);
        CollectPickups(dt, theirs);
        // Someone looking through this seat at this machine: their camera
        // follows them, on whatever map this is.
        if (seat_states[player.seat].viewed) {
            camera.SetBounds(map.Width(), map.Height());
            camera.Follow(player.x + player.LookAhead().x, player.y + player.LookAhead().y, dt);
        }
    });
}

void World::InteractWith(int kind, int index, const GameContext& ctx) {
    // Only what is really there, and near: a friend's machine says what E was
    // pressed on, and the host looks for itself whether it could have been.
    float tx = player.x, ty = player.y;
    if (kind == InteractTarget::Npc) {
        if (index < 0 || index >= static_cast<int>(npcs.size()) || npcs[index]->Away()) return;
        tx = npcs[index]->x; ty = npcs[index]->y;
    } else if (kind == InteractTarget::Object) {
        if (index < 0 || index >= static_cast<int>(map.Objects().size())) return;
        tx = map.Objects()[index].x; ty = map.Objects()[index].y;
    } else if (kind != InteractTarget::PortalDoor && kind != InteractTarget::None) {
        return;
    }
    if (Length(tx - player.x, ty - player.y) > 160.0f) return;
    player.interact.kind = static_cast<InteractTarget::Kind>(kind);
    player.interact.index = index;
    TryInteract(ctx);
}

void World::HandOver(World& to) {
    if (!to.map.Loaded() || to.map_id != map_id) {
        to.map = std::move(map);
        to.map_id = map_id;
        to.camera.SetBounds(to.map.Width(), to.map.Height());
    }
    to.enemies = std::move(enemies);
    to.npcs = std::move(npcs);
    to.pickups = std::move(pickups);
    to.projectiles = std::move(projectiles);
    to.ground_effects = std::move(ground_effects);
    to.impacts = std::move(impacts);
    to.motes = std::move(motes);
    to.shots_seen = std::move(shots_seen);
    for (auto& g : guests) to.guests.push_back(std::move(g));
    for (auto& [seat, state] : seat_states) to.seat_states[seat] = std::move(state);
    to.next_net_id = std::max(to.next_net_id, next_net_id);
    enemies.clear(); npcs.clear(); pickups.clear(); projectiles.clear();
    ground_effects.clear(); impacts.clear(); guests.clear(); seat_states.clear();
    motes.clear(); shots_seen.clear();
    // Whoever either host seat was fighting has changed hands.
    targeting.Clear();
    to.targeting.Clear();
    gather_index = -1;
    to.PlaceCampObjects();
}

void World::BeginActing(Player& who) {
    if (acting || &who == &player) return;
    SeatState& s = seat_states[who.seat];
    SwapSeat(who, s);
    acting = &who;
    acting_flags = &s.private_flags;
    acting_flags_rw = &s.private_flags;
    if (s.own_journal) quest_log = s.own_journal;
}

void World::EndActing() {
    if (!acting) return;
    Player& who = *acting;
    // The seat number travelled with the swap: it is `player`'s now.
    SeatState& s = seat_states[player.seat];
    acting = nullptr;
    acting_flags = nullptr;
    acting_flags_rw = nullptr;
    quest_log = host_quests;
    SwapSeat(who, s);
}

void World::SwapSeat(Player& who, SeatState& s) {
    std::swap(player, who);
    if (s.viewed) std::swap(camera, s.camera);
    std::swap(targeting, s.targeting);
    std::swap(gather_index, s.gather_index);
    std::swap(gather_timer, s.gather_timer);
    std::swap(gather_needed, s.gather_needed);
    std::swap(hazard_timer, s.hazard_timer);
    std::swap(gate_note_timer, s.gate_note_timer);
    std::swap(lifesteal_bank, s.lifesteal_bank);
    std::swap(portals_armed, s.portals_armed);
    std::swap(arrival_released, s.arrival_released);
    std::swap(transition_pending, s.transition_pending);
    std::swap(next_map, s.next_map);
    std::swap(next_spawn, s.next_spawn);
    std::swap(next_has_point, s.next_has_point);
    std::swap(next_x, s.next_x);
    std::swap(next_y, s.next_y);
    int w = static_cast<int>(waking); waking = static_cast<WakeReason>(s.waking); s.waking = w;
    std::swap(fade, s.fade);
    std::swap(fade_speed, s.fade_speed);
    std::swap(fade_dir, s.fade_dir);
    std::swap(fade_caption, s.fade_caption);
    std::swap(dream.active, s.dream_active);
    std::swap(dream.map, s.dream_map);
    std::swap(dream.x, s.dream_x);
    std::swap(dream.y, s.dream_y);
    std::swap(requests, s.requests);
}

Player& World::OwnerOf(bool local, uint8_t seat) {
    if (local) return player;
    if (Player* g = Guest(seat)) return *g;
    return player;
}

Player* World::PlayerTouching(const SDL_FRect& box) {
    for (Player* p : Players())
        if (!p->IsDead() && !p->puppet && !p->resting && RectsOverlap(box, p->BodyBox())) return p;
    return nullptr;
}

bool World::AnyPlayerNear(float px, float py, float range) {
    for (Player* p : Players())
        if (!p->IsDead() && Length(p->x - px, p->y - py) < range) return true;
    return false;
}

void World::FlushKills(const GameContext& ctx) {
    if (kill_log.empty() || acting) return;
    for (const QuestEvent& e : kill_log) {
        if (host_quests && !player.absent) host_quests->Notify(e, player.inventory);
        // A boss: `secondary` says which. Whoever is here had a hand in it.
        if (!e.secondary.empty() && !player.absent && !visiting) AwardBoss(e.secondary, ctx);
        for (auto& g : guests) {
            if (g->puppet) continue;
            SeatState& seat = seat_states[g->seat];
            (seat.own_journal ? *seat.own_journal : seat.journal).Notify(e, g->inventory);
            // Someone on the same couch is here in person, and is given theirs
            // here. A friend down the wire keeps their character on their own
            // machine: the kill is relayed to it, and it is given there --
            // see Guest::OnDelta -- and comes back on their sheet.
            if (!e.secondary.empty() && seat.own_journal) ActAs(*g, [&] { AwardBoss(e.secondary, ctx); });
        }
    }
    kill_log.clear();
}

vector<Player*> World::Players() {
    vector<Player*> all{&player};
    for (auto& g : guests) all.push_back(g.get());
    return all;
}

Player& World::NearestPlayer(float px, float py) {
    Player* best = &player;
    float best_d = Length(player.x - px, player.y - py);
    for (auto& g : guests) {
        if (g->IsDead()) continue;
        const float d = Length(g->x - px, g->y - py);
        if (d < best_d || best->IsDead()) { best = g.get(); best_d = d; }
    }
    return *best;
}

// -----------------------------------------------------------------------------
//  Sleep, dreams and camps
// -----------------------------------------------------------------------------

void World::PlaceCampObjects() {
    map.RemoveObjects("player_camp");
    if (!camp.pitched || camp.map != map_id) return;

    MapObject tent;
    tent.id     = "player_camp";
    tent.type   = "camp";
    tent.x      = camp.x;
    tent.y      = camp.y;
    tent.sprite = "assets/props/tent.png";
    tent.title  = "Your camp";
    map.AddObject(tent);

    MapObject fire;
    fire.id     = "player_camp_fire";
    fire.type   = "camp_fire";
    fire.x      = camp.x + 46.0f;
    fire.y      = camp.y + 22.0f;
    fire.sprite = "assets/props/campfire_ring.png";
    map.AddObject(fire);
}

string World::SleepRefusal() const {
    if (!clock.CanSleep())
        return "Not tired yet. Sleep comes after dusk.";
    for (const auto& e : enemies) {
        if (!Targeting::Targetable(*e) || !e->Def() || e->Def()->aggro_range <= 0.0f) continue;
        if (Length(e->x - player.x, e->y - player.y) < SLEEP_SAFE_RANGE || e->Engaged())
            return "You cannot sleep with enemies nearby.";
    }
    return "";
}

bool World::AskToSleep(const string& title, int fee) {
    if (InDream() || transition_pending || player.IsDead()) return false;
    const string why = SleepRefusal();
    if (!why.empty()) {
        AddText(why, player.x, player.y - 54.0f, {210, 200, 240, 255}, 1.8f);
        Audio::Play(Sfx::UiError);
        return false;
    }
    WorldRequest r;
    r.type  = WorldRequest::Type::Sleep;
    r.title = title;
    r.count = std::max(0, fee);      // what the bed costs; the panel takes it
    requests.push_back(r);
    return true;
}

bool World::Sleep(SleepChoice how, const GameContext& ctx) {
    (void)ctx;
    if (visiting) {
        // The bed is the host's: say which way, and wait to be told.
        visitor_acts.push_back({5, "", "", how == SleepChoice::Reverie ? 1 : 0});
        return true;
    }
    if (InDream() || transition_pending || player.IsDead()) return false;

    // Asked again, not trusted from the prompt: the panel pauses the world,
    // but this is also what a request from another machine will come through.
    const string why = SleepRefusal();
    if (!why.empty()) {
        AddText(why, player.x, player.y - 54.0f, {210, 200, 240, 255}, 1.8f);
        Audio::Play(Sfx::UiError);
        return false;
    }

    player.Rest();
    targeting.Clear();
    fade_speed = SLEEP_FADE_SPEED;

    if (how == SleepChoice::Reverie) {
        dream.active = true;
        dream.map = map_id;
        dream.x = player.x;
        dream.y = player.y;
        RequestTransition(DREAM_MAP, "arrival");
        fade_speed = SLEEP_FADE_SPEED;
        fade_caption = "You drift off to sleep...";
    } else if (company) {
        // In company the night is everyone's. Lie down: out of the fight,
        // nothing can hurt you, and the clock keeps its own pace until every
        // one of you is abed or dreaming -- then it is dawn for all at once.
        // Any key gets up. coop::Host watches for the moment.
        player.resting = true;
        WorldRequest r;
        r.type = WorldRequest::Type::Toast;
        r.text = "You lie down. Dawn comes when everyone is abed or dreaming; move to get up.";
        requests.push_back(r);
        return true;
    } else {
        // The same map and the same spot, on the other side of the night. It
        // is a transition like waking from a dream is, so the morning finds
        // the place as any arrival would: monsters back where they live, and
        // whatever was dropped on the floor gone.
        const float x = player.x, y = player.y;
        RequestTransition(map_id, "");
        next_has_point = true;
        next_x = x;
        next_y = y;
        fade_speed = SLEEP_FADE_SPEED;
        fade_caption = "You sleep the night through...";
        waking = WakeReason::Slept;
    }
    Audio::Play(Sfx::Sleep);
    return true;
}

void World::Wake(WakeReason why) {
    if (!InDream() || transition_pending || why == WakeReason::None) return;
    const string where = (dream.active && !dream.map.empty()) ? dream.map : string("overworld");
    RequestTransition(where, dream.active ? "" : "start");
    if (dream.active) {
        next_has_point = true;
        next_x = dream.x;
        next_y = dream.y;
    }
    fade_speed = SLEEP_FADE_SPEED;
    fade_caption = why == WakeReason::Nightmare ? "The nightmare throws you awake."
                 : why == WakeReason::Stone     ? "You wake."
                                                : "Dawn breaks.";
    waking = why;
}

string World::PitchCamp(int slot, const GameContext& ctx) {
    (void)ctx;
    if (visiting || acting)
        return "A camp is the host's to pitch, for now.";
    if (slot < 0 || slot >= player.inventory.SlotCount() || player.inventory.Slot(slot).Empty())
        return "There is nothing there to pitch.";
    if (InDream())
        return "There is no ground in a dream to pitch a camp on.";
    if (map.IsInterior() || map.Ambient() == "dungeon")
        return "A camp needs open sky.";
    if (transition_pending || player.IsDead() || player.IsJumping())
        return "Not now.";
    if (targeting.InCombat())
        return "Not with enemies about.";

    // The tent goes just behind the player, its fire off to one side, and
    // both need clear ground away from any way out.
    const float tx = player.x, ty = player.y - 20.0f;
    const SDL_FRect tent_base = {tx - 28.0f, ty - 14.0f, 56.0f, 14.0f};
    const SDL_FRect fire_base = {tx + 46.0f - 14.0f, ty + 22.0f - 10.0f, 28.0f, 10.0f};
    const SDL_FRect clearing  = {tx - 40.0f, ty - 50.0f, 110.0f, 90.0f};
    if (map.Blocked(tent_base) || map.Blocked(fire_base))
        return "There is no room for a tent here.";
    if (map.PortalAt(clearing))
        return "Too close to the way through.";
    if (map.LevelAt(tx, ty) != map.LevelAt(player.x, player.y) ||
        map.LevelAt(fire_base.x, fire_base.y) != map.LevelAt(player.x, player.y))
        return "The ground here is too uneven.";

    const bool moved = camp.pitched;
    player.inventory.RemoveSlot(slot, 1);
    // One camp at a time: pitching another packs the first away.
    if (moved) player.inventory.Add("bedroll", 1);

    camp.pitched = true;
    camp.map = map_id;
    camp.x = tx;
    camp.y = ty;
    PlaceCampObjects();
    Audio::Play(Sfx::Chop, 0.6f, 1.2f);
    return "";
}

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
        if (g.element != Element::Fire || !g.Active()) continue;
        lights.push_back({g.x, g.y, g.radius * 2.2f, {255, 150, 70, 255}, 0.8f * dark});
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

void World::ApplyTransition(const GameContext& ctx) {
    const WakeReason why = waking;
    waking = WakeReason::None;
    if (LoadMap(next_map, next_spawn, ctx)) {
        if (next_has_point) {
            player.x = next_x;
            player.y = next_y;
            camera.SnapTo(player.x, player.y);
        }
        if (why != WakeReason::None) {
            // Back where you lay down, rested -- or, from a nightmare, alive.
            if (player.IsDead()) player.Respawn(player.x, player.y);
            player.Rest();
            // A nightmare costs the rest of the night; a night slept through
            // is the rest of the night.
            if (why == WakeReason::Nightmare || why == WakeReason::Slept) clock.SkipToDawn();
            if (why == WakeReason::Slept) fade_caption = "Dawn breaks.";
            dream = {};
            woke = why;
            Audio::Play(Sfx::Wake);
            // The bed is under you; do not step straight off it into a portal.
            portals_armed = false;
            arrival_released = false;
        }
    }
    next_has_point = false;
    if (!map.Loaded() || map_id != next_map) {
        SDL_Log("World: failed to enter map '%s'", next_map.c_str());
        WorldRequest r;
        r.type = WorldRequest::Type::Toast;
        r.text = "That path could not be opened. Your current area is unchanged.";
        requests.push_back(r);
        // Do not retry a broken exit every frame while standing on it.
        portals_armed = false;
        arrival_released = false;
    }
    transition_pending = false;
    fade_dir = -1;
}

// -----------------------------------------------------------------------------
//  Frame update
// -----------------------------------------------------------------------------

bool World::ObjectPresent(const MapObject& o) const {
    if (o.needs_quest.empty()) return true;
    return quest_log && quest_log->IsActive(o.needs_quest);
}

void World::Update(float dt, const GameContext& ctx) {
    quest_log = ctx.quests;
    host_quests = ctx.quests;
    // --- screen wipe ---------------------------------------------------------
    if (fade_dir != 0) {
        fade += fade_dir * fade_speed * dt;
        if (fade_dir > 0 && fade >= 1.0f) {
            fade = 1.0f;
            if (transition_pending) ApplyTransition(ctx);
        } else if (fade_dir < 0 && fade <= 0.0f) {
            fade = 0.0f;
            fade_dir = 0;
            fade_speed = FADE_SPEED;
            fade_caption.clear();
        }
    }

    // --- the clock -----------------------------------------------------------
    {
        const bool was_night = clock.IsNight();
        clock.Advance(dt);
        if (!was_night && clock.IsNight() && !InDream()) {
            WorldRequest r;
            r.type = WorldRequest::Type::Toast;
            r.text = HasNightPosts()
                ? "Night falls, and things are abroad that are not by day. Keep to the road, or find a bed."
                : "Night falls. A bed or a camp will see you through it, or into a dream.";
            requests.push_back(r);
        }
        if (was_night && !clock.IsNight() && !InDream() && HasNightPosts()) {
            WorldRequest r;
            r.type = WorldRequest::Type::Toast;
            r.text = "Dawn. What the dark let out has gone to ground.";
            requests.push_back(r);
        }
    }
    // Dawn, or a day nobody has been told of yet -- a load, a friend arriving.
    if (told_day != clock.QuestDay() || (guests.size() + 1) != told_players) {
        told_players = guests.size() + 1;
        TellTheDay();
    }
    // The seat at this machine, and then the place. A friend's seat is done
    // by StepGuest, to their own clock, acting as them.
    if (!player.absent) UpdateSeat(dt, ctx);
    UpdateShared(dt, ctx);
    if (!visiting && !player.absent) CollectPickups(dt, ctx);
    FlushKills(ctx);

    if (!player.absent) {
        camera.Follow(player.x + player.LookAhead().x, player.y + player.LookAhead().y, dt);
        ambience.Update(dt, camera);
        Audio::SetListener(player.x, player.y);
    }
}

void World::UpdateSeat(float dt, const GameContext& ctx) {
    // Movement stays frozen while the screen is covered, but only for as long
    // as it is covered: Game owns input_locked for open panels, so borrow it
    // and hand it back rather than latching it on.
    const bool frozen = (fade_dir > 0 && transition_pending);
    const bool locked_by_game = player.input_locked;
    player.input_locked = locked_by_game || frozen;

    // The hands of the seat at this machine, from the device -- unless the
    // co-op client is filling them, with what it is also sending the host.
    if (!player.hands_external)
        player.hands = ctx.input ? PlayerInput::FromDevice(*ctx.input) : PlayerInput{};
    // Lying down for the night, the hands are still -- until they move.
    if (player.resting) {
        if (player.hands.pressed != 0 || Length(player.hands.move.x, player.hands.move.y) > 0.5f)
            player.resting = false;
        player.hands = PlayerInput{};
    }

    // Targeting first, so a swing or a shot starting this frame knows who it
    // is for.
    {
        // With the guard held and an ability carried there, lock on is that
        // ability's button and the target stays who it was.
        const bool third = player.hands.Down(PlayerInput::Block) &&
                           player.talents.Ability(SkillTrees::ABILITY_SLOTS - 1) != nullptr;
        const bool cycle = !player.input_locked && !third && player.hands.Pressed(PlayerInput::Target);
        switch (targeting.Update(player, enemies, map, cycle)) {
            case Targeting::Change::Locked:
            case Targeting::Change::Switched: Audio::Play(Sfx::UiMove, 0.8f, 0.8f); break;
            case Targeting::Change::Released: Audio::Play(Sfx::UiBack, 0.6f); break;
            default: break;
        }
    }

    player.Update(dt, *this, ctx);

    player.input_locked = locked_by_game;

    // A dream lasts as long as the night. Dying in one ends it early, a moment
    // into the fall, before the game can treat it as a real death.
    // What follows decides things -- a swing landing, a log falling, a door
    // opening -- and in a guest's window those are the host's to decide. The
    // window only finds what E would do, so the prompt can say so.
    if (visiting) {
        if (gate_note_timer > 0.0f) gate_note_timer -= dt;
        if (!player.IsDead()) ResolveInteractTarget(ctx);
        else player.interact = {};
        return;
    }

    if (InDream() && !transition_pending) {
        if (player.IsDead()) {
            if (player.DeathTimer() < 1.6f) Wake(WakeReason::Nightmare);
        } else if (clock.DreamOver()) {
            Wake(WakeReason::Dawn);
        }
    }

    if (gate_note_timer > 0.0f) gate_note_timer -= dt;

    // --- hazards --------------------------------------------------------------
    // Standing on lava or burning ground takes a bite every half second, a
    // number over the head and a flash, so it is felt rather than noticed on
    // the health bar afterwards. Jumping over it is safe.
    if (!player.IsDead() && !transition_pending && !player.IsJumping()) {
        const Hazard* h = map.HazardAt(player.Bounds());
        if (h) {
            hazard_timer -= dt;
            if (hazard_timer <= 0.0f) {
                hazard_timer = HAZARD_TICK;
                // Ground that burns takes half as much out of anyone wearing
                // the Drowned King's boots.
                const float share = player.Passive(Player::PASSIVE_MARSHSTRIDE) ? 0.5f : 1.0f;
                const int dmg = std::max(1, static_cast<int>(std::lround(h->dps * HAZARD_TICK * share)));
                player.Damage(dmg);
                player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
                AddText(std::to_string(dmg), player.x, player.y - 44.0f, {255, 140, 60, 255});
            }
        } else {
            hazard_timer = 0.0f;
        }
    }

    if (!player.IsDead()) {
        ApplyPlayerAttack(ctx);
        ApplyPlayerAbility(ctx);
        ResolveInteractTarget(ctx);
        UpdateGathering(dt, ctx);
        if (gather_index < 0 && !player.GatherClip().empty()) player.StopGathering();

        // Step-through portals fire without a button press.
        //
        // But not for a walk that began on the previous map. Arrival spawns
        // sit a pace or two from the way back -- forty pixels on the field
        // outside Havenbrook -- so holding a direction through the fade used
        // to carry the player straight into the return portal and bounce them
        // back where they came from, over and over for as long as the key was
        // down. So the portals wait until the key has been let go -- and until
        // the player is standing clear of them, because letting go a step
        // too late leaves you on top of the way back, and arming it there
        // bounced you just the same.
        if (!portals_armed) {
            if (Length(player.hands.move.x, player.hands.move.y) < 0.01f)
                arrival_released = true;
            if (arrival_released && !map.PortalAt(player.Bounds()))
                portals_armed = true;
        }

        if (!transition_pending && portals_armed) {
            if (const Portal* p = map.PortalAt(player.Bounds()))
                if (!p->requires_interact && p->locked_by.empty()) {
                    if (p->min_combat > player.skills.CombatLevel()) {
                        if (gate_note_timer <= 0.0f) {
                            AddText("Too dangerous for you yet: Combat " + std::to_string(p->min_combat) + " needed.",
                                    player.x, player.y - 52.0f, {255, 150, 150, 255}, 2.2f);
                            Audio::Play(Sfx::Locked);
                            gate_note_timer = 2.5f;
                        }
                    } else if (RequestTransition(p->target_map, p->target_spawn)) {
                        Audio::Play(Sfx::Portal);
                    }
                }
        }
    } else {
        player.interact = {};
        gather_index = -1;
    }
}

void World::UpdateShared(float dt, const GameContext& ctx) {
    if (visiting) {
        // A window: the monsters, the shots and what lies on the ground are
        // posed by coop::Guest from what the host says. Only what is for
        // show runs here.
        for (auto& n : npcs) n->Update(dt, *this, ctx);
        UpdateImpacts(dt);
        UpdateDust(dt);
        ShedFromShots();
        ShedFromGround(dt);
        UpdateMotes(dt);
        UpdateElevation(dt);
        for (auto& p : pickups) p.bob += dt * 3.4f;
        UpdateTexts(dt);
        return;
    }

    // Who each monster is after: whoever is nearest, but it takes a clear
    // margin to turn one away from whoever it already has.
    const bool company = !guests.empty();
    if (company) {
        for (auto& e : enemies) {
            Player* best = nullptr;
            float best_d = 1e9f;
            for (Player* p : Players()) {
                if (p->IsDead() || p->puppet || p->resting) continue;
                float d = Length(p->x - e->x, p->y - e->y);
                if (static_cast<int>(p->seat) == e->target_seat) d *= 0.7f;
                // Whoever last drew blood is who it is angriest with, for a
                // few seconds; and Stand Fast outranks that, however far.
                if (static_cast<int>(p->seat) == e->GrudgeSeat()) d *= 0.25f;
                if (static_cast<int>(p->seat) == e->TauntedBy()) d = 0.0f;
                if (d < best_d) { best_d = d; best = p; }
            }
            e->target_seat = best ? static_cast<int>(best->seat) : -1;
        }
    }
    // Every monster is thought about once a frame, by whoever it is after.
    std::set<const Enemy*> thought;
    const auto think = [&](Enemy& e) {
        if (!thought.insert(&e).second) return;
        if (e.night) {
            const bool abroad = Abroad(e);
            if (e.CurrentState() == Enemy::State::Dead) {
                // Nightfall, for something whose night it is: up at its post,
                // but not under anybody's feet -- the same room a respawn waits for.
                const float clearance = e.Def() ? std::max(240.0f, e.Def()->aggro_range + 96.0f) : 240.0f;
                if (abroad && e.CorpseGone() && !AnyPlayerNear(e.home_x, e.home_y, clearance)) e.Revive();
                else e.Update(dt, *this, ctx);
                return;
            }
            // Dawn. It finishes the fight it is in first.
            if (!abroad && !e.Engaged()) { e.GoToGround(); return; }
        }
        if (e.CurrentState() == Enemy::State::Dead) {
            e.TickRespawn(dt);
            // Not while anyone is standing on its spawn point. A boar
            // killed where it grazed came back in the same spot a minute later,
            // already inside its aggro range, and a player who had stopped to
            // open their bag was dead before they closed it. Wait until they
            // are clear of where it would start chasing them.
            const float clearance = e.Def() ? std::max(192.0f, e.Def()->aggro_range + 64.0f)
                                            : 192.0f;
            const bool slain_today = e.Def() && e.Def()->is_boss && SlainToday(map_id, e.post);
            if (e.ReadyToRespawn() && !slain_today && !AnyPlayerNear(e.home_x, e.home_y, clearance)) e.Revive();
            else                                                                     e.Update(dt, *this, ctx);
            return;
        }
        e.Update(dt, *this, ctx);
    };
    if (!company) {
        for (auto& e : enemies) think(*e);
    } else {
        // One acting-as per player, with every monster after them thought
        // about inside it, rather than one per monster.
        for (Player* p : Players()) {
            // Not as somebody who is not there. On a map only friends are on
            // the host's own Player is a stand-in, standing at the arrival
            // point with `absent` set; thinking as it put every monster a
            // hundred yards from whoever it was chasing, once a frame, so it
            // gave up on the same frame it set off and never landed a blow.
            // The friend it is after is in the same loop, a line below.
            if (p == &player && player.absent) continue;
            bool any = false;
            for (auto& e : enemies) any |= e->target_seat == static_cast<int>(p->seat);
            const bool nobody = p == &player;      // and the ones after no one, with the host
            if (!any && !nobody) continue;
            // Read before acting as them: inside, the slot `p` points at
            // holds the host.
            const int seat = static_cast<int>(p->seat);
            ActAs(*p, [&] {
                for (auto& e : enemies)
                    if (e->target_seat == seat || (nobody && e->target_seat < 0)) think(*e);
            });
        }
        // And whatever is left: the ones after nobody on a map the host is not
        // on. They have no one to chase, but they still wander, rot and come
        // back, and none of that happens to a monster nobody thinks about.
        for (auto& e : enemies) think(*e);
    }

    for (auto& n : npcs) n->Update(dt, *this, ctx);

    UpdateProjectiles(dt, ctx);
    UpdateGroundEffects(dt, ctx);
    ForgetSpentCasts();
    UpdateImpacts(dt);
    UpdateDust(dt);
    ShedFromShots();
    ShedFromGround(dt);
    UpdateMotes(dt);
    UpdateElevation(dt);
    UpdatePickups(dt, ctx);
    UpdateTexts(dt);
}

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
                spell = ctx.spells->BestFor(player.SelectedElement(), player.skills.Level(SKILL_MAGIC));
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
        Audio::Play(Sfx::BowShot);
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
        p.extra_homing = player.talents.Effect("homing", style);
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
    const auto strike = [&](float radius, float delay, float mult, Element el) {
        const SDL_FPoint at = strike_point();
        GroundEffect g;
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
        AddText(ComboNameFor(atk.move, style), player.x, player.y - 58.0f, {255, 232, 150, 255}, 0.8f);
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

    if (technique == "volley") {
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
        strike(58.0f, 0.6f, damage_mult * 1.5f, element);
    // --- the ancient spells' shapes ----------------------------------------------
    } else if (shape == "darts") {
        // Three that seek: the old missile that does not miss.
        for (float deg : {-8.0f, 0.0f, 8.0f}) {
            const Vec2 d = turned(deg);
            if (Projectile* p = loose(d.x, d.y, damage_mult, true)) p->extra_homing += 6.0f;
        }
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

    constexpr int   LANES = 44;            // arrows a rain has on the go
    constexpr float CYCLE = 0.78f;         // a lane's arrow: down, stood, gone, and the next
    constexpr float FALL = 0.20f, STAND = 0.50f;
    constexpr float FROM_X = -46.0f, FROM_Y = -150.0f;   // where it comes from, off where it lands
    constexpr float SHAFT = 16.0f;

    for (const GroundEffect& g : ground_effects) {
        if (!g.rain) continue;
        // In flight before the first volley lands, and stopping as the last does.
        if (g.delay > FALL) continue;
        const float lift = LiftAt(g.x, g.y);
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

void World::Burst(float x, float y, float radius, SDL_Color color, int count) {
    for (int i = 0; i < count; ++i) {
        const float a = 6.2831853f * i / count;
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
    if (atk.move != ComboMove::None) return ComboName(atk.move);
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

    const float mult  = atk.damage_mult * player.TalentDamage(AttackStyle::Melee, atk.type);
    const float knock = atk.profile.knockback * (1.0f + player.talents.Effect("knockback", AttackStyle::Melee));

    // A combo says its name over the player as it comes out.
    if (atk.move != ComboMove::None)
        AddText(ComboName(atk.move), player.x, player.y - 58.0f, {255, 232, 150, 255}, 0.8f);

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

// -----------------------------------------------------------------------------
//  Interaction
// -----------------------------------------------------------------------------

void World::ResolveInteractTarget(const GameContext& ctx) {
    InteractTarget best;

    auto consider = [&](InteractTarget::Kind kind, int index,
                        const string& label, float tx, float ty) {
        const float d = Length(tx - player.x, ty - player.y);
        if (d > INTERACT_RANGE || d >= best.distance) return;
        best.kind = kind;
        best.index = index;
        best.label = label;
        best.distance = d;
    };

    for (size_t i = 0; i < npcs.size(); ++i)
        if (!npcs[i]->Away()) consider(InteractTarget::Npc, static_cast<int>(i),
                 "Talk to " + npcs[i]->Name() + (npcs[i]->Shop().empty() ? "" : "  -  trades"),
                 npcs[i]->x, npcs[i]->y);

    const auto& objects = map.Objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const MapObject& o = objects[i];
        string label;

        if (!ObjectPresent(o)) continue;
        if (o.type == "chest") {
            label = Flagged(o.id) ? "" : "Open chest";
        } else if (o.type == "storage") {
            // A chest you keep things in rather than one you loot once, so it
            // never goes quiet after the first use.
            // The whole title lower-cased, not just its first letter the way
            // a workbench does it: a chest is named "Storage Chest" on the
            // panel that opens, and "Open the storage Chest" is not a sentence.
            string noun = o.title.empty() ? string("chest") : o.title;
            for (char& c : noun) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            label = "Open the " + noun;
        } else if (o.type == "search") {
            // Something to look under or behind. Says what it is before it is
            // searched and nothing afterwards, like a chest.
            label = Flagged(o.id) ? "" : (o.title.empty() ? "Search" : o.title);
        } else if (o.type == "lever") {
            label = Flagged(o.id) ? "" : (o.title.empty() ? "Use it" : o.title);
        } else if (o.type == "note") {
            label = "Read note";
        } else if (o.type == "board") {
            label = o.title.empty() ? "Read mission board" : ("Read " + o.title);
        } else if (o.type == "sign") {
            label = "Read sign";
        } else if (o.type == "bed") {
            label = clock.CanSleep() ? "Go to bed" : "Bed  -  you can sleep after dusk";
        } else if (o.type == "campsite") {
            label = clock.CanSleep() ? "Sleep by the fire" : "Campsite  -  you can sleep after dusk";
        } else if (o.type == "camp") {
            label = clock.CanSleep() ? "Sleep at your camp" : "Pack up your camp";
        } else if (o.type == "dream_wake") {
            label = "Touch the stone and wake";
        } else if (o.type == "range" || o.type == "workbench") {
            // Map titles are written as names ("Kitchen fire", "Anvil"), but
            // here they follow "the" mid-sentence.
            string noun = o.title.empty() ? string(o.type == "range" ? "fire" : o.station)
                                          : o.title;
            noun[0] = static_cast<char>(tolower(static_cast<unsigned char>(noun[0])));
            label = (o.type == "range" ? "Cook at the " : "Use the ") + noun;
        } else if (o.type == "waystone") {
            label = Flagged(o.id) ? "Touch the waystone" : "Wake the waystone";
        } else if (o.type == "totem_circle") {
            label = player.talents.PlacedTotem().empty() ? "Touch the ring"
                  : player.talents.TotemAwake()          ? "Touch the totem"
                                                         : "Touch the totem  -  it is asleep";
        } else if (o.type == "altar") {
            string noun = o.title.empty() ? string("enchanting table") : o.title;
            noun[0] = static_cast<char>(tolower(static_cast<unsigned char>(noun[0])));
            label = "Use the " + noun;
        } else if (o.type == "herb") {
            // A picked plant offers nothing until it has grown back.
            if (!Picked(o)) {
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level)
                    label = "Needs Foraging " + std::to_string(o.skill_level) + " for the " + o.title;
                else
                    label = "Pick " + o.title;
            }
        // A felled tree or a worked-out seam offers nothing either, until it
        // is back.
        } else if (!o.skill.empty() && !Spent(o)) {
            const int s = SkillFromName(o.skill);
            const bool fishing = o.skill == "Fishing";
            if (s >= 0 && player.skills.Level(s) < o.skill_level) {
                label = "Needs " + o.skill + " " + std::to_string(o.skill_level) +
                        (o.title.empty() ? string("") : " for the " + o.title);
            } else {
                label = (fishing ? "Fish the " : o.skill == "Mining" ? "Mine " : "Chop ") +
                        (o.title.empty() ? string(fishing ? "water" : "node") : o.title);
                // Say before the button is pressed that the tool is missing.
                const string tool = Gathering::ToolFor(o.skill);
                if (ctx.items && !Gathering::BestTool(player.inventory, player.equipment, *ctx.items,
                                                      player.skills, tool))
                    label += string("  -  needs ") + Gathering::ToolNoun(tool);
            }
        }

        if (!label.empty())
            consider(InteractTarget::Object, static_cast<int>(i), label, o.x, o.y);
    }

    // Doors are only offered when nothing closer wants the button.
    if (const Portal* p = map.PortalAt(player.BodyBox()))
        if (p->requires_interact) {
            const float cx = p->rect.x + p->rect.w / 2.0f;
            const float cy = p->rect.y + p->rect.h / 2.0f;
            // Say so at the door when what is inside outclasses the player; the
            // mine used to be found out about by dying in its first room.
            string label = p->label;
            if (p->min_combat > player.skills.CombatLevel())
                label += "  -  needs Combat " + std::to_string(p->min_combat);
            else if (p->danger_level > player.skills.CombatLevel())
                label += "  -  dangerous: Combat " + std::to_string(p->danger_level) + " advised";
            consider(InteractTarget::PortalDoor, 0, label, cx, cy);
        }

    player.interact = best;
}

void World::TryInteract(const GameContext& ctx) {
    if (player.IsDead() || transition_pending) return;
    if (visiting) {
        // The host does it, and says what came of it.
        visitor_acts.push_back({1, std::to_string(static_cast<int>(player.interact.kind)), "", player.interact.index});
        return;
    }
    // Lying down, E gets up.
    if (player.resting) { player.resting = false; return; }

    // Interrupting a gather is what the button does while one is running.
    if (gather_index >= 0) { gather_index = -1; return; }

    const InteractTarget& t = player.interact;

    switch (t.kind) {
        case InteractTarget::Npc: {
            if (t.index < 0 || t.index >= static_cast<int>(npcs.size())) break;
            Npc& npc = *npcs[t.index];
            npc.FaceToward(player.x, player.y);

            if (npc.DialogueRoot().empty()) {
                AddText("...", npc.x, npc.y - 48.0f, {200, 200, 210, 255});
                break;
            }
            WorldRequest r;
            r.type  = WorldRequest::Type::Dialogue;
            r.id    = npc.Id();
            r.title = npc.Name();
            r.text  = npc.DialogueRoot();
            requests.push_back(r);
            break;
        }

        case InteractTarget::Object: {
            const auto& objects = map.Objects();
            if (t.index < 0 || t.index >= static_cast<int>(objects.size())) break;
            const MapObject& o = objects[t.index];

            if (o.type == "lever") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                AddText(o.text.empty() ? "It gives." : o.text, o.x, o.y - 34.0f, {200, 235, 255, 255}, 2.4f);
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    ctx.quests->Notify(e, player.inventory);
                }
                break;
            }
            if (o.type == "totem_circle") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Totem;
                r.id    = o.id;
                r.title = o.title.empty() ? "The ring" : o.title;
                requests.push_back(r);
                Audio::Play(Sfx::UiConfirm);
                break;
            }
            if (o.type == "storage") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Storage;
                r.id    = o.id;
                r.title = o.title.empty() ? "Storage Chest" : o.title;
                r.count = o.capacity > 0 ? o.capacity : 100;
                requests.push_back(r);
                Audio::Play(Sfx::ChestOpen);
                break;
            }
            if (o.type == "search") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                if (!o.loot_table.empty()) SpawnLoot(o.loot_table, o.x, o.y + 10.0f, ctx);
                if (!o.loot_item.empty())
                    DropItem(o.loot_item, std::max(1, o.loot_qty), o.x, o.y + 10.0f, ctx);
                AddText(o.text.empty() ? "There is something under it." : o.text,
                        o.x, o.y - 30.0f, {255, 225, 120, 255}, 2.4f);
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                break;
            }
            if (o.type == "chest") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                if (!o.loot_table.empty()) SpawnLoot(o.loot_table, o.x, o.y + 10.0f, ctx);
                // What a chest holds by name, which no loot table can roll.
                if (!o.loot_item.empty())
                    DropItem(o.loot_item, std::max(1, o.loot_qty), o.x, o.y + 10.0f, ctx);
                AddText("Opened!", o.x, o.y - 34.0f, {255, 225, 120, 255});
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    ctx.quests->Notify(e, player.inventory);
                }
            } else if (o.type == "bed" || o.type == "campsite") {
                AskToSleep(!o.title.empty() ? o.title : string(o.type == "bed" ? "A bed for the night" : "By the fire"),
                           o.fee);
            } else if (o.type == "camp") {
                if (clock.CanSleep()) {
                    AskToSleep("Your camp");
                } else if (player.inventory.Full()) {
                    AddText("No room in your pack for the bedroll.", player.x, player.y - 54.0f,
                            {255, 170, 150, 255}, 1.6f);
                    Audio::Play(Sfx::UiError);
                } else {
                    camp = {};
                    PlaceCampObjects();
                    player.inventory.Add("bedroll", 1);
                    AddText("Camp packed away.", player.x, player.y - 54.0f, {220, 220, 200, 255});
                    Audio::Play(Sfx::Pickup);
                }
            } else if (o.type == "dream_wake") {
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                Wake(WakeReason::Stone);
            } else if (o.type == "range" || o.type == "workbench") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Craft;
                r.id    = o.id;
                r.text  = o.type == "range" ? string("range") : o.station;
                r.title = !o.title.empty() ? o.title
                        : o.type == "range" ? string("Cooking fire")
                        : o.station == "anvil" ? string("Anvil")
                        : o.station == "loom" ? string("Loom")
                        : o.station == "rack" ? string("Tanning Rack") : string("Workbench");
                requests.push_back(r);
            } else if (o.type == "waystone") {
                if (!Flagged(o.id)) {
                    // The first hand on it wakes it, and that is all the first
                    // touch does: a stone is found before it is used.
                    SetFlag(o.id);
                    Burst(o.x, o.y - 40.0f, 90.0f, {150, 220, 255, 255}, 26);
                    AddText("The waystone wakes", o.x, o.y - 84.0f, {170, 228, 255, 255}, 2.4f);
                    Audio::PlayAt(Sfx::QuestStart, o.x, o.y);
                    WorldRequest r;
                    r.type = WorldRequest::Type::Toast;
                    r.text = "The waystone is awake. Touch it again to go to any other you have woken.";
                    requests.push_back(r);
                } else {
                    WorldRequest r;
                    r.type = WorldRequest::Type::Travel;
                    r.id   = o.id;
                    requests.push_back(r);
                }
            } else if (o.type == "altar") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Enchant;
                r.id    = o.id;
                r.title = o.title.empty() ? "Enchanting Table" : o.title;
                requests.push_back(r);
            } else if (o.type == "note" || o.type == "sign") {
                // Reading something can be what a quest asks for.
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                // A note can also leave something behind, but only once.
                if (!o.loot_table.empty() && !Flagged(o.id))
                    SpawnLoot(o.loot_table, o.x, o.y + 8.0f, ctx);

                WorldRequest r;
                r.type  = WorldRequest::Type::Note;
                r.id    = o.id;
                r.title = o.title.empty() ? (o.type == "sign" ? "Sign" : "A scrawled note") : o.title;
                r.text  = o.text;
                r.list  = o.starts_quest.empty() ? vector<string>{}
                                                 : vector<string>{o.starts_quest};
                requests.push_back(r);
                SetFlag(o.id);
            } else if (o.type == "board") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Board;
                r.id    = o.id;
                r.title = o.title.empty() ? "Mission Board" : o.title;
                r.list  = o.quests;
                requests.push_back(r);
            } else if (o.type == "herb") {
                if (Picked(o)) break;
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level) {
                    AddText("Foraging " + std::to_string(o.skill_level) + " needed", o.x, o.y - 30.0f,
                            {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                // Picked by hand: no tool, and the level alone sets the pace.
                gather_index  = t.index;
                gather_timer  = 0.0f;
                gather_needed = Gathering::WorkTime(o.gather_time, player.skills.Level(SKILL_FORAGING), 1.0f);
                player.StartGathering("gather", "", o.x, o.y);
            } else if (!o.skill.empty()) {
                const int s = SkillFromName(o.skill);
                if (s < 0 || !ctx.items) break;
                if (Spent(o)) break;
                if (player.skills.Level(s) < o.skill_level) {
                    AddText("Level too low", o.x, o.y - 34.0f, {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                // No work without the tool for it: the fastest one carried that
                // the player has the level to use.
                const string tool_kind = Gathering::ToolFor(o.skill);
                const ItemDef* locked = nullptr;
                const ItemDef* tool = Gathering::BestTool(player.inventory, player.equipment, *ctx.items,
                                                          player.skills, tool_kind, &locked);
                if (!tool) {
                    string why = string("You need ") + Gathering::ToolNoun(tool_kind) + ".";
                    if (locked)
                        for (const auto& req : locked->requirements)
                            why = "Your " + locked->name + " needs " + SkillName(req.first) + " " +
                                  std::to_string(req.second) + ".";
                    AddText(why, player.x, player.y - 54.0f, {255, 170, 150, 255}, 1.8f);
                    Audio::Play(Sfx::UiError);
                    break;
                }
                gather_index  = t.index;
                gather_timer  = 0.0f;
                // The level and the tool together decide the pace, down to a floor.
                gather_needed = Gathering::WorkTime(o.gather_time, player.skills.Level(s), tool->tool_speed);
                player.StartGathering(Gathering::ClipFor(o.skill), tool->model, o.x, o.y);
                if (o.skill == "Fishing") Audio::PlayAt(Sfx::Splash, o.x, o.y);
            }
            break;
        }

        case InteractTarget::PortalDoor: {
            const Portal* p = map.PortalAt(player.BodyBox());
            if (!p) break;
            if (!p->locked_by.empty() && !player.inventory.Has(p->locked_by)) {
                AddText("It is locked.", player.x, player.y - 52.0f, {255, 150, 150, 255});
                Audio::Play(Sfx::Locked);
                break;
            }
            if (p->min_combat > player.skills.CombatLevel()) {
                AddText("Too dangerous for you yet: Combat " + std::to_string(p->min_combat) + " needed.",
                        player.x, player.y - 52.0f, {255, 150, 150, 255}, 2.2f);
                Audio::Play(Sfx::Locked);
                break;
            }
            if (RequestTransition(p->target_map, p->target_spawn)) Audio::Play(Sfx::Door);
            break;
        }

        default: break;
    }
}

// Cooking works the way the rest of the skills do: stand at a fire, press the
// button, turn one raw thing into one cooked thing. Burning is possible until
// the level is comfortably above the recipe.
void World::CookOne(const MapObject& range, const GameContext& ctx) {
    if (!ctx.items) return;

    const int level = player.skills.Level(SKILL_COOKING);
    // The lowest requirement among raw things the player cannot cook yet, so
    // the refusal can name the level that would actually help.
    int needed = 0;

    for (int slot = 0; slot < player.inventory.SlotCount(); ++slot) {
        const ItemStack& stack = player.inventory.Slot(slot);
        if (stack.Empty()) continue;

        const ItemDef* def = ctx.items->Get(stack.id);
        if (!def || def->cook_result.empty()) continue;

        // Skip what is beyond the player rather than stopping at it. This used
        // to give up at the first raw item in the bag, so a boar haunch
        // (Cooking 12) sitting ahead of plain raw meat (Cooking 1) meant a new
        // character could cook nothing at all.
        if (level < def->cook_level) {
            needed = (needed == 0) ? def->cook_level : std::min(needed, def->cook_level);
            continue;
        }

        player.inventory.RemoveSlot(slot, 1);

        // Chance to burn falls away as the level climbs past the requirement.
        const int margin = level - def->cook_level;
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        const float burn_chance = std::max(0.0f, 0.34f - margin * 0.03f);

        if (ctx.rng && roll(*ctx.rng) < burn_chance) {
            AddText("Burnt!", player.x, player.y - 54.0f, {200, 110, 90, 255});
            Audio::Play(Sfx::Burn);
            player.GrantXp(SKILL_COOKING, std::max(1, def->cook_xp / 8));
            return;
        }

        player.inventory.Add(def->cook_result, 1);
        player.GrantXp(SKILL_COOKING, def->cook_xp);
        Audio::Play(Sfx::Cook);

        const ItemDef* cooked = ctx.items->Get(def->cook_result);
        AddText("+ " + (cooked ? cooked->name : def->cook_result),
                player.x, player.y - 54.0f, {200, 255, 200, 255});
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        return;
    }

    if (needed > 0)
        AddText("Cooking " + std::to_string(needed) + " needed",
                range.x, range.y - 34.0f, {255, 150, 150, 255});
    else
        AddText("Nothing raw to cook", range.x, range.y - 34.0f, {200, 200, 210, 255});
}

Inventory& World::Storage(const string& object_id, int slots, const ItemDatabase* db) {
    auto it = storage.find(object_id);
    if (it == storage.end())
        it = storage.emplace(object_id, Inventory(db, std::max(1, slots))).first;
    // A chest that has been saved and loaded comes back without a database,
    // and one whose capacity has been changed in the data comes back the old
    // size. Both are fixed here rather than at load, so there is one place
    // that knows what a chest is supposed to be.
    it->second.SetDatabase(db);
    it->second.Resize(std::max(1, slots));
    return it->second;
}

bool World::Picked(const MapObject& o) const {
    auto it = picked.find(map_id + ":" + o.id);
    return it != picked.end() && GameHours() < it->second;
}

void World::Pick(const MapObject& o) {
    picked[map_id + ":" + o.id] = GameHours() + o.regrow_hours;
    if (journal) picked_log.push_back({map_id + ":" + o.id, GameHours() + o.regrow_hours});
    // Anything already grown back is dropped, so the save does not keep
    // every herb ever picked.
    for (auto it = picked.begin(); it != picked.end();)
        it = (GameHours() >= it->second) ? picked.erase(it) : std::next(it);
}

bool World::Spent(const MapObject& o) const {
    return o.deplete > 0.0f && Picked(o);
}

void World::UpdateGathering(float dt, const GameContext& ctx) {
    if (gather_index < 0) return;

    const auto& objects = map.Objects();
    if (gather_index >= static_cast<int>(objects.size())) { gather_index = -1; return; }
    const MapObject& o = objects[gather_index];

    // Walking away cancels it, and so does walking at all: work is done
    // standing still.
    if (Length(o.x - player.x, o.y - player.y) > INTERACT_RANGE + 12.0f || player.Moving() ||
        player.Attacking() || player.IsJumping()) {
        gather_index = -1;
        return;
    }

    // A strike every so often while the work goes on, not just at the end.
    // Fishing is quiet until something bites, and so is picking.
    const bool fishing = o.skill == "Fishing" || o.type == "herb";
    constexpr float STRIKE = 0.62f;
    const float before = gather_timer;
    gather_timer += dt;
    if (!fishing && (std::floor(before / STRIKE) != std::floor(gather_timer / STRIKE) || before == 0.0f))
        Audio::PlayAt(o.skill == "Mining" ? Sfx::Mine : Sfx::Chop, o.x, o.y);
    if (gather_timer < gather_needed) return;

    const int skill = SkillFromName(o.skill);

    // A herb is picked once, then grows back. The further past its level the
    // forager is, the more often a plant gives two.
    if (o.type == "herb") {
        const ItemDef* d = ctx.items ? ctx.items->Get(o.yield) : nullptr;
        int count = 1;
        if (ctx.rng) {
            const float extra = Gathering::ForageExtraChance(player.skills.Level(SKILL_FORAGING), o.skill_level);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            if (unit(*ctx.rng) < extra) count = 2;
        }
        const int added = player.inventory.Add(o.yield, count);
        if (added <= 0) {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
        player.GrantXp(SKILL_FORAGING, (d && d->forage_xp > 0 ? d->forage_xp : o.yield_xp) * added);
        AddText("+ " + (added > 1 ? std::to_string(added) + " " : string("")) + (d ? d->name : o.yield),
                player.x, player.y - 54.0f, added > 1 ? SDL_Color{255, 230, 140, 255} : SDL_Color{200, 255, 200, 255});
        Audio::PlayAt(Sfx::Pickup, o.x, o.y);
        Pick(o);
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        gather_index = -1;
        return;
    }

    if (fishing && ctx.items && ctx.rng) {
        const int level = player.skills.Level(SKILL_FISHING);
        const string fish = Gathering::PickFish(o.fish, level, *ctx.items, *ctx.rng);
        const ItemDef* d = ctx.items->Get(fish);
        if (!d) { gather_index = -1; return; }
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const int count = Gathering::CatchCount(level, unit(*ctx.rng));
        const int added = player.inventory.Add(fish, count);
        if (added <= 0) {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
        player.GrantXp(SKILL_FISHING, d->fish_xp * added);
        AddText("+ " + (count > 1 ? std::to_string(count) + " " : string("")) + d->name,
                player.x, player.y - 54.0f, count > 1 ? SDL_Color{255, 230, 140, 255} : SDL_Color{200, 255, 200, 255});
        Audio::PlayAt(Sfx::Splash, o.x, o.y, 1.0f, 1.2f);
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        gather_timer = 0.0f;
        return;
    }

    // The log goes in the bag before the swing is paid for. It used to be the
    // other way round: with a full pack every swing still paid its experience,
    // and because the refusal below returned before the dice were rolled the
    // tree never came down either -- a full bag made any seam an endless one.
    if (!o.yield.empty()) {
        const int got = player.inventory.Add(o.yield, 1 + DreamBonus(o.yield));
        if (got > 0) {
            if (skill >= 0 && o.yield_xp > 0) player.GrantXp(skill, o.yield_xp);
            const ItemDef* d = ctx.items ? ctx.items->Get(o.yield) : nullptr;
            AddText("+ " + (got > 1 ? std::to_string(got) + " " : string("")) + (d ? d->name : o.yield),
                    player.x, player.y - 54.0f, {200, 255, 200, 255});
            if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        } else {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
    } else if (skill >= 0 && o.yield_xp > 0) {
        // Something worked for its own sake, with nothing to carry away.
        player.GrantXp(skill, o.yield_xp);
    }

    // The dice roll. A tree does not stand there giving logs for ever: on
    // each one there is a chance it comes down, and on each ore a chance the
    // seam gives out. Then it is gone for a while and the work stops.
    if (o.deplete > 0.0f && ctx.rng) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        if (Gathering::Depletes(o.deplete, unit(*ctx.rng))) {
            Pick(o);
            const bool tree = o.type == "tree";
            AddText(tree ? "The tree comes down." : "The seam is worked out.",
                    o.x, o.y - 30.0f, {220, 220, 200, 255}, 1.8f);
            Audio::PlayAt(tree ? Sfx::Land : Sfx::Mine, o.x, o.y, 1.0f, 0.6f);
            gather_index = -1;
            return;
        }
    }

    // Keep going until the player moves or presses the button again.
    gather_timer = 0.0f;
}

void World::ShowGather(float progress, const string& clip, const string& model) {
    if (clip.empty()) {
        if (shown_gather > 0.0f || !player.GatherClip().empty()) player.StopGathering();
        shown_gather = 0.0f;
        return;
    }
    if (player.GatherClip() != clip) {
        const float dx = player.facing == FACE_LEFT ? -1.0f : player.facing == FACE_RIGHT ? 1.0f : 0.0f;
        const float dy = player.facing == FACE_UP ? -1.0f : player.facing == FACE_DOWN ? 1.0f : 0.0f;
        player.StartGathering(clip, model, player.x + dx * 24.0f, player.y + dy * 24.0f);
    }
    shown_gather = std::max(0.001f, progress);
}

float World::GatherProgress() const {
    if (visiting) return std::clamp(shown_gather, 0.0f, 1.0f);
    if (gather_index < 0 || gather_needed <= 0.0f) return 0.0f;
    return std::clamp(gather_timer / gather_needed, 0.0f, 1.0f);
}

// -----------------------------------------------------------------------------
//  Loot and floating text
// -----------------------------------------------------------------------------

// One place where a hit lands, whether it came from a sword, an arrow or a
// bolt of fire, so the element matchup and the XP are applied consistently.
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
    DamageResult r = RollAttack(owner, e.Profile(), style, damage_mult, *ctx.rng, true);

    // Every way the player can hurt something -- swing, arrow, bolt, burning
    // ground -- comes through here, so this is where the bar first appears.
    // Before the miss check: a swing that misses has still started the fight.
    e.RevealHealthBar();

    if (!r.hit) {
        AddText("miss", e.x, e.y - 46.0f, {150, 150, 168, 235});
        return;
    }

    // Elements only matter when both sides have one.
    const float matchup = ElementMultiplier(element, e.ElementOf());
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

    const float steal = player.talents.Effect("lifesteal", style);
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
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
                    if (!RectsOverlap(box, e->BodyBox())) continue;

                    const void* key = e.get();
                    if (std::find(p.already_hit.begin(), p.already_hit.end(), key) !=
                        p.already_hit.end())
                        continue;
                    p.already_hit.push_back(key);

                    ActAs(OwnerOf(p.owner_local, p.owner_seat), [&] {
                        crit_next = p.sure_crit;
                        cast_next = p.cast_id;
                        HitEnemy(*e, p.owner, p.style, p.element, p.damage_mult,
                                 p.def->knockback * p.knockback_mult, p.x - p.vx, p.y - p.vy, ctx);
                        crit_next = false;
                        cast_next = 0;
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
                    DamageResult r = RollAttack(p.owner, player.Profile(), p.style,
                                                p.damage_mult, *ctx.rng);
                    if (r.hit && r.damage > 0) {
                        // Where the shot came from is back along its flight.
                        HitPlayer(r.damage, p.owner, p.x - p.vx, p.y - p.vy);
                    } else {
                        AddText("miss", player.x, player.y - 44.0f, {150, 150, 168, 235});
                    }
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
                g.burst = true;
                AddGroundEffect(g);
            }
        }
    }

    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
                                     [](const Projectile& p) { return p.finished; }),
                      projectiles.end());
}

void World::UpdateElevation(float dt) {
    // One pass a frame rather than a lookup inside every draw call: the height
    // grid is a hash and a clamp, but Render is called from a sorted queue that
    // may visit the same entity's bounds several times.
    if (!map.HasElevation()) {
        // A hop on flat ground still leaves the ground.
        player.draw_lift = player.IsJumping() ? player.JumpLift() : player.RushLift();
        // A puppet's lift is what the host said it was.
        for (auto& g : guests) if (!g->puppet) g->draw_lift = g->IsJumping() ? g->JumpLift() : g->RushLift();
        for (auto& e : enemies) e->draw_lift = 0.0f;
        for (auto& n : npcs)    n->draw_lift = 0.0f;
        return;
    }
    // Walking up a flight of steps crosses from one level's cell to the next
    // in a single pixel, and the lift used to go with it: a whole level, all at
    // once. It closes on the ground's height instead, a level in about a tenth
    // of a second, so a climb is a climb. Anything more than two levels off --
    // a map just loaded, someone just arrived -- is simply put there.
    const auto settle = [&](float& lift, float want) {
        const float gap = want - lift;
        const float step = ELEVATION_RISE * 9.0f * std::max(0.0f, dt);
        if (dt <= 0.0f || fabsf(gap) > ELEVATION_RISE * 2.5f || fabsf(gap) <= step) lift = want;
        else lift += gap > 0.0f ? step : -step;
    };
    const auto walker = [&](Player& p) {
        if (p.IsJumping()) { p.ground_lift = p.JumpLift(); p.draw_lift = p.ground_lift; return; }
        settle(p.ground_lift, map.HeightAt(p.x, p.y));
        p.draw_lift = p.ground_lift + p.RushLift();
    };
    walker(player);
    for (auto& g : guests) if (!g->puppet) walker(*g);
    for (auto& e : enemies) settle(e->draw_lift, map.HeightAt(e->x, e->y));
    for (auto& n : npcs)    settle(n->draw_lift, map.HeightAt(n->x, n->y));
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
                      float speed, float size, float lift) {
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
            Mote m = Speck(px, py, Between(-8, 8), Between(-8, 8), Between(0.2f, 0.4f), 1.0f, SPARK_NEW, SPARK_OLD);
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
            ShedTrail(motes, seen.shed, p.x, p.y, p.vx / speed, p.vy / speed, speed, seen.size, seen.lift);
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
        if (g.finished || !g.Active() || g.rain || g.once || g.element == Element::None) continue;
        const float lift = LiftAt(g.x, g.y);
        const float rate = g.radius * (g.element == Element::Earth ? 4.0f : g.element == Element::Fire ? 3.2f : 1.6f);
        int n = static_cast<int>(rate * dt);
        if (Chance(rate * dt - n)) ++n;
        for (; n > 0; --n) {
            // Anywhere in the circle, evenly.
            const float a = Between(0.0f, 6.2831853f), d = sqrtf(Between(0.0f, 1.0f)) * g.radius * 0.92f;
            const float x = g.x + cosf(a) * d, y = g.y + sinf(a) * d;
            Mote m;
            switch (g.element) {
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

void World::UpdateImpacts(float dt) {
    for (Impact& im : impacts) {
        im.life -= dt;
        if (im.life <= 0.0f) im.finished = true;
    }
    impacts.erase(std::remove_if(impacts.begin(), impacts.end(),
                                 [](const Impact& i) { return i.finished; }),
                  impacts.end());
}

void World::UpdateGroundEffects(float dt, const GameContext& ctx) {
    for (GroundEffect& g : ground_effects) {
        if (g.finished) continue;

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
            Burst(g.x, g.y, g.radius * 0.8f, {226, 210, 172, 255}, 4);
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
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
                    if (!inside(*e)) continue;
                    if (g.finished && g.once) break;      // a snare holds one thing
                    crit_next = g.sure_crit;
                    cast_next = g.cast_id;
                    if (g.hit_mult >= 0.0f)
                        HitEnemy(*e, g.owner, g.style, g.element, g.hit_mult, g.knockback, g.x, g.y, ctx);
                    else
                        HitEnemy(*e, g.owner, AttackStyle::Magic, g.element,
                                 static_cast<float>(g.damage) * 0.5f, 8.0f, g.x, g.y, ctx);
                    crit_next = false;
                    cast_next = 0;
                    if (g.stagger > 0.0f) e->Stagger(g.stagger);
                    if (g.once) {
                        g.finished = true;
                        AddText("Snared", e->x, e->y - 64.0f, {200, 190, 160, 255}, 1.4f);
                        Burst(g.x, g.y, g.radius, {200, 190, 160, 255}, 10);
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
    player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
    AddText(std::to_string(damage), player.x, player.y - 44.0f, {255, 60, 40, 255}, 1.2f);
    player.GrantXp(SKILL_DEFENCE, std::max(1, damage));
    player.knock_x += knock_x * push;
    player.knock_y += knock_y * push;
    return damage;
}

void World::SpawnLoot(const string& table_id, float x, float y, const GameContext& ctx) {
    if (!ctx.loot) return;
    vector<LootDrop> drops = ctx.loot->Roll(table_id);
    // Deeper in the dream there is more of it: once for whatever this was, on
    // the first of its shards, not once a stack.
    for (auto& d : drops)
        if (const int more = DreamBonus(d.item)) { d.qty += more; break; }

    int index = 0;
    for (const auto& d : drops) {
        // Fan the pile out so overlapping drops stay clickable.
        const float angle = 1.9f * index;
        const float radius = drops.size() > 1 ? 9.0f + 3.0f * index : 0.0f;
        DropItem(d.item, d.qty, x + cosf(angle) * radius, y + sinf(angle) * radius * 0.6f, ctx);
        ++index;
    }
}

void World::DropItem(const string& item_id, int qty, float x, float y,
                     const GameContext& ctx, bool by_player) {
    if (item_id.empty() || qty <= 0) return;
    if (visiting) {
        if (by_player) visitor_acts.push_back({2, item_id, "", qty});
        return;
    }
    Pickup p;
    p.net_id = next_net_id++;
    p.dropper_seat = player.seat;
    p.item_id = item_id;
    p.qty     = qty;
    p.dropped = by_player;
    p.x = x;
    p.y = y;
    if (const ItemDef* d = ctx.items ? ctx.items->Get(item_id) : nullptr) p.icon = d->icon;
    pickups.push_back(p);
}

void World::UpdatePickups(float dt, const GameContext& ctx) {
    (void)ctx;
    for (auto& p : pickups) {
        p.life += dt;
        p.bob  += dt * 3.4f;
        if (p.dropped && p.life > DROP_LIFE) p.collected = true;
    }
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                 [](const Pickup& p) { return p.collected; }),
                  pickups.end());
}

void World::CollectPickups(float dt, const GameContext& ctx) {
    for (auto& p : pickups) {
        if (p.collected || p.life < PICKUP_ARM) continue;
        if (player.IsDead()) continue;
        const float dist = Length(p.x - player.x, p.y - player.y);
        if (p.dropped && !p.cleared && p.dropper_seat == player.seat) {
            // Whoever put it down steps clear of it once before it can be
            // theirs again. Anyone else may have it: that is how things
            // change hands.
            if (dist > DROP_CLEAR) p.cleared = true;
            continue;
        }
        if (dist > PICKUP_RANGE) continue;

        const int added = player.inventory.Add(p.item_id, p.qty);
        if (added <= 0) {
            // Say so once every couple of seconds rather than every frame.
            if (fmodf(p.life, 2.0f) < dt)
                AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            continue;
        }

        const ItemDef* d = ctx.items ? ctx.items->Get(p.item_id) : nullptr;
        const string name = d ? d->name : p.item_id;
        AddText("+" + std::to_string(added) + " " + name, player.x, player.y - 50.0f,
                {230, 230, 255, 255});
        Audio::Play(p.item_id == "coins" ? Sfx::Coins : Sfx::Pickup);

        p.collected = true;
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
    }

    pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                 [](const Pickup& p) { return p.collected; }),
                  pickups.end());
}

void World::AddText(const string& text, float x, float y, SDL_Color color, float life) {
    FloatingText t;
    t.text = text;
    t.x = x;
    // Lifted with whoever it is about. Text is put forty-odd pixels over
    // someone's feet, so the ground asked about is the ground under those. A
    // guest's window is told where the host already put it.
    t.y = visiting ? y : y - LiftAt(x, y + 46.0f);
    t.life = t.max_life = life;
    t.color = color;
    texts.push_back(t);
    if (journal) text_log.push_back(t);
}

void World::UpdateTexts(float dt) {
    for (auto& t : texts) t.life -= dt;
    texts.erase(std::remove_if(texts.begin(), texts.end(),
                               [](const FloatingText& t) { return t.life <= 0.0f; }),
                texts.end());
}

vector<WorldRequest> World::TakeRequests() {
    vector<WorldRequest> out;
    out.swap(requests);
    return out;
}

// -----------------------------------------------------------------------------
//  Rendering
// -----------------------------------------------------------------------------

void World::Render(SDL_Renderer* r, TextureCache& cache) const {
    const SDL_Color bg = map.BackgroundColor();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);
    if (InDream()) RenderStars(r);

    map.RenderLayer(r, cache, camera, LAYER_GROUND);
    // The exposed earth on the downhill side of every raised cell, drawn over
    // the ground and under everything that stands on it.
    map.RenderCliffs(r, cache, camera);

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
        // As round as what it burns, and on the ground it lies on.
        const SDL_FPoint centre = camera.ToScreen(g.x, g.y - LiftAt(g.x, g.y));
        const float rx = g.radius * camera.zoom;
        const float ry = g.radius * camera.zoom;
        const SDL_Color c = ElementColor(g.element);

        if (g.delay > 0.0f) {
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

    vector<const TileInstance*> decor;
    map.CollectDecor(camera, decor);
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
    if (!player.IsDead() || player.DeathTimer() > 0.0f)
        queue.push_back({player.SortY(), 1, &player});
    for (const auto& g : guests)
        if (RectsOverlap(g->BodyBox(), view)) queue.push_back({g->SortY(), 1, g.get()});

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

                SDL_SetTextureAlphaMod(tex, alpha);
                if (dulled) SDL_SetTextureColorMod(tex, 118, 112, 108);
                SDL_RenderTexture(r, tex, nullptr, &dst);
                if (dulled) SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                break;
            }
        }
    }

    // Embers, drops and the rest, over everything that stands: see Mote.
    DrawMotes(r);

    // The player's swing, over everything at ground level: it is the one
    // thing on screen that says where a blow is landing.
    DrawSwing(r);
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
    lighting.Render(r, camera, AmbientLight(), CollectLights());

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
