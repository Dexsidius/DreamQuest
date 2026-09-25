#include "projectile.h"
#include <fstream>

static const char* kElementNames[] = {"none", "fire", "water", "earth", "air", "electric", "arcane"};

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
        case Element::Fire:   return {255, 138,  62, 255};
        case Element::Water:  return { 96, 172, 235, 255};
        case Element::Earth:  return {186, 146,  86, 255};
        case Element::Air:    return {198, 226, 235, 255};
        // Lightning, not the battery: the bar the charge fills is green, the
        // element is the colour of the arc itself.
        case Element::Electric: return {250, 232, 108, 255};
        case Element::Arcane: return {186, 140, 255, 255};
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
    // The ancient magic and the lightning stand outside the cycle.
    if (attacker == Element::Arcane || defender == Element::Arcane) return 1.0f;
    if (attacker == Element::Electric || defender == Element::Electric) return 1.0f;
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
        d.bounces        = o.value("bounces", 0);
        d.bounce_damping = o.value("bounce_damping", 0.25f);
        d.impact_size    = o.value("impact_size", 5.0f);
        d.knockback = o.value("knockback", 40.0f);
        d.power     = std::max(0.0f, o.value("power", 1.0f));
        d.homing    = o.value("homing", 0.0f);
        d.thrown    = o.value("thrown", false);
        d.element = ElementFromName(o.value("element", string("none")));
        d.tint = ColorFromJson(o.contains("tint") ? o["tint"] : json(),
                               ElementColor(d.element));

        d.frames  = std::max(1, o.value("frames", 1));
        d.fps     = o.value("fps", 12.0f);
        d.upright = o.value("upright", false);
        d.glow    = o.value("glow", 0.0f);
        const auto pivot = [](const json& from, float& x, float& y) {
            if (from.contains("pivot") && from["pivot"].is_array() && from["pivot"].size() >= 2) {
                x = from["pivot"][0].get<float>();
                y = from["pivot"][1].get<float>();
            }
        };
        pivot(o, d.pivot_x, d.pivot_y);
        if (o.contains("tail")) {
            const json& t = o["tail"];
            d.tail        = t.value("sprite", string(""));
            d.tail_frames = std::max(1, t.value("frames", 1));
            pivot(t, d.tail_pivot_x, d.tail_pivot_y);
        }
        const string trail = o.value("trail", string(""));
        d.shed = trail.empty() ? d.element : ElementFromName(trail);
        if (o.contains("trail_color")) d.shed_color = ColorFromJson(o["trail_color"], {0, 0, 0, 0});
        if (o.contains("status")) d.status = StatusProcFromJson(o["status"]);
        d.leech = std::clamp(o.value("leech", 0.0f), 0.0f, 1.0f);
        d.armour_pierce = std::clamp(o.value("armour_pierce", 0.0f), 0.0f, 0.9f);

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
