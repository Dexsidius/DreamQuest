#include "ui.h"
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

static const float kSizes[static_cast<int>(TextSize::COUNT)] = {13.0f, 17.0f, 23.0f, 38.0f};
static constexpr int CACHE_LIMIT = 512;

// Prefer a font shipped with the game, then fall back to something every
// platform has, so a fresh clone still renders text.
static const char* kFontCandidates[] = {
    "assets/fonts/dreamquest.ttf",
    "assets/fonts/font.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/seguisb.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
};

bool UI::Init(SDL_Renderer* r) {
    renderer = r;

    std::error_code ec;
    for (const char* candidate : kFontCandidates) {
        if (!fs::exists(candidate, ec)) continue;
        bool all_opened = true;
        for (int i = 0; i < static_cast<int>(TextSize::COUNT); ++i) {
            fonts[i] = TTF_OpenFont(candidate, kSizes[i]);
            if (!fonts[i]) { all_opened = false; break; }
        }
        if (all_opened) {
            font_path = candidate;
            SDL_Log("UI: using font '%s'", candidate);
            return true;
        }
        // Partial open: clean up and try the next candidate.
        for (auto& f : fonts) { if (f) TTF_CloseFont(f); f = nullptr; }
    }

    SDL_Log("UI: no usable font found; text will not render");
    return false;
}

void UI::Shutdown() {
    for (auto& kv : cache)
        if (kv.second.texture) SDL_DestroyTexture(kv.second.texture);
    cache.clear();
    for (auto& f : fonts) { if (f) TTF_CloseFont(f); f = nullptr; }
}

UI::CachedText& UI::GetText(const string& text, TextSize size, SDL_Color color) {
    const int index = std::clamp(static_cast<int>(size), 0,
                                 static_cast<int>(TextSize::COUNT) - 1);

    // Colour is part of the key: the same string in two colours is two blits.
    char key_buf[64];
    SDL_snprintf(key_buf, sizeof(key_buf), "%d|%02x%02x%02x%02x|",
                 index, color.r, color.g, color.b, color.a);
    const string key = string(key_buf) + text;

    auto it = cache.find(key);
    if (it != cache.end()) {
        it->second.last_used = frame;
        return it->second;
    }

    CachedText entry;
    entry.last_used = frame;

    if (fonts[index] && !text.empty()) {
        SDL_Surface* surface = TTF_RenderText_Blended(fonts[index], text.c_str(), 0, color);
        if (surface) {
            entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
            entry.w = static_cast<float>(surface->w);
            entry.h = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (entry.texture) SDL_SetTextureScaleMode(entry.texture, SDL_SCALEMODE_NEAREST);
        }
    }

    EvictIfLarge();
    return cache.emplace(key, entry).first->second;
}

// Strings churn (damage numbers, timers), so drop the coldest half rather than
// letting the cache grow without bound.
void UI::EvictIfLarge() {
    if (cache.size() < CACHE_LIMIT) return;

    vector<pair<int, string>> ages;
    ages.reserve(cache.size());
    for (const auto& kv : cache) ages.emplace_back(kv.second.last_used, kv.first);
    std::sort(ages.begin(), ages.end());

    for (size_t i = 0; i < ages.size() / 2; ++i) {
        auto it = cache.find(ages[i].second);
        if (it != cache.end()) {
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            cache.erase(it);
        }
    }
}

SDL_FPoint UI::Measure(const string& text, TextSize size) {
    if (text.empty()) return {0.0f, LineHeight(size)};
    CachedText& c = GetText(text, size, Palette::Text);
    return {c.w, c.h};
}

float UI::LineHeight(TextSize size) const {
    const int index = std::clamp(static_cast<int>(size), 0,
                                 static_cast<int>(TextSize::COUNT) - 1);
    if (!fonts[index]) return kSizes[index] * 1.3f;
    return static_cast<float>(TTF_GetFontHeight(fonts[index]));
}

void UI::Text(const string& text, float x, float y, TextSize size,
              SDL_Color color, Align align) {
    if (text.empty()) return;
    ++frame;
    CachedText& c = GetText(text, size, color);
    if (!c.texture) return;

    float draw_x = x;
    if (align == Align::Center)     draw_x = x - c.w / 2.0f;
    else if (align == Align::Right) draw_x = x - c.w;

    SDL_FRect dst = {roundf(draw_x), roundf(y), c.w, c.h};
    SDL_RenderTexture(renderer, c.texture, nullptr, &dst);
}

void UI::TextShadowed(const string& text, float x, float y, TextSize size,
                      SDL_Color color, Align align) {
    Text(text, x + 1.0f, y + 1.0f, size, {0, 0, 0, 190}, align);
    Text(text, x, y, size, color, align);
}

float UI::WrappedHeight(const string& text, float wrap_width, TextSize size) {
    return TextWrapped(text, 0.0f, 0.0f, wrap_width, size, Palette::Text, false);
}

float UI::TextWrapped(const string& text, float x, float y, float wrap_width,
                      TextSize size, SDL_Color color, bool draw) {
    const float line_h = LineHeight(size) + 2.0f;
    float cursor_y = y;

    // Break on explicit newlines first, then greedily on spaces.
    std::istringstream paragraphs(text);
    string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        if (paragraph.empty()) { cursor_y += line_h * 0.6f; continue; }

        std::istringstream words(paragraph);
        string word, line;
        while (words >> word) {
            const string candidate = line.empty() ? word : line + " " + word;
            if (Measure(candidate, size).x > wrap_width && !line.empty()) {
                if (draw) Text(line, x, cursor_y, size, color);
                cursor_y += line_h;
                line = word;
            } else {
                line = candidate;
            }
        }
        if (!line.empty()) {
            if (draw) Text(line, x, cursor_y, size, color);
            cursor_y += line_h;
        }
    }
    return cursor_y - y;
}

void UI::Fill(const SDL_FRect& r, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &r);
}

void UI::Outline(const SDL_FRect& r, SDL_Color c, float thickness) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    for (float i = 0; i < thickness; ++i) {
        SDL_FRect inset = {r.x + i, r.y + i, r.w - i * 2.0f, r.h - i * 2.0f};
        if (inset.w <= 0 || inset.h <= 0) break;
        SDL_RenderRect(renderer, &inset);
    }
}

void UI::Panel(const SDL_FRect& r, bool raised) {
    // Soft drop shadow, body, then a double frame: outer dark, inner gold.
    Fill({r.x + 3.0f, r.y + 4.0f, r.w, r.h}, Palette::Shadow);
    Fill(r, raised ? Palette::Panel : Palette::PanelLight);
    Outline(r, Palette::BorderDim, 2.0f);
    Outline({r.x + 2.0f, r.y + 2.0f, r.w - 4.0f, r.h - 4.0f}, Palette::Border, 1.0f);
}

void UI::Bar(const SDL_FRect& r, float fraction, SDL_Color fill,
             SDL_Color back, bool bordered) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    Fill(r, back);
    if (fraction > 0.0f) {
        SDL_FRect inner = {r.x + 1.0f, r.y + 1.0f,
                           (r.w - 2.0f) * fraction, r.h - 2.0f};
        Fill(inner, fill);
        // Highlight along the top edge so the bar reads as filled, not flat.
        Fill({inner.x, inner.y, inner.w, std::max(1.0f, inner.h * 0.28f)},
             {static_cast<Uint8>(std::min(255, fill.r + 45)),
              static_cast<Uint8>(std::min(255, fill.g + 45)),
              static_cast<Uint8>(std::min(255, fill.b + 45)), fill.a});
    }
    if (bordered) Outline(r, Palette::BorderDim, 1.0f);
}

void UI::FramedBar(const SDL_FRect& outer, float fraction, SDL_Color fill,
                   SDL_Color back, int ticks) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    // A dark outline, then a brass band lit at the top left and shadowed at the
    // bottom right, so the frame has a direction of light like the minimap
    // bezel and the panels.
    Fill(outer, {30, 22, 15, 255});
    const SDL_FRect band = {outer.x + 1.0f, outer.y + 1.0f, outer.w - 2.0f, outer.h - 2.0f};
    Fill(band, {124, 98, 52, 255});
    Fill({band.x, band.y, band.w, 1.0f}, {186, 156, 92, 255});
    Fill({band.x, band.y, 1.0f, band.h}, {168, 138, 78, 255});
    Fill({band.x, band.y + band.h - 1.0f, band.w, 1.0f}, {74, 56, 28, 255});
    Fill({band.x + band.w - 1.0f, band.y, 1.0f, band.h}, {86, 66, 34, 255});

    // The track, sunk into the band.
    const SDL_FRect track = {outer.x + 3.0f, outer.y + 3.0f, outer.w - 6.0f, outer.h - 6.0f};
    Fill(track, back);
    Fill({track.x, track.y, track.w, 1.0f}, {0, 0, 0, 90});

    if (fraction > 0.0f) {
        const float w = std::max(1.0f, roundf((track.w - 2.0f) * fraction));
        const SDL_FRect f = {track.x + 1.0f, track.y + 1.0f, w, track.h - 2.0f};
        Fill(f, fill);
        Fill({f.x, f.y, f.w, std::max(1.0f, f.h * 0.34f)},
             {static_cast<Uint8>(std::min(255, fill.r + 52)),
              static_cast<Uint8>(std::min(255, fill.g + 52)),
              static_cast<Uint8>(std::min(255, fill.b + 52)), fill.a});
        Fill({f.x, f.y + f.h - 1.0f, f.w, 1.0f},
             {static_cast<Uint8>(fill.r / 2), static_cast<Uint8>(fill.g / 2),
              static_cast<Uint8>(fill.b / 2), fill.a});
    }

    for (int i = 1; i < ticks; ++i) {
        const float x = roundf(track.x + track.w * i / static_cast<float>(ticks));
        Fill({x, track.y + 1.0f, 1.0f, track.h - 2.0f}, {0, 0, 0, 70});
    }

    // Rivets, matching the bezel -- but only where there is room for them.
    if (outer.h >= 18.0f) {
        const float r = 2.0f;
        for (const SDL_FPoint& c : {SDL_FPoint{band.x + 1.0f, band.y + 1.0f},
                                    SDL_FPoint{band.x + band.w - 1.0f - r, band.y + 1.0f},
                                    SDL_FPoint{band.x + 1.0f, band.y + band.h - 1.0f - r},
                                    SDL_FPoint{band.x + band.w - 1.0f - r,
                                               band.y + band.h - 1.0f - r}})
            Fill({c.x, c.y, r, r}, {214, 184, 116, 255});
    }
}

void UI::Dim(float amount) {
    Fill({0, 0, view_w, view_h},
         {0, 0, 0, static_cast<Uint8>(std::clamp(amount, 0.0f, 1.0f) * 255.0f)});
}

void UI::MenuItem(const SDL_FRect& r, const string& label, bool selected,
                  bool enabled, const string& right_text) {
    if (selected) {
        Fill(r, {70, 55, 32, 220});
        Outline(r, Palette::Highlight, 1.0f);
        // Caret marking the current row, for controller navigation clarity.
        Text(">", r.x + 8.0f, r.y + (r.h - LineHeight()) / 2.0f,
             TextSize::Body, Palette::Highlight);
    }

    const SDL_Color color = !enabled  ? SDL_Color{110, 100, 90, 255}
                          : selected  ? Palette::Highlight
                                      : Palette::Text;

    const float text_y = r.y + (r.h - LineHeight()) / 2.0f;
    Text(label, r.x + 26.0f, text_y, TextSize::Body, color);
    if (!right_text.empty())
        Text(right_text, r.x + r.w - 14.0f, text_y, TextSize::Body,
             enabled ? Palette::TextDim : SDL_Color{100, 92, 84, 255}, Align::Right);
}
