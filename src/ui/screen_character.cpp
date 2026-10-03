// =============================================================================
//  The Game class's screens, continued: the character panel
//
//  The figure, in what it is wearing and holding, with the pieces round it the
//  way a paper doll has them -- head, amulet, bags and body down the left;
//  hands, legs, feet and ring down the right; the two hands under the figure --
//  and beside it the combat level, the attributes and everything running on
//  the character. It shows; the bag is where things are put on and taken off,
//  and it is one key away.
//
//  One class in several files, cut along the banners screens.cpp always had:
//  screens.cpp keeps the menus; the rest is screen_hud, screen_inventory,
//  screen_skills, screen_character, screen_journal, screen_talk and
//  screen_trade. What two of them share is in screens_shared.h.
// =============================================================================
#include "../entity/attributes.h"
#include "../game.h"
#include "screens_shared.h"

namespace {

// Where the pieces stand: two sides of five, each side its column of four and
// then the hand under the figure on that side. Up and down go round a side;
// left and right go across to the other, level with where they were.
constexpr int kSideCount = 2, kPerSide = 5, kPieces = kSideCount * kPerSide;
// Not a slot: the bags worn, which only ever grow. A bag is worn on the back,
// which is where a paper doll has its back.
constexpr int PIECE_BAGS = SLOT_COUNT;
constexpr int kPieceSlot[kPieces] = {
    SLOT_HEAD,  SLOT_AMULET, PIECE_BAGS, SLOT_BODY, SLOT_WEAPON,
    SLOT_HANDS, SLOT_LEGS,   SLOT_FEET,  SLOT_RING, SLOT_SHIELD,
};

// The figure turns about as a figure does, a quarter at a time: front, its
// right side, its back, its left.
constexpr Facing kTurn[4] = {FACE_DOWN, FACE_LEFT, FACE_UP, FACE_RIGHT};

const char* PieceName(int slot) {
    switch (slot) {
        case SLOT_WEAPON: return "Weapon";
        case SLOT_SHIELD: return "Off hand";
        case SLOT_HEAD:   return "Head";
        case SLOT_BODY:   return "Body";
        case SLOT_HANDS:  return "Hands";
        case SLOT_LEGS:   return "Legs";
        case SLOT_FEET:   return "Feet";
        case SLOT_AMULET: return "Amulet";
        case SLOT_RING:   return "Ring";
        default:          return "Bags";
    }
}

// What stands faintly in an empty square, so it says what goes there: an iron
// piece of the kind, the plainest amulet and ring, the first bag.
string GhostOf(const ItemDatabase& items, int slot) {
    const auto piece = [&](const char* kind) { return items.TierPiece("iron", kind); };
    switch (slot) {
        case SLOT_WEAPON: return piece("sword");
        case SLOT_SHIELD: return piece("shield");
        case SLOT_HEAD:   return piece("helm");
        case SLOT_BODY:   return piece("body");
        case SLOT_HANDS:  return piece("gauntlets");
        case SLOT_LEGS:   return piece("legs");
        case SLOT_FEET:   return piece("boots");
        case SLOT_AMULET: return "bone_amulet";
        case SLOT_RING:   return "copper_ring";
        default:          return "bag_satchel";
    }
}

SDL_Color AffinityColour(AttackStyle style) {
    return style == AttackStyle::Ranged ? SDL_Color{150, 210, 130, 255}
         : style == AttackStyle::Magic  ? SDL_Color{170, 150, 240, 255}
                                        : SDL_Color{236, 176, 96, 255};
}

// What a line in the boons says it is, in its colour: a boss's gold, the
// totem's green, a meal's warmth, a draught's, a ward's and an ability's blues,
// a charm's violet, and red for whatever a monster left.
SDL_Color BoonColour(BoonLine::Kind k) {
    switch (k) {
        case BoonLine::Kind::Boon:       return {242, 200, 96, 255};
        case BoonLine::Kind::Totem:      return {140, 214, 170, 255};
        case BoonLine::Kind::Meal:       return {236, 168, 110, 255};
        case BoonLine::Kind::Draught:    return {126, 196, 122, 255};
        case BoonLine::Kind::Ward:       return {150, 196, 240, 255};
        case BoonLine::Kind::Ability:    return {130, 190, 240, 255};
        case BoonLine::Kind::Charm:      return {186, 150, 250, 255};
        case BoonLine::Kind::Passive:    return {230, 214, 150, 255};
        default:                         return {235, 120, 110, 255};
    }
}

SDL_FColor Float(SDL_Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}; }

// A box that darkens from its top to its foot, or lightens: the stage.
void Gradient(SDL_Renderer* r, const SDL_FRect& b, SDL_Color top, SDL_Color foot) {
    const SDL_FColor t = Float(top), f = Float(foot);
    const SDL_Vertex v[4] = {{{b.x, b.y}, t, {0, 0}}, {{b.x + b.w, b.y}, t, {0, 0}},
                             {{b.x + b.w, b.y + b.h}, f, {0, 0}}, {{b.x, b.y + b.h}, f, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// A soft pool of light: `c` in the middle, gone at the rim.
void Glow(SDL_Renderer* r, float cx, float cy, float rx, float ry, SDL_Color c) {
    constexpr int N = 40;
    SDL_Vertex v[N + 1];
    int idx[N * 3];
    const SDL_FColor mid = Float(c);
    const SDL_FColor rim = {mid.r, mid.g, mid.b, 0.0f};
    v[0] = {{cx, cy}, mid, {0, 0}};
    for (int i = 0; i < N; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / N;
        v[i + 1] = {{cx + rx * cosf(a), cy + ry * sinf(a)}, rim, {0, 0}};
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = (i + 1) % N + 1;
    }
    SDL_RenderGeometry(r, nullptr, v, N + 1, idx, N * 3);
}

// A heading across a column, the way the panel's sections are marked off.
void SectionBar(UI& ui, const SDL_FRect& r, const string& title) {
    ui.Fill(r, {46, 36, 26, 240});
    ui.Fill({r.x, r.y + r.h - 1.0f, r.w, 1.0f}, Palette::Border);
    ui.Outline(r, Palette::BorderDim, 1.0f);
    ui.Text(title, r.x + r.w / 2.0f, r.y + (r.h - ui.LineHeight(TextSize::Body)) / 2.0f, TextSize::Body,
            Palette::Text, Align::Center);
}

// A line cut to the width it has, rather than run off the panel.
string Fit(UI& ui, const string& t, float w) { return ui.Fit(t, w, TextSize::Small); }

} // namespace

// =============================================================================
//  Update
// =============================================================================

void Game::UpdateCharacterPanel() {
    // Back, pause or its own key again: back to the game.
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause) ||
        (state_time > 0.0f && input.Pressed(Action::Character))) {
        SetState(GameState::Play);
        return;
    }
    const int slot = kPieceSlot[std::clamp(sheet_piece, 0, kPieces - 1)];
    // The bag, where things are put on and taken off, open on the piece the
    // cursor was on.
    if (input.Pressed(Action::Inventory)) {
        inventory_on_equipment = slot != PIECE_BAGS;
        if (inventory_on_equipment) equipment_cursor = slot;
        SetState(GameState::Inventory);
        return;
    }

    const int was = sheet_piece;
    int side = std::clamp(sheet_piece, 0, kPieces - 1) / kPerSide, at = sheet_piece % kPerSide;
    if (input.MenuUp())   at = (at + kPerSide - 1) % kPerSide;
    if (input.MenuDown()) at = (at + 1) % kPerSide;
    if (input.MenuLeft() || input.MenuRight()) side = (side + 1) % kSideCount;
    sheet_piece = side * kPerSide + at;
    if (was != sheet_piece) Audio::Play(Sfx::UiMove);

    // Turning the figure round: the spell keys, the right stick on a pad.
    const int step = input.Pressed(Action::SpellNext) ? 1 : input.Pressed(Action::SpellPrev) ? 3 : 0;
    if (step) {
        int i = 0;
        while (i < 4 && kTurn[i] != sheet_facing) ++i;
        sheet_facing = kTurn[(i % 4 + step) % 4];
        Audio::Play(Sfx::UiMove);
    }
}

// =============================================================================
//  Draw
// =============================================================================

void Game::DrawCharacterPanel() {
    ui.Dim(0.5f);
    const Player& p = world->player;
    const SDL_FRect panel = CenteredPanel(ui, std::min(940.0f, ui.ViewWidth() - 16.0f),
                                          std::min(600.0f, ui.ViewHeight() - 16.0f));
    ui.Panel(panel);

    // --- who: the combat level and the one they are, as a crest and a title -----------
    const AttackStyle mine = p.Affinity();
    const SDL_Color own = AffinityColour(mine);
    string who = "Adventurer";
    for (int i = 0; i < kCharacterCount; ++i)
        if (p.sprite_id == kCharacterIds[i]) who = kCharacterLabels[i];
    {
        // The crest: what is in their hand, or what they set out with.
        const SDL_FRect crest = {panel.x + 14.0f, panel.y + 8.0f, 40.0f, 40.0f};
        ui.Fill(crest, {20, 16, 13, 245});
        ui.Outline(crest, own, 2.0f);
        const vector<string> kit = Player::StartingKit(p.sprite_id);
        const ItemDef* emblem = p.equipment.Weapon();
        if (!emblem && !kit.empty()) emblem = items.Get(kit.front());
        if (emblem && !emblem->icon.empty())
            if (SDL_Texture* tex = textures->Get(emblem->icon)) {
                const SDL_FRect in = {crest.x + 4.0f, crest.y + 4.0f, crest.w - 8.0f, crest.h - 8.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &in);
            }
        const string level = "Combat " + std::to_string(p.skills.CombatLevel()) + "  ";
        const float lw = ui.Measure(level, TextSize::Large).x, ww = ui.Measure(who, TextSize::Large).x;
        const float tx = panel.x + (panel.w - lw - ww) / 2.0f;
        ui.Text(level, tx, panel.y + 14.0f, TextSize::Large, Palette::Highlight);
        ui.Text(who, tx + lw, panel.y + 14.0f, TextSize::Large, own);
        ui.Text(string("of ") + Player::AffinityName(mine), panel.x + panel.w - 18.0f, panel.y + 22.0f,
                TextSize::Small, Palette::TextDim, Align::Right);
    }

    const float top = panel.y + 58.0f;
    const float foot = panel.y + panel.h - 38.0f;

    // --- the figure and its pieces -------------------------------------------------------
    const float doll_w = std::min(540.0f, floorf(panel.w * 0.58f));
    const SDL_FRect doll = {panel.x + 16.0f, top, doll_w, foot - top};
    const float tile = std::clamp(floorf((doll.h - 20.0f) / 7.0f), 42.0f, 56.0f);
    const float stage_h = doll.h - tile - 10.0f;
    const float col_l = doll.x + 6.0f, col_r = doll.x + doll.w - 6.0f - tile;
    const SDL_FRect stage = {col_l + tile + 12.0f, doll.y, col_r - 12.0f - (col_l + tile + 12.0f), stage_h};

    // The stage: dark stone, lighter toward the floor, the figure lit from
    // in front, and a pool of light where it stands.
    Gradient(renderer, stage, {20, 17, 19, 245}, {50, 42, 35, 245});
    ui.Outline(stage, Palette::BorderDim, 1.0f);
    const float floor_y = stage.y + stage.h * 0.88f;
    Glow(renderer, stage.x + stage.w / 2.0f, stage.y + stage.h * 0.50f, stage.w * 0.48f, stage.h * 0.48f, {150, 130, 108, 58});
    Glow(renderer, stage.x + stage.w / 2.0f, floor_y, stage.w * 0.40f, stage.h * 0.06f, {190, 164, 120, 110});

    // The figure, drawn the way the world draws it -- the same layers, the same
    // metal on each piece -- at a whole multiple of its art so no pixel is
    // bigger than its neighbour, and as big as the stage will take: the rig
    // stands about FIGURE_PIXELS tall in its 64-pixel frame, and the frame's
    // empty sky is cut off at the stage's edge.
    constexpr float FIGURE_PIXELS = 28.0f;
    if (const SpriteDef* def = sprites.Get(p.sprite_id)) {
        Sprite fig;
        fig.SetDef(def);
        fig.style = p.BuildLayerStyle(&items);
        fig.facing = sheet_facing;
        fig.Play("idle");
        // The panel's own clock: a fresh sprite is stepped to it from nothing
        // every frame, and the time the program has been up would be a longer
        // walk every hour.
        fig.Update(state_time);
        const float frame = static_cast<float>(std::max(1, fig.FrameSize()));
        // A little over half the stage's height: bigger, and the pixels are
        // bricks; smaller, and the stage is sky.
        const float scale = std::clamp(floorf(stage.h * 0.56f / FIGURE_PIXELS), 3.0f, 10.0f);
        const float side = frame * scale;
        // The feet a few pixels above the frame's anchor, which is the foot of
        // the shadow under them.
        const SDL_FRect dst = {floorf(stage.x + (stage.w - side) / 2.0f), floorf(floor_y - (def->anchor_y - 4.0f) * scale),
                               side, side};
        const SDL_Rect clip = {static_cast<int>(stage.x) + 1, static_cast<int>(stage.y) + 1,
                               static_cast<int>(stage.w) - 2, static_cast<int>(stage.h) - 2};
        SDL_SetRenderClipRect(renderer, &clip);
        fig.DrawAt(renderer, *textures, dst);
        SDL_SetRenderClipRect(renderer, nullptr);
    }
    ui.Text(input.PromptFor(Action::SpellPrev) + " " + input.PromptFor(Action::SpellNext) + " turn",
            stage.x + stage.w - 8.0f, stage.y + stage.h - 22.0f, TextSize::Small, {150, 138, 118, 255}, Align::Right);

    // The pieces: a column of four each side, spaced down the stage, and the
    // two hands under it.
    const float pitch = std::min(tile + 26.0f, (stage.h - tile) / 3.0f);
    const float col_top = stage.y + (stage.h - (3.0f * pitch + tile)) / 2.0f;
    const float mid = stage.x + stage.w / 2.0f;
    SDL_FRect rects[kPieces];
    for (int side = 0; side < kSideCount; ++side)
        for (int at = 0; at < kPerSide; ++at) {
            SDL_FRect& r = rects[side * kPerSide + at];
            if (at < 4) r = {side == 0 ? col_l : col_r, col_top + at * pitch, tile, tile};
            else        r = {side == 0 ? mid - tile - 8.0f : mid + 8.0f, stage.y + stage.h + 10.0f, tile, tile};
        }

    const float pulse = 0.5f + 0.5f * sinf(state_time * 4.0f);
    const ItemDef* in_hand = p.equipment.Weapon();
    const bool both_hands = in_hand && (in_hand->two_handed || in_hand->kind == WeaponKind::Bow);
    for (int i = 0; i < kPieces; ++i) {
        const SDL_FRect& r = rects[i];
        const int slot = kPieceSlot[i];
        const bool here = i == sheet_piece;
        // The bags are the last one put on; every other square what is in it.
        string id = slot == PIECE_BAGS ? (p.Bags().empty() ? string() : p.Bags().back())
                                       : p.equipment.InSlot(slot);
        const ItemDef* d = id.empty() ? nullptr : items.Get(id);
        // Its frame in the colour of its metal, the way a better piece is
        // told from a worse one at a glance; a charmed piece in violet.
        SDL_Color edge = Palette::BorderDim;
        if (d) {
            if (const TierDef* t = d->tier.empty() ? nullptr : items.Tier(d->tier)) edge = t->colour;
            else edge = Palette::Border;
            if (!d->enchant.empty()) edge = {186, 150, 250, 255};
        }
        ui.Fill({r.x + 2.0f, r.y + 3.0f, r.w, r.h}, Palette::Shadow);
        ui.Fill(r, {22, 18, 15, 245});
        ui.Outline(r, edge, d ? 2.0f : 1.0f);
        const SDL_FRect in = {r.x + 5.0f, r.y + 5.0f, r.w - 10.0f, r.h - 10.0f};
        if (d) {
            SDL_Texture* tex = d->icon.empty() ? nullptr : textures->Get(d->icon);
            if (tex) SDL_RenderTexture(renderer, tex, nullptr, &in);
            else     DrawItemPlaceholder(ui, d, id, in);
        } else {
            // Empty: what goes there, faint -- or, in the off hand of someone
            // holding a bow or a two-handed blade, that weapon's shadow.
            const ItemDef* ghost = (slot == SLOT_SHIELD && both_hands) ? in_hand : items.Get(GhostOf(items, slot));
            if (SDL_Texture* tex = (ghost && !ghost->icon.empty()) ? textures->Get(ghost->icon) : nullptr) {
                const bool held = ghost == in_hand;
                SDL_SetTextureColorMod(tex, held ? 170 : 120, held ? 160 : 112, held ? 150 : 104);
                SDL_SetTextureAlphaMod(tex, held ? 130 : 105);
                SDL_RenderTexture(renderer, tex, nullptr, &in);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
            }
        }
        if (slot == PIECE_BAGS && !p.Bags().empty())
            ui.TextShadowed(std::to_string(p.Bags().size()), r.x + r.w - 4.0f, r.y + r.h - 18.0f, TextSize::Small,
                            Palette::Highlight, Align::Right);
        if (here) {
            const SDL_Color lit = {static_cast<Uint8>(200 + 55 * pulse), static_cast<Uint8>(170 + 44 * pulse), 96, 255};
            ui.Outline({r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f}, lit, 2.0f);
        }
    }

    // --- the right: the combat level, the attributes, the boons ---------------------------
    const float rx = doll.x + doll.w + 16.0f, rw = panel.x + panel.w - 16.0f - rx;
    float y = top;
    {
        // The headline number, and how far to the next -- which is when
        // Defence moves: twice for the hero, once for the others.
        const SDL_FRect box = {rx, y, rw, 70.0f};
        ui.Fill(box, {30, 24, 20, 235});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        SectionBar(ui, {box.x, box.y, box.w, 24.0f}, "Combat Level");
        ui.Text(std::to_string(p.skills.CombatLevel()), box.x + box.w / 2.0f, box.y + 27.0f, TextSize::Title,
                Palette::Highlight, Align::Center);
        const double exact = p.skills.CombatLevelExact();
        const float frac = static_cast<float>(exact - floor(exact));
        ui.Fill({box.x + 10.0f, box.y + box.h - 8.0f, box.w - 20.0f, 3.0f}, {24, 30, 24, 230});
        ui.Fill({box.x + 10.0f, box.y + box.h - 8.0f, (box.w - 20.0f) * frac, 3.0f}, Palette::Xp);
        const bool twice = Player::DefencePerLevel(mine) == 2;
        ui.Text("Defence", box.x + box.w - 10.0f, box.y + 30.0f, TextSize::Small, Palette::TextDim, Align::Right);
        ui.Text(twice ? "twice this" : "the same", box.x + box.w - 10.0f, box.y + 46.0f, TextSize::Small,
                Palette::Text, Align::Right);
        y += box.h + 8.0f;
    }

    // Two columns of seven: what keeps them standing, what they fight with.
    {
        const vector<AttributeLine> lines = CharacterAttributes(p);
        const float row_h = 18.0f;
        const SDL_FRect bar = {rx, y, rw, 24.0f};
        SectionBar(ui, bar, "Attributes");
        y += bar.h + 4.0f;
        const float cw = (rw - 8.0f) / 2.0f;
        for (size_t i = 0; i < lines.size(); ++i) {
            const AttributeLine& a = lines[i];
            const int col = static_cast<int>(i) / ATTRIBUTE_ROWS, row = static_cast<int>(i) % ATTRIBUTE_ROWS;
            const float cx = rx + col * (cw + 8.0f), cy = y + row * row_h;
            if (row % 2 == 0) ui.Fill({cx, cy - 1.0f, cw, row_h}, {255, 240, 210, 10});
            ui.Text(a.label, cx + 6.0f, cy + 1.0f, TextSize::Small, Palette::TextDim);
            const SDL_Color tone = a.tone > 0 ? Palette::Xp : a.tone < 0 ? SDL_Color{235, 150, 120, 255} : Palette::Text;
            float right = cx + cw - 6.0f;
            if (!a.extra.empty()) {
                ui.Text(a.extra, right, cy + 1.0f, TextSize::Small, {150, 190, 140, 255}, Align::Right);
                right -= std::max(30.0f, ui.Measure(a.extra, TextSize::Small).x) + 8.0f;
            }
            ui.Text(a.value, right, cy + 1.0f, TextSize::Small, tone, Align::Right);
        }
        y += ATTRIBUTE_ROWS * row_h + 8.0f;
    }

    // Everything running on them, with how long it has.
    {
        const SDL_FRect bar = {rx, y, rw, 24.0f};
        const vector<BoonLine> boons = CharacterBoons(p, &status_db, [&] {
            const double h = world->clock.Hours();
            return h < WorldClock::NIGHT_END ? WorldClock::NIGHT_END - h : 24.0 - h + WorldClock::NIGHT_END;
        }());
        SectionBar(ui, bar, boons.empty() ? string("Boons") : "Boons (" + std::to_string(boons.size()) + ")");
        y += bar.h + 6.0f;
        const float room = foot - y;
        if (boons.empty()) {
            ui.Text("Nothing running.", rx + rw / 2.0f, y + 6.0f, TextSize::Body, Palette::TextDim, Align::Center);
            ui.TextWrapped("A boss's first fall leaves a boon for a day. Your totem at home, a good meal, a draught "
                           "and a ward do the rest.",
                           rx + 10.0f, y + 32.0f, rw - 20.0f, TextSize::Small, Palette::TextDim);
        } else {
            // The name, and what it does under it, whole -- wrapped onto as
            // many lines as it takes. Where they do not all fit that way, the
            // longest are cut to a line, one at a time, until they do; where
            // even a line each is too many, the names alone, and a count of
            // the rest.
            const float name_h = 16.0f, gap = 3.0f, name_only_h = 20.0f;
            const float detail_w = rw - 24.0f;
            const float line_h = ui.LineHeight(TextSize::Small) + 2.0f;   // a wrapped line, as TextWrapped sets it
            vector<int> lines(boons.size(), 0);
            vector<bool> cut(boons.size(), false);
            for (size_t i = 0; i < boons.size(); ++i)
                if (!boons[i].detail.empty())
                    lines[i] = std::max(1, static_cast<int>(std::lround(ui.WrappedHeight(boons[i].detail, detail_w, TextSize::Small) / line_h)));
            const auto row_of = [&](size_t i) { return name_h + (cut[i] ? std::min(lines[i], 1) : lines[i]) * line_h + gap; };
            const auto total = [&] {
                float t = 0.0f;
                for (size_t i = 0; i < boons.size(); ++i) t += row_of(i);
                return t;
            };
            // Each whole; then, while they do not fit, the longest cut to a line.
            while (total() > room) {
                int longest = -1;
                for (size_t i = 0; i < boons.size(); ++i)
                    if (!cut[i] && lines[i] > 1 && (longest < 0 || lines[i] > lines[longest])) longest = static_cast<int>(i);
                if (longest < 0) break;
                cut[longest] = true;
            }
            const bool names_only = total() > room;
            const int fits = names_only ? std::max(1, static_cast<int>(room / name_only_h)) : static_cast<int>(boons.size());
            const int shown = static_cast<int>(boons.size()) <= fits ? static_cast<int>(boons.size()) : fits - 1;
            float by = y;
            for (int i = 0; i < shown; ++i) {
                const BoonLine& b = boons[i];
                const SDL_Color c = BoonColour(b.kind);
                ui.Fill({rx + 4.0f, by + 5.0f, 7.0f, 7.0f}, c);
                const float left_w = b.left.empty() ? 0.0f : ui.Measure(b.left, TextSize::Small).x + 12.0f;
                ui.Text(Fit(ui, b.name, rw - 24.0f - left_w), rx + 18.0f, by, TextSize::Small,
                        b.kind == BoonLine::Kind::Affliction ? c : Palette::Text);
                if (!b.left.empty())
                    ui.Text(b.left, rx + rw - 6.0f, by, TextSize::Small, {150, 200, 230, 255}, Align::Right);
                if (!names_only && !b.detail.empty()) {
                    if (cut[i]) ui.Text(Fit(ui, b.detail, detail_w), rx + 18.0f, by + name_h, TextSize::Small, Palette::TextDim);
                    else        ui.TextWrapped(b.detail, rx + 18.0f, by + name_h, detail_w, TextSize::Small, Palette::TextDim);
                }
                by += names_only ? name_only_h : row_of(static_cast<size_t>(i));
            }
            if (shown < static_cast<int>(boons.size()))
                ui.Text("and " + std::to_string(boons.size() - shown) + " more", rx + 18.0f, by,
                        TextSize::Small, Palette::TextDim);
        }
    }

    // --- how to work it --------------------------------------------------------------------
    ui.Text("arrows choose a piece     " + input.PromptFor(Action::Inventory) + " the bag, to change it     " +
                input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 26.0f, TextSize::Small, Palette::TextDim, Align::Center);

    // The card last, over everything it might cover.
    DrawPieceCard(sheet_piece, rects[std::clamp(sheet_piece, 0, kPieces - 1)], stage);
}

// What is on the piece the cursor has, beside it and over the stage: its name
// in its metal, where it goes and what it asks, and every number it carries.
void Game::DrawPieceCard(int piece, const SDL_FRect& tile, const SDL_FRect& stage) {
    const Player& p = world->player;
    piece = std::clamp(piece, 0, kPieces - 1);
    const int slot = kPieceSlot[piece];
    const float pad = 10.0f, row_h = 16.0f;

    string title, sub;
    SDL_Color title_c = Palette::Highlight;
    vector<ItemStat> rows;
    vector<string> notes;           // said under the numbers, wrapped
    SDL_Color note_c = Palette::Highlight;
    if (slot == PIECE_BAGS) {
        title = "Bags";
        const int worn = static_cast<int>(p.Bags().size());
        sub = std::to_string(worn) + " of 4 worn: " + std::to_string(p.BagSlots()) + " slots";
        for (const string& id : p.Bags())
            if (const ItemDef* d = items.Get(id)) rows.push_back({d->name, "+" + std::to_string(d->bag_slots), "", 0});
        if (worn < 4) notes.push_back("Each one adds a row. They are made at a tanning rack, or found.");
        note_c = Palette::TextDim;
    } else if (const ItemDef* d = items.Get(p.equipment.InSlot(slot))) {
        title = d->name;
        if (const TierDef* t = d->tier.empty() ? nullptr : items.Tier(d->tier)) title_c = t->colour;
        // Where it goes, its tier, and what it asks.
        sub =(slot == SLOT_SHIELD && d->slot == SLOT_WEAPON) ? string("Off hand") : string(PieceName(slot));
        if (const TierDef* t = d->tier.empty() ? nullptr : items.Tier(d->tier)) sub += ", " + t->name + " tier";
        for (const auto& rq : d->requirements)
            sub += ", " + string(SkillName(rq.first)) + " " + std::to_string(rq.second);
        rows = ItemStatLines(*d, nullptr, false);
        // What it does beyond its numbers -- a charm's line is written into its
        // twin's, after whatever the piece already said.
        if (!d->passive_text.empty()) notes.push_back(d->passive_text);
    } else {
        title = PieceName(slot);
        title_c = Palette::Text;
        const ItemDef* w = p.equipment.Weapon();
        if (slot == SLOT_SHIELD && w && (w->two_handed || w->kind == WeaponKind::Bow)) {
            sub = "Both hands are on the " + w->name + ".";
        } else {
            sub = "Nothing worn.";
            notes.push_back("Put something on from the bag: " + input.PromptFor(Action::Inventory) + ".");
        }
        note_c = Palette::TextDim;
    }

    // As wide as its longest line wants, within reason.
    float label_w = 0.0f, value_w = 0.0f;
    for (const ItemStat& r : rows) {
        label_w = std::max(label_w, ui.Measure(r.label, TextSize::Small).x);
        value_w = std::max(value_w, ui.Measure(r.value, TextSize::Small).x);
    }
    float w = std::max({ui.Measure(title, TextSize::Body).x, ui.Measure(sub, TextSize::Small).x,
                        label_w + 24.0f + value_w, 180.0f});
    // No wider than the stage has room for beside a square: a name longer than
    // that -- "Orichalcum Gauntlets of the Hawk's Eye I" -- takes two lines.
    w = std::min(w, std::min(320.0f, stage.w - tile.w - 24.0f)) + pad * 2.0f;
    const float inner = w - pad * 2.0f;
    const float title_h = ui.WrappedHeight(title, inner, TextSize::Body);
    float notes_h = 0.0f;
    for (const string& n : notes) notes_h += ui.WrappedHeight(n, inner, TextSize::Small) + 4.0f;
    const float h = pad * 2.0f + title_h + 2.0f + ui.WrappedHeight(sub, inner, TextSize::Small) + 6.0f +
                    rows.size() * row_h + (notes.empty() ? 0.0f : 4.0f + notes_h);

    // Beside the square, over the stage: to the right of the left column, to
    // the left of the right one, above the two hands.
    SDL_FRect card = {0, 0, w, h};
    const int side = piece / kPerSide, at = piece % kPerSide;
    if (at == 4) {
        card.x = tile.x + tile.w / 2.0f - w / 2.0f;
        card.y = tile.y - h - 10.0f;
    } else {
        card.x = side == 0 ? tile.x + tile.w + 12.0f : tile.x - w - 12.0f;
        card.y = tile.y - 4.0f;
    }
    card.x = std::clamp(card.x, stage.x + 4.0f, std::max(stage.x + 4.0f, stage.x + stage.w - w - 4.0f));
    card.y = std::clamp(card.y, 8.0f, std::max(8.0f, ui.ViewHeight() - h - 8.0f));

    ui.Panel(card);
    float y = card.y + pad;
    y += ui.TextWrapped(title, card.x + pad, y, inner, TextSize::Body, title_c) + 2.0f;
    y += ui.TextWrapped(sub, card.x + pad, y, inner, TextSize::Small, Palette::TextDim) + 6.0f;
    for (const ItemStat& r : rows) {
        ui.Text(r.label, card.x + pad, y, TextSize::Small, Palette::Text);
        ui.Text(r.value, card.x + w - pad, y, TextSize::Small, Palette::Xp, Align::Right);
        y += row_h;
    }
    if (!notes.empty()) y += 4.0f;
    for (const string& n : notes)
        y += ui.TextWrapped(n, card.x + pad, y, inner, TextSize::Small, note_c) + 4.0f;
}
