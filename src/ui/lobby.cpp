#include "../game.h"

// =============================================================================
//  Play Together
// =============================================================================
//
// Milestone 0 of the co-op plan: the screen that hosts a world or joins one,
// says who is connected, and carries a typed line to the other screen. No
// world is shared yet -- a host who goes off to play is playing alone, with
// the door open behind them -- but everything a friend on the tailnet needs
// to reach this machine, and everything that can go wrong on the way, is
// here to be seen.
//
// Typing is the one thing in the game that is not an Action. While a field is
// being typed into, key presses go to it instead of to the input map, or J
// would confirm, K would back out and WASD would walk the cursor away in the
// middle of a name.

namespace {

enum Row { ROW_NAME = 0, ROW_LOOK, ROW_PASS, ROW_HOST, ROW_JOIN, ROW_SAY, ROW_BACK, ROW_COUNT };

constexpr size_t kAddressLimit = 64;

// Drops the last whole UTF-8 character.
void PopCharacter(string& s) {
    if (s.empty()) return;
    size_t at = s.size() - 1;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) --at;
    s.erase(at);
}

string LookLabel(const string& look) {
    if (look == "player_hero")     return "the hero";
    if (look == "player_warden")   return "the warden";
    if (look == "player_wayfarer") return "the wayfarer";
    return "";
}

const SDL_Color kGood{126, 196, 122, 255};
const SDL_Color kBad {235, 140, 120, 255};

} // namespace

// -----------------------------------------------------------------------------
//  Text entry
// -----------------------------------------------------------------------------

void Game::BeginTextEntry(string* target, size_t limit) {
    text_target = target;
    text_limit = limit;
    text_commit = text_cancel = false;
    text_nav = 0;
    if (window) SDL_StartTextInput(window);
}

void Game::EndTextEntry() {
    text_target = nullptr;
    text_commit = text_cancel = false;
    text_nav = 0;
    if (window) SDL_StopTextInput(window);
}

bool Game::TextEntryEvent(const SDL_Event& e) {
    if (!text_target) return false;

    const auto append = [&](const char* utf8) {
        // Whole characters or none: never half of one at the limit.
        for (const char* p = utf8; *p;) {
            size_t n = 1;
            const unsigned char lead = static_cast<unsigned char>(*p);
            if      ((lead & 0xE0) == 0xC0) n = 2;
            else if ((lead & 0xF0) == 0xE0) n = 3;
            else if ((lead & 0xF8) == 0xF0) n = 4;
            size_t have = 0;
            while (have < n && p[have]) ++have;
            if (have < n || text_target->size() + n > text_limit) return;
            if (lead >= 0x20 && lead != 0x7F) text_target->append(p, n);
            p += n;
        }
    };

    switch (e.type) {
        case SDL_EVENT_TEXT_INPUT:
            append(e.text.text);
            return true;

        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_BACKSPACE: PopCharacter(*text_target); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  if (!e.key.repeat) text_commit = true; break;
                case SDLK_ESCAPE:    text_cancel = true; break;
                case SDLK_UP:        text_nav = -1; break;
                case SDLK_DOWN:
                case SDLK_TAB:       text_nav = 1; break;
                case SDLK_V:
                    // An address copied out of the Tailscale window.
                    if (e.key.mod & SDL_KMOD_CTRL) {
                        if (char* clip = SDL_GetClipboardText()) {
                            string first(clip);
                            SDL_free(clip);
                            first = first.substr(0, first.find_first_of("\r\n"));
                            append(net::CleanLine(first, text_limit).c_str());
                        }
                    }
                    break;
                default: break;
            }
            // Swallowed either way: a key typed into a field is not an action.
            return true;

        // Releases go through, so a key that was down when typing began is
        // not left held in the input map for ever.
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
//  The session, from the game's side
// -----------------------------------------------------------------------------

net::Session::Identity Game::NetIdentity() const {
    net::Session::Identity who;
    who.name = net::CleanLine(settings.player_name, net::MAX_NAME);
    if (who.name.empty()) who.name = "Traveller";
    who.look = has_session ? world.player.sprite_id : pending_character;
    who.password = mp_password;
    return who;
}

bool Game::StartHosting(uint16_t port) {
    net::Session::Identity who = NetIdentity();
    session.bring_your_own = settings.bring_your_own;
    if (settings.host_password != mp_password) { settings.host_password = mp_password; settings.Save(); }
    mp_error.clear();
    if (!session.Host(port, who, who.name + "'s Hollowmarch", mp_error)) {
        mp_error = "Could not host: " + mp_error + ".";
        return false;
    }
    // Worked out once, when hosting starts: what a friend should type.
    mp_hostname = net::LocalHostName();
    mp_addresses = net::LocalAddresses();
    return true;
}

bool Game::StartJoining(const string& address) {
    mp_error.clear();
    net::Session::Identity who = NetIdentity();
    // A character kept from another day arrives as who they are, whatever
    // the Character row says.
    {
        coop::Character kept;
        if (coop::LoadCharacter(coop::CharacterPath(characters_dir, who.name, "", true), kept))
            who.look = kept.player.value("sprite", who.look);
    }
    if (!session.Join(address, who, mp_error)) return false;
    // Remembered whether or not anyone answers: a mistyped name is one
    // Backspace from right, and the last five are kept.
    const string clean = net::CleanLine(address, kAddressLimit);
    auto& recent = settings.recent_hosts;
    recent.erase(std::remove(recent.begin(), recent.end(), clean), recent.end());
    recent.insert(recent.begin(), clean);
    if (recent.size() > 5) recent.resize(5);
    settings.Save();
    return true;
}

// -----------------------------------------------------------------------------
//  The world, shared
// -----------------------------------------------------------------------------

void Game::UpdateCoop(float dt) {
    if (session.Hosting() && session.Hosted()) {
        // A game that is never saved keeps nothing of anyone's.
        if (never_save) coop_host.kept_dir.clear();
        coop_host.Update(dt, *session.Hosted(), world, ctx, has_session && !guest_session);
        return;
    }
    if (!world.guests.empty() && !guest_session && !session.Active()) coop_host.Reset(world);

    const bool guest = session.As() == net::Session::Role::Guest;
    if (guest) {
        coop_guest.Update(dt, session.Me(), world, ctx);
        if (coop_guest.HasEnter()) {
            const net::Enter enter = coop_guest.PendingEnter();
            if (enter.map.empty()) EndGuestSession("The host has left the world. You are still seated.");
            else                   EnterAsGuest(enter);
        }
    } else if (guest_session) {
        // The line is gone: the reason is on the Play Together screen.
        EndGuestSession(session.Me().Reason());
    }
}

string Game::GuestCharacterPath() const {
    return coop::CharacterPath(characters_dir, net::CleanLine(settings.player_name, net::MAX_NAME),
                               session.Me().WorldName(), session.Me().BringYourOwn());
}

void Game::SaveGuestCharacter() {
    if (!guest_session || never_save) return;
    coop::Character c;
    c.player = world.player.ToJson();
    c.quests = quests.ToJson();
    for (const string& key : world.Flags()) if (coop::PrivateFlag(key)) c.flags.push_back(key);
    c.storage = json::object();
    for (const auto& kv : world.Storages()) {
        const json slots = kv.second.ToJson();
        bool any = false;
        for (const json& sl : slots) any = any || !sl.is_null();
        if (any) c.storage[kv.first] = slots;
    }
    c.playtime = playtime;
    if (!coop::SaveCharacter(GuestCharacterPath(), c))
        PushToast("Could not write your character file.", {235, 120, 120, 255});
}

void Game::EnterAsGuest(const net::Enter& enter) {
    if (!guest_session) {
        // Their own character, from their own machine, if they have been out
        // before; otherwise a new one, dressed as one.
        banner_active = false;
        banner_zone.clear();
        banner_seen_map.clear();
        quests.FromJson(json::object());
        world.SetFlags({});
        world.SetCamp({});
        world.SetDream({});
        world.SetPickedHerbs({});
        world.shops.Clear();
        world.guests.clear();
        world.visiting = true;
        world.SetStorages({});
        world.player = Player();
        playtime = 0.0f;
        coop::Character kept;
        if (!never_save && coop::LoadCharacter(GuestCharacterPath(), kept)) {
            world.player.Init(ctx, kept.player.value("sprite", pending_character));
            world.player.FromJson(kept.player, ctx);
            quests.FromJson(kept.quests);
            for (const string& key : kept.flags) if (coop::PrivateFlag(key)) world.SetFlag(key);
            map<string, Inventory> chests;
            if (kept.storage.is_object())
                for (auto it = kept.storage.begin(); it != kept.storage.end(); ++it) {
                    if (!it.value().is_array()) continue;
                    Inventory inv(nullptr, static_cast<int>(it.value().size()));
                    inv.FromJson(it.value());
                    chests.emplace(it.key(), std::move(inv));
                }
            world.SetStorages(std::move(chests));
            playtime = kept.playtime;
            PushToast("Welcome back, " + settings.player_name + ".", Palette::Xp);
        } else {
            world.player.Init(ctx, pending_character);
            const vector<string> kit = Player::StartingKit(pending_character);
            world.player.inventory.Add("coins", 25);
            for (const string& id : kit) world.player.inventory.Add(id, 1);
            world.player.inventory.Add("cooked_meat", 3);
            world.SetFlag("starter_tools");
            string why;
            for (const string& worn : kit)
                for (int slot = 0; slot < world.player.inventory.SlotCount(); ++slot)
                    if (world.player.inventory.Slot(slot).id == worn)
                        world.player.EquipFromInventory(slot, why);
        }
        autosave_timer = 0.0f;
        quest_day_seen = -1;
        welcome_pending = false;
    }
    if (!world.LoadMap(enter.map, "", ctx)) {
        session.Leave();
        EndGuestSession("The host is somewhere this game has no map of.");
        return;
    }
    world.player.x = enter.x;
    world.player.y = enter.y;
    world.camera.SnapTo(enter.x, enter.y);
    coop_guest.Arrived(world);
    // Up from the ground, or from a dream: the screen that was waiting goes.
    if (guest_session && state == GameState::Death) SetState(GameState::Play);
    if (!guest_session) {
        guest_session = true;
        has_session = true;
        EndTextEntry();
        SetState(GameState::Play);
        PushToast("You are in " + session.Me().WorldName() + ".", Palette::Highlight);
    }
}

void Game::EndGuestSession(const string& why) {
    SaveGuestCharacter();
    coop_guest.Reset(world);
    world.visiting = false;
    world.player.hands_external = false;
    if (!guest_session) return;
    guest_session = false;
    has_session = false;
    SetState(GameState::MainMenu);
    if (!why.empty()) mp_error = why;
    OpenMultiplayer();
}

void Game::DrawNameTags() {
    // Who that is. Over friends only: you know who you are.
    for (const auto& g : world.guests) {
        if (g->name.empty()) continue;
        const SDL_FPoint p = world.camera.ToScreen(g->x, g->y - g->draw_lift - 58.0f);
        ui.TextShadowed(g->name, p.x, p.y, TextSize::Small, {214, 232, 255, 255}, Align::Center);
    }
}

void Game::DrawParty() {
    // Who else is in the realm, and how they are doing: a name and a bar each,
    // under the vitals. A friend on another map is named, and the map.
    if (!session.Me().Seated() || session.Me().Roster().size() < 2) return;
    struct Row { string name, where; int hp = 0, max_hp = 0; bool resting = false; };
    vector<Row> rows;
    if (session.Hosting() && session.Hosted()) {
        for (const coop::Host::Member& m : coop_host.Party(world, *session.Hosted())) {
            if (m.seat == session.Me().Seat()) continue;
            rows.push_back({m.name, m.map == world.MapId() ? string() : m.map, m.hp, m.max_hp, m.resting});
        }
    } else {
        for (const net::SeatInfo& info : session.Me().Roster()) {
            if (info.seat == session.Me().Seat()) continue;
            const Player* g = world.Guest(info.seat);
            rows.push_back({info.name, g ? string() : string("elsewhere"), g ? g->hp : 0, g ? g->max_hp : 0, false});
        }
    }
    float y = 128.0f;
    for (const Row& r : rows) {
        ui.TextShadowed(r.name, 20.0f, y, TextSize::Small, {214, 232, 255, 255});
        if (r.max_hp > 0 && r.where.empty()) {
            ui.Bar({20.0f, y + 18.0f, 120.0f, 7.0f}, std::clamp(static_cast<float>(r.hp) / r.max_hp, 0.0f, 1.0f),
                   Palette::Health, Palette::HealthBack);
            if (r.resting) ui.TextShadowed("abed", 146.0f, y + 12.0f, TextSize::Small, Palette::TextDim);
        } else {
            string where = r.where;
            for (char& c : where) if (c == '_') c = ' ';
            ui.TextShadowed(where.empty() ? string("...") : where, 20.0f, y + 14.0f, TextSize::Small, Palette::TextDim);
        }
        y += 34.0f;
    }
}

void Game::UpdateSession(float dt) {
    session.Update(dt);
    // Away from the lobby, what is said arrives as a toast, so a host out in
    // the meadow still hears a friend knocking.
    for (const net::Client::ChatLine& line : session.Me().TakeNewLines()) {
        if (state == GameState::Multiplayer) continue;
        PushToast(line.name.empty() ? line.text : line.name + ": " + line.text,
                  line.name.empty() ? Palette::TextDim : Palette::Text);
    }
}

// -----------------------------------------------------------------------------
//  The screen
// -----------------------------------------------------------------------------

void Game::OpenMultiplayer() {
    mp_name = settings.player_name;
    if (mp_password.empty()) mp_password = settings.host_password;
    if (mp_address.empty() && !settings.recent_hosts.empty()) mp_address = settings.recent_hosts.front();
    OpenPanel(GameState::Multiplayer);
    // On the row that is the next thing to do. With a session under way that
    // is talking -- and never the row that has just become "Leave" or "Stop
    // hosting", one stray press from hanging up on everyone.
    cursor = session.Active() ? ROW_SAY : ROW_HOST;
}

void Game::UpdateMultiplayer() {
    const bool seated = session.Me().Seated();
    const auto leave_screen = [&] {
        EndTextEntry();
        ClosePanel();
    };

    // --- typing ------------------------------------------------------------------
    if (text_target) {
        const bool commit = text_commit, cancel = text_cancel;
        const int nav = text_nav;
        text_commit = text_cancel = false;
        text_nav = 0;

        if (cursor == ROW_SAY && commit) {
            // Enter sends and keeps typing: a conversation is more than a line.
            session.Me().Say(mp_say);
            mp_say.clear();
            return;
        }
        if (commit || cancel || nav != 0) {
            const int row = cursor;
            EndTextEntry();
            if (row == ROW_NAME) {
                mp_name = net::CleanLine(mp_name, net::MAX_NAME);
                if (mp_name.empty()) mp_name = settings.player_name;
                if (mp_name != settings.player_name) { settings.player_name = mp_name; settings.Save(); }
            } else if (row == ROW_JOIN && commit && !mp_address.empty()) {
                StartJoining(mp_address);
            }
            if (nav != 0) cursor = std::clamp(cursor + nav, 0, static_cast<int>(ROW_COUNT) - 1);
        }
        return;
    }

    // --- rows --------------------------------------------------------------------
    MoveCursor(cursor, ROW_COUNT);

    // The join row steps through the hosts dialled before, so a controller --
    // which cannot type -- can still rejoin a friend.
    if (cursor == ROW_JOIN && !session.Active() && !settings.recent_hosts.empty() &&
        (input.MenuLeft() || input.MenuRight())) {
        const auto& recent = settings.recent_hosts;
        const int n = static_cast<int>(recent.size());
        int at = 0;
        for (int i = 0; i < n; ++i) if (recent[i] == mp_address) at = i;
        at = ((at + (input.MenuRight() ? 1 : -1)) % n + n) % n;
        mp_address = recent[at];
        Audio::Play(Sfx::UiMove);
    }

    // Who to arrive as. Fixed once there is a world to be in, or a seat.
    if (cursor == ROW_LOOK && !session.Active() && !has_session && (input.MenuLeft() || input.MenuRight())) {
        int at = 0;
        for (int i = 0; i < kCharacterCount; ++i) if (pending_character == kCharacterIds[i]) at = i;
        at = ((at + (input.MenuRight() ? 1 : -1)) % kCharacterCount + kCharacterCount) % kCharacterCount;
        pending_character = kCharacterIds[at];
        Audio::Play(Sfx::UiMove);
    }

    if (input.Pressed(Action::Confirm) || input.Pressed(Action::Interact)) {
        switch (cursor) {
            case ROW_LOOK:
                break;
            case ROW_PASS:
                if (session.Active()) mp_error = "The password is fixed while you are connected.";
                else BeginTextEntry(&mp_password, net::MAX_PASSWORD);
                break;
            case ROW_NAME:
                if (session.Active()) mp_error = "Your name is fixed while you are connected.";
                else BeginTextEntry(&mp_name, net::MAX_NAME);
                break;
            case ROW_HOST:
                if (session.Hosting())     session.Leave();
                else if (session.Active()) mp_error = "Leave the world you have joined first.";
                else                       StartHosting(mp_port);
                break;
            case ROW_JOIN:
                if (session.As() == net::Session::Role::Guest) session.Leave();
                else if (session.Hosting()) mp_error = "Stop hosting first.";
                else if (has_session) mp_error = "Join from the title screen: a guest's game replaces the one running.";
                else BeginTextEntry(&mp_address, kAddressLimit);
                break;
            case ROW_SAY:
                if (seated) BeginTextEntry(&mp_say, net::MAX_CHAT);
                else        mp_error = "Nobody to talk to yet.";
                break;
            case ROW_BACK:
                leave_screen();
                return;
        }
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) leave_screen();
}

void Game::DrawMultiplayer() {
    if (has_session) ui.Dim(0.6f);
    const SDL_FRect panel = {(ui.ViewWidth() - 760.0f) / 2.0f, (ui.ViewHeight() - 560.0f) / 2.0f, 760.0f, 560.0f};
    ui.Panel(panel);
    ui.Text("Play Together", panel.x + panel.w / 2.0f, panel.y + 16.0f, TextSize::Large,
            Palette::Highlight, Align::Center);

    const net::Client& me = session.Me();
    const bool hosting = session.Hosting();
    const bool guest = session.As() == net::Session::Role::Guest;
    const bool blink = std::fmod(state_time, 1.0f) < 0.55f;
    const auto field = [&](const string& value, bool editing, const string& placeholder) {
        if (editing) return value + (blink ? "_" : " ");
        return value.empty() ? placeholder : value;
    };

    // --- the rows ----------------------------------------------------------------
    const float left_w = 330.0f;
    const float row_h = 42.0f;
    const float top = panel.y + 62.0f;
    const string labels[ROW_COUNT] = {
        "Name",
        "Character",
        "Password",
        hosting ? "Stop hosting" : "Host a world",
        guest ? "Leave" : "Join",
        "Say",
        "Back",
    };
    const string values[ROW_COUNT] = {
        field(mp_name, text_target == &mp_name, "Traveller"),
        LookLabel(has_session ? world.player.sprite_id : pending_character),
        text_target == &mp_password ? field(mp_password, true, "") : (mp_password.empty() ? string("none") : string(mp_password.size(), '*')),
        hosting ? "UDP " + std::to_string(session.Port()) : "",
        guest ? session.Address() : field(mp_address, text_target == &mp_address, "a friend's machine"),
        "",
        "",
    };
    const bool enabled[ROW_COUNT] = {
        !session.Active(), !session.Active() && !has_session, !session.Active(), !guest, !hosting && (!has_session || guest), me.Seated(), true,
    };
    for (int i = 0; i < ROW_COUNT; ++i) {
        const SDL_FRect row = {panel.x + 16.0f, top + i * row_h, left_w, row_h - 4.0f};
        ui.MenuItem(row, labels[i], i == cursor, enabled[i], values[i]);
    }

    // What is being typed to the others, under the rows and full width of them.
    const SDL_FRect say_box = {panel.x + 16.0f, top + static_cast<float>(ROW_COUNT) * row_h + 6.0f, left_w, 34.0f};
    if (me.Seated()) {
        ui.Fill(say_box, Palette::PanelLight);
        ui.Outline(say_box, text_target == &mp_say ? Palette::Border : Palette::BorderDim, 1.0f);
        const string shown = field(mp_say, text_target == &mp_say, "");
        // Keep the end of a long line in view rather than the start.
        string tail = shown;
        while (tail.size() > 1 && ui.Measure(tail, TextSize::Small).x > say_box.w - 16.0f) {
            size_t n = 1;
            while (n < tail.size() && (static_cast<unsigned char>(tail[n]) & 0xC0) == 0x80) ++n;
            tail.erase(0, n);
        }
        ui.Text(tail.empty() && text_target != &mp_say ? string("Say, then type a line") : tail,
                say_box.x + 8.0f, say_box.y + 8.0f, TextSize::Small,
                tail.empty() || text_target != &mp_say ? Palette::TextDim : Palette::Text);
    }

    // What the cursor's row does, in a sentence.
    {
        string help;
        switch (cursor) {
            case ROW_NAME: help = session.Active() ? "Your name is fixed while you are connected."
                                                   : "What friends see you as. Up to sixteen characters."; break;
            case ROW_LOOK: help = has_session ? "The character you are playing."
                                : session.Active() ? "Fixed while you are connected."
                                : "Left and right: who you arrive as in a friend's world. Hosting, you are whoever your own game says.";
                           break;
            case ROW_PASS: help = "Hosting: a word friends must give at the door. Joining: the word the host "
                                  "gave you. Leave it empty and the tailnet is the door.";
                           break;
            case ROW_HOST: help = hosting ? "Lets everyone go and closes the door."
                                : guest   ? "Leave the world you have joined first."
                                          : "Opens this machine to friends on your tailnet. Whoever joins walks "
                                            "into the game you are playing, on the map you are on."; break;
            case ROW_JOIN: help = guest   ? "Hangs up."
                                : hosting ? "Stop hosting first."
                                : has_session ? "Join from the title screen: a guest's game replaces the one running."
                                          : "Type the host's machine name from Tailscale, or their 100.x address. "
                                            "Left and right step through the last five."; break;
            case ROW_SAY:  help = me.Seated() ? "Enter sends the line and keeps typing. Esc stops."
                                              : "Nobody to talk to yet."; break;
            default: break;
        }
        ui.TextWrapped(help, panel.x + 20.0f, say_box.y + say_box.h + 12.0f, left_w - 8.0f,
                       TextSize::Small, Palette::TextDim);
    }

    // --- who, where, and what has been said -----------------------------------------
    const float rx = panel.x + 16.0f + left_w + 20.0f;
    const float rw = panel.x + panel.w - 16.0f - rx;
    float y = top;

    // The line that says where things stand.
    {
        string status = "Not connected.";
        SDL_Color colour = Palette::TextDim;
        using S = net::Client::State;
        if (hosting && me.Seated())      { status = "Hosting " + me.WorldName() + "."; colour = kGood; }
        else if (hosting)                { status = "Opening the door..."; }
        else if (me.Where() == S::Connecting) { status = "Dialling " + session.Address() + "..."; colour = Palette::Text; }
        else if (me.Where() == S::Greeting)   { status = "Reached them. Asking for a seat..."; colour = Palette::Text; }
        else if (me.Seated())            { status = "In " + me.WorldName() + "."; colour = kGood; }
        else if (me.Where() == S::Refused || me.Where() == S::Lost) { status = me.Reason(); colour = kBad; }
        if (!mp_error.empty() && !session.Active()) { status = mp_error; colour = kBad; }
        y += ui.TextWrapped(status, rx, y, rw, TextSize::Body, colour) + 8.0f;
    }

    if (hosting) {
        // What a friend types. The machine's name is its MagicDNS name unless
        // it was renamed in the Tailscale admin page; the address always works.
        string how = "Friends join:  ";
        if (!mp_hostname.empty()) how += mp_hostname;
        for (const net::LocalAddress& a : mp_addresses) {
            if (!a.tailnet) continue;
            how += (how.back() == ' ' ? "" : "   or   ") + a.ip;
        }
        bool any_tailnet = false;
        for (const net::LocalAddress& a : mp_addresses) any_tailnet |= a.tailnet;
        y += ui.TextWrapped(how, rx, y, rw, TextSize::Small, Palette::Text) + 2.0f;
        if (!any_tailnet)
            y += ui.TextWrapped("No tailnet address (100.x) on this machine: is Tailscale running?",
                                rx, y, rw, TextSize::Small, kBad) + 2.0f;

        // The reachable light. It turns on the first time anybody from
        // outside gets as far as the door -- refused or not.
        const bool reached = session.Hosted() && session.Hosted()->ReachedFromOutside();
        const SDL_FRect lamp = {rx, y + 5.0f, 10.0f, 10.0f};
        ui.Fill(lamp, reached ? kGood : SDL_Color{70, 62, 52, 255});
        ui.Outline(lamp, Palette::BorderDim, 1.0f);
        y += ui.TextWrapped(reached ? "A friend has reached this machine."
                                    : "Nobody has reached this machine yet. If a friend cannot, allow DreamQuest "
                                      "through Windows Firewall on private networks (UDP " +
                                      std::to_string(session.Port()) + ").",
                            rx + 18.0f, y, rw - 18.0f, TextSize::Small,
                            reached ? kGood : Palette::TextDim) + 8.0f;
    }

    // The seats.
    ui.Text("Who is here", rx, y, TextSize::Small, Palette::Highlight);
    y += 22.0f;
    for (int seat = 0; seat < net::MAX_SEATS; ++seat) {
        const net::SeatInfo* who = nullptr;
        for (const net::SeatInfo& s : me.Roster()) if (s.seat == seat) who = &s;
        const SDL_FRect row = {rx, y, rw, 26.0f};
        ui.Fill(row, seat % 2 ? Palette::Panel : Palette::PanelLight);
        ui.Text(std::to_string(seat + 1), row.x + 8.0f, row.y + 4.0f, TextSize::Small, Palette::TextDim);
        if (who) {
            const bool mine = me.Seated() && who->seat == me.Seat();
            ui.Text(who->name + (who->host ? "  (host)" : ""), row.x + 30.0f, row.y + 4.0f, TextSize::Small,
                    mine ? Palette::Highlight : Palette::Text);
            ui.Text(LookLabel(who->look), row.x + row.w - 8.0f, row.y + 4.0f, TextSize::Small,
                    Palette::TextDim, Align::Right);
        } else {
            ui.Text("empty", row.x + 30.0f, row.y + 4.0f, TextSize::Small, {96, 88, 76, 255});
        }
        y += 28.0f;
    }
    y += 8.0f;

    // The chat: as many of the latest lines as fit, newest at the bottom.
    ui.Text("What has been said", rx, y, TextSize::Small, Palette::Highlight);
    y += 22.0f;
    const float log_bottom = panel.y + panel.h - 44.0f;
    const SDL_FRect log_box = {rx, y, rw, log_bottom - y};
    ui.Fill(log_box, {18, 14, 12, 220});
    ui.Outline(log_box, Palette::BorderDim, 1.0f);
    {
        const auto& log = me.Log();
        float used = 6.0f;
        size_t first = log.size();
        while (first > 0) {
            const auto& line = log[first - 1];
            const string text = line.name.empty() ? line.text : line.name + ": " + line.text;
            const float h = ui.WrappedHeight(text, log_box.w - 16.0f, TextSize::Small) + 2.0f;
            if (used + h > log_box.h - 6.0f) break;
            used += h;
            --first;
        }
        float ly = log_box.y + 6.0f;
        for (size_t i = first; i < log.size(); ++i) {
            const auto& line = log[i];
            const string text = line.name.empty() ? line.text : line.name + ": " + line.text;
            ly += ui.TextWrapped(text, log_box.x + 8.0f, ly, log_box.w - 16.0f, TextSize::Small,
                                 line.name.empty() ? Palette::TextDim : Palette::Text) + 2.0f;
        }
        if (log.empty())
            ui.Text("Nothing yet.", log_box.x + 8.0f, log_box.y + 6.0f, TextSize::Small, {96, 88, 76, 255});
    }

    // Which build this is, to read to a friend whose door says theirs differs.
    const net::DataHashes& h = session.Hashes();
    ui.Text("protocol " + std::to_string(net::PROTOCOL_VERSION) + "   data " + net::ShortHash(h.data) +
            "   maps " + net::ShortHash(h.maps),
            panel.x + 20.0f, panel.y + panel.h - 28.0f, TextSize::Small, {110, 100, 86, 255});
    ui.Text(text_target ? "Enter done     Esc stop typing"
                        : input.PromptFor(Action::Confirm) + " choose     " + input.PromptFor(Action::Back) + " back",
            panel.x + panel.w - 20.0f, panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Right);
}
