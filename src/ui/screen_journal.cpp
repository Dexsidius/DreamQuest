// =============================================================================
//  The Game class's screens, continued: the quest journal and the world map
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
//  Quest log
// =============================================================================

const char* Game::QuestTabName(int tab) {
    switch (tab) {
        case 0:  return "Story";
        case 1:  return "Tutorials";
        default: return "Side quests";
    }
}

// What each tab holds, in the order it is listed: what you are doing, then what
// is still ahead of you, then what is done. A story quest is one marked
// "major" in data/quests.json and a tutorial one marked "tutorial"; everything
// else -- board contracts, daily orders, the favours people ask -- is a side
// quest, so no list is buried under another.
//
// Quests not yet taken are listed too, which is the point of the story tab:
// what is coming is as much a part of a journal as what is in hand. Dailies
// are the exception -- there are dozens, they come back every day, and listing
// every one of them unasked-for is the clutter the tabs exist to stop.
void Game::QuestList(int tab, vector<string>& out, size_t& active_count,
                     size_t& not_started_count) const {
    const auto belongs = [&](const string& id) {
        const QuestDef* d = quests->Definition(id);
        if (!d) return false;
        const int home = d->major ? 0 : (d->tutorial ? 1 : 2);
        return home == tab;
    };
    out.clear();
    for (const string& id : quests->Active()) if (belongs(id)) out.push_back(id);
    active_count = out.size();

    vector<const QuestDef*> ahead;
    for (const auto& kv : quests->Definitions()) {
        const QuestDef& d = kv.second;
        if (d.daily || !belongs(kv.first)) continue;
        if (quests->Status(kv.first) != QuestStatus::NotStarted) continue;
        ahead.push_back(&d);
    }
    // In the order they are meant to be met.
    std::sort(ahead.begin(), ahead.end(), [](const QuestDef* a, const QuestDef* b) {
        if (a->recommended_level != b->recommended_level)
            return a->recommended_level < b->recommended_level;
        return a->id < b->id;
    });
    for (const QuestDef* d : ahead) out.push_back(d->id);
    not_started_count = ahead.size();

    for (const string& id : quests->Completed()) if (belongs(id)) out.push_back(id);
}

// Red for what has not been started, blue for what is in hand, green for what
// is done -- the three states a journal line can be in, told apart at a glance
// rather than by reading.
namespace {
constexpr SDL_Color kQuestNotStarted{216, 108, 100, 255};
constexpr SDL_Color kQuestActive    {120, 172, 238, 255};
constexpr SDL_Color kQuestDone      {120, 202, 118, 255};
}

void Game::UpdateQuestPanel() {
    vector<string> list;
    size_t active_count = 0, ahead = 0;

    // Left and right step between the tabs; the cursor of each is its own.
    const int before = quest_tab;
    if (input.MenuLeft())  quest_tab = (quest_tab + kQuestTabs - 1) % kQuestTabs;
    if (input.MenuRight()) quest_tab = (quest_tab + 1) % kQuestTabs;
    if (quest_tab != before) Audio::Play(Sfx::UiMove);

    QuestList(quest_tab, list, active_count, ahead);
    MoveCursor(quest_cursor[quest_tab], static_cast<int>(list.size()));
    // Confirm on a quest in hand follows it: the waypoint is that one's until
    // it is done, or until this is pressed on it again.
    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && !list.empty()) {
        const size_t at = static_cast<size_t>(std::clamp(quest_cursor[quest_tab], 0, static_cast<int>(list.size()) - 1));
        if (at < active_count) {
            quests->Follow(list[at]);
            Audio::Play(Sfx::UiConfirm);
        } else {
            Audio::Play(Sfx::UiError);
        }
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::QuestLog) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

// =============================================================================
//  The world map
// =============================================================================

void Game::UpdateWorldMap() {
    // It opens on where you are. The Hollowmarch is the other side of the page.
    if (state_time <= 0.0f) map_overview = false;
    if (world_map.HasOverview(world->MapId()) &&
        (input.Pressed(Action::Confirm) || input.MenuLeft() || input.MenuRight())) {
        map_overview = !map_overview;
        Audio::Play(Sfx::UiMove);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::WorldMap) ||
        input.Pressed(Action::Pause))
        SetState(GameState::Play);
}

void Game::DrawWorldMap() {
    const Waypoint& waypoint = CurrentWaypoint();
    const QuestDef* following = quests ? quests->Definition(quests->Followed()) : nullptr;
    world_map.SetRoads(&waypoints);
    world_map.Draw(renderer, *textures, ui, (*world),
                   input.PromptFor(Action::WorldMap) + " or " + input.PromptFor(Action::Back) + " close",
                   input.PromptFor(Action::Confirm) + " turn to", map_overview,
                   settings.quest_waypoints ? &waypoint : nullptr, following ? following->name : string());
}

void Game::DrawQuestPanel() {
    ui.Dim(0.5f);
    const SDL_FRect panel = CenteredPanel(ui, 760.0f, 460.0f);
    ui.Panel(panel);
    ui.Text("Quest Journal", panel.x + 24.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight);

    vector<string> list;
    size_t active_size = 0, ahead_size = 0;
    QuestList(quest_tab, list, active_size, ahead_size);
    const int cursor_here = std::clamp(quest_cursor[quest_tab], 0,
                                       std::max(0, static_cast<int>(list.size()) - 1));
    // What each row is: in hand, still ahead, or finished.
    const auto state_of = [&](size_t i) {
        if (i < active_size) return 1;
        return i < active_size + ahead_size ? 0 : 2;
    };
    const SDL_Color kStateColour[3] = {kQuestNotStarted, kQuestActive, kQuestDone};

    // The two tabs, drawn as headings with the count each holds. The one you
    // are in is lit and underlined.
    {
        float tx = panel.x + 24.0f;
        for (int tab = 0; tab < kQuestTabs; ++tab) {
            vector<string> in_tab;
            size_t tab_active = 0, tab_ahead = 0;
            QuestList(tab, in_tab, tab_active, tab_ahead);
            // In hand out of everything the tab knows about.
            const string label = string(QuestTabName(tab)) + "  " +
                                 std::to_string(tab_active) + "/" + std::to_string(in_tab.size());
            const bool on = tab == quest_tab;
            const float w = ui.Measure(label, TextSize::Small).x + 24.0f;
            const SDL_FRect box = {tx, panel.y + 46.0f, w, 24.0f};
            if (on) {
                ui.Fill(box, {58, 46, 28, 200});
                ui.Outline(box, Palette::Highlight, 1.0f);
            }
            ui.Text(label, tx + 12.0f, box.y + 5.0f, TextSize::Small,
                    on ? Palette::Highlight : Palette::TextDim);
            tx += w + 10.0f;
        }
    }

    if (list.empty()) {
        const string what = quest_tab == 0 ? "The story has not found you yet."
                          : quest_tab == 1 ? "Nobody has offered to teach you a trade."
                                           : "No contracts, orders or favours in hand.";
        const string where = quest_tab == 0 ? "Talk to the people who have been here longest."
                           : quest_tab == 1 ? "The sawpit, the gravel pit and the mill pond are in Havenbrook."
                                            : "Look for a mission board in town, or ask at a forge.";
        ui.Text(what, panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f - 20.0f,
                TextSize::Body, Palette::TextDim, Align::Center);
        ui.Text(where, panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f + 6.0f,
                TextSize::Small, Palette::TextDim, Align::Center);
    } else {
        // Left: the list. Right: detail for whatever is highlighted.
        const float list_w = 290.0f;
        const float row_h = 34.0f;

        // The window of rows the cursor is inside, so a long list scrolls
        // rather than running off the panel.
        const size_t visible = 9;
        size_t first = 0;
        if (list.size() > visible)
            first = std::min(list.size() - visible,
                             static_cast<size_t>(std::max(0, cursor_here - static_cast<int>(visible) / 2)));

        for (size_t k = 0; k < visible && first + k < list.size(); ++k) {
            const size_t i = first + k;
            const SDL_FRect row = {panel.x + 20.0f, panel.y + 82.0f + k * row_h,
                                   list_w, row_h - 4.0f};
            const bool selected = (static_cast<int>(i) == cursor_here);
            const int state = state_of(i);

            if (selected) {
                ui.Fill(row, {58, 46, 28, 200});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests->Definition(list[i]);
            ui.Text(d ? d->name : list[i], row.x + 10.0f, row.y + 5.0f, TextSize::Small,
                    kStateColour[state]);
            if (state == 2)
                ui.Text("done", row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small,
                        kQuestDone, Align::Right);
            else if (state == 1 && list[i] == quests->Followed()) {
                // The tag gives way to the name. "Cooking: Meat and Fire" and
                // "following (newest)" do not both fit on a row, and drawn
                // anyway they were one unreadable word. A gold bar down the
                // row's edge marks it whatever happens; the words are added
                // when there is room for them -- the long form, the short one,
                // or none.
                const SDL_Color gold{255, 214, 96, 255};
                ui.Fill({row.x, row.y, 3.0f, row.h}, gold);
                const float name_w = ui.Measure(d ? d->name : list[i], TextSize::Small).x;
                const float room = row.w - 18.0f - name_w - 14.0f;
                string tag = quests->Chosen() ? "following" : "following (newest)";
                if (ui.Measure(tag, TextSize::Small).x > room) tag = "following";
                if (ui.Measure(tag, TextSize::Small).x <= room)
                    ui.Text(tag, row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small, gold, Align::Right);
            }
        }
        if (list.size() > visible)
            ui.Text(std::to_string(cursor_here + 1) + "/" + std::to_string(list.size()),
                    panel.x + 20.0f + list_w, panel.y + 82.0f + visible * row_h, TextSize::Small,
                    Palette::TextDim, Align::Right);

        const int index = cursor_here;
        if (const QuestDef* d = quests->Definition(list[index])) {
            const float dx = panel.x + list_w + 40.0f;
            const float dw = panel.w - list_w - 64.0f;
            float y = panel.y + 82.0f;

            const int state = state_of(static_cast<size_t>(index));
            ui.Text(d->name, dx, y, TextSize::Body, kStateColour[state]);
            y += 28.0f;
            const char* word = state == 0 ? "Not started" : (state == 1 ? "In progress" : "Completed");
            ui.Text(word, dx, y, TextSize::Small, kStateColour[state]);
            ui.Text("Suggested level " + std::to_string(d->recommended_level) +
                        (d->daily ? "     daily, done " + std::to_string(quests->Completions(list[index])) + "x" : string("")),
                    dx + dw, y, TextSize::Small, Palette::TextDim, Align::Right);
            y += 24.0f;
            y += ui.TextWrapped(d->summary, dx, y, dw, TextSize::Small, Palette::Text);
            y += 12.0f;

            ui.Text(state == 0 ? "First step" : "Objective", dx, y, TextSize::Small, Palette::Highlight);
            y += 20.0f;
            // A quest not yet taken has no progress to report, so its own
            // first stage stands in for the objective line.
            const string objective = state == 0
                ? (d->stages.empty() ? string("Nobody has offered this yet.") : d->stages.front().description)
                : quests->CurrentObjectiveText(list[index]);
            y += ui.TextWrapped(objective, dx, y, dw, TextSize::Small,
                                state == 0 ? Palette::TextDim : Palette::Text);
            y += 14.0f;
            if (state == 0 && d->combat_level > 0) {
                ui.Text("Needs Combat " + std::to_string(d->combat_level), dx, y, TextSize::Small,
                        world->player.skills.CombatLevel() >= d->combat_level ? kQuestDone : kQuestNotStarted);
                y += 20.0f;
            }

            if (!d->rewards.xp.empty() || d->rewards.coins > 0 || !d->rewards.items.empty()) {
                ui.Text("Rewards", dx, y, TextSize::Small, Palette::Highlight);
                y += 20.0f;
                for (const auto& xp : d->rewards.xp) {
                    ui.Text(std::to_string(xp.second) + " " + SkillName(xp.first) + " XP",
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                if (d->rewards.coins > 0) {
                    ui.Text(std::to_string(d->rewards.coins) + " coins", dx, y,
                            TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
                for (const auto& it : d->rewards.items) {
                    const ItemDef* def = items.Get(it.first);
                    ui.Text(std::to_string(it.second) + "x " + (def ? def->name : it.first),
                            dx, y, TextSize::Small, Palette::Xp);
                    y += 18.0f;
                }
            }
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + " follow: its waypoint is shown     Left / Right  switch tab     " +
                input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}
