// =============================================================================
//  The Game class's screens, continued: dialogue, boards, notes, the bed, the waystones and the totem ring
//
//  One class in several files, cut along the banners screens.cpp always had:
//  screens.cpp keeps the menus; the rest is screen_hud, screen_inventory,
//  screen_skills, screen_journal, screen_talk and screen_trade. What two of
//  them share is in screens_shared.h.
// =============================================================================
#include "../systems/gathering.h"
#include "../systems/waystones.h"
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

namespace {

// The beast a Guild bounty is on, the way the Guild's page shows it: its own
// figure, idling, in its own colours, on a dark stage -- as big as whole
// pixels let it be in `box`, its drawn part (not its frame's empty sky)
// centred. A beast too big for the box at its own size is drawn smaller.
void DrawBeast(UI& ui, SDL_Renderer* r, TextureCache& cache, const SpriteLibrary& sprites, const EnemyDef& beast,
               const SDL_FRect& box, float t) {
    constexpr int kBands = 8;
    for (int i = 0; i < kBands; ++i) {
        const float k = static_cast<float>(i) / (kBands - 1);
        const auto mix = [&](int a, int b) { return static_cast<Uint8>(a + (b - a) * k); };
        ui.Fill({box.x, box.y + box.h * i / kBands, box.w, box.h / kBands + 1.0f},
                {mix(22, 54), mix(18, 45), mix(20, 37), 245});
    }
    ui.Outline(box, Palette::BorderDim, 1.0f);

    const SpriteDef* def = sprites.Get(beast.sprite);
    const AnimClip* idle = def ? def->Find("idle") : nullptr;
    if (!idle) return;
    const SDL_Point sheet = cache.Size(idle->sheet);
    const int fw = sheet.x / std::max(1, idle->frames), fh = sheet.y / std::max(1, idle->rows > 0 ? idle->rows : def->rows);
    if (fw <= 0 || fh <= 0) return;
    // Measured on the first frame facing down -- the one the figure is drawn in.
    const SDL_FRect seen = cache.OpaqueBoundsIn(idle->sheet, {0, 0, fw, fh});
    const float room = box.h - 12.0f;
    const float span = std::max({seen.w, seen.h, 1.0f});
    const float scale = span > room ? room / span : std::min(6.0f, floorf(room / span));
    const SDL_FRect dst = {floorf(box.x + box.w / 2.0f - (seen.x + seen.w / 2.0f) * scale),
                           floorf(box.y + box.h / 2.0f - (seen.y + seen.h / 2.0f) * scale), fw * scale, fh * scale};
    Sprite fig;
    fig.SetDef(def);
    fig.facing = FACE_DOWN;
    fig.Play("idle");
    fig.Update(t);
    const SDL_Rect clip = {static_cast<int>(box.x) + 1, static_cast<int>(box.y) + 1, static_cast<int>(box.w) - 2,
                           static_cast<int>(box.h) - 2};
    SDL_SetRenderClipRect(r, &clip);
    fig.DrawAt(r, cache, dst, beast.tint);
    SDL_SetRenderClipRect(r, nullptr);
}

} // namespace

vector<string> Game::BoardList() const {
    vector<string> out;
    const int combat = world->player.skills.CombatLevel();
    for (const string& id : board_quests) {
        // Only what the player can actually take on right now.
        if (!quests->CanStart(id, world->player.skills)) continue;
        const QuestDef* d = quests->Definition(id);
        if (board_in_range && !board_orders && d && !QuestLog::InRange(d->recommended_level, combat)) continue;
        out.push_back(id);
    }
    // A board reads from the easiest down. An order book keeps its own order.
    if (!board_orders)
        std::stable_sort(out.begin(), out.end(), [&](const string& a, const string& b) {
            const QuestDef* da = quests->Definition(a);
            const QuestDef* db = quests->Definition(b);
            return (da ? da->recommended_level : 0) < (db ? db->recommended_level : 0);
        });
    return out;
}

void Game::UpdateBoard() {
    // The filter: everything, or only what is within reach.
    if (!board_orders && input.Pressed(Action::Target)) {
        board_in_range = !board_in_range;
        board_cursor = 0;
        Audio::Play(Sfx::UiMove);
    }

    const vector<string> available = BoardList();
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
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 480.0f);
    ui.Panel(panel);
    ui.Text(board_title.empty() ? "Mission Board" : board_title,
            panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const vector<string> available = BoardList();
    const int combat = world->player.skills.CombatLevel();
    const float list_w = 330.0f;
    // What the filter is showing, over the list.
    if (!board_orders)
        ui.Text(board_in_range ? "Within " + std::to_string(QuestLog::LEVEL_RANGE) + " levels of your Combat (" +
                                     std::to_string(combat) + ")"
                               : string("Everything posted"),
                panel.x + 20.0f, panel.y + 56.0f, TextSize::Small, board_in_range ? Palette::Xp : Palette::TextDim);

    // The Guild's ledger: how much of it this character has closed, and
    // whether it is open to them at all -- Guild Master Orlend opens it.
    int ledger = 0, closed = 0;
    bool ledger_open = false;
    for (const string& id : board_quests) {
        const QuestDef* q = quests->Definition(id);
        if (!q || !q->guild_bounty) continue;
        ++ledger;
        if (quests->IsComplete(id)) ++closed;
        bool ready = true;
        for (const string& p : q->prerequisites) ready = ready && quests->IsComplete(p);
        ledger_open = ledger_open || ready;
    }
    if (ledger > 0 && ledger_open)
        ui.Text("Closed " + std::to_string(closed) + " of " + std::to_string(ledger), panel.x + 20.0f + list_w,
                panel.y + 56.0f, TextSize::Small, closed == ledger ? Palette::Xp : Palette::TextDim, Align::Right);

    if (available.empty()) {
        const bool filtered = board_in_range && !board_orders;
        const bool shut = ledger > 0 && !ledger_open, done = ledger > 0 && closed == ledger;
        ui.Text(board_orders ? "No orders for you today."
                : shut       ? string("The Guild's ledger is not open to you.")
                : done       ? string("Every bounty in the ledger is closed.")
                : filtered   ? "Nothing posted within " + std::to_string(QuestLog::LEVEL_RANGE) + " levels of you."
                             : string("Nothing new is pinned up today."),
                panel.x + panel.w / 2.0f,
                panel.y + panel.h / 2.0f - 20.0f, TextSize::Body, Palette::TextDim,
                Align::Center);
        ui.Text(board_orders ? "New orders come in at dawn. Anything you have taken is in your journal."
                : shut       ? string("Speak to the Guild Master.")
                : done       ? string("There is no name left in it the Guild can pay you for.")
                : filtered   ? input.PromptFor(Action::Target) + " shows everything posted."
                             : string("Come back after you have finished what you already took on."),
                panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        const float row_h = 40.0f;
        const float top = panel.y + 82.0f;
        // The window of rows the cursor is inside, so a full board scrolls --
        // a night's bounties are a dozen and more -- rather than running off.
        const int visible = 8;
        const int n = static_cast<int>(available.size());
        const int index = std::clamp(board_cursor, 0, n - 1);
        const int first = n > visible ? std::min(n - visible, std::max(0, index - visible / 2)) : 0;

        for (int k = 0; k < visible && first + k < n; ++k) {
            const int i = first + k;
            const SDL_FRect row = {panel.x + 20.0f, top + k * row_h, list_w, row_h - 5.0f};
            const bool selected = i == index;
            if (selected) {
                ui.Fill(row, {58, 46, 28, 210});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests->Definition(available[i]);
            // The tag: what it is and its level -- a bounty's in the colour of
            // how it sits against the character: within reach, beyond it, or
            // beneath them.
            string tag;
            SDL_Color tone = Palette::TextDim;
            if (d) {
                const bool bounty = d->bounty || d->guild_bounty;
                tag = string(bounty ? "bounty   " : d->daily ? (board_orders ? "order   " : "daily   ") : "") +
                      "Lv " + std::to_string(d->recommended_level);
                tone = !bounty && !d->daily ? Palette::TextDim
                     : !bounty ? Palette::Xp
                     : QuestLog::InRange(d->recommended_level, combat) ? Palette::Xp
                     : d->recommended_level > combat ? SDL_Color{235, 120, 100, 255}
                                                     : Palette::TextDim;
            }
            const float tag_w = ui.Measure(tag, TextSize::Small).x;
            ui.Text(ui.Fit(d ? d->name : available[i], row.w - 30.0f - tag_w, TextSize::Small),
                    row.x + 10.0f, row.y + 3.0f, TextSize::Small, selected ? Palette::Highlight : Palette::Text);
            if (!tag.empty())
                ui.Text(tag, row.x + row.w - 8.0f, row.y + 3.0f, TextSize::Small, tone, Align::Right);
        }
        // How far down the list the cursor is, when it does not all fit.
        if (n > visible)
            ui.Text(std::to_string(index + 1) + " of " + std::to_string(n),
                    panel.x + 20.0f + list_w, top + visible * row_h + 2.0f, TextSize::Small, Palette::TextDim,
                    Align::Right);

        if (const QuestDef* d = quests->Definition(available[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 62.0f;

            // A Guild bounty is a page of the Guild's ledger: the beast drawn
            // beside its name, and where it lairs, so the page introduces it.
            const EnemyDef* beast = nullptr;
            string lair;
            if (d->guild_bounty && !d->stages.empty()) {
                const QuestStage& st = d->stages.front();
                if (st.type == ObjectiveType::Kill) beast = enemy_db.Get(st.target);
                const auto area = waypoints.Areas().find(st.where);
                if (area != waypoints.Areas().end()) lair = area->second.name;
            }
            constexpr float kFigure = 104.0f;
            const float top = y;
            const float tw = beast ? dw - kFigure - 12.0f : dw;     // the text beside the figure
            y += ui.TextWrapped(d->name, dx, y, tw, TextSize::Body, Palette::Highlight) + 10.0f;
            if (d->guild_bounty) {
                y += ui.TextWrapped("Guild bounty: taken once, and paid the day it falls, wherever it falls.", dx, y,
                                    tw, TextSize::Small, Palette::Xp) + 4.0f;
                if (!lair.empty())
                    y += ui.TextWrapped("Lair: " + lair, dx, y, tw, TextSize::Small, Palette::TextDim) + 4.0f;
                if (beast) {
                    DrawBeast(ui, renderer, *textures, sprites, *beast, {dx + dw - kFigure, top, kFigure, kFigure},
                              state_time);
                    y = std::max(y, top + kFigure + 8.0f);
                }
            } else if (d->bounty) {
                y += ui.TextWrapped("Bounty: tonight only. It lapses at dawn if it is not done.", dx, y, dw,
                                    TextSize::Small, Palette::Xp) + 4.0f;
            } else if (d->daily) {
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

            // What it pays, the things and a choice among them included: the
            // board used to say only the experience and the coins, and a sword
            // was a surprise at the end.
            DrawQuestRewards(*d, string(), dx, y, dw);
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + " accept     " +
                (board_orders ? string()
                              : input.PromptFor(Action::Target) +
                                    (board_in_range ? " show everything     " : " only what is within reach     ")) +
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
//  The old stones: one in each town, one at the door of the player's house in
//  Mossvale, and three checkpoints out in the wild -- the Ashen Path, the top of
//  the climb onto Purgatory's Plateau, and the Bayou by the Hexmire's gate (see
//  src/systems/waystones.h). Each is asleep until somebody puts a hand on it,
//  and a woken one is a door to every other woken one -- so the road to a place
//  is walked once, and the dungeons are always walked. There is no fare: the
//  price of a waystone is having got there.
//
//  The panel has two tabs, the towns' stones and the wilds', stepped between
//  with left and right (or the panel keys, I and O, or the shoulders), and
//  opens on the tab the stone being touched is under.
// =============================================================================

void Game::UpdateTravel() {
    // Which tab it opens on is SetState's to say: see there.
    const int tab_was = travel_tab;
    if (input.MenuLeft() || input.Pressed(Action::Inventory))  travel_tab = 0;
    if (input.MenuRight() || input.Pressed(Action::Skills) || input.Pressed(Action::Ability)) travel_tab = 1;
    if (travel_tab != tab_was) {
        travel_tab_dir = travel_tab > tab_was ? 1 : -1;
        travel_tab_at = state_time;
        travel_cursor = 0;
        Audio::Play(Sfx::UiMove);
    }
    const vector<const WaystoneDef*> list = WaystonesIn(travel_tab == 0);
    const int count = static_cast<int>(list.size());
    MoveCursor(travel_cursor, count);
    travel_cursor = std::clamp(travel_cursor, 0, std::max(0, count - 1));

    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && count > 0) {
        const WaystoneDef& to = *list[travel_cursor];
        string why;
        if (travel_from == to.id) {
            PushToast("You are standing at it.", Palette::TextDim);
            Audio::Play(Sfx::UiError);
        } else if (!world->CanTravel(to, travel_from, why)) {
            PushToast(why, {235, 190, 120, 255});
            Audio::Play(Sfx::UiError);
        } else if (world->visiting) {
            // A friend is taken by the host, who asks the stones again.
            SetState(GameState::Play);
            world->AskToTravel(to.id, travel_from);
            Audio::Play(Sfx::QuestStart);
        } else {
            SetState(GameState::Play);
            // To the spawn named for the stone: in front of it, whichever of a
            // map's stones it is.
            if (world->RequestTransition(to.map, to.id)) Audio::Play(Sfx::QuestStart);
        }
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawTravel() {
    ui.Dim(0.55f);
    const float row_h = 58.0f;
    // As tall as the longer tab needs, so switching does not resize the panel.
    const int most = static_cast<int>(std::max(WaystonesIn(true).size(), WaystonesIn(false).size()));
    const SDL_FRect panel = CenteredPanel(ui, 560.0f, 196.0f + most * row_h);
    ui.Panel(panel);
    ui.Text("Waystone", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);
    ui.Text("A woken stone opens on every other you have woken.", panel.x + panel.w / 2.0f,
            panel.y + 52.0f, TextSize::Small, Palette::TextDim, Align::Center);

    // --- the tabs --------------------------------------------------------------------
    const SDL_Color cold{150, 220, 255, 255};
    {
        const float tab_w = (panel.w - 40.0f - 8.0f) / 2.0f;
        for (int t = 0; t < 2; ++t) {
            const vector<const WaystoneDef*> in = WaystonesIn(t == 0);
            int awake = 0;
            for (const WaystoneDef* w : in) awake += world->Flagged(w->id) ? 1 : 0;
            const SDL_FRect tab = {panel.x + 20.0f + t * (tab_w + 8.0f), panel.y + 78.0f, tab_w, 34.0f};
            const bool on = t == travel_tab;
            ui.Fill(tab, on ? SDL_Color{70, 54, 30, 235} : SDL_Color{30, 24, 20, 200});
            ui.Outline(tab, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
            ui.Text(t == 0 ? "Towns" : "The wilds", tab.x + 14.0f, tab.y + 7.0f, TextSize::Body,
                    on ? Palette::Highlight : Palette::Text);
            ui.Text(std::to_string(awake) + " of " + std::to_string(in.size()) + " awake", tab.x + tab.w - 12.0f,
                    tab.y + 10.0f, TextSize::Small, awake > 0 ? cold : Palette::TextDim, Align::Right);
        }
    }

    // --- the stones under it -----------------------------------------------------------
    // Coming in from the side the tab was stepped toward, one after another.
    const vector<const WaystoneDef*> list = WaystonesIn(travel_tab == 0);
    const float since = state_time - travel_tab_at;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const WaystoneDef& w = *list[i];
        float k = std::clamp((since - 0.04f * i) / 0.2f, 0.0f, 1.0f);
        k = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        const float slide = (1.0f - k) * 36.0f * travel_tab_dir;
        const SDL_FRect row = {panel.x + 20.0f + slide, panel.y + 124.0f + i * row_h, panel.w - 40.0f, row_h - 8.0f};
        const bool selected = (i == travel_cursor);
        const bool here = travel_from == w.id;
        const bool awake = world->Flagged(w.id);
        // Woken, by somebody, and still beyond this character's gate.
        const bool barred = awake && !here && world->player.skills.CombatLevel() < w.combat;
        ui.Fill(row, selected ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(row, selected ? Palette::Highlight : Palette::BorderDim, selected ? 2.0f : 1.0f);
        // A lit or a dark eye, the way the stone itself shows it.
        ui.Fill({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, awake ? cold : SDL_Color{52, 50, 56, 255});
        ui.Outline({row.x + 14.0f, row.y + 16.0f, 16.0f, 16.0f}, Palette::BorderDim, 1.0f);
        ui.Text(w.name, row.x + 44.0f, row.y + 6.0f, TextSize::Body,
                !awake ? SDL_Color{120, 110, 100, 255} : (selected ? Palette::Highlight : Palette::Text));
        ui.Text(w.note, row.x + 44.0f, row.y + 28.0f, TextSize::Small, Palette::TextDim);
        ui.Text(here ? "you are here" : barred ? "Combat " + std::to_string(w.combat) : (awake ? "awake" : "asleep"),
                row.x + row.w - 12.0f, row.y + 8.0f, TextSize::Small,
                here ? Palette::Highlight : barred ? SDL_Color{235, 150, 120, 255}
                                                   : (awake ? cold : SDL_Color{150, 110, 100, 255}),
                Align::Right);
        // Fading up with it: a veil of the panel over the row, thinning away.
        if (k < 1.0f)
            ui.Fill({row.x - 2.0f, row.y - 2.0f, row.w + 4.0f, row.h + 4.0f},
                    {Palette::Panel.r, Palette::Panel.g, Palette::Panel.b, static_cast<Uint8>((1.0f - k) * 242.0f)});
    }

    ui.Text(input.PromptFor(Action::Confirm) + " go     left / right towns or the wilds     " +
                input.PromptFor(Action::Back) + " stay",
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
    // Two lines of blessing to a row: Grave Chill's and the Priest's Poppet's
    // ran a hundred pixels past the panel on one.
    const float row_h = 62.0f;
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
        const string where = standing ? (awake ? "awake until dawn" : "in the ring, asleep") : "in your pack";
        const float name_room = row.w - 46.0f - 10.0f - ui.Measure(where, TextSize::Small).x - 12.0f;
        ui.Text(ui.Fit(thing ? thing->name : have[i], name_room, TextSize::Body), row.x + 46.0f, row.y + 3.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        ui.Text(where, row.x + row.w - 10.0f, row.y + 5.0f, TextSize::Small,
                awake ? SDL_Color{255, 214, 120, 255} : (standing ? SDL_Color{170, 160, 190, 255} : Palette::TextDim),
                Align::Right);
        if (def) ui.TextWrapped(def->name + ": " + def->text, row.x + 46.0f, row.y + 22.0f, row.w - 56.0f,
                                TextSize::Small, Palette::TextDim);
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

void Game::UpdateAsk() {
    MoveCursor(ask_cursor, 2);
    const bool yes = (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && ask_cursor == 0;
    // A choice has two answers and no way out but one of them: backing out of
    // it is not the second.
    const bool no = ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && ask_cursor == 1) ||
                    (!(ask_story && ask_choice) && (input.Pressed(Action::Back) || input.Pressed(Action::Pause)));
    if (!yes && !no) return;
    SetState(GameState::Play);
    Audio::Play(yes ? Sfx::UiConfirm : Sfx::UiBack);
    if (ask_story) {
        story.Answer(yes);
        ask_story = false;
        return;
    }
    // A door's: ready, and through it.
    if (yes && !ask_flag.empty()) {
        world->SetFlag(ask_flag);
        world->SettleStory();
        world->TryInteract(ctx);
    }
}

void Game::DrawAsk() {
    ui.Dim(0.55f);
    const float h = 150.0f + ui.WrappedHeight(ask_text, 440.0f, TextSize::Body);
    const SDL_FRect panel = CenteredPanel(ui, 520.0f, h);
    ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
    ui.Fill(panel, {214, 197, 158, 250});
    ui.Outline(panel, {120, 96, 58, 255}, 2.0f);
    ui.TextWrapped(ask_text, panel.x + 40.0f, panel.y + 26.0f, 440.0f, TextSize::Body, {68, 48, 28, 255});
    const string answers[2] = {ask_yes.empty() ? string("Yes") : ask_yes, ask_no.empty() ? string("Not yet") : ask_no};
    for (int i = 0; i < 2; ++i) {
        const SDL_FRect row = {panel.x + 40.0f + i * 228.0f, panel.y + panel.h - 92.0f, 212.0f, 42.0f};
        const bool on = i == ask_cursor;
        if (on) { ui.Fill(row, {236, 222, 184, 255}); ui.Outline(row, {120, 96, 58, 255}, 2.0f); }
        else    ui.Outline(row, {170, 148, 108, 255}, 1.0f);
        ui.Text(ui.Fit(answers[i], row.w - 16.0f), row.x + row.w / 2.0f, row.y + 12.0f, TextSize::Body,
                on ? SDL_Color{52, 34, 16, 255} : SDL_Color{88, 68, 42, 255}, Align::Center);
    }
    ui.Text(input.PromptFor(Action::Confirm) + " choose     " + input.PromptFor(Action::Back) + " " +
                (ask_no.empty() ? string("not yet") : ask_no),
            panel.x + panel.w / 2.0f, panel.y + panel.h - 30.0f, TextSize::Small, {96, 74, 44, 255}, Align::Center);
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
        // Paid as they lie down, and only if they do: a friend's at the host.
        world->Sleep(how, ctx, sleep_fee);
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
