// =============================================================================
//  The Game class's screens, continued: a scene from the story, and the signs
//  its tips come up on
//
//  What a scene shows is the director's to say (StoryDirector::View): this only
//  draws it -- the bars that come in from the top and bottom, a line said or
//  seen, a note held up to read, a fade to black or to white, the title card
//  over all of it, and how far the skip button has been held.
// =============================================================================
#include "../game.h"
#include "screens_shared.h"

string Game::FillPrompts(const string& text) const {
    struct Key { const char* token; Action action; };
    static const Key kKeys[] = {
        {"{Interact}", Action::Interact}, {"{Light}", Action::LightAttack}, {"{Heavy}", Action::StrongAttack},
        {"{Guard}", Action::Block}, {"{Sprint}", Action::Sprint}, {"{Journal}", Action::QuestLog},
        {"{Bag}", Action::Inventory}, {"{Map}", Action::WorldMap}, {"{Menu}", Action::Menu},
        {"{Confirm}", Action::Confirm}, {"{Back}", Action::Back}, {"{Target}", Action::Target},
        {"{Jump}", Action::Jump}, {"{Skills}", Action::Skills}, {"{Character}", Action::Character},
    };
    string out = text;
    const auto put = [&](const string& token, const string& with) {
        for (size_t at = out.find(token); at != string::npos; at = out.find(token, at + with.size()))
            out.replace(at, token.size(), with);
    };
    for (const Key& k : kKeys) put(k.token, input.PromptFor(k.action));
    put("{Move}", input.ActiveDevice() == InputMode::Controller
                      ? string("the left stick")
                      : input.PromptFor(Action::MoveUp) + input.PromptFor(Action::MoveLeft) +
                            input.PromptFor(Action::MoveDown) + input.PromptFor(Action::MoveRight));
    // Eating what is to hand is a chord: the shift and interact (Game::SeatChores).
    put("{Eat}", input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(Action::Interact));
    put("{Next}", input.PromptFor(input.ShiftAction()) + "+" + input.PromptFor(Action::Sprint));
    return out;
}

void Game::DrawStory() {
    const StoryView& v = story.View();
    const float W = ui.ViewWidth(), H = ui.ViewHeight();

    // --- the bars ----------------------------------------------------------------------------
    const float bar = 0.11f * H * std::clamp(v.bars, 0.0f, 1.0f);
    if (bar > 0.5f) {
        ui.Fill({0.0f, 0.0f, W, bar}, {0, 0, 0, 255});
        ui.Fill({0.0f, H - bar, W, bar}, {0, 0, 0, 255});
    }

    // --- the fade ----------------------------------------------------------------------------
    if (v.fade > 0.0f) {
        SDL_Color c = v.fade_colour;
        c.a = static_cast<Uint8>(255.0f * std::clamp(v.fade, 0.0f, 1.0f));
        ui.Fill({0.0f, 0.0f, W, H}, c);
    }

    // --- a line, said or seen -----------------------------------------------------------------
    if (v.line) {
        const float w = std::min(W - 80.0f, 860.0f);
        const float x = (W - w) / 2.0f;
        const float pad = 18.0f;
        const SDL_Color said = Palette::Text;
        const SDL_Color seen = {206, 198, 236, 255};
        // Measured first, so the box is as tall as what it holds.
        const float text_h = ui.WrappedHeight(v.text, w - pad * 2.0f, TextSize::Body);
        const float head = v.narration ? 0.0f : 28.0f;
        const float h = pad * 2.0f + head + text_h + 10.0f;
        const float y = H - std::max(bar, 0.0f) - h - 14.0f;
        const SDL_FRect box = {x, y, w, h};
        ui.Fill({box.x + 3.0f, box.y + 4.0f, box.w, box.h}, Palette::Shadow);
        ui.Fill(box, v.narration ? SDL_Color{24, 20, 36, 236} : SDL_Color{22, 18, 16, 238});
        ui.Outline(box, v.narration ? SDL_Color{126, 108, 176, 255} : Palette::Highlight, 1.0f);
        if (!v.narration)
            ui.Text(v.speaker, x + pad, y + pad - 2.0f, TextSize::Body, Palette::Highlight);
        ui.TextWrapped(v.text, x + pad, y + pad + head, w - pad * 2.0f, TextSize::Body, v.narration ? seen : said);
        // The way on, once it can be taken.
        if (v.line_age > 0.25f) {
            const float pulse = 0.55f + 0.45f * sinf(static_cast<float>(SDL_GetTicks()) / 260.0f);
            SDL_Color c = Palette::TextDim;
            c.a = static_cast<Uint8>(255.0f * pulse);
            ui.Text(input.PromptFor(Action::Confirm) + "  >", x + w - pad, y + h - 24.0f, TextSize::Small, c,
                    Align::Right);
        }
    }

    // --- a note, held up to read --------------------------------------------------------------
    if (v.note) {
        const SDL_FRect panel = CenteredPanel(ui, 520.0f, 300.0f);
        ui.Fill({panel.x + 3.0f, panel.y + 4.0f, panel.w, panel.h}, Palette::Shadow);
        ui.Fill(panel, {214, 197, 158, 250});
        ui.Outline(panel, {120, 96, 58, 255}, 2.0f);
        if (!v.note_title.empty()) {
            ui.Text(v.note_title, panel.x + panel.w / 2.0f, panel.y + 24.0f, TextSize::Large, {68, 48, 28, 255},
                    Align::Center);
            ui.Fill({panel.x + 40.0f, panel.y + 64.0f, panel.w - 80.0f, 1.0f}, {140, 116, 78, 255});
        }
        ui.TextWrapped(v.note_text, panel.x + 44.0f, panel.y + (v.note_title.empty() ? 52.0f : 84.0f),
                       panel.w - 88.0f, TextSize::Body, {52, 38, 24, 255});
        ui.Text(input.PromptFor(Action::Confirm) + " put it away", panel.x + panel.w / 2.0f,
                panel.y + panel.h - 32.0f, TextSize::Small, {96, 74, 44, 255}, Align::Center);
    }

    // --- the title card, over everything -------------------------------------------------------
    if (v.title_alpha > 0.0f && !v.title.empty()) {
        const Uint8 a = static_cast<Uint8>(255.0f * std::clamp(v.title_alpha, 0.0f, 1.0f));
        ui.TextShadowed(v.title, W / 2.0f, H / 2.0f - 40.0f, TextSize::Title, {236, 222, 186, a}, Align::Center);
        if (!v.subtitle.empty()) {
            const float rule = std::min(420.0f, ui.Measure(v.subtitle, TextSize::Large).x + 60.0f);
            ui.Fill({W / 2.0f - rule / 2.0f, H / 2.0f + 6.0f, rule, 1.0f}, {196, 170, 112, a});
            ui.TextShadowed(v.subtitle, W / 2.0f, H / 2.0f + 18.0f, TextSize::Large, {214, 204, 236, a},
                            Align::Center);
        }
    }

    // --- the way out of a scene ----------------------------------------------------------------
    if (v.in_scene) {
        // Esc on the keys (K backs out too), the button that backs out on a pad.
        const string hint = "Hold " + input.PromptFor(input.ActiveDevice() == InputMode::Controller ? Action::Back
                                                                                                     : Action::Pause) + " to skip";
        const float tw = ui.Measure(hint, TextSize::Small).x;
        const float x = W - 18.0f, y = H - std::max(bar, 0.0f) / 2.0f - 9.0f - (bar < 20.0f ? 14.0f : 0.0f);
        SDL_Color c = Palette::TextDim;
        c.a = 170;
        ui.Text(hint, x, y, TextSize::Small, c, Align::Right);
        if (v.skip > 0.0f) {
            ui.Fill({x - tw, y + 18.0f, tw, 3.0f}, {60, 54, 70, 220});
            ui.Fill({x - tw, y + 18.0f, tw * v.skip, 3.0f}, Palette::Highlight);
        }
    }
}

void Game::DrawTip() {
    if (tips.empty()) return;
    const StoryTip& t = tips.front();
    const float W = ui.ViewWidth(), H = ui.ViewHeight();
    // In and out, a sign swung into view and taken down again.
    const float in = std::clamp(tip_age / 0.35f, 0.0f, 1.0f), out = std::clamp((10.0f - tip_age) / 0.6f, 0.0f, 1.0f);
    const float k = std::min(in, out);
    if (k <= 0.0f) return;
    const string text = FillPrompts(t.text);
    const float w = std::min(W - 80.0f, 540.0f);
    const float text_h = ui.WrappedHeight(text, w - 36.0f, TextSize::Small);
    const float h = 50.0f + text_h + 14.0f;
    const float x = (W - w) / 2.0f;
    const float y = H - 64.0f - h + (1.0f - k) * 24.0f;
    const Uint8 a = static_cast<Uint8>(255.0f * k);
    // Planks, not parchment: a sign by the road.
    ui.Fill({x + 3.0f, y + 4.0f, w, h}, {0, 0, 0, static_cast<Uint8>(110.0f * k)});
    ui.Fill({x, y, w, h}, {74, 52, 34, static_cast<Uint8>(240.0f * k)});
    ui.Fill({x + 4.0f, y + 4.0f, w - 8.0f, h - 8.0f}, {96, 68, 44, static_cast<Uint8>(240.0f * k)});
    ui.Outline({x, y, w, h}, {176, 140, 86, a}, 2.0f);
    ui.Text(FillPrompts(t.title), x + 18.0f, y + 14.0f, TextSize::Body, {246, 214, 140, a});
    ui.TextWrapped(text, x + 18.0f, y + 44.0f, w - 36.0f, TextSize::Small, {240, 228, 206, a});
}
