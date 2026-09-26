#include "skills.h"

static const char* kSkillNames[SKILL_COUNT] = {
    "Attack", "Strength", "Defence", "Hitpoints", "Ranged",
    "Magic", "Woodcutting", "Mining", "Crafting", "Cooking", "Fishing",
    "Smithing", "Foraging", "Brewing", "Tanning", "Clothier", "Enchanting"
};

const char* CategoryName(int category) {
    switch (category) {
        case CATEGORY_FORGING:    return "Forging";
        case CATEGORY_COMBAT:     return "Combat";
        case CATEGORY_GATHERING:  return "Gathering";
        case CATEGORY_WITCHCRAFT: return "Witchcraft";
        default:                  return "?";
    }
}

const char* CategoryBlurb(int category) {
    switch (category) {
        case CATEGORY_FORGING:    return "Made at a station: the anvil, the rack, the loom and the bench.";
        case CATEGORY_COMBAT:     return "Fighting, and staying alive through it.";
        case CATEGORY_GATHERING:  return "Taken from the land, and cooked over a fire.";
        case CATEGORY_WITCHCRAFT: return "Brews at a cauldron, and charms at an enchanting table.";
        default:                  return "";
    }
}

const char* SkillBlurb(int skill) {
    switch (skill) {
        case SKILL_ATTACK:      return "Trained by landing light attacks. It decides whether a blow lands.";
        case SKILL_STRENGTH:    return "Trained by landing strong and charged attacks. It decides how hard a blow lands.";
        case SKILL_DEFENCE:     return "Trained by taking hits. It softens every blow that reaches you.";
        case SKILL_HITPOINTS:   return "Trained by every blow you deal. It is how much you can take.";
        case SKILL_RANGED:      return "Trained by landing shots with a bow, a crossbow or knives.";
        case SKILL_MAGIC:       return "Trained by landing spells with a staff, a wand, a grimoire or an orb.";
        case SKILL_WOODCUTTING: return "Trained by chopping trees, with an axe.";
        case SKILL_MINING:      return "Trained by working ore seams, with a pickaxe.";
        case SKILL_CRAFTING:    return "Trained at a workbench: the wooden tier, bows, a rod, a dreamcatcher.";
        case SKILL_COOKING:     return "Trained at a fire, with something raw in your pack.";
        case SKILL_FISHING:     return "Trained by fishing ponds, streams and lakes, with a rod.";
        case SKILL_SMITHING:    return "Trained by smelting bars and smithing metal at an anvil.";
        case SKILL_FORAGING:    return "Trained by picking herbs and plants, and catching bugs.";
        case SKILL_BREWING:     return "Trained by brewing potions and dyes at a cauldron.";
        case SKILL_TANNING:     return "Trained on a tanning rack: hides, boots, bags and bedrolls. Nessa's book pays in it.";
        case SKILL_CLOTHIER:    return "Trained at a loom: cloth from any fibre, and the robes. Wynn's book pays in it.";
        case SKILL_ENCHANTING:  return "Trained at an enchanting table. Every tier of a charm the level reaches is yours to work.";
        default:                return "";
    }
}

const vector<int>& CategorySkills(int category) {
    // Making things at a station; fighting; taking things from the land, and
    // cooking what was taken; and the two that are half magic.
    static const vector<int> kSkills[CATEGORY_COUNT] = {
        {SKILL_SMITHING, SKILL_TANNING, SKILL_CLOTHIER, SKILL_CRAFTING},
        {SKILL_HITPOINTS, SKILL_RANGED, SKILL_MAGIC, SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE},
        {SKILL_WOODCUTTING, SKILL_FISHING, SKILL_FORAGING, SKILL_MINING, SKILL_COOKING},
        {SKILL_BREWING, SKILL_ENCHANTING},
    };
    static const vector<int> kNone;
    return category >= 0 && category < CATEGORY_COUNT ? kSkills[category] : kNone;
}

int SkillCategoryOf(int skill) {
    for (int c = 0; c < CATEGORY_COUNT; ++c)
        for (int s : CategorySkills(c))
            if (s == skill) return c;
    return -1;
}

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

void Skills::RestoreDrained() {
    for (int i = 0; i < SKILL_COUNT; ++i) current[i] = std::max(current[i], LevelForXp(xp[i]));
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

    // Before Smithing was its own skill, every bar and blade trained Crafting.
    // A save from then keeps what it earned: its Smithing starts where its
    // Crafting stands, so nobody loses the tiers they could already work.
    if (j.contains("xp") && !j["xp"].contains("Smithing") && j["xp"].contains("Crafting"))
        xp[SKILL_SMITHING] = xp[SKILL_CRAFTING];
    // The same again when the rack and the loom left Crafting for Tanning and
    // the Clothier, and the enchanting table left Magic for Enchanting: a save
    // from before starts each where the skill that used to gate it stands, so
    // no hide, robe or charm it could make is taken away.
    if (j.contains("xp") && !j["xp"].contains("Tanning") && j["xp"].contains("Crafting"))
        xp[SKILL_TANNING] = xp[SKILL_CRAFTING];
    if (j.contains("xp") && !j["xp"].contains("Clothier") && j["xp"].contains("Crafting"))
        xp[SKILL_CLOTHIER] = xp[SKILL_CRAFTING];
    if (j.contains("xp") && !j["xp"].contains("Enchanting") && j["xp"].contains("Magic"))
        xp[SKILL_ENCHANTING] = xp[SKILL_MAGIC];

    ResetCurrent();

    if (j.contains("current"))
        for (auto it = j["current"].begin(); it != j["current"].end(); ++it) {
            const int s = SkillFromName(it.key());
            if (s >= 0) current[s] = it.value().get<int>();
        }
}
