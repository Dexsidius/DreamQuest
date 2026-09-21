#include "talents.h"
#include "skills.h"
#include <fstream>

namespace {
const char* kStyleKeys[3] = {"melee", "ranged", "magic"};
}

const TalentNode* TalentTree::At(int branch, int row) const {
    for (const TalentNode& n : nodes)
        if (n.branch == branch && n.row == row) return &n;
    return nullptr;
}

int TalentTree::BranchCount() const {
    int count = static_cast<int>(branches.size());
    for (const TalentNode& n : nodes) count = std::max(count, n.branch + 1);
    return std::max(count, SkillTrees::BRANCHES);
}

bool SkillTrees::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("SkillTrees: cannot open '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("SkillTrees: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (int s = 0; s < 3; ++s) {
        TalentTree& t = trees[s];
        t = TalentTree();
        t.id = kStyleKeys[s];
        if (!root.contains(t.id)) continue;
        const json& tj = root[t.id];
        t.name = tj.value("name", t.id);
        t.skill = std::max(0, SkillFromName(tj.value("skill", string("Attack"))));
        if (tj.contains("branches"))
            for (const auto& b : tj["branches"]) t.branches.push_back(b.get<string>());
        if (tj.contains("nodes"))
            for (const auto& nj : tj["nodes"]) {
                TalentNode n;
                n.id          = nj.value("id", string(""));
                n.name        = nj.value("name", n.id);
                n.description = nj.value("desc", string(""));
                n.technique   = nj.value("technique", string(""));
                n.ability     = nj.value("ability", string(""));
                n.cooldown    = nj.value("cooldown", 0.0f);
                n.stamina_cost = nj.value("stamina", 0);
                n.mana_cost   = nj.value("mana", 0);
                n.ranks       = std::max(1, nj.value("ranks", 1));
                n.branch      = nj.value("branch", 0);
                n.row         = nj.value("row", 0);
                n.level       = nj.value("level", 1);
                if (nj.contains("effects"))
                    for (auto e = nj["effects"].begin(); e != nj["effects"].end(); ++e)
                        n.effects[e.key()] = e.value().get<float>();
                t.nodes.push_back(n);
            }
    }
    boons.clear();
    if (root.contains("boons") && root["boons"].is_array())
        for (const auto& bj : root["boons"]) {
            BoonDef b;
            b.id   = bj.value("id", string(""));
            b.name = bj.value("name", b.id);
            b.text = bj.value("text", string(""));
            if (bj.contains("paths") && bj["paths"].is_array())
                for (const auto& p : bj["paths"])
                    for (int s = 0; s < 3; ++s)
                        if (p.is_string() && p.get<string>() == kStyleKeys[s]) b.paths.push_back(static_cast<AttackStyle>(s));
            if (bj.contains("effects") && bj["effects"].is_object())
                for (auto e = bj["effects"].begin(); e != bj["effects"].end(); ++e)
                    if (e.value().is_number()) b.effects[e.key()] = e.value().get<float>();
            if (!b.id.empty() && !b.effects.empty()) boons.push_back(b);
        }
    totems.clear();
    if (root.contains("totems") && root["totems"].is_array())
        for (const auto& tj : root["totems"]) {
            TotemDef t;
            t.item = tj.value("item", string(""));
            t.boss = tj.value("boss", string(""));
            t.name = tj.value("name", t.item);
            t.text = tj.value("text", string(""));
            if (tj.contains("effects") && tj["effects"].is_object())
                for (auto e = tj["effects"].begin(); e != tj["effects"].end(); ++e)
                    if (e.value().is_number()) t.effects[e.key()] = e.value().get<float>();
            if (!t.item.empty() && !t.boss.empty() && !t.effects.empty()) totems.push_back(t);
        }
    SDL_Log("SkillTrees: %d / %d / %d nodes, %d boons, %d totems", static_cast<int>(trees[0].nodes.size()),
            static_cast<int>(trees[1].nodes.size()), static_cast<int>(trees[2].nodes.size()),
            static_cast<int>(boons.size()), static_cast<int>(totems.size()));
    return true;
}

const TotemDef* SkillTrees::Totem(const string& item) const {
    for (const TotemDef& t : totems) if (t.item == item) return &t;
    return nullptr;
}

const TotemDef* SkillTrees::TotemOf(const string& boss) const {
    for (const TotemDef& t : totems) if (t.boss == boss) return &t;
    return nullptr;
}

const BoonDef* SkillTrees::Boon(const string& id) const {
    for (const BoonDef& b : boons) if (b.id == id) return &b;
    return nullptr;
}

const TalentNode* SkillTrees::Find(const string& id, AttackStyle* style) const {
    for (int s = 0; s < 3; ++s)
        for (const TalentNode& n : trees[s].nodes)
            if (n.id == id) {
                if (style) *style = static_cast<AttackStyle>(s);
                return &n;
            }
    return nullptr;
}

bool TalentEffectIsGlobal(const string& effect) {
    return effect == "defence" || effect == "stamina" || effect == "stamina_regen" ||
           effect == "move_speed" || effect == "mana_regen" || effect == "charge" ||
           effect == "max_mana" || effect == "evade" || effect == "hurt_mana" || effect == "block_cost" ||
           effect == "max_health";
}

int Talents::PointsEarned(AttackStyle style, const Skills& skills) const {
    if (!db) return 0;
    // A boss's point is for the character's own tree, which is the only one
    // they have; with no path set -- the self-test's plain Talents -- any.
    return skills.Level(db->Tree(style).skill) / SkillTrees::LEVELS_PER_POINT +
           (Open(style) ? BonusPoints() : 0);
}

Talents::Trophy Talents::SlayBoss(const string& boss_id, std::mt19937& rng) {
    Trophy out;
    if (boss_id.empty()) return out;
    // Every time is counted; the fifteenth is the one that leaves the totem,
    // and it is the count passing fifteen that does it, so there is one.
    out.kills = ++kills[boss_id];
    if (out.kills == TOTEM_KILLS && db) out.totem = db->TotemOf(boss_id);
    if (!slain.insert(boss_id).second) return out;
    out.first = true;
    if (!db) return out;
    // One they can use and have not got; and if they have every one of those,
    // one they can use -- a second helping rather than nothing.
    vector<const BoonDef*> fresh, any;
    for (const BoonDef& b : db->Boons()) {
        if (has_path && !b.For(path)) continue;
        any.push_back(&b);
        if (!HasBoon(b.id)) fresh.push_back(&b);
    }
    const vector<const BoonDef*>& from = fresh.empty() ? any : fresh;
    if (from.empty()) return out;
    out.boon = from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(rng)];
    boons.push_back(out.boon->id);
    return out;
}

string Talents::PlaceTotem(const string& item, int quest_day) {
    if (item.empty() || (db && !db->Totem(item))) return string();
    string was = placed == item ? string() : placed;
    placed = item;
    totem_day = quest_day;
    today = quest_day;
    return was;
}

string Talents::TakeTotem() {
    string was = placed;
    placed.clear();
    totem_day = -1;
    return was;
}

const TotemDef* Talents::ActiveTotem() const {
    return (db && TotemAwake()) ? db->Totem(placed) : nullptr;
}

float Talents::BoonEffect(const string& effect) const {
    if (!db) return 0.0f;
    float total = 0.0f;
    // The totem in the ring, if it has been touched today. One, ever.
    if (const TotemDef* t = ActiveTotem()) {
        const auto it = t->effects.find(effect);
        if (it != t->effects.end()) total += it->second;
    }
    for (const string& id : boons)
        if (const BoonDef* b = db->Boon(id)) {
            const auto it = b->effects.find(effect);
            if (it != b->effects.end()) total += it->second;
        }
    return total;
}

int Talents::PointsSpent(AttackStyle style) const {
    if (!db) return 0;
    int n = 0;
    for (const TalentNode& node : db->Tree(style).nodes) n += Rank(node.id);
    return n;
}

void Talents::SetPath(AttackStyle style) {
    has_path = true;
    path = style;
    DropOtherPaths();
}

void Talents::DropOtherPaths() {
    if (!db || !has_path) return;
    for (int s = 0; s < 3; ++s) {
        if (static_cast<AttackStyle>(s) == path) continue;
        for (const TalentNode& n : db->Tree(static_cast<AttackStyle>(s)).nodes) ranks.erase(n.id);
        technique[s].clear();
    }
    for (string& slot : ability) {
        AttackStyle style = AttackStyle::Melee;
        const TalentNode* n = slot.empty() ? nullptr : db->Find(slot, &style);
        if (!n || n->ability.empty() || style != path || !Has(slot)) slot.clear();
    }
}

const TalentNode* Talents::Ability(int slot) const {
    if (!db || slot < 0 || slot >= SkillTrees::ABILITY_SLOTS || ability[slot].empty()) return nullptr;
    const TalentNode* n = db->Find(ability[slot]);
    return (n && !n->ability.empty() && Has(n->id)) ? n : nullptr;
}

int Talents::SlotOf(const string& node_id) const {
    for (int i = 0; i < SkillTrees::ABILITY_SLOTS; ++i) if (ability[i] == node_id) return i;
    return -1;
}

int Talents::CycleAbility(const string& node_id) {
    const TalentNode* node = db ? db->Find(node_id) : nullptr;
    if (!node || node->ability.empty() || !Has(node_id)) return -1;
    const int at = SlotOf(node_id);
    if (at < 0) {
        // Not carried: into the first slot with room, so picking up a second
        // ability never costs the first. With no room, it takes slot one.
        for (int i = 0; i < SkillTrees::ABILITY_SLOTS; ++i)
            if (ability[i].empty()) { ability[i] = node_id; return i; }
        ability[0] = node_id;
        return 0;
    }
    if (at + 1 < SkillTrees::ABILITY_SLOTS) {
        // To the next slot, changing places with whatever is there.
        std::swap(ability[at], ability[at + 1]);
        return at + 1;
    }
    ability[at].clear();                           // from the last slot: put away
    return -1;
}

Talents::Why Talents::CanLearn(const string& id, const Skills& skills) const {
    AttackStyle style = AttackStyle::Melee;
    const TalentNode* node = db ? db->Find(id, &style) : nullptr;
    if (!node) return Why::Unknown;
    if (!Open(style)) return Why::OtherPath;
    if (Rank(id) >= node->ranks) return Why::Learned;
    if (skills.Level(db->Tree(style).skill) < node->level) return Why::Level;
    if (node->row > 0) {
        const TalentNode* above = db->Tree(style).At(node->branch, node->row - 1);
        if (above && !Has(above->id)) return Why::Prerequisite;
    }
    if (PointsFree(style, skills) <= 0) return Why::NoPoints;
    return Why::Ok;
}

bool Talents::Learn(const string& id, const Skills& skills) {
    if (CanLearn(id, skills) != Why::Ok) return false;
    ++ranks[id];
    return true;
}

void Talents::Reset(AttackStyle style) {
    if (!db) return;
    for (const TalentNode& n : db->Tree(style).nodes) {
        ranks.erase(n.id);
        for (string& slot : ability) if (slot == n.id) slot.clear();
    }
    technique[static_cast<int>(style)].clear();
}

bool Talents::ToggleTechnique(const string& node_id) {
    AttackStyle style = AttackStyle::Melee;
    const TalentNode* node = db ? db->Find(node_id, &style) : nullptr;
    if (!node || node->technique.empty() || !Has(node_id)) return false;
    string& slot = technique[static_cast<int>(style)];
    slot = (slot == node->technique) ? string() : node->technique;
    return true;
}

float Talents::Effect(const string& effect, AttackStyle style) const {
    if (!db) return 0.0f;
    if (TalentEffectIsGlobal(effect)) return Global(effect);
    float total = BoonEffect(effect);          // a boon is no style's: it is the character's
    for (const TalentNode& n : db->Tree(style).nodes) {
        const int rank = Rank(n.id);
        if (rank <= 0) continue;
        auto it = n.effects.find(effect);
        if (it != n.effects.end()) total += it->second * static_cast<float>(rank);
    }
    return total;
}

float Talents::Global(const string& effect) const {
    if (!db) return 0.0f;
    float total = BoonEffect(effect);
    for (int s = 0; s < 3; ++s)
        for (const TalentNode& n : db->Tree(static_cast<AttackStyle>(s)).nodes) {
            const int rank = Rank(n.id);
            if (rank <= 0) continue;
            auto it = n.effects.find(effect);
            if (it != n.effects.end()) total += it->second * static_cast<float>(rank);
        }
    return total;
}

json Talents::ToJson() const {
    json j;
    // "learned" is what saves have always had, and is still written so an
    // older build reads a newer save; "ranks" says how many of each.
    j["learned"] = json::array();
    j["ranks"] = json::object();
    for (const auto& [id, rank] : ranks) {
        if (rank <= 0) continue;
        j["learned"].push_back(id);
        j["ranks"][id] = rank;
    }
    for (int s = 0; s < 3; ++s)
        if (!technique[s].empty()) j["technique"][kStyleKeys[s]] = technique[s];
    j["abilities"] = json::array();
    for (const string& slot : ability) j["abilities"].push_back(slot);
    // What the bosses left. Unlearning a tree does not touch either: the point
    // comes back with the rest, and a boon was never bought.
    j["bosses"] = json::array();
    for (const string& id : slain) j["bosses"].push_back(id);
    j["boons"] = json::array();
    for (const string& id : boons) j["boons"].push_back(id);
    j["boss_kills"] = json::object();
    for (const auto& [id, n] : kills) if (n > 0) j["boss_kills"][id] = n;
    if (!placed.empty()) {
        j["totem"] = placed;
        j["totem_day"] = totem_day;
    }
    return j;
}

void Talents::FromJson(const json& j) {
    ranks.clear();
    slain.clear();
    kills.clear();
    boons.clear();
    placed.clear();
    totem_day = -1;
    for (string& t : technique) t.clear();
    for (string& a : ability) a.clear();
    if (!j.is_object()) return;
    if (j.contains("bosses") && j["bosses"].is_array())
        for (const auto& id : j["bosses"]) if (id.is_string()) slain.insert(id.get<string>());
    // Before the boons, which may be no more than the bosses: all of the bosses first.
    if (j.contains("boss_kills") && j["boss_kills"].is_object())
        for (auto it = j["boss_kills"].begin(); it != j["boss_kills"].end(); ++it)
            if (it.value().is_number_integer() && it.value().get<int>() > 0) {
                kills[it.key()] = it.value().get<int>();
                slain.insert(it.key());          // killed is killed, however it was written down
            }
    // A save from before kills were counted: each boss in it was killed once.
    for (const string& id : slain) if (!kills.count(id)) kills[id] = 1;
    if (j.contains("boons") && j["boons"].is_array())
        for (const auto& id : j["boons"])
            // One that has gone from the file is simply gone; never more than
            // there were bosses to leave them.
            if (id.is_string() && (!db || db->Boon(id.get<string>())) && boons.size() < slain.size())
                boons.push_back(id.get<string>());
    {
        const string standing = j.value("totem", string(""));
        if (!standing.empty() && (!db || db->Totem(standing))) {
            placed = standing;
            totem_day = j.value("totem_day", -1);
        }
    }
    if (j.contains("learned") && j["learned"].is_array())
        for (const auto& id : j["learned"]) if (id.is_string()) ranks[id.get<string>()] = 1;
    if (j.contains("ranks") && j["ranks"].is_object())
        for (auto it = j["ranks"].begin(); it != j["ranks"].end(); ++it)
            if (it.value().is_number_integer()) ranks[it.key()] = std::max(1, it.value().get<int>());
    // A node that has gone from the tree, or has fewer ranks than were bought.
    if (db)
        for (auto it = ranks.begin(); it != ranks.end();) {
            const TalentNode* n = db->Find(it->first);
            if (!n) { it = ranks.erase(it); continue; }
            it->second = std::min(it->second, n->ranks);
            ++it;
        }
    if (j.contains("technique") && j["technique"].is_object())
        for (int s = 0; s < 3; ++s)
            technique[s] = j["technique"].value(kStyleKeys[s], string(""));
    if (j.contains("abilities") && j["abilities"].is_array())
        for (size_t i = 0; i < j["abilities"].size() && i < static_cast<size_t>(SkillTrees::ABILITY_SLOTS); ++i)
            if (j["abilities"][i].is_string()) ability[i] = j["abilities"][i].get<string>();
    DropOtherPaths();
}
