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

// Things the world needs the UI layer to put on screen. The world never opens
// a panel itself; it raises a request and Game decides what state to enter.
struct WorldRequest {
    enum class Type { Dialogue, Board, Note, Shop, Toast, Craft } type = Type::Toast;
    string id;            // npc id / object id / shop id
    string title;
    string text;          // note body, toast message, dialogue root node
    vector<string> list;  // quest ids on a board
};

class World {
public:
    // Maps live in maps/<id>.mx. Passing an empty spawn uses the map default.
    bool LoadMap(const string& map_id, const string& spawn, const GameContext& ctx);
    // Queued from a portal; applied at the top of the next frame.
    void RequestTransition(const string& map_id, const string& spawn);
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

    void SpawnLoot(const string& table_id, float x, float y, const GameContext& ctx);
    void DropItem(const string& item_id, int qty, float x, float y, const GameContext& ctx);
    void AddText(const string& text, float x, float y, SDL_Color color, float life = 0.9f);

    vector<WorldRequest> TakeRequests();

    // Object flags record one-shot world state: a chest already looted, a note
    // already read. They ride along in the save so the world stays consistent.
    bool  Flagged(const string& key) const { return flags.count(key) > 0; }
    void  SetFlag(const string& key) { flags.insert(key); }
    const std::set<string>& Flags() const { return flags; }
    void  SetFlags(const std::set<string>& f) { flags = f; }

    // Gathering (Woodcutting / Mining) in progress, 0..1 for the HUD bar.
    float GatherProgress() const;
    bool  Gathering() const { return gather_index >= 0; }

    const Map& CurrentMap() const { return map; }
    const string& MapId() const { return map_id; }

    Map     map;
    Camera  camera{1280.0f, 720.0f};
    Ambience ambience;
    Player  player;
    // Who the player is fighting; see targeting.h.
    Targeting targeting;
    // The time of day; see clock.h.
    WorldClock clock;

    // --- sleep and dreams --------------------------------------------------------
    // After dusk, a bed, a campsite or the player's own camp puts them to
    // sleep, and sleep is a journey: to the dreamworld, for as long as the
    // night lasts. Dawn brings them back to exactly where they lay down. So
    // does dying in the dream, which costs the rest of the night and nothing
    // else, and so does the waking stone, for anyone who has had enough.
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
    enum class WakeReason { None, Dawn, Nightmare, Stone };

    bool InDream() const { return map.Ambient() == "dream"; }
    const DreamReturn& Dream() const { return dream; }
    void SetDream(const DreamReturn& d) { dream = d; }
    const Camp& PlayerCamp() const { return camp; }
    // Takes effect on the next map load.
    void SetCamp(const Camp& c) { camp = c; }

    // Lies down if the night and the neighbourhood allow it; says why not in
    // the world if they do not. True when the player is falling asleep.
    bool TrySleep(const GameContext& ctx);
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
    void UpdateDust(float dt);
    void SpawnEntitiesFromMap(const GameContext& ctx);
    void ApplyPlayerAttack(const GameContext& ctx);
    void ResolveInteractTarget(const GameContext& ctx);
    void UpdatePickups(float dt, const GameContext& ctx);
    void UpdateProjectiles(float dt, const GameContext& ctx);
    void UpdateGroundEffects(float dt, const GameContext& ctx);
    void UpdateImpacts(float dt);
    // Tells every entity how far the terrain under it lifts it on screen.
    void UpdateElevation();
    // Marks a wall where a projectile struck it, facing back along the normal.
    void AddImpact(const Projectile& p, float nx, float ny);
    void FirePlayerProjectile(const GameContext& ctx);
    // At the target in combat, along the facing out of it.
    Vec2 PlayerAim() const;
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
    vector<WorldRequest> requests;

    int   gather_index = -1;      // index into map objects
    float gather_timer = 0.0f;
    float gather_needed = 0.0f;
};
