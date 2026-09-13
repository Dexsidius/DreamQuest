#include "skills.h"

static const char* kSkillNames[SKILL_COUNT] = {
    "Attack", "Strength", "Defence", "Hitpoints", "Ranged",
    "Magic", "Woodcutting", "Mining", "Crafting", "Cooking", "Fishing"
};

const char* SkillName(int skill) {
    if (skill < 0 || skill >= SKILL_COUNT) return "?";
    return kSkillNames[skill];
}

int SkillFromName(const string& name) {
    for (int i = 0; i < SKILL_COUNT; ++i) {
        const string a = kSkillNames[i];
        if (a.size() != name.size()) continue;
        bool same = true;
        for (size_t c = 0; c < a.size(); ++c)
            if (tolower(a[c]) != tolower(name[c])) { same = false; break; }
        if (same) return i;
    }
    return -1;
}

// Built once; the curve is fixed so there is no reason to recompute it.
static const vector<int>& XpTable() {
    static vector<int> table = [] {
        vector<int> t(MAX_SKILL_LEVEL + 2, 0);
        double points = 0;
        for (int level = 1; level <= MAX_SKILL_LEVEL; ++level) {
            t[level] = static_cast<int>(points / 4.0);
            points += floor(level + 300.0 * pow(2.0, level / 7.0));
        }
        t[MAX_SKILL_LEVEL + 1] = t[MAX_SKILL_LEVEL];
        return t;
    }();
    return table;
}

int XpForLevel(int level) {
    level = std::clamp(level, 1, MAX_SKILL_LEVEL);
    return XpTable()[level];
}

int LevelForXp(int xp) {
    const vector<int>& t = XpTable();
    int level = 1;
    while (level < MAX_SKILL_LEVEL && xp >= t[level + 1]) ++level;
    return level;
}

Skills::Skills() {
    for (int i = 0; i < SKILL_COUNT; ++i) xp[i] = 0;
    // Hitpoints starts at 10, as it does in OSRS, so a new character is not
    // one hit from death.
    xp[SKILL_HITPOINTS] = XpForLevel(10);
    ResetCurrent();
}

int Skills::Xp(int skill) const {
    if (skill < 0 || skill >= SKILL_COUNT) return 0;
    return xp[skill];
}

int Skills::Level(int skill) const {
    if (skill < 0 || skill >= SKILL_COUNT) return 1;
    return LevelForXp(xp[skill]);
}

int Skills::Current(int skill) const {
    if (skill < 0 || skill >= SKILL_COUNT) return 1;
    return current[skill];
}

void Skills::SetCurrent(int skill, int value) {
    if (skill < 0 || skill >= SKILL_COUNT) return;
    // Boosts may exceed the base level, but nothing drops below zero.
    current[skill] = std::max(0, value);
}

void Skills::ResetCurrent() {
    for (int i = 0; i < SKILL_COUNT; ++i) current[i] = LevelForXp(xp[i]);
}

bool Skills::AddXp(int skill, int amount, LevelUp& out) {
    if (skill < 0 || skill >= SKILL_COUNT || amount <= 0) return false;

    const int before = LevelForXp(xp[skill]);
    xp[skill] = std::min(xp[skill] + amount, XpForLevel(MAX_SKILL_LEVEL));
    const int after = LevelForXp(xp[skill]);

    if (after > before) {
        // A level in a skill also raises the working value, and gaining
        // Hitpoints levels heals the difference rather than leaving a gap.
        current[skill] += (after - before);
        out.skill = skill;
        out.level = after;
        return true;
    }
    return false;
}

void Skills::SetXp(int skill, int value) {
    if (skill < 0 || skill >= SKILL_COUNT) return;
    xp[skill] = std::clamp(value, 0, XpForLevel(MAX_SKILL_LEVEL));
}

int Skills::CombatLevel() const {
    const double att = Level(SKILL_ATTACK), str = Level(SKILL_STRENGTH);
    const double def = Level(SKILL_DEFENCE), hp  = Level(SKILL_HITPOINTS);
    const double rng = Level(SKILL_RANGED),  mag = Level(SKILL_MAGIC);

    const double base = 0.25 * (def + hp);
    const double melee  = 0.325 * (att + str);
    const double ranged = 0.325 * (floor(rng / 2.0) + rng);
    const double magic  = 0.325 * (floor(mag / 2.0) + mag);

    return static_cast<int>(floor(base + std::max({melee, ranged, magic})));
}

int Skills::TotalLevel() const {
    int total = 0;
    for (int i = 0; i < SKILL_COUNT; ++i) total += Level(i);
    return total;
}

long long Skills::TotalXp() const {
    long long total = 0;
    for (int i = 0; i < SKILL_COUNT; ++i) total += xp[i];
    return total;
}

json Skills::ToJson() const {
    json j;
    for (int i = 0; i < SKILL_COUNT; ++i) {
        j["xp"][kSkillNames[i]] = xp[i];
        j["current"][kSkillNames[i]] = current[i];
    }
    return j;
}

void Skills::FromJson(const json& j) {
    for (int i = 0; i < SKILL_COUNT; ++i) xp[i] = 0;
    xp[SKILL_HITPOINTS] = XpForLevel(10);

    if (j.contains("xp"))
        for (auto it = j["xp"].begin(); it != j["xp"].end(); ++it) {
            const int s = SkillFromName(it.key());
            if (s >= 0) xp[s] = it.value().get<int>();
        }

    ResetCurrent();

    if (j.contains("current"))
        for (auto it = j["current"].begin(); it != j["current"].end(); ++it) {
            const int s = SkillFromName(it.key());
            if (s >= 0) current[s] = it.value().get<int>();
        }
}
