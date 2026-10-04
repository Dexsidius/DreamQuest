#include "story.h"
#include "world.h"
#include "../systems/quest.h"
#include "../systems/audio.h"
#include <fstream>

// -----------------------------------------------------------------------------
//  Reading the file
// -----------------------------------------------------------------------------

static Facing FacingOf(const string& s, Facing fallback) {
    return s == "left" ? FACE_LEFT : s == "right" ? FACE_RIGHT : s == "up" ? FACE_UP : s == "down" ? FACE_DOWN : fallback;
}

static float Ease(float k) {
    k = std::clamp(k, 0.0f, 1.0f);
    return k * k * (3.0f - 2.0f * k);
}

static Sfx SfxNamed(const string& n) {
    static const std::map<string, Sfx> names = {
        {"door", Sfx::Door}, {"locked", Sfx::Locked}, {"portal", Sfx::Portal}, {"sleep", Sfx::Sleep},
        {"wake", Sfx::Wake}, {"chest", Sfx::ChestOpen}, {"pickup", Sfx::Pickup}, {"eat", Sfx::Eat},
        {"hurt", Sfx::PlayerHurt}, {"step", Sfx::FootstepStone}, {"quest", Sfx::QuestStart},
        {"hit", Sfx::Hit}, {"heavy", Sfx::SwingHeavy}, {"roar", Sfx::Roar}, {"splash", Sfx::Splash},
        {"echo", Sfx::Echo}, {"grind", Sfx::Grind}, {"vanish", Sfx::Vanish}, {"thunder", Sfx::Thunder},
        {"bell", Sfx::Bell}, {"chime", Sfx::Chime}, {"anvil", Sfx::Anvil}, {"howl", Sfx::Howl},
        {"laugh", Sfx::Laugh}, {"tear", Sfx::Tear}, {"bump", Sfx::Bump}, {"shatter", Sfx::Shatter},
        {"gust", Sfx::Gust}, {"plop", Sfx::Plop}, {"reel", Sfx::Reel}, {"snap", Sfx::Snap},
    };
    const auto it = names.find(n);
    return it == names.end() ? Sfx::UiMove : it->second;
}

bool StoryDirector::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("StoryDirector: no story at '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("StoryDirector: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }
    scenes.clear();
    actors.clear();
    tips.clear();
    locks.clear();
    if (root.contains("locks") && root["locks"].is_array())
        for (const auto& l : root["locks"])
            locks.push_back({l.value("skill", string("")), FlagCond::FromJson(l.value("when", json())),
                             l.value("text", string(""))});
    if (root.contains("tips"))
        for (auto it = root["tips"].begin(); it != root["tips"].end(); ++it)
            tips[it.key()] = {it.key(), it.value().value("title", string("")), it.value().value("text", string(""))};
    if (root.contains("actors"))
        for (auto it = root["actors"].begin(); it != root["actors"].end(); ++it) {
            const json& a = it.value();
            ActorDef d;
            d.sprite = a.value("sprite", string(""));
            d.image  = a.value("image", string(""));
            d.name   = a.value("name", it.key());
            d.scale  = a.value("scale", 1.0f);
            d.image_from = a.value("from_row", 0);
            if (a.contains("named") && a["named"].is_array())
                for (const auto& n : a["named"])
                    d.named.push_back({FlagCond::FromJson(n.value("when", json())), n.value("name", string(""))});
            actors[it.key()] = d;
        }
    if (root.contains("scenes"))
        for (const auto& j : root["scenes"]) {
            Scene s;
            s.id = j.value("id", string(""));
            if (s.id.empty()) continue;
            s.then = j.value("then", string(""));
            s.needs = j.value("needs", string(""));
            if (j.contains("when")) s.when = FlagCond::FromJson(j["when"]);
            if (j.contains("on")) {
                const json& on = j["on"];
                if (on.is_string()) s.on = on.get<string>();
                else if (on.is_object()) {
                    for (const char* kind : {"enter", "near", "talk", "use", "flag"})
                        if (on.contains(kind)) { s.on = kind; s.at = on[kind].get<string>(); }
                    s.map = on.value("map", string(""));
                    s.radius = on.value("radius", 48.0f);
                    // "enter" names the map it is about; "near" the mark on it.
                    if (s.on == "enter") { s.map = s.at; s.at = on.value("spawn", string("")); }
                }
            }
            if (j.contains("steps") && j["steps"].is_array()) s.steps = j["steps"];
            scenes.push_back(s);
        }
    SDL_Log("StoryDirector: %d scenes, %d actors, %d tips", static_cast<int>(scenes.size()),
            static_cast<int>(actors.size()), static_cast<int>(tips.size()));
    return true;
}

const StoryDirector::Scene* StoryDirector::Find(const string& id) const {
    for (const Scene& s : scenes) if (s.id == id) return &s;
    return nullptr;
}

const StoryTip* StoryDirector::FindTip(const string& id) const {
    const auto it = tips.find(id);
    return it == tips.end() ? nullptr : &it->second;
}

string StoryDirector::Current() const { return run.scene ? run.scene->id : string(); }

string StoryDirector::ActorName(const string& id, const World& w) const {
    const auto it = actors.find(id);
    if (it == actors.end()) return id;
    for (const auto& n : it->second.named)
        if (w.Holds(n.first)) return n.second;
    return it->second.name;
}

void StoryDirector::Reset() {
    seen_map.clear();
}

bool StoryDirector::Carries(const Scene& s, const World& w) const {
    return s.needs.empty() || w.player.inventory.Has(s.needs, 1);
}

// -----------------------------------------------------------------------------
//  Where and who
// -----------------------------------------------------------------------------

Npc* StoryDirector::Who(const string& id, World& w) {
    if (id.empty() || id == "player") return nullptr;
    return w.FindNpc(id);
}

bool StoryDirector::Point(const json& v, const World& w, float& x, float& y) const {
    // Two names, ["player", "vexel"]: halfway between them, for a camera
    // that keeps both in the picture wherever the player came in from.
    if (v.is_array() && v.size() == 2 && v[0].is_string() && v[1].is_string()) {
        float ax = 0, ay = 0, bx = 0, by = 0;
        if (!Point(v[0], w, ax, ay) || !Point(v[1], w, bx, by)) return false;
        x = (ax + bx) * 0.5f;
        y = (ay + by) * 0.5f;
        return true;
    }
    if (v.is_array() && v.size() >= 2) {
        x = v[0].get<float>();
        y = v[1].get<float>();
        return true;
    }
    if (!v.is_string()) return false;
    const string name = v.get<string>();
    if (name == "player") { x = w.player.x; y = w.player.y; return true; }
    for (const auto& n : w.npcs)
        if (n->Id() == name) { x = n->x; y = n->y; return true; }
    SDL_FPoint p;
    if (w.map.Spawn(name, p) || w.map.Mark(name, p)) { x = p.x; y = p.y; return true; }
    SDL_Log("StoryDirector: no mark '%s' on %s", name.c_str(), w.MapId().c_str());
    return false;
}

bool StoryDirector::PointOf(const json& s, const World& w, float& x, float& y) const {
    if (!s.contains("at") || !Point(s["at"], w, x, y)) return false;
    x += s.value("dx", 0.0f);
    y += s.value("dy", 0.0f);
    return true;
}

float StoryDirector::CamX(const World& w) const {
    if (w.cam_hold.on) return w.cam_hold.x;
    const SDL_FRect r = w.camera.VisibleWorldRect(0.0f);
    return r.x + r.w * 0.5f;
}

float StoryDirector::CamY(const World& w) const {
    if (w.cam_hold.on) return w.cam_hold.y;
    const SDL_FRect r = w.camera.VisibleWorldRect(0.0f);
    return r.y + r.h * 0.5f;
}

Npc* StoryDirector::Spawn(const string& actor, float x, float y, World& w, const GameContext& ctx) {
    Remove(actor, w);
    NpcDef def;
    def.id = actor;
    const auto it = actors.find(actor);
    if (it != actors.end()) def.sprite = it->second.sprite;
    def.name = ActorName(actor, w);
    def.x = x;
    def.y = y;
    auto n = std::make_unique<Npc>();
    n->Init(def, ctx);
    n->actor = true;
    n->scripted = true;
    if (it != actors.end()) {
        n->image = it->second.image;
        n->image_from = it->second.image_from;
        n->scale = it->second.scale;
        n->sprite.size_scale = it->second.scale;
    }
    Npc* raw = n.get();
    w.npcs.push_back(std::move(n));
    run.spawned.push_back(actor);
    return raw;
}

void StoryDirector::Remove(const string& actor, World& w) {
    w.npcs.erase(std::remove_if(w.npcs.begin(), w.npcs.end(),
                                [&](const std::unique_ptr<Npc>& n) { return n->actor && n->Id() == actor; }),
                 w.npcs.end());
}

void StoryDirector::Hold(const string& id, World& w) {
    Npc* n = Who(id, w);
    if (!n || n->actor) return;
    n->scripted = true;
    if (std::find(run.held.begin(), run.held.end(), id) == run.held.end()) run.held.push_back(id);
}

void StoryDirector::PlaceAttached(World& w) {
    if (!run.attached) return;
    if (Npc* n = Who(run.attach_to, w)) {
        w.player.x = n->x + run.attach_dx;
        w.player.y = n->y + run.attach_dy;
    }
}

void StoryDirector::SetFade(const json& s, float& from, float& to, SDL_Color& colour) const {
    const string target = s.value("to", string("clear"));
    from = view.fade;
    // Part of the way, with an amount: the light dimming as a shadow passes.
    to = target == "clear" ? 0.0f : std::clamp(s.value("amount", 1.0f), 0.0f, 1.0f);
    if (target == "white") colour = {240, 236, 255, 255};
    else if (target == "black") colour = {0, 0, 0, 255};
    // Fading to clear keeps whatever colour the screen is fading out of.
}

// -----------------------------------------------------------------------------
//  Running a scene
// -----------------------------------------------------------------------------

bool StoryDirector::Start(const string& id, World& w, QuestLog& q, const GameContext& ctx) {
    if (Running()) return false;
    const Scene* s = Find(id);
    if (!s) {
        SDL_Log("StoryDirector: no scene '%s'", id.c_str());
        return false;
    }
    return StartScene(s, w, q, ctx, false);
}

bool StoryDirector::StartScene(const Scene* s, World& w, QuestLog& q, const GameContext& ctx, bool skipping) {
    (void)q;
    (void)ctx;
    const bool chained = run.scene != nullptr;
    // A scene that follows another keeps its actors and its hold on the camera.
    if (!chained) run = Run{};
    run.scene = s;
    run.step = 0;
    run.begun = false;
    run.step_t = 0.0f;
    run.skipping = skipping;
    view.in_scene = true;
    w.player.input_locked = true;
    w.player.StopGathering();
    skip_released = false;
    seen_map = w.MapId();
    return true;
}

bool StoryDirector::OnTalk(const string& npc_id, World& w, QuestLog& q, const GameContext& ctx) {
    if (Running()) return true;
    for (const Scene& s : scenes)
        if (s.on == "talk" && s.at == npc_id && (s.map.empty() || s.map == w.MapId()) && w.Holds(s.when) &&
            Carries(s, w))
            return StartScene(&s, w, q, ctx, false);
    return false;
}

bool StoryDirector::OnUse(const string& object_id, World& w, QuestLog& q, const GameContext& ctx) {
    if (Running()) return true;
    for (const Scene& s : scenes)
        if (s.on == "use" && s.at == object_id && (s.map.empty() || s.map == w.MapId()) && w.Holds(s.when) &&
            Carries(s, w))
            return StartScene(&s, w, q, ctx, false);
    return false;
}

void StoryDirector::CheckTriggers(World& w, QuestLog& q, const GameContext& ctx) {
    if (w.TransitionPending()) return;
    // Walked into a map: once for each arrival.
    if (w.MapId() != seen_map) {
        seen_map = w.MapId();
        for (const Scene& s : scenes) {
            if (s.on != "enter" || s.map != w.MapId() || !w.Holds(s.when) || !Carries(s, w)) continue;
            if (!s.at.empty()) {
                // Only by this way in: arrived at that spawn.
                SDL_FPoint p;
                if (!w.map.Spawn(s.at, p) || Length(w.player.x - p.x, w.player.y - p.y) > 48.0f) continue;
            }
            StartScene(&s, w, q, ctx, false);
            return;
        }
    }
    for (const Scene& s : scenes) {
        if (s.on == "near") {
            if ((!s.map.empty() && s.map != w.MapId()) || !w.Holds(s.when) || !Carries(s, w)) continue;
            float x = 0, y = 0;
            if (!Point(json(s.at), w, x, y)) continue;
            if (Length(w.player.x - x, w.player.y - y) > s.radius) continue;
            StartScene(&s, w, q, ctx, false);
            return;
        }
        if (s.on == "flag") {
            if (!w.Flagged(s.at) || !w.Holds(s.when) || !Carries(s, w)) continue;
            if (!s.map.empty() && s.map != w.MapId()) continue;
            StartScene(&s, w, q, ctx, false);
            return;
        }
    }
}

void StoryDirector::Update(float dt, World& w, QuestLog& q, const GameContext& ctx, bool confirm, bool skip_held) {
    // What quests finished want set (QuestDef::sets_flag), and the flag stages
    // they wait on.
    for (const string& f : q.TakeFlagsToSet()) w.SetFlag(f);
    q.RefreshFlagObjectives([&](const string& f) { return w.Flagged(f); }, w.player.inventory);

    if (!Running()) {
        // Nothing running: the bars go out, and nothing is ever left dark.
        view.bars = std::max(0.0f, view.bars - dt * 2.5f);
        view.fade = std::max(0.0f, view.fade - dt * 1.5f);
        view.title_alpha = std::max(0.0f, view.title_alpha - dt * 1.5f);
        view.line = view.note = false;
        view.skip = 0.0f;
        skip_held_for = 0.0f;
        if (!skip_held) skip_released = true;
        CheckTriggers(w, q, ctx);
        if (!Running()) return;
    }

    // Held, the skip button ends it -- once it has been let go since the
    // scene began, so the press that dismissed something is not taken for it.
    if (!skip_held) skip_released = true;
    if (skip_held && skip_released) skip_held_for += dt;
    else                            skip_held_for = 0.0f;
    view.skip = std::clamp(skip_held_for / 0.8f, 0.0f, 1.0f);
    if (skip_held_for >= 0.8f) {
        skip_held_for = 0.0f;
        Skip(w, q, ctx);
        return;
    }

    w.player.input_locked = true;
    Tick(dt, w);
    Advance(dt, w, q, ctx, confirm);
}

void StoryDirector::Tick(float dt, World& w) {
    for (Tween& t : run.tweens) {
        t.t = std::min(t.time, t.t + dt);
        const float k = t.time > 0.0f ? t.t / t.time : 1.0f;
        const float e = Ease(k);
        switch (t.kind) {
            case Tween::Fade:  view.fade = t.from[0] + (t.to[0] - t.from[0]) * k; break;
            case Tween::Bars:  view.bars = t.from[0] + (t.to[0] - t.from[0]) * e; break;
            case Tween::Veil:  w.SetReverieVeil(t.from[0] + (t.to[0] - t.from[0]) * k); break;
            case Tween::Cam:
                w.cam_hold.on = true;
                w.cam_hold.snap = true;
                w.cam_hold.x = t.from[0] + (t.to[0] - t.from[0]) * e;
                w.cam_hold.y = t.from[1] + (t.to[1] - t.from[1]) * e;
                if (t.zoom_to > 0.0f) w.camera.SetZoom(t.zoom_from + (t.zoom_to - t.zoom_from) * e);
                break;
            case Tween::Clock: {
                // A time-lapse: the hours run by as the seconds do.
                const double hours = t.from[0] + (t.to[0] - t.from[0]) * k;
                const int day = static_cast<int>(std::floor(hours / 24.0));
                w.clock.Set(day, static_cast<float>(hours - day * 24.0));
                break;
            }
            case Tween::Alpha:
                if (Npc* n = Who(t.who, w)) n->alpha = t.from[0] + (t.to[0] - t.from[0]) * e;
                break;
            case Tween::Dissolve:
                if (Npc* n = Who(t.who, w)) n->dissolve = t.from[0] + (t.to[0] - t.from[0]) * k;
                break;
            case Tween::Sink:
                // Pulled under and held there, or dipped and let up again.
                if (Npc* n = Who(t.who, w))
                    n->sink = t.to[0] > 0.5f ? t.from[0] * e : t.from[0] * sinf(3.1415926f * k);
                break;
            case Tween::PlayerMove: {
                w.player.x = t.from[0] + (t.to[0] - t.from[0]) * k;
                w.player.y = t.from[1] + (t.to[1] - t.from[1]) * k;
                const float dx = t.to[0] - t.from[0], dy = t.to[1] - t.from[1];
                if (fabsf(dx) > fabsf(dy)) w.player.facing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
                else                       w.player.facing = dy > 0 ? FACE_DOWN : FACE_UP;
                w.player.sprite.facing = w.player.facing;
                w.player.scene_clip = k < 1.0f ? t.clip : string();
                break;
            }
        }
    }
    run.tweens.erase(std::remove_if(run.tweens.begin(), run.tweens.end(),
                                    [](const Tween& t) { return t.t >= t.time; }),
                     run.tweens.end());
    // The camera kept on somebody walking.
    if (!run.cam_follow.empty()) {
        float x = 0, y = 0;
        if (Point(json(run.cam_follow), w, x, y)) {
            w.cam_hold.on = true;
            w.cam_hold.snap = true;
            w.cam_hold.x = x;
            w.cam_hold.y = y - 20.0f;
        }
    }
    PlaceAttached(w);
    if (view.line || view.note) view.line_age += dt;
}

void StoryDirector::Advance(float dt, World& w, QuestLog& q, const GameContext& ctx, bool confirm) {
    // Several steps can go by in a frame: a flag, a quest and a fade that does
    // not wait. Never more than the scene has.
    for (int guard = 0; guard < 64 && run.scene; ++guard) {
        const json& steps = run.scene->steps;
        if (run.step >= steps.size()) {
            Finish(w, q, ctx);
            return;
        }
        const json& s = steps[run.step];
        if (!run.begun) {
            run.begun = true;
            run.step_t = 0.0f;
            if (Begin(s, w, q, ctx)) { ++run.step; run.begun = false; continue; }
            confirm = false;           // the press that started a line does not end it
        } else {
            run.step_t += dt;
        }
        if (!Done(s, w, confirm)) return;
        ++run.step;
        run.begun = false;
        confirm = false;
    }
}

// The steps. Each starts here; a step that takes time says so by returning
// false, and Done says when it is over.
bool StoryDirector::Begin(const json& s, World& w, QuestLog& q, const GameContext& ctx) {
    const string op = s.value("do", string(""));
    const bool wait = s.value("wait", true);
    const string who = s.value("who", string(""));
    Npc* n = Who(who, w);
    if (n && !n->actor) Hold(who, w);

    if (op == "bars") {
        Tween t; t.kind = Tween::Bars; t.from[0] = view.bars; t.to[0] = s.value("on", true) ? 1.0f : 0.0f;
        t.time = s.value("time", 0.6f);
        run.tweens.push_back(t);
        return !wait;
    }
    if (op == "fade") {
        Tween t; t.kind = Tween::Fade;
        SetFade(s, t.from[0], t.to[0], view.fade_colour);
        t.time = s.value("time", 1.0f);
        run.tweens.push_back(t);
        return !wait;
    }
    if (op == "wait") return false;
    if (op == "say" || op == "narrate") {
        view.line = true;
        view.narration = op == "narrate";
        view.text = s.value("text", string(""));
        view.speaker = view.narration ? string()
                     : s.contains("name") ? s["name"].get<string>()
                     : n ? n->Name()
                     : actors.count(who) ? ActorName(who, w) : who;
        view.line_age = 0.0f;
        return false;
    }
    if (op == "note") {
        view.note = true;
        view.note_title = s.value("title", string(""));
        view.note_text = s.value("text", string(""));
        view.line_age = 0.0f;
        return false;
    }
    if (op == "title") {
        view.title = s.value("text", string(""));
        view.subtitle = s.value("sub", string(""));
        run.title_time = s.value("time", 4.0f);
        return false;
    }
    if (op == "tip") {
        if (const StoryTip* t = FindTip(s.value("id", string("")))) tip_queue.push_back(*t);
        return true;
    }
    if (op == "map") {
        // Loaded under the dark, wherever the scene says.
        w.LoadMap(s.value("map", string("")), s.value("spawn", string("")), ctx);
        seen_map = w.MapId();
        float x = 0, y = 0;
        if (PointOf(s, w, x, y)) { w.player.x = x; w.player.y = y; }
        w.camera.SnapTo(w.player.x, w.player.y);
        return true;
    }
    if (op == "player") {
        float x = 0, y = 0;
        if (PointOf(s, w, x, y)) { w.player.x = x; w.player.y = y; }
        if (s.contains("face")) {
            w.player.facing = FacingOf(s["face"].get<string>(), w.player.facing);
            w.player.sprite.facing = w.player.facing;
        }
        if (s.contains("pose")) {
            w.player.scene_clip = s["pose"].get<string>();
            if (!w.player.scene_clip.empty()) w.player.sprite.Play(w.player.scene_clip, true);
        }
        if (s.contains("hidden")) w.player.scene_hidden = s["hidden"].get<bool>();
        if (s.contains("lift"))   w.player.scene_lift = s["lift"].get<float>();
        if (s.contains("bias"))   w.player.sort_bias = s["bias"].get<float>();
        return true;
    }
    if (op == "camera") {
        if (s.value("release", false)) {
            run.cam_follow.clear();
            w.cam_hold.on = false;
            return true;
        }
        if (s.contains("follow")) {
            run.cam_follow = s["follow"].get<string>();
            return true;
        }
        run.cam_follow.clear();
        float x = 0, y = 0;
        if (!PointOf(s, w, x, y)) { x = CamX(w); y = CamY(w); }
        Tween t; t.kind = Tween::Cam;
        t.from[0] = CamX(w); t.from[1] = CamY(w);
        t.to[0] = x; t.to[1] = y;
        t.time = s.value("time", 0.0f);
        if (s.contains("zoom")) {
            // Pulled back to see more, or in close; put back as it was when
            // the scene is over.
            if (run.zoom0 <= 0.0f) run.zoom0 = w.camera.zoom;
            t.zoom_from = w.camera.zoom;
            t.zoom_to = s["zoom"].get<float>() * run.zoom0;
        }
        if (t.time <= 0.0f) {
            w.cam_hold.on = true; w.cam_hold.snap = true; w.cam_hold.x = x; w.cam_hold.y = y;
            if (t.zoom_to > 0.0f) w.camera.SetZoom(t.zoom_to);
            return true;
        }
        run.tweens.push_back(t);
        return !wait;
    }
    if (op == "spawn") {
        float x = 0, y = 0;
        PointOf(s, w, x, y);
        Npc* a = Spawn(s.value("actor", string("")), x, y, w, ctx);
        // Left where it is when the scene is over, for as long as the map is up.
        if (s.value("keep", false)) run.spawned.pop_back();
        if (s.contains("bob")) a->bob = s["bob"].get<float>();
        if (s.contains("face")) { a->facing = FacingOf(s["face"].get<string>(), FACE_DOWN); a->sprite.facing = a->facing; }
        if (s.contains("pose")) a->Hold(s["pose"].get<string>(), true);
        if (s.contains("alpha")) a->alpha = s["alpha"].get<float>();
        if (s.contains("name")) a->SetName(s["name"].get<string>());
        if (s.contains("bias")) a->sort_bias = s["bias"].get<float>();
        if (s.contains("lift")) a->lift = s["lift"].get<float>();
        if (s.contains("flicker")) a->flicker = s["flicker"].get<bool>();
        return true;
    }
    if (op == "remove") {
        Remove(s.value("actor", who), w);
        return true;
    }
    if (op == "walk") {
        float x = 0, y = 0;
        if (s.contains("toward")) {
            // A step toward somebody -- or, a negative distance, away from them.
            float tx = 0, ty = 0;
            if (!Point(s["toward"], w, tx, ty)) return true;
            const float ox = who == "player" ? w.player.x : n ? n->x : 0.0f;
            const float oy = who == "player" ? w.player.y : n ? n->y : 0.0f;
            const float d = std::max(1.0f, Length(tx - ox, ty - oy));
            const float dist = s.value("dist", 24.0f);
            x = ox + (tx - ox) / d * dist;
            y = oy + (ty - oy) / d * dist;
        } else if (!PointOf(s, w, x, y)) {
            return true;
        }
        const float speed = s.value("speed", 60.0f);
        const string clip = s.value("clip", string("walk"));
        if (who == "player") {
            Tween t; t.kind = Tween::PlayerMove;
            t.from[0] = w.player.x; t.from[1] = w.player.y;
            t.to[0] = x; t.to[1] = y;
            t.time = Length(x - w.player.x, y - w.player.y) / std::max(1.0f, speed);
            t.clip = clip;
            t.who = "player";
            run.tweens.push_back(t);
            return !wait;
        }
        if (n) n->WalkTo(x, y, speed, clip);
        return !wait;
    }
    if (op == "face") {
        Facing f = FACE_DOWN;
        if (s.contains("toward")) {
            float tx = 0, ty = 0;
            Point(s["toward"], w, tx, ty);
            const float ox = who == "player" ? w.player.x : n ? n->x : 0.0f;
            const float oy = who == "player" ? w.player.y : n ? n->y : 0.0f;
            const float dx = tx - ox, dy = ty - oy;
            f = fabsf(dx) > fabsf(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT) : (dy > 0 ? FACE_DOWN : FACE_UP);
        } else {
            f = FacingOf(s.value("dir", string("down")), FACE_DOWN);
        }
        if (who == "player") { w.player.facing = f; w.player.sprite.facing = f; }
        else if (n) { n->facing = f; n->sprite.facing = f; }
        return true;
    }
    if (op == "pose") {
        const string clip = s.value("clip", string(""));
        if (who == "player") {
            w.player.scene_clip = clip;
            if (!clip.empty()) w.player.sprite.Play(clip, true);
        } else if (n) {
            n->Hold(clip, true);
        }
        return !s.value("wait", false);
    }
    if (op == "show") {
        // Fading somebody in or out, or simply there or not.
        const float to = s.value("alpha", 1.0f);
        const float time = s.value("time", 0.0f);
        if (who == "player") { w.player.scene_hidden = to < 0.5f; return true; }
        if (!n) return true;
        if (time <= 0.0f) { n->alpha = to; return true; }
        Tween t; t.kind = Tween::Alpha; t.who = who; t.from[0] = n->alpha; t.to[0] = to; t.time = time;
        run.tweens.push_back(t);
        return !wait;
    }
    if (op == "name") {
        if (n) n->SetName(s.value("text", string("")));
        return true;
    }
    if (op == "crowd") {
        // Everybody out in the street at once (scene 50: "The townsfolk freeze
        // and look up"): stopped where they stand and turned the way the scene
        // says -- and, with "release", let go again to make up their rounds.
        // Whoever is named in "except", or is the scene's already, is left be.
        if (s.value("release", false)) {
            for (const string& id : run.crowd)
                if (Npc* c = w.FindNpc(id)) { c->scripted = false; c->StopWalking(); c->Hold(""); }
            run.held.erase(std::remove_if(run.held.begin(), run.held.end(), [&](const string& id) {
                               return std::find(run.crowd.begin(), run.crowd.end(), id) != run.crowd.end();
                           }),
                           run.held.end());
            run.crowd.clear();
            return true;
        }
        std::set<string> except;
        if (s.contains("except") && s["except"].is_array())
            for (const auto& e : s["except"]) if (e.is_string()) except.insert(e.get<string>());
        const Facing f = FacingOf(s.value("dir", string("up")), FACE_UP);
        for (auto& c : w.npcs) {
            if (c->actor || c->scripted || c->Away() || c->Asleep() || c->alpha < 0.5f || except.count(c->Id())) continue;
            c->scripted = true;
            c->StopWalking();
            c->Hold("");
            c->facing = f;
            c->sprite.facing = f;
            run.crowd.push_back(c->Id());
            run.held.push_back(c->Id());
        }
        return true;
    }
    if (op == "dip") {
        // Something afloat dipping -- a bobber at a nibble -- `depth` rows into
        // the water and up again, or with "hold" pulled right under and kept there.
        if (!n) return true;
        Tween t;
        t.kind = Tween::Sink;
        t.who = who;
        t.from[0] = s.value("depth", 3.0f);
        t.to[0] = s.value("hold", false) ? 1.0f : 0.0f;
        t.time = std::max(0.05f, s.value("time", 0.45f));
        run.tweens.push_back(t);
        w.Ripple(n->x, n->y, t.from[0] + 4.0f);
        return !wait;
    }
    if (op == "attach") {
        run.attached = true;
        run.attach_to = s.value("to", string(""));
        run.attach_dx = s.value("dx", 0.0f);
        run.attach_dy = s.value("dy", 0.0f);
        PlaceAttached(w);
        return true;
    }
    if (op == "detach") {
        run.attached = false;
        return true;
    }
    if (op == "fx") {
        const string kind = s.value("kind", string(""));
        if (kind == "vanish" && n) {
            // The stranger's going: drawn in to himself as smoke, and gone.
            w.Smoke(n->x, n->y, 30.0f * n->scale + 14.0f, true);
            Audio::Play(Sfx::Vanish, 0.9f);
            if (s.value("sink", true)) n->Hold("death", true);
            Tween t; t.kind = Tween::Dissolve; t.who = who; t.from[0] = 0.0f; t.to[0] = 1.0f;
            t.time = s.value("time", 1.1f);
            run.tweens.push_back(t);
            return !wait;
        }
        if (kind == "appear" && n) {
            w.Smoke(n->x, n->y, 30.0f * n->scale + 14.0f, false);
            Audio::Play(Sfx::Vanish, 0.8f, 0.85f);
            n->alpha = 1.0f;
            n->dissolve = 1.0f;
            Tween t; t.kind = Tween::Dissolve; t.who = who; t.from[0] = 1.0f; t.to[0] = 0.0f;
            t.time = s.value("time", 1.0f);
            run.tweens.push_back(t);
            return !wait;
        }
        if (kind == "take") {
            // The same smoke, round the player.
            w.Smoke(w.player.x, w.player.y, 34.0f, true);
            w.Smoke(w.player.x, w.player.y, 20.0f, false);
            Audio::Play(Sfx::Vanish, 1.0f, 0.7f);
            return true;
        }
        if (kind == "flash") {
            const json& c = s.value("colour", json::array({240, 236, 255}));
            w.Flash({static_cast<Uint8>(c[0].get<int>()), static_cast<Uint8>(c[1].get<int>()),
                     static_cast<Uint8>(c[2].get<int>()), 255}, s.value("amount", 1.0f));
            return true;
        }
        if (kind == "lightning") {
            w.Flash({235, 240, 255, 255}, s.value("amount", 1.0f));
            Audio::Play(Sfx::Thunder, s.value("volume", 0.9f), 0.9f + 0.2f * (SDL_rand(100) / 100.0f));
            return true;
        }
        if (kind == "veil") {
            Tween t; t.kind = Tween::Veil; t.from[0] = w.ReverieVeil(); t.to[0] = s.value("to", 1.0f);
            t.time = s.value("time", 0.0f);
            if (t.time <= 0.0f) { w.SetReverieVeil(t.to[0]); return true; }
            run.tweens.push_back(t);
            return !wait;
        }
        if (kind == "dream") {
            // Into a dream of the scene's choosing, by the flip. A sleeper's
            // ("story") keeps no time; a locked one has no way out but through.
            w.EnterDream(s.value("map", string("")), s.value("spawn", string("arrival")),
                         s.value("story", false), s.value("locked", false));
            return true;
        }
        if (kind == "tear") {
            // Pulled in, not fallen asleep: the picture tears (World::TearInto).
            Audio::Play(Sfx::Tear, 1.0f);
            w.TearInto(s.value("map", string("")), s.value("spawn", string("arrival")));
            return true;
        }
        if (kind == "shatter") {
            // A barrier breaking like glass: a burst of dark shards, a flash.
            float x = 0, y = 0;
            if (!PointOf(s, w, x, y)) { x = w.player.x; y = w.player.y; }
            const float radius = s.value("radius", 90.0f);
            for (int k = 0; k < 6; ++k) {
                const float a = k * 1.0471976f;
                w.Smoke(x + cosf(a) * radius, y + sinf(a) * radius * 0.6f, 26.0f, false);
            }
            w.Shock(x, y, 1.0f, 0.5f);
            w.Flash({206, 186, 250, 255}, s.value("amount", 0.6f));
            Audio::Play(Sfx::Shatter, s.value("volume", 1.0f));
            return true;
        }
        if (kind == "golden") {
            // The Dawn Bells' light washing out across the dream.
            float x = 0, y = 0;
            if (!PointOf(s, w, x, y)) { x = w.player.x; y = w.player.y; }
            w.Shock(x, y, 1.2f, 0.4f);
            w.Flash({255, 214, 140, 255}, s.value("amount", 0.9f));
            return true;
        }
        if (kind == "steam" && n) {
            // Something curling up off somebody: a temper, a cold breath.
            w.Steam(n->x, n->y - 34.0f, 10.0f);
            return true;
        }
        if (kind == "shadow") {
            // A shadow sliding over everything -- a dragon's, never the dragon.
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!s.contains("from") || !Point(s["from"], w, x0, y0)) { x0 = w.player.x - 700.0f; y0 = w.player.y - 260.0f; }
            if (!s.contains("to")   || !Point(s["to"], w, x1, y1))   { x1 = w.player.x + 700.0f; y1 = w.player.y + 260.0f; }
            w.PassShadow(s.value("image", string("assets/props/dragon_shadow.png")), x0, y0, x1, y1,
                         s.value("time", 3.0f), s.value("alpha", 0.55f));
            return !wait;
        }
        return true;
    }
    if (op == "wake") {
        // Out of a story's dream, under the white (or the dark) of a fade the
        // scene has already brought up: the dream is over, and the player is
        // wherever the scene says they wake.
        w.EndDream();
        w.SetReverieVeil(0.0f);
        w.LoadMap(s.value("map", string("")), s.value("spawn", string("")), ctx);
        seen_map = w.MapId();
        float x = 0, y = 0;
        if (PointOf(s, w, x, y)) { w.player.x = x; w.player.y = y; }
        w.camera.SnapTo(w.player.x, w.player.y);
        return true;
    }
    if (op == "banish") {
        // Every monster on the map (or every one of a kind) gone as dust.
        w.Banish(s.value("type", string("")));
        return true;
    }
    if (op == "tint") {
        // Somebody's colour: a temper rising, the life coming back into a face.
        const json& c = s.value("colour", json::array({255, 255, 255}));
        const SDL_Color col{static_cast<Uint8>(c[0].get<int>()), static_cast<Uint8>(c[1].get<int>()),
                           static_cast<Uint8>(c[2].get<int>()), 255};
        if (n) n->tint = col;
        return true;
    }
    if (op == "confirm") {
        // A question, yes or no: Game asks it; a no ends the scene there.
        view.ask = true;
        view.ask_text = s.value("text", string(""));
        view.ask_yes = s.value("yes", string("Yes"));
        view.ask_no = s.value("no", string("Not yet"));
        run.answer = -1;
        return false;
    }
    if (op == "sfx") {
        Audio::Play(SfxNamed(s.value("id", string(""))), s.value("volume", 1.0f), s.value("pitch", 1.0f));
        return true;
    }
    if (op == "music") {
        Audio::Music(s.value("cue", string("")), s.value("fade", 1.5f));
        return true;
    }
    if (op == "clock") {
        // A flashback keeps the clock it borrowed: saved before, put back after.
        if (s.value("save", false)) { run.saved_hours = w.clock.GameHours(); return true; }
        if (s.value("restore", false)) {
            if (run.saved_hours >= 0.0) {
                const int day = static_cast<int>(std::floor(run.saved_hours / 24.0));
                w.clock.Set(day, static_cast<float>(run.saved_hours - day * 24.0));
            }
            return true;
        }
        const float hour = s.value("hour", 9.0f);
        int day = s.value("day", w.clock.Day());
        // The next morning: tomorrow's, if this hour has gone by today.
        if (s.value("next_morning", false) && w.clock.Hours() >= hour) day = w.clock.Day() + 1;
        w.clock.Set(day, hour);
        return true;
    }
    if (op == "timelapse") {
        // The hours run by: to `to` o'clock -- past midnight if it must -- in
        // `time` seconds.
        double now = w.clock.GameHours();
        double to = std::floor(now / 24.0) * 24.0 + s.value("to", 7.0f);
        while (to <= now) to += 24.0;
        to += 24.0 * s.value("days", 0);
        Tween t; t.kind = Tween::Clock; t.from[0] = static_cast<float>(now); t.to[0] = static_cast<float>(to);
        t.time = s.value("time", 3.0f);
        run.tweens.push_back(t);
        return !wait;
    }
    if (op == "flag") {
        if (s.contains("set"))   w.SetFlag(s["set"].get<string>());
        if (s.contains("clear")) w.ClearFlag(s["clear"].get<string>());
        w.SettleStory();
        return true;
    }
    if (op == "quest") {
        if (s.contains("start")) q.Start(s["start"].get<string>());
        return true;
    }
    if (op == "give") {
        const string item = s.value("item", string(""));
        const int qty = s.value("qty", 1);
        if (!item.empty() && qty > 0) w.player.inventory.Add(item, qty);
        return true;
    }
    if (op == "take" && s.contains("item")) {
        // Something handed over, or used up: a key turned, tokens slotted.
        const string item = s["item"].get<string>();
        w.player.inventory.Remove(item, s.value("qty", 1));
        return true;
    }
    if (op == "take") {
        // What is worn and carried goes: the prologue's character has nothing.
        if (s.value("everything", false)) {
            for (int slot = 0; slot < w.player.inventory.SlotCount(); ++slot) {
                const ItemStack& st = w.player.inventory.Slot(slot);
                if (!st.Empty()) w.player.inventory.RemoveSlot(slot, st.qty);
            }
        }
        return true;
    }
    if (op == "settle") {
        w.SettleStory(true);
        return true;
    }
    if (op == "count") {
        // One more of something counted: the first of FLAG_1..FLAG_n not yet set.
        const string f = s.value("flag", string(""));
        const int of = std::max(1, s.value("of", 3));
        for (int k = 1; k <= of; ++k)
            if (!w.Flagged(f + "_" + std::to_string(k))) { w.SetFlag(f + "_" + std::to_string(k)); break; }
        w.SettleStory();
        return true;
    }
    if (op == "dialogue") {
        // A conversation with something that is not anyone: a lock's slots.
        w.OpenDialogue(s.value("id", string("")), s.value("title", string("")));
        return true;
    }
    SDL_Log("StoryDirector: scene '%s' has a step it does not know: '%s'",
            run.scene ? run.scene->id.c_str() : "?", op.c_str());
    return true;
}

bool StoryDirector::Done(const json& s, World& w, bool confirm) {
    const string op = s.value("do", string(""));
    const string who = s.value("who", string(""));
    if (op == "wait") {
        if (s.value("for", string("")) == "all") {
            for (const auto& n : w.npcs) if (n->scripted && n->Walking()) return false;
            return run.tweens.empty();
        }
        return run.step_t >= s.value("time", 1.0f);
    }
    if (op == "confirm") {
        if (run.answer < 0) return false;
        if (run.answer == 1) {
            if (s.contains("flag")) { w.SetFlag(s["flag"].get<string>()); w.SettleStory(); }
        } else if (run.scene) {
            // No: nothing more of this scene.
            run.step = run.scene->steps.size() - 1;
        }
        return true;
    }
    if (op == "say" || op == "narrate" || op == "note") {
        // Read on, or -- a line that says how long -- by itself.
        const float time = s.value("time", 0.0f);
        const bool over = time > 0.0f ? run.step_t >= time : (confirm && view.line_age > 0.25f);
        if (over) {
            view.line = false;
            view.note = false;
        }
        return over;
    }
    if (op == "title") {
        const float in = 1.0f, out = 1.0f;
        const float t = run.step_t;
        view.title_alpha = t < in ? t / in : t > run.title_time - out ? std::max(0.0f, (run.title_time - t) / out) : 1.0f;
        if (t >= run.title_time) { view.title_alpha = 0.0f; return true; }
        return false;
    }
    if (op == "walk") {
        if (who == "player") {
            for (const Tween& t : run.tweens) if (t.kind == Tween::PlayerMove) return false;
            return true;
        }
        Npc* n = Who(who, w);
        return !n || !n->Walking();
    }
    if (op == "pose") {
        if (who == "player") return w.player.sprite.Finished() || run.step_t > 3.0f;
        Npc* n = Who(who, w);
        return !n || n->sprite.Finished() || run.step_t > 3.0f;
    }
    // Everything that waits on its own tween.
    for (const Tween& t : run.tweens) {
        if (op == "bars" && t.kind == Tween::Bars) return false;
        if (op == "fade" && t.kind == Tween::Fade) return false;
        if (op == "camera" && t.kind == Tween::Cam) return false;
        if (op == "timelapse" && t.kind == Tween::Clock) return false;
        if (op == "show" && t.kind == Tween::Alpha && t.who == who) return false;
        if (op == "dip" && t.kind == Tween::Sink && t.who == who) return false;
        if (op == "fx" && (t.kind == Tween::Dissolve || t.kind == Tween::Veil) && (t.who == who || t.who.empty()))
            return false;
    }
    return true;
}

// A step's end, at once: what a skip does in place of waiting for it.
void StoryDirector::Instant(const json& s, World& w, QuestLog& q, const GameContext& ctx) {
    const string op = s.value("do", string(""));
    const string who = s.value("who", string(""));
    if (op == "wait" || op == "say" || op == "narrate" || op == "note" || op == "title" || op == "sfx") return;
    // Skipped, a question is answered yes: whatever it would set is set.
    if (op == "confirm") {
        if (s.contains("flag")) w.SetFlag(s["flag"].get<string>());
        view.ask = false;
        return;
    }
    if (op == "fx" && s.value("kind", string("")) == "shadow") return;
    if (op == "walk") {
        // Begun and finished at once: the walk's own reckoning of where it ends.
        Begin(s, w, q, ctx);
        for (Tween& t : run.tweens) t.t = t.time;
        Tick(0.0f, w);
        if (who == "player") { w.player.scene_clip.clear(); return; }
        if (Npc* n = Who(who, w)) {
            float x = 0, y = 0;
            if (!s.contains("toward") && PointOf(s, w, x, y)) { n->x = x; n->y = y; }
            n->StopWalking();
        }
        return;
    }
    if (op == "fx") {
        const string kind = s.value("kind", string(""));
        Npc* n = Who(who, w);
        if (kind == "vanish" && n) { n->dissolve = 1.0f; n->alpha = 0.0f; return; }
        if (kind == "appear" && n) { n->dissolve = 0.0f; n->alpha = 1.0f; return; }
        if (kind == "veil") { w.SetReverieVeil(s.value("to", 1.0f)); return; }
        if (kind == "dream" || kind == "tear") { Begin(s, w, q, ctx); return; }
        return;
    }
    if (op == "timelapse") {
        double now = w.clock.GameHours();
        double to = std::floor(now / 24.0) * 24.0 + s.value("to", 7.0f);
        while (to <= now) to += 24.0;
        to += 24.0 * s.value("days", 0);
        const int day = static_cast<int>(std::floor(to / 24.0));
        w.clock.Set(day, static_cast<float>(to - day * 24.0));
        return;
    }
    // Everything else is done the moment it begins: begin it, and finish
    // whatever tween that started.
    Begin(s, w, q, ctx);
    for (Tween& t : run.tweens) t.t = t.time;
    Tick(0.0f, w);
}

void StoryDirector::Skip(World& w, QuestLog& q, const GameContext& ctx) {
    if (!Running()) return;
    run.skipping = true;
    for (Tween& t : run.tweens) t.t = t.time;
    Tick(0.0f, w);
    for (auto& n : w.npcs) if (n->scripted) n->StopWalking();
    view.line = view.note = false;
    view.title_alpha = 0.0f;
    // The rest of this scene and everything it leads straight on to.
    for (int guard = 0; guard < 32 && run.scene; ++guard) {
        const json& steps = run.scene->steps;
        size_t i = run.step + (run.begun ? 0 : 0);
        for (; i < steps.size(); ++i) Instant(steps[i], w, q, ctx);
        run.step = steps.size();
        Finish(w, q, ctx);
        if (!run.scene) break;
        run.skipping = true;
    }
}

void StoryDirector::Stop(World& w) {
    if (!Running()) return;
    if (run.zoom0 > 0.0f) w.camera.SetZoom(run.zoom0);
    for (const string& a : run.spawned) Remove(a, w);
    for (auto& n : w.npcs) n->scripted = false;
    w.cam_hold.on = false;
    w.player.input_locked = false;
    w.player.scene_clip.clear();
    w.player.scene_hidden = false;
    w.player.scene_lift = 0.0f;
    w.player.sort_bias = 0.0f;
    run = Run{};
    view = StoryView{};
}

void StoryDirector::Finish(World& w, QuestLog& q, const GameContext& ctx) {
    const Scene* next = run.scene && !run.scene->then.empty() ? Find(run.scene->then) : nullptr;
    const bool skipping = run.skipping;
    view.line = view.note = false;
    if (next) {
        // Straight on: the actors, the camera and the player stay as they are.
        StartScene(next, w, q, ctx, skipping);
        return;
    }
    for (const string& a : run.spawned) Remove(a, w);
    for (const string& id : run.held)
        if (Npc* n = w.FindNpc(id)) { n->scripted = false; n->StopWalking(); n->Hold(""); }
    if (run.zoom0 > 0.0f) w.camera.SetZoom(run.zoom0);
    w.cam_hold.on = false;
    w.player.input_locked = false;
    w.player.scene_clip.clear();
    w.player.scene_hidden = false;
    w.player.scene_lift = 0.0f;
    w.player.sort_bias = 0.0f;
    // Whoever the scene let go of stands as the story has them now.
    w.SettleStory(true);
    run = Run{};
    view.in_scene = false;
    view.skip = 0.0f;
}
