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
                n.branch      = nj.value("branch", 0);
                n.row         = nj.value("row", 0);
                n.level       = nj.value("level", 1);
                if (nj.contains("effects"))
                    for (auto e = nj["effects"].begin(); e != nj["effects"].end(); ++e)
                        n.effects[e.key()] = e.value().get<float>();
                t.nodes.push_back(n);
            }
    }
    SDL_Log("SkillTrees: %d / %d / %d nodes", static_cast<int>(trees[0].nodes.size()),
            static_cast<int>(trees[1].nodes.size()), static_cast<int>(trees[2].nodes.size()));
    return true;
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
           effect == "move_speed" || effect == "mana_regen" || effect == "charge";
}

int Talents::PointsEarned(AttackStyle style, const Skills& skills) const {
    if (!db) return 0;
    return skills.Level(db->Tree(style).skill) / SkillTrees::LEVELS_PER_POINT;
}

int Talents::PointsSpent(AttackStyle style) const {
    if (!db) return 0;
    int n = 0;
    for (const TalentNode& node : db->Tree(style).nodes)
        if (Has(node.id)) ++n;
    return n;
}

Talents::Why Talents::CanLearn(const string& id, const Skills& skills) const {
    AttackStyle style = AttackStyle::Melee;
    const TalentNode* node = db ? db->Find(id, &style) : nullptr;
    if (!node) return Why::Unknown;
    if (Has(id)) return Why::Learned;
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
    learned.insert(id);
    return true;
}

void Talents::Reset(AttackStyle style) {
    if (!db) return;
    for (const TalentNode& n : db->Tree(style).nodes) learned.erase(n.id);
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
    float total = 0.0f;
    for (const TalentNode& n : db->Tree(style).nodes) {
        if (!Has(n.id)) continue;
        auto it = n.effects.find(effect);
        if (it != n.effects.end()) total += it->second;
    }
    return total;
}

float Talents::Global(const string& effect) const {
    if (!db) return 0.0f;
    float total = 0.0f;
    for (int s = 0; s < 3; ++s)
        for (const TalentNode& n : db->Tree(static_cast<AttackStyle>(s)).nodes) {
            if (!Has(n.id)) continue;
            auto it = n.effects.find(effect);
            if (it != n.effects.end()) total += it->second;
        }
    return total;
}

json Talents::ToJson() const {
    json j;
    j["learned"] = json::array();
    for (const string& id : learned) j["learned"].push_back(id);
    for (int s = 0; s < 3; ++s)
        if (!technique[s].empty()) j["technique"][kStyleKeys[s]] = technique[s];
    return j;
}

void Talents::FromJson(const json& j) {
    learned.clear();
    for (string& t : technique) t.clear();
    if (!j.is_object()) return;
    if (j.contains("learned"))
        for (const auto& id : j["learned"]) learned.insert(id.get<string>());
    if (j.contains("technique"))
        for (int s = 0; s < 3; ++s)
            technique[s] = j["technique"].value(kStyleKeys[s], string(""));
}
