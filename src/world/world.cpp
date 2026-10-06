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
    // A ritual the world's own player lit ends as they go; a friend's goes on
    // with the map, in the world of its own it is left in (see HandOver).
    if (ritual.active && ritual.owner_host && !visiting) EndRitual(false, ctx);
    // Friends on the map being left stay on it, in a world of their own.
    if (before_unload && map.Loaded()) before_unload(*this);
    map = std::move(arriving);
    ritual = Ritual{};
    ring_heard = RingHeard{};

    map_id = id;
    // A fight's own flags -- a boss's summons, called one after another (the
    // Pit Lord's, 80) -- are the fight's: gone as the map is, so the next
    // time it is fought from the start.
    for (auto it = flags.begin(); it != flags.end();) {
        if (it->rfind("FIGHT_", 0) == 0) { it = flags.erase(it); ++story_version; }
        else ++it;
    }
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
    falls.clear();
    claws.clear();
    shocks.clear();
    ripples.clear();
    swim_seen.clear();
    shake = flash_amount = 0.0f;
    ice_cracks.clear();
    ice_holes.clear();
    strikes.clear();
    strike_log.clear();
    ice_strain = ice_grace = 0.0f;
    ice_sink = -1.0f;
    ice_safe_known = false;
    ice_warned = 0;
    targeting.Clear();
    gather_index = -1;
    player.StopGathering();

    SpawnEntitiesFromMap(ctx);
    // Whoever the story has asleep, moved or gone; doors as they stand.
    SettleStory(true);
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
    HoldWayBack(p.x, p.y);

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

vector<string> World::DreamBounties(const QuestLog& quests, int day, const string& maps_dir) {
    // Every bounty, by the land its kills are to be made on.
    std::map<string, vector<const QuestDef*>> lands;
    for (const auto& kv : quests.Definitions())
        if (kv.second.bounty && !kv.second.stages.empty() && !kv.second.stages.front().map_id.empty())
            lands[kv.second.stages.front().map_id].push_back(&kv.second);
    // A land's posts are read from its file once: it does not change while
    // the game is running.
    static std::map<string, vector<EnemySpawnDef>> known;
    vector<string> out;
    for (auto& [land, wanted] : lands) {
        const string path = maps_dir + "/" + land + ".mx";
        auto posts = known.find(path);
        if (posts == known.end()) posts = known.emplace(path, Map::ReadPosts(path)).first;
        // What is out there tonight, kind by kind. Nothing that comes and goes
        // -- a night visitor, a roamer, a ritual's -- is counted on.
        std::map<string, int> tonight;
        for (size_t i = 0; i < posts->second.size(); ++i) {
            const EnemySpawnDef& post = posts->second[i];
            if (post.night || !post.route.empty() || !post.ritual.empty()) continue;
            ++tonight[ResolveSpawn(post, land, day, static_cast<int>(i)).type];
        }
        vector<const QuestDef*> able;
        for (const QuestDef* q : wanted)
            if (tonight[q->stages.front().target] >= q->stages.front().count) able.push_back(q);
        // Which of them, by the night: the same all night, others the next.
        std::sort(able.begin(), able.end(), [](const QuestDef* a, const QuestDef* b) { return a->id < b->id; });
        uint32_t seed = 2166136261u;
        for (char c : land) seed = (seed ^ static_cast<unsigned char>(c)) * 16777619u;
        seed ^= static_cast<uint32_t>(day) * 2654435761u;
        std::mt19937 rng(seed);
        std::shuffle(able.begin(), able.end(), rng);
        for (size_t i = 0; i < able.size() && i < static_cast<size_t>(BOUNTIES_PER_LAND); ++i)
            out.push_back(able[i]->id);
    }
    std::sort(out.begin(), out.end());
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

World::RoamDay World::RoamDraw(const string& map_id, int day, int index, float chance, int points) {
    uint32_t h = 2166136261u;
    for (char c : map_id) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    h = (h ^ static_cast<uint32_t>(day)) * 16777619u;
    h = (h ^ 0x726f616du) * 16777619u;                         // "roam"
    h = (h ^ static_cast<uint32_t>(index)) * 16777619u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    RoamDay d;
    d.out = chance >= 1.0f || (chance > 0.0f && static_cast<float>(h & 0xFFFFu) / 65536.0f < chance);
    d.start = points > 0 ? static_cast<int>((h >> 16) % static_cast<uint32_t>(points)) : 0;
    return d;
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

void World::TellTheHour(const GameContext& ctx) {
    const double now = clock.GameHours();
    for (Player* p : Players()) {
        const vector<string> gone = p->talents.SetNow(now);
        if (gone.empty()) continue;
        // Health or mana may have been the boon: the pools are what they now are.
        p->SyncHitpoints();
        p->SyncMana();
        // A friend is told on their own machine, where their clock runs too.
        if (p != &player) continue;
        for (const string& id : gone) {
            const BoonDef* b = ctx.trees ? ctx.trees->Boon(id) : nullptr;
            WorldRequest r;
            r.type = WorldRequest::Type::Toast;
            r.text = "A day has passed: your boon of " + (b ? b->name : id) + " has worn off.";
            requests.push_back(r);
        }
    }
}

void World::AwardBoss(const string& boss_id, const GameContext& ctx) {
    if (boss_id.empty()) return;
    std::mt19937 spare(0xb055u);
    const Talents::Trophy won = player.talents.SlayBoss(boss_id, ctx.rng ? *ctx.rng : spare);
    if (won.totem) {
        // Into the bag, or not yet. It was dropped at their feet when the
        // pack was full, and lost with the next map; now the next kill offers
        // it again, until it goes in (Talents::TotemGiven).
        const EnemyDef* whose = ctx.enemies ? ctx.enemies->Get(boss_id) : nullptr;
        const ItemDef* thing = ctx.items ? ctx.items->Get(won.totem->item) : nullptr;
        const bool given = player.inventory.Add(won.totem->item, 1) > 0;
        if (given) player.talents.TotemGiven(boss_id);
        WorldRequest t;
        t.type = WorldRequest::Type::Toast;
        if (!given) {
            t.text = string(whose ? whose->name : boss_id) +
                     " would leave you its totem, but your pack is full. Make room: the next time, it will.";
            requests.push_back(t);
        } else {
            t.text = string(whose ? whose->name : boss_id) + ", " + std::to_string(won.kills) +
                     " times: it leaves you its totem.";
            requests.push_back(t);
            t.text = (thing ? thing->name : won.totem->item) + ". Stand it in the ring in your house at Mossvale.";
            requests.push_back(t);
            AddText("A totem", player.x, player.y - 94.0f, {255, 214, 120, 255}, 3.2f);
            Burst(player.x, player.y - 30.0f, 80.0f, {255, 190, 90, 255}, 26);
            Audio::PlayAt(Sfx::QuestComplete, player.x, player.y);
        }
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
        r.text = "It lasts a day: " + std::to_string(static_cast<int>(Talents::BOON_HOURS)) + " hours by the clock.";
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

SDL_FPoint World::OpenGroundNear(float x, float y, const SDL_FRect& foot) const {
    const auto open = [&](float px, float py) {
        return !map.Blocked(SDL_FRect{px + foot.x, py + foot.y, foot.w, foot.h});
    };
    if (open(x, y)) return {x, y};
    const int level = map.LevelAt(x, y);
    for (float r = 8.0f; r <= 320.0f; r += 8.0f) {
        const int steps = std::max(8, static_cast<int>(r * 6.2831853f / 8.0f));
        for (int i = 0; i < steps; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(steps);
            const float px = x + cosf(a) * r, py = y + sinf(a) * r;
            if (open(px, py) && map.LevelAt(px, py) == level) return {px, py};
        }
    }
    return map.DefaultSpawn();
}

void World::CatchUpUsedObjects(const GameContext& ctx) {
    // Whoever `player` is, with their own journal: alone, the host; Player
    // Two while the game serves them; a friend at their own machine, whose
    // journal is the real one there (a relay is only listening).
    QuestLog* log = ctx.quests;
    if (!log || log->relay) return;
    for (const string& id : log->Active()) {
        const QuestDef* d = log->Definition(id);
        const int stage = log->Stage(id);
        if (!d || stage < 0 || stage >= static_cast<int>(d->stages.size())) continue;
        const QuestStage& st = d->stages[stage];
        // Something a quest asks to be used is the character's own (the
        // self-test holds every one to it): what they have used themselves,
        // never what a friend has.
        if (st.type != ObjectiveType::Interact || st.target.empty() || !UsedOwn(st.target)) continue;
        QuestEvent e;
        e.type   = ObjectiveType::Interact;
        e.target = st.target;
        e.map_id = st.map_id;
        e.quest  = id;
        log->Notify(e, player.inventory);
    }
}

void World::SettleStory(bool force) {
    if (!force && story_settled == story_version) return;
    story_settled = story_version;
    for (auto& n : npcs) {
        if (n->actor || n->scripted) continue;
        const vector<NpcState>& states = n->States();
        int pick = -1;
        for (size_t i = 0; i < states.size(); ++i)
            if (Holds(states[i].when)) { pick = static_cast<int>(i); break; }
        if (force || pick != n->StateIndex()) n->ApplyState(pick);
    }
    for (const MapObject& o : map.Objects()) {
        // Not there, nothing to walk into -- a cart left in the road only on
        // the prologue's morning -- and a door open is nothing in the way.
        if (o.type != "door" && o.when.Empty()) continue;
        bool solid = o.when.Empty() || Holds(o.when);
        if (o.type == "door") solid = solid && !Holds(o.open_if);
        map.SetColliderOn(o.collider, solid);
    }
    for (auto& e : enemies) {
        if (e->dormant && !e->wake_flag.empty() && Flagged(e->wake_flag)) e->WakeUp();
        // Its story come: on its way, out of smoke (World::Update).
        if (e->held_back && Holds(e->appear_when)) {
            e->held_back = false;
            e->appear_in = e->appear_after;
        }
    }
}

void World::UpdateSquads(float dt) {
    // Coming out of smoke, each when its time comes.
    for (auto& e : enemies) {
        if (e->appear_in < 0.0f || e->puppet) continue;
        if ((e->appear_in -= dt) > 0.0f) continue;
        e->appear_in = -1.0f;
        e->Revive();
        e->Provoke(-1);
        Smoke(e->x, e->y - 20.0f, 30.0f * (e->Def() ? e->Def()->scale : 1.0f), false);
        Audio::PlayAt(Sfx::Vanish, e->x, e->y, 0.6f, 0.8f);
    }
    // A squad all down sets its flag. A friend's machine is told the flag.
    if (visiting || (squad_check -= dt) > 0.0f) return;
    squad_check = 0.2f;
    std::map<string, bool> down;
    for (auto& e : enemies) {
        if (e->squad.empty() || e->puppet) continue;
        bool& all = down.emplace(e->squad, true).first->second;
        if (e->CurrentState() != Enemy::State::Dead || e->Pending()) all = false;
    }
    for (const auto& [flag, all] : down)
        if (all && !Flagged(flag)) {
            SetFlag(flag);
            SettleStory();
        }
}

void World::SetReverieVeil(float v) {
    reverie_veil = std::clamp(v, 0.0f, 1.0f);
    // Its motes come with it, and go with it.
    const bool want = reverie_veil > 0.05f && !InDream();
    if (want != veil_motes) {
        veil_motes = want;
        ambience.SetKind(want ? string("dream") : map.Ambient(), map.IsInterior());
    }
}

bool World::EnterDream(const string& dream_map, const string& spawn, bool story, bool locked) {
    if (transition_pending || player.IsDead()) return false;
    dream.active = true;
    dream.map = map_id;
    dream.x = player.x;
    dream.y = player.y;
    dream.story = story;
    dream.locked = locked;
    dream.talisman = false;
    player.Rest();
    targeting.Clear();
    if (!RequestTransition(dream_map, spawn)) return false;
    fade_speed = SLEEP_FADE_SPEED;
    flip = true;
    Audio::Play(Sfx::Sleep);
    return true;
}

bool World::TearInto(const string& dream_map, const string& spawn) {
    if (!EnterDream(dream_map, spawn, true, true)) return false;
    // Faster than falling asleep, and torn: see ScreenFrame.
    fade_speed = SLEEP_FADE_SPEED * 2.6f;
    tear = true;
    return true;
}

void World::PassShadow(const string& image, float x0, float y0, float x1, float y1, float time, float alpha) {
    shadow.on = true;
    shadow.image = image;
    shadow.x0 = x0; shadow.y0 = y0; shadow.x1 = x1; shadow.y1 = y1;
    shadow.t = 0.0f;
    shadow.time = std::max(0.1f, time);
    shadow.alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void World::SweepBeam(float x, float y, float x0, float y0, float x1, float y1, float time, SDL_Color colour) {
    beam.on = true;
    beam.x = x; beam.y = y;
    beam.x0 = x0; beam.y0 = y0; beam.x1 = x1; beam.y1 = y1;
    beam.t = 0.0f;
    beam.time = std::max(0.1f, time);
    beam.colour = colour;
}

string World::DreamTwin(const string& map_id) {
    // The places the Reverie dreams as they stand, street for street: Havenbrook
    // through the mirror at the foot of the Dreaming Dark, and the four through
    // Dream Havenbrook's mirrors.
    if (map_id == "town_havenbrook")    return "dream_havenbrook";
    if (map_id == "college_grounds")    return "dream_college";
    if (map_id == "ashen_path")         return "dream_ashen_path";
    if (map_id == "bayou")              return "dream_bayou";
    if (map_id == "plateau_stronghold") return "dream_plateau";
    return "";
}

bool World::TalismanShift(string& why) {
    why.clear();
    // Not worn: nothing to say -- most of the game, there is no talisman.
    if (player.equipment.InSlot(SLOT_TALISMAN).empty()) return false;
    if (visiting || Acting()) {
        why = "In company the talisman is the host's to use, for now.";
        return false;
    }
    if (transition_pending || player.IsDead()) return false;
    if (talisman_rest > 0.0f) {
        why = "The talisman is still warm.";
        return false;
    }
    if (InDream()) {
        // Out the way it came in. A dream it did not open -- the night's, a
        // sleeper's -- is not its to end.
        if (!dream.active || !dream.talisman || dream.map.empty()) {
            why = "The talisman is cold here.";
            return false;
        }
        const string back = dream.map;
        const float x = dream.x, y = dream.y;
        if (!RequestTransition(back, "")) return false;
        next_has_point = true;
        next_x = x;
        next_y = y;
        dream.talisman = false;
    } else {
        const string twin = DreamTwin(map_id);
        if (twin.empty()) {
            why = "The veil will not part here.";
            return false;
        }
        const float x = player.x, y = player.y;
        if (!RequestTransition(twin, "")) return false;
        dream.active = true;
        dream.map = map_id;
        dream.x = x;
        dream.y = y;
        dream.story = dream.locked = false;
        dream.talisman = true;
        next_has_point = true;
        next_x = x;
        next_y = y;
    }
    // A step through, not a night's sleep: nothing healed, no time gone.
    talisman_landing = true;
    talisman_rest = TALISMAN_REST;
    targeting.Clear();
    fade_speed = FADE_SPEED * 1.6f;
    flip = true;
    Flash({206, 186, 250, 255}, 0.45f);
    Audio::Play(Sfx::Chime, 0.8f, 0.7f);
    return true;
}

int World::FitLevel(int fit) const {
    return std::max(1, player.skills.CombatLevel() + fit);
}

int World::Banish(const string& type) {
    int gone = 0;
    for (auto& e : enemies) {
        if (e->Dead() || !e->Def() || (!type.empty() && e->Def()->id != type)) continue;
        Smoke(e->x, e->y, 20.0f * e->Def()->scale + 8.0f, false);
        e->LieDead();
        ++gone;
    }
    return gone;
}

void World::SettlePlayer() {
    const SDL_FPoint at = OpenGroundNear(player.x, player.y, player.foot_box);
    player.x = at.x;
    player.y = at.y;
}

bool World::SlainToday(const string& map, int post) const {
    const auto it = slain.find(map + ":" + std::to_string(post));
    return it != slain.end() && it->second >= clock.QuestDay();
}

void World::SpawnEntitiesFromMap(const GameContext& ctx) {
    int post = 0;
    for (const auto& written : map.Enemies()) {
        EnemySpawnDef def = ResolveSpawn(written, map_id, clock.QuestDay(), post++);
        // A monster already killed this session stays dead until its timer
        // brings it back; flags cover the permanent ones.
        const EnemyDef* stats = ctx.enemies ? ctx.enemies->Get(def.type) : nullptr;
        if (!stats) {
            SDL_Log("World: unknown enemy type '%s'", def.type.c_str());
            continue;
        }
        // Fitted to the player: as strong as they are and `fit` more.
        if (def.fitted) def.shown = FitLevel(def.fit);
        // Scaled to how strong it should look, whatever the day put here, with
        // the day's spread on top of that.
        if (def.shown > 0) def.level = Enemy::PostLevel(*stats, def) + (def.level - written.level);
        auto e = std::make_unique<Enemy>();
        e->Init(stats, def, ctx);
        e->post = post - 1;
        // A roamer is somewhere different on its loop each day, and some days
        // not out at all.
        const RoamDay roam = e->Roams() ? RoamDraw(map_id, clock.QuestDay(), e->post, def.chance,
                                                   static_cast<int>(e->route.size()))
                                        : RoamDay{};
        if (e->Roams()) e->StartRoute(roam.start);
        if (!roam.out) e->LieDead();
        // Killed already today: it keeps its place in the list, which friends
        // count monsters by, and is not there.
        if (stats->is_boss && SlainToday(map_id, e->post)) e->LieDead();
        // A night visitor by day, or on a night that is not one of its own.
        if (e->night && !Abroad(*e)) e->LieDead();
        // A ritual's: nobody's until a witch table calls it, and not back
        // by itself once it is down. See World::Ritual.
        if (!def.ritual.empty()) e->LieDead();
        // Kept only while the story says: not there, and not back -- unless
        // it is to come when the story does (appear).
        e->squad = def.squad;
        e->appear_after = def.appear_after;
        e->appear_when = def.when;
        if (!Holds(def.when)) {
            e->LieDead();
            e->held_back = def.appear;
        }
        // Stood on its pedestal until it is told, or struck.
        if (!def.dormant.empty() && !Flagged(def.dormant)) {
            e->dormant = true;
            e->wake_flag = def.dormant;
            e->perch = def.perch;
        }
        enemies.push_back(std::move(e));
    }

    for (const auto& def : map.Npcs()) {
        auto n = std::make_unique<Npc>();
        n->Init(def, ctx);
        npcs.push_back(std::move(n));
    }
}

bool World::PortalHeld(const Portal& p) const {
    if (portals_armed) return false;
    // How far the nearest edge of it is from where the player came in.
    const float dx = std::max({p.rect.x - arrival.x, 0.0f, arrival.x - (p.rect.x + p.rect.w)});
    const float dy = std::max({p.rect.y - arrival.y, 0.0f, arrival.y - (p.rect.y + p.rect.h)});
    return Length(dx, dy) <= ARRIVAL_GUARD;
}

// Whether the straight line from a to b passes through r (Liang-Barsky).
static bool LineCrosses(SDL_FPoint a, SDL_FPoint b, const SDL_FRect& r) {
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {a.x - r.x, r.x + r.w - a.x, a.y - r.y, r.y + r.h - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return false;
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) t0 = std::max(t0, t);
        else             t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

bool World::PastWayBack() const {
    if (portals_armed) return false;
    const SDL_FRect feet = player.Bounds();
    const SDL_FPoint at = player.GroundCentre();
    for (const Portal& p : map.Portals())
        if (!p.requires_interact && PortalHeld(p) && !RectsOverlap(feet, p.rect) && LineCrosses(arrival, at, p.rect))
            return true;
    return false;
}

bool World::RequestTransition(const string& id, const string& spawn) {
    if (transition_pending) return false;
    // A friend's window goes nowhere by itself: the host, stepping them,
    // finds them in the doorway and takes them through (coop::Host::Doors),
    // and a waystone's way is asked of the host (AskToTravel).
    if (visiting) return false;
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
    // A friend's ritual goes on with the map, ring and all.
    if (ritual.active) {
        to.ritual = ritual;
        to.map.SetRing(ritual.x, ritual.y, ritual.radius);
    }
    ritual = Ritual{};
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
    acting_log = &s.private_log;
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
    acting_log = nullptr;
    quest_log = host_quests;
    SwapSeat(who, s);
}

std::set<string> World::SeenFlags() const {
    if (!acting_flags) return flags;
    std::set<string> seen;
    for (const string& key : flags) if (!PrivateFlag(key)) seen.insert(key);
    seen.insert(acting_flags->begin(), acting_flags->end());
    return seen;
}

void World::SwapSeat(Player& who, SeatState& s) {
    std::swap(player, who);
    if (s.viewed) std::swap(camera, s.camera);
    std::swap(targeting, s.targeting);
    std::swap(gather_index, s.gather_index);
    std::swap(gather_timer, s.gather_timer);
    std::swap(gather_needed, s.gather_needed);
    std::swap(angler, s.angler);
    std::swap(angler_fish, s.angler_fish);
    std::swap(angler_x, s.angler_x);
    std::swap(angler_y, s.angler_y);
    std::swap(reel_click, s.reel_click);
    std::swap(hazard_timer, s.hazard_timer);
    std::swap(ice_strain, s.ice_strain);
    std::swap(ice_grace, s.ice_grace);
    std::swap(ice_sink, s.ice_sink);
    std::swap(ice_safe, s.ice_safe);
    std::swap(ice_mark, s.ice_mark);
    std::swap(ice_fell, s.ice_fell);
    std::swap(ice_safe_known, s.ice_safe_known);
    std::swap(ice_was_up, s.ice_was_up);
    std::swap(ice_warned, s.ice_warned);
    std::swap(gate_note_timer, s.gate_note_timer);
    std::swap(lifesteal_bank, s.lifesteal_bank);
    std::swap(portals_armed, s.portals_armed);
    std::swap(arrival_released, s.arrival_released);
    std::swap(arrival, s.arrival);
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
    std::swap(dream.story, s.dream_story);
    std::swap(dream.locked, s.dream_locked);
    std::swap(dream.talisman, s.dream_talisman);
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

bool World::Sleep(SleepChoice how, const GameContext& ctx, int fee) {
    (void)ctx;
    if (visiting) {
        // The bed is the host's, and so is the till: say which way and what
        // it costs, and wait to be told.
        visitor_acts.push_back({5, std::to_string(std::max(0, fee)), "", how == SleepChoice::Reverie ? 1 : 0});
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
    if (fee > 0) {
        if (player.inventory.Coins() < fee) {
            AddText("A bed here is " + std::to_string(fee) + " coins.", player.x, player.y - 54.0f,
                    {235, 150, 120, 255}, 1.8f);
            Audio::Play(Sfx::UiError);
            return false;
        }
        player.inventory.SpendCoins(fee);
        WorldRequest paid;
        paid.type = WorldRequest::Type::Toast;
        paid.text = "Paid " + std::to_string(fee) + " coins for the bed.";
        requests.push_back(paid);
    }

    player.Rest();
    targeting.Clear();
    fade_speed = SLEEP_FADE_SPEED;

    if (how == SleepChoice::Reverie) {
        dream.active = true;
        dream.map = map_id;
        dream.x = player.x;
        dream.y = player.y;
        dream.story = dream.locked = dream.talisman = false;
        RequestTransition(DREAM_MAP, "arrival");
        fade_speed = SLEEP_FADE_SPEED;
        fade_caption = "You drift off to sleep...";
        flip = true;
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
    // A story's dream left early -- by the stone, or thrown out of it -- takes
    // its music with it: the scene that would have ended it never comes.
    if (dream.story && !visiting && !acting) Audio::Music("", 1.0f);
    fade_caption = why == WakeReason::Nightmare ? "The nightmare throws you awake."
                 : why == WakeReason::Stone     ? "You wake."
                                                : "Dawn breaks.";
    waking = why;
    flip = true;
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
            // The talisman lands them where they stood, in the other world --
            // on open ground there, should the dream have built on the spot.
            if (talisman_landing) SettlePlayer();
            camera.SnapTo(player.x, player.y);
            HoldWayBack(player.x, player.y);
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
            HoldWayBack(player.x, player.y);
        }
    }
    next_has_point = false;
    talisman_landing = false;
    if (!map.Loaded() || map_id != next_map) {
        SDL_Log("World: failed to enter map '%s'", next_map.c_str());
        WorldRequest r;
        r.type = WorldRequest::Type::Toast;
        r.text = "That path could not be opened. Your current area is unchanged.";
        requests.push_back(r);
        // Do not retry a broken exit every frame while standing on it.
        HoldWayBack(player.x, player.y);
    }
    transition_pending = false;
    fade_dir = -1;
}

// -----------------------------------------------------------------------------
//  Frame update
// -----------------------------------------------------------------------------

bool World::ObjectPresent(const MapObject& o) const {
    // A curio lies where it was left until whoever is looking has picked it
    // up: while its quest has not been begun and it is not in their bag. So a
    // friend finds their own, and a thing picked up and handed over does not
    // turn up again where it was found.
    if (o.type == "curio") {
        if (!o.loot_item.empty() && player.inventory.Has(o.loot_item, 1)) return false;
        if (!o.starts_quest.empty() && quest_log && !quest_log->relay &&
            quest_log->Status(o.starts_quest) != QuestStatus::NotStarted) return false;
        if (!o.starts_quest.empty() && quest_log && quest_log->relay &&
            quest_log->relay_active.count(o.starts_quest)) return false;
        return true;
    }
    // The Cinder King's and the Quintessence's chests stood beside them and
    // opened in the middle of the fight; the Heart is "taken from where the
    // Quintessence fell". Now each is there once its boss is down today.
    if (!o.needs_slain.empty() && !Used(o)) {
        bool down = false;
        int post = 0;
        for (const auto& e : map.Enemies()) {
            if (e.type == o.needs_slain && SlainToday(map_id, post)) { down = true; break; }
            ++post;
        }
        if (!down) return false;
    }
    if (!Holds(o.when)) return false;
    if (o.needs_quest.empty()) return true;
    return quest_log && quest_log->IsActive(o.needs_quest);
}

void World::Update(float dt, const GameContext& ctx) {
    quest_log = ctx.quests;
    host_quests = ctx.quests;
    statuses_now = ctx.statuses;
    if ((catch_up_timer -= dt) <= 0.0f) {
        catch_up_timer = 0.5f;
        CatchUpUsedObjects(ctx);
    }
    // --- a shadow passing over ------------------------------------------------
    if (shadow.on && (shadow.t += dt) >= shadow.time) shadow.on = false;
    // A temper, as a puff of steam off somebody now and then (NpcState::steam).
    for (auto& n : npcs) {
        const float every = n->SteamEvery();
        if (every <= 0.0f) { n->steam_timer = 0.0f; continue; }
        if ((n->steam_timer += dt) >= every) {
            n->steam_timer = 0.0f;
            Steam(n->x, n->y - 40.0f, 9.0f);
        }
    }
    if (beam.on && (beam.t += dt) >= beam.time) beam.on = false;
    for (TellLine& t : tell_lines) t.t += dt;
    tell_lines.erase(std::remove_if(tell_lines.begin(), tell_lines.end(),
                                    [](const TellLine& t) { return t.t >= t.time; }),
                     tell_lines.end());
    talisman_rest = std::max(0.0f, talisman_rest - dt);

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
            flip = false;
            tear = false;
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
    TellTheHour(ctx);
    SettleStory();
    shut_note_timer = std::max(0.0f, shut_note_timer - dt);
    // The seat at this machine, and then the place. A friend's seat is done
    // by StepGuest, to their own clock, acting as them.
    if (!player.absent) UpdateSeat(dt, ctx);
    UpdateShared(dt, ctx);
    AgeIce(dt);
    UpdateStrikes(dt);
    if (!visiting && !player.absent) CollectPickups(dt, ctx);
    FlushKills(ctx);

    if (cam_hold.on) {
        if (cam_hold.snap) camera.SnapTo(cam_hold.x, cam_hold.y);
        else               camera.Follow(cam_hold.x, cam_hold.y, dt);
        ambience.Update(dt, camera);
        Audio::SetListener(cam_hold.x, cam_hold.y);
    } else if (!player.absent) {
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

    // The scripted fight out of the Ashen Path: battered, never felled -- on
    // the Ashen Path, and nowhere a waystone could take them from it.
    player.unfelled = Flagged("ACT2_ESCAPE_ACTIVE") && map_id == "ashen_path";
    // The Magister's first lesson: any staff holds the old magic.
    player.ancient_lesson = Flagged("ACT2_ANCIENT_SLOT_UNLOCKED");

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
        // With the abilities' shift held and an ability carried there, lock
        // on is that ability's button and the target stays who it was. The
        // shift, not the guard: on a pad the shift is RB, and RB with the
        // trigger moved the lock before the third ability went, while B with
        // it -- the guard, on a pad nothing more -- did nothing at all.
        const bool third = player.hands.Down(PlayerInput::Ability) &&
                           player.talents.Ability(SkillTrees::ABILITY_SLOTS - 1) != nullptr;
        const bool cycle = !player.input_locked && !third && player.hands.Pressed(PlayerInput::Target);
        switch (targeting.Update(player, enemies, map, cycle)) {
            case Targeting::Change::Locked:
            case Targeting::Change::Switched: Audio::Play(Sfx::UiMove, 0.8f, 0.8f); break;
            case Targeting::Change::Released: Audio::Play(Sfx::UiBack, 0.6f); break;
            default: break;
        }
    }

    // A way back that is waiting can be stood on, and stepped off again the
    // way you came, but not walked out through. Havenbrook's gate stood in
    // open field, and leaving the town by its south road put you on the road
    // north of it, walking south: with the key still down you walked into the
    // gate, which was waiting, and out of the far side of it into the field,
    // through a town gate without going into the town.
    const bool was_past = PastWayBack();
    const float was_x = player.x, was_y = player.y;
    player.Update(dt, *this, ctx);
    if (!was_past && PastWayBack()) {
        player.x = was_x;
        player.y = was_y;
    }

    // The fog on the minimap lifts round whoever is looked through: the seat
    // at this machine, Player Two, a friend at their own window. A friend's
    // copy at the host is never looked through, and keeps no map of its own.
    if (!player.absent && (!acting || seat_states[player.seat].viewed))
        player.exploration.Reveal(map_id, map.Width(), map.Height(), player.x, player.y, dt);

    player.input_locked = locked_by_game;
    // Thin ice is the player's own, whoever's window this is: see IceStrain.
    UpdateThinIce(dt, ctx);
    if (ice_sink >= 0.0f) player.input_locked = true;

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
        } else if (clock.DreamOver() && !dream.story && !dream.talisman) {
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
                // the Drowned King's boots, and half out of anyone warded
                // against burning -- the two do not add up to nothing.
                const bool fire_warded = h->kind == "fire" && player.Warded(Status::Burn);
                const float share = (player.Passive(Player::PASSIVE_MARSHSTRIDE) || fire_warded)
                                        ? std::min(0.5f, Player::FIRE_WARD_GROUND) : 1.0f;
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
        // But not the way back, for a walk that began on the previous map.
        // Arrival spawns sit a pace or two from it -- forty pixels on the
        // field outside Havenbrook -- so holding a direction through the fade
        // used to carry the player straight into the return portal and bounce
        // them back where they came from, over and over for as long as the key
        // was down. So it waits until the key has been let go -- and until the
        // player is standing clear of it, because letting go a step too late
        // leaves you on top of the way back, and arming it there bounced you
        // just the same.
        //
        // Only the way back, though. This used to hold every portal on the
        // map, and a player who never let go -- rolling from one key onto the
        // next, or steering the stick round without letting it centre --
        // walked straight over every gate they came to, for as long as they
        // kept moving. And not for ever even then: walked well away from where
        // they came in, they have left the way back behind them, and a player
        // who comes back to it means to go through.
        if (!portals_armed) {
            if (Length(player.hands.move.x, player.hands.move.y) < 0.01f)
                arrival_released = true;
            const bool left = Length(player.x - arrival.x, player.y - arrival.y) > ARRIVAL_LEFT;
            bool on_it = false;
            for (const Portal& p : map.Portals())
                if (!p.requires_interact && PortalHeld(p) && RectsOverlap(player.Bounds(), p.rect)) on_it = true;
            if ((arrival_released || left) && !on_it) portals_armed = true;
        }

        if (!transition_pending) {
            const Portal* p = nullptr;
            for (const Portal& q : map.Portals())
                if (!q.requires_interact && q.locked_by.empty() && RectsOverlap(player.Bounds(), q.rect) &&
                    !PortalHeld(q)) { p = &q; break; }
            const string* shut = p ? p->ShutBy([&](const string& f) { return Flagged(f); }) : nullptr;
            if (shut) {
                if (shut_note_timer <= 0.0f) {
                    AddText(shut->empty() ? string("The way is shut.") : *shut, player.x, player.y - 52.0f,
                            {235, 200, 160, 255}, 2.4f);
                    Audio::Play(Sfx::Locked);
                    shut_note_timer = 2.8f;
                }
            } else if (p) {
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
        UpdateFalling(dt);
        UpdateClaws(dt);
        UpdateArcs(dt);
        UpdateNodes(dt, ctx);
        ShedFromShots();
        ShedFromGround(dt);
        ShedFromRing(dt);
        ShedFromStatuses(dt);
        UpdateChimneys(dt);
        UpdateMotes(dt);
        UpdateScreenFx(dt);
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

    // A witch table's ritual, after the monsters it called have had their turn.
    UpdateRitual(dt, ctx);
    UpdateSquads(dt);

    UpdateProjectiles(dt, ctx);
    UpdateGroundEffects(dt, ctx);
    ForgetSpentCasts();
    UpdateImpacts(dt);
    UpdateDust(dt);
    ShedFromShots();
    ShedFromGround(dt);
    ShedFromRing(dt);
    ShedFromStatuses(dt);
    UpdateChimneys(dt);
    UpdateMotes(dt);
    UpdateScreenFx(dt);
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

// --- thin ice -------------------------------------------------------------------------------
bool World::SeenHere() { return !acting || seat_states[player.seat].viewed; }

// The cracks and holes are the map's, whoever made them: they fade once a frame.
void World::AgeIce(float dt) {
    for (IceCrack& c : ice_cracks) c.age += dt;
    ice_cracks.erase(std::remove_if(ice_cracks.begin(), ice_cracks.end(),
                                    [](const IceCrack& c) { return c.age > ICE_CRACK_LIFE; }),
                     ice_cracks.end());
    for (IceHole& h : ice_holes) h.age += dt;
    ice_holes.erase(std::remove_if(ice_holes.begin(), ice_holes.end(),
                                   [](const IceHole& h) { return h.age > ICE_HOLE_LIFE; }),
                    ice_holes.end());
}

// The footing of whoever is `player` just now: the host's own, or -- acting as
// them -- a friend's, with their seat's strain swapped in (SwapSeat).
void World::UpdateThinIce(float dt, const GameContext& ctx) {
    player.under_ice = false;
    if (map.ThinIces().empty()) return;
    if (ice_grace > 0.0f) ice_grace -= dt;
    if (player.IsDead() || transition_pending) { ice_sink = -1.0f; return; }
    // A friend's window foresees; the host decides and says so. Its words and
    // sounds reach the friend from the host, so the window keeps its own quiet.
    const bool decides = !visiting;
    // Whose screen this is: the host's own, a seat looked through here, or --
    // acting for a friend down the wire -- nobody's here.
    const bool seen_here = SeenHere();

    // Going under: a moment in the black water, then out on the shore.
    if (ice_sink >= 0.0f) {
        ice_sink += dt;
        player.x = ice_fell.x;
        player.y = ice_fell.y;
        player.under_ice = ice_sink > 0.15f;
        if (ice_sink >= ICE_SINK_TIME) {
            ice_sink = -1.0f;
            player.under_ice = false;
            SDL_FPoint to = ice_safe;
            if (!ice_safe_known || map.ThinIceAt(to.x, to.y)) to = map.DefaultSpawn();
            player.x = to.x;
            player.y = to.y;
            if (seen_here) camera.SnapTo(player.x, player.y);
            ice_grace = ICE_GRACE;
            ice_strain = 0.0f;
            ice_warned = 0;
            if (decides) {
                Audio::PlayAt(Sfx::Splash, player.x, player.y);
                AddText("You drag yourself out onto the shore", player.x, player.y - 60.0f, {206, 232, 255, 255}, 2.4f);
            }
        }
        return;
    }

    const bool up = player.IsJumping();
    const ThinIce* ice = map.ThinIceAt(player.x, player.y);
    const bool moving = Length(player.hands.move.x, player.hands.move.y) > 0.1f;
    if (!ice) {
        if (!up) {
            ice_safe = {player.x, player.y};
            ice_safe_known = true;
        }
        ice_strain = std::max(0.0f, ice_strain - ICE_SETTLE_LAND * dt);
        if (ice_strain < 0.2f) ice_warned = 0;
        ice_mark = {player.x, player.y};
        ice_was_up = up;
        return;
    }

    const float before = ice_strain;
    if (ice_grace <= 0.0f) {
        if (ice_was_up && !up) ice_strain += ICE_LANDING;
        if (up) {
            // In the air it bears nothing.
        } else if (moving && player.Sprinting()) {
            ice_strain += dt / ICE_SPRINT_TIME;
        } else if (moving) {
            ice_strain += dt * ice->weak;
            if (ice->weak <= 0.0f) ice_strain -= ICE_SETTLE_WALK * dt;
        } else {
            ice_strain -= ICE_SETTLE_REST * dt;
        }
    } else {
        ice_strain -= ICE_SETTLE_REST * dt;
    }
    ice_strain = std::clamp(ice_strain, 0.0f, 1.0f);
    ice_was_up = up;

    // The crack follows whoever is making it, and forks as it goes on.
    const float step = Length(player.x - ice_mark.x, player.y - ice_mark.y);
    if (ice_strain > before && ice_strain > 0.06f && step > 12.0f) {
        const float nx = -(player.y - ice_mark.y) / step, ny = (player.x - ice_mark.x) / step;
        const float wob = (static_cast<float>(afflict_dice() % 100) / 100.0f - 0.5f) * 8.0f;
        const SDL_FPoint to = {player.x + nx * wob, player.y + ny * wob};
        ice_cracks.push_back({ice_mark, to, 0.0f});
        if (ice_strain > 0.4f && afflict_dice() % 3 == 0) {
            const float side = (afflict_dice() % 2) ? 1.0f : -1.0f;
            const float len = 10.0f + 16.0f * ice_strain;
            ice_cracks.push_back({to, {to.x + nx * side * len + (to.x - ice_mark.x) * 0.3f,
                                       to.y + ny * side * len + (to.y - ice_mark.y) * 0.3f}, 0.0f});
        }
        ice_mark = to;
    } else if (ice_strain <= before) {
        ice_mark = {player.x, player.y};
    }

    if (ice_strain > 0.35f && ice_warned < 1) {
        ice_warned = 1;
        if (decides) {
            AddText("The ice cracks!", player.x, player.y - 52.0f, {214, 236, 255, 255}, 1.4f);
            Audio::PlayAt(Sfx::Block, player.x, player.y, 0.7f, 0.6f);
        }
    } else if (ice_strain > 0.7f && ice_warned < 2) {
        ice_warned = 2;
        if (decides) {
            AddText("It won't hold!", player.x, player.y - 52.0f, {255, 200, 170, 255}, 1.4f);
            Audio::PlayAt(Sfx::Block, player.x, player.y, 0.9f, 0.45f);
        }
        Shock(player.x, player.y, 0.15f, seen_here ? 0.25f : 0.0f);
    }
    if (ice_strain >= 1.0f) BreakIce(ctx);
}

void World::BreakIce(const GameContext& ctx) {
    ice_sink = 0.0f;
    ice_fell = {player.x, player.y};
    ice_holes.push_back({ice_fell, 0.0f});
    for (int k = 0; k < 6; ++k) {
        const float a = k * 1.0472f + 0.3f;
        ice_cracks.push_back({ice_fell, {ice_fell.x + cosf(a) * 34.0f, ice_fell.y + sinf(a) * 24.0f}, 0.0f});
    }
    const bool seen_here = SeenHere();
    Burst(player.x, player.y - 6.0f, 26.0f, {196, 226, 255, 255}, 26);
    Shock(player.x, player.y, 0.35f, seen_here ? 0.5f : 0.0f);
    if (seen_here) Flash({180, 214, 255, 255}, 0.35f);
    ice_strain = 0.0f;
    // A friend's own window stops here: the rest is the host's to deal, to
    // them as to anyone, and it tells them.
    if (visiting) return;

    Audio::PlayAt(Sfx::Splash, player.x, player.y, 1.0f, 0.8f);
    AddText("The ice gives way!", player.x, player.y - 52.0f, {160, 210, 255, 255}, 1.6f);
    // The water: a third of them, never all of it, and they come out soaked
    // and chilled -- a chill on the soaked is a frost, and it holds them a
    // moment on the shore.
    const int dmg = std::min(std::max(1, static_cast<int>(std::lround(player.max_hp * ICE_FALL_SHARE))),
                             std::max(0, player.hp - 1));
    if (dmg > 0) {
        player.Damage(dmg);
        player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
        AddText(std::to_string(dmg), player.x, player.y - 40.0f, {150, 200, 255, 255});
    }
    if (ctx.statuses) {
        player.Afflict(Status::Wet, 0, *ctx.statuses, player.x, player.y);
        player.Afflict(Status::Chill, 0, *ctx.statuses, player.x, player.y);
    }
}
