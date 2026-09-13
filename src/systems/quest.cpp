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
        d.completion_text   = o.value("completion", string(""));

        if (o.contains("req"))
            for (auto r = o["req"].begin(); r != o["req"].end(); ++r) {
                const int s = SkillFromName(r.key());
                if (s >= 0) d.requirements[s] = r.value().get<int>();
            }

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

bool QuestLog::CanStart(const string& id, const Skills& skills) const {
    const QuestDef* d = Definition(id);
    if (!d) return false;
    if (Status(id) != QuestStatus::NotStarted) return false;

    for (const auto& p : d->prerequisites)
        if (Status(p) != QuestStatus::Complete) return false;

    for (const auto& r : d->requirements)
        if (skills.Level(r.first) < r.second) return false;

    return true;
}

bool QuestLog::Start(const string& id) {
    const QuestDef* d = Definition(id);
    if (!d || Status(id) != QuestStatus::NotStarted) return false;

    QuestProgress p;
    p.status  = QuestStatus::Active;
    p.stage   = 0;
    p.counter = 0;
    progress[id] = p;
    just_started.push_back(id);

    // A quest whose first stage is already satisfied should not sit there
    // looking stuck.
    if (d->stages.empty()) {
        progress[id].status = QuestStatus::Complete;
        just_completed.push_back(id);
    }
    return true;
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
            just_completed.push_back(id);
            return;
        }
        // A Collect stage can already be satisfied by what the player carries,
        // so keep walking forward until one genuinely blocks.
        if (d->stages[p.stage].type != ObjectiveType::Collect) break;
    }
}

void QuestLog::Notify(const QuestEvent& e, const Inventory& inv) {
    for (auto& kv : progress) {
        QuestProgress& p = kv.second;
        if (p.status != QuestStatus::Active) continue;

        const QuestDef* d = Definition(kv.first);
        if (!d || p.stage >= static_cast<int>(d->stages.size())) continue;

        const QuestStage& st = d->stages[p.stage];
        if (st.type != e.type || st.target != e.target) continue;
        if (!st.map_id.empty() && st.map_id != e.map_id) continue;
        if (st.type == ObjectiveType::Deliver && st.deliver_to != e.secondary) continue;

        p.counter = std::min(p.counter + e.amount, st.count);
        AdvanceStage(kv.first, inv);
    }
}

void QuestLog::RefreshCollectObjectives(const Inventory& inv) {
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
    if (st.count > 1) text += " (" + std::to_string(p.counter) + "/" + std::to_string(st.count) + ")";
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

json QuestLog::ToJson() const {
    json j = json::object();
    for (const auto& kv : progress) {
        j[kv.first] = json{
            {"status",  static_cast<int>(kv.second.status)},
            {"stage",   kv.second.stage},
            {"counter", kv.second.counter}};
    }
    return j;
}

void QuestLog::FromJson(const json& j) {
    progress.clear();
    just_completed.clear();
    just_started.clear();
    if (!j.is_object()) return;

    for (auto it = j.begin(); it != j.end(); ++it) {
        QuestProgress p;
        p.status  = static_cast<QuestStatus>(it.value().value("status", 0));
        p.stage   = it.value().value("stage", 0);
        p.counter = it.value().value("counter", 0);
        progress[it.key()] = p;
    }
}
