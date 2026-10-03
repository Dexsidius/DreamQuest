#include "quest.h"
#include "skills.h"
#include "items.h"
#include <fstream>

static ObjectiveType ObjectiveFromName(const string& s) {
    if (s == "kill")     return ObjectiveType::Kill;
    if (s == "collect")  return ObjectiveType::Collect;
    if (s == "reach")    return ObjectiveType::Reach;
    if (s == "interact") return ObjectiveType::Interact;
    if (s == "deliver")  return ObjectiveType::Deliver;
    if (s == "craft" || s == "cook" || s == "make") return ObjectiveType::Craft;
    return ObjectiveType::Talk;
}

static QuestSource SourceFromName(const string& s) {
    if (s == "npc")  return QuestSource::Npc;
    if (s == "note") return QuestSource::Note;
    return QuestSource::Board;
}

bool QuestLog::LoadDefinitions(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("QuestLog: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("QuestLog: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        QuestDef d;
        d.id      = it.key();
        d.name    = o.value("name", d.id);
        d.summary = o.value("summary", string(""));
        d.source  = SourceFromName(o.value("source", string("board")));
        d.giver   = o.value("giver", string(""));
        d.recommended_level = o.value("level", 1);
        d.major = o.value("major", false);
        d.tutorial = o.value("tutorial", false);
        d.completion_text   = o.value("completion", string(""));
        d.then              = o.value("then", string(""));

        if (o.contains("req"))
            for (auto r = o["req"].begin(); r != o["req"].end(); ++r) {
                if (r.key() == "Combat") { d.combat_level = r.value().get<int>(); continue; }
                const int s = SkillFromName(r.key());
                if (s >= 0) d.requirements[s] = r.value().get<int>();
            }
        d.daily = o.value("repeat", string("")) == "daily";
        d.bounty = o.value("bounty", false);
        if (d.bounty) d.daily = true;
        // Whatever the file says, nothing off a board and nothing repeatable
        // is a story quest: those are what the side tab exists for.
        if (d.source == QuestSource::Board || d.daily) { d.major = false; d.tutorial = false; }
        if (d.tutorial) d.major = false;
        d.pool  = o.value("pool", d.giver);
        d.posts = std::max(0, o.value("posts", 0));

        if (o.contains("prereq"))
            for (const auto& p : o["prereq"]) d.prerequisites.push_back(p.get<string>());

        if (o.contains("stages"))
            for (const auto& s : o["stages"]) {
                QuestStage st;
                st.description = s.value("desc", string(""));
                st.type        = ObjectiveFromName(s.value("type", string("talk")));
                st.target      = s.value("target", string(""));
                st.deliver_to  = s.value("to", string(""));
                st.map_id      = s.value("map", string(""));
                st.count       = std::max(1, s.value("count", 1));
                st.start       = std::clamp(s.value("start", 0), 0, st.count - 1);
                st.where       = s.value("where", string(""));
                st.hidden      = s.value("hidden", false);
                d.stages.push_back(st);
            }

        if (o.contains("rewards")) {
            const json& r = o["rewards"];
            d.rewards.coins = r.value("coins", 0);
            if (r.contains("xp"))
                for (auto x = r["xp"].begin(); x != r["xp"].end(); ++x) {
                    const int s = SkillFromName(x.key());
                    if (s >= 0) d.rewards.xp[s] = x.value().get<int>();
                }
            if (r.contains("items"))
                for (const auto& i : r["items"])
                    d.rewards.items.emplace_back(i.value("id", string("")), i.value("qty", 1));
            // "choices": one of these as well, the player's pick. Each is an
            // object with any of "label", "style" ("melee", "ranged", "magic"),
            // "items", "coins" and "xp" -- or, for an option that is one thing,
            // "id" and "qty" on the option itself.
            if (r.contains("choices") && r["choices"].is_array())
                for (const auto& c : r["choices"]) {
                    if (!c.is_object()) continue;
                    QuestRewardChoice ch;
                    ch.label = c.value("label", string(""));
                    ch.style = c.value("style", string(""));
                    if (!ch.style.empty() && ch.style != "melee" && ch.style != "ranged" && ch.style != "magic") {
                        SDL_Log("QuestLog: %s offers a choice for \"%s\", which is no way of fighting "
                                "(melee, ranged or magic)", d.id.c_str(), ch.style.c_str());
                        ch.style.clear();
                    }
                    ch.coins = c.value("coins", 0);
                    if (c.contains("xp") && c["xp"].is_object())
                        for (auto x = c["xp"].begin(); x != c["xp"].end(); ++x) {
                            const int s = SkillFromName(x.key());
                            if (s >= 0) ch.xp[s] = x.value().get<int>();
                        }
                    if (c.contains("items") && c["items"].is_array())
                        for (const auto& i : c["items"])
                            ch.items.emplace_back(i.value("id", string("")), std::max(1, i.value("qty", 1)));
                    if (c.contains("id"))
                        ch.items.emplace_back(c.value("id", string("")), std::max(1, c.value("qty", 1)));
                    d.rewards.choices.push_back(ch);
                }
        }

        defs[d.id] = d;
    }

    SDL_Log("QuestLog: loaded %d quests", static_cast<int>(defs.size()));
    return true;
}

const QuestDef* QuestLog::Definition(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

QuestStatus QuestLog::Status(const string& id) const {
    auto it = progress.find(id);
    return it == progress.end() ? QuestStatus::NotStarted : it->second.status;
}

int QuestLog::Stage(const string& id) const {
    auto it = progress.find(id);
    return it == progress.end() ? 0 : it->second.stage;
}

int QuestLog::Counter(const string& id) const {
    auto it = progress.find(id);
    return it == progress.end() ? 0 : it->second.counter;
}

void QuestLog::SetDay(int day) {
    if (day == today && !sweep) return;
    today = day;
    sweep = false;
    // A bounty is for one night's monsters, and they are not there the next:
    // whatever of one was not done lapses at dawn. How many times it has been
    // done stays, and a reward from one finished is still owed.
    for (auto& [id, p] : progress) {
        const QuestDef* d = Definition(id);
        if (!d || !d->bounty || p.status != QuestStatus::Active || p.started_day >= today) continue;
        p.status  = QuestStatus::NotStarted;
        p.stage   = 0;
        p.counter = 0;
        ++lapsed;
    }
}

void QuestLog::PostBounties(int day, const vector<string>& ids) {
    bounty_day = day;
    bounties = std::set<string>(ids.begin(), ids.end());
}

int QuestLog::PostsPerDay(const string& pool) const {
    int posts = 0;
    for (const auto& kv : defs)
        if (kv.second.daily && !kv.second.bounty && kv.second.pool == pool) posts = std::max(posts, kv.second.posts);
    return posts > 0 ? posts : DAILY_PER_POOL;
}

bool QuestLog::MeetsRequirements(const QuestDef& d, const Skills& skills) const {
    if (d.combat_level > 0 && skills.CombatLevel() < d.combat_level) return false;
    for (const auto& p : d.prerequisites)
        if (Status(p) != QuestStatus::Complete) return false;
    for (const auto& r : d.requirements)
        if (skills.Level(r.first) < r.second) return false;
    return true;
}

vector<string> QuestLog::PoolToday(const string& pool, const Skills* skills) const {
    vector<string> members;
    for (const auto& kv : defs)
        if (kv.second.daily && !kv.second.bounty && kv.second.pool == pool) members.push_back(kv.first);
    std::sort(members.begin(), members.end());

    // Shuffle the pool with the day and the pool's name as the seed, and post
    // the first few: stable all day, different tomorrow. The order does not
    // depend on the player, so gaining a level only lets a quest they could
    // not yet take be replaced by one they can.
    unsigned seed = 2166136261u;
    for (char c : pool) seed = (seed ^ static_cast<unsigned char>(c)) * 16777619u;
    seed ^= static_cast<unsigned>(today) * 2654435761u;
    std::mt19937 rng(seed);
    std::shuffle(members.begin(), members.end(), rng);

    const int posts = PostsPerDay(pool);
    vector<string> out;
    if (!skills) {
        for (const string& id : members) {
            if (static_cast<int>(out.size()) >= posts) break;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    // Work at the player's own level first. An order more than ORDER_BAND
    // levels below them, in the trade it is for, is beneath them: it is
    // posted only when there is not enough at their level to fill the day,
    // and then the highest of it first. A master smith used to be posted
    // bronze swords as often as anyone, from a book that had nothing above
    // steel in it.
    vector<string> stale;
    for (const string& id : members) {
        const QuestDef& d = defs.at(id);
        if (!MeetsRequirements(d, *skills)) continue;
        if (skills->Level(TradeOf(d)) - OrderLevel(d) > ORDER_BAND) { stale.push_back(id); continue; }
        if (static_cast<int>(out.size()) < posts) out.push_back(id);
    }
    std::stable_sort(stale.begin(), stale.end(), [&](const string& a, const string& b) {
        return OrderLevel(defs.at(a)) > OrderLevel(defs.at(b));
    });
    for (const string& id : stale) {
        if (static_cast<int>(out.size()) >= posts) break;
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int QuestLog::TradeOf(const QuestDef& d) {
    // The skill it pays most in: a cook's order for moonpetal tea asks for
    // Foraging to find the petals, but it is Cooking it is for. Paying no
    // skill at all, the one it asks most of.
    int skill = -1, paid = -1;
    for (const auto& x : d.rewards.xp)
        if (x.second > paid) { skill = x.first; paid = x.second; }
    if (skill >= 0) return skill;
    int most = -1;
    for (const auto& r : d.requirements)
        if (r.second > most) { skill = r.first; most = r.second; }
    return skill >= 0 ? skill : 0;
}

int QuestLog::OrderLevel(const QuestDef& d) {
    const int skill = TradeOf(d);
    const auto it = d.requirements.find(skill);
    return it == d.requirements.end() ? 1 : it->second;
}

bool QuestLog::OfferedToday(const string& id, const Skills* skills) const {
    const QuestDef* d = Definition(id);
    if (!d || !d->daily) return true;
    if (Status(id) == QuestStatus::Active) return true;
    if (d->bounty) return bounty_day == today && bounties.count(id) > 0;
    const vector<string> posted = PoolToday(d->pool, skills);
    return std::find(posted.begin(), posted.end(), id) != posted.end();
}

vector<string> QuestLog::ReadyToDeliver(const string& npc, const Inventory& inv) const {
    vector<string> out;
    for (const auto& kv : progress) {
        const QuestProgress& p = kv.second;
        const QuestDef* d = Definition(kv.first);
        if (!d || !d->daily || d->giver != npc || p.status != QuestStatus::Active) continue;
        if (p.stage >= static_cast<int>(d->stages.size())) continue;
        const QuestStage& st = d->stages[p.stage];
        if (st.type != ObjectiveType::Deliver || st.deliver_to != npc) continue;
        if (inv.Count(st.target) >= st.count - p.counter) out.push_back(kv.first);
    }
    return out;
}

int QuestLog::Completions(const string& id) const {
    auto it = progress.find(id);
    return it == progress.end() ? 0 : it->second.completions;
}

bool QuestLog::CanStart(const string& id, const Skills& skills) const {
    const QuestDef* d = Definition(id);
    if (!d) return false;
    const QuestStatus st = Status(id);
    if (d->daily) {
        // Taken once a day at most, and only while its board is posting it.
        if (st == QuestStatus::Active) return false;
        if (st == QuestStatus::Complete && progress.at(id).completed_day >= today) return false;
        if (!OfferedToday(id, &skills)) return false;
    } else if (st != QuestStatus::NotStarted) {
        return false;
    }
    return MeetsRequirements(*d, skills);
}

vector<const ItemDef*> QuestLog::StartFromFinds(const Inventory& bag, const ItemDatabase& items) {
    vector<const ItemDef*> begun;
    for (int i = 0; i < bag.SlotCount(); ++i) {
        const string& id = bag.Slot(i).id;
        if (id.empty()) continue;
        const ItemDef* d = items.Get(id);
        if (!d || d->starts_quest.empty() || Status(d->starts_quest) != QuestStatus::NotStarted) continue;
        if (Start(d->starts_quest)) begun.push_back(d);
    }
    return begun;
}

bool QuestLog::Start(const string& id) {
    const QuestDef* d = Definition(id);
    if (!d) return false;
    const QuestStatus st = Status(id);
    // A finished daily can be taken again on a later day; nothing else can.
    const bool again = d->daily && st == QuestStatus::Complete &&
                       progress[id].completed_day < today;
    if (st != QuestStatus::NotStarted && !again) return false;

    QuestProgress p;
    // Taken before -- done, or a bounty that lapsed -- it keeps its count of
    // times done, and a pick still owed from the last time is owed yet.
    if (const auto before = progress.find(id); before != progress.end()) {
        p.completions = before->second.completions;
        p.completed_day = before->second.completed_day;
        p.owed = before->second.owed;
        p.chosen = before->second.chosen;
    }
    p.started_day = today;
    p.status  = QuestStatus::Active;
    p.stage   = 0;
    p.counter = d->stages.empty() ? 0 : d->stages.front().start;
    progress[id] = p;
    just_started.push_back(id);
    taken_order.erase(std::remove(taken_order.begin(), taken_order.end(), id), taken_order.end());
    taken_order.push_back(id);
    // The newest is the one followed, unless the player has said otherwise.
    if (!Chosen()) { followed = id; chosen = false; }

    // A quest whose first stage is already satisfied should not sit there
    // looking stuck.
    if (d->stages.empty()) {
        progress[id].status = QuestStatus::Complete;
        if (!d->rewards.choices.empty()) ++progress[id].owed;
        just_completed.push_back(id);
        if (!d->then.empty()) follow_ups.push_back(d->then);
        BeginFollowUps();
    }
    return true;
}

void QuestLog::BeginFollowUps() {
    // Taken out before any is begun: beginning one can finish it at once -- a
    // quest with no stages -- and that can name another.
    for (int depth = 0; depth < 8 && !follow_ups.empty(); ++depth) {
        vector<string> now;
        now.swap(follow_ups);
        for (const string& id : now) Start(id);
    }
    follow_ups.clear();
}

bool QuestLog::StageSatisfied(const QuestDef& def, const QuestProgress& p,
                              const Inventory& inv) const {
    if (p.stage >= static_cast<int>(def.stages.size())) return true;
    const QuestStage& st = def.stages[p.stage];

    if (st.type == ObjectiveType::Collect)
        return inv.Count(st.target) >= st.count;

    return p.counter >= st.count;
}

void QuestLog::AdvanceStage(const string& id, const Inventory& inv) {
    auto it = progress.find(id);
    if (it == progress.end()) return;
    const QuestDef* d = Definition(id);
    if (!d) return;

    QuestProgress& p = it->second;
    while (p.status == QuestStatus::Active && StageSatisfied(*d, p, inv)) {
        ++p.stage;
        p.counter = 0;
        if (p.stage >= static_cast<int>(d->stages.size())) {
            p.status = QuestStatus::Complete;
            p.completed_day = today;
            ++p.completions;
            if (!d->rewards.choices.empty()) ++p.owed;
            just_completed.push_back(id);
            // What it leads to begins once whatever is walking the journal
            // has finished with it: see BeginFollowUps.
            if (!d->then.empty()) follow_ups.push_back(d->then);
            return;
        }
        p.counter = d->stages[p.stage].start;
        // A Collect stage can already be satisfied by what the player carries,
        // so keep walking forward until one genuinely blocks.
        if (d->stages[p.stage].type != ObjectiveType::Collect) break;
    }
}

void QuestLog::Notify(const QuestEvent& e, const Inventory& inv) {
    if (relay) { relayed.push_back(e); return; }
    for (auto& kv : progress) {
        QuestProgress& p = kv.second;
        if (p.status != QuestStatus::Active) continue;
        if (!e.quest.empty() && kv.first != e.quest) continue;

        const QuestDef* d = Definition(kv.first);
        if (!d || p.stage >= static_cast<int>(d->stages.size())) continue;

        const QuestStage& st = d->stages[p.stage];
        // A boss's kill says what kind of thing it was (a chief is a
        // "lizardman", for the contracts) and, as its second word, which one:
        // a stage can name either.
        const bool named = st.type == ObjectiveType::Kill && !e.secondary.empty() && st.target == e.secondary;
        if (st.type != e.type || (st.target != e.target && !named)) continue;
        if (!st.map_id.empty() && st.map_id != e.map_id) continue;
        if (st.type == ObjectiveType::Deliver && st.deliver_to != e.secondary) continue;

        // A negative amount takes progress back -- a ritual broken takes back
        // the waves it had got through -- but never below nothing.
        p.counter = std::clamp(p.counter + e.amount, 0, st.count);
        AdvanceStage(kv.first, inv);
    }
    BeginFollowUps();
}

void QuestLog::RefreshCollectObjectives(const Inventory& inv) {
    if (relay) return;
    // Copy the ids first: AdvanceStage mutates the map it would iterate.
    vector<string> active;
    for (const auto& kv : progress)
        if (kv.second.status == QuestStatus::Active) active.push_back(kv.first);

    // A collect stage is judged by what the player is carrying, not by a
    // running count, but the tracker prints the counter -- which nothing used
    // to write, so it read (0/12) with eight logs in the bag right up until
    // the quest suddenly completed. Mirror the carried amount into it.
    const auto sync = [&](QuestProgress& p, const QuestDef& d) {
        if (p.status != QuestStatus::Active) return;
        if (p.stage >= static_cast<int>(d.stages.size())) return;
        const QuestStage& st = d.stages[p.stage];
        if (st.type == ObjectiveType::Collect)
            p.counter = std::min(inv.Count(st.target), st.count);
    };

    for (const auto& id : active) {
        const QuestDef* d = Definition(id);
        if (!d) continue;
        QuestProgress& p = progress[id];
        if (p.stage >= static_cast<int>(d->stages.size())) continue;
        if (d->stages[p.stage].type != ObjectiveType::Collect) continue;
        sync(p, *d);
        AdvanceStage(id, inv);
        sync(p, *d);    // the stage it moved on to may be a collect stage too
    }
    BeginFollowUps();
}

vector<string> QuestLog::Active() const {
    vector<string> out;
    for (const auto& kv : progress)
        if (kv.second.status == QuestStatus::Active) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

vector<string> QuestLog::Completed() const {
    vector<string> out;
    for (const auto& kv : progress)
        if (kv.second.status == QuestStatus::Complete) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

vector<string> QuestLog::AvailableFrom(const string& giver, const Skills& skills) const {
    vector<string> out;
    for (const auto& kv : defs)
        if (kv.second.giver == giver && CanStart(kv.first, skills))
            out.push_back(kv.first);
    std::sort(out.begin(), out.end(), [&](const string& a, const string& b) {
        return defs.at(a).recommended_level < defs.at(b).recommended_level;
    });
    return out;
}

string QuestLog::CurrentObjectiveText(const string& id) const {
    const QuestDef* d = Definition(id);
    if (!d) return "";

    auto it = progress.find(id);
    if (it == progress.end()) return d->summary;
    const QuestProgress& p = it->second;

    if (p.status == QuestStatus::Complete) return "Complete";
    if (p.stage >= static_cast<int>(d->stages.size())) return "Complete";

    const QuestStage& st = d->stages[p.stage];
    string text = st.description;
    // A collect stage counts what is carried and completes by itself, so its
    // counter is worth printing. A deliver stage is the walk back with the
    // goods: its counter is what has been handed over, which is nothing until
    // the moment it is everything, and "(0/10)" under "Bring the 10 logs to
    // Jessa" with ten logs in the bag read as the game having lost count. So
    // a deliver stage says only what to do.
    if (st.count > 1 && st.type != ObjectiveType::Deliver)
        text += " (" + std::to_string(p.counter) + "/" + std::to_string(st.count) + ")";
    return text;
}

vector<string> QuestLog::TakeJustCompleted() {
    vector<string> out;
    out.swap(just_completed);
    return out;
}

vector<string> QuestLog::TakeJustStarted() {
    vector<string> out;
    out.swap(just_started);
    return out;
}

int QuestLog::ChoicesOwed(const string& id) const {
    const QuestDef* d = Definition(id);
    auto it = progress.find(id);
    // A save that owes a pick of a quest the file no longer offers one of owes
    // nothing: there is nothing to pick from.
    if (!d || d->rewards.choices.empty() || it == progress.end()) return 0;
    return it->second.owed;
}

vector<string> QuestLog::WithChoicesOwed() const {
    vector<string> out;
    for (const auto& kv : progress)
        if (ChoicesOwed(kv.first) > 0) out.push_back(kv.first);
    return out;
}

const QuestRewardChoice* QuestLog::TakeChoice(const string& id, int index) {
    if (ChoicesOwed(id) <= 0) return nullptr;
    const QuestDef* d = Definition(id);
    if (index < 0 || index >= static_cast<int>(d->rewards.choices.size())) return nullptr;
    QuestProgress& p = progress[id];
    --p.owed;
    p.chosen = index;
    return &d->rewards.choices[index];
}

int QuestLog::LastChosen(const string& id) const {
    auto it = progress.find(id);
    return it == progress.end() ? -1 : it->second.chosen;
}

string QuestLog::StyleOf(const QuestRewardChoice& c, const ItemDatabase& items) {
    if (!c.style.empty()) return c.style;
    // What it hands over says whose it is: a weapon by how it is used, a
    // piece of armour by the style it adds to.
    for (const auto& thing : c.items) {
        const ItemDef* d = items.Get(thing.first);
        if (!d || d->slot == SLOT_NONE) continue;
        if (d->slot == SLOT_WEAPON)
            return d->kind == WeaponKind::Bow ? "ranged" : d->kind == WeaponKind::Staff ? "magic" : "melee";
        if (d->magic_bonus > 0 && d->magic_bonus >= d->ranged_bonus) return "magic";
        if (d->ranged_bonus > 0) return "ranged";
        if (d->attack_bonus > 0 || d->strength_bonus > 0) return "melee";
    }
    return "";
}

string QuestLog::Followed() const {
    if (!followed.empty() && IsActive(followed)) return followed;
    for (auto it = taken_order.rbegin(); it != taken_order.rend(); ++it)
        if (IsActive(*it)) return *it;
    const vector<string> active = Active();
    return active.empty() ? string() : active.front();
}

void QuestLog::Follow(const string& id) {
    if (!IsActive(id)) return;
    if (chosen && followed == id) { chosen = false; followed.clear(); return; }
    followed = id;
    chosen = true;
}

json QuestLog::ToJson() const {
    json j = json::object();
    // Beside the quests, under a name no quest has.
    j["_following"] = json{{"quest", followed}, {"chosen", chosen}, {"order", taken_order}};
    for (const auto& kv : progress) {
        json q = json{
            {"status",  static_cast<int>(kv.second.status)},
            {"stage",   kv.second.stage},
            {"counter", kv.second.counter},
            {"completed_day", kv.second.completed_day},
            {"completions", kv.second.completions}};
        if (kv.second.started_day >= 0) q["started_day"] = kv.second.started_day;
        // Only where there is a choice to speak of.
        if (kv.second.owed > 0)    q["owed"] = kv.second.owed;
        if (kv.second.chosen >= 0) q["chosen"] = kv.second.chosen;
        j[kv.first] = q;
    }
    return j;
}

void QuestLog::FromJson(const json& j) {
    progress.clear();
    just_completed.clear();
    just_started.clear();
    follow_ups.clear();
    if (!j.is_object()) return;

    followed.clear();
    chosen = false;
    taken_order.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key() == "_following") {
            if (!it.value().is_object()) continue;
            followed = it.value().value("quest", string());
            chosen   = it.value().value("chosen", false);
            if (it.value().contains("order") && it.value()["order"].is_array())
                for (const json& q : it.value()["order"]) if (q.is_string()) taken_order.push_back(q.get<string>());
            continue;
        }
        if (!it.value().is_object()) continue;
        QuestProgress p;
        p.status  = static_cast<QuestStatus>(it.value().value("status", 0));
        p.stage   = it.value().value("stage", 0);
        p.counter = it.value().value("counter", 0);
        p.completed_day = it.value().value("completed_day", -1);
        p.started_day = it.value().value("started_day", -1);
        p.completions = it.value().value("completions", p.status == QuestStatus::Complete ? 1 : 0);
        p.owed    = std::max(0, it.value().value("owed", 0));
        p.chosen  = it.value().value("chosen", -1);
        progress[it.key()] = p;
    }
    // A quest finished before it led anywhere -- in a save from a build that
    // did not have what comes after it -- leads on now, as it would have: the
    // "Quest started" for it is waiting when the game is loaded.
    for (const auto& kv : defs)
        if (!kv.second.then.empty() && Status(kv.first) == QuestStatus::Complete &&
            Status(kv.second.then) == QuestStatus::NotStarted)
            follow_ups.push_back(kv.second.then);
    BeginFollowUps();
    sweep = true;
}
