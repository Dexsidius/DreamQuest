#include "projectile.h"
#include <fstream>

static const char* kElementNames[] = {"none", "fire", "water", "earth", "air"};

const char* ElementName(Element e) {
    const int i = static_cast<int>(e);
    if (i < 0 || i >= static_cast<int>(Element::COUNT)) return "none";
    return kElementNames[i];
}

Element ElementFromName(const string& name) {
    for (int i = 0; i < static_cast<int>(Element::COUNT); ++i) {
        const string a = kElementNames[i];
        if (a.size() != name.size()) continue;
        bool same = true;
        for (size_t c = 0; c < a.size(); ++c)
            if (tolower(a[c]) != tolower(name[c])) { same = false; break; }
        if (same) return static_cast<Element>(i);
    }
    return Element::None;
}

SDL_Color ElementColor(Element e) {
    switch (e) {
        case Element::Fire:  return {255, 138,  62, 255};
        case Element::Water: return { 96, 172, 235, 255};
        case Element::Earth: return {186, 146,  86, 255};
        case Element::Air:   return {198, 226, 235, 255};
        default:             return {235, 235, 235, 255};
    }
}

Element ElementBeats(Element e) {
    switch (e) {
        case Element::Water: return Element::Fire;
        case Element::Fire:  return Element::Earth;
        case Element::Earth: return Element::Air;
        case Element::Air:   return Element::Water;
        default:             return Element::None;
    }
}

float ElementMultiplier(Element attacker, Element defender) {
    if (attacker == Element::None || defender == Element::None) return 1.0f;
    if (attacker == defender)                                   return 0.75f;
    if (ElementBeats(attacker) == defender)                     return 1.60f;
    if (ElementBeats(defender) == attacker)                     return 0.60f;
    return 1.0f;
}

static SDL_Color ColorFromJson(const json& j, SDL_Color fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {static_cast<Uint8>(j[0].get<int>()),
            static_cast<Uint8>(j[1].get<int>()),
            static_cast<Uint8>(j[2].get<int>()),
            static_cast<Uint8>(j.size() > 3 ? j[3].get<int>() : 255)};
}

bool ProjectileDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("ProjectileDatabase: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("ProjectileDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const json& o = it.value();
        ProjectileDef d;
        d.id     = it.key();
        d.sprite = o.value("sprite", string(""));
        d.speed  = o.value("speed", 260.0f);
        d.life   = o.value("life", 1.6f);
        d.radius = o.value("radius", 6.0f);
        d.scale  = o.value("scale", 1.0f);
        d.sprite_angle = o.value("sprite_angle", 0.0f);
        d.spin   = o.value("spin", false);
        d.pierce = o.value("pierce", 0);
        d.knockback = o.value("knockback", 40.0f);
        d.element = ElementFromName(o.value("element", string("none")));
        d.tint = ColorFromJson(o.contains("tint") ? o["tint"] : json(),
                               ElementColor(d.element));

        if (o.contains("patch")) {
            const json& p = o["patch"];
            d.patch_time   = p.value("time", 0.0f);
            d.patch_radius = p.value("radius", 22.0f);
            d.patch_damage = p.value("damage", 1);
            d.patch_tick   = p.value("tick", 0.5f);
        }
        if (o.contains("erupt")) {
            const json& e = o["erupt"];
            d.erupts       = true;
            d.erupt_radius = e.value("radius", 34.0f);
            d.erupt_delay  = e.value("delay", 0.35f);
            d.erupt_damage = e.value("damage", 2);
        }

        defs[d.id] = d;
    }

    SDL_Log("ProjectileDatabase: loaded %d projectile types", static_cast<int>(defs.size()));
    return true;
}

const ProjectileDef* ProjectileDatabase::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}
