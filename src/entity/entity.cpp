#include "entity.h"

SDL_FRect Entity::Bounds() const {
    return {x + foot_box.x, y + foot_box.y, foot_box.w, foot_box.h};
}

SDL_FRect Entity::BodyBox() const {
    return {x + body_box.x, y + body_box.y, body_box.w, body_box.h};
}

void Entity::Damage(int amount) {
    if (amount <= 0) return;
    hp = std::max(0, hp - amount);
    hurt_flash = 0.18f;
}

void Entity::Heal(int amount) {
    if (amount <= 0) return;
    hp = std::min(max_hp, hp + amount);
}

void Entity::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    // Flash red on the frames right after taking a hit.
    SDL_Color tint{255, 255, 255, 255};
    if (hurt_flash > 0.0f) tint = {255, 110, 110, 255};
    sprite.Draw(r, cache, cam, x, y - draw_lift, tint);
}
