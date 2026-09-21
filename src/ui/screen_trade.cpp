// =============================================================================
//  The Game class's screens, continued: crafting, enchanting and shops
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
//  Crafting
// =============================================================================

void Game::UpdateCrafting() {
    const vector<const ItemDef*> recipes = items.Recipes(craft_station);
    // Opened on the row it was left on. state_time is nothing on the frame a
    // panel opens, which is the one frame the cursor is put back rather than
    // remembered.
    int& kept = craft_cursor_at[std::clamp(static_cast<int>(craft_station), 0, 7)];
    if (state_time <= 0.0f) craft_cursor = std::clamp(kept, 0, std::max(0, static_cast<int>(recipes.size()) - 1));
    MoveCursor(craft_cursor, static_cast<int>(recipes.size()));
    kept = craft_cursor;

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !recipes.empty()) {
        const ItemDef* recipe = recipes[std::clamp(craft_cursor, 0,
                                                   static_cast<int>(recipes.size()) - 1)];
        Player& p = world->player;
        const int skill = CraftSkill(craft_station);

        const ItemDef* result = items.Get(recipe->craft_result);
        // A dye is brewed but never taught: see ItemDef::untaught.
        const bool brew_taught = craft_station == CraftStation::Cauldron && !(result && result->untaught);
        if ((brew_taught || (result && result->needs_recipe)) &&
            !world->KnowsRecipe(recipe->craft_result)) {
            PushToast("You have not learned that recipe yet.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        if (p.skills.Level(skill) < recipe->craft_level) {
            PushToast("Needs " + string(SkillName(skill)) + " " + std::to_string(recipe->craft_level) + ".",
                      {235, 150, 120, 255});
            return;
        }

        // One, or with sprint held as many as there is the stuff for: the same
        // hand the shop uses for ten. Twenty bars used to be twenty presses.
        // Each is made exactly as one is -- its own materials, its own room in
        // the pack, its own chance of burning -- so holding the button is a
        // convenience and never a better deal.
        const int wanted = input.Down(Action::Sprint) ? 99 : 1;
        int made_n = 0, burnt_n = 0;
        string stopped;
        for (int n = 0; n < wanted; ++n) {
            bool have_all = true;
            for (const auto& in : recipe->craft_inputs)
                if (!p.inventory.Has(in.first, in.second)) { have_all = false; break; }
            if (!have_all) { stopped = "You are missing materials."; break; }
            if (p.inventory.FreeSlots() == 0) { stopped = "Your pack is full."; break; }

            for (const auto& in : recipe->craft_inputs) p.inventory.Remove(in.first, in.second);

            // At a fire it can still be ruined, the way it always could: the
            // chance falls away as the cook's level climbs past the dish's.
            if (craft_station == CraftStation::Range) {
                const float burn = std::max(0.0f, 0.34f - (p.skills.Level(skill) - recipe->craft_level) * 0.03f);
                if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < burn) {
                    p.GrantXp(skill, std::max(1, recipe->craft_xp / 8));
                    ++burnt_n;
                    continue;
                }
            }

            p.inventory.Add(recipe->craft_result, recipe->craft_qty);
            p.GrantXp(skill, recipe->craft_xp);
            // Tell the journal something was made. After the burn roll, so a
            // ruined dinner does not count, and by how many came off -- a
            // fleece spins into two bolts, and an order for two is filled by
            // one of them.
            QuestEvent made_it;
            made_it.type   = ObjectiveType::Craft;
            made_it.target = recipe->craft_result;
            made_it.amount = std::max(1, recipe->craft_qty);
            made_it.map_id = world->MapId();
            quests->Notify(made_it, p.inventory);
            ++made_n;
        }

        const ItemDef* made = items.Get(recipe->craft_result);
        const string what = made ? made->name : recipe->craft_result;
        if (made_n > 0) {
            const int total = made_n * std::max(1, recipe->craft_qty);
            PushToast(string(craft_station == CraftStation::Cauldron ? "Brewed " :
                             craft_station == CraftStation::Anvil ? "Smithed " :
                             craft_station == CraftStation::Range ? "Cooked " :
                             craft_station == CraftStation::Loom ? "Wove " :
                             craft_station == CraftStation::Rack ? "Cut and sewed " : "Crafted ") +
                      (total > 1 ? std::to_string(total) + "x " : string()) + what + ".", Palette::Xp);
        }
        if (burnt_n > 0) {
            PushToast(burnt_n > 1 ? "Burnt " + std::to_string(burnt_n) + " of them." : string("Burnt it."),
                      {200, 110, 90, 255});
            Audio::Play(Sfx::Burn);
        }
        // Said when nothing at all could be made; a run that simply came to
        // the end of the iron has nothing to apologise for.
        if (made_n == 0 && burnt_n == 0 && !stopped.empty()) PushToast(stopped, {235, 150, 120, 255});
        quests->RefreshCollectObjectives(p.inventory);
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawCrafting() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 450.0f);
    ui.Panel(panel);
    const bool anvil = (craft_station == CraftStation::Anvil);
    const bool cauldron = (craft_station == CraftStation::Cauldron);
    const bool fire = (craft_station == CraftStation::Range);
    const bool loom = (craft_station == CraftStation::Loom);
    const bool rack = (craft_station == CraftStation::Rack);
    const int skill = CraftSkill(craft_station);
    ui.Text(craft_title.empty() ? (cauldron ? "Cauldron" : anvil ? "Anvil" : fire ? "Cooking fire"
                                 : loom ? "Loom" : rack ? "Tanning Rack" : "Workbench") : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world->player;
    ui.Text(string(SkillName(skill)) + " " + std::to_string(p.skills.Level(skill)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // Say where the rest is made, so a missing recipe reads as "elsewhere"
    // rather than "gone".
    ui.Text(cauldron ? "Brewing: herbs and a vial. A recipe has to be learned before it can be brewed."
            : anvil  ? "Smithing: anything made from metal. Wood and leather are worked at a workbench."
            // One line, and it has to fit the panel at 1280x720: --audit counts
            // anything wider as a runoff, and it is right to.
            : fire   ? "Cooking: plain food, and dishes that sit with you a while."
            : loom   ? "Weaving: cloth from any fibre, and the robes. Dyes are boiled at a cauldron."
            : rack   ? "Leatherwork: hide armour, boots, bags and bedrolls. Wood is worked at a bench."
                     : "Wood and bows. Hide is cut on a tanner's rack, cloth woven at a loom, metal smithed.",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 50.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    const vector<const ItemDef*> recipes = items.Recipes(craft_station);
    if (recipes.empty()) {
        ui.Text("Nothing to make here.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f, TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    const float list_w = 280.0f, row_h = 34.0f;
    // A window of nine rows that follows the cursor: the anvil alone makes
    // sixty-odd things now, one of every piece in every tier.
    constexpr int SHOWN = 9;
    const int count = static_cast<int>(recipes.size());
    const int first = std::clamp(craft_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, panel.y + 46.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, panel.y + 62.0f + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const ItemDef* r = recipes[i];
        const ItemDef* made = items.Get(r->craft_result);
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + (i - first) * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (i == craft_cursor);
        // A brew, or anything else marked as taught rather than worked out.
        const bool taught = (cauldron && !(made && made->untaught)) || (made && made->needs_recipe);
        const bool known = !taught || world->KnowsRecipe(r->craft_result);
        const bool unlocked = known && p.skills.Level(skill) >= r->craft_level;

        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        if (made && !made->icon.empty())
            if (SDL_Texture* tex = textures->Get(made->icon)) {
                const SDL_FRect ic = {row.x + 4.0f, row.y + 3.0f, 24.0f, 24.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(known ? (made ? made->name : r->craft_result) : string("Unknown recipe"), row.x + 34.0f, row.y + 5.0f,
                TextSize::Small,
                !unlocked ? SDL_Color{120, 110, 100, 255}
                          : (selected ? Palette::Highlight : Palette::Text));
        ui.Text("Lv " + std::to_string(r->craft_level), row.x + row.w - 8.0f,
                row.y + 5.0f, TextSize::Small, Palette::TextDim, Align::Right);
    }

    // Detail for the highlighted recipe: what it needs and what you hold.
    const int index = std::clamp(craft_cursor, 0, static_cast<int>(recipes.size()) - 1);
    const ItemDef* r = recipes[index];
    const ItemDef* made = items.Get(r->craft_result);
    const float dx = panel.x + list_w + 40.0f;
    float y = panel.y + 62.0f;

    ui.Text(made ? made->name : r->craft_result, dx, y, TextSize::Body, Palette::Highlight);
    y += 28.0f;
    const bool taught_here = (cauldron && !(made && made->untaught)) || (made && made->needs_recipe);
    if (taught_here && !world->KnowsRecipe(r->craft_result)) {
        y += ui.TextWrapped(string(cauldron ? "You have not learned to brew this yet."
                                            : "You have not been shown how to make this yet.") +
                            (made && !made->recipe_from.empty() ? " " + made->recipe_from : string("")),
                            dx, y, panel.w - list_w - 64.0f, TextSize::Small, {235, 150, 120, 255}) + 8.0f;
    }
    if (made) y += ui.TextWrapped(made->description, dx, y, panel.w - list_w - 64.0f,
                                  TextSize::Small, Palette::TextDim) + 8.0f;
    // A dish says what it is worth eating for, and for how long.
    if (made && made->IsDish()) {
        const auto share = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; };
        string line;
        if (made->dish_max_hp > 0.0f)      line += "+" + share(made->dish_max_hp) + " max health   ";
        if (made->dish_max_mana > 0.0f)    line += "+" + share(made->dish_max_mana) + " max mana   ";
        if (made->dish_max_stamina > 0.0f) line += "+" + share(made->dish_max_stamina) + " max breath   ";
        for (const auto& b : made->dish_levels)
            line += "+" + std::to_string(b.second) + " " + SkillName(b.first) + "   ";
        y += ui.TextWrapped(line, dx, y, panel.w - list_w - 64.0f, TextSize::Small, Palette::Xp) + 2.0f;
        const int mins = static_cast<int>(made->dish_minutes);
        ui.Text("for " + std::to_string(mins) + (mins == 1 ? " minute" : " minutes") + ", and one dish at a time",
                dx, y, TextSize::Small, Palette::TextDim);
        y += 22.0f;
    }
    if (made && !made->requirements.empty()) {
        string req = "To use: ";
        for (const auto& rq : made->requirements)
            req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
        ui.Text(req, dx, y, TextSize::Small, Palette::Text);
        y += 22.0f;
    }
    // What it would be worth wearing, against what is being worn. The reason
    // to stand at an anvil is that the thing on it is better than the thing in
    // your hand, and until now the panel never said so.
    if (made) y += DrawItemStats(*made, dx, y, panel.w - list_w - 64.0f);

    ui.Text("Materials", dx, y, TextSize::Small, Palette::Highlight);
    y += 20.0f;
    for (const auto& in : r->craft_inputs) {
        const ItemDef* mat = items.Get(in.first);
        const int held = p.inventory.Count(in.first);
        const bool enough = held >= in.second;
        char line[128];
        SDL_snprintf(line, sizeof(line), "%s  %d / %d",
                     (mat ? mat->name.c_str() : in.first.c_str()), held, in.second);
        ui.Text(line, dx, y, TextSize::Small,
                enough ? Palette::Xp : SDL_Color{225, 130, 120, 255});
        y += 18.0f;
    }

    y += 10.0f;
    ui.Text(std::to_string(r->craft_xp) + " " + SkillName(skill) + " XP", dx, y, TextSize::Small,
            Palette::TextDim);

    const string verb = cauldron ? " brew" : anvil ? " smith" : fire ? " cook" : loom ? " weave"
                      : rack ? " cut" : " craft";
    ui.Text(input.PromptFor(Action::Confirm) + verb + "     " +
            input.PromptFor(Action::Sprint) + " + " + input.PromptFor(Action::Confirm) + verb + " all     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Enchanting
//
//  Like the crafting panel, but the thing made is not on a list: it is one of
//  the player's own pieces, with a charm worked into it. So the left-hand list
//  is the charms, and the right-hand side says what the chosen one does, what
//  it costs, and which piece in the bag it would go into -- stepped through
//  with left and right, since a bag can hold three rings.
// =============================================================================

void Game::UpdateEnchanting() {
    const vector<const EnchantDef*> list = items.Enchantments();
    MoveCursor(enchant_cursor, static_cast<int>(list.size()));
    Player& p = world->player;
    const EnchantDef* e = list.empty() ? nullptr
        : list[std::clamp(enchant_cursor, 0, static_cast<int>(list.size()) - 1)];
    const vector<int> targets = e ? ::Enchanting::Targets(items, *e, p.inventory) : vector<int>{};
    const int n = static_cast<int>(targets.size());
    if (n > 0) {
        if (input.MenuRight()) { enchant_target = (enchant_target + 1) % n; Audio::Play(Sfx::UiMove); }
        if (input.MenuLeft())  { enchant_target = (enchant_target + n - 1) % n; Audio::Play(Sfx::UiMove); }
        enchant_target = std::clamp(enchant_target, 0, n - 1);
    } else {
        enchant_target = 0;
    }

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && e) {
        const SDL_Color bad = {235, 150, 120, 255};
        if (!world->KnowsEnchantment(e->id)) {
            PushToast("You have not learned that enchantment yet.", bad);
            Audio::Play(Sfx::UiError);
        } else if (p.skills.Level(SKILL_MAGIC) < e->level) {
            PushToast("Needs Magic " + std::to_string(e->level) + ".", bad);
            Audio::Play(Sfx::UiError);
        } else {
            string why;
            const int slot = n > 0 ? targets[enchant_target] : -1;
            const string piece = slot >= 0 ? p.inventory.Slot(slot).id : string();
            if (::Enchanting::Work(items, *e, p.inventory, slot, why)) {
                p.GrantXp(SKILL_MAGIC, e->xp);
                const ItemDef* made = items.Get(items.EnchantedId(piece, e->id));
                PushToast("Enchanted: " + (made ? made->name : piece) + ".", Palette::Xp);
                Audio::Play(Sfx::SpellCast);
                quests->RefreshCollectObjectives(p.inventory);
            } else {
                PushToast(why, bad);
                Audio::Play(Sfx::UiError);
            }
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawEnchanting() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 660.0f, 450.0f);
    ui.Panel(panel);
    ui.Text(craft_title.empty() ? "Enchanting Table" : craft_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const Player& p = world->player;
    ui.Text("Magic " + std::to_string(p.skills.Level(SKILL_MAGIC)),
            panel.x + panel.w - 24.0f, panel.y + 24.0f, TextSize::Small,
            Palette::TextDim, Align::Right);
    ui.Text("A charm worked into a worn piece, for Magic. Each is learned before it is worked.",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 50.0f, TextSize::Small,
            Palette::TextDim, Align::Center);

    const vector<const EnchantDef*> list = items.Enchantments();
    if (list.empty()) {
        ui.Text("Nothing to work here.", panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f, TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    const float list_w = 250.0f, row_h = 34.0f;
    constexpr int SHOWN = 9;
    const int count = static_cast<int>(list.size());
    const int first = std::clamp(enchant_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, panel.y + 46.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, panel.y + 62.0f + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const EnchantDef* e = list[i];
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + (i - first) * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (i == enchant_cursor);
        const bool known = world->KnowsEnchantment(e->id);
        const bool unlocked = known && p.skills.Level(SKILL_MAGIC) >= e->level;
        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        ui.Text(known ? e->name : string("Unknown enchantment"), row.x + 10.0f, row.y + 5.0f,
                TextSize::Small,
                !unlocked ? SDL_Color{120, 110, 100, 255}
                          : (selected ? Palette::Highlight : Palette::Text));
        ui.Text("Lv " + std::to_string(e->level), row.x + row.w - 8.0f,
                row.y + 5.0f, TextSize::Small, Palette::TextDim, Align::Right);
    }

    // The chosen charm: what it does, what it fits, what it costs, and the
    // piece it would go into.
    const EnchantDef* e = list[std::clamp(enchant_cursor, 0, count - 1)];
    const float dx = panel.x + list_w + 40.0f;
    const float dw = panel.w - list_w - 64.0f;
    float y = panel.y + 62.0f;
    const bool known = world->KnowsEnchantment(e->id);

    ui.Text(known ? e->name : string("Unknown enchantment"), dx, y, TextSize::Body, Palette::Highlight);
    y += 28.0f;
    if (!known)
        y += ui.TextWrapped("You have not learned this yet." + (e->from.empty() ? string("") : " " + e->from),
                            dx, y, dw, TextSize::Small, {235, 150, 120, 255}) + 8.0f;
    if (!e->text.empty())
        y += ui.TextWrapped(e->text, dx, y, dw, TextSize::Small, Palette::TextDim) + 8.0f;
    {
        string fits = "Fits: ";
        for (size_t i = 0; i < e->slots.size(); ++i)
            fits += string(i ? ", " : "") + EquipSlotName(e->slots[i]);
        ui.Text(fits, dx, y, TextSize::Small, Palette::Text);
        y += 22.0f;
    }

    ui.Text("Materials", dx, y, TextSize::Small, Palette::Highlight);
    y += 20.0f;
    for (const auto& in : e->inputs) {
        const ItemDef* mat = items.Get(in.first);
        const int held = p.inventory.Count(in.first);
        char line[128];
        SDL_snprintf(line, sizeof(line), "%s  %d / %d",
                     (mat ? mat->name.c_str() : in.first.c_str()), held, in.second);
        ui.Text(line, dx, y, TextSize::Small,
                held >= in.second ? Palette::Xp : SDL_Color{225, 130, 120, 255});
        y += 18.0f;
    }
    y += 10.0f;
    ui.Text(std::to_string(e->xp) + " Magic XP", dx, y, TextSize::Small, Palette::TextDim);
    y += 26.0f;

    const vector<int> targets = ::Enchanting::Targets(items, *e, p.inventory);
    if (targets.empty()) {
        ui.TextWrapped("Nothing in your pack takes this enchantment.", dx, y, dw, TextSize::Small, Palette::TextDim);
    } else {
        const int n = static_cast<int>(targets.size());
        const int which = std::clamp(enchant_target, 0, n - 1);
        const ItemStack& s = p.inventory.Slot(targets[which]);
        const ItemDef* d = items.Get(s.id);
        ui.Text("Work it into", dx, y, TextSize::Small, Palette::Highlight);
        y += 20.0f;
        char line[160];
        if (n > 1)
            SDL_snprintf(line, sizeof(line), "<  %s  >   %d of %d", d ? d->name.c_str() : s.id.c_str(), which + 1, n);
        else
            SDL_snprintf(line, sizeof(line), "%s", d ? d->name.c_str() : s.id.c_str());
        if (d && !d->icon.empty())
            if (SDL_Texture* tex = textures->Get(d->icon)) {
                const SDL_FRect ic = {dx, y - 2.0f, 22.0f, 22.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(line, dx + 28.0f, y, TextSize::Small, Palette::Xp);
    }

    ui.Text(input.PromptFor(Action::Confirm) + " enchant     left / right choose the piece     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Shops
// =============================================================================

void Game::OpenShop(const string& id) {
    if (!shop_db.Get(id)) return;
    shop_id = id;
    shop_tab = 0;
    shop_cursor = 0;
    world->shops.SetDay(world->clock.QuestDay());
    SetState(GameState::Shop);
}

vector<string> Game::ShopSellRows() const {
    vector<string> rows;
    const Inventory& bag = world->player.inventory;
    for (int i = 0; i < bag.SlotCount(); ++i) {
        const ItemStack& s = bag.Slot(i);
        if (s.Empty() || s.id == "coins") continue;
        if (std::find(rows.begin(), rows.end(), s.id) == rows.end()) rows.push_back(s.id);
    }
    return rows;
}

void Game::UpdateShop() {
    const ShopDef* shop = shop_db.Get(shop_id);
    if (!shop || input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Play);
        return;
    }

    if (input.MenuLeft() || input.MenuRight()) {
        shop_tab = 1 - shop_tab;
        shop_cursor = 0;
        Audio::Play(Sfx::UiMove);
    }

    Player& p = world->player;
    const vector<const ShopStock*> shelf = Trade::Shelf(*shop, quests);
    const vector<string> sell_rows = ShopSellRows();
    const int count = shop_tab == 0 ? static_cast<int>(shelf.size()) : static_cast<int>(sell_rows.size());
    MoveCursor(shop_cursor, count);
    shop_cursor = std::clamp(shop_cursor, 0, std::max(0, count - 1));

    if (!(input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) || count == 0) return;

    // One at a time, or ten -- or the whole stack when selling -- with sprint held.
    const bool many = input.Down(Action::Sprint);
    const string item = shop_tab == 0 ? shelf[shop_cursor]->item : sell_rows[shop_cursor];
    const ItemDef* d = items.Get(item);
    const string name = d ? d->name : item;

    TradeOutcome t;
    if (shop_tab == 0) {
        t = Trade::Buy(*shop, world->shops, p.inventory, items, quests, item, many ? 10 : 1);
        if (t.result == TradeResult::Ok)
            PushToast("Bought " + std::to_string(t.qty) + "x " + name + " for " +
                      std::to_string(t.coins) + " coins.", Palette::Xp);
    } else {
        t = Trade::Sell(*shop, p.inventory, items, item, many ? p.inventory.Count(item) : 1);
        if (t.result == TradeResult::Ok)
            PushToast("Sold " + std::to_string(t.qty) + "x " + name + " for " +
                      std::to_string(t.coins) + " coins.", Palette::Highlight);
    }

    if (t.result == TradeResult::Ok) {
        Audio::Play(Sfx::Coins);
        quests->RefreshCollectObjectives(p.inventory);
    } else {
        Audio::Play(Sfx::UiError);
        PushToast(Trade::Message(t.result), {235, 150, 120, 255});
    }
}

void Game::DrawShop() {
    const ShopDef* shop = shop_db.Get(shop_id);
    if (!shop) return;
    const Player& p = world->player;

    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 480.0f);
    ui.Panel(panel);
    ui.Text(shop->name, panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);
    ui.Text(std::to_string(p.inventory.Coins()) + " coins", panel.x + panel.w - 24.0f,
            panel.y + 24.0f, TextSize::Small, Palette::Highlight, Align::Right);

    // The two tabs.
    const float list_w = 330.0f;
    for (int t = 0; t < 2; ++t) {
        const SDL_FRect tab = {panel.x + 20.0f + t * 110.0f, panel.y + 52.0f, 100.0f, 26.0f};
        const bool on = (t == shop_tab);
        ui.Fill(tab, on ? SDL_Color{58, 46, 28, 230} : SDL_Color{30, 26, 22, 200});
        ui.Outline(tab, on ? Palette::Highlight : Palette::Border, 1.0f);
        ui.Text(t == 0 ? "Buy" : "Sell", tab.x + tab.w / 2.0f, tab.y + 4.0f, TextSize::Small,
                on ? Palette::Highlight : Palette::TextDim, Align::Center);
    }
    ui.Text("< " + string(shop_tab == 0 ? "What is on the shelf" : "What you carry") + " >",
            panel.x + 250.0f, panel.y + 56.0f, TextSize::Small, Palette::TextDim);

    const vector<const ShopStock*> shelf = Trade::Shelf(*shop, quests);
    const vector<string> sell_rows = ShopSellRows();
    const int count = shop_tab == 0 ? static_cast<int>(shelf.size()) : static_cast<int>(sell_rows.size());

    if (count == 0) {
        ui.Text(shop_tab == 0 ? "Nothing on the shelf for you yet." : "You have nothing to sell.",
                panel.x + 20.0f + list_w / 2.0f, panel.y + 200.0f, TextSize::Body,
                Palette::TextDim, Align::Center);
    }

    const float row_h = 34.0f, top = panel.y + 92.0f;
    constexpr int SHOWN = 9;
    const int first = std::clamp(shop_cursor - SHOWN / 2, 0, std::max(0, count - SHOWN));
    if (first > 0)
        ui.Text("^", panel.x + 20.0f + list_w / 2.0f, top - 16.0f, TextSize::Small, Palette::TextDim, Align::Center);
    if (first + SHOWN < count)
        ui.Text("v", panel.x + 20.0f + list_w / 2.0f, top + SHOWN * row_h - 2.0f,
                TextSize::Small, Palette::TextDim, Align::Center);

    for (int i = first; i < count && i < first + SHOWN; ++i) {
        const string id = shop_tab == 0 ? shelf[i]->item : sell_rows[i];
        const ItemDef* d = items.Get(id);
        const SDL_FRect row = {panel.x + 20.0f, top + (i - first) * row_h, list_w, row_h - 4.0f};
        const bool selected = (i == shop_cursor);
        if (selected) {
            ui.Fill(row, {58, 46, 28, 210});
            ui.Outline(row, Palette::Highlight, 1.0f);
        }
        if (d && !d->icon.empty())
            if (SDL_Texture* tex = textures->Get(d->icon)) {
                const SDL_FRect ic = {row.x + 4.0f, row.y + 3.0f, 24.0f, 24.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }

        string right;
        SDL_Color right_c = Palette::Text;
        bool dim = false;
        if (shop_tab == 0) {
            const int left = world->shops.Remaining(*shop, id);
            right = d ? std::to_string(Trade::BuyPrice(*shop, *d)) + "c" : "";
            right_c = (d && p.inventory.Coins() >= Trade::BuyPrice(*shop, *d)) ? Palette::Highlight
                                                                                : SDL_Color{225, 130, 120, 255};
            dim = (left == 0);
            ui.Text(left == 0 ? "sold out" : ("x" + std::to_string(left)),
                    row.x + row.w - 64.0f, row.y + 6.0f, TextSize::Small, Palette::TextDim, Align::Right);
        } else {
            const int offer = d ? Trade::SellPrice(*shop, items, *d) : 0;
            right = offer > 0 ? std::to_string(offer) + "c" : "--";
            right_c = offer > 0 ? Palette::Xp : Palette::TextDim;
            dim = (offer == 0);
            ui.Text("x" + std::to_string(p.inventory.Count(id)), row.x + row.w - 64.0f, row.y + 6.0f,
                    TextSize::Small, Palette::TextDim, Align::Right);
        }
        ui.Text(d ? d->name : id, row.x + 34.0f, row.y + 6.0f, TextSize::Small,
                dim ? SDL_Color{120, 110, 100, 255} : (selected ? Palette::Highlight : Palette::Text));
        ui.Text(right, row.x + row.w - 8.0f, row.y + 6.0f, TextSize::Small, right_c, Align::Right);
    }

    // Detail for the highlighted row.
    if (count > 0) {
        const int index = std::clamp(shop_cursor, 0, count - 1);
        const string id = shop_tab == 0 ? shelf[index]->item : sell_rows[index];
        if (const ItemDef* d = items.Get(id)) {
            const float dx = panel.x + list_w + 44.0f;
            const float dw = panel.w - list_w - 68.0f;
            float y = top;
            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 28.0f;
            y += ui.TextWrapped(d->description, dx, y, dw, TextSize::Small, Palette::TextDim) + 8.0f;
            if (!d->requirements.empty()) {
                string req = "To use: ";
                for (const auto& rq : d->requirements)
                    req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
                ui.Text(req, dx, y, TextSize::Small, Palette::Text);
                y += 22.0f;
            }
            // The numbers, and what they would be instead of what is on: a
            // shelf of swords is a row of names until it says which is better
            // than the one you came in with.
            y += DrawItemStats(*d, dx, y, dw);
            ui.Text("You carry " + std::to_string(p.inventory.Count(id)), dx, y, TextSize::Small, Palette::Text);
            y += 20.0f;

            if (shop_tab == 0) {
                ui.Text("Price " + std::to_string(Trade::BuyPrice(*shop, *d)) + " coins", dx, y,
                        TextSize::Small, Palette::Highlight);
                y += 20.0f;
                ui.Text(std::to_string(world->shops.Remaining(*shop, id)) + " left today; restocks at dawn",
                        dx, y, TextSize::Small, Palette::TextDim);
            } else {
                const int offer = Trade::SellPrice(*shop, items, *d);
                if (offer > 0) {
                    ui.Text("They pay " + std::to_string(offer) + " coins each", dx, y, TextSize::Small, Palette::Xp);
                    y += 20.0f;
                    // Where it would fetch more, so selling is a choice.
                    int best = offer;
                    string where;
                    for (const auto& kv : shop_db.All()) {
                        const int o = Trade::SellPrice(kv.second, items, *d);
                        if (o > best) { best = o; where = kv.second.name; }
                    }
                    if (!where.empty())
                        y += ui.TextWrapped(where + " would pay " + std::to_string(best) + ".",
                                            dx, y, dw, TextSize::Small, Palette::TextDim);
                } else {
                    ui.TextWrapped("This trader does not deal in that.", dx, y, dw, TextSize::Small,
                                   {225, 130, 120, 255});
                }
            }
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + (shop_tab == 0 ? " buy     " : " sell     ") +
            input.PromptFor(Action::Sprint) + " + " + input.PromptFor(Action::Confirm) +
            (shop_tab == 0 ? " buy 10     " : " sell all     ") +
            string(input.ActiveDevice() == InputMode::Controller ? "Left/Right" : "A/D") + " buy / sell     " +
            input.PromptFor(Action::Back) + " leave",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}
