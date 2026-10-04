#pragma once
#include "../headers.h"
#include "map.h"

class World;
class Npc;
class QuestLog;
struct GameContext;

// =============================================================================
//  The story: scenes, and what sets them off
//
//  data/story.json holds the prologue -- and whatever acts come after it -- as
//  scenes: a list of steps (a line said, a walk, a pan, a fade, a flag set, a
//  quest begun) and what starts one ("on": a new game, walking into a map,
//  coming near a mark, talking to somebody, using one of the story's things, a
//  flag being set) and when it may ("when": flags, as FlagCond has them). A
//  scene can be followed straight on by another ("then") without the player
//  being handed back in between.
//
//  While a scene runs the player's hands are its: the HUD goes, the bars come
//  in, and the world goes on around them the way the scene says -- people walk,
//  the clock turns, the camera looks where it is told. Held, the skip button
//  ends the scene where it would have ended, everything it would have done
//  done: the flags, the quests, the things given, where everybody stands.
//
//  The director is the world's side of it and knows nothing of Game, so the
//  self-test can play a whole scene headless: it says what the screen should
//  show (View), Game draws that, and Game tells it when a line has been read
//  on and when the skip button is down.
// =============================================================================

// A sign for something the game has just let the player do: said once, the
// moment it is new -- a meal to eat, a stone to wake by, what an Echo is.
struct StoryTip { string id, title, text; };

// What a scene puts on the screen, for Game to draw.
struct StoryView {
    bool   in_scene = false;
    float  bars = 0.0f;                    // the letterbox, 0..1
    SDL_Color fade_colour{0, 0, 0, 255};
    float  fade = 0.0f;                    // over everything but the title card
    // A line: said by somebody, or only seen (narration, in italics).
    bool   line = false, narration = false;
    string speaker, text;
    float  line_age = 0.0f;
    // A note held up to read.
    bool   note = false;
    string note_title, note_text;
    // The title card.
    float  title_alpha = 0.0f;
    string title, subtitle;
    // How far the skip button has been held, 0..1.
    float  skip = 0.0f;
    // A question put to the player, yes or no: Game asks it (GameState::Ask)
    // and gives the answer back with StoryDirector::Answer.
    bool   ask = false;
    string ask_text, ask_yes, ask_no;
};

class StoryDirector {
public:
    bool Load(const string& path);
    size_t SceneCount() const { return scenes.size(); }
    // The skills the story keeps from the player until it gives them.
    const vector<SkillLock>& Locks() const { return locks; }
    bool HasScene(const string& id) const { return Find(id) != nullptr; }

    // Every frame of play: starts a scene whose time has come (when none is
    // running) and runs the one that is. `confirm` is the read-on button,
    // pressed this frame; `skip_held` is the skip button, down.
    void Update(float dt, World& w, QuestLog& q, const GameContext& ctx, bool confirm, bool skip_held);
    // Starts this scene now, whatever its trigger -- a new game's opening, a
    // dev flag. False if there is no such scene or one is running.
    bool Start(const string& id, World& w, QuestLog& q, const GameContext& ctx);
    // Somebody spoken to, or one of the story's things used: true when a scene
    // took it, and nothing else should come of it.
    bool OnTalk(const string& npc_id, World& w, QuestLog& q, const GameContext& ctx);
    bool OnUse(const string& object_id, World& w, QuestLog& q, const GameContext& ctx);

    bool Running() const { return run.scene != nullptr; }
    // The answer to the question the running scene asked (StoryView::ask).
    void Answer(bool yes) { run.answer = yes ? 1 : 0; view.ask = false; }
    string Current() const;
    // Ends the running scene where it would have ended, everything done.
    void Skip(World& w, QuestLog& q, const GameContext& ctx);
    // Leaves it where it is, undone: the game is being left.
    void Stop(World& w);
    // A new game or a load: what was walked into and stood near is forgotten.
    void Reset();

    const StoryView& View() const { return view; }
    vector<StoryTip> TakeTips() { vector<StoryTip> out; out.swap(tip_queue); return out; }
    const StoryTip* FindTip(const string& id) const;
    // What somebody of the story's is called now: a name a flag has changed.
    string ActorName(const string& id, const World& w) const;

    // Everything the self-test asks about.
    struct Scene {
        string id, on, map, at, then;
        float  radius = 48.0f;
        FlagCond when;
        // Something the player has to be carrying for it: the Dreamcatcher.
        string needs;
        json   steps = json::array();
    };
    const vector<Scene>& Scenes() const { return scenes; }
    const Scene* Find(const string& id) const;

private:
    vector<SkillLock> locks;
    struct ActorDef {
        string sprite, image, name;
        vector<std::pair<FlagCond, string>> named;
        float scale = 1.0f;
        int   image_from = 0;      // the picture from this row down
    };
    // Something going on while the steps go on: a fade, a pan, a time-lapse.
    struct Tween {
        enum Kind { Fade, Bars, Veil, Cam, Clock, Alpha, Dissolve, PlayerMove, Sink } kind = Fade;
        string who;
        float  from[2] = {0, 0}, to[2] = {0, 0};
        float  time = 0.0f, t = 0.0f;
        string clip;          // PlayerMove: walked in this
        float  zoom_from = 0.0f, zoom_to = 0.0f;   // Cam: and the zoom, when it changes
    };
    struct Run {
        const Scene* scene = nullptr;
        size_t step = 0;
        bool   begun = false;
        float  step_t = 0.0f;
        bool   skipping = false;
        vector<Tween>  tweens;
        vector<string> spawned;          // actors to take away at the end
        vector<string> held;             // the map's own people the scene has hold of
        vector<string> crowd;            // and those of them a "crowd" step stopped in the street
        string cam_follow;               // the camera kept on somebody
        bool   attached = false;         // the player laid on an actor (the cart)
        string attach_to;
        float  attach_dx = 0.0f, attach_dy = 0.0f;
        float  title_time = 0.0f;
        float  zoom0 = 0.0f;             // the camera's zoom before the scene changed it
        double saved_hours = -1.0;       // the clock as it was before a flashback
        int    answer = -1;              // a question's: -1 not yet, 0 no, 1 yes
    };
    bool  Carries(const Scene& s, const World& w) const;

    bool StartScene(const Scene* s, World& w, QuestLog& q, const GameContext& ctx, bool skipping);
    void Advance(float dt, World& w, QuestLog& q, const GameContext& ctx, bool confirm);
    // Starts step `s`; true when it is done at once.
    bool Begin(const json& s, World& w, QuestLog& q, const GameContext& ctx);
    // Whether the step begun is done.
    bool Done(const json& s, World& w, bool confirm);
    // The step's end, at once: for a skip.
    void Instant(const json& s, World& w, QuestLog& q, const GameContext& ctx);
    void Tick(float dt, World& w);
    void Finish(World& w, QuestLog& q, const GameContext& ctx);
    void CheckTriggers(World& w, QuestLog& q, const GameContext& ctx);

    bool  Point(const json& v, const World& w, float& x, float& y) const;
    bool  PointOf(const json& s, const World& w, float& x, float& y) const;   // "at" and its dx, dy
    Npc* Who(const string& id, World& w);
    Npc* Spawn(const string& actor, float x, float y, World& w, const GameContext& ctx);
    void  Remove(const string& actor, World& w);
    void  Hold(const string& id, World& w);
    void  PlaceAttached(World& w);
    void  SetFade(const json& s, float& from, float& to, SDL_Color& colour) const;
    float CamX(const World& w) const;
    float CamY(const World& w) const;

    vector<Scene> scenes;
    map<string, ActorDef> actors;
    map<string, StoryTip> tips;
    Run run;
    StoryView view;
    vector<StoryTip> tip_queue;
    string seen_map;                     // the map an "enter" was last looked for on
    float  skip_held_for = 0.0f;
    bool   skip_released = true;         // a skip needs the button let go between scenes
};
