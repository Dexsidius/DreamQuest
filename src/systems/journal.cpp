#include "journal.h"
#include <fstream>

bool QuestChapters::Load(const string& path) {
    chapters.clear();
    where.clear();
    threads.clear();
    thread_of.clear();
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
    if (root.contains("threads") && root["threads"].is_array())
        for (const json& t : root["threads"]) {
            QuestThread th;
            th.id = t.value("id", string(""));
            th.title = t.value("title", th.id);
            th.sub = t.value("sub", string(""));
            if (t.contains("quests"))
                for (const json& q : t["quests"]) th.quests.push_back(q.get<string>());
            const int index = static_cast<int>(threads.size());
            for (size_t i = 0; i < th.quests.size(); ++i) thread_of[th.quests[i]] = {index, static_cast<int>(i)};
            threads.push_back(std::move(th));
        }
    return true;
}

int QuestChapters::ThreadOf(const string& quest_id, int* place) const {
    const auto it = thread_of.find(quest_id);
    if (it == thread_of.end()) return -1;
    if (place) *place = it->second.second;
    return it->second.first;
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

bool Begun(const QuestLog& log, const QuestChapter& chapter) {
    for (const string& id : chapter.quests)
        if (log.Status(id) != QuestStatus::NotStarted) return true;
    return false;
}

string NextOf(const QuestLog& log, const QuestThread& thread) {
    for (const string& id : thread.quests)
        if (log.Status(id) == QuestStatus::NotStarted) return id;
    return "";
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
    // Then the questlines, in the order the file has them.
    const int threads = static_cast<int>(chapters.Threads().size());
    for (const QuestThread& t : chapters.Threads()) headings.push_back({t.title, t.sub});
    vector<string> next_of;
    for (const QuestThread& t : chapters.Threads()) next_of.push_back(NextOf(log, t));
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
    // Which parts have begun: the rest are kept out of the journal entirely.
    vector<bool> begun;
    for (const QuestChapter& c : chapters.All()) begun.push_back(Begun(log, c));
    const auto add = [&](const string& id, int state) {
        const QuestDef* d = log.Definition(id);
        if (!d || TabOf(*d) != tab) return;
        Line l;
        l.row.id = id;
        l.row.state = state;
        bool side = false;
        int place = 0;
        const int part = chapters.Of(id, &side, &place);
        if (state == AHEAD && part >= 0 && part < parts && !begun[static_cast<size_t>(part)]) return;
        int step = 0;
        const int thread = part < 0 ? chapters.ThreadOf(id, &step) : -1;
        // Of a questline not yet taken up, only the step it goes on with.
        if (state == AHEAD && thread >= 0 && thread < threads && next_of[static_cast<size_t>(thread)] != id) return;
        if (part >= 0 && part < parts) {
            l.row.section = part;
            // A part's own before what goes alongside it, in its order.
            l.key0 = side ? 1000 + place : place;
        } else if (thread >= 0 && thread < threads) {
            l.row.section = parts + thread;
            l.key0 = step;
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
