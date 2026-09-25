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
    // Totem is the last, and FromPanel in coop.cpp says so: what goes down the
    // wire is a number, and one past the end is held to the end.
    enum class Type { Dialogue, Board, Note, Shop, Toast, Craft, Storage, Enchant, Sleep, Travel, Totem } type = Type::Toast;
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
    // Their footing on thin ice: see World::IceStrain.
    float  ice_strain = 0.0f, ice_grace = 0.0f, ice_sink = -1.0f;
    SDL_FPoint ice_safe{}, ice_mark{}, ice_fell{};
    bool   ice_safe_known = false, ice_was_up = false;
    int    ice_warned = 0;
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

    // --- the screen's own effects: see world_screen.cpp ----------------------------
    // A shockwave from a point on the ground: a ring pushed out through the
    // picture, `strength` 0..1, and the screen shaken by `shake` 0..1. Whatever
    // lands hard calls it: a meteor, a dropped slab, a bolt from the sky, a
    // leader's heavy blow.
    void Shock(float x, float y, float strength, float shake);
    // The whole view flashed toward a colour, by `amount` at most.
    void Flash(SDL_Color colour, float amount);
    // Where a shake puts the camera this frame, in whole world pixels; nothing
    // when the player has turned shaking off.
    SDL_FPoint ShakeOffset() const;
    // How dark it is for what glows: 0 by day, 1 in the dead of night, in a
    // dream and underground in the dark.
    float GlowDarkness() const;

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
    // `leaves` is what the blow can leave on them if it gets through -- a
    // spider's poison, a hag's charm -- and a charm draws them to (charm_x,
    // charm_y), the attacker's own place unless a shot says where it came from.
    // `by` is the monster that swung, when one did: a parry leaves it reeling.
    int HitPlayer(int damage, const CombatProfile& attacker, float from_x, float from_y,
                  float knock_x = 0.0f, float knock_y = 0.0f, const StatusProc& leaves = {},
                  float charm_x = -1.0f, float charm_y = -1.0f, class Enemy* by = nullptr);
    // A leader's heavy attack landing. No shield stops it, and one raised
    // against it makes it worse: the guard shatters, the bar empties and the
    // blow lands harder. Returns the damage taken.
    static constexpr float HEAVY_BLOCK_PUNISH = 1.5f;
    int HeavyHitPlayer(int damage, float from_x, float from_y, float knock_x, float knock_y,
                       const StatusProc& leaves = {}, class Enemy* by = nullptr);
    // Rolls a status against the player a blow of `blow` just landed on, and
    // says so over their head if it takes. Its own dice, never the context's:
    // a fight that leaves nothing throws exactly the numbers it always did.
    // Nothing is rolled without the statuses loaded.
    void AfflictPlayer(const StatusProc& proc, int blow, float charm_x, float charm_y);
    // Those dice start from the same place in every world, which is what keeps
    // the self-test's fights the same fight each run. The game seeds its own
    // from the machine, and hands each world it makes a draw from the last,
    // or every session would open with the same run of luck.
    void SeedDice(uint32_t seed) { afflict_dice.seed(seed); }
    uint32_t DrawSeed() { return static_cast<uint32_t>(afflict_dice()); }

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

    // --- a spell is paid for when it lands --------------------------------------------
    // Casting paid its Magic experience as the bolt left the staff, "whether or
    // not the bolt finds anything" -- and mana comes back by itself, so a wall
    // in the middle of Havenbrook was the best teacher in the game: no risk, no
    // cost, and every spell's experience for as long as anyone cared to stand
    // there. A cast is *owed* its experience now, and is paid the first time
    // anything it threw -- a bolt, one of a fan of them, the fire a bolt left
    // on the ground, a meteor -- takes something off a monster. Once a cast,
    // however many things it hits; never for a miss, which would only have
    // made a monster that cannot be hit into the same wall; and a cast with
    // nothing of it left in the air is forgotten.
    size_t OwedCasts() const { return owed_casts.size(); }

    // --- a boss is killed once a day ----------------------------------------------
    // Every map load used to stand every monster back up, the Pit Lord and the
    // dragons with the rest: out of the door and in again, and a hundred and
    // forty thousand coins of demonite was on its feet waiting. A boss that has
    // been killed stays killed until the next dawn, on its map and across
    // loads, and is back the day after. Ordinary monsters are as they were.
    void NoteSlain(int post);
    bool SlainToday(const string& map, int post) const;
    const std::map<string, int>& Slain() const { return slain; }
    void SetSlain(const std::map<string, int>& s) { slain = s; }
    void  SetFlags(const std::set<string>& f) { flags = f; }

    // What is in a storage chest, by object id. A chest the player owns is
    // theirs wherever it stands, so this rides in the save beside the flags
    // rather than in the map -- a map is regenerated by the tools and would
    // take the contents with it.
    Inventory& Storage(const string& object_id, int slots, const ItemDatabase* db);
    // Everything a save holds that is the world's and not the character's, put
    // back to what a world that has never been played is: what a new game
    // starts from. It is one function so that there is one list. A new game
    // used to clear these a line at a time, and the two newest things a save
    // had learned to keep -- what is in the storage chest, and which bosses
    // have been killed today -- were not on it: someone who played one save and
    // then started another found the first character's chest in the new
    // character's house, and wrote it into the new save.
    void StartAfresh();
    // A bolt thrown for practice, from one point at another, by somebody in the
    // college's practice hall: it touches nobody. See Projectile::show.
    void ThrowPracticeBolt(const string& bolt, float x, float y, float tx, float ty, const GameContext& ctx);
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

    // --- the marks a combo leaves: see world_strikes.cpp ---------------------------------------
    // Drawn by the fx shader's strike shapes, for show only, and only with the
    // visual effects on.
    struct Strike {
        Shaders::Shape shape = Shaders::SHAPE_IMPACT;
        float x = 0, y = 0;              // on the ground, world px
        float radius = 20;               // half the square it is drawn in
        float lift = 0;                  // drawn this far above the ground point
        float p[4] = {0, 0, 0, 0};       // its four numbers (see fx.frag)
        int   grows = -1;                // which of them runs over its life, -1 none
        float grows_from = 0, grows_to = 1;
        float age = 0, life = 0.3f, delay = 0;
        SDL_FColor colour{1, 1, 1, 1};
        float seed = 0;
    };
    const vector<Strike>& Strikes() const { return strikes; }
    // What the strikes drew, written down while `journal` is set for the host
    // to tell friends, whose windows never swing and so never draw them: a
    // mark, a shock through the picture (with whose blow it was, since only
    // their screen shakes), or a burst of sparks. Drained by coop::Host, and
    // drawn at the other end by ReplayStrike.
    struct StrikeNote {
        enum Kind : uint8_t { MARK = 0, SHOCK = 1, BURST = 2 } kind = MARK;
        Strike mark;                                    // MARK
        float x = 0, y = 0, size = 0, amount = 0;       // SHOCK: strength, shake. BURST: radius, turn
        uint8_t seat = 0;                               // SHOCK
        SDL_Color colour{255, 255, 255, 255};           // BURST
        int count = 0;                                  // BURST
    };
    vector<StrikeNote> strike_log;
    // One drawn in a friend's window, whose own seat is `my_seat`.
    void ReplayStrike(const StrikeNote& note, uint8_t my_seat);
    // The combos' marks: as a swing lands, on each thing it lands on, as a
    // shot is loosed and where it strikes; and a parry's, and a riposte's.
    void ComboSwingFx(ComboMove move, const ItemDef* weapon);
    void ComboHitFx(ComboMove move, const ItemDef* weapon, const Enemy& e);
    void ComboShotFx(ComboMove move, AttackStyle style, Element element, float x, float y, float angle);
    void ComboShotHitFx(ComboMove move, AttackStyle style, Element element, float x, float y, float angle);
    void ParryFx(float x, float y, float angle);
    void RiposteFx(float x, float y, float angle);

    // --- thin ice ------------------------------------------------------------------------
    // A frozen lake bears a walker. Sprint on it and it cracks behind you,
    // the crack running as long as the sprint does; let it run too long and
    // it gives way: through into the black water, out again on the last dry
    // ground stood on, soaked, chilled and a third of your health the poorer.
    // Stop, or walk, and it settles. A jump come down on it strains it too.
    // Every player's own: a friend's is kept in their seat (SeatState) and
    // swapped in with them, so the host's own strain and theirs never mix.
    // As with lava, the host decides: the fall, the water's bite and the chill
    // are dealt to a friend exactly as to the host, by the host. A friend's
    // own window only foresees it -- the crack, the going under, the shore --
    // so that it feels at once; what it cost them it is told.
    float IceStrain() const { return ice_strain; }
    bool  ThroughTheIce() const { return ice_sink >= 0.0f; }
    size_t IceCracks() const { return ice_cracks.size(); }
    static constexpr float ICE_SPRINT_TIME = 2.4f;    // of sprinting unbroken, and it goes
    static constexpr float ICE_LANDING     = 0.22f;   // a jump come down on it
    static constexpr float ICE_SETTLE_WALK = 0.18f;   // a second, walking on sound ice
    static constexpr float ICE_SETTLE_REST = 0.5f;    // a second, standing still
    static constexpr float ICE_SETTLE_LAND = 1.2f;    // a second, off it
    static constexpr float ICE_SINK_TIME   = 0.9f;    // going under, before the shore
    static constexpr float ICE_GRACE       = 2.5f;    // after, before it can crack again
    static constexpr float ICE_FALL_SHARE  = 0.35f;   // of their health, what the cold water takes
    static constexpr float ICE_CRACK_LIFE  = 45.0f, ICE_HOLE_LIFE = 30.0f;
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

    // --- what comes out at night ----------------------------------------------------
    // Night changed the light and nothing else. Now the wilds have visitors
    // after dark: things that live somewhere worse, a few of them, off the
    // roads -- wolves down on the meadow, the barrow's dead out past its
    // fence, something from the bottom of the well in the Mire. A post marked
    // `night` is kept from nightfall (20:00) to dawn (05:00), on the nights the
    // day's hash says (a pack comes or stays away together), and what is killed
    // stays killed until the next night, door or no door. At dawn whatever is
    // left goes to ground -- once it is done with any fight it is in.
    //
    // It is always in the list, up or not, because friends count monsters by
    // their place in it; by day it lies the way a boss killed today does.
    static bool KeptTonight(const string& map_id, int day, int index, const string& group, float chance);
    // A roaming post's day: whether it is out at all (`chance` of days), and
    // which point of its loop of `points` the day finds it on. A pure hash of
    // map, day and post, so a friend's machine agrees without being told.
    struct RoamDay { bool out = true; int start = 0; };
    static RoamDay RoamDraw(const string& map_id, int day, int index, float chance, int points);
    bool Abroad(const Enemy& e) const;
    // Whether this map has any such posts at all: what the nightfall note says.
    bool HasNightPosts() const;

    // A boss brought down, for whoever `player` is: the first time, a point for
    // their tree and a boon; the fifteenth, its totem, into the bag or at their
    // feet. See Talents::SlayBoss. Says so, to them.
    void AwardBoss(const string& boss_id, const GameContext& ctx);
    // The day has turned, or someone has just been given one: everybody's
    // talents are told what day it is, and their pools are what they now are --
    // a totem's blessing is over at dawn, and some of them are health.
    void TellTheDay();
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
    bool AskToSleep(const string& title, int fee = 0);
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

    // What the spells in the air are shedding, and what they threw up where
    // they landed: see Mote. `BurstOf` is a bolt of that element ending at a
    // point, thrown back along (nx, ny) -- the face of a wall, or the way it
    // came -- or all round for nothing; `size` is one for an apprentice's bolt.
    vector<Mote> motes;
    void BurstOf(Element e, float x, float y, float lift, float size, float nx, float ny);

    // Shots that are owed: a Mineral Burst is eight stones one after another,
    // and the seven after the first are let go from wherever the caster has
    // got to by then.
    struct QueuedShot { float in = 0; string projectile; float mult = 1; float spread = 0; uint32_t cast = 0; float life = 1; };
    vector<QueuedShot> queued_shots;
    // A square of the ground torn up and thrown about: the Slabstrike. Swung
    // through an arc in front of the caster -- a small one on a light, a bigger
    // and slower one on a heavy -- or, held and let go, carried over whoever is
    // being fought and dropped on them, where it breaks into chunks.
    struct SlabSwing {
        float x = 0, y = 0;            // the caster it swings about, or where a dropped one lands
        float facing = 0, from = 0, to = 0;
        float radius = 58.0f;          // how far out it is swung
        float side = 5.0f;             // how big the square is, in world pixels
        bool  drop = false;
        float life = 0, max_life = 0.34f, lift = 0;
        float told = 0.0f, dust = 0.0f;
        float Progress() const { return 1.0f - std::clamp(life / std::max(0.01f, max_life), 0.0f, 1.0f); }
        // Swung: it is thrown out in front in the first fifth, comes round, and
        // is held a moment at the far end before it drops.
        float AngleAt(float p) const {
            float t = std::clamp((p - 0.12f) / 0.74f, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t);
            return from + (to - from) * t;
        }
        float OutAt(float p) const { return radius * (0.42f + 0.58f * std::clamp(p / 0.18f, 0.0f, 1.0f)); }
        float Angle() const { return AngleAt(Progress()); }
        float Out() const { return OutAt(Progress()); }
        // Dropped: 0 while it is still falling, 1 from the moment it lands.
        float Fallen() const { return std::clamp(Progress() / SLAB_DROP_FALL, 0.0f, 1.0f); }
        // And how far through breaking up it is, once it is down.
        float Broken() const {
            return std::clamp((Progress() - SLAB_DROP_FALL) / (1.0f - SLAB_DROP_FALL), 0.0f, 1.0f);
        }
    };
    vector<SlabSwing> slabs;

    // Something coming down out of the sky onto a point: the Meteor. It is as
    // wide as the ground it will cover, so what you see falling is the size of
    // what is about to be hit -- there is nothing else on the screen saying how
    // big a meteor is.
    struct Falling {
        float x = 0, y = 0;            // where it lands
        float size = 32.0f;            // across, in world pixels
        Element element = Element::Fire;
        float life = 0, max_life = 0.6f, lift = 0, told = 0.0f;
        // 0 when it is let go, 1 as it strikes. It falls faster the further it
        // has fallen, which is what tells the eye it is heavy.
        float Progress() const { return 1.0f - std::clamp(life / std::max(0.01f, max_life), 0.0f, 1.0f); }
        float Above() const { const float p = Progress(); return 1.0f - p * p; }
        // How far it has to fall, and how far back along its path it starts.
        // Not much more than its own width again: it is as wide as the ground
        // it covers, so three times that put it off the top of the screen for
        // most of the fall and the first thing seen of it was the shadow.
        float Drop() const { return size * 1.5f; }
        float Lead() const { return size * 0.55f; }
    };
    vector<Falling> falls;

    // A claw conjured at the caster's hand, thrust out and raked across whatever
    // is in front: the Vampiric Touch's, and the Ice Touch's talons. It reaches
    // as far as the bolt these spells used to throw, which is a hand's reach and
    // a little more.
    struct ClawSwipe {
        float x = 0, y = 0;            // the hand it comes out of
        float facing = 0, reach = 88.0f;
        uint8_t look = 0;              // 0 flesh and blood, 1 ice
        float life = 0, max_life = 0.42f, lift = 0, told = 0.0f;
        float Progress() const { return 1.0f - std::clamp(life / std::max(0.01f, max_life), 0.0f, 1.0f); }
        // Out in the first third, raking across the middle third, drawn back in
        // the last: `Out` is how far, `Rake` is where round, -1 to 1.
        float Out() const {
            const float p = Progress();
            if (p < 0.30f) return 0.25f + 0.75f * (p / 0.30f);
            if (p < 0.70f) return 1.0f;
            return 1.0f - 0.55f * ((p - 0.70f) / 0.30f);
        }
        float Rake() const {
            const float p = std::clamp((Progress() - 0.28f) / 0.42f, 0.0f, 1.0f);
            return -1.0f + 2.0f * (p * p * (3.0f - 2.0f * p));
        }
    };
    vector<ClawSwipe> claws;
    static constexpr float CLAW_TIME = 0.42f, CLAW_SWEEP = 0.62f;
    void AddClaw(float x, float y, float facing, float reach, uint8_t look, float lift);
    void HearOfClaw(float x, float y, float facing, float reach, uint8_t look);
    void UpdateClaws(float dt);

    void AddFalling(float x, float y, float size, Element element, float seconds, float lift);
    void HearOfFalling(float x, float y, float size, Element element);
    void UpdateFalling(float dt);
    // One swing or one drop. The caster's world makes it; a guest's makes the
    // same one from the numbers a snapshot gives it, once, however many
    // snapshots go on saying so -- `told` is how long ago one last did.
    static constexpr float SLAB_SWEEP = 1.15f, SLAB_TIME = 0.34f;
    // A dropped one lives longer: most of that is the fall, and the rest is the
    // chunks of it sliding off whatever it landed on.
    static constexpr float SLAB_DROP_TIME = 0.86f, SLAB_DROP_FALL = 0.52f;
    // How big a square each is, in world pixels. The heavy is the light and
    // half again: a character stands about forty pixels tall, so a light is
    // half their height and a heavy is most of it.
    static constexpr float SLAB_LIGHT = 20.0f, SLAB_HEAVY = 32.0f;
    void AddSlabSwing(float x, float y, float facing, float radius, float side, float lift);
    void AddSlabDrop(float x, float y, float side, float lift);
    void HearOfSlab(float x, float y, float facing, float radius, float side, bool drop);

    // How big the Mana Shield's dome is over somebody, in world pixels: half
    // its width, and its height above their feet. Taken from their own body so
    // that whoever is under it is under the whole of it -- it was a pair of
    // numbers, 31 tall against a body of 42, which put the dome at the
    // shoulders and left the head out in the weather. Here rather than buried
    // in the drawing so the self-test can hold it to covering them.
    static SDL_FPoint ShieldDome(const Player& who);

    // --- lightning ------------------------------------------------------------------
    // An arc: a jagged line between two points that is drawn for a moment and
    // is gone. Every lightning spell is made of these -- the Zap is one, the
    // Electrocute three, a Discharge one to everything it reaches, the
    // Electro-Node one on every tick, the Call of Thunder one from out of the
    // sky -- so the only thing that ever has to cross the wire is "an arc, from
    // here, that way, this far".
    //
    // It is drawn and nothing else: the damage was resolved where it was made.
    struct Arc {
        float x = 0, y = 0;              // where it starts
        float facing = 0, reach = 60.0f; // and which way, and how far
        uint8_t look = 0;                // 0 an arc between two things, 1 out of the sky
        float life = 0, max_life = 0.22f, told = 0.0f;
        // The seed keeps one arc's jags its own, and keeps them still while it
        // is drawn: a bolt redrawn from new random numbers every frame is a
        // flicker, not a bolt.
        uint32_t seed = 1;
        float Progress() const { return 1.0f - std::clamp(life / std::max(0.01f, max_life), 0.0f, 1.0f); }
    };
    vector<Arc> arcs;
    static constexpr float ARC_TIME = 0.22f;
    void AddArc(float x, float y, float to_x, float to_y, uint8_t look);
    void HearOfArc(float x, float y, float facing, float reach, uint8_t look);
    void UpdateArcs(float dt);

    // The Electro-Node: a translucent orb left standing where it was thrown,
    // that arcs to whatever is near it every so often until it runs down. It
    // is the one lightning spell that keeps working after the cast.
    struct Node {
        float x = 0, y = 0, lift = 0;
        float life = 0, max_life = 5.0f;
        float tick = 0.0f;               // until the next chain
        float hit_mult = 1.0f;
        int   chains = 2;                // how many it reaches on each tick
        bool  from_player = true;
        bool  mine = true;               // this machine resolves it
        CombatProfile owner;
        StatusProc status;
        uint32_t told_id = 0;
        float told = 0.0f;
        float Progress() const { return 1.0f - std::clamp(life / std::max(0.01f, max_life), 0.0f, 1.0f); }
    };
    vector<Node> nodes;
    static constexpr float NODE_EVERY = 0.55f, NODE_REACH = 132.0f;
    void AddNode(const Node& n);
    void HearOfNode(float x, float y, float life, float max_life);
    void UpdateNodes(float dt, const GameContext& ctx);

    // Rolls a status against a monster a blow of `blow` has just landed on, and
    // says so over its head if it takes: see systems/status.h. Nothing is
    // rolled, and nothing left, without the statuses loaded.
    void TryAfflict(Enemy& e, const StatusProc& proc, int blow, const GameContext& ctx);

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

    // The screen's own effects, and what draws them: see world_screen.cpp.
    struct ShockRing { float x = 0, y = 0, age = 0, strength = 0; };
    vector<ShockRing> shocks;
    float shake = 0.0f, shake_clock = 0.0f;
    SDL_Color flash_colour{255, 255, 255, 255};
    float flash_amount = 0.0f;
    // Rings spreading on the water: a lurker waiting, a swimmer's wake.
    vector<Shaders::Ripple> ripples;
    float ripple_clock = 0.0f;
    int   ripple_tick = 0;
    std::unordered_map<const Enemy*, SDL_FPoint> swim_seen;
    static constexpr float SHOCK_TIME = 0.65f, RIPPLE_TIME = 1.6f;
    void UpdateScreenFx(float dt);
    Shaders::Frame ScreenFrame(TextureCache& cache) const;
    void DrawReflections(SDL_Renderer* r, TextureCache& cache, const vector<const TileInstance*>& decor) const;
    void DrawFloorLight(SDL_Renderer* r) const;
    vector<Strike> strikes;
    uint32_t strike_seed = 0;
    void AddStrike(const Strike& s);
    // Shock and Burst, for the strikes: written down for friends as well.
    void StrikeShock(float x, float y, float strength, float shake);
    void StrikeBurst(float x, float y, float radius, SDL_Color colour, int count, float turn = 0.0f);
    void UpdateStrikes(float dt);
    void DrawStrikes(SDL_Renderer* r) const;
    bool DrawSwingShaded(SDL_Renderer* r, float cx, float cy, float base, float half, float reach, float sweep,
                         float alpha, bool thrust) const;
    // Whether whoever is `player` just now is looked at on this machine: the
    // host's own, or a seat drawn here -- not a friend down the wire the host
    // is acting for. The screen shakes and flashes only for them.
    bool SeenHere();
    // A parry landing on the player: the stagger, the opening, the riposte owed.
    void Parried(class Enemy* by, float from_x, float from_y, bool heavy);
    // The ice: see IceStrain.
    float ice_strain = 0.0f, ice_grace = 0.0f, ice_sink = -1.0f;
    SDL_FPoint ice_safe{}, ice_mark{}, ice_fell{};
    bool  ice_safe_known = false, ice_was_up = false;
    int   ice_warned = 0;
    struct IceCrack { SDL_FPoint a, b; float age = 0.0f; };
    vector<IceCrack> ice_cracks;
    struct IceHole { SDL_FPoint at; float age = 0.0f; };
    vector<IceHole> ice_holes;
    void UpdateThinIce(float dt, const GameContext& ctx);
    void AgeIce(float dt);
    void BreakIce(const GameContext& ctx);
    void DrawIce(SDL_Renderer* r) const;
    void DrawGlows(SDL_Renderer* r, TextureCache& cache, const vector<const TileInstance*>& decor) const;
    // Every shot in the air, by its number: where it was a frame ago, and how
    // far it has gone since it last shed anything. Kept here and not on the
    // shot, because a friend's machine is handed its shots anew with every
    // word from the host and would forget.
    struct ShotSeen { float x = 0, y = 0, lift = 0, owed = 0, size = 1; Element shed = Element::None; bool here = false; };
    std::map<uint32_t, ShotSeen> shots_seen;
    void UpdateMotes(float dt);
    void ShedFromShots();
    void ShedFromGround(float dt);
    void DrawMotes(SDL_Renderer* r) const;
    void SpawnEntitiesFromMap(const GameContext& ctx);
    void ApplyPlayerAttack(const GameContext& ctx);
    // What an ability begun this step does to the place: see Player::TryAbility.
    void ApplyPlayerAbility(const GameContext& ctx);
    void ResolveInteractTarget(const GameContext& ctx);
    void UpdatePickups(float dt, const GameContext& ctx);
    void UpdateProjectiles(float dt, const GameContext& ctx);
    void UpdateGroundEffects(float dt, const GameContext& ctx);
    void UpdateSlabs(float dt);
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
    // `turn` is where round the ring the first of them goes: something that
    // bursts again and again wants a different one each time, or its marks are
    // four fixed points on the ground.
    void Burst(float x, float y, float radius, SDL_Color color, int count, float turn = 0.0f);
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
    // `swing` is what kind of blow it was, which for melee decides what it
    // trains: a light swing feeds Attack, a heavy one Strength, a charged one
    // both. A bow and a staff train their own skill whatever the button, so
    // everything that is not melee leaves it at the default.
    void HitEnemy(Enemy& e, const CombatProfile& owner, AttackStyle style,
                  Element element, float damage_mult, float knockback,
                  float from_x, float from_y, const GameContext& ctx,
                  AttackType swing = AttackType::Light);
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
    std::map<string, int> slain;      // "map:post" -> the quest day it died on
    int told_day = -999999;           // the quest day the talents were last told
    size_t told_players = 0;          // and how many of them there were to tell
    std::map<string, Inventory> storage;
    vector<WorldRequest> requests;
    // Borrowed each frame from the context, so drawing can ask what the
    // player is in the middle of. Never owned, never outlives a frame's use.
    const class QuestLog* quest_log = nullptr;

    float lifesteal_bank = 0.0f;  // healing on hit, in fractions of a point
    // The next HitEnemy strikes critically whatever the dice say: a shot
    // loosed with Take Aim, set by whatever carries it just before it lands.
    bool  crit_next = false;
    struct OwedCast { uint32_t id = 0; int skill = 0; int xp = 0; };
    vector<OwedCast> owed_casts;
    uint32_t next_cast_id = 1;
    uint32_t casting = 0;         // the cast being let go of: what is spawned now carries it
    uint32_t cast_next = 0;       // the cast the next HitEnemy came of, set as crit_next is
    // What the next HitEnemy can leave on what it strikes, and what of it
    // comes back as health: a projectile's, or the ground's. Set as crit_next
    // is. A swing sets nothing and is asked its weapon.
    StatusProc proc_next;
    float leech_next = 0.0f;
    // And the share of its Defence the next HitEnemy goes past: a bolt's. A
    // swing is asked its weapon, and its combo.
    float pierce_next = 0.0f;
    void  ShedFromStatuses(float dt);
    uint32_t OpenCast(int skill, int xp);
    void  PayCast(uint32_t id);
    void  ForgetSpentCasts();
    // What a monster's blow leaves on the player: rolled on these, and read
    // from the statuses the frame's context last carried.
    std::mt19937 afflict_dice{0xA11C7u};
    const StatusDatabase* statuses_now = nullptr;
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
