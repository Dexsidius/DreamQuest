#include "player.h"
#include "../world/map.h"
#include "../world/world.h"
#include "../systems/audio.h"

static constexpr float COMBO_WINDOW   = 0.42f;
static constexpr float RUN_THRESHOLD  = 0.62f;
static constexpr float KNOCK_DECAY    = 9.0f;
static constexpr float DEATH_DURATION = 2.4f;

Player::Player() : inventory(nullptr), equipment(nullptr) {
    foot_box = {-8.0f, -10.0f, 16.0f, 10.0f};
    body_box = {-13.0f, -42.0f, 26.0f, 42.0f};
}

void Player::Init(const GameContext& ctx, const string& id) {
    sprite_id = id;
    item_db = ctx.items;
    if (ctx.sprites) {
        // A save may name a character this build no longer has -- the two
        // pack-art characters were dropped when the game went to art it can
        // distribute. Falling back keeps those saves playable instead of
        // loading them as an invisible player.
        const SpriteDef* def = ctx.sprites->Get(sprite_id);
        if (!def) {
            SDL_Log("Player: no sprite '%s'; falling back to %s",
                    sprite_id.c_str(), kDefaultCharacter);
            sprite_id = kDefaultCharacter;
            def = ctx.sprites->Get(sprite_id);
        }
        sprite.SetDef(def);
    }
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
    talents.SetDatabase(ctx.trees);
    talents.SetPath(AffinityFor(id));
    bags.clear();
    SizeBag();
    sprite.Play("idle");
    SyncHitpoints();
    hp = max_hp;
    SyncMana();
    mana = max_mana;
}

void Player::SyncMana() {
    max_mana = static_cast<int>(std::lround(SpellBook::MaxMana(skills.Level(SKILL_MAGIC)) *
                                            (1.0f + talents.Global("max_mana") +
                                             (meal ? meal->dish_max_mana : 0.0f))));
    mana = std::clamp(mana, 0, max_mana);
}

bool Player::SpendMana(int cost) {
    if (cost <= 0) return true;
    if (mana < cost) return false;
    mana -= cost;
    return true;
}

void Player::CycleElement(int delta) {
    // An element's own staff steps through its four spells, and then the
    // ancient magic if there is any on the fifth slot.
    if (StaffElement() != Element::None) {
        const int count = 4 + (arcane_spell.empty() ? 0 : 1);
        int index = selected_element == Element::Arcane ? 4 : spell_slot;
        index = ((index + delta) % count + count) % count;
        if (index == 4) selected_element = Element::Arcane;
        else SelectSlot(index);
        return;
    }
    // The four, then the lightning if any of it is reached, then the ancient
    // magic if any of it is known. Stepping is over what is actually there --
    // a pad has one button for this and it must not land on an empty school.
    vector<Element> round;
    for (int i = FIRST_ELEMENT; i <= LAST_ELEMENT; ++i) {
        const Element e = static_cast<Element>(i);
        if (e == Element::Arcane && arcane_spell.empty()) continue;
        if (e == Element::Electric && electric_spell.empty()) continue;
        round.push_back(e);
    }
    if (round.empty()) return;
    auto it = std::find(round.begin(), round.end(), selected_element);
    const int at = it == round.end() ? 0 : static_cast<int>(it - round.begin());
    const int count = static_cast<int>(round.size());
    selected_element = round[((at + delta) % count + count) % count];
}

void Player::CycleElement(int delta, const vector<string>& known) {
    if (!known.empty() && std::find(known.begin(), known.end(), arcane_spell) == known.end())
        arcane_spell = known.front();
    CycleElement(delta);
}

void Player::SelectElectric(const vector<string>& known) {
    if (known.empty()) return;
    if (selected_element != Element::Electric || electric_spell.empty()) {
        // Keep the one chosen before if the level still reaches it; the first
        // otherwise, which is Zap, which is the one that fills the bar.
        if (std::find(known.begin(), known.end(), electric_spell) == known.end()) electric_spell = known.front();
        selected_element = Element::Electric;
        return;
    }
    auto it = std::find(known.begin(), known.end(), electric_spell);
    const size_t next = (it == known.end()) ? 0 : (static_cast<size_t>(it - known.begin()) + 1) % known.size();
    electric_spell = known[next];
}

static int HeldIndex(Element e) {
    const int i = static_cast<int>(e) - static_cast<int>(Element::Fire);
    return i >= 0 && i < 4 ? i : -1;
}

const string& Player::HeldSpell(Element e) const {
    static const string none;
    const int i = HeldIndex(e);
    return i < 0 ? none : held_spell[i];
}

void Player::HoldSpell(Element e, const string& id) {
    const int i = HeldIndex(e);
    if (i >= 0) held_spell[i] = id;
}

// The spells held to, as a save and a friend's host are told them: only the
// elements that are held to anything.
static json HeldToJson(const string held[4]) {
    json out = json::object();
    for (int i = 0; i < 4; ++i)
        if (!held[i].empty()) out[ElementName(static_cast<Element>(i + static_cast<int>(Element::Fire)))] = held[i];
    return out;
}

static void HeldFromJson(const json& j, string held[4]) {
    for (int i = 0; i < 4; ++i) {
        held[i].clear();
        const char* name = ElementName(static_cast<Element>(i + static_cast<int>(Element::Fire)));
        if (j.is_object() && j.contains(name) && j[name].is_string()) held[i] = j[name].get<string>();
    }
}

vector<const SpellDef*> Player::SpellChoices(const SpellBook& book, const vector<string>& arcane,
                                             const vector<string>& electric) const {
    const int magic = skills.Level(SKILL_MAGIC);
    const Element e = SelectedElement();
    vector<const SpellDef*> out;
    const auto known = [&](const vector<string>& ids) {
        for (const string& id : ids)
            if (const SpellDef* s = book.Get(id)) out.push_back(s);
    };
    if (e == Element::Arcane) {
        known(arcane);
    } else if (e == Element::Electric) {
        known(electric);
    } else if (StaffElement() != Element::None) {
        // An element's own staff: its four spells are its four slots.
        for (int slot = 0; slot < 4; ++slot)
            if (const SpellDef* s = slot == 0 ? book.Chosen(StaffElement(), magic, HeldSpell(StaffElement()))
                                              : book.ForSlot(StaffElement(), slot + 1, magic))
                out.push_back(s);
    } else {
        const vector<int>& slots = SpellSlots(e);
        const vector<const SpellDef*> offered = slots.empty() ? book.Of(e) : book.ForWeapon(e, slots, magic);
        for (const SpellDef* s : offered) if (s->level <= magic) out.push_back(s);
    }
    return out;
}

bool Player::StepSpell(int step, const SpellBook& book, const vector<string>& arcane,
                       const vector<string>& electric) {
    const vector<const SpellDef*> list = SpellChoices(book, arcane, electric);
    const int n = static_cast<int>(list.size());
    if (n < 2 || step == 0) return false;
    const int magic = skills.Level(SKILL_MAGIC);
    const Element e = SelectedElement();
    const auto along = [&](int at) { return ((at + step) % n + n) % n; };

    if (e == Element::Arcane || e == Element::Electric) {
        const string& now = e == Element::Arcane ? arcane_spell : electric_spell;
        int at = 0;
        for (int i = 0; i < n; ++i) if (list[i]->id == now) at = i;
        (e == Element::Arcane ? arcane_spell : electric_spell) = list[along(at)]->id;
        return true;
    }
    if (StaffElement() != Element::None) {
        // The slots with a spell on them, and from the one chosen.
        vector<int> open;
        for (int slot = 0; slot < 4; ++slot)
            if (slot == 0 ? book.Chosen(StaffElement(), magic, HeldSpell(StaffElement())) != nullptr
                          : book.ForSlot(StaffElement(), slot + 1, magic) != nullptr)
                open.push_back(slot);
        const int m = static_cast<int>(open.size());
        int at = 0;
        for (int i = 0; i < m; ++i) if (open[i] == spell_slot) at = i;
        SelectSlot(open[((at + step) % m + m) % m]);
        return true;
    }
    const SpellDef* now = SpellOf(e, book);
    int at = 0;
    for (int i = 0; i < n; ++i) if (now && list[i]->id == now->id) at = i;
    const SpellDef* next = list[along(at)];
    // The weapon's first is where an element starts, and held to nothing it
    // goes on growing with the Magic level -- held to by name it would stay the
    // tier it is. So landing on it lets go.
    const vector<int>& slots = SpellSlots(e);
    const SpellDef* first = slots.empty() ? book.Chosen(e, magic, string()) : book.ChosenFor(e, slots, magic, string());
    HoldSpell(e, first && next->id == first->id ? string() : next->id);
    return true;
}

void Player::SelectArcane(const vector<string>& known) {
    if (known.empty()) return;
    if (selected_element != Element::Arcane || arcane_spell.empty()) {
        // Keep the one chosen before if it is still known; otherwise the first.
        if (std::find(known.begin(), known.end(), arcane_spell) == known.end()) arcane_spell = known.front();
        selected_element = Element::Arcane;
        return;
    }
    auto it = std::find(known.begin(), known.end(), arcane_spell);
    const size_t next = (it == known.end()) ? 0 : (static_cast<size_t>(it - known.begin()) + 1) % known.size();
    arcane_spell = known[next];
}

string Player::ComboLabel(ComboMove move) const {
    const ItemDef::ComboTwist* t = Twist(move);
    return t && !t->name.empty() ? t->name : string(ComboNameFor(move, Style()));
}

const ItemDef::ComboTwist* Player::Twist(ComboMove move) const {
    const ItemDef* w = equipment.Weapon();
    const int i = move == ComboMove::Crush ? 0 : move == ComboMove::Cleave ? 1 : move == ComboMove::Backhand ? 2
                : move == ComboMove::CrossCut ? 3 : -1;
    return (w && i >= 0) ? &w->combos[i] : nullptr;
}

void Player::StartReload() {
    const ItemDef* w = equipment.Weapon();
    if (!w || w->reload <= 0.0f) return;
    // Quick hands span it quicker: the same talents and the same Rapid Fire
    // that hurry a bow.
    reload_time = reload_left = w->reload * std::clamp(WeaponSpeed() / std::max(0.1f, w->attack_speed), 0.4f, 1.5f);
}

Element Player::StaffElement() const {
    const ItemDef* w = equipment.Weapon();
    return (w && w->kind == WeaponKind::Staff) ? w->element : Element::None;
}

AttackStyle Player::Style() const {
    switch (equipment.Kind()) {
        case WeaponKind::Bow:   return AttackStyle::Ranged;
        case WeaponKind::Staff: return AttackStyle::Magic;
        default:                return AttackStyle::Melee;
    }
}

LayerStyle Player::BuildLayerStyle(const ItemDatabase* db) const {
    LayerStyle s;
    const SDL_Color armour = equipment.ArmourTint();
    s.body = armour;
    // The head only takes the tint when something is actually worn on it, so a
    // bare-headed character keeps their own colouring.
    s.head = equipment.InSlot(SLOT_HEAD).empty() ? SDL_Color{255, 255, 255, 255} : armour;
    s.weapon = equipment.WeaponTint();
    s.show_weapon = !equipment.InSlot(SLOT_WEAPON).empty();
    s.attachments = equipment.Attachments();

    // Worn plate, a layer at a time: each piece turns on the sheet for its own
    // slot and paints it its own metal, so a bronze cuirass over iron greaves
    // is drawn as a bronze cuirass over iron greaves rather than as one
    // averaged colour over the whole character.
    if (db) {
        struct Wear { int slot; ArmourLayer layer; const char* name; };
        static const Wear kWear[] = {
            {SLOT_LEGS,   ARMOUR_LEGS,   "legs"},
            {SLOT_BODY,   ARMOUR_BODY,   "body"},
            {SLOT_HANDS,  ARMOUR_HANDS,  "hands"},
            {SLOT_HEAD,   ARMOUR_HEAD,   "head"},
            {SLOT_SHIELD, ARMOUR_SHIELD, "shield"},
        };
        for (const Wear& w : kWear) {
            const ItemDef* d = db->Get(equipment.InSlot(w.slot));
            if (!d || d->armour_layer != w.name) continue;
            s.armour[w.layer].show = true;
            s.armour[w.layer].tint = d->tint;
            s.armour[w.layer].cut = d->armour_cut;
        }
    }

    // The rig's own weapon layers draw a sword. When the equipped weapon
    // carries its own art, hide them and let that stand in instead, or the
    // character ends up holding a staff and a sword at once.
    if (db) {
        const ItemDef* w = db->Get(equipment.InSlot(SLOT_WEAPON));
        if (w && w->worn && !w->worn_sprite.empty()) s.show_weapon = false;
        if (w) s.weapon_model = w->model;
        // A second dagger, in the hand a shield would be on.
        if (const ItemDef* off = equipment.Offhand()) s.offhand_model = off->model;
    }
    // Picking herbs is done bare-handed, so the weapon is put away.
    if (gather_clip == "gather") s.show_weapon = false;
    // At work the hands hold the tool, whatever is normally in them.
    if (!gather_model.empty()) {
        s.offhand_model.clear();
        s.weapon_model = gather_model;
        s.show_weapon = true;
        s.weapon = {255, 255, 255, 255};
    }
    return s;
}

void Player::SyncHitpoints() {
    // A dinner is worth a share of what the pool already is, so it is worth
    // eating at fifty as well as at five.
    const float fed = meal ? meal->dish_max_hp : 0.0f;
    // And a boss's boon the same way: a share of the pool, so it is worth as
    // much to whoever has it at eighty as it was at twenty.
    const float blessed = talents.Global("max_health");
    max_hp = std::max(1, static_cast<int>(std::lround(skills.Level(SKILL_HITPOINTS) * (1.0f + fed + blessed))));
    hp = std::clamp(skills.Current(SKILL_HITPOINTS), 0, max_hp);
}

AttackStyle Player::AffinityFor(const string& character_id) {
    if (character_id == "player_warden")   return AttackStyle::Ranged;
    if (character_id == "player_wayfarer") return AttackStyle::Magic;
    return AttackStyle::Melee;
}

vector<string> Player::StartingKit(const string& character_id) {
    // Each in the wooden tier's armour of their own kind. The warden and the
    // wayfarer both set out in the hero's Barkwood Cuirass, which is plate: it
    // does nothing for a bow or a staff, and the first thing either of them
    // learned about armour was that theirs was the wrong sort. The whole set of
    // the right sort -- head, body and legs -- so the card at the start shows
    // the character they are going to be, and the set's small push to their
    // own style is there from the first fight.
    switch (AffinityFor(character_id)) {
        // Rawhide: coif, jerkin and chaps. A bow takes both hands, so the
        // warden's other piece is the boots a ranger would wear to keep the
        // distance rather than a shield they could not raise.
        case AttackStyle::Ranged:
            return {"oak_shortbow", "wood_hide_head", "wood_hide_body", "wood_hide_legs", "hide_boots"};
        // Homespun: hat, robe and skirt. A staff is held in one hand, so the
        // shield stays -- and cloth turns less than wood does, so it matters more.
        case AttackStyle::Magic:
            return {"wood_staff", "wood_robe_head", "wood_robe_body", "wood_robe_legs", "wooden_shield"};
        default:
            return {"wood_sword", "wood_body", "wooden_shield"};
    }
}

LayerStyle Player::KitStyle(const string& character_id, const ItemDatabase* db) {
    Player dressed;
    dressed.sprite_id = character_id;
    dressed.item_db = db;
    dressed.equipment.SetDatabase(db);
    dressed.inventory.SetDatabase(db);
    // The whole kit, because the card is a picture of the character you are
    // about to play and the line under it already names the weapon: the warden
    // with the bow, the wayfarer with the staff, each in the barkwood they set
    // out in. Drawn from the layers rather than the rig's composed sheet, which
    // has the rig's own sword baked into it whatever the character fights with.
    for (const string& id : StartingKit(character_id))
        if (const ItemDef* d = db ? db->Get(id) : nullptr)
            if (d->slot != SLOT_NONE) dressed.equipment.Equip(d->slot, id);
    return dressed.BuildLayerStyle(db);
}

const char* Player::AffinityName(AttackStyle style) {
    switch (style) {
        case AttackStyle::Ranged: return "the bow";
        case AttackStyle::Magic:  return "the staff";
        default:                  return "the blade";
    }
}

CombatProfile Player::Profile() const {
    CombatProfile p;
    p.attack_level   = skills.Current(SKILL_ATTACK);
    p.strength_level = skills.Current(SKILL_STRENGTH);
    p.defence_level  = skills.Current(SKILL_DEFENCE);
    p.ranged_level   = skills.Current(SKILL_RANGED);
    p.magic_level    = skills.Current(SKILL_MAGIC);
    p.attack_bonus   = equipment.AttackBonus();
    p.strength_bonus = equipment.StrengthBonus();
    p.defence_bonus  = equipment.DefenceBonus() + static_cast<int>(talents.Global("defence"));
    p.ranged_bonus   = equipment.RangedBonus();
    p.magic_bonus    = equipment.MagicBonus();
    // The affinity: a little more accuracy with the character's own style.
    switch (Affinity()) {
        case AttackStyle::Ranged: p.ranged_bonus += AFFINITY_BONUS; break;
        case AttackStyle::Magic:  p.magic_bonus  += AFFINITY_BONUS; break;
        default:                  p.attack_bonus += AFFINITY_BONUS; break;
    }
    // What is on them: a concussed or poisoned player guards worse, a
    // concussed or arcing one swings worse, as a monster would.
    if (status_db && statuses.Any()) {
        float attack = 1.0f, defence = 1.0f;
        for (int i = 0; i < STATUS_COUNT; ++i) {
            const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
            if (!d) continue;
            attack *= d->attack;
            defence *= d->defence;
        }
        const auto scale = [](int level, float k) { return std::max(1, static_cast<int>(std::lround(level * k))); };
        p.attack_level  = scale(p.attack_level, attack);
        p.ranged_level  = scale(p.ranged_level, attack);
        p.magic_level   = scale(p.magic_level, attack);
        p.defence_level = scale(p.defence_level, defence);
    }
    return p;
}

bool Player::Held() const {
    if (!statuses.Any()) return false;
    if (!status_db) return statuses.Has(Status::Frozen);
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
        if (d && d->holds) return true;
    }
    return false;
}

Status Player::Afflict(Status kind, int blow, const StatusDatabase& db, float from_x, float from_y) {
    if (dead || hp <= 0 || kind == Status::COUNT) return Status::COUNT;
    status_db = &db;
    const StatusDef* d = db.Get(kind);
    if (!d) return Status::COUNT;
    const auto lasts = [](const StatusDef& of) { return of.seconds * of.player_share; };
    // A chill on someone soaked is a frost, as it is on a monster.
    if (d->becomes != Status::COUNT && d->if_has != Status::COUNT && statuses.Has(d->if_has)) {
        const StatusDef* other = db.Get(d->becomes);
        if (other && lasts(*other) > 0.0f) { kind = d->becomes; d = other; }
    }
    for (Status stops : d->blocked_by) if (statuses.Has(stops)) return Status::COUNT;
    const float seconds = lasts(*d);
    if (seconds <= 0.0f) return Status::COUNT;
    for (Status over : d->ends) statuses.End(over);

    const int i = static_cast<int>(kind);
    if (d->dot_share > 0.0f) {
        const float fresh = std::max(static_cast<float>(d->dot_min), static_cast<float>(blow) * d->dot_share);
        const float owed = statuses.left[i] * statuses.rate[i];
        const float total = d->stacks ? owed + fresh : std::max(owed, fresh);
        statuses.rate[i] = total / seconds;
    }
    statuses.left[i] = std::max(statuses.left[i], seconds);
    if (kind == Status::Charm) { charm_x = from_x; charm_y = from_y; }
    // Held fast or beguiled, whatever they were winding up comes to nothing.
    if (d->holds || kind == Status::Charm) {
        charging = strong_armed = false;
        charge_held = 0.0f;
        sprinting = false;
    }
    return kind;
}

bool Player::ShakeOff(const StatusDatabase& db, Status except) {
    bool any = false;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const Status s = static_cast<Status>(i);
        if (s == except || statuses.left[i] <= 0.0f) continue;
        const StatusDef* d = db.Get(s);
        if (d && d->breaks_on_hit) { statuses.End(s); any = true; }
    }
    return any;
}

float Player::StatusInvites(Status s) const {
    float mult = 1.0f;
    if (!status_db || s == Status::COUNT) return mult;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
        if (d && std::find(d->invites.begin(), d->invites.end(), s) != d->invites.end()) mult *= d->invite_mult;
    }
    return mult;
}

float Player::StatusSpeed() const {
    float mult = 1.0f;
    if (!status_db || !statuses.Any()) return mult;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        const StatusDef* d = statuses.left[i] > 0.0f ? status_db->Get(static_cast<Status>(i)) : nullptr;
        if (d) mult *= d->speed;
    }
    return mult;
}

void Player::ShowStatuses(uint16_t bits, float cx, float cy) {
    // Kept on a moment past what was said, so they last until the next word
    // about them: a snapshot comes several times a second.
    for (int i = 0; i < STATUS_COUNT; ++i) {
        if ((bits >> i) & 1u) statuses.left[i] = std::max(statuses.left[i], 0.4f);
        else                  statuses.End(static_cast<Status>(i));
    }
    charm_x = cx;
    charm_y = cy;
}

void Player::TickStatuses(float dt, World& world) {
    if (!statuses.Any()) return;
    // A friend's machine is told what is on them and how hurt they are; only
    // the host counts what a poison owes.
    const bool mine = !world.visiting;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        if (statuses.left[i] <= 0.0f) continue;
        const Status kind = static_cast<Status>(i);
        const StatusDef* d = status_db ? status_db->Get(kind) : nullptr;
        const float step = std::min(dt, statuses.left[i]);
        statuses.left[i] -= step;
        if (mine && hp > 0 && statuses.rate[i] > 0.0f) {
            statuses.bank[i] += statuses.rate[i] * step;
            const int whole = static_cast<int>(statuses.bank[i]);
            if (whole > 0) {
                statuses.bank[i] -= static_cast<float>(whole);
                // Taken without the flash, the sound or the lost sprint of a
                // blow: a burn ticks several times a second, and each tick
                // treated as a blow would drown out the blows themselves.
                const float flash = hurt_flash;
                Damage(whole);
                hurt_flash = flash;
                heard_hp = hp;
                skills.SetCurrent(SKILL_HITPOINTS, hp);
                world.AddText(std::to_string(whole), x + 10.0f, y - 38.0f,
                              d ? d->color : SDL_Color{200, 60, 70, 255}, 0.7f);
            }
        }
        if (statuses.left[i] <= 0.0f) {
            statuses.End(kind);
            // A frost thaws into a chill.
            if (mine && d && d->then != Status::COUNT && status_db && hp > 0)
                Afflict(d->then, 0, *status_db, charm_x, charm_y);
        }
    }
}

void Player::GrantXp(int skill, int amount) {
    if (skill < 0 || skill >= SKILL_COUNT || amount <= 0) return;
    LevelUp up;
    if (skills.AddXp(skill, amount, up)) {
        pending_levels.push_back(up);
        if (up.skill == SKILL_HITPOINTS) SyncHitpoints();
    }
    pending_xp.emplace_back(skill, amount);
}

void Player::BankXp(int skill, float amount) {
    if (skill < 0 || skill >= SKILL_COUNT || amount <= 0.0f) return;
    xp_fraction[skill] += amount;
    const int whole = static_cast<int>(xp_fraction[skill]);
    if (whole > 0) {
        xp_fraction[skill] -= whole;
        GrantXp(skill, whole);
    }
}

bool Player::CanRush() const {
    return rush_cooldown <= 0.0f && Style() == AttackStyle::Melee &&
           talents.Effect("rushing_strike", AttackStyle::Melee) > 0.0f;
}

float Player::RushLift() const {
    if (!rushing) return 0.0f;
    const float flight = attack.profile.windup + attack.profile.active;
    const float t = std::clamp(attack.timer / std::max(0.001f, flight), 0.0f, 1.0f);
    return sinf(t * 3.14159265f) * RUSH_HEIGHT;
}

bool Player::StartRush(const World& world) {
    if (!CanRush()) return false;

    // At whatever is being fought, when it is close enough to leap at;
    // otherwise on along the way the character was already running.
    float dx = move_axis.x, dy = move_axis.y;
    if (const Enemy* t = CurrentTarget(world)) {
        const float tx = t->x - x, ty = t->y - y;
        if (Length(tx, ty) <= RUSH_SEEK) { dx = tx; dy = ty; }
    }
    const float len = Length(dx, dy);
    if (len < 0.001f) return false;
    rush_dx = dx / len;
    rush_dy = dy / len;
    if (fabsf(rush_dx) > fabsf(rush_dy)) facing = rush_dx > 0 ? FACE_RIGHT : FACE_LEFT;
    else                                 facing = rush_dy > 0 ? FACE_DOWN  : FACE_UP;
    sprite.facing = facing;

    // The opening light swing's shape with the leap's own timing: a gather,
    // the flight -- whose last tenth of a second is when the blow lands -- and
    // a recovery on the ground. Weapon speed does not stretch it; the leap is
    // footwork, not a swing.
    const AttackProfile& light = ProfileFor(AttackType::Light, 0);
    AttackProfile p = light;
    p.windup      = 0.24f;
    p.active      = 0.10f;
    p.recover     = 0.20f;
    p.cooldown    = 0.12f;
    p.damage_mult = light.damage_mult * RUSH_DAMAGE;
    p.reach       = 34.0f;
    p.width       = 42.0f;
    p.knockback   = 70.0f;
    p.move_scale  = 0.0f;           // the leap steers itself
    ShapeForWeapon(p);

    combo = 0;
    combo_window = 0.0f;
    attack.type        = AttackType::Light;
    attack.profile     = p;
    attack.rate        = 1.0f;
    attack.damage_mult = p.damage_mult;
    attack.reach_scale = 1.0f;
    attack.combo       = 0;
    attack.timer       = 0.0f;
    attack.consumed    = false;
    rushing = true;
    rush_cooldown = RUSH_COOLDOWN;
    sprinting = false;

    const bool has_clip = sprite.Def() && sprite.Def()->Find("rush");
    sprite.speed_scale = 1.0f;
    sprite.Play(has_clip ? BothHands("rush") : AttackClip(), true);
    FitSwing();
    Audio::Play(Sfx::Jump);
    Audio::Play(Sfx::SwingHeavy, 0.9f, 1.1f);
    return true;
}

const ItemDef* Player::Shield() const {
    if (!item_db) return nullptr;
    const ItemDef* d = item_db->Get(equipment.InSlot(SLOT_SHIELD));
    return (d && d->block > 0.0f) ? d : nullptr;
}

bool Player::CanBlock() const {
    return Shield() && !dead && !jumping && !attack.Active() && !charging && !strong_armed &&
           !guard_broken && stamina > 0.0f && gather_clip.empty();
}

bool Player::GuardFacing(float from_x, float from_y) const {
    return blocking && Shield() && InFrontOf(facing, from_x - x, from_y - y);
}

void Player::ShatterGuard() {
    stamina = 0.0f;
    stamina_delay = STAMINA_DELAY * 2.0f;
    guard_broken = true;
    blocking = false;
    sprinting = false;
    Audio::Play(Sfx::Winded);
}

BlockOutcome Player::TryBlock(int damage, int attacker_level, float from_x, float from_y) {
    BlockOutcome none;
    none.taken = std::max(0, damage);
    const ItemDef* shield = Shield();
    if (!blocking || !shield || damage <= 0) return none;
    // Only what comes at the shield. A blow from behind finds the back.
    if (!InFrontOf(facing, from_x - x, from_y - y)) return none;

    // Bulwark: the shield arm learns, and a caught blow costs less breath.
    const float cost = shield->block_stamina * std::max(0.2f, 1.0f - talents.Global("block_cost"));
    BlockOutcome out = ResolveBlock(damage, attacker_level, shield->block, cost, stamina);
    stamina = std::max(0.0f, stamina - out.stamina);
    stamina_delay = STAMINA_DELAY;
    // Stopping a blow trains Defence at the rate landing one trains the skill
    // it was made with.
    BankXp(SKILL_DEFENCE, out.blocked * BLOCK_XP_PER_DAMAGE);
    if (out.broke) {
        guard_broken = true;
        blocking = false;
        Audio::Play(Sfx::Winded);
    }
    return out;
}

// XP follows the style used, the way OSRS ties training to how you fight:
// light swings feed Attack, heavy swings feed Strength, and everything feeds
// Hitpoints.
void Player::AwardCombatXp(int damage, AttackType type, float worth) {
    if (damage <= 0) return;

    auto bank = [&](int skill, float amount) {
        xp_fraction[skill] += amount;
        const int whole = static_cast<int>(xp_fraction[skill]);
        if (whole > 0) {
            xp_fraction[skill] -= whole;
            GrantXp(skill, whole);
        }
    };

    const float d = static_cast<float>(damage) * std::max(0.0f, worth);

    // A bow trains Ranged and a staff trains Magic whichever button fired it;
    // only melee splits its XP by how heavy the swing was.
    switch (Style()) {
        case AttackStyle::Ranged:
            bank(SKILL_RANGED, d * 4.0f);
            break;
        case AttackStyle::Magic:
            bank(SKILL_MAGIC, d * 4.0f);
            break;
        default:
            switch (type) {
                case AttackType::Light:   bank(SKILL_ATTACK, d * 4.0f); break;
                case AttackType::Strong:  bank(SKILL_STRENGTH, d * 4.0f); break;
                case AttackType::Charged: bank(SKILL_ATTACK, d * 2.0f);
                                          bank(SKILL_STRENGTH, d * 2.0f); break;
                // A blow with no swing behind it still landed: it is a light
                // one for this purpose, rather than one that teaches nothing.
                default:                  bank(SKILL_ATTACK, d * 4.0f); break;
            }
            break;
    }
    bank(SKILL_HITPOINTS, d * 1.33f);
}

float Player::ChargeProgress() const {
    return charging ? ChargeRatio(charge_held) : 0.0f;
}

vector<LevelUp> Player::TakeLevelUps() {
    vector<LevelUp> out;
    out.swap(pending_levels);
    return out;
}

vector<pair<int,int>> Player::TakeXpDrops() {
    vector<pair<int,int>> out;
    out.swap(pending_xp);
    return out;
}

void Player::StartGathering(const string& clip, const string& tool_model, float tx, float ty) {
    gather_clip = clip;
    gather_model = tool_model;
    // Face the tree, the rock or the water, not whichever way the walk ended.
    const float dx = tx - x, dy = ty - y;
    if (Length(dx, dy) > 1.0f) {
        if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
        else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
        sprite.facing = facing;
    }
}

void Player::StopGathering() {
    gather_clip.clear();
    gather_model.clear();
}

void Player::FacePoint(float tx, float ty) {
    const float dx = tx - x, dy = ty - (y - 16.0f);
    if (Length(dx, dy) < 1.0f) return;
    if (fabsf(dx) > fabsf(dy)) facing = (dx > 0) ? FACE_RIGHT : FACE_LEFT;
    else                       facing = (dy > 0) ? FACE_DOWN  : FACE_UP;
    sprite.facing = facing;
}

void Player::TurnToTarget(const World& world) {
    const Enemy* t = CurrentTarget(world);
    if (!t) return;
    const SDL_FPoint a = Targeting::AimPoint(*t);
    if (Style() == AttackStyle::Melee &&
        Length(a.x - x, a.y - (y - 16.0f)) > Targeting::MELEE_ASSIST * WeaponReach()) return;
    FacePoint(a.x, a.y);
}

float Player::WeaponReach() const {
    const ItemDef* w = equipment.Weapon();
    return (w && Style() == AttackStyle::Melee) ? std::max(0.5f, w->reach) : 1.0f;
}

void Player::ShapeForWeapon(AttackProfile& p) const {
    const ItemDef* w = equipment.Weapon();
    if (!w || Style() != AttackStyle::Melee) return;
    p.reach     *= std::max(0.5f, w->reach);
    p.width     *= std::clamp(w->sweep, 0.3f, 2.0f);
    p.sweep_deg *= std::clamp(w->sweep, 0.3f, 2.0f);      // a spear's cleave is a spear's
    p.knockback *= std::max(0.0f, w->push);
}

string Player::AttackClip() const {
    const ItemDef* w = equipment.Weapon();
    if (w && !w->attack_clip.empty() && sprite.Def() && sprite.Def()->Find(w->attack_clip))
        return w->attack_clip;
    return "attack";
}

string Player::BothHands(const string& clip) const {
    const ItemDef* w = equipment.Weapon();
    if (w && w->two_handed && w->kind == WeaponKind::Melee && sprite.Def() && sprite.Def()->Find(clip + "_2h"))
        return clip + "_2h";
    return clip;
}

void Player::FitSwing() {
    // The sword's clips run at their own rate and are cut off where the attack
    // ends, which suits six quick frames. An eight-frame swing that is half
    // wind-up does not survive that: at any fixed rate it is either over before
    // a heavy has landed or has not come round by the time a light is done. So
    // a clip that asks is stretched over the attack, whatever the attack is --
    // and the weapon's slowness, which is already in the attack, is in the clip.
    const AnimClip* c = sprite.Def() ? sprite.Def()->Find(sprite.current) : nullptr;
    const float total = attack.profile.Total();
    if (!c || !c->fit || total <= 0.01f || c->fps <= 0.0f) return;
    sprite.speed_scale = static_cast<float>(c->frames) / (c->fps * total);
}

void Player::FireStrong(bool charged, float ratio, const World& world) {
    const float speed = WeaponSpeed();
    attack.type    = charged ? AttackType::Charged : AttackType::Strong;
    attack.move    = ComboMove::None;
    attack.profile = ScaleForSpeed(ProfileFor(attack.type), speed);
    ShapeForWeapon(attack.profile);
    attack.rate    = speed;
    attack.damage_mult = charged ? ChargeMultiplier(ratio) : attack.profile.damage_mult;
    // A greataxe held and let go is a chop, not a bigger sweep: longer down
    // the line of it, half as wide, and harder.
    const ItemDef* held = equipment.Weapon();
    const bool own_charge = charged && held && !held->charge_clip.empty() && Style() == AttackStyle::Melee;
    if (own_charge) {
        attack.profile.reach     *= held->charge_reach;
        attack.profile.width     *= held->charge_sweep;
        attack.profile.sweep_deg *= held->charge_sweep;
        attack.damage_mult       *= held->charge_damage;
    }
    // A fuller charge also swings wider.
    attack.reach_scale = charged ? (1.0f + 0.35f * ratio) : 1.0f;
    attack.combo    = 0;
    attack.timer    = 0.0f;
    attack.consumed = false;
    TurnToTarget(world);
    sprite.speed_scale = 1.0f / std::clamp(speed, 0.35f, 3.0f);
    sprite.Play(own_charge && sprite.Def() && sprite.Def()->Find(held->charge_clip) ? held->charge_clip : AttackClip(), true);
    FitSwing();
    if (Style() == AttackStyle::Melee)
        Audio::Play(Sfx::SwingHeavy, charged ? 1.0f : 0.85f, charged ? 0.85f : 1.0f);
    combo        = 0;
    combo_window = 0.0f;
    after_strong = false;
}

string Player::ComboClip(ComboMove move) const {
    const char* name = move == ComboMove::Crush    ? "crush"
                     : move == ComboMove::Cleave   ? "cleave"
                     : move == ComboMove::Backhand ? "backhand"
                     : move == ComboMove::CrossCut ? "spin" : "";
    // A bow or a staff plays its own draw or cast: the sword's combo clips
    // would swing it like a blade.
    if (Style() == AttackStyle::Melee && *name && sprite.Def() && sprite.Def()->Find(name)) return BothHands(name);
    return AttackClip();
}

void Player::StartCombo(ComboMove move, AttackType type, const World& world) {
    const float speed = WeaponSpeed();
    attack.type        = type;
    attack.move        = move;
    attack.profile     = ScaleForSpeed(ProfileForCombo(move), speed);
    ShapeForWeapon(attack.profile);
    attack.rate        = speed;
    attack.damage_mult = attack.profile.damage_mult;
    attack.reach_scale = 1.0f;
    attack.combo       = combo;
    attack.timer       = 0.0f;
    attack.consumed    = false;
    TurnToTarget(world);
    combo_window = 0.0f;
    after_strong = false;
    sprite.speed_scale = 1.0f / std::clamp(speed, 0.35f, 3.0f);
    sprite.Play(ComboClip(move), true);
    FitSwing();                                       // a wand's combo is a wand's flick
    if (Style() != AttackStyle::Melee) return;
    switch (move) {
        case ComboMove::Crush:    Audio::Play(Sfx::SwingHeavy, 0.95f, 0.9f);  break;
        case ComboMove::Cleave:   Audio::Play(Sfx::SwingHeavy, 1.0f,  0.8f);  break;
        case ComboMove::Backhand: Audio::Play(Sfx::Swing,      1.0f,  1.15f); break;
        case ComboMove::CrossCut: Audio::Play(Sfx::SwingHeavy, 1.0f,  1.2f);  break;
        default: break;
    }
}

void Player::CountChainHit(const string& label) {
    ++chain_hits;
    chain_trail.push_back(label);
    // The HUD has a line's worth of room: the run's count is the number, the
    // trail is only ever its tail.
    if (chain_trail.size() > 6) chain_trail.erase(chain_trail.begin());
    chain_show = CHAIN_HOLD;
}

void Player::BreakChain() {
    chain_hits = 0;
    chain_trail.clear();
    chain_show = 0.0f;
}

float Player::ChainFade() const {
    return std::clamp(chain_show / CHAIN_FADE, 0.0f, 1.0f);
}

ComboMove Player::NextCombo(bool light) const {
    if (combo_window <= 0.0f) return ComboMove::None;
    if (light) return after_strong ? ComboMove::Backhand : ComboMove::None;
    if (after_strong) return ComboMove::None;
    return combo == 0 ? ComboMove::Crush : ComboMove::Cleave;
}

const Enemy* Player::CurrentTarget(const World& world) const {
    return local ? world.targeting.Current() : nullptr;
}

const Enemy* Player::LockedTarget(const World& world) const {
    return local ? world.targeting.Locked() : nullptr;
}

void Player::Pose(float px, float py, Facing face, const string& clip, int frame, const ItemDatabase* db) {
    x = px;
    y = py;
    facing = face;
    sprite.facing = face;
    sprite.style = BuildLayerStyle(db);
    sprite.Play(clip.empty() ? string("idle") : clip);
    sprite.SetFrame(frame);
}

void Player::HandleAttackInput(const PlayerInput& in, float dt, const World& world) {
    const float speed = WeaponSpeed();
    // The combos read the same with every weapon; what comes out of them is
    // the weapon's own. So nothing here asks what is in hand.
    const bool  melee = true;
    const bool  raw_light  = in.Pressed(PlayerInput::Light);
    const bool  raw_strong = in.Pressed(PlayerInput::Strong);

    // A press during a swing, or in the gap after it, is kept for a moment
    // and used the instant the next swing may start, so a chain does not hang
    // on a frame-perfect tap. The two buttons are kept apart, so two presses
    // inside one swing still read as "together".
    if (raw_light  && !CanAttack()) buf_light  = BUFFER_WINDOW;
    if (raw_strong && !CanAttack()) buf_strong = BUFFER_WINDOW;
    if (buf_light  > 0.0f) buf_light  = std::max(0.0f, buf_light  - dt);
    if (buf_strong > 0.0f) buf_strong = std::max(0.0f, buf_strong - dt);
    bool light_press = raw_light, strong_press = raw_strong;
    if (CanAttack()) {
        if (buf_light  > 0.0f) light_press  = true;
        if (buf_strong > 0.0f) strong_press = true;
        buf_light = buf_strong = 0.0f;
    }

    // --- both at once: the Cross Cut ---------------------------------------------
    // The two buttons inside a few frames of each other. Whichever came first
    // has already started something -- a light swing, or the hold a strong
    // begins with -- and it is taken back: the swing has not reached its
    // active frames and the hold has barely begun. It costs stamina, so with
    // none left the presses mean what they mean on their own.
    const bool fresh_light = attack.type == AttackType::Light && attack.move == ComboMove::None &&
                             !rushing && attack.timer <= TOGETHER_WINDOW;
    const bool fresh_hold  = strong_armed && charge_held <= TOGETHER_WINDOW;
    // It needs the breath it spends. The test used to be "any breath at all",
    // and the spend below is clamped at nothing, so on an empty bar it cost one
    // frame's regeneration and could be thrown about once a second for ever.
    const bool together = melee && !jumping && stamina >= CROSS_CUT_STAMINA && !winded &&
        ((light_press && strong_press && CanAttack()) ||
         (raw_strong && fresh_light) ||
         (raw_light && fresh_hold && CanAttack()));
    if (together) {
        strong_armed = charging = false;
        charge_held  = 0.0f;
        StartCombo(ComboMove::CrossCut, AttackType::Strong, world);
        stamina = std::max(0.0f, stamina - CROSS_CUT_STAMINA);
        stamina_delay = STAMINA_DELAY;
        return;
    }

    // --- the light button ------------------------------------------------------
    // At a sprint, with Rushing Strike learned and rested, the light attack is
    // a leap. Only as an opener: mid-chain it stays the next link. While the
    // heavy button is held past the "together" window, the hold owns the
    // hands and a light does nothing.
    //
    // A sprint, not a push on the stick. This used to ask only that the stick
    // was past the run threshold, which a controller's walk is not and every
    // step on a keyboard is: so with the move learned, any light attack made
    // while walking leapt whenever the three seconds were up, sprint button
    // or no. `sprinting` is last frame's answer -- this runs before it is
    // worked out again -- and already means the button held, a real push,
    // breath to spend and no lockout.
    const bool rushed = light_press && CanAttack() && !strong_armed && combo_window <= 0.0f &&
                        sprinting && StartRush(world);
    if (!rushed && light_press && CanAttack() && !strong_armed) {
        if (melee && after_strong && combo_window > 0.0f) {
            // A light on the heels of a heavy: the Backhand. It stands in for
            // the first two links, so the chain goes on from it -- the next
            // light is the finisher, the next heavy the Cleave.
            combo = 1;
            StartCombo(ComboMove::Backhand, AttackType::Light, world);
        } else {
            const int index = (combo_window > 0.0f) ? std::min(combo + 1, 2) : 0;
            combo = index;
            attack.type        = AttackType::Light;
            attack.move        = ComboMove::None;
            attack.profile     = ScaleForSpeed(ProfileFor(AttackType::Light, index), speed);
            ShapeForWeapon(attack.profile);
            attack.rate        = speed;
            attack.damage_mult = attack.profile.damage_mult;
            attack.reach_scale = 1.0f;
            attack.combo       = index;
            attack.timer       = 0.0f;
            attack.consumed    = false;
            TurnToTarget(world);
            sprite.speed_scale = 1.0f / std::clamp(speed, 0.35f, 3.0f);
            // A pair of daggers strikes hand after hand: right, left, right.
            const ItemDef* held = equipment.Weapon();
            const bool pair = held && equipment.DualWielding() && !held->offhand_clip.empty() &&
                              sprite.Def() && sprite.Def()->Find(held->offhand_clip);
            const bool left = pair && left_hand_next;
            left_hand_next = pair && !left_hand_next;
            sprite.Play(left ? held->offhand_clip : AttackClip(), true);
            FitSwing();
            combo_window = 0.0f;
            after_strong = false;
            // A bow or a staff makes its own noise when the shot leaves.
            if (Style() == AttackStyle::Melee) Audio::Play(Sfx::Swing, 1.0f, 1.0f + 0.06f * index);
        }
    }

    // --- the heavy button ------------------------------------------------------
    if (strong_press && CanAttack() && !strong_armed) {
        if (melee && combo_window > 0.0f && !after_strong) {
            // A heavy inside the chain comes out on the press, with no hold:
            // the Crushing Blow after one light, the Cleave after two.
            StartCombo(combo == 0 ? ComboMove::Crush : ComboMove::Cleave, AttackType::Strong, world);
        } else if (!in.Down(PlayerInput::Strong)) {
            // Pressed and let go again inside the last swing: a plain strong,
            // now, rather than a hold that has already ended.
            FireStrong(false, 0.0f, world);
        } else {
            // Strong and charged share the button: press starts the hold,
            // release decides which one actually comes out.
            strong_armed = true;
            charge_held  = 0.0f;
            charging     = false;
        }
    }

    if (strong_armed && in.Down(PlayerInput::Strong)) {
        charge_held += dt * (1.0f + talents.Global("charge"));
        if (charge_held >= CHARGE_HOLD_THRESHOLD) charging = true;
    }

    if (strong_armed && in.Released(PlayerInput::Strong)) {
        const bool was_charged = charging && charge_held >= CHARGE_HOLD_THRESHOLD;
        FireStrong(was_charged, ChargeRatio(charge_held), world);
        strong_armed = false;
        charging     = false;
        charge_held  = 0.0f;
    }

    // Releasing off-screen or with the button remapped mid-hold: fail safe.
    if (strong_armed && !in.Down(PlayerInput::Strong) && !in.Released(PlayerInput::Strong)) {
        strong_armed = false;
        charging = false;
        charge_held = 0.0f;
    }
}

void Player::UpdateAttack(float dt) {
    if (!attack.Active()) {
        if (attack_cooldown > 0.0f) {
            attack_cooldown -= dt;
            if (attack_cooldown <= 0.0f) {
                attack_cooldown = 0.0f;
                // The swing is fully over, so hand the frame rate back to the
                // walk and idle clips.
                sprite.speed_scale = 1.0f;
            }
        }
        return;
    }
    attack.timer += dt;
    // Let go. A breath held or a spell overloaded goes into this one, and is
    // spent here rather than in the world, so a friend's window -- where the
    // world decides nothing -- spends it at the same moment the host does.
    if (!attack.loosed && attack.timer >= attack.profile.windup) {
        attack.loosed = true;
        if (Style() == AttackStyle::Ranged && aim_timer > 0.0f)      { attack.empowered = true; aim_timer = 0.0f; }
        if (Style() == AttackStyle::Magic && overload_timer > 0.0f)  { attack.empowered = true; overload_timer = 0.0f; }
    }
    if (attack.Finished()) {
        // What the next press means is decided here. A light that was not the
        // finisher leaves the window open to go on with the chain; a plain
        // strong leaves one for a Backhand; everything else -- the finisher,
        // the leap, a charged attack, any of the combos -- closes it, so the
        // next chain starts from the top. The finisher used to leave it open
        // too, and a fourth light was another finisher.
        const bool light = attack.type == AttackType::Light && !rushing;
        const bool plain_strong = attack.type == AttackType::Strong && attack.move == ComboMove::None;
        if (light && combo < 2) {
            combo_window = COMBO_WINDOW;
            after_strong = false;
        } else if (plain_strong) {
            combo_window = COMBO_WINDOW;
            after_strong = true;
            combo = 0;
        } else {
            combo_window = 0.0f;
            after_strong = false;
            combo = 0;
        }
        rushing = false;
        attack_cooldown = attack.profile.cooldown;
        cooldown_total  = std::max(0.0001f, attack.profile.cooldown);
        attack.Clear();
    }
}

Player::JumpPlan Player::PlanJump(const Map& map, float dir_x, float dir_y) const {
    JumpPlan plan;
    const float len = Length(dir_x, dir_y);
    if (len < 0.01f) return plan;
    const float ux = dir_x / len, uy = dir_y / len;

    const SDL_FRect here = Bounds();
    const int from_level = map.LevelAt(x, y);

    auto box_at = [&](float px, float py) {
        return SDL_FRect{here.x + (px - x), here.y + (py - y), here.w, here.h};
    };

    // Walk outward looking for the edge. Every sample on the way has to be
    // clear of walls and within reach of the starting level, so a jump never
    // passes through a tree or over the top of a sheer three-level cliff to
    // land on the far side of it.
    constexpr float STEP = 4.0f, REACH = 64.0f;
    for (float d = STEP; d <= REACH; d += STEP) {
        const float px = x + ux * d, py = y + uy * d;
        if (map.Blocked(box_at(px, py))) break;

        const int level = map.LevelAt(px, py);
        const int diff = level - from_level;
        if (std::abs(diff) > CLIMB_LEVELS) break;

        if (diff != 0) {
            // Found the ledge. Carry on a little past the edge so the whole
            // body lands on the new level rather than teetering on its lip.
            const float land = d + 14.0f;
            const float lx = x + ux * land, ly = y + uy * land;
            if (map.Blocked(box_at(lx, ly)) || map.LevelAt(lx, ly) != level)
                break;
            plan.ok = true;
            plan.x = lx;
            plan.y = ly;
            plan.levels = diff;
            return plan;
        }
    }

    // No ledge within reach: a short hop along flat ground, if there is room.
    constexpr float HOP = 22.0f;
    const float hx = x + ux * HOP, hy = y + uy * HOP;
    if (!map.Blocked(box_at(hx, hy)) && map.LevelAt(hx, hy) == from_level &&
        !map.LevelChangeBlocked(x, y, hx, hy)) {
        plan.ok = true;
        plan.x = hx;
        plan.y = hy;
        plan.levels = 0;
    }
    return plan;
}

void Player::UpdateJump(float dt, const Map& map) {
    (void)map;
    jump_timer += dt;
    const float t = std::clamp(jump_timer / JUMP_DURATION, 0.0f, 1.0f);
    // Eased, so the take-off and landing read as effort rather than as a
    // constant-speed slide between two points.
    const float e = t * t * (3.0f - 2.0f * t);
    x = jump_from_x + (jump_to_x - jump_from_x) * e;
    y = jump_from_y + (jump_to_y - jump_from_y) * e;

    if (t >= 1.0f) {
        jumping = false;
        x = jump_to_x;
        y = jump_to_y;
        Audio::Play(Sfx::Land, 0.8f);
    }
}

float Player::JumpLift() const {
    if (!jumping) return 0.0f;
    const float t = std::clamp(jump_timer / JUMP_DURATION, 0.0f, 1.0f);
    const float e = t * t * (3.0f - 2.0f * t);
    const float ground = jump_lift_from + (jump_lift_to - jump_lift_from) * e;
    return ground + sinf(t * 3.14159265f) * JUMP_HEIGHT;
}

float Player::CooldownProgress() const {
    if (attack_cooldown <= 0.0f) return 0.0f;
    return std::clamp(attack_cooldown / cooldown_total, 0.0f, 1.0f);
}

float Player::TalentDamage(AttackStyle style, AttackType type) const {
    float mult = 1.0f + talents.Effect("damage", style);
    if (style == Affinity()) mult += AFFINITY_DAMAGE;
    if (type == AttackType::Charged) mult += talents.Effect("charged_damage", style);
    if (max_hp > 0 && hp * 3 < max_hp) mult += talents.Effect("low_hp_damage", style);
    // The shout's strength is in the arm, whatever it holds.
    if (war_cry_timer > 0.0f && style == AttackStyle::Melee) mult += WAR_CRY_DAMAGE;
    return mult;
}

// -----------------------------------------------------------------------------
//  Abilities
// -----------------------------------------------------------------------------

bool Player::TryAbility(int slot, World& world) {
    const TalentNode* node = talents.Ability(slot);
    if (!node || slot < 0 || slot >= SkillTrees::ABILITY_SLOTS) return false;
    if (dead || jumping || resting || ability_cd[slot] > 0.0f) return false;
    // Not out of a swing: an ability is a decision, not a cancel. The roll and
    // the blink are the exceptions -- getting out is what they are for.
    const bool escape = node->ability == "tumble" || node->ability == "blink";
    if (!escape && (attack.Active() || charging)) return false;
    if (node->stamina_cost > 0 && (winded || stamina < static_cast<float>(node->stamina_cost))) return false;
    if (node->mana_cost > 0 && mana < node->mana_cost) return false;

    // Which way: where the stick is pushed, or failing that the facing.
    float dx = move_axis.x, dy = move_axis.y;
    const bool steering = Length(dx, dy) > 0.3f;
    if (!steering) {
        dx = (facing == FACE_LEFT) ? -1.0f : (facing == FACE_RIGHT ? 1.0f : 0.0f);
        dy = (facing == FACE_UP)   ? -1.0f : (facing == FACE_DOWN  ? 1.0f : 0.0f);
    }
    const float len = std::max(0.001f, Length(dx, dy));
    dx /= len; dy /= len;

    if (node->ability == "blink") {
        // As far as there is somewhere to stand, on the level being stood on:
        // through a monster or across a gap, not through a wall or up a cliff.
        const int level = world.map.LevelAt(x, y);
        float best = 0.0f;
        for (float d = BLINK_DISTANCE; d >= 12.0f; d -= 8.0f) {
            SDL_FRect there = Bounds();
            there.x += dx * d;
            there.y += dy * d;
            if (world.map.Blocked(there) || world.map.LevelAt(x + dx * d, y + dy * d) != level) continue;
            best = d;
            break;
        }
        if (best <= 0.0f) return false;          // nowhere to go: nothing is spent
        x += dx * best;
        y += dy * best;
        knock_x = knock_y = 0.0f;
    } else if (node->ability == "tumble") {
        // Standing still, a roll goes back the way you came.
        const float sign = steering ? 1.0f : -1.0f;
        knock_x = dx * sign * TUMBLE_SPEED;
        knock_y = dy * sign * TUMBLE_SPEED;
        tumble_timer = TUMBLE_TIME;
        attack.Clear();
        strong_armed = charging = false;
        charge_held = 0.0f;
    } else if (node->ability == "war_cry") {
        war_cry_timer = WAR_CRY_TIME;
    } else if (node->ability == "mana_shield") {
        mana_shield_timer = MANA_SHIELD_TIME;
    } else if (node->ability == "frenzy") {
        frenzy_timer = FRENZY_TIME;
    } else if (node->ability == "stand_fast") {
        stand_fast_timer = STAND_FAST_TIME;
        knock_x = knock_y = 0.0f;
    } else if (node->ability == "take_aim") {
        aim_timer = AIM_WINDOW;
    } else if (node->ability == "rapid_fire") {
        rapid_timer = RAPID_TIME;
    } else if (node->ability == "overload") {
        overload_timer = OVERLOAD_WINDOW;
    } else if (node->ability == "invoke") {
        if (mana >= max_mana) return false;       // nothing to draw back: nothing is spent
        invoke_timer = INVOKE_TIME;
        invoke_bank = 0.0f;
    }

    if (node->stamina_cost > 0) {
        stamina = std::max(0.0f, stamina - static_cast<float>(node->stamina_cost));
        stamina_delay = STAMINA_DELAY;
    }
    if (node->mana_cost > 0) SpendMana(node->mana_cost);
    ability_cd[slot] = node->cooldown;
    pending_ability = node->ability;
    Audio::Play(node->mana_cost > 0 ? Sfx::SpellCast : Sfx::SwingHeavy, 0.9f, 0.8f);
    return true;
}

int Player::AbsorbWithMana(int damage) {
    if (mana_shield_timer <= 0.0f || damage <= 1 || mana < MANA_PER_HP) return damage;
    const int want = damage / 2;
    const int paid = std::min(want, mana / MANA_PER_HP);
    mana -= paid * MANA_PER_HP;
    if (paid > 0) shield_struck = 0.0f;
    return damage - paid;
}

void Player::NoteBlock() {
    if (talents.Effect("riposte", AttackStyle::Melee) > 0.0f) riposte_timer = RIPOSTE_WINDOW;
}

int Player::NoteShotOn(const void* who) {
    if (who == weak_target) weak_stacks = std::min(WEAK_POINT_MAX, weak_stacks + 1);
    else { weak_target = who; weak_stacks = 0; }
    return weak_stacks;
}

void Player::NoteCast(Element e) {
    if (e == Element::None) return;
    if (e == attune_element) attune_stacks = std::min(ATTUNE_MAX, attune_stacks + 1);
    else { attune_element = e; attune_stacks = 0; }
}

float Player::WeaponSpeed() const {
    const float base = item_db ? equipment.AttackSpeed() : 1.0f;
    // Below one is faster, so a speed talent takes a share off the time.
    float time = base * (1.0f - talents.Effect("speed", Style()));
    if (frenzy_timer > 0.0f && Style() == AttackStyle::Melee) time *= 1.0f - FRENZY_SPEED;
    if (rapid_timer > 0.0f && Style() == AttackStyle::Ranged) time *= 1.0f - RAPID_SPEED;
    return std::max(0.3f, time);
}

void Player::UpdateAnimation(const Vec2& move) {
    if (dead) { sprite.Play("death"); return; }
    if (attack.Active()) return;                     // attack clip owns the frames
    if (blocking) {
        // Guard up, stepping or not. A rig with no guard pose stands in its
        // idle rather than walking with its shield down.
        const bool has_clip = sprite.Def() && sprite.Def()->Find("block");
        sprite.Play(has_clip ? "block" : "idle");
        if (attack_cooldown <= 0.0f) sprite.speed_scale = 1.0f;
        return;
    }

    const float mag = Length(move.x, move.y);
    // A crossbow being spanned, standing: the nose down and the string hauled back.
    if (reload_left > 0.0f && mag < 0.05f && sprite.Def() && sprite.Def()->Find("reload")) {
        sprite.Play("reload");
        sprite.speed_scale = 1.0f;
        return;
    }
    // The work, looped for as long as it goes on. A rig with no clip for it
    // swings its attack over and over instead.
    if (!gather_clip.empty() && mag < 0.05f) {
        const bool has_clip = sprite.Def() && sprite.Def()->Find(gather_clip);
        if (has_clip) {
            sprite.Play(gather_clip);
        } else if (sprite.current != "attack" || sprite.Finished()) {
            sprite.Play("attack", true);
        }
        if (attack_cooldown <= 0.0f) sprite.speed_scale = 1.0f;
        return;
    }
    // Playback speed belongs to the attack until its cooldown ends.
    const bool own_speed = attack_cooldown <= 0.0f;
    if (mag < 0.05f)              sprite.Play("idle");
    else if (mag < RUN_THRESHOLD) sprite.Play("walk");
    else if (sprinting) {
        // A rig with its own sprint plays it. One without runs faster, so
        // its feet still keep up with the ground going by.
        const bool has_clip = sprite.Def() && sprite.Def()->Find("sprint");
        sprite.Play(has_clip ? "sprint" : "run");
        if (own_speed) sprite.speed_scale = has_clip ? 1.0f : SPRINT_MULT * 0.85f;
        return;
    }
    else                          sprite.Play("run");
    if (own_speed) sprite.speed_scale = 1.0f;
}

void Player::Update(float dt, World& world, const GameContext& ctx) {
    // What abilities and their passives leave running.
    since_hurt += dt;
    if (reload_left > 0.0f) {
        reload_left = std::max(0.0f, reload_left - dt);
        // Put down for something else, it is not spanned by the time it is picked up again.
        const ItemDef* held = equipment.Weapon();
        if (!held || held->reload <= 0.0f) reload_left = 0.0f;
    }
    for (float& cd : ability_cd) cd = std::max(0.0f, cd - dt);
    tumble_timer      = std::max(0.0f, tumble_timer - dt);
    war_cry_timer     = std::max(0.0f, war_cry_timer - dt);
    mana_shield_timer = std::max(0.0f, mana_shield_timer - dt);
    shield_struck     = std::min(99.0f, shield_struck + dt);
    riposte_timer     = std::max(0.0f, riposte_timer - dt);
    hit_run_timer     = std::max(0.0f, hit_run_timer - dt);
    frenzy_timer      = std::max(0.0f, frenzy_timer - dt);
    stand_fast_timer  = std::max(0.0f, stand_fast_timer - dt);
    aim_timer         = std::max(0.0f, aim_timer - dt);
    rapid_timer       = std::max(0.0f, rapid_timer - dt);
    overload_timer    = std::max(0.0f, overload_timer - dt);
    if (invoke_timer > 0.0f) {
        // Half of all the mana there is, over the four seconds.
        const float step = std::min(dt, invoke_timer);
        invoke_timer -= step;
        invoke_bank += static_cast<float>(max_mana) * INVOKE_SHARE * step / INVOKE_TIME;
        const int whole = static_cast<int>(invoke_bank);
        if (whole > 0) { invoke_bank -= static_cast<float>(whole); GainMana(whole); }
    }
    // Frenzy: the chain does not lapse between blows.
    if (frenzy_timer > 0.0f && chain_hits > 0) chain_show = std::max(chain_show, CHAIN_HOLD);
    if (hurt_flash > 0.0f) hurt_flash = std::max(0.0f, hurt_flash - dt);
    if (ctx.statuses) status_db = ctx.statuses;
    if (!dead) TickStatuses(dt, world);
    // Every source of damage lowers hp; listening for that catches them all.
    if (heard_hp >= 0 && hp < heard_hp && hp > 0) {
        Audio::Play(Sfx::PlayerHurt);
        // A hit breaks a sprint and holds it off long enough to matter.
        sprint_lockout = SPRINT_LOCKOUT;
        sprinting = false;
    }
    heard_hp = hp;
    if (sprint_lockout > 0.0f) sprint_lockout = std::max(0.0f, sprint_lockout - dt);
    if (rush_cooldown > 0.0f) rush_cooldown = std::max(0.0f, rush_cooldown - dt);
    if (combo_window > 0.0f) {
        combo_window -= dt;
        if (combo_window <= 0.0f) { combo = 0; after_strong = false; }
    }
    if (chain_show > 0.0f) {
        chain_show -= dt;
        if (chain_show <= 0.0f) BreakChain();
    }

    // --- death ---------------------------------------------------------------
    if (hp <= 0 && !dead) {
        dead = true;
        sprinting = false;
        look_ahead = {0, 0};
        death_timer = DEATH_DURATION;
        attack.Clear();
        attack_cooldown = 0.0f;
        sprite.speed_scale = 1.0f;
        charging = strong_armed = after_strong = false;
        buf_light = buf_strong = combo_window = 0.0f;
        combo = 0;
        BreakChain();
        jumping = false;
        statuses.Clear();
        sprite.Play("death", true);
        Audio::Play(Sfx::PlayerDie);
    }
    if (dead) {
        death_timer = std::max(0.0f, death_timer - dt);
        sprite.Update(dt);
        return;
    }

    // --- airborne ------------------------------------------------------------
    // A jump owns the player for its whole length: no steering, no attacks,
    // no knockback. It was validated when it started, so nothing mid-air can
    // make it land somewhere it should not.
    if (jumping) {
        UpdateJump(dt, world.map);
        climb_hint.clear();
        sprite.style = BuildLayerStyle(item_db);
        sprite.Update(dt);
        return;
    }

    // --- input ---------------------------------------------------------------
    Vec2 move{0, 0};
    moving = false;
    // The hands, never the device: see player_input.h.
    // Held fast or beguiled, the hands are not theirs: no swing, no guard, no
    // jump, and their feet go where the charm draws them or nowhere at all.
    const bool bound = Held() || Charmed();
    if (!input_locked) {
        move = hands.move;
        if (Held()) {
            move = {0.0f, 0.0f};
        } else if (Charmed()) {
            // To whoever cast it, and no nearer than arm's length.
            const float cdx = charm_x - x, cdy = charm_y - y;
            const float far = Length(cdx, cdy);
            move = far > 28.0f ? Vec2{cdx / far, cdy / far} : Vec2{0.0f, 0.0f};
        } else if (Confused()) {
            // Which way is which is backwards.
            move = {-move.x, -move.y};
        }
        moving = Length(move.x, move.y) > 0.3f;
        move_axis = move;
        // The guard first: a raised shield is not something a swing starts
        // from, and a strong press that was being held is let go of.
        blocking = !bound && hands.Down(PlayerInput::Block) && CanBlock();
        // The abilities' shift held and an attack button: an ability, if one
        // is carried there, and the press is the ability's, not a swing. The
        // shift is RB on a pad and the guard on the keys: see Action::Ability.
        PlayerInput for_attacks = hands;
        if (bound) for_attacks.down = for_attacks.pressed = for_attacks.released = 0;
        if (!bound && hands.Down(PlayerInput::Ability)) {
            for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
                const uint8_t button = slot == 0 ? PlayerInput::Light : slot == 1 ? PlayerInput::Strong : PlayerInput::Target;
                if (!talents.Ability(slot)) continue;
                if (hands.Pressed(static_cast<PlayerInput::Button>(button))) TryAbility(slot, world);
                for_attacks.pressed &= static_cast<uint8_t>(~button);
            }
        }
        if (blocking) {
            strong_armed = charging = false;
            charge_held = 0.0f;
        } else {
            HandleAttackInput(for_attacks, dt, world);
        }

        // Which way a jump would go: where you are steering, or failing that
        // where you are facing.
        float jx = move.x, jy = move.y;
        if (Length(jx, jy) < 0.3f) {
            jx = (facing == FACE_LEFT) ? -1.0f : (facing == FACE_RIGHT ? 1.0f : 0.0f);
            jy = (facing == FACE_UP)   ? -1.0f : (facing == FACE_DOWN  ? 1.0f : 0.0f);
        }

        const JumpPlan plan = PlanJump(world.map, jx, jy);

        // Only a ledge earns a prompt, and only while you are pushing at it --
        // standing near an edge you have no intention of climbing is not
        // something the screen needs to keep pointing out.
        climb_hint.clear();
        if (plan.ok && plan.levels != 0 && Length(move.x, move.y) > 0.3f)
            climb_hint = plan.levels > 0 ? "Climb up" : "Drop down";

        if (!bound && hands.Pressed(PlayerInput::Jump) && !attack.Active() && !charging) {
            jumping      = true;
            sprinting    = false;
            jump_timer   = 0.0f;
            jump_from_x  = x;  jump_from_y = y;
            // A jump nothing can land from is still a jump: a hop in place,
            // so the button always answers.
            jump_to_x    = plan.ok ? plan.x : x;
            jump_to_y    = plan.ok ? plan.y : y;
            jump_lift_from = world.map.HeightAt(x, y);
            jump_lift_to   = world.map.HeightAt(jump_to_x, jump_to_y);
            knock_x = knock_y = 0.0f;
            sprite.speed_scale = 1.0f;
            sprite.Play("jump", true);
            Audio::Play(Sfx::Jump);
            climb_hint.clear();
            sprite.style = BuildLayerStyle(item_db);
            sprite.Update(dt);
            return;
        }
    } else {
        climb_hint.clear();
        blocking = false;
        // Dropping input mid-charge should not leave a swing armed.
        strong_armed = false;
        charging = false;
        charge_held = 0.0f;
    }

    UpdateAttack(dt);

    // Sprinting: the button held, a real push on the stick, and nothing else
    // going on. A light tilt stays a walk however hard the button is held.
    sprinting = !input_locked && hands.Down(PlayerInput::Sprint) &&
                Length(move.x, move.y) >= RUN_THRESHOLD &&
                !attack.Active() && !charging && !strong_armed && !blocking &&
                sprint_lockout <= 0.0f && !winded && stamina > 0.0f;

    // Stamina: spent by the second while sprinting, back after a breather.
    if (sprinting) {
        stamina = std::max(0.0f, stamina - STAMINA_DRAIN * dt);
        stamina_delay = STAMINA_DELAY;
        if (stamina <= 0.0f) {
            // Out of breath. The sprint ends now, not next frame.
            winded = true;
            sprinting = false;
            Audio::Play(Sfx::Winded);
        }
    } else if (stamina_delay > 0.0f) {
        stamina_delay = std::max(0.0f, stamina_delay - dt);
    } else if (stamina < MaxStamina()) {
        const float rate = STAMINA_REGEN * (1.0f + talents.Global("stamina_regen")) *
            (Length(move.x, move.y) > 0.05f ? STAMINA_REGEN_MOVE : 1.0f);
        stamina = std::min(MaxStamina(), stamina + rate * dt);
    }
    if (winded && stamina >= MaxStamina() * STAMINA_RECOVER) winded = false;
    if (guard_broken && stamina >= MaxStamina() * STAMINA_RECOVER) guard_broken = false;
    {
        const float lead = sprinting ? 56.0f : 0.0f;
        const float k = std::min(1.0f, dt * (sprinting ? 2.5f : 4.0f));
        look_ahead.x += (move.x * lead - look_ahead.x) * k;
        look_ahead.y += (move.y * lead - look_ahead.y) * k;
    }

    // Face the way you are moving, but never mid-swing. Standing still with a
    // lock on, face the locked monster, so the next shot does not have to turn.
    // Behind a shield, keep it between you and whatever you are fighting while
    // you step: turning to walk away would turn the guard away with you.
    const Enemy* guard_target = blocking ? CurrentTarget(world) : nullptr;
    if (guard_target) {
        const SDL_FPoint a = Targeting::AimPoint(*guard_target);
        FacePoint(a.x, a.y);
    } else if (!attack.Active() && Length(move.x, move.y) > 0.05f) {
        if (fabsf(move.x) > fabsf(move.y)) facing = (move.x > 0) ? FACE_RIGHT : FACE_LEFT;
        else                               facing = (move.y > 0) ? FACE_DOWN  : FACE_UP;
    } else if (!attack.Active()) {
        if (const Enemy* t = LockedTarget(world)) {
            const SDL_FPoint a = Targeting::AimPoint(*t);
            FacePoint(a.x, a.y);
        }
    }
    sprite.facing = facing;

    // --- movement ------------------------------------------------------------
    float speed = move_speed * (1.0f + talents.Global("move_speed"));
    // A chill in the legs, a head still ringing, a charm's slow walk.
    speed *= StatusSpeed();
    if (hit_run_timer > 0.0f) speed *= 1.0f + talents.Effect("hit_run", AttackStyle::Ranged);
    if (Passive(PASSIVE_MARSHSTRIDE)) speed *= MARSHSTRIDE_SPEED;
    // Boots and charms: hide boots are a twentieth, an enchantment more.
    speed *= 1.0f + equipment.MoveSpeed();
    if (sprinting)            speed *= SPRINT_MULT;
    if (attack.Active())      speed *= attack.profile.move_scale;
    else if (charging)        speed *= 0.42f;      // charging slows you to a walk
    else if (blocking)        speed *= BLOCK_MOVE_SCALE;

    float dx = move.x * speed * dt;
    float dy = move.y * speed * dt;
    // The leap carries the character its whole distance through the flight,
    // and stops them where they land.
    if (rushing) {
        const float flight = attack.profile.windup + attack.profile.active;
        if (attack.timer < flight) {
            dx += rush_dx * (RUSH_DISTANCE / flight) * dt;
            dy += rush_dy * (RUSH_DISTANCE / flight) * dt;
        }
    }

    // Knockback rides on top of steering and decays quickly.
    dx += knock_x * dt;
    dy += knock_y * dt;
    const float decay = std::max(0.0f, 1.0f - KNOCK_DECAY * dt);
    knock_x *= decay;
    knock_y *= decay;

    const SDL_FRect box = Bounds();
    const SDL_FPoint resolved = world.map.MoveWithCollision(box, dx, dy);
    const float moved = Length(resolved.x - foot_box.x - x, resolved.y - foot_box.y - y);
    x = resolved.x - foot_box.x;
    y = resolved.y - foot_box.y;

    // Footsteps: by distance actually covered, so pushing into a wall is
    // silent. Plank floors indoors, stone in the mines, earth outside.
    if (Length(move.x, move.y) > 0.05f && moved > 0.0f) {
        stride += moved;
        // A sprint covers more ground per step, and each one kicks up dust.
        const float STRIDE = sprinting ? 30.0f : 21.0f;
        if (stride >= STRIDE) {
            stride -= STRIDE;
            if (sprinting && !world.map.IsInterior()) world.AddDust(x, y, move.x, move.y);
            const Map& m = world.map;
            const Sfx step = (m.Ambient() == "dungeon") ? Sfx::FootstepStone
                           : m.IsInterior()             ? Sfx::FootstepWood
                                                        : Sfx::Footstep;
            Audio::Play(step, sprinting ? 1.0f : 0.9f, sprinting ? 1.08f : 1.0f);
        }
    } else {
        stride = 14.0f;     // the first step after standing comes quickly
    }

    // --- the meal ----------------------------------------------------------------
    if (meal_left > 0.0f) {
        meal_left = std::max(0.0f, meal_left - dt);
        if (meal_left <= 0.0f) meal = nullptr;
        // Whatever it lifts, it lifts for as long as it lasts: the decay below
        // walks a potion's boost back down a point at a time, and a meal is
        // put back up under it. When the meal goes, the decay finds it and
        // takes it down for good.
        HoldMeal();
        SyncHitpoints();
        SyncMana();
    }

    // --- boosts wearing off -----------------------------------------------------
    // Hitpoints is its own thing -- it drains with damage -- so only the
    // levels a potion can lift drift back toward the real level.
    eat_cooldown = std::max(0.0f, eat_cooldown - dt);

    boost_timer += dt;
    if (boost_timer >= BOOST_DECAY) {
        boost_timer -= BOOST_DECAY;
        for (int s = 0; s < SKILL_COUNT; ++s) {
            if (s == SKILL_HITPOINTS) continue;
            const int level = skills.Level(s), now = skills.Current(s);
            if (now > level) skills.SetCurrent(s, now - 1);
            else if (now < level) skills.SetCurrent(s, now + 1);
        }
    }

    // --- mana ----------------------------------------------------------------
    SyncMana();
    if (mana < max_mana) {
        mana_fraction += SpellBook::RegenPerSecond(skills.Level(SKILL_MAGIC)) *
                         (1.0f + talents.Global("mana_regen")) * dt;
        const int whole = static_cast<int>(mana_fraction);
        if (whole > 0) {
            mana_fraction -= whole;
            mana = std::min(max_mana, mana + whole);
        }
    } else {
        mana_fraction = 0.0f;
    }

    UpdateAnimation(move);
    sprite.style = BuildLayerStyle(item_db);
    sprite.Update(dt);
    skills.SetCurrent(SKILL_HITPOINTS, hp);
}

void Player::Render(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    SDL_Color tint{255, 255, 255, 255};
    // Struck: a flash, a red one -- white would say they had struck something.
    // Drawn by the sprite shader when it can, else as the red tint it was.
    Shaders::SpriteFx fx;
    const bool flash = hurt_flash > 0.0f && Shaders::Effects() && Shaders::GetOptions().flashes;
    if (flash) fx.flash = {1.0f, 0.42f, 0.36f, std::clamp(hurt_flash / 0.1f, 0.0f, 1.0f) * 0.7f};
    else if (hurt_flash > 0.0f) tint = {255, 110, 110, 255};
    if (charging && !flash) {
        // Warm glow that builds with the charge, so the wind-up reads.
        const float t = ChargeRatio(charge_held);
        tint = {255,
                static_cast<Uint8>(255 - 90 * t),
                static_cast<Uint8>(255 - 150 * t), 255};
        // And a glow round them as it fills, with the shader.
        if (Shaders::Effects() && t > 0.05f) fx.glow = {1.0f, 0.78f, 0.36f, 0.25f + 0.45f * t};
    }
    // What a monster has left on them shows on them, as it does on a monster:
    // drawn by the sprite shader, or as a pull of their colours toward the
    // status's without it.
    if (statuses.Any() && !dead) {
        if (Shaders::Effects()) {
            fx.seed = 7.0f + static_cast<float>(seat);
            fx.burn = statuses.Has(Status::Burn) ? 1.0f : 0.0f;
            fx.cold = statuses.Has(Status::Frozen) ? 1.0f : statuses.Has(Status::Chill) ? 0.5f : 0.0f;
            fx.electrified = statuses.Has(Status::Electrified) ? 1.0f : 0.0f;
            fx.poison = statuses.Has(Status::Poison) ? 1.0f : 0.0f;
            fx.wet = statuses.Has(Status::Wet) ? 1.0f : 0.0f;
            fx.bleed = statuses.Has(Status::Bleed) ? 1.0f : 0.0f;
        } else {
            const auto toward = [&](SDL_Color c, float k) {
                tint = {static_cast<Uint8>(tint.r + (c.r - tint.r) * k), static_cast<Uint8>(tint.g + (c.g - tint.g) * k),
                        static_cast<Uint8>(tint.b + (c.b - tint.b) * k), tint.a};
            };
            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            if (statuses.Has(Status::Wet))    toward({150, 190, 255, 255}, 0.35f);
            if (statuses.Has(Status::Poison)) toward({150, 230, 120, 255}, 0.45f);
            if (statuses.Has(Status::Burn))   toward({255, 150, 80, 255}, 0.30f + 0.20f * sinf(t * 14.0f));
            if (statuses.Has(Status::Chill))  toward({170, 215, 255, 255}, 0.50f);
            if (statuses.Has(Status::Frozen)) toward({190, 232, 255, 255}, 0.85f);
        }
        // Beguiled, they are a little flushed.
        if (Charmed()) tint = {tint.r, static_cast<Uint8>(tint.g * 0.84f), static_cast<Uint8>(tint.b * 0.92f), tint.a};
    }
    sprite.Draw(r, cache, cam, x, y - draw_lift, tint, SDL_BLENDMODE_BLEND, 1.0f, fx.Any() ? &fx : nullptr);
    if (!dead && (Charmed() || Confused())) DrawDazes(r, cam);
}

void Player::DrawDazes(SDL_Renderer* r, const Camera& cam) const {
    // Over their head, in whole art pixels, drawn the plain way so it reads
    // with the effects off as well as on: hearts rising off someone charmed,
    // and stars going round the head of someone who does not know which way
    // is which.
    const float z = cam.zoom;
    const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const float head = y - draw_lift + body_box.y - 4.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const auto pixel = [&](float wx, float wy, int px, int py) {
        const SDL_FPoint at = cam.ToScreen(wx, wy);
        const SDL_FRect d = {roundf(at.x / z) * z + px * z, roundf(at.y / z) * z + py * z, z, z};
        SDL_RenderFillRect(r, &d);
    };
    if (Charmed()) {
        // .X.X.
        // XXXXX
        // .XXX.
        // ..X..
        static const char* kHeart[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
        for (int k = 0; k < 3; ++k) {
            const float t = fmodf(now * 0.8f + k / 3.0f, 1.0f);
            const float hx = x + (k == 0 ? -9.0f : k == 1 ? 7.0f : -1.0f) + sinf(now * 3.0f + k * 2.0f) * 2.0f;
            const float hy = head - 2.0f - t * 16.0f;
            const Uint8 a = static_cast<Uint8>(255.0f * std::clamp((1.0f - t) * 1.6f, 0.0f, 1.0f));
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 5; ++col) {
                    if (kHeart[row][col] != 'X') continue;
                    // A lighter pixel where the light catches it.
                    if (row == 1 && col == 1) SDL_SetRenderDrawColor(r, 255, 214, 236, a);
                    else                      SDL_SetRenderDrawColor(r, 240, 96, 170, a);
                    pixel(hx, hy, col - 2, row - 2);
                }
        }
    }
    if (Confused()) {
        // Three little stars going round, the far side of the circle dimmer.
        for (int k = 0; k < 3; ++k) {
            const float a = now * 4.2f + k * 2.0943951f;
            const float sx = x + cosf(a) * 11.0f, sy = head - 2.0f + sinf(a) * 3.0f;
            const Uint8 alpha = static_cast<Uint8>(sinf(a) > 0.0f ? 255 : 170);
            SDL_SetRenderDrawColor(r, 255, 236, 120, alpha);
            pixel(sx, sy, 0, -1); pixel(sx, sy, -1, 0); pixel(sx, sy, 1, 0); pixel(sx, sy, 0, 1);
            SDL_SetRenderDrawColor(r, 255, 255, 236, alpha);
            pixel(sx, sy, 0, 0);
        }
    }
}

void Player::Respawn(float sx, float sy) {
    dead = false;
    death_timer = 0.0f;
    x = sx;
    y = sy;
    knock_x = knock_y = 0.0f;
    attack.Clear();
    attack_cooldown = 0.0f;
    sprite.speed_scale = 1.0f;
    charging = strong_armed = false;
    jumping = false;
    sprinting = winded = false;
    statuses.Clear();
    stamina = MaxStamina();
    stamina_delay = 0.0f;
    // What was stored is lost with the fight it was stored for. Mana and
    // stamina are pools and come back full; the battery is not a pool.
    battery = 0.0f;
    climb_hint.clear();
    skills.ResetCurrent();
    SyncHitpoints();
    hp = max_hp;
    SyncMana();
    RestoreMana();
    sprite.Play("idle", true);
}

void Player::Rest() {
    if (dead) return;
    statuses.Clear();
    SyncHitpoints();
    hp = max_hp;
    skills.SetCurrent(SKILL_HITPOINTS, hp);
    heard_hp = hp;
    SyncMana();
    RestoreMana();
    stamina = MaxStamina();
    stamina_delay = 0.0f;
    winded = false;
}

int Player::BagSlots() const {
    int slots = INVENTORY_SLOTS;
    for (const string& id : bags) {
        const ItemDef* def = item_db ? item_db->Get(id) : nullptr;
        slots += def && def->bag_slots > 0 ? def->bag_slots : BAG_ROW;
    }
    return std::min(slots, MAX_INVENTORY_SLOTS);
}

void Player::SizeBag() {
    // A bag is never taken off, so in play this only grows. It shrinks when a
    // different character is read into the same Player, and what is in the bag
    // then is about to be replaced by theirs.
    inventory.Resize(BagSlots());
}

bool Player::WearBag(int slot, string& why_not) {
    why_not.clear();
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;
    const string id = inventory.Slot(slot).id;
    const ItemDef* def = id.empty() ? nullptr : item_db->Get(id);
    if (!def || def->use != "bag") { why_not = "That is not something to carry things in."; return false; }
    if (std::find(bags.begin(), bags.end(), id) != bags.end()) {
        why_not = "You already carry a " + def->name + ". A second one is only worth what it sells for.";
        return false;
    }
    if (BagSlots() >= MAX_INVENTORY_SLOTS) { why_not = "You could not carry any more if you had it."; return false; }
    inventory.RemoveSlot(slot, 1);
    bags.push_back(id);
    SizeBag();
    return true;
}

void Player::HoldMeal() {
    if (!meal) return;
    for (const auto& b : meal->dish_levels)
        skills.SetCurrent(b.first, std::max(skills.Current(b.first), skills.Level(b.first) + b.second));
}

bool Player::Eat(int slot) {
    string why;
    return Consume(slot, why);
}

bool Player::Consume(int slot, string& why_not) {
    why_not.clear();
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;
    const ItemStack& s = inventory.Slot(slot);
    if (s.Empty()) return false;

    const ItemDef* def = item_db->Get(s.id);
    if (!def || !def->consumable) { why_not = "You cannot eat that."; return false; }
    if (def->heal > 0 && eat_cooldown > 0.0f) {
        why_not = "You are still getting the last mouthful down.";
        return false;
    }

    // Only worth using if something would change: food at full health is
    // refused, but a potion that also boosts or restores is not.
    bool helps = (def->heal > 0 && hp < max_hp) || (def->mana > 0 && mana < max_mana) ||
                 (def->stamina && stamina < MaxStamina());
    // A dinner is worth eating on a full stomach: it is not the healing in it.
    if (def->IsDish() && def != meal) helps = true;
    for (const auto& b : def->boosts) {
        const int level = skills.Level(b.first);
        const int target = level + ItemDef::BoostGain(b.second, level);
        if (skills.Current(b.first) < target) helps = true;
    }
    if (!helps) {
        why_not = def->boosts.empty() && def->mana == 0 ? "You are already at full health."
                                                        : "It would do nothing for you right now.";
        return false;
    }

    // A dish sits with you: what it lifts, it lifts for its own few minutes,
    // and a second dish is the one you are on rather than both at once.
    if (def->IsDish()) {
        meal = def;
        meal_left = def->dish_minutes * 60.0f;
        HoldMeal();
        SyncHitpoints();
        SyncMana();
    }
    if (def->heal > 0) { Heal(def->heal); eat_cooldown = EAT_COOLDOWN; }
    if (def->mana > 0) { SyncMana(); mana = std::min(max_mana, mana + def->mana); }
    if (def->stamina) { stamina = MaxStamina(); stamina_delay = 0.0f; winded = false; }
    for (const auto& b : def->boosts) {
        const int level = skills.Level(b.first);
        const int target = level + ItemDef::BoostGain(b.second, level);
        // A boost never stacks past its own ceiling, and never lowers one.
        skills.SetCurrent(b.first, std::max(skills.Current(b.first), target));
    }
    if (!def->boosts.empty()) boost_timer = 0.0f;
    skills.SetCurrent(SKILL_HITPOINTS, hp);
    inventory.RemoveSlot(slot, 1);
    return true;
}

bool Player::EquipFromInventory(int slot, string& why_not) {
    why_not.clear();
    if (!item_db || slot < 0 || slot >= inventory.SlotCount()) return false;

    const ItemStack& stack = inventory.Slot(slot);
    if (stack.Empty()) return false;

    const ItemDef* def = item_db->Get(stack.id);
    if (!def || def->slot == SLOT_NONE) {
        why_not = "You cannot wear that.";
        return false;
    }

    for (const auto& req : def->requirements) {
        if (skills.Level(req.first) < req.second) {
            why_not = "Needs " + std::to_string(req.second) + " " + SkillName(req.first) + ".";
            return false;
        }
    }

    // A bow takes both hands. Equipping one takes the shield off, and a
    // shield takes the bow off -- but only if the bag has room for what comes
    // off, or the item would be lost.
    //
    // A dagger is the one weapon the other hand will hold. With a dagger already
    // in the right and no dagger in the left, the next one equipped goes to the
    // left, in place of whatever was there; with a pair already, it replaces the
    // right like any weapon. And a dagger in the left stays only while there is
    // one in the right: any other weapon coming in sends it back to the bag.
    int unseat = SLOT_NONE;
    int into = def->slot;
    if (item_db && (def->slot == SLOT_WEAPON || def->slot == SLOT_SHIELD)) {
        const ItemDef* worn_weapon = item_db->Get(equipment.InSlot(SLOT_WEAPON));
        const ItemDef* worn_off    = item_db->Get(equipment.InSlot(SLOT_SHIELD));
        const bool off_is_weapon   = worn_off && worn_off->slot == SLOT_WEAPON;
        if (def->slot == SLOT_WEAPON && def->offhand && worn_weapon && worn_weapon->offhand && !off_is_weapon)
            into = SLOT_SHIELD;
        const bool bow_in   = def->slot == SLOT_WEAPON && def->two_handed;
        const bool shield_in = def->slot == SLOT_SHIELD;
        const bool lone_in  = into == SLOT_WEAPON && !def->offhand && off_is_weapon;
        const int  clash = ((bow_in || lone_in) && !equipment.InSlot(SLOT_SHIELD).empty()) ? SLOT_SHIELD
                         : (shield_in && worn_weapon && worn_weapon->two_handed)
                               ? SLOT_WEAPON : SLOT_NONE;
        if (clash != SLOT_NONE) {
            const int freed = (stack.qty == 1) ? 1 : 0;
            const int needed = 1 + (equipment.InSlot(into).empty() ? 0 : 1);
            if (inventory.FreeSlots() + freed < needed) {
                why_not = clash != SLOT_SHIELD ? "A shield needs a free hand, and your pack has no room for what you are holding."
                        : off_is_weapon        ? "Your pack has no room for the dagger in your other hand."
                                               : "That needs both hands, and your pack has no room for the shield.";
                return false;
            }
            unseat = clash;
        }
    }

    const string item_id = stack.id;
    inventory.RemoveSlot(slot, 1);
    const string displaced = equipment.Equip(into, item_id);
    // The freed slot guarantees room for whatever came off.
    if (!displaced.empty()) inventory.Add(displaced, 1);
    // After the item has left the bag, so its slot counts toward the room.
    if (unseat != SLOT_NONE) inventory.Add(equipment.Unequip(unseat), 1);
    return true;
}

bool Player::UnequipSlot(int equip_slot) {
    const string id = equipment.InSlot(equip_slot);
    if (id.empty()) return false;
    if (inventory.Full()) return false;
    // Putting away the right hand's dagger of a pair: the left's changes hands,
    // rather than being left in a hand that holds nothing without the other.
    const bool pair = equip_slot == SLOT_WEAPON && equipment.DualWielding();
    equipment.Unequip(equip_slot);
    if (pair) equipment.Equip(SLOT_WEAPON, equipment.Unequip(SLOT_SHIELD));
    inventory.Add(id, 1);
    return true;
}

vector<string> Player::QuickChoices() const {
    vector<string> out;
    if (!item_db) return out;
    for (int i = 0; i < inventory.SlotCount(); ++i) {
        const ItemStack& st = inventory.Slot(i);
        if (st.Empty()) continue;
        const ItemDef* d = item_db->Get(st.id);
        if (!d || !d->consumable) continue;
        if (std::find(out.begin(), out.end(), st.id) == out.end()) out.push_back(st.id);
    }
    return out;
}

string Player::CycleQuickItem() {
    const vector<string> choices = QuickChoices();
    if (choices.empty()) return string();
    const auto at = std::find(choices.begin(), choices.end(), quick_item);
    quick_item = (at == choices.end() || at + 1 == choices.end()) ? choices.front() : *(at + 1);
    return quick_item;
}

bool Player::UseQuickItem(string& why_not) {
    why_not.clear();
    // Nothing chosen yet: the first thing in the pack that can be eaten.
    if (quick_item.empty() || !inventory.Has(quick_item, 1)) {
        const vector<string> choices = QuickChoices();
        if (quick_item.empty() && !choices.empty()) quick_item = choices.front();
        if (quick_item.empty() || !inventory.Has(quick_item, 1)) {
            const ItemDef* d = item_db ? item_db->Get(quick_item) : nullptr;
            why_not = quick_item.empty() ? string("You have nothing to eat or drink.")
                                         : "You have no " + (d ? d->name : quick_item) + " left.";
            return false;
        }
    }
    for (int i = 0; i < inventory.SlotCount(); ++i)
        if (inventory.Slot(i).id == quick_item) return Consume(i, why_not);
    return false;
}

json Player::ToJson() const {
    return json{
        {"sprite",    sprite_id},
        {"x",         x},
        {"y",         y},
        {"facing",    static_cast<int>(facing)},
        {"hp",        hp},
        {"mana",      mana},
        {"element",   ElementName(selected_element)},
        {"arcane_spell", arcane_spell},
        {"held_spells", HeldToJson(held_spell)},
        {"electric_spell", electric_spell},
        {"battery", battery},
        {"spell_slot", spell_slot},
        {"quick_item", quick_item},
        {"skills",    skills.ToJson()},
        {"bags",      bags},
        {"inventory", inventory.ToJson()},
        {"equipment", equipment.ToJson()},
        {"talents",   talents.ToJson()},
    };
}

void Player::ApplySheet(const json& j, const GameContext& ctx) {
    item_db = ctx.items;
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
    talents.SetDatabase(ctx.trees);
    talents.SetPath(AffinityFor(sprite_id));
    talents.FromJson(j.value("talents", json::object()));
    if (j.contains("skills"))    skills.FromJson(j["skills"]);
    // The bags first: they say how big a bag there is to put things back into.
    bags.clear();
    if (j.contains("bags") && j["bags"].is_array())
        for (const auto& b : j["bags"])
            if (b.is_string() && std::find(bags.begin(), bags.end(), b.get<string>()) == bags.end())
                bags.push_back(b.get<string>());
    SizeBag();
    if (j.contains("inventory")) inventory.FromJson(j["inventory"]);
    if (j.contains("equipment")) equipment.FromJson(j["equipment"]);
    const int was = hp;
    SyncHitpoints();
    hp = std::clamp(was, 0, max_hp);
    skills.SetCurrent(SKILL_HITPOINTS, hp);
    SyncMana();
    arcane_spell = j.value("arcane_spell", string(""));
    HeldFromJson(j.value("held_spells", json::object()), held_spell);
    electric_spell = j.value("electric_spell", string(""));
    battery = std::clamp(j.value("battery", 0.0f), 0.0f, 1.0f);
    spell_slot = std::clamp(j.value("spell_slot", 0), 0, 3);
    quick_item   = j.value("quick_item", quick_item);
    selected_element = ElementFromName(j.value("element", string("fire")));
    if (selected_element == Element::None) selected_element = Element::Fire;
    if (selected_element == Element::Arcane && arcane_spell.empty()) selected_element = Element::Fire;
}

void Player::FromJson(const json& j, const GameContext& ctx) {
    sprite_id = j.value("sprite", string(kDefaultCharacter));
    item_db = ctx.items;
    if (ctx.sprites) sprite.SetDef(ctx.sprites->Get(sprite_id));
    inventory.SetDatabase(ctx.items);
    equipment.SetDatabase(ctx.items);
    talents.SetDatabase(ctx.trees);
    // One path, one tree: a save from before that was so loses what it had
    // bought in the other two.
    talents.SetPath(AffinityFor(sprite_id));
    talents.FromJson(j.value("talents", json::object()));

    x = j.value("x", 0.0f);
    y = j.value("y", 0.0f);
    facing = static_cast<Facing>(j.value("facing", 0));

    if (j.contains("skills"))    skills.FromJson(j["skills"]);
    // The bags first: they say how big a bag there is to put things back into.
    bags.clear();
    if (j.contains("bags") && j["bags"].is_array())
        for (const auto& b : j["bags"])
            if (b.is_string() && std::find(bags.begin(), bags.end(), b.get<string>()) == bags.end())
                bags.push_back(b.get<string>());
    SizeBag();
    if (j.contains("inventory")) inventory.FromJson(j["inventory"]);
    if (j.contains("equipment")) equipment.FromJson(j["equipment"]);

    SyncHitpoints();
    hp = std::clamp(j.value("hp", max_hp), 1, max_hp);
    SyncMana();
    mana = std::clamp(j.value("mana", max_mana), 0, max_mana);
    arcane_spell = j.value("arcane_spell", string(""));
    HeldFromJson(j.value("held_spells", json::object()), held_spell);
    electric_spell = j.value("electric_spell", string(""));
    battery = std::clamp(j.value("battery", 0.0f), 0.0f, 1.0f);
    spell_slot = std::clamp(j.value("spell_slot", 0), 0, 3);
    quick_item   = j.value("quick_item", quick_item);
    selected_element = ElementFromName(j.value("element", string("fire")));
    if (selected_element == Element::None) selected_element = Element::Fire;
    if (selected_element == Element::Arcane && arcane_spell.empty()) selected_element = Element::Fire;
    dead = false;
    death_timer = 0.0f;
    sprite.facing = facing;
    sprite.Play("idle", true);
}
