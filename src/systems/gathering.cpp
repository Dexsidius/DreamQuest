#include "gathering.h"

namespace Gathering {

float ForageExtraChance(int level, int plant_level) {
    return std::clamp((level - plant_level) * 0.01f, 0.0f, 0.5f);
}

const char* ToolFor(const string& skill) {
    if (skill == "Woodcutting") return "axe";
    if (skill == "Mining")      return "pickaxe";
    if (skill == "Fishing")     return "rod";
    return "";
}

const char* ClipFor(const string& skill) {
    if (skill == "Woodcutting") return "chop";
    if (skill == "Mining")      return "mine";
    if (skill == "Fishing")     return "fish";
    return "idle";
}

const char* ToolNoun(const string& tool) {
    if (tool == "axe")     return "an axe";
    if (tool == "pickaxe") return "a pickaxe";
    if (tool == "rod")     return "a fishing rod";
    return "a tool";
}

float Speed(int level, float tool_speed) {
    return (1.0f + 0.02f * std::max(1, level)) * std::max(0.1f, tool_speed);
}

float WorkTime(float base_seconds, int level, float tool_speed) {
    // No quicker than this, however good the axe. It was 0.6, which a platinum
    // axe reached on the day it could first be held -- so the three tiers
    // above it, demonite, dracon and enchanted, cut no faster than it did.
    return std::max(0.42f, base_seconds / Speed(level, tool_speed));
}

const ItemDef* BestTool(const Inventory& bag, const Equipment& worn, const ItemDatabase& db,
                        const Skills& skills, const string& tool, const ItemDef** unusable) {
    const ItemDef* best = nullptr;
    const ItemDef* best_locked = nullptr;
    const auto consider = [&](const string& id) {
        const ItemDef* d = id.empty() ? nullptr : db.Get(id);
        if (!d || d->tool != tool) return;
        bool allowed = true;
        for (const auto& req : d->requirements)
            if (skills.Level(req.first) < req.second) allowed = false;
        const ItemDef*& slot = allowed ? best : best_locked;
        if (!slot || d->tool_speed > slot->tool_speed) slot = d;
    };
    for (int i = 0; i < bag.SlotCount(); ++i) consider(bag.Slot(i).id);
    for (int s = 0; s < SLOT_COUNT; ++s) consider(worn.InSlot(s));
    if (unusable) *unusable = best ? nullptr : best_locked;
    return best;
}

const vector<Milestone>& FishingMilestones() {
    static const vector<Milestone> table = {
        {20, 0.10f, 0.00f},
        {40, 0.20f, 0.00f},
        {60, 0.30f, 0.00f},
        {80, 0.30f, 0.10f},
        {99, 0.30f, 0.20f},
    };
    return table;
}

Milestone ExtraCatch(int level) {
    Milestone m{0, 0.0f, 0.0f};
    for (const Milestone& ms : FishingMilestones())
        if (level >= ms.level) m = ms;
    return m;
}

int CatchCount(int level, float roll) {
    const Milestone m = ExtraCatch(level);
    if (roll < m.three) return 3;
    if (roll < m.three + m.two) return 2;
    return 1;
}

string PickFish(const vector<string>& fish, int level, const ItemDatabase& db, std::mt19937& rng) {
    // Best first.
    vector<const ItemDef*> open;
    for (const string& id : fish)
        if (const ItemDef* d = db.Get(id))
            if (d->fish_level <= level) open.push_back(d);
    if (open.empty()) return "";
    std::sort(open.begin(), open.end(),
              [](const ItemDef* a, const ItemDef* b) { return a->fish_level > b->fish_level; });

    // Each fish in turn, best first, bites with a chance that grows the
    // further the level is past it; the least of them always bites.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t i = 0; i + 1 < open.size(); ++i) {
        const float chance = std::min(0.75f, 0.30f + (level - open[i]->fish_level) * 0.01f);
        if (unit(rng) < chance) return open[i]->id;
    }
    return open.back()->id;
}

// --- the second dip, and the fight ------------------------------------------------------

Fight FightFor(int fish_level, int fishing_level) {
    // From a minnow (1) to the best of the deep water (90 and over).
    const float d = std::clamp((fish_level - 1) / 89.0f, 0.0f, 1.0f);
    const auto lerp = [&](float easy, float hard) { return easy + (hard - easy) * d; };
    Fight f;
    // A fisher far past the fish: a point of band for every ten levels, to three.
    const float mastery = std::min(0.03f, std::max(0, fishing_level - fish_level) * 0.001f);
    // Tuned against a simulated player who sees the gauge a little late
    // (tools/selftest's TestFishing holds it there): an attentive one lands a
    // minnow every time and the best fish four times in five, an ordinary one
    // four in five at 60 and one in five at 90, and one who does nothing, or
    // holds the button down, never lands anything.
    f.band  = lerp(0.24f, 0.14f) + mastery;
    f.speed = lerp(0.14f, 0.38f);
    f.darts = lerp(0.25f, 0.75f);
    f.land  = lerp(2.0f, 4.5f);
    f.slip  = lerp(0.30f, 0.60f);
    f.snap  = lerp(2.6f, 1.7f);
    f.bite  = lerp(1.0f, 0.7f);
    return f;
}

void Angler::Cast(float wait, const Fight& f, std::mt19937& rng) {
    (void)rng;
    fight = f;
    phase = Phase::Waiting;
    t = 0.0f;
    length = std::max(0.5f, wait);
    line = 0.5f; line_v = 0.0f;
    band = band_goal = 0.5f; band_v = 0.0f;
    progress = 0.0f;
    strain = 0.0f;
}

Angler::Outcome Angler::Strike() {
    switch (phase) {
        case Phase::Idle:    return Outcome::None;
        case Phase::Reeling: return Outcome::None;
        case Phase::Waiting: phase = Phase::Idle; return Outcome::ReeledIn;
        case Phase::Nibble:
        case Phase::Lull:    phase = Phase::Idle; return Outcome::TooSoon;
        case Phase::Bite:
            phase = Phase::Reeling;
            t = 0.0f;
            line = 0.5f; line_v = 0.0f;
            band = band_goal = 0.5f; band_v = 0.0f;
            progress = HEAD_START;
            strain = 0.0f;
            return Outcome::Hooked;
    }
    return Outcome::None;
}

void Angler::NewGoal(std::mt19937& rng) {
    // Somewhere else on the gauge -- the harder the fish, the further it
    // tends to bolt -- and never so near an end that the band runs off it.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float lo = fight.band, hi = 1.0f - fight.band;
    float goal = lo + (hi - lo) * unit(rng);
    if (std::fabs(goal - band) < 0.15f) goal = band + (goal < band ? -0.15f : 0.15f);
    band_goal = std::clamp(goal, lo, hi);
}

Angler::Outcome Angler::Update(float dt, bool reel, std::mt19937& rng) {
    if (phase == Phase::Idle) return Outcome::None;
    t += dt;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // --- waiting for it: the float, the nibble, the lull, the bite -----------------------
    if (phase != Phase::Reeling) {
        if (t < length) return Outcome::None;
        t = 0.0f;
        switch (phase) {
            case Phase::Waiting: phase = Phase::Nibble; length = NIBBLE_TIME; break;
            case Phase::Nibble:  phase = Phase::Lull; length = LULL_MIN + (LULL_MAX - LULL_MIN) * unit(rng); break;
            case Phase::Lull:    phase = Phase::Bite; length = fight.bite; break;
            case Phase::Bite:    phase = Phase::Idle; return Outcome::Missed;
            default: break;
        }
        return Outcome::None;
    }

    // --- the fight ------------------------------------------------------------------------
    // The fish: towards where it is making for, picking up speed and losing it
    // again the way a pulled line does, and every so often bolting elsewhere.
    if (std::fabs(band_goal - band) < 0.02f || unit(rng) < fight.darts * dt) NewGoal(rng);
    const float want = (band_goal > band ? 1.0f : -1.0f) * fight.speed;
    band_v += (want - band_v) * std::min(1.0f, dt * 5.0f);
    band = std::clamp(band + band_v * dt, fight.band, 1.0f - fight.band);

    // The line: wound up while the button is held, dropping back while not,
    // and stopped dead -- not bounced -- at either end.
    line_v = std::clamp(line_v + (reel ? REEL_ACCEL : -FALL_ACCEL) * dt, -LINE_TOP_SPEED, LINE_TOP_SPEED);
    line += line_v * dt;
    if (line < 0.0f) { line = 0.0f; line_v = std::max(0.0f, line_v); }
    if (line > 1.0f) { line = 1.0f; line_v = std::min(0.0f, line_v); }

    // In the green it comes in, and the line eases; out of it the fish wins
    // ground back and the strain builds until the line goes.
    if (InBand()) {
        progress += dt / fight.land;
        strain = std::max(0.0f, strain - dt * 2.0f);
    } else {
        progress = std::max(0.0f, progress - fight.slip * dt / fight.land);
        strain += dt;
        if (strain >= fight.snap) { phase = Phase::Idle; return Outcome::Snapped; }
    }
    if (progress >= 1.0f) { progress = 1.0f; phase = Phase::Idle; return Outcome::Landed; }
    return Outcome::None;
}

float Angler::Dip() const {
    constexpr float PI = 3.1415926f;
    switch (phase) {
        case Phase::Nibble:  return 0.35f * std::sin(PI * std::clamp(t / NIBBLE_TIME, 0.0f, 1.0f));
        case Phase::Bite:    return std::min(1.0f, t / 0.08f);
        // On the line, it thrashes: mostly under, now and then up.
        case Phase::Reeling: return 0.72f + 0.28f * std::sin(t * 17.0f);
        default:             return 0.0f;
    }
}

bool Depletes(float chance, float roll) {
    return chance > 0.0f && roll < chance;
}

int SpotLevel(const vector<string>& fish, const ItemDatabase& db) {
    int lowest = 0;
    for (const string& id : fish)
        if (const ItemDef* d = db.Get(id))
            lowest = (lowest == 0) ? d->fish_level : std::min(lowest, d->fish_level);
    return std::max(1, lowest);
}

}
