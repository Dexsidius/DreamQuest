#include "journal.h"
#include <fstream>

bool QuestChapters::Load(const string& path) {
    chapters.clear();
    where.clear();
    std::ifstream in(path);
    if (!in) {
        SDL_Log("QuestChapters: cannot open '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("QuestChapters: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }
    if (!root.contains("chapters") || !root["chapters"].is_array()) return false;
    for (const json& c : root["chapters"]) {
        QuestChapter ch;
        ch.id = c.value("id", string(""));
        ch.title = c.value("title", ch.id);
        ch.sub = c.value("sub", string(""));
        if (c.contains("quests"))
            for (const json& q : c["quests"]) ch.quests.push_back(q.get<string>());
        if (c.contains("side"))
            for (const json& q : c["side"]) ch.side.push_back(q.get<string>());
        const int index = static_cast<int>(chapters.size());
        for (size_t i = 0; i < ch.quests.size(); ++i) where[ch.quests[i]] = {index, false, static_cast<int>(i)};
        for (size_t i = 0; i < ch.side.size(); ++i) where[ch.side[i]] = {index, true, static_cast<int>(i)};
        chapters.push_back(std::move(ch));
    }
    return true;
}

int QuestChapters::Of(const string& quest_id, bool* side, int* place) const {
    const auto it = where.find(quest_id);
    if (it == where.end()) return -1;
    if (side) *side = it->second.side;
    if (place) *place = it->second.place;
    return it->second.chapter;
}

namespace Journal {

int TabOf(const QuestDef& d) {
    return d.major ? STORY : d.tutorial ? TUTORIALS : SIDE;
}

Page Build(const QuestLog& log, const QuestChapters& chapters, int tab) {
    // The headings this tab can have, in the order they come: in the Story tab
    // each part and then the tales; elsewhere what goes alongside each part,
    // then the kinds of everything else.
    struct Heading { string title, sub; };
    vector<Heading> headings;
    const int parts = static_cast<int>(chapters.All().size());
    for (const QuestChapter& c : chapters.All()) {
        if (tab == STORY) headings.push_back({c.title, c.sub});
        else              headings.push_back({"Alongside " + c.title, c.sub});
    }
    enum Kind { TALES = 0, TRADES, FAVOURS, LEDGER, BOARDS, KINDS };
    const int kind_at = static_cast<int>(headings.size());
    headings.push_back({"Tales of the Hollowmarch", ""});
    headings.push_back({"Trades", ""});
    headings.push_back({"Favours", ""});
    headings.push_back({"The Guild's ledger", ""});
    headings.push_back({"Boards and orders", ""});

    // Where each quest goes, and the key it is ordered by within its heading:
    // a part's quests in the order the part tells them, whatever has become of
    // them; the rest what is in hand first, then what is ahead, then what is
    // done -- and by level within each.
    struct Line { Row row; int key0 = 0, key1 = 0; string key2; };
    vector<Line> lines;
    const auto add = [&](const string& id, int state) {
        const QuestDef* d = log.Definition(id);
        if (!d || TabOf(*d) != tab) return;
        Line l;
        l.row.id = id;
        l.row.state = state;
        bool side = false;
        int place = 0;
        const int part = chapters.Of(id, &side, &place);
        if (part >= 0 && part < parts) {
            l.row.section = part;
            // A part's own before what goes alongside it, in its order.
            l.key0 = side ? 1000 + place : place;
        } else {
            const int kind = tab == STORY       ? TALES
                           : tab == TUTORIALS   ? TRADES
                           : d->guild_bounty    ? LEDGER
                           : d->daily           ? BOARDS
                                                : FAVOURS;
            l.row.section = kind_at + kind;
            l.key0 = state == IN_HAND ? 0 : state == AHEAD ? 1 : 2;
            l.key1 = d->recommended_level;
            l.key2 = id;
        }
        lines.push_back(std::move(l));
    };
    for (const string& id : log.Active()) add(id, IN_HAND);
    for (const auto& kv : log.Definitions())
        if (!kv.second.daily && log.Status(kv.first) == QuestStatus::NotStarted) add(kv.first, AHEAD);
    for (const string& id : log.Completed()) add(id, DONE);

    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
        if (a.row.section != b.row.section) return a.row.section < b.row.section;
        if (a.key0 != b.key0) return a.key0 < b.key0;
        if (a.key1 != b.key1) return a.key1 < b.key1;
        return a.key2 < b.key2;
    });

    // Only the headings something is listed under, numbered afresh.
    Page page;
    vector<int> renumber(headings.size(), -1);
    for (Line& l : lines) {
        int& n = renumber[static_cast<size_t>(l.row.section)];
        if (n < 0) {
            n = static_cast<int>(page.sections.size());
            const Heading& h = headings[static_cast<size_t>(l.row.section)];
            page.sections.push_back({h.title, h.sub, 0, 0});
        }
        l.row.section = n;
        Section& s = page.sections[static_cast<size_t>(n)];
        ++s.count;
        if (l.row.state == DONE) ++s.done;
        if (l.row.state == IN_HAND) ++page.in_hand;
        page.rows.push_back(std::move(l.row));
    }
    return page;
}

}   // namespace Journal
