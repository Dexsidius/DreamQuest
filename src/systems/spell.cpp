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
        d.taught_by   = o.value("taught_by", string(""));
        if (d.arcane) d.element = Element::Arcane;
        defs[d.id] = d;
    }

    SDL_Log("SpellBook: loaded %d spells", static_cast<int>(defs.size()));
    return true;
}

const SpellDef* SpellBook::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

const SpellDef* SpellBook::BestFor(Element e, int magic_level) const {
    const SpellDef* best = nullptr;
    for (const auto& kv : defs) {
        const SpellDef& s = kv.second;
        if (s.element != e || s.level > magic_level) continue;
        if (!best || s.tier > best->tier) best = &s;
    }
    return best;
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
        if (s.element != e || s.level <= magic_level) continue;
        if (!next || s.level < next->level) next = &s;
    }
    return next;
}
