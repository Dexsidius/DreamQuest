#include "spell.h"
#include <fstream>

bool SpellBook::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("SpellBook: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("SpellBook: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        SpellDef d;
        d.id          = it.key();
        d.name        = o.value("name", d.id);
        d.description = o.value("desc", string(""));
        d.element     = ElementFromName(o.value("element", string("fire")));
        d.tier        = o.value("tier", 1);
        d.level       = o.value("level", 1);
        d.mana        = o.value("mana", 4);
        d.damage_mult = o.value("damage", 1.0f);
        d.xp          = o.value("xp", 10);
        d.projectile  = o.value("projectile", string(""));
        d.arcane      = o.value("school", string("elemental")) == "arcane";
        d.shape       = o.value("shape", string("bolt"));
        d.slot        = std::clamp(o.value("slot", 1), 1, MAX_SPELL_SLOT);
        d.taught_by   = o.value("taught_by", string(""));
        d.battery_gain  = o.value("battery_gain", 0.0f);
        d.battery_cost  = o.value("battery_cost", 0.0f);
        d.battery_heavy = o.value("battery_heavy", d.battery_cost);
        d.battery_needs = o.value("battery_needs", 0.0f);
        if (d.arcane) d.element = Element::Arcane;
        defs[d.id] = d;
    }

    SDL_Log("SpellBook: loaded %d spells", static_cast<int>(defs.size()));
    return true;
}

vector<const SpellDef*> SpellBook::Electric(int magic_level) const {
    vector<const SpellDef*> out;
    for (const auto& kv : defs)
        if (kv.second.element == Element::Electric && !kv.second.arcane && kv.second.level <= magic_level)
            out.push_back(&kv.second);
    std::sort(out.begin(), out.end(), [](const SpellDef* a, const SpellDef* b) {
        if (a->level != b->level) return a->level < b->level;
        return a->slot < b->slot;
    });
    return out;
}

const SpellDef* SpellBook::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

const SpellDef* SpellBook::BestFor(Element e, int magic_level) const {
    const SpellDef* best = nullptr;
    for (const auto& kv : defs) {
        const SpellDef& s = kv.second;
        if (s.element != e || s.slot != 1 || s.level > magic_level) continue;
        if (!best || s.tier > best->tier) best = &s;
    }
    return best;
}

const SpellDef* SpellBook::Chosen(Element e, int magic_level, const string& held) const {
    const SpellDef* s = held.empty() ? nullptr : Get(held);
    if (s && !s->arcane && s->element == e && s->level <= magic_level) return s;
    return BestFor(e, magic_level);
}

const SpellDef* SpellBook::ForSlot(Element e, int slot, int magic_level) const {
    const SpellDef* best = nullptr;
    for (const auto& kv : defs) {
        const SpellDef& sp = kv.second;
        if (sp.arcane || sp.element != e || sp.slot != slot || sp.level > magic_level) continue;
        if (!best || sp.tier > best->tier) best = &sp;
    }
    return best;
}

const SpellDef* SpellBook::FirstOnSlot(Element e, int slot) const {
    const SpellDef* first = nullptr;
    for (const auto& kv : defs) {
        const SpellDef& sp = kv.second;
        if (sp.arcane || sp.element != e || sp.slot != slot) continue;
        if (!first || sp.level < first->level) first = &sp;
    }
    return first;
}

vector<const SpellDef*> SpellBook::ForWeapon(Element e, const vector<int>& slots, int magic_level) const {
    vector<const SpellDef*> out;
    // Slot one is every element's bolt, and a weapon that reaches it offers all
    // of them -- an Ember is worth having beside a Pyre, being cheaper.
    for (int slot : slots) {
        if (slot == 1) {
            for (const SpellDef* s : Of(e))
                if (s->level <= magic_level) out.push_back(s);
        } else if (const SpellDef* s = ForSlot(e, slot, magic_level)) {
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const SpellDef* a, const SpellDef* b) {
        if (a->slot != b->slot) return a->slot < b->slot;
        if (a->tier != b->tier) return a->tier < b->tier;
        return a->level < b->level;
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

const SpellDef* SpellBook::ChosenFor(Element e, const vector<int>& slots, int magic_level,
                                     const string& held) const {
    if (!held.empty()) {
        if (const SpellDef* s = Get(held)) {
            if (!s->arcane && s->element == e && s->level <= magic_level &&
                std::find(slots.begin(), slots.end(), s->slot) != slots.end())
                return s;
        }
    }
    return BestFor(e, magic_level);
}

vector<const SpellDef*> SpellBook::Of(Element e) const {
    vector<const SpellDef*> out;
    for (const auto& kv : defs)
        if (!kv.second.arcane && kv.second.element == e && kv.second.slot == 1) out.push_back(&kv.second);
    std::sort(out.begin(), out.end(), [](const SpellDef* a, const SpellDef* b) {
        if (a->tier != b->tier) return a->tier < b->tier;
        return a->level < b->level;
    });
    return out;
}

vector<const SpellDef*> SpellBook::Arcane() const {
    vector<const SpellDef*> out;
    for (const auto& kv : defs) if (kv.second.arcane) out.push_back(&kv.second);
    std::sort(out.begin(), out.end(), [](const SpellDef* a, const SpellDef* b) {
        if (a->level != b->level) return a->level < b->level;
        return a->name < b->name;
    });
    return out;
}

const SpellDef* SpellBook::NextFor(Element e, int magic_level) const {
    const SpellDef* next = nullptr;
    for (const auto& kv : defs) {
        const SpellDef& s = kv.second;
        if (s.element != e || s.slot != 1 || s.level <= magic_level) continue;
        if (!next || s.level < next->level) next = &s;
    }
    return next;
}
