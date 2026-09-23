#include "../game.h"
#include "../systems/shaders.h"

// =============================================================================
//  Split screen: two players at one machine
// =============================================================================
//
// Player Two is a seat in the same realm friends across the wire sit in (see
// coop/coop.h), with no wire: their hands are read from a second controller
// each frame, their character stands in the world itself, and their journal is
// a real one. So everything co-op does, the couch does -- the same monsters,
// shared chests, each to their own bag and journal, going separate ways across
// maps, one night for both -- and it works beside friends online, who see
// Player Two like anyone else.
//
// The game serves one seat at a time. Everything in Game is written for "the
// player": `world->player`, `quests`, `input`. ServeSeat(1) points those at
// Player Two -- the world they are on, acting as them; their journal; their
// controller -- and everything written for the player works for them: their
// half of the screen is drawn by the same Render and the same HUD, and their
// bag, skills, journal, shops and conversations are the same panels. A panel
// takes the whole screen and stops the game for both, as a panel always has.
//
// Each half is drawn into a texture of its own size and then placed, rather
// than through a viewport on the window: the night's light map, the dream's
// stars and the HUD all ask how big the output is, and this way the answer is
// the half they are drawing.

namespace {

const char* kLooks[] = {"player_hero", "player_warden", "player_wayfarer"};

string LookName(const string& look) {
    if (look == "player_warden")   return "the warden";
    if (look == "player_wayfarer") return "the wayfarer";
    return "the hero";
}

} // namespace

// -----------------------------------------------------------------------------
//  Serving a seat
// -----------------------------------------------------------------------------

void Game::ServeSeat(int seat) {
    if (seat == serving) return;
    if (serving == 1) {
        world->EndActing();
        world = &home_world;
        quests = &own_quests;
        input.Borrow(nullptr);
        serving = 0;
    }
    if (seat == 1 && split_active) {
        World* theirs = coop_host.WorldOf(p2_seat);
        Player* who = theirs ? theirs->Guest(p2_seat) : nullptr;
        if (theirs && who) {
            world = theirs;
            world->BeginActing(*who);
            quests = &quests_two;
            input.Borrow(&input_two);
            serving = 1;
        }
    }
    ctx.quests = quests;
}

net::Server& Game::RealmServer() {
    return session.Hosting() && session.Hosted() ? *session.Hosted() : offline_server;
}

// -----------------------------------------------------------------------------
//  Joining and leaving
// -----------------------------------------------------------------------------

string Game::PlayerTwoPath() const {
    return coop::CharacterPath(characters_dir, net::CleanLine(settings.p2_name, net::MAX_NAME), "", true);
}

bool Game::JoinSplit(bool without_a_controller) {
    if (split_active) return true;
    if (!has_session || guest_session) {
        PushToast("Player Two joins a game that is running: start or load one first.", Palette::TextDim);
        return false;
    }
    const int pads = Input::ConnectedPads();
    if (pads == 0 && !without_a_controller) {
        PushToast("Player Two needs a controller. Plug one in and try again.", {235, 150, 120, 255});
        return false;
    }

    string name = net::CleanLine(settings.p2_name, net::MAX_NAME);
    if (name.empty()) name = "Player Two";
    string look = settings.p2_look;
    if (!sprites.Has(look)) look = world->player.sprite_id == "player_warden" ? "player_hero" : "player_warden";

    // Their own character, if they have sat here before.
    coop::Character kept;
    json character;
    quests_two.FromJson(json::object());
    p2_flags.clear();
    if (!never_save && coop::LoadCharacter(PlayerTwoPath(), kept)) {
        character = kept.player;
        quests_two.FromJson(kept.quests);
        p2_flags = kept.flags;
        look = kept.player.value("sprite", look);
    }

    const int seat = RealmServer().ReserveSeat(name, look);
    if (seat < 0) {
        PushToast("Every seat in this world is taken.", {235, 150, 120, 255});
        return false;
    }
    p2_seat = static_cast<uint8_t>(seat);
    coop_host.AddLocal(p2_seat, name, look, &quests_two, character);
    quests_two.SetDay(home_world.clock.QuestDay());

    // One keyboard and one controller: the controller is Player Two's. Two
    // controllers: one each, and the keyboard stays with Player One.
    input.SetDevices(true, pads >= 2 ? 0 : -1, true);
    input_two.SetDevices(false, pads >= 2 ? 1 : (pads == 1 ? 0 : -1), true);

    split_active = true;
    p2_arrived = false;
    LayoutViews();
    PushToast(name + " joins, as " + LookName(look) + ".", Palette::Highlight);
    return true;
}

void Game::SavePlayerTwo() {
    if (!split_active || never_save) return;
    const Player* who = coop_host.PlayerOf(p2_seat);
    World* theirs = coop_host.WorldOf(p2_seat);
    if (!who || !theirs) return;
    coop::Character c;
    c.player = who->ToJson();
    c.quests = quests_two.ToJson();
    for (const string& key : theirs->SeatOf(p2_seat).private_flags) c.flags.push_back(key);
    c.storage = json::object();
    c.playtime = playtime;
    if (!coop::SaveCharacter(PlayerTwoPath(), c))
        PushToast("Could not write Player Two's character file.", {235, 120, 120, 255});
}

void Game::LeaveSplit() {
    if (!split_active) return;
    ServeSeat(0);
    SavePlayerTwo();
    coop_host.RemoveLocal(p2_seat);
    RealmServer().ReleaseSeat(p2_seat);
    split_active = false;
    input.SetDevices(true, 0, false);
    input_two.SetDevices(false, -1, true);
    if (!session.Hosting()) coop_host.Reset(home_world);
    LayoutViews();
}

// -----------------------------------------------------------------------------
//  The frame
// -----------------------------------------------------------------------------

void Game::LayoutViews() {
    const float w = static_cast<float>(screen_w), h = static_cast<float>(screen_h);
    if (!split_active) {
        view_rect[0] = {0.0f, 0.0f, w, h};
        view_rect[1] = {0.0f, 0.0f, 0.0f, 0.0f};
    } else if (settings.split_stacked) {
        view_rect[0] = {0.0f, 0.0f, w, floorf(h / 2.0f) - 1.0f};
        view_rect[1] = {0.0f, floorf(h / 2.0f) + 1.0f, w, h - floorf(h / 2.0f) - 1.0f};
    } else {
        view_rect[0] = {0.0f, 0.0f, floorf(w / 2.0f) - 1.0f, h};
        view_rect[1] = {floorf(w / 2.0f) + 1.0f, 0.0f, w - floorf(w / 2.0f) - 1.0f, h};
    }
    home_world.camera.SetViewport(view_rect[0].w, view_rect[0].h);
    home_world.camera.SetZoom(settings.zoom);
    // The textures the halves are drawn into are remade at the new size.
    for (SDL_Texture*& t : view_texture) { if (t) SDL_DestroyTexture(t); t = nullptr; }
}

void Game::UpdatePlayerTwo(float dt) {
    (void)dt;
    if (!split_active) return;
    // One panel at a time: if Player One has just opened theirs, wait.
    if (state != GameState::Play) return;
    World* theirs = coop_host.WorldOf(p2_seat);
    Player* who = theirs ? theirs->Guest(p2_seat) : nullptr;
    if (!theirs || !who) return;          // not arrived yet: the realm seats them on its next step

    if (!p2_arrived) {
        p2_arrived = true;
        for (const string& key : p2_flags) if (World::PrivateFlag(key)) theirs->SeatOf(p2_seat).private_flags.insert(key);
    }
    // Their camera is their half of the screen.
    SeatState& seat = theirs->SeatOf(p2_seat);
    seat.camera.SetViewport(view_rect[1].w, view_rect[1].h);
    seat.camera.SetZoom(settings.zoom);

    // Their hands, for the realm's next step.
    coop_host.FeedLocal(p2_seat, PlayerInput::FromDevice(input_two));

    // And everything the game does for a player each frame, done for them:
    // what the world asked to be opened, what levelled, what the journal
    // noted, and the buttons that open their panels.
    quests_two.SetDay(home_world.clock.QuestDay());
    ServeSeat(1);
    if (serving == 1) {
        HandleWorldRequests();
        SeatChores();
        // A panel of theirs stays theirs until it closes.
        if (state == GameState::Play) ServeSeat(0);
    }
}

void Game::RenderSplit() {
    const int panel_seat = serving;
    SDL_Texture* window_target = SDL_GetRenderTarget(renderer);
    for (int seat = 0; seat < 2; ++seat) {
        const SDL_FRect& rect = view_rect[seat];
        const int w = std::max(1, static_cast<int>(rect.w)), h = std::max(1, static_cast<int>(rect.h));
        if (!view_texture[seat]) {
            view_texture[seat] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            if (view_texture[seat]) SDL_SetTextureScaleMode(view_texture[seat], SDL_SCALEMODE_NEAREST);
        }
        if (!view_texture[seat]) continue;

        ServeSeat(seat);
        if (serving != seat) {
            // Player Two has not arrived, or is between maps: their half waits.
            SDL_SetRenderTarget(renderer, view_texture[seat]);
            SDL_SetRenderDrawColor(renderer, 16, 13, 18, 255);
            SDL_RenderClear(renderer);
            continue;
        }
        SDL_SetRenderTarget(renderer, view_texture[seat]);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ui.SetViewport(rect.w, rect.h);
        world->camera.SetViewport(rect.w, rect.h);
        const SDL_FPoint shake = world->ShakeOffset();
        world->camera.xpos += shake.x;
        world->camera.ypos += shake.y;
        SDL_Texture* scene = Shaders::BeginView(renderer, world->CurrentMap(), world->camera);
        if (scene) SDL_SetRenderTarget(renderer, scene);
        world->Render(renderer, *textures);
        if (scene) {
            SDL_SetRenderTarget(renderer, view_texture[seat]);
            Shaders::DrawPost(renderer, scene);
        }
        DrawNameTags();
        DrawWorldText();
        world->camera.xpos -= shake.x;
        world->camera.ypos -= shake.y;
        DrawHud();
        if (world->player.Fallen())
            ui.TextShadowed("You have fallen", rect.w / 2.0f, rect.h * 0.4f, TextSize::Large, {235, 120, 120, 255}, Align::Center);
        else if (world->player.resting)
            ui.TextShadowed("Abed. Move to get up.", rect.w / 2.0f, rect.h * 0.4f, TextSize::Body, {200, 206, 240, 255}, Align::Center);
    }
    ServeSeat(panel_seat);

    SDL_SetRenderTarget(renderer, window_target);
    SDL_SetRenderDrawColor(renderer, 10, 8, 10, 255);
    SDL_RenderClear(renderer);
    for (int seat = 0; seat < 2; ++seat)
        if (view_texture[seat]) SDL_RenderTexture(renderer, view_texture[seat], nullptr, &view_rect[seat]);
    ui.SetViewport(static_cast<float>(screen_w), static_cast<float>(screen_h));
}

// -----------------------------------------------------------------------------
//  The pause menu's rows for it
// -----------------------------------------------------------------------------

string Game::SplitRowLabel() const {
    if (split_active) return "Player Two leaves";
    return "Player Two joins   < " + LookName(settings.p2_look) + " >";
}

void Game::CycleSplitLook(int step) {
    int at = 0;
    for (int i = 0; i < 3; ++i) if (settings.p2_look == kLooks[i]) at = i;
    at = ((at + step) % 3 + 3) % 3;
    settings.p2_look = kLooks[at];
    settings.Save();
    Audio::Play(Sfx::UiMove);
}
