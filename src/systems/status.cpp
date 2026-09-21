#include "status.h"
#include <fstream>

static const char* kStatusIds[STATUS_COUNT] = {"burn", "wet", "concussed", "bleed", "poison", "chill", "frozen"};

const char* StatusId(Status s) {
    const int i = static_cast<int>(s);
    return i >= 0 && i < STATUS_COUNT ? kStatusIds[i] : "none";
}

Status StatusFromId(const string& id) {
    for (int i = 0; i < STATUS_COUNT; ++i)
        if (id == kStatusIds[i]) return static_cast<Status>(i);
    return Status::COUNT;
}

StatusProc StatusProcFromJson(const json& j) {
    StatusProc p;
    if (!j.is_object()) return p;
    p.kind = StatusFromId(j.value("id", string("")));
    p.chance = std::clamp(j.value("chance", 0.0f), 0.0f, 1.0f);
    if (p.kind == Status::COUNT) p.chance = 0.0f;
    return p;
}

bool StatusDatabase::Empty() const {
    for (bool l : loaded) if (l) return false;
    return true;
}

bool StatusDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("StatusDatabase: cannot open '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("StatusDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    const auto kinds = [](const json& o, const char* key) {
        vector<Status> out;
        if (o.contains(key) && o[key].is_array())
            for (const auto& v : o[key])
                if (v.is_string() && StatusFromId(v.get<string>()) != Status::COUNT)
                    out.push_back(StatusFromId(v.get<string>()));
        return out;
    };

    int count = 0;
    for (auto it = root.begin(); it != root.end(); ++it) {
        const Status kind = StatusFromId(it.key());
        if (kind == Status::COUNT) continue;           // "_comment", or a status from a later version
        const json& o = it.value();
        StatusDef d;
        d.kind      = kind;
        d.name      = o.value("name", string(it.key()));
        d.seconds   = o.value("seconds", 3.0f);
        d.dot_share = o.value("dot_share", 0.0f);
        d.dot_min   = o.value("dot_min", 0);
        d.stacks    = o.value("stacks", false);
        d.speed     = o.value("speed", 1.0f);
        d.attack    = o.value("attack", 1.0f);
        d.defence   = o.value("defence", 1.0f);
        d.cooldown  = o.value("cooldown", 1.0f);
        d.stagger   = o.value("stagger", 0.0f);
        d.holds     = o.value("holds", false);
        d.boss_share = o.value("boss_share", 0.5f);
        d.ends       = kinds(o, "ends");
        d.blocked_by = kinds(o, "blocked_by");
        d.if_has  = StatusFromId(o.value("if_has", string("")));
        d.becomes = StatusFromId(o.value("becomes", string("")));
        d.then    = StatusFromId(o.value("then", string("")));
        if (o.contains("weak_to") && o["weak_to"].is_array())
            for (const auto& v : o["weak_to"])
                if (v.is_string() && ElementFromName(v.get<string>()) != Element::None)
                    d.weak_to.push_back(ElementFromName(v.get<string>()));
        d.weak_mult = o.value("weak_mult", 1.0f);
        if (o.contains("color") && o["color"].is_array() && o["color"].size() >= 3)
            d.color = {static_cast<Uint8>(o["color"][0].get<int>()), static_cast<Uint8>(o["color"][1].get<int>()),
                       static_cast<Uint8>(o["color"][2].get<int>()), 255};
        defs[static_cast<int>(kind)] = d;
        loaded[static_cast<int>(kind)] = true;
        ++count;
    }
    SDL_Log("StatusDatabase: loaded %d statuses", count);
    return count > 0;
}
