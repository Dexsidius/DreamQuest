#pragma once
#include "../headers.h"
#include "map.h"
#include "../camera.h"
#include "../entity/entity.h"
#include "../entity/player.h"
#include "../entity/enemy.h"
#include "../entity/npc.h"
#include "../systems/projectile.h"
#include "ambience.h"
#include "targeting.h"
#include "lighting.h"
#include "../systems/clock.h"
#include "../systems/shop.h"

// Things the world needs the UI layer to put on screen. The world never opens
// a panel itself; it raises a request and Game decides what state to enter.
struct WorldRequest {
    enum class Type { Dialogue, Board, Note, Shop, Toast, Craft, Storage, Enchant, Sleep } type = Type::Toast;
    string id;            // npc id / object id / shop id
    string title;
    string text;          // note body, toast message, dialogue root node
    vector<string> list;  // quest ids on a board
    int    count = 0;     // how many slots a storage chest holds
};

// Everything the world keeps about one player that is not in the Player: who
// they are fighting, the log they are chopping, the door they are halfway
// through, the dream they will wake from. Alone there is one set of these and
// it is the world's own members. With friends there is one per friend, and
// World::ActAs swaps a friend's into place -- with their Player -- so that
// every line written for "the player" works on them unchanged.
struct SeatState {
    Targeting targeting;
    int    gather_index = -1;
    float  gather_timer = 0.0f, gather_needed = 0.0f;
    float  hazard_timer = 0.0f, gate_note_timer = 0.0f, lifesteal_bank = 0.0f;
    bool   portals_armed = true, arrival_released = true;
    // A way through a door, asked for and not yet taken. For a friend the
    // host takes them through it: see coop::Host.
    bool   transition_pending = false;
    string next_map, next_spawn;
    bool   next_has_point = false;
    float  next_x = 0.0f, next_y = 0.0f;
    int    waking = 0;
    float  fade = 0.0f, fade_speed = 3.2f;
    int    fade_dir = 0;
    string fade_caption;
    bool   dream_active = false;
    string dream_map;
    float  dream_x = 0.0f, dream_y = 0.0f;
    // Panels asked for while acting as them: theirs to open, not the host's.
    vector<WorldRequest> requests;
    // What their machine has told the host that is theirs alone: the recipes
    // and spells they know. The world's own flags are everybody's.
    std::set<string> private_flags;
    // Their journal, listening only; see QuestLog::relay.
    QuestLog journal;
    // Or, for someone sitting at this machine, their real one: Player Two's
    // journal is here, not across a wire.
    QuestLog* own_journal = nullptr;
    // A seat that is looked through at this machine has a camera of its own,
    // which follows them whichever world they are in.
    Camera camera{640.0f, 720.0f};
    bool   viewed = false;
    SeatState() { journal.relay = true; }
};

class World {
public:
    // Maps live in maps/<id>.mx. Passing an empty spawn uses the map default.
    bool LoadMap(const string& map_id, const string& spawn, const GameContext& ctx);
    // Queued from a portal; applied at the top of the next frame. False if
    // it was not: one is already under way, or this world is a guest's window
    // and the host leads the way.
    bool RequestTransition(const string& map_id, const string& spawn);
    bool TransitionPending() const { return transition_pending; }
    // Screen-wipe progress, 0 = clear, 1 = fully black.
    float FadeAmount() const { return fade; }

    void Update(float dt, const GameContext& ctx);
    void Render(SDL_Renderer* r, TextureCache& cache) const;

    // Called by Game when the player presses Interact.
    void TryInteract(const GameContext& ctx);

    // Fires a projectile from a point along a direction. The direction does not
    // need normalising.
    void SpawnProjectile(const string& def_id, float x, float y,
                         float dir_x, float dir_y,
                         const CombatProfile& owner, AttackStyle style,
                         float damage_mult, bool from_player,
                         const GameContext& ctx);
    void AddGroundEffect(const GroundEffect& effect);

    // A blow that has already beaten the player's defence, from an attacker
    // standing at (from_x, from_y). The shield gets its say first; whatever
    // gets past it is taken, shown and trains Defence the way a hit always
    // has. Every monster swing and every shot comes through here, so blocking
    // cannot be forgotten by one of them. Returns the damage actually taken.
    int HitPlayer(int damage, const CombatProfile& attacker, float from_x, float from_y,
                  float knock_x = 0.0f, float knock_y = 0.0f);
    // A leader's heavy attack landing. No shield stops it, and one raised
    // against it makes it worse: the guard shatters, the bar empties and the
    // blow lands harder. Returns the damage taken.
    static constexpr float HEAVY_BLOCK_PUNISH = 1.5f;
    int HeavyHitPlayer(int damage, float from_x, float from_y, float knock_x, float knock_y);

    void SpawnLoot(const string& table_id, float x, float y, const GameContext& ctx);
    // Puts an item on the ground. `by_player` is one dropped from the bag,
    // which waits for them to step away before it can be picked up again and
    // is gone after DROP_LIFE seconds -- leaving the map loses it sooner.
    static constexpr float DROP_LIFE = 180.0f;
    void DropItem(const string& item_id, int qty, float x, float y, const GameContext& ctx,
                  bool by_player = false);
    void AddText(const string& text, float x, float y, SDL_Color color, float life = 0.9f);
    // How far the ground lifts what is drawn at a point: a shot, a drop, a
    // number over someone's head. See UpdateElevation.
    float LiftAt(float x, float y) const { return map.HasElevation() ? map.HeightAt(x, y) : 0.0f; }

    vector<WorldRequest> TakeRequests();

    // Object flags record one-shot world state: a chest already looted, a note
    // already read. They ride along in the save so the world stays consistent.
    // False for an object whose quest is not being done right now; such an
    // object is not drawn, not lit and cannot be used.
    bool  ObjectPresent(const MapObject& o) const;
    bool  Flagged(const string& key) const {
        return flags.count(key) > 0 || (acting_flags && acting_flags->count(key) > 0);
    }
    void  SetFlag(const string& key) {
        // What someone acting here learns is theirs, not the host's.
        if (acting_flags_rw && PrivateFlag(key)) { acting_flags_rw->insert(key); return; }
        if (flags.insert(key).second && journal) flag_log.push_back(key);
    }
    const std::set<string>& Flags() const { return flags; }
    void  SetFlags(const std::set<string>& f) { flags = f; }

    // What is in a storage chest, by object id. A chest the player owns is
    // theirs wherever it stands, so this rides in the save beside the flags
    // rather than in the map -- a map is regenerated by the tools and would
    // take the contents with it.
    Inventory& Storage(const string& object_id, int slots, const ItemDatabase* db);
    const std::map<string, Inventory>& Storages() const { return storage; }
    void SetStorages(std::map<string, Inventory>&& s) { storage = std::move(s); }

    // Herbs picked and growing back: "map:object" -> the absolute game hour
    // (day * 24 + hour) it is ready again. Saved.
    bool  Picked(const MapObject& o) const;
    void  Pick(const MapObject& o);
    const std::map<string, double>& PickedHerbs() const { return picked; }
    void  SetPickedHerbs(const std::map<string, double>& p) { picked = p; }
    double GameHours() const { return clock.Day() * 24.0 + clock.Hours(); }
    // Recipes the player has learned to brew live in the flags as "recipe:<id>".
    bool  KnowsRecipe(const string& id) const { return Flagged("recipe:" + id); }
    // Enchantments the same way, as "recipe:enchant:<id>": a scroll's `learn`
    // and a dialogue's are written "enchant:<id>", so both go through the one
    // flag without knowing what they teach.
    bool  KnowsEnchantment(const string& id) const { return Flagged("recipe:enchant:" + id); }
    // And the ancient spells, as "recipe:spell:<id>": the magister's lesson
    // and a tome's `learn` both read "spell:<id>".
    bool  KnowsSpell(const string& id) const { return Flagged("recipe:spell:" + id); }
    // The ancient spells learned, in the order the college teaches them.
    vector<string> KnownArcane(const class SpellBook& book) const;
    // A felled tree or a worked-out seam, until it is back. Kept in `picked`
    // beside the herbs, so it is saved the same way.
    bool  Spent(const MapObject& o) const;

    // Gathering (Woodcutting / Mining) in progress, 0..1 for the HUD bar.
    float GatherProgress() const;
    bool  Gathering() const { return visiting ? shown_gather > 0.0f : gather_index >= 0; }
    // In a guest's window the log is the host's to fell: this is what the
    // host says of it, for the bar and the axe in hand.
    void  ShowGather(float progress, const string& clip, const string& model);

    const Map& CurrentMap() const { return map; }
    const string& MapId() const { return map_id; }

    Map     map;
    Camera  camera{1280.0f, 720.0f};
    Ambience ambience;
    // The seat at this machine: whose camera, targeting, interact prompt and
    // bag the world's are. Alone, the only player there is.
    Player  player;
    // Everyone else who is here. On the host these are friends' characters,
    // stepped by StepGuest with the inputs their machines send; on a client
    // they are puppets, posed from snapshots. Kept across a map change: until
    // the co-op plan's M4 the host leads, and everyone goes through the door
    // together.
    vector<std::unique_ptr<Player>> guests;
    Player* AddGuest(uint8_t seat, const string& name, const string& look, const GameContext& ctx);
    void    RemoveGuest(uint8_t seat);
    Player* Guest(uint8_t seat);
    // One step of a friend's character, by their own hands and their own
    // clock: everything the frame does for the seat at this machine -- the
    // same Player::Update their machine ran to predict it, and then their
    // swing landing, their axe biting, the coin at their feet -- done acting
    // as them.
    void    StepGuest(Player& guest, const PlayerInput& hands, float dt, const GameContext& ctx);
    SeatState& SeatOf(uint8_t seat) { return seat_states[seat]; }
    // Runs `fn` with `who` standing where `player` does, and their seat's
    // state where the world's own is. For `player` itself it just runs it.
    template <class Fn> void ActAs(Player& who, Fn&& fn) {
        if (&who == &player || acting) { fn(); return; }
        SeatState& s = seat_states[who.seat];
        SwapSeat(who, s);
        acting = &who;
        acting_flags = &s.private_flags;
        acting_flags_rw = &s.private_flags;
        fn();
        acting_flags = nullptr;
        acting_flags_rw = nullptr;
        acting = nullptr;
        SwapSeat(who, s);
    }
    bool    Acting() const { return acting != nullptr; }
    // The same, held open across frames: the game serves one seat at a time
    // -- draws their half of the screen, opens their bag -- and while it is
    // serving Player Two they stand where `player` does. Nothing else may
    // step this world meanwhile.
    void    BeginActing(Player& who);
    void    EndActing();
    // Which flags are a player's own -- recipes learned, places seen -- rather
    // than the world's.
    static bool PrivateFlag(const string& key) {
        return key.rfind("recipe:", 0) == 0 || key.rfind("visited:", 0) == 0 || key.rfind("starter_", 0) == 0;
    }
    // Whoever owns a shot or a patch of burning ground.
    Player& OwnerOf(bool local, uint8_t seat);
    // Someone alive whose body a box touches, or null.
    Player* PlayerTouching(const SDL_FRect& box);
    bool    AnyPlayerNear(float x, float y, float range);
    // A kill, to be credited to everyone here when the frame's acting is done.
    void    CreditKill(const QuestEvent& e) { kill_log.push_back(e); }
    void    FlushKills(const GameContext& ctx);
    // Picks up what is lying at `player`'s feet. Once a frame for each seat.
    void    CollectPickups(float dt, const GameContext& ctx);
    // A panel to open, from outside: what the host said to open, on a guest's
    // machine.
    void    PushRequest(const WorldRequest& r) { requests.push_back(r); }

    // While set, what the world shows and marks is also written down, for the
    // host to tell friends: floating text, flags newly set, herbs picked and
    // trees felled. Drained by coop::Host.
    bool    journal = false;
    vector<FloatingText> text_log;
    vector<string>       flag_log;
    vector<pair<string, double>> picked_log;
    // `player` and then every guest.
    vector<Player*> Players();
    // Whoever is nearest a point, never null: alone, that is `player`.
    Player& NearestPlayer(float x, float y);
    // A world a guest is looking through decides nothing: its monsters are
    // puppets posed from what the host says, and what its player does to the
    // place -- E on a chest, a thing dropped, a bed chosen -- is written down
    // here for coop::Guest to send, instead of being done. Set before LoadMap.
    bool    visiting = false;
    struct VisitorAct { int kind = 0; string a, b; int n = 0; };   // kinds are net::Action's
    vector<VisitorAct> visitor_acts;
    // True while anyone else is in the realm, on this map or another. A night
    // slept through then waits for everyone: see Sleep.
    bool    company = false;
    // Called as the map is about to be replaced, and when the new one is in:
    // how coop::Host leaves friends behind on the old map, in a world of
    // their own, and finds the ones already on the new.
    std::function<void(World&)> before_unload, after_load;
    // Gives the place and everything in it -- monsters, loot, shots, friends
    // and their seats -- to another world. What is the player's stays.
    void    HandOver(World& to);
    // A herb picked or a tree felled elsewhere in the realm.
    void    SetPicked(const string& key, double when) { picked[key] = when; }
    // Acts as a friend with their own journal in place of the host's: `fn`
    // is given the context to use. Kills made meanwhile are handed round.
    template <class Fn> void AsSeat(Player& guest, const GameContext& ctx, Fn&& fn) {
        SeatState& seat = seat_states[guest.seat];
        QuestLog* journal = seat.own_journal ? seat.own_journal : &seat.journal;
        ActAs(guest, [&] {
            GameContext theirs = ctx;
            theirs.quests = journal;
            theirs.input = nullptr;
            quest_log = journal;
            fn(theirs);
            quest_log = host_quests;
        });
        FlushKills(ctx);
    }
    // E, pressed on something by whoever `player` is.
    void    InteractWith(int kind, int index, const GameContext& ctx);
    // Who the player is fighting; see targeting.h.
    Targeting targeting;
    // The time of day; see clock.h.
    WorldClock clock;
    // What every trader has sold today; see shop.h.
    ShopLedger shops;

    // --- sleep and dreams --------------------------------------------------------
    // After dusk, a bed, a campsite or the player's own camp asks how the
    // night is to be spent, and the answer is the sleeper's own:
    //
    //   Sleep through the night   the clock goes to dawn and they wake where
    //                             they lay down, rested, with no dream.
    //   Go into the Reverie       sleep is a journey: to the dreamworld, for as
    //                             long as the night lasts. Dawn brings them
    //                             back to exactly where they lay down. So does
    //                             dying in the dream, which costs the rest of
    //                             the night and nothing else, and so does the
    //                             waking stone, for anyone who has had enough.
    //
    // A bed used to do only the second. The world asks by raising a Sleep
    // request; the game shows the two rows and calls Sleep() with the answer.
    static constexpr float SLEEP_FADE_SPEED = 0.9f;
    static constexpr float SLEEP_SAFE_RANGE = 260.0f;   // no sleeping with a monster this close
    static inline const char* DREAM_MAP = "dreamworld";

    // Where to wake up. Saved, so a dream survives a reload.
    struct DreamReturn {
        bool   active = false;
        string map;
        float  x = 0.0f, y = 0.0f;
    };
    // The camp a bedroll pitches: a tent and a fire, on one outdoor map.
    struct Camp {
        bool   pitched = false;
        string map;
        float  x = 0.0f, y = 0.0f;
    };
    // Slept is a night slept through; the other three end a dream.
    enum class WakeReason { None, Dawn, Nightmare, Stone, Slept };
    enum class SleepChoice { Through, Reverie };

    bool InDream() const { return map.Ambient() == "dream"; }
    // The Reverie goes down: three depths, a ladder between each. Everything
    // a dream is -- dawn ends it, nothing in it kills you, the waking stone --
    // is true at every depth, because all of that asks InDream() and not
    // which map. What the depth changes is how hard it is, how dark, and how
    // many shards there are in it: one more from every kill, crystal and
    // chest for each ladder climbed down.
    int  DreamBonus(const string& item_id) const;

    // Which monster keeps a post tonight, and at what level. A post with a
    // pool is kept by one of the pool, chosen by the day, the map and the
    // post's group -- or its place in the file if it has none -- so that:
    //   - it is the same all night, up and down the ladders and across a
    //     reload, since QuestDay does not turn over until dawn;
    //   - it is different the next night;
    //   - posts in a group agree, so a platform holds a pack and not a zoo;
    //   - a guest's machine, which builds its own monsters from the map file
    //     and is only told where they are, comes to the same answer as the
    //     host from the day it was already being sent.
    // Pure, and static, so the self-test can ask it about any night.
    static EnemySpawnDef ResolveSpawn(const EnemySpawnDef& def, const string& map_id, int day, int index);
    const DreamReturn& Dream() const { return dream; }
    void SetDream(const DreamReturn& d) { dream = d; }
    const Camp& PlayerCamp() const { return camp; }
    // Takes effect on the next map load.
    void SetCamp(const Camp& c) { camp = c; }

    // Why nobody could lie down here right now, or empty if they could: the
    // hour, or a monster too near.
    string SleepRefusal() const;
    // A bed has been used. Raises the Sleep request if the night and the
    // neighbourhood allow it; says why not in the world if they do not. True
    // when the question has been asked. `title` names what is being slept on.
    bool AskToSleep(const string& title);
    // Lies down, the way chosen, under the same conditions. True when the
    // player is falling asleep.
    bool Sleep(SleepChoice how, const GameContext& ctx);
    // Pitches the bedroll in this inventory slot as a camp in front of the
    // player. Empty on success, otherwise the reason it could not be done.
    string PitchCamp(int slot, const GameContext& ctx);
    // Leaves the dream now.
    void Wake(WakeReason why);
    // Why the player last woke, once, for the game to react to.
    WakeReason TakeWake() { WakeReason w = woke; woke = WakeReason::None; return w; }

    // A line to show over the screen while it is dark for sleeping or waking.
    const string& FadeCaption() const { return fade_caption; }
    // The colour of the light right now: white by day, blue at night, violet
    // in a dream.
    SDL_Color AmbientLight() const;
    // Every light that should cut through that, this frame.
    vector<Light> CollectLights() const;
    vector<std::unique_ptr<Enemy>> enemies;
    vector<std::unique_ptr<Npc>>   npcs;
    vector<Pickup>      pickups;
    vector<FloatingText> texts;
    vector<Projectile>   projectiles;
    vector<GroundEffect> ground_effects;
    vector<Impact>       impacts;

    // A puff kicked up by a sprinting footfall, drifting back the way the
    // runner came.
    struct Dust { float x, y, vx, vy, life, max_life, size; };
    vector<Dust> dust;
    void AddDust(float x, float y, float dir_x, float dir_y);

private:
    // The frame, in two parts: what is done for one seat, and what is done
    // once for the place.
    void UpdateSeat(float dt, const GameContext& ctx);
    void UpdateShared(float dt, const GameContext& ctx);
    void SwapSeat(Player& who, SeatState& s);
    std::map<uint8_t, SeatState> seat_states;
    Player* acting = nullptr;                       // the guest slot holding `player`'s own data meanwhile
    const std::set<string>* acting_flags = nullptr;
    std::set<string>* acting_flags_rw = nullptr;
    vector<QuestEvent> kill_log;
    class QuestLog* host_quests = nullptr;
    uint32_t next_net_id = 1;
    float shown_gather = 0.0f;
    void UpdateDust(float dt);
    void SpawnEntitiesFromMap(const GameContext& ctx);
    void ApplyPlayerAttack(const GameContext& ctx);
    // What an ability begun this step does to the place: see Player::TryAbility.
    void ApplyPlayerAbility(const GameContext& ctx);
    void ResolveInteractTarget(const GameContext& ctx);
    void UpdatePickups(float dt, const GameContext& ctx);
    void UpdateProjectiles(float dt, const GameContext& ctx);
    void UpdateGroundEffects(float dt, const GameContext& ctx);
    void UpdateImpacts(float dt);
    // Tells every entity how far the terrain under it lifts it on screen.
    void UpdateElevation(float dt);
    // Marks a wall where a projectile struck it, facing back along the normal.
    void AddImpact(const Projectile& p, float nx, float ny);
    void FirePlayerProjectile(const GameContext& ctx);
    // A charged attack chosen from the skill tree, in place of the plain one.
    // True when it handled the attack.
    bool MeleeTechnique(const string& technique, const GameContext& ctx);
    // Rings of sparks and puffs of dust for techniques that have no projectile.
    void Burst(float x, float y, float radius, SDL_Color color, int count);
    // At the target in combat, along the facing out of it.
    Vec2 PlayerAim() const;
    // Strikes everything whose body is within a radius of the player's chest:
    // a whirlwind, a ground slam, a Cross Cut. Returns how many it struck.
    int  HitAround(float radius, float damage_mult, float knockback, const GameContext& ctx);
    // Whether the player's blade can reach it at all: alive, and not up or
    // down more than one level of cliff from where they stand.
    bool Strikeable(const Enemy& e) const;
    // What the swing in flight is called, for the chain counter's trail.
    string SwingLabel(const GameContext& ctx) const;
    // The swing itself, drawn: a crescent swept through the arc a melee blow
    // covers, brightest on its active frames. See the definition.
    void DrawSwing(SDL_Renderer* r) const;
    void DrawArrowRain(SDL_Renderer* r) const;
    // Applies a hit from a projectile or a ground effect to one enemy.
    void HitEnemy(Enemy& e, const CombatProfile& owner, AttackStyle style,
                  Element element, float damage_mult, float knockback,
                  float from_x, float from_y, const GameContext& ctx);
    void UpdateTexts(float dt);
    void UpdateGathering(float dt, const GameContext& ctx);
    void CookOne(const struct MapObject& range, const GameContext& ctx);
    void ApplyTransition(const GameContext& ctx);
    void PlaceCampObjects();
    void RenderStars(SDL_Renderer* r) const;
    void RenderObjects(SDL_Renderer* r, TextureCache& cache,
                       vector<pair<float, std::function<void()>>>& queue) const;

    string map_id;
    bool   transition_pending = false;
    string next_map, next_spawn;
    // A transition that arrives at a point rather than a named spawn: waking
    // up where you went to sleep.
    bool   next_has_point = false;
    float  next_x = 0.0f, next_y = 0.0f;
    float  fade_speed = 3.2f;
    string fade_caption;
    WakeReason waking = WakeReason::None;   // set while a wake transition runs
    WakeReason woke = WakeReason::None;
    DreamReturn dream;
    Camp camp;
    mutable Lighting lighting;
    float  fade = 0.0f;
    int    fade_dir = 0;          // -1 fading in, +1 fading out, 0 idle
    // Step-through portals on a freshly entered map stay inert until movement
    // input has been let go and the player is standing clear of every portal.
    // See World::Update.
    bool   portals_armed = true;
    bool   arrival_released = true;

    std::set<string> flags;
    std::map<string, Inventory> storage;
    vector<WorldRequest> requests;
    // Borrowed each frame from the context, so drawing can ask what the
    // player is in the middle of. Never owned, never outlives a frame's use.
    const class QuestLog* quest_log = nullptr;

    float lifesteal_bank = 0.0f;  // healing on hit, in fractions of a point
    // The next HitEnemy strikes critically whatever the dice say: a shot
    // loosed with Take Aim, set by whatever carries it just before it lands.
    bool  crit_next = false;
    // Whether a blow slips past someone on the move. HitPlayer is handed no
    // dice, and only ever decides anything on the host.
    std::mt19937 evade_dice{0x51199u};
    std::map<string, double> picked;
    int   gather_index = -1;      // index into map objects
    float gather_timer = 0.0f;
    float gather_needed = 0.0f;
    float hazard_timer = 0.0f;     // until the next burn from the ground underfoot
    float gate_note_timer = 0.0f;  // so a closed way says so once, not every frame
};
