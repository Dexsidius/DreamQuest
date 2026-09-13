#include "targeting.h"
#include "map.h"
#include "../entity/enemy.h"
#include "../entity/player.h"

SDL_FPoint Targeting::AimPoint(const Enemy& e) {
    const SDL_FRect b = e.BodyBox();
    return {b.x + b.w / 2.0f, b.y + b.h / 2.0f};
}

bool Targeting::Targetable(const Enemy& e) {
    return !e.Dead() && e.CurrentState() != Enemy::State::Dead;
}

bool Targeting::InFight(const Enemy& e) {
    if (!Targetable(e)) return false;
    // Coming for the player, or already hurt by them. A monster that has given
    // up and is walking home is out of it, even with its health bar still up.
    return e.Engaged() ||
           (e.HealthBarVisible() && e.CurrentState() != Enemy::State::Return);
}

SDL_FPoint Targeting::Muzzle(const Player& p) {
    return {p.x, p.y - 16.0f};
}

bool Targeting::ClearLine(const Map& map, float x0, float y0, float x1, float y1) {
    // Sampled a few pixels apart with a box the size of an arrow, the same test
    // a projectile in flight makes, so a target offered is one a shot could
    // actually reach.
    constexpr float STEP = 6.0f, R = 2.0f;
    const float len = Length(x1 - x0, y1 - y0);
    const int steps = static_cast<int>(len / STEP);
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const float px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
        if (map.Blocked({px - R, py - R, R * 2.0f, R * 2.0f})) return false;
    }
    return true;
}

Targeting::Change Targeting::Update(const Player& player,
                                    const vector<std::unique_ptr<Enemy>>& enemies,
                                    const Map& map, bool cycle) {
    const auto in_list = [&](const Enemy* e) {
        if (!e) return false;
        for (const auto& u : enemies) if (u.get() == e) return true;
        return false;
    };
    if (!in_list(combat)) combat = nullptr;
    if (!in_list(locked)) locked = nullptr;

    Change change = Change::None;
    if (player.IsDead()) {
        if (locked) change = Change::Released;
        Clear();
        return change;
    }

    const SDL_FPoint from = Muzzle(player);
    const auto distance = [&](const Enemy& e) {
        const SDL_FPoint a = AimPoint(e);
        return Length(a.x - from.x, a.y - from.y);
    };

    // --- the lock lets go ------------------------------------------------------
    if (locked && (!Targetable(*locked) || distance(*locked) > LOCK_BREAK)) {
        locked = nullptr;
        change = Change::Released;
    }

    // --- combat target ---------------------------------------------------------
    // Nearest wins, weighted toward the way the player faces: one straight
    // ahead counts as it is, one directly behind as if it were 60% further
    // away. The current pick gets a little credit, so two monsters at nearly
    // the same range do not make the marker flicker between them.
    const float fx = player.facing == FACE_LEFT ? -1.0f : player.facing == FACE_RIGHT ? 1.0f : 0.0f;
    const float fy = player.facing == FACE_UP   ? -1.0f : player.facing == FACE_DOWN  ? 1.0f : 0.0f;
    Enemy* best = nullptr;
    float best_score = 1e9f;
    for (const auto& u : enemies) {
        Enemy& e = *u;
        if (!InFight(e)) continue;
        const SDL_FPoint a = AimPoint(e);
        const float dx = a.x - from.x, dy = a.y - from.y;
        const float d = Length(dx, dy);
        if (d > COMBAT_RANGE) continue;
        const float dot = d > 0.01f ? (dx * fx + dy * fy) / d : 1.0f;
        float score = d * (1.3f - 0.3f * dot);
        if (&e == combat) score *= 0.85f;
        if (score >= best_score) continue;
        if (!ClearLine(map, from.x, from.y, a.x, a.y)) continue;
        best = &e;
        best_score = score;
    }
    combat = best;

    // --- stepping the lock -----------------------------------------------------
    if (cycle) {
        vector<pair<float, Enemy*>> reach;
        for (const auto& u : enemies) {
            if (!Targetable(*u)) continue;
            const float d = distance(*u);
            if (d <= LOCK_RANGE) reach.emplace_back(d, u.get());
        }
        std::sort(reach.begin(), reach.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        if (!locked) {
            // The first press takes whatever the fight is already with, so the
            // button does what the marker suggests; failing that, the nearest.
            locked = combat ? combat : (reach.empty() ? nullptr : reach.front().second);
            change = locked ? Change::Locked : Change::None;
        } else {
            size_t i = 0;
            while (i < reach.size() && reach[i].second != locked) ++i;
            if (i + 1 < reach.size())      { locked = reach[i + 1].second;  change = Change::Switched; }
            else if (i == reach.size() && !reach.empty())
                                           { locked = reach.front().second; change = Change::Switched; }
            else                           { locked = nullptr;              change = Change::Released; }
        }
    }
    return change;
}
