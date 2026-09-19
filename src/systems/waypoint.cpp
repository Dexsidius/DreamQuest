#include "waypoint.h"
#include <fstream>
#include "loot.h"
#include "items.h"
#include "../world/world.h"
#include "../entity/enemy.h"
#include "../entity/npc.h"

bool WaypointIndex::Load(const string& path) {
    areas.clear();
    std::ifstream in(path);
    if (!in) {
        SDL_Log("Waypoints: no %s; quests will not be pointed at", path.c_str());
        return false;
    }
    json root;
    try { in >> root; } catch (const std::exception& e) {
        SDL_Log("Waypoints: %s is malformed: %s", path.c_str(), e.what());
        return false;
    }
    if (!root.contains("maps") || !root["maps"].is_object()) return false;
    for (auto it = root["maps"].begin(); it != root["maps"].end(); ++it) {
        const json& j = it.value();
        Area a;
        a.name  = j.value("name", it.key());
        a.dream = j.value("dream", false);
        if (j.contains("exits"))
            for (const json& e : j["exits"])
                a.exits.push_back({e.value("x", 0.0f), e.value("y", 0.0f), e.value("to", string()), e.value("label", string())});
        if (j.contains("people"))
            for (const json& p : j["people"])
                a.people.push_back({p.value("id", string()), p.value("name", string()), p.value("x", 0.0f), p.value("y", 0.0f)});
        if (j.contains("things"))
            for (const json& t : j["things"])
                a.things.push_back({t.value("id", string()), t.value("kind", string()), t.value("yield", string()), t.value("title", string()),
                                    t.value("x", 0.0f), t.value("y", 0.0f)});
        if (j.contains("posts"))
            for (const json& p : j["posts"]) {
                Post post;
                post.x = p.value("x", 0.0f);
                post.y = p.value("y", 0.0f);
                if (p.contains("types")) for (const json& t : p["types"]) if (t.is_string()) post.types.push_back(t.get<string>());
                a.posts.push_back(std::move(post));
            }
        areas.emplace(it.key(), std::move(a));
    }
    return !areas.empty();
}

vector<string> WaypointIndex::Route(const string& from, const string& to) const {
    if (!areas.count(from) || !areas.count(to)) return {};
    if (from == to) return {from};
    std::map<string, string> came_from;
    vector<string> frontier{from};
    came_from[from] = "";
    while (!frontier.empty() && !came_from.count(to)) {
        vector<string> next;
        for (const string& at : frontier) {
            const auto area = areas.find(at);
            if (area == areas.end()) continue;
            for (const Exit& e : area->second.exits) {
                if (e.to.empty() || came_from.count(e.to) || !areas.count(e.to)) continue;
                came_from[e.to] = at;
                next.push_back(e.to);
            }
        }
        frontier = std::move(next);
    }
    if (!came_from.count(to)) return {};
    vector<string> route;
    for (string at = to; !at.empty(); at = came_from[at]) route.push_back(at);
    std::reverse(route.begin(), route.end());
    return route;
}

vector<WaypointIndex::Spot> WaypointIndex::SpotsFor(const QuestStage& stage, int holding,
                                                     const EnemyDatabase* enemies, const LootSystem* loot,
                                                     const ItemDatabase* items) const {
    vector<Spot> out;
    const auto person = [&](const string& id) {
        for (const auto& kv : areas)
            for (const Person& p : kv.second.people)
                if (p.id == id) out.push_back({kv.first, p.x, p.y, p.name});
    };
    // Wherever a thing can be got: what yields it when worked, and what drops it.
    const auto sources = [&](const string& item) {
        // A fish comes out of any water worth casting at.
        const ItemDef* def = items ? items->Get(item) : nullptr;
        const bool fish = def && def->fish_level > 0;
        for (const auto& kv : areas) {
            for (const Thing& t : kv.second.things)
                if (t.yield == item || (fish && t.kind == "fishing_spot"))
                    out.push_back({kv.first, t.x, t.y, fish ? string("somewhere to fish") : t.title});
            if (!enemies || !loot) continue;
            for (const Post& post : kv.second.posts)
                for (const string& type : post.types) {
                    const EnemyDef* def = enemies->Get(type);
                    if (def && loot->ChanceOf(def->loot_table, item) > 0.0f) { out.push_back({kv.first, post.x, post.y, def->name}); break; }
                }
        }
    };

    switch (stage.type) {
        case ObjectiveType::Talk:
            person(stage.target);
            break;
        case ObjectiveType::Deliver:
            // With it in the bag, whoever wants it. Without, wherever it comes
            // from -- and if nowhere on any map makes it, back to who wants it,
            // who is at least the right person to ask.
            if (holding < stage.count) sources(stage.target);
            if (out.empty()) person(stage.deliver_to);
            break;
        case ObjectiveType::Collect:
            sources(stage.target);
            break;
        case ObjectiveType::Kill:
            for (const auto& kv : areas) {
                if (!stage.map_id.empty() && kv.first != stage.map_id) continue;
                for (const Post& post : kv.second.posts)
                    for (const string& type : post.types) {
                        const EnemyDef* def = enemies ? enemies->Get(type) : nullptr;
                        if (type == stage.target || (def && def->kill_target == stage.target)) {
                            out.push_back({kv.first, post.x, post.y, def ? def->name : type});
                            break;
                        }
                    }
            }
            break;
        case ObjectiveType::Interact:
            for (const auto& kv : areas)
                for (const Thing& t : kv.second.things)
                    if (t.id == stage.target) out.push_back({kv.first, t.x, t.y, t.title});
            break;
        case ObjectiveType::Reach:
            break;      // a place is not a spot: Resolve takes the road to it
    }
    return out;
}

Waypoint WaypointIndex::Resolve(const QuestLog& log, const string& quest_id, const World& world,
                                const EnemyDatabase* enemies, const LootSystem* loot, const ItemDatabase* items) const {
    Waypoint w;
    w.quest = quest_id;
    const QuestDef* def = log.Definition(quest_id);
    if (!def || !log.IsActive(quest_id) || areas.empty()) return w;
    const int at_stage = log.Stage(quest_id);
    if (at_stage < 0 || at_stage >= static_cast<int>(def->stages.size())) return w;
    const QuestStage& stage = def->stages[static_cast<size_t>(at_stage)];

    const string here = world.MapId();
    const auto here_area = areas.find(here);
    if (here_area == areas.end()) return w;
    const float px = world.player.x, py = world.player.y;

    // How many doors from here to everywhere, and which map the first of them opens on.
    std::map<string, int> doors;
    std::map<string, string> first_step;
    {
        vector<string> frontier{here};
        doors[here] = 0;
        while (!frontier.empty()) {
            vector<string> next;
            for (const string& at : frontier) {
                const auto area = areas.find(at);
                if (area == areas.end()) continue;
                for (const Exit& e : area->second.exits) {
                    if (e.to.empty() || doors.count(e.to) || !areas.count(e.to)) continue;
                    doors[e.to] = doors[at] + 1;
                    first_step[e.to] = at == here ? e.to : first_step[at];
                    next.push_back(e.to);
                }
            }
            frontier = std::move(next);
        }
    }
    // The way out of here that starts towards a map, the nearest if there are two.
    const auto way_out = [&](const string& towards, float& ox, float& oy, string& label) {
        const auto step = first_step.find(towards);
        if (step == first_step.end()) return false;
        float best = 1.0e18f;
        for (const Exit& e : here_area->second.exits) {
            if (e.to != step->second) continue;
            const float d = (e.x - px) * (e.x - px) + (e.y - py) * (e.y - py);
            if (d < best) { best = d; ox = e.x; oy = e.y; label = e.label; }
        }
        return best < 1.0e18f;
    };
    const auto no_road = [&](const string& to) {
        const auto there = areas.find(to);
        const bool dream_there = there != areas.end() && there->second.dream;
        if (dream_there && !here_area->second.dream) return string("In the Reverie: sleep in a bed after dusk, and choose to dream.");
        if (!dream_there && here_area->second.dream) return string("In the waking world: this will keep until morning.");
        return string();
    };

    if (stage.type == ObjectiveType::Reach) {
        const auto there = areas.find(stage.target);
        if (there == areas.end() || stage.target == here) return w;
        w.map = stage.target;
        w.what = w.place = there->second.name;
        if (!doors.count(stage.target)) { w.hint = no_road(stage.target); return w; }
        w.maps_away = doors[stage.target];
        w.found = way_out(stage.target, w.local_x, w.local_y, w.via);
        return w;
    }

    const int holding = stage.type == ObjectiveType::Deliver ? world.player.inventory.Count(stage.target) : 0;
    const vector<Spot> spots = SpotsFor(stage, holding, enemies, loot, items);
    if (spots.empty()) return w;

    // The nearest: by doors first, and then by the walk on this map -- to the
    // thing if it is here, to the way out if it is not.
    const Spot* best = nullptr;
    int best_doors = 0;
    float best_walk = 0.0f;
    for (const Spot& s : spots) {
        const auto d = doors.find(s.map);
        if (d == doors.end()) continue;
        float tx = s.x, ty = s.y;
        string label;
        if (d->second > 0 && !way_out(s.map, tx, ty, label)) continue;
        const float walk = (tx - px) * (tx - px) + (ty - py) * (ty - py);
        if (!best || d->second < best_doors || (d->second == best_doors && walk < best_walk)) {
            best = &s; best_doors = d->second; best_walk = walk;
        }
    }
    if (!best) { w.hint = no_road(spots.front().map); w.map = spots.front().map; w.what = spots.front().label; return w; }

    w.found = true;
    w.map = best->map;
    w.x = best->x; w.y = best->y;
    w.what = best->label;
    w.maps_away = best_doors;
    w.here = best_doors == 0;
    w.place = areas.at(best->map).name;
    if (!w.here) {
        way_out(best->map, w.local_x, w.local_y, w.via);
        return w;
    }
    w.local_x = w.x; w.local_y = w.y;

    // Here, so look at what is actually standing about rather than at where the
    // map file put it: a villager is somewhere on their round, and the boar that
    // was at that post may be dead while another is not.
    const bool to_person = stage.type == ObjectiveType::Talk ||
                           (stage.type == ObjectiveType::Deliver && holding >= stage.count);
    if (to_person) {
        const string& id = stage.type == ObjectiveType::Talk ? stage.target : stage.deliver_to;
        for (const auto& n : world.npcs)
            if (n->Id() == id) {
                if (n->Away()) w.hint = w.what + " has gone in for the night.";
                else { w.x = w.local_x = n->x; w.y = w.local_y = n->y; }
            }
    } else if (stage.type == ObjectiveType::Kill) {
        float nearest = 1.0e18f;
        for (const auto& e : world.enemies) {
            if (e->Dead() || e->CurrentState() == Enemy::State::Dead || !e->Def()) continue;
            if (e->TypeId() != stage.target && e->Def()->kill_target != stage.target) continue;
            const float d = (e->x - px) * (e->x - px) + (e->y - py) * (e->y - py);
            if (d < nearest) { nearest = d; w.x = w.local_x = e->x; w.y = w.local_y = e->y; w.what = e->Def()->name; }
        }
    }
    return w;
}
