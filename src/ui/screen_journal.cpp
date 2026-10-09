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

// What each tab holds, under its headings: see Journal::Build. A story quest is
// one marked "major" in data/quests.json and a tutorial one marked "tutorial";
// everything else -- board contracts, daily orders, the favours people ask --
// is a side quest, so no list is buried under another. The Story tab is headed
// by the story's parts (data/chapters.json), each with its quests in the order
// it tells them, so where the story has got to reads off the page.
//
// Quests not yet taken are listed too, which is the point of the story tab:
// what is coming is as much a part of a journal as what is in hand. Dailies
// are the exception -- there are dozens, they come back every day, and listing
// every one of them unasked-for is the clutter the tabs exist to stop.
Journal::Page Game::QuestPage(int tab) const {
    return Journal::Build(*quests, chapters, tab);
}

// Red for what has not been started, blue for what is in hand, green for what
// is done -- the three states a journal line can be in, told apart at a glance
// rather than by reading.
namespace {
constexpr SDL_Color kQuestNotStarted{216, 108, 100, 255};
constexpr SDL_Color kQuestActive    {120, 172, 238, 255};
constexpr SDL_Color kQuestDone      {120, 202, 118, 255};
// A reward still to be chosen.
constexpr SDL_Color kRewardOwed     {255, 214, 96, 255};

// --- the three ways of fighting, as a quest's reward choices name them ------------------
// The quest file's word for one, and how it is shown: in the colours the
// character select gives the three affinities.
const char* StyleWord(AttackStyle s) {
    return s == AttackStyle::Ranged ? "ranged" : s == AttackStyle::Magic ? "magic" : "melee";
}

string StyleTitle(const string& style) {
    return style == "ranged" ? "Ranged" : style == "magic" ? "Magic" : style == "melee" ? "Melee" : "";
}

SDL_Color StyleColour(const string& style) {
    if (style == "ranged") return {150, 210, 130, 255};
    if (style == "magic")  return {170, 150, 240, 255};
    if (style == "melee")  return {236, 176, 96, 255};
    return Palette::Xp;
}

// Whether an option is the character's own: by its calling where it names one
// -- the Knight and the Warden both fight in melee -- else by its way of
// fighting.
bool IsMine(const QuestRewardChoice& c, const string& style, const Player& p) {
    if (!c.calling.empty()) return c.calling == Player::CallingKey(p.GetCalling());
    return !style.empty() && style == StyleWord(p.Affinity());
}

// And its colour: the calling's where it names one, else its way of fighting's.
SDL_Color ChoiceColour(const QuestRewardChoice& c, const string& style) {
    for (int k = 0; k < Player::CALLINGS; ++k)
        if (c.calling == Player::CallingKey(static_cast<Player::Calling>(k)))
            return CallingColour(static_cast<Player::Calling>(k));
    return StyleColour(style);
}

// What an option is called: its own label, else its way of fighting, else
// the first thing it holds.
string ChoiceTitle(const QuestRewardChoice& c, const string& style, const ItemDatabase& items) {
    if (!c.label.empty()) return c.label;
    if (!style.empty()) return StyleTitle(style);
    if (!c.items.empty()) {
        const ItemDef* d = items.Get(c.items.front().first);
        return d ? d->name : c.items.front().first;
    }
    return "A reward";
}

// What it holds, in a line: "Iron Bow", "Iron Bow and 3 Healing Draughts", "120 coins".
string ChoiceContents(const QuestRewardChoice& c, const ItemDatabase& items) {
    vector<string> parts;
    for (const auto& t : c.items) {
        const ItemDef* d = items.Get(t.first);
        const string name = d ? d->name : t.first;
        parts.push_back(t.second > 1 ? std::to_string(t.second) + " " + name : name);
    }
    if (c.coins > 0) parts.push_back(std::to_string(c.coins) + " coins");
    for (const auto& x : c.xp) parts.push_back(std::to_string(x.second) + " " + SkillName(x.first) + " XP");
    string out;
    for (size_t i = 0; i < parts.size(); ++i)
        out += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return out.empty() ? string("nothing") : out;
}
}

// The reward lines of a quest's detail: what it pays, and the choice it
// offers beside that. `quest_id` empty is a quest not taken -- the board's --
// with no pick owed or made to speak of.
float Game::DrawQuestRewards(const QuestDef& d, const string& quest_id, float x, float y, float w) {
    const QuestRewards& r = d.rewards;
    if (r.xp.empty() && r.coins <= 0 && r.items.empty() && r.choices.empty()) return 0.0f;
    const float top = y;
    ui.Text("Rewards", x, y, TextSize::Small, Palette::Highlight);
    y += 20.0f;
    for (const auto& xp : r.xp) {
        ui.Text(std::to_string(xp.second) + " " + SkillName(xp.first) + " XP", x, y, TextSize::Small, Palette::Xp);
        y += 18.0f;
    }
    if (r.coins > 0) {
        ui.Text(std::to_string(r.coins) + " coins", x, y, TextSize::Small, Palette::Xp);
        y += 18.0f;
    }
    for (const auto& it : r.items) {
        const ItemDef* def = items.Get(it.first);
        ui.Text(std::to_string(it.second) + "x " + (def ? def->name : it.first), x, y, TextSize::Small, Palette::Xp);
        y += 18.0f;
    }
    if (!r.choices.empty()) {
        const int owed = quest_id.empty() ? 0 : quests->ChoicesOwed(quest_id);
        const int took = quest_id.empty() ? -1 : quests->LastChosen(quest_id);
        ui.Text(owed > 0 ? "And one of these, still to choose:" : "And one of these, your choice:", x, y,
                TextSize::Small, owed > 0 ? kRewardOwed : Palette::Highlight);
        y += 20.0f;
        for (size_t i = 0; i < r.choices.size(); ++i) {
            const QuestRewardChoice& c = r.choices[i];
            const string style = QuestLog::StyleOf(c, items);
            const string title = ChoiceTitle(c, style, items);
            const string holds = ChoiceContents(c, items);
            string line = title == holds ? holds : title + ": " + holds;
            if (owed == 0 && static_cast<int>(i) == took) line = "Taken -- " + line;
            // A line each, cut to the width: five weapons, or four kits of four
            // pieces, wrapped, ran the detail off the foot of the page. The
            // choosing panel has each whole.
            ui.Text(ui.Fit(line, w - 10.0f, TextSize::Small), x + 10.0f, y, TextSize::Small, ChoiceColour(c, style));
            y += 18.0f;
        }
    }
    return y - top;
}

// =============================================================================
//  A reward to choose
//
//  A quest whose rewards have "choices" in data/quests.json pays the rest as
//  it completes, and then asks which of those the player will have -- a sword,
//  a bow or a staff, say -- on a panel of cards, one an option, with the one
//  for the character's own way of fighting lit first. Back puts it off, and
//  the journal keeps it waiting ("reward!") until it is taken.
// =============================================================================

void Game::AskRewardChoice(const string& quest_id) {
    // Two quests can finish in one moment: each is asked about in turn.
    vector<string>& queue = reward_queue[serving == 1 ? 1 : 0];
    if (std::find(queue.begin(), queue.end(), quest_id) == queue.end()) queue.push_back(quest_id);
    if (state != GameState::RewardChoice) OpenRewardChoice(queue.front());
}

void Game::OpenRewardChoice(const string& quest_id) {
    const QuestDef* d = quests->Definition(quest_id);
    if (!d || d->rewards.choices.empty()) return;
    reward_quest = quest_id;
    reward_cursor = OwnRewardChoice(*d);
    if (state != GameState::RewardChoice) OpenPanel(GameState::RewardChoice);
    reward_opened_at = state_time;
}

void Game::NextRewardOrClose() {
    vector<string>& queue = reward_queue[serving == 1 ? 1 : 0];
    queue.erase(std::remove(queue.begin(), queue.end(), reward_quest), queue.end());
    queue.erase(std::remove_if(queue.begin(), queue.end(),
                               [&](const string& q) { return quests->ChoicesOwed(q) <= 0; }),
                queue.end());
    if (!queue.empty()) OpenRewardChoice(queue.front());
    else                ClosePanel();
}

int Game::OwnRewardChoice(const QuestDef& d) const {
    for (size_t i = 0; i < d.rewards.choices.size(); ++i)
        if (IsMine(d.rewards.choices[i], QuestLog::StyleOf(d.rewards.choices[i], items), world->player))
            return static_cast<int>(i);
    return 0;
}

void Game::UpdateRewardChoice() {
    const QuestDef* d = quests->Definition(reward_quest);
    const int n = d ? static_cast<int>(d->rewards.choices.size()) : 0;
    if (n == 0 || quests->ChoicesOwed(reward_quest) <= 0) {
        NextRewardOrClose();
        return;
    }

    const int was = reward_cursor;
    if (input.MenuLeft())  reward_cursor = (reward_cursor + n - 1) % n;
    if (input.MenuRight()) reward_cursor = (reward_cursor + 1) % n;
    reward_cursor = std::clamp(reward_cursor, 0, n - 1);
    if (reward_cursor != was) Audio::Play(Sfx::UiMove);

    // A moment before a press counts. A quest finished mid-fight opens this
    // under a hand still hammering the attack button -- which, on a keyboard,
    // is the button that takes a reward.
    if (state_time - reward_opened_at < 0.6f) return;

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        if (const QuestRewardChoice* c = quests->TakeChoice(reward_quest, reward_cursor)) {
            if (const QuestDef* qd = quests->Definition(reward_quest); qd && !qd->choice_flag.empty())
                world->SetFlag(qd->choice_flag);
            GiveRewards(c->xp, c->items, c->coins);
            quests->RefreshCollectObjectives(world->player.inventory);
            PushToast("Taken: " + ChoiceContents(*c, items) + ".", Palette::Xp);
        }
        NextRewardOrClose();
        return;
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        PushToast("The reward waits in the journal: " + input.PromptFor(Action::QuestLog) + ".", Palette::TextDim);
        NextRewardOrClose();
    }
}

void Game::DrawRewardChoice() {
    ui.Dim(0.55f);
    const QuestDef* d = quests->Definition(reward_quest);
    if (!d || d->rewards.choices.empty()) return;

    const int n = static_cast<int>(d->rewards.choices.size());
    const float gap = 14.0f;
    const float card_w = std::min(230.0f, (ui.ViewWidth() - 120.0f - gap * (n - 1)) / static_cast<float>(n));
    const float cards_w = card_w * n + gap * (n - 1);
    // As tall as the fullest card needs: its name, whose it is, what it holds,
    // and the numbers of the first thing in it that can be worn -- the same
    // lines, in the same order, as the cards below draw them.
    float card_h = 150.0f;
    for (const QuestRewardChoice& c : d->rewards.choices) {
        const string style = QuestLog::StyleOf(c, items);
        const size_t shown = std::min<size_t>(c.items.size(), 3);
        float h = 16.0f + 26.0f + (IsMine(c, style, world->player) ? 20.0f : 0.0f) + 4.0f;
        h += shown * 42.0f + (c.items.size() > shown ? 18.0f : 0.0f);
        h += (c.coins > 0 ? 18.0f : 0.0f) + c.xp.size() * 18.0f;
        for (const auto& t : c.items) {
            const ItemDef* def = items.Get(t.first);
            if (!def || def->slot == SLOT_NONE) continue;
            // The line it is weighed against can take two, for the longest names.
            h += 6.0f + 36.0f + ItemStatLines(*def, WornAgainst(*def), !GoesInOtherHand(*def)).size() * 16.0f;
            if (!def->passive_text.empty())
                h += ui.WrappedHeight(def->passive_text, card_w - 24.0f, TextSize::Small) + 6.0f;
            break;
        }
        card_h = std::max(card_h, h + 12.0f);
    }
    card_h = std::min(card_h, ui.ViewHeight() - 200.0f);
    const SDL_FRect panel = CenteredPanel(ui, std::max(540.0f, cards_w + 48.0f), card_h + 186.0f);
    ui.Panel(panel);
    const float cx = panel.x + panel.w / 2.0f;

    ui.Text("Choose your reward", cx, panel.y + 16.0f, TextSize::Large, Palette::Highlight, Align::Center);
    ui.Text(d->name, cx, panel.y + 54.0f, TextSize::Body, Palette::Text, Align::Center);
    // What the quest's own last words are, where it has them: nothing else
    // ever showed them.
    if (!d->completion_text.empty()) {
        const float wrap = panel.w - 64.0f;
        if (ui.Measure(d->completion_text, TextSize::Small).x <= wrap)
            ui.Text(d->completion_text, cx, panel.y + 82.0f, TextSize::Small, Palette::TextDim, Align::Center);
        else
            ui.TextWrapped(d->completion_text, panel.x + 32.0f, panel.y + 82.0f, wrap, TextSize::Small, Palette::TextDim);
    }

    const float top = panel.y + 124.0f;
    const float left = cx - cards_w / 2.0f;
    for (int i = 0; i < n; ++i) {
        const QuestRewardChoice& c = d->rewards.choices[i];
        const string style = QuestLog::StyleOf(c, items);
        const SDL_Color tone = ChoiceColour(c, style);
        const SDL_FRect card = {left + i * (card_w + gap), top, card_w, card_h};
        const bool on = i == reward_cursor;

        ui.Fill(card, on ? SDL_Color{58, 46, 28, 235} : SDL_Color{30, 24, 20, 220});
        ui.Outline(card, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
        // A band of the way of fighting's colour across the top.
        ui.Fill({card.x + 2.0f, card.y + 2.0f, card.w - 4.0f, 5.0f}, tone);

        float y = card.y + 16.0f;
        ui.Text(ChoiceTitle(c, style, items), card.x + 12.0f, y, TextSize::Body, on ? Palette::Highlight : tone);
        y += 26.0f;
        // The character's own: said, and lit first.
        if (IsMine(c, style, world->player)) {
            ui.Text("Recommended", card.x + 12.0f, y, TextSize::Small, tone);
            y += 20.0f;
        }
        y += 4.0f;

        // What it holds, a line and an icon each -- the first three, and a
        // count of the rest.
        const size_t shown = std::min<size_t>(c.items.size(), 3);
        for (size_t k = 0; k < shown; ++k) {
            const auto& t = c.items[k];
            const ItemDef* def = items.Get(t.first);
            const SDL_FRect icon = {card.x + 12.0f, y, 36.0f, 36.0f};
            ui.Fill(icon, {22, 18, 14, 230});
            ui.Outline(icon, Palette::BorderDim, 1.0f);
            const SDL_FRect inner = {icon.x + 3.0f, icon.y + 3.0f, icon.w - 6.0f, icon.h - 6.0f};
            SDL_Texture* tex = (def && !def->icon.empty()) ? textures->Get(def->icon) : nullptr;
            if (tex) SDL_RenderTexture(renderer, tex, nullptr, &inner);
            else     DrawItemPlaceholder(ui, def, t.first, inner);
            const string name = (t.second > 1 ? std::to_string(t.second) + "  " : string()) + (def ? def->name : t.first);
            ui.TextWrapped(name, icon.x + icon.w + 8.0f, y + 2.0f, card.w - icon.w - 38.0f, TextSize::Small, Palette::Text);
            y += 42.0f;
        }
        if (c.items.size() > shown) {
            ui.Text("and " + std::to_string(c.items.size() - shown) + " more", card.x + 12.0f, y, TextSize::Small,
                    Palette::TextDim);
            y += 18.0f;
        }
        if (c.coins > 0) {
            ui.Text(std::to_string(c.coins) + " coins", card.x + 12.0f, y, TextSize::Small, Palette::Xp);
            y += 18.0f;
        }
        for (const auto& x : c.xp) {
            ui.Text(std::to_string(x.second) + " " + SkillName(x.first) + " XP", card.x + 12.0f, y, TextSize::Small,
                    Palette::Xp);
            y += 18.0f;
        }
        // And what the first thing in it that can be worn would do, against
        // what is worn now: the numbers a choice between weapons is about.
        for (const auto& t : c.items) {
            const ItemDef* def = items.Get(t.first);
            if (!def || def->slot == SLOT_NONE) continue;
            DrawItemStats(*def, card.x + 12.0f, y + 6.0f, card.w - 24.0f);
            break;
        }
    }

    ui.Text(input.PromptFor(Action::Confirm) + " take it     left / right choose     " +
                input.PromptFor(Action::Back) + " later: it waits in the journal",
            cx, panel.y + panel.h - 30.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

void Game::UpdateQuestPanel() {
    // Left and right step between the tabs; the cursor of each is its own.
    const int before = quest_tab;
    if (input.MenuLeft())  quest_tab = (quest_tab + kQuestTabs - 1) % kQuestTabs;
    if (input.MenuRight()) quest_tab = (quest_tab + 1) % kQuestTabs;
    if (quest_tab != before) Audio::Play(Sfx::UiMove);

    // The cursor goes from quest to quest; the headings between are passed over.
    const Journal::Page page = QuestPage(quest_tab);
    MoveCursor(quest_cursor[quest_tab], static_cast<int>(page.rows.size()));
    // Confirm on a quest in hand follows it: the waypoint is that one's until
    // it is done, or until this is pressed on it again. On a finished one with
    // a reward still to choose, it asks which.
    if ((input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) && !page.rows.empty()) {
        const Journal::Row& row =
            page.rows[static_cast<size_t>(std::clamp(quest_cursor[quest_tab], 0, static_cast<int>(page.rows.size()) - 1))];
        if (row.state == Journal::IN_HAND) {
            quests->Follow(row.id);
            Audio::Play(Sfx::UiConfirm);
        } else if (quests->ChoicesOwed(row.id) > 0) {
            OpenRewardChoice(row.id);
            return;
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

    const Journal::Page page = QuestPage(quest_tab);
    vector<string> list;
    for (const Journal::Row& row : page.rows) list.push_back(row.id);
    const int cursor_here = std::clamp(quest_cursor[quest_tab], 0,
                                       std::max(0, static_cast<int>(list.size()) - 1));
    // What each row is: in hand, still ahead, or finished.
    const auto state_of = [&](size_t i) { return i < page.rows.size() ? page.rows[i].state : 0; };
    const SDL_Color kStateColour[3] = {kQuestNotStarted, kQuestActive, kQuestDone};

    // The two tabs, drawn as headings with the count each holds. The one you
    // are in is lit and underlined.
    {
        float tx = panel.x + 24.0f;
        for (int tab = 0; tab < kQuestTabs; ++tab) {
            const Journal::Page in_tab = QuestPage(tab);
            // In hand out of everything the tab knows about.
            const string label = string(QuestTabName(tab)) + "  " +
                                 std::to_string(in_tab.in_hand) + "/" + std::to_string(in_tab.rows.size());
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
        const float head_h = 26.0f;
        const size_t visible = 9;

        // The lines as drawn: each part's heading, then its quests. The cursor
        // is only ever on a quest.
        struct Line { bool heading; size_t index; };
        vector<Line> lines;
        size_t cursor_line = 0;
        for (size_t i = 0; i < page.rows.size(); ++i) {
            if (i == 0 || page.rows[i].section != page.rows[i - 1].section)
                lines.push_back({true, static_cast<size_t>(page.rows[i].section)});
            if (static_cast<int>(i) == cursor_here) cursor_line = lines.size();
            lines.push_back({false, i});
        }
        // The window of lines the cursor is inside, so a long list scrolls
        // rather than running off the panel: about half of it above the
        // cursor, the heading of its part with it where there is room.
        const float budget = visible * row_h;
        const auto tall = [&](const Line& l) { return l.heading ? head_h : row_h; };
        size_t first = cursor_line, last = cursor_line;
        float used = tall(lines[cursor_line]);
        while (first > 0 && used + tall(lines[first - 1]) <= budget * 0.5f) used += tall(lines[--first]);
        while (last + 1 < lines.size() && used + tall(lines[last + 1]) <= budget) used += tall(lines[++last]);
        while (first > 0 && used + tall(lines[first - 1]) <= budget) used += tall(lines[--first]);

        float ly = panel.y + 82.0f;
        for (size_t at = first; at <= last && at < lines.size(); ++at) {
            const Line& line = lines[at];
            if (line.heading) {
                // A part's heading: its title and, in the Story tab, the line
                // under it on its title card; how many of its quests are done.
                const Journal::Section& sec = page.sections[line.index];
                const string tally = std::to_string(sec.done) + "/" + std::to_string(sec.count);
                const float tally_w = ui.Measure(tally, TextSize::Small).x;
                string head = sec.title;
                if (quest_tab == Journal::STORY && !sec.sub.empty()) head += "  -  " + sec.sub;
                head = ui.Fit(head, list_w - tally_w - 26.0f, TextSize::Small);
                ui.Text(head, panel.x + 24.0f, ly + 5.0f, TextSize::Small, Palette::Highlight);
                ui.Text(tally, panel.x + 20.0f + list_w - 4.0f, ly + 5.0f, TextSize::Small, Palette::TextDim, Align::Right);
                ui.Fill({panel.x + 24.0f, ly + head_h - 5.0f, list_w - 8.0f, 1.0f}, Palette::BorderDim);
                ly += head_h;
                continue;
            }
            const size_t i = line.index;
            const SDL_FRect row = {panel.x + 20.0f, ly, list_w, row_h - 4.0f};
            ly += row_h;
            const bool selected = (static_cast<int>(i) == cursor_here);
            const int state = state_of(i);

            if (selected) {
                ui.Fill(row, {58, 46, 28, 200});
                ui.Outline(row, Palette::Highlight, 1.0f);
            }
            const QuestDef* d = quests->Definition(list[i]);
            ui.Text(d ? d->name : list[i], row.x + 10.0f, row.y + 5.0f, TextSize::Small,
                    kStateColour[state]);
            if (state == 2) {
                const bool owed = quests->ChoicesOwed(list[i]) > 0;
                ui.Text(owed ? "reward!" : "done", row.x + row.w - 8.0f, row.y + 5.0f, TextSize::Small,
                        owed ? kRewardOwed : kQuestDone, Align::Right);
            }
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
        // More than fits: which quest of how many.
        if (first > 0 || last + 1 < lines.size())
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

            y += DrawQuestRewards(*d, list[index], dx, y, dw);
        }
    }

    // On a reward still to be chosen, Confirm is the choosing.
    const bool owes = !list.empty() && quests->ChoicesOwed(list[cursor_here]) > 0;
    ui.Text(input.PromptFor(Action::Confirm) + (owes ? " choose your reward" : " follow: its waypoint is shown") +
                "     Left / Right  switch tab     " + input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}
