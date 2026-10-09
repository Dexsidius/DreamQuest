#pragma once
#include "quest.h"

// =============================================================================
//  The quest journal's lists: which tab a quest is in, and under which heading.
//
//  The story is told in parts -- the prologue and the acts (data/chapters.json)
//  -- and the Story tab lists each part under its own heading, its quests in
//  the order they come, so where the story has got to reads off the page; the
//  major quests that belong to no act come after, as Tales of the Hollowmarch.
//  The errands and lessons alongside a part (the Dawn Chimes, the gatherers'
//  lessons, a house in Mossvale) keep to the side quests' and tutorials' tabs,
//  under "Alongside" the part's title, and the rest of those tabs is headed by
//  what it is: favours people ask, the Guild's ledger, the boards and orders,
//  the trades.
//
//  A part is not in the journal at all until it has begun -- one of its own
//  quests taken -- so nothing of the second act, not even its title, is read
//  there while the first is being played. What is in hand or done is always
//  listed, wherever it belongs.
//
//  Worked out here rather than in the screen, so the self-test can ask it.
// =============================================================================

// One of the story's parts.
struct QuestChapter {
    string id, title, sub;           // "act1", "Act I", "Learning the Rules"
    vector<string> quests, side;     // its own, in order; and those alongside it
};

class QuestChapters {
public:
    bool Load(const string& path);
    const vector<QuestChapter>& All() const { return chapters; }
    // Which part a quest is of (its index in All), whether it is alongside the
    // part rather than one of its own, and where in the part it comes; -1 for
    // a quest of no part.
    int Of(const string& quest_id, bool* side = nullptr, int* place = nullptr) const;

private:
    struct Where { int chapter = -1; bool side = false; int place = 0; };
    vector<QuestChapter> chapters;
    unordered_map<string, Where> where;
};

namespace Journal {

enum Tab { STORY = 0, TUTORIALS = 1, SIDE = 2, TABS = 3 };
// The three states a line can be in, as the journal colours them.
enum State { AHEAD = 0, IN_HAND = 1, DONE = 2 };

// Which tab a quest belongs in: the story's own, the lessons, or everything else.
int TabOf(const QuestDef& d);

struct Row {
    string id;
    int state = AHEAD;
    int section = 0;                 // index into Page::sections
};
struct Section {
    string title, sub;               // "Act I", "Learning the Rules"; "Favours", ""
    int count = 0, done = 0;         // quests listed under it, and how many are done
};
// One tab's list: its headings, and every line under them in order, each line
// after its heading. A heading nothing is listed under is left out.
struct Page {
    vector<Section> sections;
    vector<Row> rows;
    int in_hand = 0;
};

// What a tab lists: every quest of it in hand, every one still ahead (but not
// the dailies, which come back every day and would bury the rest, and not a
// part's that has not begun), and every one done.
Page Build(const QuestLog& log, const QuestChapters& chapters, int tab);
// Whether a part of the story has begun: one of its own quests taken.
bool Begun(const QuestLog& log, const QuestChapter& chapter);

}   // namespace Journal
