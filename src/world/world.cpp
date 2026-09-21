#include "world.h"
#include "../input.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include "../systems/dialogue.h"
#include "../systems/spell.h"
#include "../systems/audio.h"
#include "../systems/gathering.h"

static constexpr float FADE_SPEED     = 3.2f;
static constexpr float HAZARD_TICK = 0.5f;

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
    queued_shots.clear();
    slabs.clear();
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
        UpdateSlabs(dt);
        ShedFromShots();
        ShedFromGround(dt);
        ShedFromStatuses(dt);
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
    ShedFromStatuses(dt);
    UpdateMotes(dt);
    UpdateElevation(dt);
    UpdatePickups(dt, ctx);
    UpdateTexts(dt);
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
