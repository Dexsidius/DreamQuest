// =============================================================================
//  The Game class's screens, continued: the bag, the storage chest, and what an item is worth
//
//  One class in several files, cut along the banners screens.cpp always had:
//  screens.cpp keeps the menus; the rest is screen_hud, screen_inventory,
//  screen_skills, screen_journal, screen_talk and screen_trade. What two of
//  them share is in screens_shared.h.
// =============================================================================
#include "../systems/gathering.h"
#include "../game.h"
#include "screens_shared.h"

namespace {

// Stand-in tile for an item whose icon art has not been imported: a coloured
// swatch keyed off the item id, with its initial on top. Keeps the inventory
// readable instead of showing empty squares.
void DrawItemPlaceholder(UI& ui, const ItemDef* def, const string& id,
                         const SDL_FRect& r) {
    size_t hash = 0;
    for (char c : id) hash = hash * 31 + static_cast<unsigned char>(c);
    const SDL_Color swatch = {static_cast<Uint8>(70 + (hash % 90)),
                              static_cast<Uint8>(58 + ((hash / 7) % 80)),
                              static_cast<Uint8>(46 + ((hash / 13) % 70)), 255};

    ui.Fill(r, swatch);
    ui.Outline(r, {20, 16, 12, 200}, 1.0f);

    const string label = def ? def->name : id;
    const string letter = label.empty() ? string("?") : label.substr(0, 1);
    ui.Text(letter, r.x + r.w / 2.0f,
            r.y + (r.h - ui.LineHeight(TextSize::Small)) / 2.0f,
            TextSize::Small, Palette::Text, Align::Center);
}

// The tidy-up, as a button beside a panel's title, with the key it is on.
void ReorganizeButton(UI& ui, const string& label, float x, float y) {
    const float w = ui.Measure(label, TextSize::Small).x + 20.0f;
    const SDL_FRect b = {x, y, w, 24.0f};
    ui.Fill(b, {58, 46, 28, 235});
    ui.Outline(b, Palette::Border, 1.0f);
    ui.Text(label, b.x + 10.0f, b.y + 4.0f, TextSize::Small, Palette::Text);
}

// A quest's things stay in the bag while they are wanted. A thing found that
// started something is a keepsake once that is over, and goes like anything.
bool MustKeep(const ItemDef* d, const QuestLog& quests) {
    return d && d->keep && !(!d->starts_quest.empty() && quests.IsComplete(d->starts_quest));
}

} // namespace

// =============================================================================
//  Inventory
// =============================================================================

void Game::UpdateInventory() {
    Player& p = world->player;
    constexpr int COLS = 7;
    const int slots = p.inventory.SlotCount();
    inventory_cursor = std::clamp(inventory_cursor, 0, slots - 1);
    if (state_time <= 0.0f) { drop_armed = -1; inventory_held = -1; }
    if (inventory_held >= slots) inventory_held = -1;

    // Tidy the lot: every stack joined, everything by what it is, the gaps at
    // the end. The button that steps through the elements, which in a bag has
    // nothing to step through.
    if (input.Pressed(Action::CycleSpell)) {
        p.inventory.Reorganize();
        inventory_held = -1;
        drop_armed = -1;
        inventory_cursor = 0;
        PushToast("Bag reorganized.", Palette::Xp);
        Audio::Play(Sfx::Equip, 0.6f);
        return;
    }
    // Put back where it was, rather than the bag closing with it in hand.
    if (inventory_held >= 0 && input.Pressed(Action::Back)) {
        inventory_held = -1;
        Audio::Play(Sfx::UiMove);
        return;
    }

    if (inventory_on_equipment) {
        MoveCursor(equipment_cursor, SLOT_COUNT);
        if (input.MenuLeft()) inventory_on_equipment = false;

        if (input.Pressed(Action::Confirm)) {
            if (p.UnequipSlot(equipment_cursor)) { PushToast("Unequipped.", Palette::TextDim); Audio::Play(Sfx::Equip, 0.7f); }
            else PushToast("Nothing to remove, or your pack is full.", Palette::TextDim);
        }
    } else {
        const int grid_before = inventory_cursor;
        if (input.MenuRight()) {
            if (inventory_cursor % COLS == COLS - 1) inventory_on_equipment = true;
            else inventory_cursor = std::min(inventory_cursor + 1, slots - 1);
        }
        if (input.MenuLeft())  inventory_cursor = std::max(inventory_cursor - 1, 0);
        if (input.MenuDown())  inventory_cursor = std::min(inventory_cursor + COLS, slots - 1);
        if (input.MenuUp())    inventory_cursor = std::max(inventory_cursor - COLS, 0);
        if (inventory_cursor != grid_before || inventory_on_equipment) Audio::Play(Sfx::UiMove);
        if (inventory_cursor != grid_before) drop_armed = -1;

        // Lift what is under the cursor, and put it down again somewhere else:
        // the two change places, or join if they are the same. While something
        // is lifted, confirming puts it down.
        const bool place = inventory_held >= 0 && input.Pressed(Action::Confirm);
        if (input.Pressed(Action::Ability) || place) {
            if (inventory_held < 0) {
                if (!p.inventory.Slot(inventory_cursor).Empty()) {
                    inventory_held = inventory_cursor;
                    Audio::Play(Sfx::UiMove);
                }
            } else {
                if (inventory_held != inventory_cursor) {
                    p.inventory.Move(inventory_held, inventory_cursor);
                    Audio::Play(Sfx::Pickup, 0.6f);
                }
                inventory_held = -1;
                drop_armed = -1;
            }
            return;
        }

        if (input.Pressed(Action::Confirm)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            if (!s.Empty()) {
                const ItemDef* def = items.Get(s.id);
                if (def && def->consumable) {
                    const bool potion = !def->boosts.empty() || def->mana > 0 || def->stamina ||
                                        std::find(def->tags.begin(), def->tags.end(), "potion") != def->tags.end();
                    string why;
                    if (p.Consume(inventory_cursor, why)) {
                        PushToast((potion ? "You drink the " : "You eat the ") + def->name + ".", Palette::Xp);
                        Audio::Play(Sfx::Eat);
                    } else {
                        PushToast(why, Palette::TextDim);
                    }
                } else if (def && !def->learn.empty()) {
                    // A recipe scroll: a brew, or -- when it reads
                    // "enchant:<id>" -- a charm for the enchanting table.
                    const bool charm = def->learn.rfind("enchant:", 0) == 0;
                    const bool tome  = def->learn.rfind("spell:", 0) == 0;
                    const EnchantDef* ench = charm ? items.Enchantment(def->learn.substr(8)) : nullptr;
                    const SpellDef* sp = tome ? spells.Get(def->learn.substr(6)) : nullptr;
                    const ItemDef* brew = (charm || tome) ? nullptr : items.Get(def->learn);
                    const string name = ench ? ench->name : sp ? sp->name : brew ? brew->name : def->learn;
                    if (world->KnowsRecipe(def->learn)) {
                        PushToast(charm ? "You already know the enchantment " + name + "."
                                  : tome ? "You already know " + name + "."
                                         : "You already know how to brew " + name + ".", Palette::TextDim);
                    } else {
                        world->SetFlag("recipe:" + def->learn);
                        p.inventory.RemoveSlot(inventory_cursor, 1);
                        PushToast(charm ? "Enchantment learned: " + name + ". Work it at an enchanting table."
                                  : tome ? "Spell learned: " + name + ". Press " + input.PromptFor(Action::SelectArcane) +
                                           " with a staff in hand."
                                         : "Recipe learned: " + name + ". Brew it at a cauldron.", Palette::Highlight);
                        Audio::Play(Sfx::QuestStart);
                    }
                } else if (def && def->use == "light") {
                    // A firestarter is no use on its own: it lights whatever
                    // in the bag says what it becomes when lit, and it is not
                    // used up doing it.
                    int slot = -1;
                    for (int k = 0; k < p.inventory.SlotCount() && slot < 0; ++k) {
                        const ItemDef* carried = items.Get(p.inventory.Slot(k).id);
                        if (carried && !carried->lights.empty()) slot = k;
                    }
                    const ItemDef* unlit = slot < 0 ? nullptr : items.Get(p.inventory.Slot(slot).id);
                    const ItemDef* lit = unlit ? items.Get(unlit->lights) : nullptr;
                    if (!lit) {
                        PushToast("You have nothing to light.", Palette::TextDim);
                        Audio::Play(Sfx::UiError);
                    } else {
                        p.inventory.RemoveSlot(slot, 1);
                        p.inventory.Add(lit->id, 1);
                        // Both names read badly in the sentence ("You light the
                        // Lit Lantern", "You light the Unlit Lantern"), so the
                        // state word is dropped and the rest lower-cased: what
                        // you lit is a lantern.
                        string what = lit->name;
                        if (what.rfind("Lit ", 0) == 0) what = what.substr(4);
                        for (char& c : what) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                        PushToast("You light the " + what + ".", Palette::Highlight);
                        Audio::Play(Sfx::QuestStart);
                        quests->RefreshCollectObjectives(p.inventory);
                    }
                } else if (def && def->use == "bag") {
                    string why;
                    const string name = def->name;
                    if (p.WearBag(inventory_cursor, why)) {
                        PushToast("You shoulder the " + name + ". Your bag holds " +
                                  std::to_string(p.inventory.SlotCount()) + " now.", Palette::Highlight);
                        Audio::Play(Sfx::Equip);
                    } else {
                        PushToast(why, Palette::TextDim);
                        Audio::Play(Sfx::UiError);
                    }
                } else if (def && def->use == "camp") {
                    const string why = world->PitchCamp(inventory_cursor, ctx);
                    if (why.empty()) {
                        PushToast("You pitch camp. After dusk you can sleep here.", Palette::Xp);
                        SetState(GameState::Play);
                        return;
                    }
                    PushToast(why, {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                } else if (def && def->slot != SLOT_NONE) {
                    string why;
                    if (p.EquipFromInventory(inventory_cursor, why)) {
                        PushToast("Equipped " + def->name + ".", Palette::Xp);
                        Audio::Play(Sfx::Equip);
                    } else {
                        PushToast(why.empty() ? "You cannot equip that." : why,
                                  {235, 150, 120, 255});
                        Audio::Play(Sfx::UiError);
                    }
                } else {
                    PushToast("Nothing happens.", Palette::TextDim);
                }
            }
        }

        // The drop key: what the cursor is on goes on the ground at the
        // player's feet, where it can be picked back up once they have
        // stepped off it, and lies for a few minutes. A stack asks to be
        // pressed twice, so a purse of coins is not one slip from the floor.
        // Leaving the map loses it, which is as close to destroying a thing
        // as the game gets.
        // The lock-on button, which has nothing to lock on to in a bag, puts
        // what the cursor is on to hand.
        if (input.Pressed(Action::Target)) {
            const ItemStack& under = p.inventory.Slot(inventory_cursor);
            const ItemDef* ud = under.Empty() ? nullptr : items.Get(under.id);
            if (ud && ud->consumable) {
                p.SetQuickItem(under.id);
                PushToast(ud->name + " is to hand: " + input.PromptFor(input.ShiftAction()) + " + " +
                          input.PromptFor(Action::Interact) + " uses it.", Palette::Xp);
                Audio::Play(Sfx::Equip);
            } else if (ud) {
                PushToast("Only something to eat or drink can be kept to hand.", Palette::TextDim);
            }
        }
        if (input.Pressed(Action::Drop)) {
            const ItemStack& s = p.inventory.Slot(inventory_cursor);
            const ItemDef* def = s.Empty() ? nullptr : items.Get(s.id);
            if (s.Empty()) {
                drop_armed = -1;
            } else if (MustKeep(def, *quests)) {
                PushToast("You had better hold on to that.", Palette::TextDim);
                Audio::Play(Sfx::UiError);
            } else if (s.qty > 1 && drop_armed != inventory_cursor) {
                drop_armed = inventory_cursor;
                PushToast("Press " + input.PromptFor(Action::Drop) + " again to drop all " +
                          std::to_string(s.qty) + " " + (def ? def->name : s.id) + ".", Palette::Highlight);
            } else {
                const string id = s.id;
                const string name = def ? def->name : id;
                const int qty = s.qty;
                p.inventory.RemoveSlot(inventory_cursor, qty);
                world->DropItem(id, qty, p.x, p.y + 4.0f, ctx, true);
                PushToast("Dropped " + (qty > 1 ? std::to_string(qty) + " " : string("")) + name + ".",
                          Palette::TextDim);
                Audio::Play(Sfx::Pickup, 0.8f, 0.8f);
                quests->RefreshCollectObjectives(p.inventory);
                drop_armed = -1;
            }
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Inventory) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawInventory() {
    ui.Dim(0.5f);
    Player& p = world->player;

    // Four rows of seven, and a row more for every bag put on, up to eight.
    // The squares stay the size they were for as long as they fit, and the
    // grid keeps its width, so the worn list beside it does not move.
    constexpr int COLS = 7;
    const int rows = std::max(4, (p.inventory.SlotCount() + COLS - 1) / COLS);
    const float pitch = 58.0f;                                   // a square and its gap, at four rows
    const float chrome = 470.0f - 4.0f * pitch;                  // the panel, less its grid
    const float step = std::clamp(std::floor((ui.ViewHeight() - 40.0f - chrome) / rows), 40.0f, pitch);
    const float gap  = step >= 56.0f ? 6.0f : step >= 48.0f ? 5.0f : 4.0f;
    const float cell = step - gap;
    const float grid_h = rows * step;
    const SDL_FRect panel = CenteredPanel(ui, 700.0f, chrome + grid_h);
    ui.Panel(panel);
    ui.Text("Inventory", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight);

    char header[96];
    SDL_snprintf(header, sizeof(header), "%d coins    %d / %d slots used",
                 p.inventory.Coins(),
                 p.inventory.SlotCount() - p.inventory.FreeSlots(),
                 p.inventory.SlotCount());
    ui.Text(header, panel.x + panel.w - 24.0f, panel.y + 22.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    ReorganizeButton(ui, input.PromptFor(Action::CycleSpell) + "  Reorganize", panel.x + 170.0f, panel.y + 18.0f);

    // What is to hand, and how to change it: any potion or dish can be, not
    // only the one the slot happens to hold.
    {
        string hand = p.QuickItem();
        if (hand.empty() && !p.QuickChoices().empty()) hand = p.QuickChoices().front();
        const ItemDef* hd = hand.empty() ? nullptr : items.Get(hand);
        const string use = input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(Action::Interact);
        const string line = hd ? "To hand (" + use + "): " + hd->name + "    " + input.PromptFor(Action::Target) +
                                     " on a potion or dish puts it there"
                               : input.PromptFor(Action::Target) + " on a potion or dish keeps it to hand for " + use;
        ui.Text(line, panel.x + panel.w - 24.0f, panel.y + 40.0f, TextSize::Small, {230, 200, 120, 255}, Align::Right);
    }

    // --- item grid -----------------------------------------------------------
    // Centred in the width the four-row grid has, so smaller squares do not
    // drift left of the text under them.
    const float grid_x = panel.x + 24.0f + (COLS * pitch - COLS * (cell + gap)) * 0.5f;
    const float grid_y = panel.y + 62.0f;

    // Where the lit square is, so the card can be put beside it once the rest
    // of the panel has been drawn and cannot paint over it.
    SDL_FRect lit{0, 0, 0, 0};
    for (int i = 0; i < p.inventory.SlotCount(); ++i) {
        const int col = i % COLS, row = i / COLS;
        const SDL_FRect r = {grid_x + col * (cell + gap), grid_y + row * (cell + gap),
                             cell, cell};
        const bool selected = (!inventory_on_equipment && i == inventory_cursor);
        const bool lifted = i == inventory_held;
        if (selected) lit = r;

        ui.Fill(r, {34, 27, 22, 235});
        ui.Outline(r, lifted ? SDL_Color{255, 214, 96, 255} : selected ? Palette::Highlight : Palette::BorderDim,
                   selected || lifted ? 2.0f : 1.0f);

        const ItemStack& s = p.inventory.Slot(i);
        if (s.Empty()) continue;

        const ItemDef* def = items.Get(s.id);
        SDL_Texture* tex = (def && !def->icon.empty()) ? textures->Get(def->icon) : nullptr;
        const SDL_FRect inner = {r.x + 6.0f, r.y + 6.0f, r.w - 12.0f, r.h - 12.0f};
        // What is lifted is shown faint where it was: it is in the hand now.
        if (tex && lifted) SDL_SetTextureAlphaMod(tex, 90);
        if (tex) SDL_RenderTexture(renderer, tex, nullptr, &inner);
        else     DrawItemPlaceholder(ui, def, s.id, inner);
        if (tex && lifted) SDL_SetTextureAlphaMod(tex, 255);

        if (s.qty > 1)
            ui.TextShadowed(std::to_string(s.qty), r.x + r.w - 4.0f, r.y + r.h - 18.0f,
                            TextSize::Small, Palette::Highlight, Align::Right);
        // What is to hand wears a gold corner.
        if (!p.QuickItem().empty() && s.id == p.QuickItem())
            ui.Fill({r.x + 2.0f, r.y + 2.0f, 8.0f, 8.0f}, {255, 214, 96, 255});
    }

    // And in the hand, over the square it would go down in.
    if (inventory_held >= 0 && !inventory_on_equipment && lit.w > 0.0f) {
        const ItemStack& held = p.inventory.Slot(inventory_held);
        const ItemDef* hd = held.Empty() ? nullptr : items.Get(held.id);
        SDL_Texture* tex = (hd && !hd->icon.empty()) ? textures->Get(hd->icon) : nullptr;
        const SDL_FRect in_hand = {lit.x - 4.0f, lit.y - 8.0f, lit.w - 8.0f, lit.h - 8.0f};
        if (tex) SDL_RenderTexture(renderer, tex, nullptr, &in_hand);
        if (held.qty > 1)
            ui.TextShadowed(std::to_string(held.qty), in_hand.x + in_hand.w, in_hand.y + in_hand.h - 14.0f,
                            TextSize::Small, Palette::Highlight, Align::Right);
    }

    // --- equipment -----------------------------------------------------------
    const float eq_x = panel.x + 24.0f + COLS * pitch + 18.0f;
    ui.Text("Worn", eq_x, panel.y + 62.0f, TextSize::Body, Palette::Text);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const SDL_FRect r = {eq_x, panel.y + 92.0f + i * 30.0f, 236.0f, 26.0f};
        const bool selected = (inventory_on_equipment && i == equipment_cursor);
        if (selected) lit = r;
        ui.Fill(r, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(r, selected ? Palette::Highlight : Palette::BorderDim, 1.0f);

        const string& worn = p.equipment.InSlot(i);
        const ItemDef* def = worn.empty() ? nullptr : items.Get(worn);
        // A second dagger sits where a shield would: the row says which it is.
        const bool second = def && i == SLOT_SHIELD && def->slot == SLOT_WEAPON;
        ui.Text(second ? "off hand" : EquipSlotName(i), r.x + 8.0f, r.y + 4.0f, TextSize::Small, Palette::TextDim);
        ui.Text(def ? def->name : "-", r.x + r.w - 8.0f, r.y + 4.0f, TextSize::Small,
                def ? Palette::Text : Palette::TextDim, Align::Right);
    }

    // --- bonuses -------------------------------------------------------------
    // Two lines rather than one: robes and staves carry magic, and a bow
    // carries ranged, so leaving those off understated half the equipment.
    //
    // Laid out as two columns under the worn list. All three melee bonuses on
    // one line ran past the right edge of the panel once any of them reached
    // two digits, which the starting sword and shield already do.
    const float bonus_y = panel.y + 92.0f + static_cast<float>(SLOT_COUNT) * 30.0f + 2.0f;
    const float col2_x  = eq_x + 118.0f;
    const auto bonus_cell = [&](const char* label, int value, float x, float y) {
        char buf[48];
        SDL_snprintf(buf, sizeof(buf), "%s +%d", label, value);
        ui.Text(buf, x, y, TextSize::Small, Palette::Xp);
    };
    bonus_cell("Attack",   p.equipment.AttackBonus(),   eq_x,   bonus_y);
    bonus_cell("Strength", p.equipment.StrengthBonus(), col2_x, bonus_y);
    bonus_cell("Defence",  p.equipment.DefenceBonus(),  eq_x,   bonus_y + 16.0f);
    bonus_cell("Ranged",   p.equipment.RangedBonus(),   col2_x, bonus_y + 16.0f);
    bonus_cell("Magic",    p.equipment.MagicBonus(),    eq_x,   bonus_y + 32.0f);
    // Boots and charms that quicken the step, as the percentage they add.
    if (p.equipment.MoveSpeed() != 0.0f) {
        char walk[48];
        SDL_snprintf(walk, sizeof(walk), "Walk +%d%%",
                     static_cast<int>(std::lround(p.equipment.MoveSpeed() * 100.0f)));
        ui.Text(walk, col2_x, bonus_y + 32.0f, TextSize::Small, Palette::Xp);
    }

    char bonus[128];

    // Attack speed is stated as a rate rather than as the raw multiplier: the
    // underlying number is a multiplier on swing time, so lower is faster,
    // and a stat where smaller is better wants explaining every time it is
    // read. "Swings 1.25x" does not.
    {
        const float sp = p.WeaponSpeed();
        // Bands chosen against the actual spread in data/items.json: bows sit
        // at 0.80-0.88 and daggers at 0.70-0.80, so a threshold of 0.85 called
        // a shortbow "even". Anything a tenth either side of the baseline is
        // worth naming.
        const char* word = sp < 0.92f ? "fast" : (sp > 1.08f ? "slow" : "even");
        const float reach = p.WeaponReach();
        if (reach > 1.05f)
            SDL_snprintf(bonus, sizeof(bonus), "Attack speed  %.2fx  (%s)   Reach  %.2fx", 1.0f / sp, word, reach);
        else
            SDL_snprintf(bonus, sizeof(bonus), "Attack speed  %.2fx  (%s)", 1.0f / sp, word);
        ui.Text(bonus, eq_x, bonus_y + 50.0f, TextSize::Small, Palette::TextDim);
    }

    // --- selected item detail ------------------------------------------------
    const string sel_id = inventory_on_equipment
        ? p.equipment.InSlot(equipment_cursor)
        : p.inventory.Slot(inventory_cursor).id;

    if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id)) {
        const float y = grid_y + grid_h + 12.0f;
        const float text_x = panel.x + 24.0f;
        ui.Text(def->name, text_x, y, TextSize::Body, Palette::Highlight);
        // The tier and what it needs, on the right of the name.
        string tag;
        if (const TierDef* t = def->tier.empty() ? nullptr : items.Tier(def->tier)) tag = t->name + " tier";
        for (const auto& rq : def->requirements) {
            const bool met = p.skills.Level(rq.first) >= rq.second;
            tag += (tag.empty() ? "" : "   ") + string(met ? "" : "needs ") + SkillName(rq.first) +
                   " " + std::to_string(rq.second);
        }
        if (!tag.empty())
            ui.Text(tag, text_x + COLS * pitch - 12.0f, y + 4.0f, TextSize::Small, Palette::TextDim, Align::Right);
        const float desc_h = ui.TextWrapped(def->description, text_x, y + 24.0f,
                                            COLS * pitch - 12.0f, TextSize::Small, Palette::TextDim);
        // What it does beyond its numbers, in the colour of something rare.
        if (!def->passive_text.empty())
            ui.TextWrapped(def->passive_text, text_x, y + 28.0f + desc_h,
                           COLS * pitch - 12.0f, TextSize::Small, Palette::Highlight);
    }

    const ItemDef* under_cursor = sel_id.empty() ? nullptr : items.Get(sel_id);
    const bool can_hand = !inventory_on_equipment && under_cursor && under_cursor->consumable;
    ui.Text(inventory_held >= 0
                ? input.PromptFor(Action::Ability) + " or " + input.PromptFor(Action::Confirm) + " put it down here     " +
                  input.PromptFor(Action::Back) + " put it back"
                : input.PromptFor(Action::Confirm) + " use / equip     " +
                  (can_hand ? input.PromptFor(Action::Target) + " keep to hand     " : string()) +
                  input.PromptFor(Action::Ability) + " move     " +
                  input.PromptFor(Action::Drop) + " drop     " +
                  input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    // Drawn over everything else: a card under the panel it belongs to would
    // be a card nobody can read.
    if (lit.w > 0.0f)
        if (const ItemDef* def = sel_id.empty() ? nullptr : items.Get(sel_id))
            DrawItemCard(*def, lit);
}

// =============================================================================
//  What an item is worth
//
//  Every panel that shows an item used to show its name, its description and
//  what it needed to be worn -- and not one of its numbers. A sword on a
//  smith's shelf said nothing about being better than the one in your hand,
//  which is the only question anybody was asking of it.
//
//  This draws the numbers, and next to each the difference it would make
//  against what is worn in that slot now. Green is better, red is worse, and a
//  stat that is nothing on both pieces is left off entirely: a helmet's line
//  is one line, not five zeroes. The comparison is what makes it worth
//  reading, so a piece that goes in an empty slot compares against nothing and
//  shows the whole of its bonus as the gain it is.
// =============================================================================

namespace {

const SDL_Color kWorse{225, 130, 120, 255};

// The one thing a panel decides for itself: what a verdict looks like. Which
// rows there are is ItemStatLines' business, in the item layer, where it can
// be checked by the self-test.
SDL_Color DeltaColour(int verdict) {
    return verdict > 0 ? Palette::Xp : (verdict < 0 ? kWorse : Palette::TextDim);
}

} // namespace

// What is in the same slot now, which is what a piece is weighed against.
bool Game::GoesInOtherHand(const ItemDef& d) const {
    if (d.slot != SLOT_WEAPON || !d.offhand) return false;
    // Looking at what is already in a hand is not looking at a second one.
    if (state == GameState::Inventory && inventory_on_equipment) return false;
    const ItemDef* right = world->player.equipment.Weapon();
    const ItemDef* left  = items.Get(world->player.equipment.InSlot(SLOT_SHIELD));
    return right && right->offhand && !(left && left->slot == SLOT_WEAPON);
}

const ItemDef* Game::WornAgainst(const ItemDef& d) const {
    if (d.slot == SLOT_NONE || GoesInOtherHand(d)) return nullptr;
    const string& in_slot = world->player.equipment.InSlot(d.slot);
    return in_slot.empty() ? nullptr : items.Get(in_slot);
}

// The line above the numbers, saying what they are being measured against.
string Game::WornAgainstLine(const ItemDef& d) const {
    if (d.slot == SLOT_NONE) return string();
    const string& in_slot = world->player.equipment.InSlot(d.slot);
    // It replaces nothing, and what it brings is its speed: its numbers are
    // shown, and no change against them, because there will be none.
    if (GoesInOtherHand(d)) return "In your other hand: speed, not bonuses";
    if (!in_slot.empty() && in_slot == d.id) return "Worn now";
    if (const ItemDef* worn = in_slot.empty() ? nullptr : items.Get(in_slot))
        return "Instead of " + worn->name;
    return "Nothing worn there";
}

float Game::DrawItemStats(const ItemDef& d, float x, float y, float w) {
    const Player& p = world->player;
    const float top = y;
    const float row = 16.0f;
    // Three columns: what the stat is, what this piece gives, and the change.
    // The two numbers sit near each other rather than at opposite ends of the
    // pane, because they are the pair being compared.
    const float value_x = x + w * 0.58f;
    const float delta_x = x + w * 0.88f;

    const ItemDef* worn = WornAgainst(d);
    const bool compare = (d.slot != SLOT_NONE) && !GoesInOtherHand(d);

    if (d.slot != SLOT_NONE) {
        // The longest names in the game are enchanted gloves -- "Instead of
        // Orichalcum Gauntlets of the Hawk's Eye" -- and a narrow pane cannot
        // hold them on one line, so one that does not fit takes two.
        const string head = WornAgainstLine(d);
        if (ui.Measure(head, TextSize::Small).x <= w) {
            ui.Text(head, x, y, TextSize::Small, Palette::TextDim);
            y += row + 2.0f;
        } else {
            y += ui.TextWrapped(head, x, y, w, TextSize::Small, Palette::TextDim) + 2.0f;
        }
    }
    for (const ItemStat& r : ItemStatLines(d, worn, compare)) {
        ui.Text(r.label, x, y, TextSize::Small, Palette::Text);
        ui.Text(r.value, value_x, y, TextSize::Small,
                r.value == "+0" ? Palette::TextDim : Palette::Xp, Align::Right);
        if (!r.delta.empty()) ui.Text(r.delta, delta_x, y, TextSize::Small, DeltaColour(r.verdict), Align::Right);
        y += row;
    }

    // What drinking or eating it does. A dish says the rest of its piece
    // itself, in the panel that cooks it.
    if (d.consumable && !d.IsDish()) {
        string line;
        if (d.heal > 0)  line += "Heals " + std::to_string(d.heal) + "   ";
        if (d.mana > 0)  line += "+" + std::to_string(d.mana) + " mana   ";
        if (d.stamina)   line += "full breath   ";
        // A boost is a flat amount plus a share of the level, so what it is
        // worth depends on who drinks it. Worked out for this character rather
        // than printed as the two numbers it is stored as: "+3 and 10%" is a
        // recipe, "+10 Strength" is the answer.
        for (const auto& b : d.boosts) {
            const int gain = ItemDef::BoostGain(b.second, p.skills.Level(b.first));
            char buf[64];
            SDL_snprintf(buf, sizeof(buf), "+%d %s   ", gain, SkillName(b.first));
            line += buf;
        }
        // A ward says what it keeps off, and for how long.
        if (!d.ward.empty()) {
            string names;
            for (Status s : d.ward) {
                const StatusDef* sd = status_db.Get(s);
                names += (names.empty() ? "" : ", ") + (sd ? sd->name : string(StatusId(s)));
            }
            char buf[96];
            SDL_snprintf(buf, sizeof(buf), "wards off %s for %d min   ", names.c_str(),
                         static_cast<int>(d.ward_minutes + 0.5f));
            line += buf;
        }
        if (!line.empty()) y += ui.TextWrapped(line, x, y, w, TextSize::Small, Palette::Xp);
    }

    // What it does that no stat block can say. The bag has always printed
    // this; the shop and the anvil never did, which meant the one line that
    // makes a legendary piece worth having was the one line you could not
    // read until you owned it.
    if (!d.passive_text.empty())
        y += ui.TextWrapped(d.passive_text, x, y + 2.0f, w, TextSize::Small, Palette::Highlight) + 2.0f;

    return y > top ? y - top + 4.0f : 0.0f;
}

// =============================================================================
//  The card beside the cursor
//
//  The bag and the storage chest have no room under their grids for a stat
//  block: their lower half is already the name, the tier and the description.
//  So the numbers come to the cursor instead -- a small card beside whatever
//  square is highlighted, for the things where the numbers are the question.
//
//  Only for what can be worn. A card over every rock and bar would be noise
//  covering the grid it is trying to explain.
// =============================================================================

void Game::DrawItemCard(const ItemDef& d, const SDL_FRect& slot) {
    if (d.slot == SLOT_NONE) return;
    const ItemDef* worn = WornAgainst(d);
    // A second dagger changes nothing but the speed: its numbers, and no change.
    const vector<ItemStat> rows = ItemStatLines(d, worn, !GoesInOtherHand(d));
    if (rows.empty()) return;

    const string head = WornAgainstLine(d);
    const float pad = 10.0f, row_h = 16.0f;

    // Wide enough for its own longest line, measured rather than guessed: item
    // names run from "Cap" to "Orichalcum Greatsword".
    float label_w = 0.0f, value_w = 0.0f, delta_w = 0.0f;
    for (const ItemStat& r : rows) {
        label_w = std::max(label_w, ui.Measure(r.label, TextSize::Small).x);
        value_w = std::max(value_w, ui.Measure(r.value, TextSize::Small).x);
        delta_w = std::max(delta_w, ui.Measure(r.delta, TextSize::Small).x);
    }
    const float gap = 10.0f;
    const float widest_head = std::max(ui.Measure(d.name, TextSize::Small).x,
                                       ui.Measure(head, TextSize::Small).x);
    const float card_w = std::max(widest_head, label_w + gap + value_w + gap + delta_w) + pad * 2.0f;
    const float card_h = pad * 2.0f + row_h * 2.0f + 4.0f + row_h * static_cast<float>(rows.size());

    // Beside the square, on whichever side it fits; nudged back on-screen
    // rather than allowed to hang off an edge.
    SDL_FRect card = {slot.x + slot.w + 10.0f, slot.y - 6.0f, card_w, card_h};
    if (card.x + card.w > ui.ViewWidth() - 8.0f) card.x = slot.x - card.w - 10.0f;
    card.x = std::clamp(card.x, 8.0f, std::max(8.0f, ui.ViewWidth() - card.w - 8.0f));
    card.y = std::clamp(card.y, 8.0f, std::max(8.0f, ui.ViewHeight() - card.h - 8.0f));

    ui.Panel(card);
    float y = card.y + pad;
    ui.Text(d.name, card.x + pad, y, TextSize::Small, Palette::Highlight);
    y += row_h;
    ui.Text(head, card.x + pad, y, TextSize::Small, Palette::TextDim);
    y += row_h + 4.0f;
    const float value_x = card.x + card.w - pad - delta_w - gap;
    const float delta_x = card.x + card.w - pad;
    for (const ItemStat& r : rows) {
        ui.Text(r.label, card.x + pad, y, TextSize::Small, Palette::Text);
        ui.Text(r.value, value_x, y, TextSize::Small,
                r.value == "+0" ? Palette::TextDim : Palette::Xp, Align::Right);
        if (!r.delta.empty()) ui.Text(r.delta, delta_x, y, TextSize::Small, DeltaColour(r.verdict), Align::Right);
        y += row_h;
    }
}

// =============================================================================
//  Storage
//
//  A chest you keep things in, standing open beside your pack. Two grids, the
//  same squares as the inventory, and the button moves a stack from whichever
//  side the cursor is on to the other. Nothing is ever destroyed: a move that
//  will not fit moves what fits and says so.
// =============================================================================

namespace {
constexpr int kStorageCols = 10;   // a ten by ten chest
constexpr int kBagCols     = 7;    // the same shape as the inventory screen
}

void Game::UpdateStorage() {
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause) ||
        input.Pressed(Action::Inventory)) {
        ClosePanel();
        return;
    }

    Player& p = world->player;
    Inventory& chest = world->Storage(storage_id, storage_slots, &items);
    const int bag_slots = p.inventory.SlotCount();
    const int box_slots = chest.SlotCount();
    storage_bag_cursor = std::clamp(storage_bag_cursor, 0, bag_slots - 1);

    int& cursor = storage_on_chest ? storage_cursor : storage_bag_cursor;
    const int cols  = storage_on_chest ? kStorageCols : kBagCols;
    const int count = storage_on_chest ? box_slots : bag_slots;
    const int before = cursor;
    const bool side_before = storage_on_chest;

    if (input.MenuRight()) {
        // The right-hand edge of the pack steps across into the chest, and the
        // chest's right-hand edge stops: there is nothing further right.
        if (!storage_on_chest && cursor % cols == cols - 1) storage_on_chest = true;
        else cursor = std::min(cursor + 1, count - 1);
    }
    if (input.MenuLeft()) {
        if (storage_on_chest && cursor % cols == 0) storage_on_chest = false;
        else cursor = std::max(cursor - 1, 0);
    }
    if (input.MenuDown()) cursor = std::min(cursor + cols, count - 1);
    if (input.MenuUp())   cursor = std::max(cursor - cols, 0);

    // Whichever side the cursor lands on, keep it inside that side's grid.
    storage_cursor     = std::clamp(storage_cursor, 0, std::max(0, box_slots - 1));
    storage_bag_cursor = std::clamp(storage_bag_cursor, 0, std::max(0, bag_slots - 1));
    if (cursor != before || storage_on_chest != side_before) Audio::Play(Sfx::UiMove);

    // --- tidying ------------------------------------------------------------------
    // Whichever side the cursor is on: the pack after the stowing, or the chest.
    if (input.Pressed(Action::CycleSpell)) {
        (storage_on_chest ? chest : p.inventory).Reorganize();
        cursor = 0;
        PushToast(storage_on_chest ? "Chest reorganized." : "Pack reorganized.", Palette::Xp);
        Audio::Play(Sfx::Equip, 0.6f);
        return;
    }

    // --- by the armful ---------------------------------------------------------
    // The drop button, which in a chest has nothing to drop. From the pack it
    // stows everything the chest already has some of -- the ore goes with the
    // ore -- or, with sprint held, everything but the purse and what cannot be
    // parted with. From the chest it takes the lot. Emptying a full pack into
    // a chest was fifty-six presses.
    if (input.Pressed(Action::Drop)) {
        const bool everything = input.Down(Action::Sprint);
        Inventory& from = storage_on_chest ? chest : p.inventory;
        Inventory& to   = storage_on_chest ? p.inventory : chest;
        int moved_total = 0, kinds = 0;
        bool no_room = false;
        for (int i = 0; i < from.SlotCount(); ++i) {
            const ItemStack stack = from.Slot(i);
            if (stack.Empty()) continue;
            if (!storage_on_chest) {
                const ItemDef* d = items.Get(stack.id);
                if (stack.id == "coins" || MustKeep(d, *quests)) continue;
                if (!everything && !to.Has(stack.id, 1)) continue;
            }
            const int moved = to.Add(stack.id, stack.qty);
            if (moved <= 0) { no_room = true; continue; }
            from.RemoveSlot(i, moved);
            moved_total += moved;
            ++kinds;
            if (moved < stack.qty) no_room = true;
        }
        if (moved_total > 0) {
            PushToast((storage_on_chest ? "Took " : "Stowed ") + std::to_string(moved_total) + " things, " +
                          std::to_string(kinds) + (kinds == 1 ? " kind" : " kinds") +
                          (no_room ? "  -  no room for the rest" : ""),
                      no_room ? SDL_Color{235, 200, 120, 255} : Palette::Text);
            Audio::Play(Sfx::Pickup, 0.7f);
            quests->RefreshCollectObjectives(p.inventory);
        } else {
            PushToast(no_room ? (storage_on_chest ? "Your pack is full." : "The chest is full.")
                      : storage_on_chest ? "The chest is empty."
                      : everything ? "There is nothing to stow."
                                   : "Nothing you carry is already in here. Hold " +
                                         input.PromptFor(Action::Sprint) + " to stow everything.",
                      Palette::TextDim);
            Audio::Play(Sfx::UiError);
        }
        return;
    }

    if (!(input.Pressed(Action::Confirm) || input.Pressed(Action::Interact))) return;

    // One at a time, or the whole stack with sprint held: the same hand as the
    // shop screen, so the two panels are not opposites of each other.
    const bool all = input.Down(Action::Sprint);
    Inventory& from = storage_on_chest ? chest : p.inventory;
    Inventory& to   = storage_on_chest ? p.inventory : chest;
    const int slot  = storage_on_chest ? storage_cursor : storage_bag_cursor;

    const ItemStack stack = from.Slot(slot);
    if (stack.Empty()) { Audio::Play(Sfx::UiError); return; }

    const int want = all ? stack.qty : 1;
    const int moved = to.Add(stack.id, want);
    if (moved <= 0) {
        PushToast(storage_on_chest ? "Your pack is full." : "The chest is full.",
                  {235, 150, 120, 255});
        Audio::Play(Sfx::UiError);
        return;
    }
    from.RemoveSlot(slot, moved);

    const ItemDef* d = items.Get(stack.id);
    const string name = d ? d->name : stack.id;
    PushToast((storage_on_chest ? "Took " : "Stored ") + std::to_string(moved) + "x " + name +
                  (moved < want ? "  -  no room for the rest" : ""),
              moved < want ? SDL_Color{235, 200, 120, 255} : Palette::Text);
    Audio::Play(Sfx::Pickup, 0.7f);
    quests->RefreshCollectObjectives(p.inventory);
}

void Game::DrawStorage() {
    ui.Dim(0.5f);
    Player& p = world->player;
    const Inventory& chest = world->Storage(storage_id, storage_slots, &items);

    const float cell = 34.0f, gap = 4.0f;
    const float bag_w   = kBagCols * (cell + gap) - gap;
    const float box_w   = kStorageCols * (cell + gap) - gap;
    const int   box_rows = (chest.SlotCount() + kStorageCols - 1) / kStorageCols;
    const int   bag_rows = (p.inventory.SlotCount() + kBagCols - 1) / kBagCols;
    const float grid_h  = std::max(box_rows, bag_rows) * (cell + gap) - gap;

    const SDL_FRect panel = CenteredPanel(ui, bag_w + box_w + 96.0f, grid_h + 168.0f);
    ui.Panel(panel);
    ui.Text(storage_title, panel.x + 24.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight);
    // Whichever side the cursor is on is the side that is tidied.
    ReorganizeButton(ui, input.PromptFor(Action::CycleSpell) + (storage_on_chest ? "  Reorganize the chest" : "  Reorganize your pack"),
                     panel.x + 48.0f + ui.Measure(storage_title, TextSize::Large).x, panel.y + 20.0f);

    const float grid_y = panel.y + 76.0f;
    const float bag_x  = panel.x + 24.0f;
    const float box_x  = bag_x + bag_w + 48.0f;

    // One square, drawn the same on either side so a stack does not change
    // appearance when it crosses over.
    SDL_FRect lit{0, 0, 0, 0};
    const auto square = [&](const Inventory& inv, int i, float x, float y, bool selected) {
        const SDL_FRect r = {x, y, cell, cell};
        if (selected) lit = r;
        ui.Fill(r, {34, 27, 22, 235});
        ui.Outline(r, selected ? Palette::Highlight : Palette::BorderDim, selected ? 2.0f : 1.0f);
        const ItemStack& s = inv.Slot(i);
        if (s.Empty()) return;
        const ItemDef* def = items.Get(s.id);
        SDL_Texture* tex = (def && !def->icon.empty()) ? textures->Get(def->icon) : nullptr;
        const SDL_FRect inner = {r.x + 4.0f, r.y + 4.0f, r.w - 8.0f, r.h - 8.0f};
        if (tex) SDL_RenderTexture(renderer, tex, nullptr, &inner);
        else     DrawItemPlaceholder(ui, def, s.id, inner);
        if (s.qty > 1)
            ui.TextShadowed(std::to_string(s.qty), r.x + r.w - 2.0f, r.y + r.h - 15.0f,
                            TextSize::Small, Palette::Highlight, Align::Right);
    };

    char head[96];
    SDL_snprintf(head, sizeof(head), "Your pack   %d / %d",
                 p.inventory.SlotCount() - p.inventory.FreeSlots(), p.inventory.SlotCount());
    ui.Text(head, bag_x, panel.y + 52.0f, TextSize::Small, Palette::TextDim);
    for (int i = 0; i < p.inventory.SlotCount(); ++i)
        square(p.inventory, i, bag_x + (i % kBagCols) * (cell + gap),
               grid_y + (i / kBagCols) * (cell + gap),
               !storage_on_chest && i == storage_bag_cursor);

    SDL_snprintf(head, sizeof(head), "The chest   %d / %d",
                 chest.SlotCount() - chest.FreeSlots(), chest.SlotCount());
    ui.Text(head, box_x, panel.y + 52.0f, TextSize::Small, Palette::TextDim);
    for (int i = 0; i < chest.SlotCount(); ++i)
        square(chest, i, box_x + (i % kStorageCols) * (cell + gap),
               grid_y + (i / kStorageCols) * (cell + gap),
               storage_on_chest && i == storage_cursor);

    // What is under the cursor, and how to move it.
    const ItemStack& sel = storage_on_chest ? chest.Slot(storage_cursor)
                                            : p.inventory.Slot(storage_bag_cursor);
    const float foot_y = grid_y + grid_h + 16.0f;
    if (const ItemDef* def = sel.Empty() ? nullptr : items.Get(sel.id)) {
        ui.Text(def->name, bag_x, foot_y, TextSize::Body, Palette::Highlight);
        // Wrapped to the panel: a bag's description is four lines of prose and
        // went off the side of the screen.
        ui.TextWrapped(def->description, bag_x, foot_y + 20.0f, panel.w - 48.0f, TextSize::Small, Palette::TextDim);
    }
    ui.Text(input.PromptFor(Action::Confirm) + " move one   -   hold " +
                input.PromptFor(Action::Sprint) + " for the stack   -   " +
                input.PromptFor(Action::Drop) + (storage_on_chest ? " take all" : " stow alike") + "   -   " +
                input.PromptFor(Action::CycleSpell) + (storage_on_chest ? " tidy the chest" : " tidy the pack") + "   -   " +
                input.PromptFor(Action::Back) + " close",
            panel.x + panel.w - 24.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // The card last, over the top of the grids: moving armour between a chest
    // and a pack is the other time you want to know which of two coifs is the
    // better one.
    if (lit.w > 0.0f && !sel.Empty())
        if (const ItemDef* def = items.Get(sel.id)) DrawItemCard(*def, lit);
}
