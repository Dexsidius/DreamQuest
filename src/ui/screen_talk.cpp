// =============================================================================
//  The Game class's screens, continued: dialogue, boards, notes, the bed, the waystones and the totem ring
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
//  Dialogue
// =============================================================================

void Game::UpdateDialogue(float dt) {
    dialogue.Update(dt);

    const DialogueContext dctx = MakeDialogueContext();

    if (!dialogue.Active()) {
        for (auto& n : world->npcs) n->talking = false;
        SetState(GameState::Play);
        return;
    }

    const int option_count = static_cast<int>(dialogue.VisibleOptions().size());
    if (input.MenuDown()) dialogue.MoveSelection(1);
    if (input.MenuUp())   dialogue.MoveSelection(-1);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        // First press finishes the text reveal; the next one picks an answer.
        if (!dialogue.FullyRevealed()) {
            dialogue.SkipReveal();
        } else if (option_count == 0) {
            dialogue.End();
        } else {
            dialogue.Choose(dctx);
            HandleDialogueActions(dialogue.TakeActions());

            // So does asking for the day's orders.
            if (!pending_orders.empty()) {
                const string npc = pending_orders;
                string name = npc;
                for (auto& n : world->npcs) if (n->Id() == npc) name = n->Name();
                pending_orders.clear();
                dialogue.End();
                for (auto& n : world->npcs) n->talking = false;
                OpenOrders(npc, name);
                return;
            }

            // "Show me your wares" closes the conversation and opens the shop.
            if (!pending_shop.empty()) {
                const string id = pending_shop;
                pending_shop.clear();
                dialogue.End();
                for (auto& n : world->npcs) n->talking = false;
                OpenShop(id);
                return;
            }

            for (const string& id : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(id))
                    PushToast("Quest started: " + d->name, Palette::Xp);
            for (const string& id : quests->TakeJustCompleted())
                GrantQuestRewards(id);
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) dialogue.End();
}

void Game::DrawDialogue() {
    const DialogueNode* node = dialogue.Node();
    if (!node) return;

    // Tall enough for the whole line and every answer under it: a smith with
    // orders, a shop and a lesson to offer had her answers printed over her
    // own words. Measured on the full text, so the box does not grow as it types.
    const float box_w = ui.ViewWidth() - 80.0f;
    const float text_h = ui.WrappedHeight(node->text, box_w - 48.0f, TextSize::Body);
    const float needed = 28.0f + text_h + 14.0f + dialogue.VisibleOptions().size() * 26.0f + 16.0f;
    const float box_h = std::max(210.0f, needed);
    const SDL_FRect box = {40.0f, ui.ViewHeight() - box_h - 30.0f, box_w, box_h};
    ui.Panel(box);

    // Speaker plate overlapping the top edge.
    const string speaker = dialogue.SpeakerName();
    if (!speaker.empty()) {
        const SDL_FPoint size = ui.Measure(speaker, TextSize::Body);
        const SDL_FRect plate = {box.x + 22.0f, box.y - 16.0f, size.x + 28.0f, 32.0f};
        ui.Fill(plate, Palette::PanelLight);
        ui.Outline(plate, Palette::Border, 1.0f);
        ui.Text(speaker, plate.x + 14.0f, plate.y + 5.0f, TextSize::Body, Palette::Highlight);
    }

    const float text_y = box.y + 28.0f;
    ui.TextWrapped(dialogue.RevealedText(), box.x + 24.0f, text_y, box.w - 48.0f,
                   TextSize::Body, Palette::Text);

    if (!dialogue.FullyRevealed()) {
        ui.Text(input.PromptFor(Action::Confirm) + " to skip",
                box.x + box.w - 22.0f, box.y + box.h - 26.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
        return;
    }

    const auto& options = dialogue.VisibleOptions();
    if (options.empty()) {
        ui.Text(input.PromptFor(Action::Confirm) + " to continue",
                box.x + box.w - 22.0f, box.y + box.h - 26.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
        return;
    }

    // Answers stack up from the bottom of the box.
    const float row_h = 26.0f;
    float y = box.y + box.h - 16.0f - options.size() * row_h;
    for (size_t i = 0; i < options.size(); ++i) {
        const bool selected = (static_cast<int>(i) == dialogue.Selected());
        if (selected)
            ui.Text(">", box.x + 26.0f, y, TextSize::Body, Palette::Highlight);
        ui.Text(options[i]->text, box.x + 46.0f, y, TextSize::Body,
                selected ? Palette::Highlight : Palette::TextDim);
        y += row_h;
    }
}

// =============================================================================
//  Mission board
// =============================================================================

void Game::UpdateBoard() {
    // Only offer what the player can actually take on right now.
    vector<string> available;
    for (const string& id : board_quests)
        if (quests->CanStart(id, world->player.skills)) available.push_back(id);

    MoveCursor(board_cursor, static_cast<int>(available.size()));

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) &&
        !available.empty()) {
        const string& id = available[std::clamp(board_cursor, 0,
                                                static_cast<int>(available.size()) - 1)];
        if (quests->Start(id)) {
            for (const string& started : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(started))
                    PushToast("Quest started: " + d->name, Palette::Xp);
            board_cursor = 0;
        }
    }

    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawBoard() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 720.0f, 440.0f);
    ui.Panel(panel);
    ui.Text(board_title.empty() ? "Mission Board" : board_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    vector<string> available;
    for (const string& id : board_quests)
        if (quests->CanStart(id, world->player.skills)) available.push_back(id);

    if (available.empty()) {
        ui.Text(board_orders ? "No orders for you today." : "Nothing new is pinned up today.",
                panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f - 20.0f, TextSize::Body, Palette::TextDim,
                Align::Center);
        ui.Text(board_orders ? "New orders come in at dawn. Anything you have taken is in your journal."
                             : "Come back after you have finished what you already took on.",
                panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        const float row_h = 40.0f;
        const float list_w = 300.0f;

        for (size_t i = 0; i < available.size() && i < 8; ++i) {
            const SDL_FRect row = {panel.x + 20.0f, panel.y + 62.0f + i * row_h,
                                   list_w, row_h - 5.0f};
            const bool selected = (static_cast<int>(i) == board_cursor);
            if (selected) {
                ui.Fill(row, {58, 46, 28, 210});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests->Definition(available[i]);
            ui.Text(d ? d->name : available[i], row.x + 10.0f, row.y + 3.0f,
                    TextSize::Small, selected ? Palette::Highlight : Palette::Text);
            if (d)
                ui.Text((d->daily ? string(board_orders ? "order   " : "daily   ") : string("")) +
                        "Lv " + std::to_string(d->recommended_level),
                        row.x + row.w - 8.0f, row.y + 3.0f, TextSize::Small,
                        d->daily ? Palette::Xp : Palette::TextDim, Align::Right);
        }

        const int index = std::clamp(board_cursor, 0, static_cast<int>(available.size()) - 1);
        if (const QuestDef* d = quests->Definition(available[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            ui.Text(d->name, dx, y, TextSize::Body, Palette::Highlight);
            y += 30.0f;
            if (d->daily) {
                ui.Text(board_orders ? "Order: new orders at dawn" : "Daily: new notices at dawn",
                        dx, y, TextSize::Small, Palette::Xp);
                y += 22.0f;
            }
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 14.0f;
            // What it asks for against what is in the pack.
            if (!d->stages.empty() && (d->stages[0].type == ObjectiveType::Deliver ||
                                       d->stages[0].type == ObjectiveType::Collect)) {
                const QuestStage& st = d->stages[0];
                const ItemDef* want = items.Get(st.target);
                const int held = world->player.inventory.Count(st.target);
                ui.Text((want ? want->name : st.target) + ": you carry " + std::to_string(held) +
                        " of " + std::to_string(st.count), dx, y, TextSize::Small,
                        held >= st.count ? Palette::Xp : Palette::TextDim);
                y += 22.0f;
            }
            if (!d->requirements.empty()) {
                string req = "Needs ";
                for (const auto& rq : d->requirements)
                    req += string(SkillName(rq.first)) + " " + std::to_string(rq.second) + "  ";
                ui.Text(req, dx, y, TextSize::Small, Palette::TextDim);
                y += 22.0f;
            }

            if (!d->rewards.xp.empty() || d->rewards.coins > 0) {
                ui.Text("Reward", dx, y, TextSize::Small, Palette::Highlight);
                y += 20.0f;
                for (const auto& xp : d->rewards.xp) {
                    ui.Text(std::to_string(xp.second) + " " + SkillName(xp.first) + " XP",
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                if (d->rewards.coins > 0)
                    ui.Text(std::to_string(d->rewards.coins) + " coins", dx, y,
                            TextSize::Small, Palette::Xp);
            }
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + " accept     " +
            input.PromptFor(Action::Back) + " leave",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  Notes and signs
// =============================================================================

void Game::UpdateNote() {
    const bool offers_quest = !note_quest.empty() &&
                              quests->CanStart(note_quest, world->player.skills);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (offers_quest && quests->Start(note_quest)) {
            for (const string& started : quests->TakeJustStarted())
                if (const QuestDef* d = quests->Definition(started))
                    PushToast("Quest started: " + d->name, Palette::Xp);
        }
        SetState(GameState::Play);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawNote() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 380.0f);

    // Parchment, rather than the usual dark panel.
    ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
    ui.Fill(panel, {214, 197, 158, 250});
    ui.Outline(panel, {120, 96, 58, 255}, 2.0f);

    ui.Text(note_title, panel.x + panel.w / 2.0f, panel.y + 26.0f, TextSize::Large,
            {68, 48, 28, 255}, Align::Center);
    ui.Fill({panel.x + 40.0f, panel.y + 68.0f, panel.w - 80.0f, 1.0f}, {140, 116, 78, 255});

    ui.TextWrapped(note_text, panel.x + 40.0f, panel.y + 86.0f, panel.w - 80.0f,
                   TextSize::Body, {52, 38, 24, 255});

    const bool offers_quest = !note_quest.empty() &&
                              quests->CanStart(note_quest, world->player.skills);
    ui.Text(offers_quest ? (input.PromptFor(Action::Confirm) + " look into this")
                         : (input.PromptFor(Action::Confirm) + " put it away"),
            panel.x + panel.w / 2.0f, panel.y + panel.h - 34.0f, TextSize::Small,
            {96, 74, 44, 255}, Align::Center);
}

// =============================================================================
//  The bed's question
// =============================================================================
//
// Two rows on the parchment a note is read on. The wording is the same alone
// and in company: in co-op each player answers for themselves.

namespace {
struct SleepRow { const char* name; const char* what; };
constexpr SleepRow kSleepRows[2] = {
    {"Sleep through the night", "Wake here at dawn, rested. No dream."},
    {"Go into the Reverie",     "Dream until dawn. What you find there, you keep."},
};
}

// =============================================================================
//  Waystones
//
//  Three old stones, one in each town and none anywhere else. Each is asleep
//  until somebody puts a hand on it, and a woken one is a door to every other
//  woken one -- so the road to a town is walked once, and the wilds and the
//  dungeons are always walked. There is no fare: the price of a waystone is
//  having got there.
// =============================================================================

namespace {
struct Waystone { const char* id; const char* map; const char* name; const char* note; };
const Waystone kWaystones[] = {
    {"waystone_havenbrook", "town_havenbrook", "Havenbrook", "the market town on the southern road"},
    {"waystone_mossvale",   "mossvale",        "Mossvale",   "the logging village under the Whisperwood"},
    {"waystone_fernhollow", "fernhollow",      "Fernhollow", "the hamlet on still water"},
};
constexpr int kWaystoneCount = 3;
}

void Game::UpdateTravel() {
    MoveCursor(travel_cursor, kWaystoneCount);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const Waystone& to = kWaystones[std::clamp(travel_cursor, 0, kWaystoneCount - 1)];
        if (travel_from == to.id) {
            PushToast("You are standing at it.", Palette::TextDim);
            Audio::Play(Sfx::UiError);
        } else if (!world->Flagged(to.id)) {
            PushToast("The stone at " + string(to.name) + " is still asleep. It has to be woken by hand.",
                      {235, 190, 120, 255});
            Audio::Play(Sfx::UiError);
        } else {
            SetState(GameState::Play);
            if (world->RequestTransition(to.map, "waystone")) Audio::Play(Sfx::QuestStart);
        }
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawTravel() {
    ui.Dim(0.55f);
    const float row_h = 58.0f;
    const SDL_FRect panel = CenteredPanel(ui, 520.0f, 150.0f + kWaystoneCount * row_h);
    ui.Panel(panel);
    ui.Text("Waystone", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);
    ui.Text("A woken stone opens on every other you have woken.", panel.x + panel.w / 2.0f,
            panel.y + 52.0f, TextSize::Small, Palette::TextDim, Align::Center);

    const SDL_Color cold{150, 220, 255, 255};
    for (int i = 0; i < kWaystoneCount; ++i) {
        const Waystone& w = kWaystones[i];
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 84.0f + i * row_h, panel.w - 40.0f, row_h - 8.0f};
        const bool selected = (i == travel_cursor);
        const bool here = travel_from == w.id;
        const bool awake = world->Flagged(w.id);
        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, selected ? Palette::Highlight : Palette::BorderDim, selected ? 2.0f : 1.0f);
        // A lit or a dark eye, the way the stone itself shows it.
        ui.Fill({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, awake ? cold : SDL_Color{52, 50, 56, 255});
        ui.Outline({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, Palette::BorderDim, 1.0f);
        ui.Text(w.name, row.x + 44.0f, row.y + 6.0f, TextSize::Body,
                !awake ? SDL_Color{120, 110, 100, 255} : (selected ? Palette::Highlight : Palette::Text));
        ui.Text(w.note, row.x + 44.0f, row.y + 28.0f, TextSize::Small, Palette::TextDim);
        ui.Text(here ? "you are here" : (awake ? "awake" : "asleep"), row.x + row.w - 12.0f, row.y + 8.0f,
                TextSize::Small, here ? Palette::Highlight : (awake ? cold : SDL_Color{150, 110, 100, 255}),
                Align::Right);
    }

    ui.Text(input.PromptFor(Action::Confirm) + " go     " + input.PromptFor(Action::Back) + " stay",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}

// =============================================================================
//  The ring in the house at Mossvale
//
//  What a boss leaves the fifteenth time is a totem, and the ring in the floor
//  of the player's own house is where one is stood. Touched, it gives its
//  blessing for the rest of that day -- wherever they go, and through a death --
//  and the next day it is a carving in a ring until it is touched again.
//  One at a time: standing another in the ring puts the first back in the bag.
// =============================================================================

vector<string> Game::TotemChoices() const {
    vector<string> out;
    const Player& p = world->player;
    if (!p.talents.PlacedTotem().empty()) out.push_back(p.talents.PlacedTotem());
    for (const TotemDef& t : skill_trees.Totems())
        if (p.inventory.Has(t.item) && std::find(out.begin(), out.end(), t.item) == out.end()) out.push_back(t.item);
    return out;
}

void Game::UpdateTotemRing() {
    Player& p = world->player;
    const vector<string> have = TotemChoices();
    if (!have.empty()) MoveCursor(totem_cursor, static_cast<int>(have.size()));
    totem_cursor = std::clamp(totem_cursor, 0, std::max(0, static_cast<int>(have.size()) - 1));
    const int today = world->clock.QuestDay();

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && !have.empty()) {
        const string pick = have[totem_cursor];
        const TotemDef* def = skill_trees.Totem(pick);
        const ItemDef* thing = items.Get(pick);
        const string what = thing ? thing->name : pick;
        if (pick == p.talents.PlacedTotem() && p.talents.TotemAwake()) {
            PushToast("It is awake, and will be until dawn.", Palette::TextDim);
            Audio::Play(Sfx::UiError);
            return;
        }
        if (pick == p.talents.PlacedTotem()) {
            // Standing there, asleep: a hand on it is all it wants.
            p.talents.PlaceTotem(pick, today);
        } else {
            // Out of the bag and into the ring; what was in the ring, into the
            // bag, which has the room the other one just left.
            if (!p.inventory.Remove(pick, 1)) return;
            const string was = p.talents.PlaceTotem(pick, today);
            if (!was.empty()) p.inventory.Add(was, 1);
            totem_cursor = 0;
        }
        // Some blessings are health, or mana: the pools are what they now are.
        p.SyncHitpoints();
        p.SyncMana();
        // And the house takes its colours: a breath of its light, as it wakes.
        if (def && def->house) world->Flash(def->light, 0.35f);
        // Two lines: a toast is one line from the right-hand edge, and the
        // Pit Lord's blessing alone is most of a narrow window.
        PushToast(what + " wakes, until dawn.", {255, 214, 120, 255});
        if (def) PushToast(def->name + ": " + def->text + ".", {255, 214, 120, 255});
        Audio::Play(Sfx::QuestStart);
        return;
    }
    if (input.Pressed(Action::Drop) && !p.talents.PlacedTotem().empty()) {
        if (p.inventory.Full()) {
            PushToast("There is no room in your pack to carry it.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        const string was = p.talents.TakeTotem();
        p.inventory.Add(was, 1);
        p.SyncHitpoints();
        p.SyncMana();
        const ItemDef* thing = items.Get(was);
        PushToast("You lift " + (thing ? thing->name : was) + " out of the ring. Its blessing goes with it.", Palette::TextDim);
        Audio::Play(Sfx::UiBack);
        totem_cursor = 0;
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawTotemRing() {
    ui.Dim(0.55f);
    const Player& p = world->player;
    const vector<string> have = TotemChoices();
    const float row_h = 46.0f;
    // Room for as many rows as the window has, and no fewer than three so the
    // panel is not a letterbox with one totem in it.
    const float fit_h = ui.ViewHeight() - 16.0f;
    const int   rows = std::max(3, static_cast<int>(have.size()));
    const float want_h = 176.0f + rows * row_h;
    const SDL_FRect panel = CenteredPanel(ui, std::min(620.0f, ui.ViewWidth() - 16.0f), std::min(want_h, fit_h));
    ui.Panel(panel);
    ui.Text("The Ring", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large, Palette::Highlight, Align::Center);
    const float x = panel.x + 24.0f, w = panel.w - 48.0f;
    float y = panel.y + 52.0f;
    y += ui.TextWrapped("One totem stands here at a time. Touched, it gives its blessing until dawn, wherever you go.",
                        x, y, w, TextSize::Small, Palette::TextDim) + 10.0f;

    if (have.empty()) {
        ui.TextWrapped("You have no totems. A boss leaves its totem the " + std::to_string(Talents::TOTEM_KILLS) +
                       "th time you bring it down -- it is back every dawn. The Boons page of your skills keeps count.",
                       x + 30.0f, y + 26.0f, w - 60.0f, TextSize::Small, Palette::Text);
    }
    // The cursor kept in view, in a window that may be shorter than the list.
    const int fit = std::max(1, static_cast<int>((panel.y + panel.h - 56.0f - y) / row_h));
    const int first = std::clamp(totem_cursor - fit + 1, 0, std::max(0, static_cast<int>(have.size()) - fit));
    for (int i = first; i < static_cast<int>(have.size()) && i < first + fit; ++i) {
        const TotemDef* def = skill_trees.Totem(have[i]);
        const ItemDef* thing = items.Get(have[i]);
        const SDL_FRect row = {x, y, w, row_h - 6.0f};
        const bool selected = i == totem_cursor;
        const bool standing = have[i] == p.talents.PlacedTotem();
        const bool awake = standing && p.talents.TotemAwake();
        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, awake ? SDL_Color{255, 214, 120, 255} : (selected ? Palette::Highlight : Palette::BorderDim),
                   selected || awake ? 2.0f : 1.0f);
        if (thing && !thing->icon.empty())
            if (SDL_Texture* tex = textures->Get(thing->icon)) {
                const SDL_FRect ic = {row.x + 6.0f, row.y + 4.0f, 32.0f, 32.0f};
                SDL_RenderTexture(renderer, tex, nullptr, &ic);
            }
        ui.Text(thing ? thing->name : have[i], row.x + 46.0f, row.y + 3.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        ui.Text(standing ? (awake ? "awake until dawn" : "in the ring, asleep") : "in your pack",
                row.x + row.w - 10.0f, row.y + 5.0f, TextSize::Small,
                awake ? SDL_Color{255, 214, 120, 255} : (standing ? SDL_Color{170, 160, 190, 255} : Palette::TextDim),
                Align::Right);
        if (def) ui.Text(def->name + ": " + def->text, row.x + 46.0f, row.y + 22.0f, TextSize::Small, Palette::TextDim);
        y += row_h;
    }

    string foot;
    if (!have.empty()) {
        const bool standing = have[std::clamp(totem_cursor, 0, static_cast<int>(have.size()) - 1)] == p.talents.PlacedTotem();
        foot = input.PromptFor(Action::Confirm) + (standing ? " wake it" : (p.talents.PlacedTotem().empty() ? " stand it here" : " stand it here instead")) + "     ";
        if (!p.talents.PlacedTotem().empty()) foot += input.PromptFor(Action::Drop) + " lift it out     ";
    }
    foot += input.PromptFor(Action::Back) + " leave";
    ui.Text(foot, panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

void Game::UpdateSleepPrompt() {
    MoveCursor(sleep_cursor, 2);

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        const World::SleepChoice how = sleep_cursor == 0 ? World::SleepChoice::Through
                                                         : World::SleepChoice::Reverie;
        // An inn's bed is paid for. Your own, and the ground, are not.
        Player& sleeper = world->player;
        if (sleep_fee > 0 && sleeper.inventory.Coins() < sleep_fee) {
            PushToast("A bed here is " + std::to_string(sleep_fee) + " coins, and you have " +
                      std::to_string(sleeper.inventory.Coins()) + ". A camp costs nothing.", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        SetState(GameState::Play);
        if (world->Sleep(how, ctx) && sleep_fee > 0) {
            sleeper.inventory.SpendCoins(sleep_fee);
            PushToast("Paid " + std::to_string(sleep_fee) + " coins for the bed.", Palette::TextDim);
        }
        sleep_fee = 0;
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawSleepPrompt() {
    ui.Dim(0.55f);
    const SDL_FRect panel = CenteredPanel(ui, 520.0f, 300.0f);

    ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
    ui.Fill(panel, {214, 197, 158, 250});
    ui.Outline(panel, {120, 96, 58, 255}, 2.0f);

    ui.Text(sleep_title.empty() ? string("A bed for the night") : sleep_title,
            panel.x + panel.w / 2.0f, panel.y + 24.0f, TextSize::Large, {68, 48, 28, 255}, Align::Center);
    ui.Fill({panel.x + 40.0f, panel.y + 64.0f, panel.w - 80.0f, 1.0f}, {140, 116, 78, 255});

    // How much night is left to spend, which is what the choice is about.
    const float to_dawn = world->clock.HoursToDawn();
    const int hours_left = std::max(1, static_cast<int>(to_dawn + 0.5f));
    ui.Text("It is " + world->clock.TimeText() + ". Dawn is " + std::to_string(hours_left) +
            (hours_left == 1 ? " hour off." : " hours off."),
            panel.x + panel.w / 2.0f, panel.y + 76.0f, TextSize::Small, {96, 74, 44, 255}, Align::Center);

    for (int i = 0; i < 2; ++i) {
        const SDL_FRect row = {panel.x + 40.0f, panel.y + 108.0f + i * 66.0f, panel.w - 80.0f, 56.0f};
        const bool on = (i == sleep_cursor);
        if (on) {
            ui.Fill(row, {236, 222, 184, 255});
            ui.Outline(row, {120, 96, 58, 255}, 2.0f);
        } else {
            ui.Outline(row, {170, 148, 108, 255}, 1.0f);
        }
        ui.Text(kSleepRows[i].name, row.x + 16.0f, row.y + 8.0f, TextSize::Body,
                on ? SDL_Color{52, 34, 16, 255} : SDL_Color{88, 68, 42, 255});
        ui.Text(kSleepRows[i].what, row.x + 16.0f, row.y + 31.0f, TextSize::Small,
                on ? SDL_Color{96, 74, 44, 255} : SDL_Color{128, 106, 74, 255});
    }

    ui.Text(input.PromptFor(Action::Confirm) + " lie down     " + input.PromptFor(Action::Back) + " stay up",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small,
            {96, 74, 44, 255}, Align::Center);
}
