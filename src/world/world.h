#pragma once
#include "../headers.h"
#include "map.h"
#include "../camera.h"
#include "../entity/entity.h"
#include "../entity/player.h"
#include "../entity/enemy.h"
#include "../entity/npc.h"
#include "../systems/projectile.h"

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
    Player  player;
    vector<std::unique_ptr<Enemy>> enemies;
    vector<std::unique_ptr<Npc>>   npcs;
    vector<Pickup>      pickups;
    vector<FloatingText> texts;
    vector<Projectile>   projectiles;
    vector<GroundEffect> ground_effects;
    vector<Impact>       impacts;

private:
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
    // Aim at the cursor on mouse and keyboard, and along the facing otherwise.
    Vec2 PlayerAim(const GameContext& ctx) const;
    // Applies a hit from a projectile or a ground effect to one enemy.
    void HitEnemy(Enemy& e, const CombatProfile& owner, AttackStyle style,
                  Element element, float damage_mult, float knockback,
                  float from_x, float from_y, const GameContext& ctx);
    void UpdateTexts(float dt);
    void UpdateGathering(float dt, const GameContext& ctx);
    void CookOne(const struct MapObject& range, const GameContext& ctx);
    void ApplyTransition(const GameContext& ctx);
    void RenderObjects(SDL_Renderer* r, TextureCache& cache,
                       vector<pair<float, std::function<void()>>>& queue) const;

    string map_id;
    bool   transition_pending = false;
    string next_map, next_spawn;
    float  fade = 0.0f;
    int    fade_dir = 0;          // -1 fading in, +1 fading out, 0 idle

    std::set<string> flags;
    vector<WorldRequest> requests;

    int   gather_index = -1;      // index into map objects
    float gather_timer = 0.0f;
    float gather_needed = 0.0f;
};
