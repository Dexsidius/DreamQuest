// =============================================================================
//  The Game class's screens, continued: what is on the glass while the game is played
//
//  One class in several files, cut along the banners screens.cpp always had:
//  screens.cpp keeps the menus; the rest is screen_hud, screen_inventory,
//  screen_skills, screen_journal, screen_talk and screen_trade. What two of
//  them share is in screens_shared.h.
// =============================================================================
#include "../systems/gathering.h"
#include "../game.h"
#include "screens_shared.h"

// =============================================================================
//  HUD
// =============================================================================

void Game::DrawWorldText() {
    if (!settings.damage_numbers) return;

    for (const FloatingText& t : world->texts) {
        const float progress = 1.0f - (t.life / t.max_life);
        const SDL_FPoint p = UiPoint(t.x, t.y - t.rise * progress);
        SDL_Color c = t.color;
        // Fade out over the last third of the life.
        c.a = static_cast<Uint8>(255 * std::clamp((t.life / t.max_life) * 3.0f, 0.0f, 1.0f));
        ui.TextShadowed(t.text, p.x, p.y, TextSize::Small, c, Align::Center);
    }
}

// The minimap bezel in assets/ui is 144 across and its glass 124, and the
// right-hand column of the HUD -- toasts, then the quest tracker -- starts
// below the whole dial and the map name under it.
static constexpr float kHudMargin    = 16.0f;
static constexpr float kMinimapRing  = 144.0f;
static constexpr float kMinimapGlass = 62.0f;
static constexpr float kHudRightTop  = kHudMargin + kMinimapRing + 24.0f;

const Waypoint& Game::CurrentWaypoint() {
    const int seat = serving == 1 ? 1 : 0;
    Waypoint& wp = seat_waypoint[seat];
    const string quest = (settings.quest_waypoints && quests) ? quests->Followed() : string();
    string key;
    if (!quest.empty()) {
        const QuestDef* d = quests->Definition(quest);
        const int stage = quests->Stage(quest);
        // A delivery points at the source until the bag holds enough, and at
        // whoever wants it after: how much is held is part of the question.
        int held = 0;
        if (d && stage >= 0 && stage < static_cast<int>(d->stages.size()) && d->stages[stage].type == ObjectiveType::Deliver)
            held = std::min(world->player.inventory.Count(d->stages[stage].target), d->stages[stage].count);
        key = quest + "|" + std::to_string(stage) + "|" + world->MapId() + "|" + std::to_string(held);
    }
    const Uint64 now = SDL_GetTicks();
    if (key != seat_waypoint_key[seat] || now - seat_waypoint_at[seat] > 250) {
        seat_waypoint_key[seat] = key;
        seat_waypoint_at[seat] = now;
        wp = quest.empty() ? Waypoint{} : waypoints.Resolve(*quests, quest, *world, &enemy_db, &loot, &items);
    }
    return wp;
}

// The way to the quest, in the world: a gold chevron bobbing over the thing when
// it is in sight, and when it is not, an arrow at the edge of the view pointing
// at it with how many paces off it is.
void Game::DrawWaypoint(const Waypoint& wp) {
    if (!wp.found) return;
    const float lift = world->LiftAt(wp.local_x, wp.local_y);
    // Over a head if it is somebody; over a way out, clear of the prompt that
    // names it, which is drawn after this and used to sit on top of it.
    const float up = wp.here ? 54.0f : 50.0f;
    const SDL_FPoint p = UiPoint(wp.local_x, wp.local_y - lift - up);
    const float w = ui.ViewWidth(), h = ui.ViewHeight();
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const auto tri = [&](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_Color col) {
        const SDL_FColor fc = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, col.a / 255.0f};
        const SDL_Vertex v[3] = {{a, fc, {0, 0}}, {b, fc, {0, 0}}, {c, fc, {0, 0}}};
        SDL_RenderGeometry(renderer, nullptr, v, 3, nullptr, 0);
    };
    const SDL_Color gold = {255, 214, 96, 255}, edge = {40, 28, 12, 235};
    const int paces = static_cast<int>(Length(wp.local_x - world->player.x, wp.local_y - world->player.y) / 32.0f);

    const float inset_x = 56.0f, inset_top = 96.0f, inset_bottom = 76.0f;
    const bool in_view = p.x > inset_x && p.x < w - inset_x && p.y > inset_top && p.y < h - inset_bottom;
    if (in_view) {
        // Not over somebody the player is already standing beside: it would sit on their name.
        if (paces < 2) return;
        const float bob = roundf(sinf(t * 4.0f) * 4.0f);
        const SDL_FPoint tip = {roundf(p.x), roundf(p.y + bob)};
        tri({tip.x - 12.0f, tip.y - 19.0f}, {tip.x + 12.0f, tip.y - 19.0f}, {tip.x, tip.y + 3.0f}, edge);
        tri({tip.x - 8.0f, tip.y - 16.0f}, {tip.x + 8.0f, tip.y - 16.0f}, {tip.x, tip.y - 1.0f}, gold);
        return;
    }
    // Out of sight: on the line from the middle of the view to it, as far out as the insets allow.
    const float cx = w / 2.0f, cy = h / 2.0f;
    float dx = p.x - cx, dy = p.y - cy;
    const float len = std::max(1.0f, Length(dx, dy));
    dx /= len; dy /= len;
    const float reach_x = dx > 0 ? (w - inset_x - cx) : (cx - inset_x);
    const float reach_y = dy > 0 ? (h - inset_bottom - cy) : (cy - inset_top);
    const float k = std::min(fabsf(dx) > 0.001f ? reach_x / fabsf(dx) : 1.0e9f, fabsf(dy) > 0.001f ? reach_y / fabsf(dy) : 1.0e9f);
    const SDL_FPoint at = {roundf(cx + dx * k), roundf(cy + dy * k)};
    const float nx = -dy, ny = dx;
    const float grow = 1.0f + 0.12f * sinf(t * 5.0f);
    const auto arrow = [&](float size, SDL_Color col) {
        tri({at.x + dx * size, at.y + dy * size},
            {at.x - dx * size * 0.7f + nx * size * 0.75f, at.y - dy * size * 0.7f + ny * size * 0.75f},
            {at.x - dx * size * 0.7f - nx * size * 0.75f, at.y - dy * size * 0.7f - ny * size * 0.75f}, col);
    };
    arrow(18.0f * grow, edge);
    arrow(13.0f * grow, gold);
    ui.TextShadowed(std::to_string(paces), at.x - dx * 30.0f, at.y - dy * 30.0f - 8.0f, TextSize::Small, gold, Align::Center);
}

void Game::DrawXpLines(float x, float y) {
    const Player& p = world->player;
    for (const XpLine& line : xp_lines) {
        // How far through the level this leaves you: the number that says
        // whether the next one is a swing away or an evening.
        const int level = p.skills.Level(line.skill);
        const int have = p.skills.Xp(line.skill);
        const int from = XpForLevel(level), to = XpForLevel(level + 1);
        char buf[96];
        if (level >= MAX_SKILL_LEVEL || to <= from)
            SDL_snprintf(buf, sizeof(buf), "+%d %s", line.amount, SkillName(line.skill));
        else
            SDL_snprintf(buf, sizeof(buf), "+%d %s   %d%%", line.amount, SkillName(line.skill),
                         std::clamp(static_cast<int>(100.0f * (have - from) / (to - from)), 0, 99));
        SDL_Color c = Palette::Xp;
        c.a = static_cast<Uint8>(255.0f * std::clamp((2.8f - line.age) / 0.6f, 0.0f, 1.0f));
        ui.TextShadowed(buf, x, y, TextSize::Small, c);
        y += ui.LineHeight(TextSize::Small) + 2.0f;
    }
}

void Game::DrawHud() {
    const Player& p = world->player;
    const Waypoint& waypoint = CurrentWaypoint();

    // --- vitals --------------------------------------------------------------
    // Health and mana in brass plates, each with a glyph at the left end, so
    // the two are told apart at a glance rather than by colour alone.
    const float glyph = 22.0f;
    const float line_h = ui.LineHeight(TextSize::Small);

    auto glyph_plate = [&](const SDL_FRect& box, const char* icon) {
        ui.Fill(box, {30, 22, 15, 255});
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w - 2.0f, box.h - 2.0f}, {124, 98, 52, 255});
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w - 2.0f, 1.0f}, {186, 156, 92, 255});
        ui.Fill({box.x + 1.0f, box.y + box.h - 2.0f, box.w - 2.0f, 1.0f}, {74, 56, 28, 255});
        ui.Fill({box.x + 2.0f, box.y + 2.0f, box.w - 4.0f, box.h - 4.0f}, {24, 18, 15, 255});
        if (SDL_Texture* tex = textures->Get(icon)) {
            const float s = box.h - 6.0f;
            const SDL_FRect dst = {roundf(box.x + (box.w - s) / 2.0f),
                                   roundf(box.y + (box.h - s) / 2.0f), s, s};
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    };

    const SDL_FRect hp_bar = {18.0f + glyph + 4.0f, 16.0f, 232.0f, glyph};
    glyph_plate({18.0f, hp_bar.y, glyph, glyph}, "assets/icons/hud_heart.png");
    ui.FramedBar(hp_bar, p.max_hp > 0 ? static_cast<float>(p.hp) / p.max_hp : 0.0f,
                 Palette::Health, Palette::HealthBack);
    char hp_text[32];
    SDL_snprintf(hp_text, sizeof(hp_text), "%d / %d", p.hp, p.max_hp);
    ui.TextShadowed(hp_text, hp_bar.x + hp_bar.w / 2.0f,
                    hp_bar.y + (hp_bar.h - line_h) / 2.0f,
                    TextSize::Small, Palette::Text, Align::Center);

    // --- mana ----------------------------------------------------------------
    // Only shown once the player has any, so a pure melee character is not
    // told about a resource they never spend.
    float meta_y = hp_bar.y + hp_bar.h + 8.0f;
    if (p.MaxMana() > 0) {
        const float mana_h = 16.0f;
        const SDL_FRect mana_bar = {hp_bar.x, hp_bar.y + hp_bar.h + 4.0f, 232.0f, mana_h};
        glyph_plate({18.0f, mana_bar.y, glyph, mana_h}, "assets/icons/hud_drop.png");
        ui.FramedBar(mana_bar, static_cast<float>(p.Mana()) / p.MaxMana(),
                     Palette::Mana, Palette::ManaBack);
        char mana_text[32];
        SDL_snprintf(mana_text, sizeof(mana_text), "%d / %d", p.Mana(), p.MaxMana());
        ui.TextShadowed(mana_text, mana_bar.x + mana_bar.w / 2.0f,
                        mana_bar.y + (mana_bar.h - line_h) / 2.0f,
                        TextSize::Small, Palette::Text, Align::Center);
        meta_y = mana_bar.y + mana_bar.h + 6.0f;
    }

    // --- the battery ---------------------------------------------------------
    // Lightning's charge, standing on its end beside the health and the mana
    // like a cell in a torch: green, filling from the bottom, with a terminal
    // on top so it reads as a battery and not as a third pool. Only for a
    // character whose Magic reaches any of the lightning -- nobody else has a
    // way to put anything in it, and a bar that can only ever be empty is a
    // question with no answer.
    if (!KnownElectric().empty()) {
        const float cell_w = 22.0f, nub_w = 10.0f, nub_h = 3.0f;
        const float top = hp_bar.y + nub_h;
        const float bottom = (p.MaxMana() > 0 ? hp_bar.y + hp_bar.h + 4.0f + 16.0f : hp_bar.y + hp_bar.h);
        const SDL_FRect cell = {hp_bar.x + hp_bar.w + 10.0f, top, cell_w, bottom - top};
        const float charge = p.Battery();

        // The terminal, then the case, then what is in it.
        ui.Fill({cell.x + (cell_w - nub_w) / 2.0f, cell.y - nub_h, nub_w, nub_h}, {124, 98, 52, 255});
        ui.Fill(cell, {30, 22, 15, 255});
        ui.Fill({cell.x + 1.0f, cell.y + 1.0f, cell.w - 2.0f, cell.h - 2.0f}, {124, 98, 52, 255});
        const SDL_FRect glass = {cell.x + 2.0f, cell.y + 2.0f, cell.w - 4.0f, cell.h - 4.0f};
        ui.Fill(glass, {16, 26, 18, 255});
        const float fill_h = floorf(glass.h * std::clamp(charge, 0.0f, 1.0f));
        if (fill_h > 0.0f) {
            // Brighter the fuller it is, and it breathes at the top of the bar
            // so a full battery says so without a word.
            const float pulse = charge >= 0.999f
                ? 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) * 0.008f) : 0.0f;
            const SDL_Color green = {static_cast<Uint8>(74 + 60 * charge + 40 * pulse),
                                     static_cast<Uint8>(176 + 50 * charge + 25 * pulse),
                                     static_cast<Uint8>(84 + 30 * charge + 60 * pulse), 255};
            ui.Fill({glass.x, glass.y + glass.h - fill_h, glass.w, fill_h}, green);
            ui.Fill({glass.x, glass.y + glass.h - fill_h, glass.w, 1.0f},
                    {static_cast<Uint8>(200), static_cast<Uint8>(255), static_cast<Uint8>(190), 255});
        }
        ui.Claim("battery", {cell.x, cell.y - nub_h, cell.w, cell.h + nub_h});
        // The number under it, small: what a spell's cost is measured against.
        char pct[16];
        SDL_snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(charge * 100.0f + 0.5f));
        ui.TextShadowed(pct, cell.x + cell.w / 2.0f, cell.y + cell.h + 1.0f, TextSize::Small,
                        charge > 0.0f ? SDL_Color{150, 226, 140, 255} : Palette::TextDim, Align::Center);
    }

    // --- what is on them ----------------------------------------------------
    // Beside the bars, a chip for each thing a monster has left on them: its
    // colour, its name, and how long it has to run -- a poison is worth
    // knowing the end of, and a charm or a confusion is worth knowing the
    // name of, since it explains why the keys stopped answering. A friend's
    // machine is told which, and not for how long.
    if (p.statuses.Any()) {
        float cx = hp_bar.x + hp_bar.w + 10.0f + (KnownElectric().empty() ? 0.0f : 32.0f);
        const float cy = hp_bar.y;
        const float h = line_h + 6.0f;
        const float x0 = cx;
        for (int i = 0; i < STATUS_COUNT; ++i) {
            const float left = p.statuses.left[i];
            if (left <= 0.0f) continue;
            const StatusDef* d = status_db.Get(static_cast<Status>(i));
            if (!d) continue;
            string label = d->name;
            if (left > 0.5f) label += "  " + std::to_string(static_cast<int>(ceilf(left)));
            const float w = ui.Measure(label, TextSize::Small).x + 24.0f;
            ui.Fill({cx, cy, w, h}, {24, 18, 15, 220});
            ui.Fill({cx, cy, w, 1.0f}, {124, 98, 52, 255});
            ui.Fill({cx, cy + h - 1.0f, w, 1.0f}, {74, 56, 28, 255});
            // Its colour, as a pip, and it throbs in the last second.
            const bool ending = left < 1.0f && fmodf(static_cast<float>(SDL_GetTicks()) * 0.006f, 1.0f) < 0.5f;
            const SDL_Color c = d->color;
            ui.Fill({cx + 5.0f, cy + (h - 10.0f) / 2.0f, 10.0f, 10.0f}, {14, 10, 8, 255});
            ui.Fill({cx + 6.0f, cy + (h - 8.0f) / 2.0f, 8.0f, 8.0f},
                    ending ? SDL_Color{static_cast<Uint8>(c.r / 2), static_cast<Uint8>(c.g / 2), static_cast<Uint8>(c.b / 2), 255} : c);
            ui.TextShadowed(label, cx + 19.0f, cy + 3.0f, TextSize::Small, c);
            cx += w + 4.0f;
        }
        ui.Claim("what is on them", {x0, cy, cx - x0, h});
    }

    // --- stamina -------------------------------------------------------------
    // Under whichever bar is last, and slimmer, since it changes all the time
    // and wants to be glanced at rather than read. Winded, the bar pulses red
    // until it has refilled far enough to sprint again.
    {
        const float st_h = 14.0f;
        const float top = meta_y - 2.0f;
        const SDL_FRect st_bar = {hp_bar.x, top, 232.0f, st_h};
        glyph_plate({18.0f, st_bar.y, glyph, st_h}, "assets/icons/hud_stamina.png");
        SDL_Color fill = Palette::Stamina;
        // Out of breath and out of guard pulse the same red: both are the bar
        // run dry, and both lift at the same point. A raised guard turns the
        // bar steel blue, because it is what every blow on the shield is paid
        // out of and that is worth seeing at a glance.
        const bool spent = p.Winded() || p.GuardBroken();
        if (spent) {
            const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) * 0.012f);
            fill = {static_cast<Uint8>(196 + 40 * pulse), static_cast<Uint8>(70 + 30 * pulse),
                    static_cast<Uint8>(48), 255};
        } else if (p.Blocking()) {
            fill = {122, 166, 214, 255};
        }
        ui.FramedBar(st_bar, p.Stamina() / p.MaxStamina(), fill, Palette::StaminaBack);
        const char* label = p.GuardBroken() ? "guard broken" : p.Winded() ? "winded"
                          : p.Blocking() ? "guard" : nullptr;
        if (label)
            ui.TextShadowed(label, st_bar.x + st_bar.w / 2.0f,
                            st_bar.y + (st_bar.h - line_h) / 2.0f,
                            TextSize::Small, Palette::Text, Align::Center);
        meta_y = st_bar.y + st_bar.h + 6.0f;
    }

    // --- Rushing Strike ------------------------------------------------------
    // A hairline under the stamina bar, only for a character who has the move
    // and a melee weapon to make it with: amber and full when a running light
    // attack will leap, filling back up over the three seconds after one.
    if (p.talents.Effect("rushing_strike", AttackStyle::Melee) > 0.0f && p.Style() == AttackStyle::Melee) {
        const float ready = 1.0f - std::clamp(p.RushCooldown() / Player::RUSH_COOLDOWN, 0.0f, 1.0f);
        const SDL_FRect rush_bar = {hp_bar.x, meta_y - 3.0f, 232.0f, 5.0f};
        ui.Fill(rush_bar, {24, 20, 18, 220});
        ui.Fill({rush_bar.x, rush_bar.y, rush_bar.w * ready, rush_bar.h},
                ready >= 1.0f ? SDL_Color{236, 176, 72, 255} : SDL_Color{120, 100, 70, 255});
        meta_y = rush_bar.y + rush_bar.h + 6.0f;
    }

    // --- minimap -------------------------------------------------------------
    // Top right, with the bezel hung on the corner; everything else that used
    // to live in that corner now stacks below it.
    (serving == 1 ? minimap_two : minimap).Draw(renderer, *textures, ui, (*world),
                 ui.ViewWidth() - kHudMargin - kMinimapRing / 2.0f,
                 kHudMargin + kMinimapRing / 2.0f, kMinimapGlass, &waypoint);

    char meta[96];
    SDL_snprintf(meta, sizeof(meta), "Combat %d    %d coins",
                 p.skills.CombatLevel(), p.inventory.Coins());
    ui.TextShadowed(meta, hp_bar.x, meta_y, TextSize::Small, Palette::TextDim);

    // --- the time ------------------------------------------------------------
    // A sun or a moon and the hour, under the vitals. In a dream it counts
    // down to dawn instead, which is when the dream ends.
    float under_clock = meta_y + 2.0f * (line_h + 4.0f);   // where the meal and the wards go
    {
        const WorldClock& c = world->clock;
        const float y = meta_y + line_h + 4.0f;
        under_clock = y + line_h + 6.0f;
        const bool moon = world->InDream() || c.IsNight() || string(c.Phase()) == "Dusk";
        glyph_plate({18.0f, y - 2.0f, glyph, line_h + 4.0f},
                    moon ? "assets/icons/hud_moon.png" : "assets/icons/hud_sun.png");
        char line[96];
        SDL_Color col = Palette::TextDim;
        if (world->InDream()) {
            const int s = static_cast<int>(c.SecondsToDawn());
            SDL_snprintf(line, sizeof(line), "Dreaming    dawn in %d:%02d", s / 60, s % 60);
            col = {206, 186, 250, 255};
        } else {
            SDL_snprintf(line, sizeof(line), "Day %d    %s    %s", c.Day(), c.TimeText().c_str(), c.Phase());
            if (c.CanSleep()) col = {176, 186, 236, 255};
        }
        ui.TextShadowed(line, hp_bar.x, y, TextSize::Small, col);
    }

    // Down the left edge, under everything that lives in the top corner.
    if (settings.xp_drops) DrawXpLines(hp_bar.x, floorf(ui.ViewHeight() * 0.40f));

    // Meters and prompts describe what the button does right now, so they are
    // only meaningful while the player actually has control.
    const bool live = (state == GameState::Play);

    // --- abilities -----------------------------------------------------------
    // The ones carried, bottom left, stacked up from the hint line: the keys,
    // the name, and a bar that refills as it comes back. What is running -- a
    // war cry, a mana shield -- beside. Drawn ahead of the middle of the foot
    // of the screen, which has to know how much of this corner is taken: the
    // boxes, and the line beside them.
    SDL_FRect corner[2] = {};
    {
        const Player& me = world->player;
        int carrying = 0;
        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) carrying += me.talents.Ability(slot) ? 1 : 0;
        // What is to hand goes at the foot of the same stack, when there is
        // anything that could be: a box like an ability's, with how many are
        // left where the cooldown would be.
        string quick = me.QuickItem();
        if (quick.empty() && !me.QuickChoices().empty()) quick = me.QuickChoices().front();
        const ItemDef* quick_def = quick.empty() ? nullptr : items.Get(quick);
        const int rows_up = carrying + (quick_def ? 1 : 0);
        const float ay = ui.ViewHeight() - 40.0f - 30.0f * static_cast<float>(std::max(1, rows_up));
        // Where what is running is written, beside the top box -- pushed along
        // when the top box is what is to hand and has its tab beside it.
        float running_x = 240.0f;
        if (quick_def && live) {
            const int have = me.inventory.Count(quick);
            const SDL_FRect box = {18.0f, ay + static_cast<float>(carrying) * 30.0f, 214.0f, 26.0f};
            const float chewing = std::clamp(me.EatCooldown() / Player::EAT_COOLDOWN, 0.0f, 1.0f);
            ui.Fill(box, {18, 15, 13, 190});
            if (have > 0)
                ui.Fill({box.x, box.y, box.w * (1.0f - chewing), box.h},
                        chewing > 0.0f ? SDL_Color{58, 70, 48, 210} : SDL_Color{52, 92, 56, 225});
            ui.Outline(box, have > 0 && chewing <= 0.0f ? Palette::Xp : Palette::BorderDim, 1.0f);
            ui.Text(input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(Action::Interact),
                    box.x + 6.0f, box.y + 4.0f, TextSize::Small, Palette::TextDim);
            if (!quick_def->icon.empty())
                if (SDL_Texture* tex = textures->Get(quick_def->icon)) {
                    const SDL_FRect ic = {box.x + 60.0f, box.y + 3.0f, 20.0f, 20.0f};
                    SDL_RenderTexture(renderer, tex, nullptr, &ic);
                }
            ui.Text(quick_def->name, box.x + 84.0f, box.y + 4.0f, TextSize::Small,
                    have > 0 ? Palette::Text : SDL_Color{150, 110, 100, 255});
            ui.Text("x" + std::to_string(have), box.x + box.w - 8.0f, box.y + 4.0f, TextSize::Small,
                    have > 0 ? Palette::TextDim : SDL_Color{200, 110, 100, 255}, Align::Right);
            // Anything else that could be to hand -- a potion, another dish --
            // and a tab beside the box that says how to step to it, so the
            // slot is not taken for the one thing it happens to hold. (The
            // bag's lock-on button puts one there directly.)
            if (me.QuickChoices().size() > 1) {
                const string next = input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(Action::Sprint) + " next";
                const SDL_FRect tab = {box.x + box.w + 4.0f, box.y, 14.0f + 7.2f * static_cast<float>(next.size()), box.h};
                ui.Fill(tab, {18, 15, 13, 170});
                ui.Outline(tab, Palette::BorderDim, 1.0f);
                ui.Text(next, tab.x + 7.0f, tab.y + 4.0f, TextSize::Small, Palette::TextDim);
                if (carrying == 0) running_x = tab.x + tab.w + 10.0f;
            }
        }
        int drawn = 0;
        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
            const TalentNode* carried = me.talents.Ability(slot);
            if (!carried) continue;
            const SDL_FRect box = {18.0f, ay + static_cast<float>(drawn++) * 30.0f, 214.0f, 26.0f};
            const float left = me.AbilityCooldown(slot);
            const float ready = carried->cooldown > 0.0f ? 1.0f - std::clamp(left / carried->cooldown, 0.0f, 1.0f) : 1.0f;
            const bool afford = (carried->stamina_cost <= 0 || me.Stamina() >= carried->stamina_cost) &&
                                (carried->mana_cost <= 0 || me.Mana() >= carried->mana_cost);
            ui.Fill(box, {18, 15, 13, 190});
            ui.Fill({box.x, box.y, box.w * ready, box.h}, left > 0.0f ? SDL_Color{52, 66, 88, 210}
                                                         : afford ? SDL_Color{46, 84, 120, 225} : SDL_Color{88, 52, 46, 215});
            ui.Outline(box, left > 0.0f ? Palette::BorderDim : SDL_Color{130, 190, 240, 255}, 1.0f);
            ui.Text(input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(AbilityButton(slot)),
                    box.x + 6.0f, box.y + 4.0f, TextSize::Small, Palette::TextDim);
            ui.Text(carried->name, box.x + 62.0f, box.y + 4.0f, TextSize::Small, left > 0.0f ? Palette::TextDim : Palette::Text);
            if (left > 0.0f)
                ui.Text(std::to_string(static_cast<int>(std::ceil(left))), box.x + box.w - 8.0f, box.y + 4.0f,
                        TextSize::Small, Palette::TextDim, Align::Right);
        }
        string running;
        if (me.WarCry())     running += "War Cry " + std::to_string(static_cast<int>(std::ceil(me.WarCryLeft()))) + "   ";
        if (me.ManaShield()) running += "Mana Shield " + std::to_string(static_cast<int>(std::ceil(me.ManaShieldLeft()))) + "   ";
        if (me.Frenzied())     running += "Frenzy " + std::to_string(static_cast<int>(std::ceil(me.FrenzyLeft()))) + "   ";
        if (me.StandingFast()) running += "Stand Fast " + std::to_string(static_cast<int>(std::ceil(me.StandFastLeft()))) + "   ";
        if (me.RapidFire())    running += "Rapid Fire " + std::to_string(static_cast<int>(std::ceil(me.RapidFireLeft()))) + "   ";
        if (me.Aiming())       running += "Aim held   ";
        if (me.Overloaded())   running += "Overloaded   ";
        if (me.Invoking())     running += "Invoking   ";
        if (me.RiposteReady()) running += "Riposte ready   ";
        // A dagger's parry: its moment, and then the poor guard after it; and
        // the riposte a parry left owed (Counter), with the button that makes it.
        if (me.Parrying())     running += me.ParryOpen() ? "Parry!   " : "Parrying   ";
        if (me.RiposteOwed())  running += "Riposte: " + input.PromptFor(Action::LightAttack) + "   ";
        if (!running.empty()) ui.TextShadowed(running, running_x, ay + 6.0f, TextSize::Small, {255, 214, 140, 255});

        if (rows_up > 0) corner[0] = {18.0f, ay, 214.0f, 30.0f * static_cast<float>(rows_up) - 4.0f};
        if (!running.empty()) corner[1] = {240.0f, ay + 6.0f, ui.Measure(running, TextSize::Small).x, line_h};
        ui.Claim("abilities", corner[0]);
        ui.Claim("what is running", corner[1]);
    }

    // --- the foot of the screen -------------------------------------------------
    // The key hints run along the bottom edge, and what the buttons will do
    // right now is stacked up from them in the middle: the element bar with its
    // spell named under it, the line for a combo or a technique, the prompt.
    // Each goes on top of the last and is measured, not counted up from the
    // bottom on its own -- which is how the spell's name and the key hints came
    // to be written on the same line, twenty-eight up, and a combo across the
    // element bar. At no size of window or of interface is there a line here
    // with two things on it.
    //
    // In half a screen the middle reaches over the corner the abilities are
    // in. Whatever would land on them there goes above them instead, and the
    // rest of the stack on top of that.
    const float foot_y = ui.ViewHeight() - 28.0f;
    float stack_y = foot_y;         // the top of whatever was stacked last
    float stack_half = 0.0f;        // and half the width of the widest of it
    const auto stack = [&](const char* what, float w, float h, float gap) {
        const float left = roundf(ui.ViewWidth() / 2.0f - w / 2.0f);
        float top = stack_y - gap - h;
        for (const SDL_FRect& c : corner)
            if (c.w > 0.0f && left < c.x + c.w + 8.0f && top < c.y + c.h + 4.0f && top + h > c.y - 4.0f)
                top = c.y - 4.0f - h;
        stack_y = top;
        stack_half = std::max(stack_half, w / 2.0f);
        ui.Claim(what, {left, top, w, h});
        return top;
    };

    // --- what the attack button will do ---------------------------------------
    // A staff shows the four elements with the selected one lit, and names the
    // spell that Magic level actually casts. A bow just says so.
    if (live) {
        const AttackStyle style = p.Style();
        if (style == AttackStyle::Magic) {
            static constexpr int kBoxes = 6;
            static const Element kOrder[kBoxes] = {Element::Fire, Element::Water, Element::Earth,
                                                   Element::Air, Element::Electric, Element::Arcane};
            const float box = 30.0f, gap = 5.0f;
            const float total = box * kBoxes + gap * (kBoxes - 1);
            const float x0 = ui.ViewWidth() / 2.0f - total / 2.0f;
            const vector<string> arcane_known = world->KnownArcane(spells);

            // The line under the boxes first: the bar and its name are one
            // piece of the stack, as wide as the wider of them.
            const bool arcane_on = p.SelectedElement() == Element::Arcane;
            const bool electric_on = p.SelectedElement() == Element::Electric;
            const vector<string> electric_known = KnownElectric();
            // An element's own staff: the four boxes are its four spells.
            const Element staff = p.StaffElement();
            const int magic = p.skills.Level(SKILL_MAGIC);
            const SpellDef* current = arcane_on   ? spells.Get(p.ArcaneSpell())
                                    : electric_on ? spells.Get(p.ElectricSpell())
                                                  : p.SpellOf(p.SelectedElement(), spells);
            string line;
            // With more than one to choose between in the slot, the name has
            // a mark either side: the right stick, pushed, steps through them.
            const bool choosing = SpellsInSlot().size() > 1;
            const string named = current ? (choosing ? "< " + current->name + " >" : current->name) : string();
            if (current && arcane_on && current->level > p.skills.Level(SKILL_MAGIC)) {
                line = named + "   needs Magic " + std::to_string(current->level);
            } else if (current) {
                line = named + "   " + std::to_string(current->mana) + " mana";
                // The keys still step through them the way they always did;
                // a pad says which way it steps now.
                if (arcane_on && arcane_known.size() > 1 && input.ActiveDevice() != InputMode::Controller)
                    line += "   " + input.PromptFor(Action::SelectArcane) + " again: next";
                if (electric_on && electric_known.size() > 1 && input.ActiveDevice() != InputMode::Controller)
                    line += "   " + input.PromptFor(Action::SelectElectric) + " again: next";
                // What it costs of the charge, and whether there is that much.
                if (electric_on && current->battery_cost > 0.0f) {
                    const int want = static_cast<int>(std::lround(current->battery_cost * 100.0f));
                    line += current->battery_cost >= 1.0f ? "   the whole charge"
                          : "   " + std::to_string(want) + "% charge";
                } else if (electric_on && current->battery_gain > 0.0f) {
                    line += "   +" + std::to_string(static_cast<int>(std::lround(current->battery_gain * 100.0f))) + "% charge";
                }
                // A staff's technique rides on the same line as the spell.
                if (const TalentNode* t = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique()))
                    line += "     hold " + input.PromptFor(Action::StrongAttack) + ": " + t->name;
            } else {
                const vector<const SpellDef*> all_electric = electric_on ? spells.Electric(MAX_SKILL_LEVEL)
                                                                         : vector<const SpellDef*>{};
                const SpellDef* next = arcane_on ? nullptr
                                     : electric_on ? (all_electric.empty() ? nullptr : all_electric.front())
                                     : (staff != Element::None && p.SpellSlot() > 0) ? spells.FirstOnSlot(staff, p.SpellSlot() + 1)
                                     : spells.NextFor(p.SelectedElement(), p.skills.Level(SKILL_MAGIC));
                line = next ? ("Magic " + std::to_string(next->level) + " for " + next->name)
                            : arcane_on ? "No ancient magic known" : "Nothing known";
            }
            const float y0 = stack("element bar", std::max(total, ui.Measure(line, TextSize::Small).x),
                                   box + 4.0f + line_h, 5.0f);

            for (int i = 0; i < kBoxes; ++i) {
                // An element's own staff takes the first four boxes for its
                // own four spells; the lightning and the ancient magic keep
                // theirs whatever is in hand.
                const bool slotted = staff != Element::None && i < 4;
                const bool on = slotted ? (!arcane_on && !electric_on && p.SpellSlot() == i)
                                        : (kOrder[i] == p.SelectedElement());
                const SDL_FRect r = {x0 + i * (box + gap), y0, box, box};
                const SDL_Color c = ElementColor(slotted ? staff : kOrder[i]);
                // The fifth box is lit once any ancient spell is known.
                const SpellDef* known = kOrder[i] == Element::Arcane
                    ? (arcane_known.empty() ? nullptr : spells.Get(arcane_known.front()))
                    : kOrder[i] == Element::Electric
                    ? (electric_known.empty() ? nullptr : spells.Get(p.ElectricSpell().empty() ? electric_known.front() : p.ElectricSpell()))
                    : slotted ? (i == 0 ? spells.Chosen(staff, magic, p.HeldSpell(staff)) : spells.ForSlot(staff, i + 1, magic))
                    : p.SpellOf(kOrder[i], spells);

                ui.Fill(r, {static_cast<Uint8>(c.r / (on ? 2 : 5)),
                            static_cast<Uint8>(c.g / (on ? 2 : 5)),
                            static_cast<Uint8>(c.b / (on ? 2 : 5)),
                            on ? static_cast<Uint8>(235) : static_cast<Uint8>(180)});
                ui.Outline(r, on ? c : Palette::BorderDim, on ? 2.0f : 1.0f);

                // Number key that selects it.
                ui.Text(std::to_string(i + 1), r.x + r.w / 2.0f, r.y + 6.0f,
                        TextSize::Small,
                        known ? (on ? Palette::Text : Palette::TextDim)
                              : SDL_Color{110, 100, 96, 255},
                        Align::Center);
            }

            // Just changed, it lights up for a moment, so the eye goes to it.
            const SDL_Color spell_col = !current ? Palette::TextDim
                                      : spell_flash > 0.0f ? Palette::Highlight
                                                           : ElementColor(p.SelectedElement());
            ui.TextShadowed(line, ui.ViewWidth() / 2.0f, y0 + box + 4.0f,
                            TextSize::Small, spell_col, Align::Center);
        }

        // The technique a held heavy attack will come out as, from the tree --
        // on the line under the prompts, where the bow says it is drawn. The
        // line keeps its place in the stack with nothing on it, so a prompt
        // does not hop up for the two fifths of a second a chain is open.
        const TalentNode* tech = p.ActiveTechnique().empty() ? nullptr : skill_trees.Find(p.ActiveTechnique());
        const ComboMove next_light = p.NextCombo(true), next_heavy = p.NextCombo(false);
        string line;
        SDL_Color line_col = Palette::TextDim;
        if (next_light != ComboMove::None || next_heavy != ComboMove::None) {
            // The chain is open: what each button would come out as, for the
            // moment the window lasts.
            if (next_light != ComboMove::None)
                line += input.PromptFor(Action::LightAttack) + ": " + p.ComboLabel(next_light);
            if (next_heavy != ComboMove::None)
                line += (line.empty() ? string("") : string("     ")) +
                        input.PromptFor(Action::StrongAttack) + ": " + p.ComboLabel(next_heavy);
            line_col = {255, 224, 140, 255};
        } else if (tech && style != AttackStyle::Magic) {
            line = "Hold " + input.PromptFor(Action::StrongAttack) + ": " + tech->name;
            line_col = {236, 150, 110, 255};
        } else if (style == AttackStyle::Ranged) {
            line = "Bow drawn";
        }
        const float line_y = stack("attack line", line.empty() ? 0.0f : ui.Measure(line, TextSize::Small).x, line_h, 5.0f);
        ui.TextShadowed(line, ui.ViewWidth() / 2.0f, line_y, TextSize::Small, line_col, Align::Center);
    }

    // --- the chain -------------------------------------------------------------
    // Melee swings landed one after another, under the target frame: the
    // count large, and what each swing was on a line beneath it. It holds
    // for a moment after the last hit and fades; a swing that lands on
    // nothing or a blow taken ends it. From two, so a single hit is not an
    // announcement.
    if (live && p.ChainHits() >= 2 && p.ChainFade() > 0.0f) {
        const float fade = p.ChainFade();
        const int hits = p.ChainHits();
        // Pale, then amber, then ember as the run grows.
        SDL_Color col = hits >= 8 ? SDL_Color{255, 128, 72, 255}
                      : hits >= 5 ? SDL_Color{255, 204, 96, 255}
                                  : SDL_Color{240, 236, 220, 255};
        col.a = static_cast<Uint8>(255.0f * fade);
        const float cx = ui.ViewWidth() / 2.0f, y = 70.0f;
        ui.TextShadowed(std::to_string(hits), cx - 6.0f, y, TextSize::Large, col, Align::Right);
        ui.TextShadowed("HITS", cx + 4.0f, y + ui.LineHeight(TextSize::Large) - ui.LineHeight(TextSize::Small) - 2.0f,
                        TextSize::Small, col, Align::Left);
        const auto& t = p.ChainTrail();
        string trail;
        for (size_t i = 0; i < t.size(); ++i) trail += (i ? "  >  " : "") + t[i];
        if (hits > static_cast<int>(t.size())) trail = "...  >  " + trail;
        const SDL_Color dim = {col.r, col.g, col.b, static_cast<Uint8>(215.0f * fade)};
        ui.TextShadowed(trail, cx, y + ui.LineHeight(TextSize::Large) + 2.0f, TextSize::Small, dim, Align::Center);
    }

    // --- target frame ----------------------------------------------------------
    // Who the fight is with, at the top of the screen: name, level and health,
    // with a red frame and a LOCKED tag while the lock is on. Out of combat
    // there is no target, and nothing is drawn.
    if (live) {
        if (const Enemy* t = world->targeting.Current(); t && t->Def()) {
            const bool lock = world->targeting.IsLocked();
            const float w = 280.0f, cx = ui.ViewWidth() / 2.0f;
            const SDL_FRect box = {roundf(cx - w / 2.0f), 12.0f, w, 48.0f};
            ui.Fill(box, {14, 11, 9, 200});
            ui.Outline(box, lock ? SDL_Color{206, 70, 56, 255} : Palette::BorderDim, lock ? 2.0f : 1.0f);
            // `boss` in enemies.json was read into the definition and then by
            // nothing. It is what tells a player the thing in front of them is
            // not one of a pack, so the frame says so, in the gold a rare
            // thing is written in.
            const bool boss = t->Def()->is_boss;
            ui.TextShadowed(t->Def()->name, box.x + 12.0f, box.y + 5.0f, TextSize::Small,
                            boss ? SDL_Color{255, 214, 96, 255} : (lock ? Palette::Highlight : Palette::Text));
            const string level_tag = string(boss ? "Boss   Lv " : "Lv ") + std::to_string(t->ShownLevel());
            ui.TextShadowed(lock ? "LOCKED   " + level_tag
                                 : input.PromptFor(Action::Target) + " lock   " + level_tag,
                            box.x + box.w - 12.0f, box.y + 5.0f, TextSize::Small,
                            lock ? SDL_Color{236, 110, 90, 255} : Palette::TextDim, Align::Right);
            ui.FramedBar({box.x + 12.0f, box.y + 27.0f, box.w - 24.0f, 12.0f}, t->HealthFraction(),
                         {196, 44, 40, 255}, {40, 16, 14, 255});
        }
    }

    // --- charge meter --------------------------------------------------------
    if (live && p.IsCharging()) {
        const float t = p.ChargeProgress();
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 34.0f, anchor.y + 8.0f, 68.0f, 8.0f};
        // Turns bright at full charge, so the release timing is readable.
        const SDL_Color fill = (t >= 1.0f) ? SDL_Color{255, 236, 150, 255} : Palette::Charge;
        ui.Bar(bar, t, fill, {30, 20, 12, 220});
        if (t >= 1.0f)
            ui.TextShadowed("READY", bar.x + bar.w / 2.0f, bar.y - 18.0f,
                            TextSize::Small, fill, Align::Center);
    }

    // --- attack cooldown -----------------------------------------------------
    // A gate the player cannot see is just an unresponsive button. This is
    // deliberately small and quiet -- it drains rather than fills, sits under
    // the feet, and is gone inside a fifth of a second between light attacks --
    // because the point is to make the rhythm legible, not to put a cooldown
    // bar in the middle of a fight.
    if (live && !p.IsCharging() && p.CooldownProgress() > 0.0f) {
        const float t = p.CooldownProgress();
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 20.0f, anchor.y + 9.0f, 40.0f, 3.0f};
        ui.Bar(bar, t, SDL_Color{188, 170, 140, 190}, {26, 22, 18, 150});
    }

    // --- ledge prompt --------------------------------------------------------
    // A climbable ledge is otherwise indistinguishable from a wall you cannot
    // pass: you walk into it and stop either way. Pushing against one says what
    // the jump button will do there, which is the only way anyone would guess.
    if (live && !p.ClimbHint().empty() && p.interact.kind == InteractTarget::None) {
        const string prompt = "[" + input.PromptFor(Action::Jump) + "]  " + p.ClimbHint();
        const SDL_FPoint size = ui.Measure(prompt, TextSize::Body);
        const SDL_FRect box = {ui.ViewWidth() / 2.0f - size.x / 2.0f - 14.0f,
                               stack("prompt", size.x + 28.0f, size.y + 12.0f, 17.0f),
                               size.x + 28.0f, size.y + 12.0f};
        ui.Panel(box);
        ui.Text(prompt, box.x + 14.0f, box.y + 6.0f, TextSize::Body, Palette::Highlight);
    }

    // --- gathering -----------------------------------------------------------
    if (live && world->Gathering()) {
        const SDL_FPoint anchor = UiPoint(p.x, p.y + 10.0f);
        const SDL_FRect bar = {anchor.x - 34.0f, anchor.y + 8.0f, 68.0f, 8.0f};
        ui.Bar(bar, world->GatherProgress(), Palette::Xp, {20, 30, 20, 220});
    }

    // --- interact prompt -----------------------------------------------------
    // The top of the stack at the foot of the screen, like the ledge's: there
    // is only ever one of the two.
    if (live && p.interact.kind != InteractTarget::None && !world->Gathering()) {
        const string prompt = "[" + input.PromptFor(Action::Interact) + "]  " + p.interact.label;
        const SDL_FPoint size = ui.Measure(prompt, TextSize::Body);
        const SDL_FRect box = {ui.ViewWidth() / 2.0f - size.x / 2.0f - 14.0f,
                               stack("prompt", size.x + 28.0f, size.y + 12.0f, 17.0f),
                               size.x + 28.0f, size.y + 12.0f};
        ui.Fill(box, {20, 16, 12, 210});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        ui.Text(prompt, ui.ViewWidth() / 2.0f, box.y + 6.0f, TextSize::Body,
                Palette::Text, Align::Center);
    }

    // --- the ways out ---------------------------------------------------------
    // An exit at the edge of an outdoor map is a gap in the trees, and easy to
    // walk straight past. Near one, say where it goes and which way.
    if (live && !world->CurrentMap().IsInterior()) {
        const Map& mp = world->CurrentMap();
        for (const Portal& portal : mp.Portals()) {
            if (portal.requires_interact) continue;
            const float px = portal.rect.x + portal.rect.w / 2.0f;
            const float py = portal.rect.y + portal.rect.h / 2.0f;
            const float d = Length(px - p.x, py - p.y);
            if (d > 260.0f) continue;
            const float fade = std::clamp((260.0f - d) / 100.0f, 0.0f, 1.0f);

            // Which edge the exit is on decides the arrow.
            const float to_l = px, to_r = mp.Width() - px, to_t = py, to_b = mp.Height() - py;
            const float nearest = std::min({to_l, to_r, to_t, to_b});
            string text;
            float nx = 0.0f, ny = 0.0f;
            if (nearest == to_l)      { text = "< " + portal.label; nx = -1.0f; }
            else if (nearest == to_r) { text = portal.label + " >"; nx =  1.0f; }
            else if (nearest == to_t) { text = "^ " + portal.label; ny = -1.0f; }
            else                      { text = "v " + portal.label; ny =  1.0f; }

            SDL_FPoint s = UiPoint(px, py);
            const SDL_FPoint size = ui.Measure(text, TextSize::Small);
            s.x = std::clamp(s.x - nx * 90.0f, size.x / 2.0f + 12.0f, ui.ViewWidth() - size.x / 2.0f - 12.0f);
            // A way out to the south is under the player's feet, which is
            // under the middle of the screen: over the stack at its foot, the
            // label sits on top of that rather than across it.
            const bool over_stack = fabsf(s.x - ui.ViewWidth() / 2.0f) < size.x / 2.0f + 8.0f + stack_half + 8.0f;
            const float lowest = over_stack ? std::min(ui.ViewHeight() - 60.0f, stack_y - size.y - 12.0f)
                                            : ui.ViewHeight() - 60.0f;
            s.y = std::clamp(s.y - ny * 70.0f, 40.0f, std::max(40.0f, lowest));

            ui.Fill({roundf(s.x - size.x / 2.0f - 8.0f), roundf(s.y - 4.0f), size.x + 16.0f, size.y + 8.0f},
                    {14, 11, 9, static_cast<Uint8>(170.0f * fade)});
            SDL_Color c = Palette::Highlight;
            c.a = static_cast<Uint8>(255.0f * fade);
            ui.TextShadowed(text, s.x, s.y, TextSize::Small, c, Align::Center);
        }
    }

    // --- zone banner -----------------------------------------------------------
    if (banner_active) {
        const float in  = std::clamp(banner_time / 0.6f, 0.0f, 1.0f);
        const float out = std::clamp((4.2f - banner_time) / 1.0f, 0.0f, 1.0f);
        const float a   = std::min(in, out);
        const float cx  = ui.ViewWidth() / 2.0f;
        const float y   = 96.0f - (1.0f - in) * 12.0f;

        const float tw = ui.Measure(banner_title, TextSize::Title).x;
        const float rule_y = roundf(y + ui.LineHeight(TextSize::Title) + 6.0f);

        // A dark band behind the words, feathered at both ends. Gold on the
        // foothills' sand and on sunlit grass was close to unreadable, and a
        // shadow alone did not carry a line of text that size.
        {
            const float sw = banner_subtitle.empty() ? 0.0f
                                                     : ui.Measure(banner_subtitle, TextSize::Body).x;
            const float core = std::max(tw, sw) + 60.0f;
            const float top = roundf(y - 12.0f);
            const float bottom = rule_y + (banner_subtitle.empty() ? 14.0f
                                                                    : 16.0f + ui.LineHeight(TextSize::Body));
            const Uint8 band = static_cast<Uint8>(130.0f * a);
            ui.Fill({roundf(cx - core / 2.0f), top, roundf(core), bottom - top}, {12, 10, 8, band});
            constexpr int FEATHER = 16;
            constexpr float STEP = 7.0f;
            for (int i = 0; i < FEATHER; ++i) {
                const Uint8 fa = static_cast<Uint8>(band * (1.0f - (i + 1.0f) / (FEATHER + 1.0f)));
                const float off = core / 2.0f + i * STEP;
                ui.Fill({roundf(cx - off - STEP), top, STEP, bottom - top}, {12, 10, 8, fa});
                ui.Fill({roundf(cx + off), top, STEP, bottom - top}, {12, 10, 8, fa});
            }
        }

        SDL_Color title = Palette::Highlight;
        title.a = static_cast<Uint8>(255.0f * a);
        ui.TextShadowed(banner_title, cx, y, TextSize::Title, title, Align::Center);

        const Uint8 ra = static_cast<Uint8>(170.0f * a);
        ui.Fill({roundf(cx - tw / 2.0f - 28.0f), rule_y, tw + 56.0f, 1.0f}, {242, 200, 96, ra});
        ui.Fill({roundf(cx - 5.0f), rule_y - 2.0f, 10.0f, 5.0f}, {242, 200, 96, static_cast<Uint8>(230.0f * a)});
        if (!banner_subtitle.empty()) {
            SDL_Color sub_c = Palette::Text;
            sub_c.a = static_cast<Uint8>(235.0f * a);
            ui.TextShadowed(banner_subtitle, cx, rule_y + 10.0f, TextSize::Body, sub_c, Align::Center);
        }
    }

    // Where the quest is: over the world and the arrival banner, which dimmed it,
    // and under the tracker and the bars.
    if (state == GameState::Play) DrawWaypoint(waypoint);

    // --- quest tracker -------------------------------------------------------
    vector<string> active = quests->Active();
    const string followed = quests->Followed();
    // The one being followed leads, and says where it is.
    {
        const auto it = std::find(active.begin(), active.end(), followed);
        if (it != active.end()) std::rotate(active.begin(), it, it + 1);
    }
    string where_line;
    if (settings.quest_waypoints && !followed.empty()) {
        if (waypoint.found && waypoint.here) {
            const float wx = waypoint.local_x - p.x, wy = waypoint.local_y - p.y;
            const int paces = static_cast<int>(Length(wx, wy) / 32.0f);
            static const char* kRose[8] = {"east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east"};
            const int point = ((static_cast<int>(roundf(atan2f(wy, wx) / 0.7853982f)) % 8) + 8) % 8;
            where_line = waypoint.what + (paces < 3 ? string(", right here") : ", " + std::to_string(paces) + " paces " + kRose[point]);
            if (!waypoint.hint.empty()) where_line = waypoint.hint;
        } else if (waypoint.found) {
            where_line = "in " + waypoint.place + "  -  " + (waypoint.via.empty() ? string("this way") : waypoint.via);
        } else if (!waypoint.hint.empty()) {
            where_line = waypoint.hint;
        }
    }
    // Sits below the minimap, and below however many toasts are stacked. Three
    // at most, and fewer where three would run off the bottom: half a split
    // screen, one above the other, in the smallest window has room under the
    // minimap for one -- and drew three, the last of them off its half.
    const float tracker_y = kHudRightTop + toasts.size() * 20.0f;
    const float tracker_room = ui.ViewHeight() - 8.0f - (tracker_y + 18.0f) - (where_line.empty() ? 0.0f : 18.0f);
    const size_t shown = std::min<size_t>({active.size(), size_t{3},
                                           static_cast<size_t>(std::max(0.0f, tracker_room) / 42.0f)});
    if (shown > 0) {
        const float right = ui.ViewWidth() - 18.0f;
        float y = tracker_y;

        // A dark backing sized to the text. The dim objective lines were
        // unreadable over the pale olive grass and the road, shadow or not.
        {
            float widest = ui.Measure("QUESTS", TextSize::Small).x;
            for (size_t i = 0; i < shown; ++i) {
                if (const QuestDef* d = quests->Definition(active[i])) {
                    widest = std::max(widest, ui.Measure(d->name, TextSize::Small).x);
                    widest = std::max(widest, ui.Measure(quests->CurrentObjectiveText(active[i]),
                                                         TextSize::Small).x);
                }
            }
            if (!where_line.empty()) widest = std::max(widest, ui.Measure(where_line, TextSize::Small).x);
            const SDL_FRect back = {right - widest - 10.0f, y - 6.0f, widest + 20.0f,
                                    22.0f + shown * 42.0f + 2.0f + (where_line.empty() ? 0.0f : 18.0f)};
            ui.Fill(back, {14, 11, 9, 150});
        }

        ui.TextShadowed("QUESTS", right, y, TextSize::Small, Palette::Highlight, Align::Right);
        y += 22.0f;

        // Show at most three so the tracker never crowds the view.
        for (size_t i = 0; i < shown; ++i) {
            const QuestDef* d = quests->Definition(active[i]);
            if (!d) continue;
            const bool leading = active[i] == followed && settings.quest_waypoints;
            ui.TextShadowed(d->name, right, y, TextSize::Small,
                            leading ? SDL_Color{255, 214, 96, 255} : Palette::Text, Align::Right);
            y += 18.0f;
            ui.TextShadowed(quests->CurrentObjectiveText(active[i]), right, y,
                            TextSize::Small, Palette::TextDim, Align::Right);
            if (leading && !where_line.empty()) {
                y += 18.0f;
                ui.TextShadowed(where_line, right, y, TextSize::Small, {226, 196, 120, 255}, Align::Right);
            }
            y += 24.0f;
        }
    }

    // What was last eaten, under the vitals and the clock, while it lasts: a
    // meal is worth knowing about, and worth knowing the end of. (It sat at a
    // fixed height once, which the clock's line has since grown down over.)
    if (const ItemDef* dish = p.Meal()) {
        const int left = static_cast<int>(p.MealLeft());
        char fed[96];
        SDL_snprintf(fed, sizeof(fed), "%s  %d:%02d", dish->name.c_str(), left / 60, left % 60);
        ui.TextShadowed(fed, 18.0f, under_clock, TextSize::Small, {186, 226, 150, 255});
    }
    // And under that, a ward drunk against something, with its time: what it
    // keeps off, by name, in that thing's own colour. A frost ward keeps off
    // two, drunk at the same moment, and says both on one line.
    {
        float ward_y = under_clock + (p.Meal() ? line_h + 2.0f : 0.0f);
        bool said[STATUS_COUNT] = {};
        for (int i = 0; i < STATUS_COUNT; ++i) {
            const float left = p.WardLeft(static_cast<Status>(i));
            if (left <= 0.0f || said[i]) continue;
            string names;
            for (int k = i; k < STATUS_COUNT; ++k) {
                const float other = p.WardLeft(static_cast<Status>(k));
                // One draught sets its statuses to the same moment; two drunk
                // a breath apart are two lines.
                if (said[k] || other <= 0.0f || fabsf(other - left) > 0.05f) continue;
                said[k] = true;
                const StatusDef* d = status_db.Get(static_cast<Status>(k));
                names += (names.empty() ? "" : ", ") + (d ? d->name : string(StatusId(static_cast<Status>(k))));
            }
            const StatusDef* first = status_db.Get(static_cast<Status>(i));
            const SDL_Color c = first ? first->color : SDL_Color{200, 220, 255, 255};
            const int secs = static_cast<int>(ceilf(left));
            char line[128];
            SDL_snprintf(line, sizeof(line), "Ward: %s  %d:%02d", names.c_str(), secs / 60, secs % 60);
            ui.TextShadowed(line, 18.0f, ward_y, TextSize::Small, c);
            ward_y += line_h + 2.0f;
        }
    }

    // --- controls hint -------------------------------------------------------
    if (!live) return;
    string spell_hint;
    if (p.Style() == AttackStyle::Magic)
        // An element's own staff has four spells on the keys the elements were
        // on; otherwise the keys are the elements, as many of them as this
        // character has anything on.
        spell_hint = string(input.ActiveDevice() == InputMode::Controller ? "RS " : "1-")
                   + (input.ActiveDevice() == InputMode::Controller ? ""
                      : p.StaffElement() != Element::None ? "4 "
                      : !world->KnownArcane(spells).empty() ? "6 "
                      : !KnownElectric().empty() ? "5 " : "4 ")
                   + (p.StaffElement() != Element::None ? "spell    " : "element    ");

    const string hint = spell_hint +
                        input.PromptFor(Action::LightAttack) + " attack    " +
                        input.PromptFor(Action::StrongAttack) + " heavy    " +
                        input.PromptFor(Action::Target) + " target    " +
                        // Only worth a word when there is a shield to raise.
                        (p.Shield() ? input.PromptFor(Action::Block) + " block    " : string()) +
                        input.PromptFor(Action::Sprint) + " sprint    " +
                        input.PromptFor(Action::Inventory) + " bag    " +
                        // Every panel is in the menu of menus, which says each one's own
                        // key beside it: naming them all here as well ran the line off a
                        // 1280 window at 125%. A pad's RB is the abilities' shift.
                        (input.ActiveDevice() == InputMode::Controller
                             ? input.PromptFor(Action::Ability) + " abilities    " : string()) +
                        input.PromptFor(Action::Menu) + " menus    " +
                        input.PromptFor(Action::Pause) + " pause";
    // Half a screen has no room for the line, and two players know the keys.
    // Its place is kept all the same, so the stack above it is where it was.
    if (!split_active) {
        ui.TextShadowed(hint, 18.0f, foot_y, TextSize::Small, Palette::TextDim);
        if (ui.Auditing()) ui.Claim("key hints", {18.0f, foot_y, ui.Measure(hint, TextSize::Small).x, line_h});
    }
}

void Game::DrawToasts() {
    // Under the minimap while a game is running; at the top on the menus,
    // where there is no minimap to clear.
    float y = InGameplayState() ? kHudRightTop : 18.0f;
    float right = ui.ViewWidth() - 18.0f;
    // With the skill tree open they are about what was just bought, and where
    // they usually go is where the tree says what a node does. Inside the
    // panel instead, in the gap under the description.
    if (state == GameState::SkillsPanel && skills_tab == TAB_TREE) {
        const AttackStyle mine = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        const float tree_w = 860.0f + 172.0f * (skill_trees.Tree(mine).BranchCount() - SkillTrees::BRANCHES);
        y = ui.ViewHeight() / 2.0f + 8.0f;
        right = ui.ViewWidth() / 2.0f + tree_w / 2.0f - 24.0f;
    }

    for (const Toast& t : toasts) {
        SDL_Color c = t.color;
        c.a = static_cast<Uint8>(255 * std::clamp(t.life / 0.6f, 0.0f, 1.0f));
        ui.TextShadowed(t.text, right, y, TextSize::Small, c, Align::Right);
        y += 20.0f;
    }
}
