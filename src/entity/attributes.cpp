#include "attributes.h"

namespace {

string Percent(float share) {
    return std::to_string(static_cast<int>(std::lround(share * 100.0f))) + "%";
}

string Signed(int n) { return (n >= 0 ? "+" : "") + std::to_string(n); }

int ToneOf(int now, int base) { return now > base ? 1 : now < base ? -1 : 0; }

// "a, b and c".
string Listed(const vector<string>& parts, const char* last = " and ") {
    string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += i + 1 == parts.size() ? last : ", ";
        s += parts[i];
    }
    return s;
}

} // namespace

string HoursLeftText(double hours) {
    const int minutes = std::max(0, static_cast<int>(hours * 60.0));
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%dh %02dm", minutes / 60, minutes % 60);
    return buf;
}

string SecondsLeftText(float seconds) {
    const int s = std::max(0, static_cast<int>(ceilf(seconds)));
    if (s >= 60) return std::to_string((s + 59) / 60) + " min";
    return std::to_string(s) + " s";
}

vector<AttributeLine> CharacterAttributes(const Player& p) {
    // The profile the fight is resolved with, so what is written here is what
    // a blow is weighed against: a draught's boost, a poison's weakness, the
    // affinity's accuracy and the tree's defence are all in it.
    const CombatProfile c = p.Profile();
    const Skills& s = p.skills;
    vector<AttributeLine> out;

    // --- what keeps them standing ------------------------------------------------------
    // The pools, as they are now against as full as they go: a meal or a boon
    // that widens one lights it.
    out.push_back({"Hitpoints", std::to_string(p.hp) + " / " + std::to_string(p.max_hp), "",
                   p.max_hp > s.Level(SKILL_HITPOINTS) ? 1 : 0});
    out.push_back({"Mana", std::to_string(p.Mana()) + " / " + std::to_string(p.MaxMana()), "", 0});
    const int breath = static_cast<int>(std::lround(p.MaxStamina()));
    out.push_back({"Stamina", std::to_string(breath), "", ToneOf(breath, static_cast<int>(Player::MAX_STAMINA))});
    // Defence at the level the combat level makes it, with a draught on top of
    // it or a poison under it (Player::SyncDefence).
    out.push_back({"Defence", std::to_string(c.defence_level), "", ToneOf(c.defence_level, s.Level(SKILL_DEFENCE))});
    // Everything worn, and what the tree and a boon add, as one number.
    out.push_back({"Armour", Signed(c.defence_bonus), "", 0});
    // The share of a heavy blow the two turn aside between them: see HeavySoak.
    out.push_back({"Heavy soak", Percent(HeavySoak(c.defence_level, c.defence_bonus)), "", 0});
    // What the guard button does with what is in hand.
    string guard = "None";
    if (const ItemDef* shield = p.Shield()) guard = "Block " + Percent(shield->block);
    else if (p.ParryStyle())               guard = "Parry";
    else if (p.RollsOnGuard())             guard = "Roll";
    else if (p.WardStyle())                // Magic Block, and Mirror Deflect (Player::WardGuard)
        guard = string(p.talents.Effect("mirror", AttackStyle::Magic) > 0.0f ? "Mirror " : "Ward ") +
                Percent(p.WardGuard().block);
    out.push_back({"Guard", guard, "", 0});

    // --- what they fight with -----------------------------------------------------------
    // Each style's level, and what is worn adds to its accuracy -- with the
    // affinity's eight, which is aim alone (CombatProfile::melee_aim).
    const auto level = [&](const char* name, int now, int skill, int bonus) {
        out.push_back({name, std::to_string(now), Signed(bonus), ToneOf(now, s.Level(skill))});
    };
    level("Attack",   c.attack_level,   SKILL_ATTACK,   c.attack_bonus + c.melee_aim);
    level("Strength", c.strength_level, SKILL_STRENGTH, c.strength_bonus);
    level("Ranged",   c.ranged_level,   SKILL_RANGED,   c.ranged_bonus + c.ranged_aim);
    level("Magic",    c.magic_level,    SKILL_MAGIC,    c.magic_bonus + c.magic_aim);
    // A blow's chance of striking critically with what is in hand: the tree's
    // and a boon's, and the charm on the weapon -- both blades', with a pair
    // (World::HitEnemy).
    const float crit = p.talents.Effect("crit", p.Style()) + p.equipment.WeaponCharm(&ItemDef::crit_chance);
    out.push_back({"Critical", Percent(crit), "", 0});
    // As a rate: the underlying number is a multiplier on the time a swing
    // takes, where smaller is better. See the bag's own line for it.
    char rate[16];
    SDL_snprintf(rate, sizeof(rate), "%.2fx", 1.0f / p.WeaponSpeed());
    out.push_back({"Attack speed", rate, "", 0});
    // Walking, as Player::Update works it out, sprinting aside.
    float walk = (1.0f + p.talents.Global("move_speed")) * p.StatusSpeed() * (1.0f + p.equipment.MoveSpeed());
    if (p.Passive(Player::PASSIVE_MARSHSTRIDE)) walk *= Player::MARSHSTRIDE_SPEED;
    const int walk_pct = static_cast<int>(std::lround((walk - 1.0f) * 100.0f));
    out.push_back({"Walk speed", Signed(walk_pct) + "%", "", walk_pct > 0 ? 1 : walk_pct < 0 ? -1 : 0});
    return out;
}

vector<BoonLine> CharacterBoons(const Player& p, const StatusDatabase* statuses, double hours_to_dawn) {
    using Kind = BoonLine::Kind;
    // A level drained is said with what a monster left, at the very end.
    vector<BoonLine> out, drained;
    const SkillTrees* trees = p.talents.Database();
    const ItemDatabase* items = p.ItemDb();

    // --- what the bosses have left, a day each ------------------------------------------
    if (trees)
        for (const string& id : p.talents.Boons())
            if (const BoonDef* b = trees->Boon(id))
                out.push_back({Kind::Boon, b->name, b->text, HoursLeftText(p.talents.BoonHoursLeft(id))});
    // --- the totem in the ring at home, touched today: until the day turns at dawn ------
    if (const TotemDef* t = p.talents.ActiveTotem())
        out.push_back({Kind::Totem, t->name, t->text + ", from the totem at home", HoursLeftText(hours_to_dawn)});

    // --- a meal -----------------------------------------------------------------------
    if (const ItemDef* dish = p.Meal()) {
        vector<string> parts;
        const auto share = [&](float f, const char* what) { if (f > 0.0f) parts.push_back("+" + Percent(f) + " " + what); };
        share(dish->dish_max_hp, "health");
        share(dish->dish_max_mana, "mana");
        share(dish->dish_max_stamina, "breath");
        for (const auto& [skill, n] : dish->dish_levels) parts.push_back("+" + std::to_string(n) + " " + SkillName(skill));
        out.push_back({Kind::Meal, dish->name, Listed(parts), SecondsLeftText(p.MealLeft())});
    }

    // --- a draught's boost, or a level drained --------------------------------------------
    // Hitpoints is its own pool, and what a meal holds up is the meal's line.
    for (int sk = 0; sk < SKILL_COUNT; ++sk) {
        if (sk == SKILL_HITPOINTS || (p.Meal() && p.Meal()->dish_levels.count(sk))) continue;
        const int by = p.skills.Current(sk) - p.skills.Level(sk);
        if (by == 0) continue;
        // A point every BOOST_DECAY seconds, either way (Player::Update).
        const string left = SecondsLeftText(static_cast<float>(std::abs(by)) * Player::BOOST_DECAY);
        if (by > 0)
            out.push_back({Kind::Draught, string(SkillName(sk)) + " +" + std::to_string(by),
                           "a draught, wearing off a point at a time", left});
        else
            drained.push_back({Kind::Affliction, string(SkillName(sk)) + " " + std::to_string(by),
                               "drained, coming back a point at a time", left});
    }

    // --- wards ------------------------------------------------------------------------
    // A ward against two things sets both for as long as each other: one line.
    {
        vector<pair<float, vector<string>>> wards;
        for (int i = 0; i < STATUS_COUNT; ++i) {
            const Status st = static_cast<Status>(i);
            const float left = p.WardLeft(st);
            if (left <= 0.0f) continue;
            const StatusDef* d = statuses ? statuses->Get(st) : nullptr;
            const string name = d ? d->name : string(StatusId(st));
            auto it = std::find_if(wards.begin(), wards.end(),
                                   [&](const auto& w) { return std::fabs(w.first - left) < 0.5f; });
            if (it == wards.end()) wards.push_back({left, {name}});
            else it->second.push_back(name);
        }
        for (const auto& w : wards)
            out.push_back({Kind::Ward, "Ward", "cannot be left " + Listed(w.second, " or "), SecondsLeftText(w.first)});
    }

    // --- what an ability has left running ------------------------------------------------
    {
        struct Running { uint8_t bit; const char* id; const char* fallback; string what; };
        char war_cry[64], frenzy[64], stand_fast[64], rapid[64];
        SDL_snprintf(war_cry, sizeof(war_cry), "melee blows %s harder", Percent(p.WarCryDamage()).c_str());
        SDL_snprintf(frenzy, sizeof(frenzy), "melee swings %s quicker", Percent(p.FrenzySpeed()).c_str());
        SDL_snprintf(stand_fast, sizeof(stand_fast), "blows take %s of what they would, and cannot move you",
                     Percent(p.StandFastShare()).c_str());
        SDL_snprintf(rapid, sizeof(rapid), "shots %s quicker", Percent(Player::RAPID_SPEED).c_str());
        const Running running[] = {
            {Player::BUFF_WAR_CRY,    "war_cry",    "War Cry",    war_cry},
            {Player::BUFF_FRENZY,     "frenzy",     "Frenzy",     frenzy},
            {Player::BUFF_STAND_FAST, "stand_fast", "Stand Fast", stand_fast},
            {Player::BUFF_TAKE_AIM,   "take_aim",   "Take Aim",   "the next shot strikes critically, half as hard again"},
            {Player::BUFF_RAPID_FIRE, "rapid_fire", "Rapid Fire", rapid},
            {Player::BUFF_OVERLOAD,   "overload",   "Overload",   "the next spell costs nothing and lands twice as hard"},
            {Player::BUFF_INVOKE,     "invoke",     "Invoke",     "half your mana coming back"},
        };
        for (const Running& r : running) {
            const float left = p.BuffLeft(r.bit);
            if (left <= 0.0f) continue;
            const TalentNode* n = trees ? trees->Find(r.id) : nullptr;
            out.push_back({Kind::Ability, n ? n->name : string(r.fallback), r.what, SecondsLeftText(left)});
        }
        if (p.ManaShield()) {
            const TalentNode* n = trees ? trees->Find("mana_shield") : nullptr;
            out.push_back({Kind::Ability, n ? n->name : string("Mana Shield"), "half of every blow is paid in mana",
                           SecondsLeftText(p.ManaShieldLeft())});
        }
    }

    // --- what is worn does ---------------------------------------------------------------
    // Every line a piece says of itself that no number can: "Marshstride: you
    // walk ...", and a charm's -- which is written into its twin's lines, after
    // whatever the piece said already ("Precision IV: +18% ..."). Said where it
    // is worn, and a charm in a charm's colour.
    vector<BoonLine> charms, passives;
    for (int slot = 0; slot < SLOT_COUNT; ++slot) {
        const string& id = p.equipment.InSlot(slot);
        const ItemDef* d = (id.empty() || !items) ? nullptr : items->Get(id);
        if (!d || d->passive_text.empty()) continue;
        const EnchantDef* e = d->enchant.empty() ? nullptr : items->Enchantment(d->enchant);
        const string charm = e ? e->NameAt(d->enchant_tier) + ": " : string();
        const string where = slot == SLOT_SHIELD ? "off hand" : EquipSlotName(slot);
        size_t from = 0;
        while (from < d->passive_text.size()) {
            const size_t end = d->passive_text.find('\n', from);
            const string said = d->passive_text.substr(from, end == string::npos ? string::npos : end - from);
            from = end == string::npos ? d->passive_text.size() : end + 1;
            if (said.empty()) continue;
            const size_t colon = said.find(": ");
            const bool named = colon != string::npos && colon < 32;
            BoonLine b{Kind::Passive, named ? said.substr(0, colon) : d->name, named ? said.substr(colon + 2) : said, where};
            if (!charm.empty() && said.rfind(charm, 0) == 0) { b.kind = Kind::Charm; charms.push_back(b); }
            else passives.push_back(b);
        }
    }
    out.insert(out.end(), charms.begin(), charms.end());
    out.insert(out.end(), passives.begin(), passives.end());
    // And a share of every blow back as health, from a ring or a charm.
    if (p.equipment.Leech() > 0.0f)
        out.push_back({Kind::Passive, "Leech", Percent(p.equipment.Leech()) + " of what you deal comes back as health", "worn"});

    // --- what a monster has left on them, last and in its own colour -----------------------
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const float left = p.statuses.left[i];
        if (left <= 0.0f) continue;
        const Status st = static_cast<Status>(i);
        const StatusDef* d = statuses ? statuses->Get(st) : nullptr;
        vector<string> does;
        if (st == Status::Charm)     does.push_back("walking to whoever cast it");
        if (st == Status::Confused)  does.push_back("which way is which is backwards");
        if (d && d->holds)           does.push_back("held fast");
        if (d && (d->dot_share > 0.0f || d->dot_min > 0)) does.push_back("hurting");
        if (d && d->speed < 1.0f && !d->holds) does.push_back("slowed");
        if (d && d->attack < 1.0f)   does.push_back("Attack down");
        if (d && d->defence < 1.0f)  does.push_back("Defence down");
        out.push_back({Kind::Affliction, d ? d->name : string(StatusId(st)), Listed(does), SecondsLeftText(left)});
    }
    out.insert(out.end(), drained.begin(), drained.end());
    return out;
}
