#pragma once
#include "../headers.h"
#include "../sprite.h"
#include "../texturecache.h"
#include "../camera.h"

class World;
class Input;
class SpriteLibrary;
class ItemDatabase;
class LootSystem;
class QuestLog;
class DialogueDatabase;
class EnemyDatabase;
class ProjectileDatabase;
class SpellBook;
class SkillTrees;
class StatusDatabase;

// Everything shared that entities need to reach. Owned by Game, borrowed here,
// so no entity has to know how the game is assembled.
struct GameContext {
    SDL_Renderer*     renderer  = nullptr;
    TextureCache*     textures  = nullptr;
    SpriteLibrary*    sprites   = nullptr;
    ItemDatabase*     items     = nullptr;
    LootSystem*       loot      = nullptr;
    QuestLog*         quests    = nullptr;
    DialogueDatabase* dialogue  = nullptr;
    EnemyDatabase*    enemies   = nullptr;
    ProjectileDatabase* projectiles = nullptr;
    SpellBook*        spells    = nullptr;
    SkillTrees*       trees     = nullptr;
    StatusDatabase*   statuses  = nullptr;
    Input*            input     = nullptr;
    std::mt19937*     rng       = nullptr;
};

// Base for anything that lives in the world and sorts against the decor layer.
class Entity {
public:
    // A virtual destructor would quietly take the moves away with it, and
    // World::ActAs swaps whole players: say that they are wanted.
    Entity() = default;
    Entity(const Entity&) = default;
    Entity(Entity&&) = default;
    Entity& operator=(const Entity&) = default;
    Entity& operator=(Entity&&) = default;
    virtual ~Entity() = default;

    virtual void Update(float dt, World& world, const GameContext& ctx) = 0;
    virtual void Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const;

    // Collision footprint, world space. Deliberately small and at the feet so
    // characters can overlap scenery above the waist.
    virtual SDL_FRect Bounds() const;
    // Larger box used for weapon hits and interaction range.
    virtual SDL_FRect BodyBox() const;

    float SortY() const { return y; }
    // Where it stands, for everything that is decided on the ground -- a swing,
    // a slam, burning earth: the middle of its feet, and half its width.
    SDL_FPoint GroundCentre() const {
        return {x + foot_box.x + foot_box.w * 0.5f, y + foot_box.y + foot_box.h * 0.5f};
    }
    float GroundRadius() const { return std::max(foot_box.w, body_box.w) * 0.5f; }

    // How far the terrain under this entity lifts it on screen, in pixels.
    // Set once a frame by the world, because an entity has no idea what map it
    // is standing on. Drawing uses it; sorting deliberately does not -- who is
    // in front of whom is decided by where their feet are on the ground, not
    // by how high that ground happens to be.
    float draw_lift = 0.0f;
    bool  Dead() const { return hp <= 0; }

    void  Damage(int amount);
    void  Heal(int amount);

    float x = 0, y = 0;                 // feet position
    float vx = 0, vy = 0;
    Facing facing = FACE_DOWN;
    Sprite sprite;

    int hp = 1, max_hp = 1;
    bool remove = false;                // world drops it next frame

    float hurt_flash = 0.0f;            // seconds of red tint remaining
    float knock_x = 0, knock_y = 0;     // decaying knockback velocity

    SDL_FRect foot_box{-8.0f, -11.0f, 16.0f, 11.0f};   // relative to feet
    SDL_FRect body_box{-12.0f, -40.0f, 24.0f, 40.0f};  // relative to feet
};

// A dropped item lying on the ground, from a kill or a chest.
struct Pickup {
    string item_id;
    int    qty = 1;
    float  x = 0, y = 0;
    float  bob = 0.0f;        // animation phase
    float  life = 0.0f;       // seconds since it landed
    bool   collected = false;
    // Put down by the player from the bag. It is not picked straight back up:
    // it waits until they have stepped clear of it once, and it does not lie
    // there for ever.
    bool   dropped = false;
    bool   cleared = false;
    // Who put it down, so it is they who must step clear of it: a friend
    // standing by can pick it straight up, which is how things change hands.
    uint8_t dropper_seat = 0;
    // A name that survives the vector shifting, for the wire.
    uint32_t net_id = 0;
    string icon;              // resolved image path, may be empty

    SDL_FRect Bounds() const { return {x - 8.0f, y - 8.0f, 16.0f, 16.0f}; }
};

// Damage numbers, XP drops and pickup notices that float up and fade.
struct FloatingText {
    string    text;
    float     x = 0, y = 0;
    float     life = 0.0f, max_life = 1.0f;
    SDL_Color color{255, 255, 255, 255};
    float     rise = 26.0f;
};
