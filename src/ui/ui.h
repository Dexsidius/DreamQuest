#pragma once
#include "../headers.h"
#include "../texturecache.h"

// -----------------------------------------------------------------------------
//  Drawing helpers shared by every panel, menu and HUD element.
//
//  Text is rendered through SDL3_ttf and cached by (string, size, colour), so
//  a label redrawn every frame costs one blit rather than a fresh rasterise.
// -----------------------------------------------------------------------------

enum class TextSize { Small = 0, Body, Large, Title, COUNT };
enum class Align { Left, Center, Right };

// The game's palette, kept in one place so panels stay consistent.
namespace Palette {
    inline constexpr SDL_Color Panel      {26,  20,  17,  242};
    inline constexpr SDL_Color PanelLight {44,  34,  28,  242};
    inline constexpr SDL_Color Border     {150, 118, 62,  255};
    inline constexpr SDL_Color BorderDim  {84,  66,  40,  255};
    inline constexpr SDL_Color Text       {238, 226, 200, 255};
    inline constexpr SDL_Color TextDim    {160, 148, 128, 255};
    inline constexpr SDL_Color Highlight  {242, 200, 96,  255};
    inline constexpr SDL_Color Health     {186, 54,  48,  255};
    inline constexpr SDL_Color HealthBack {58,  26,  24,  255};
    inline constexpr SDL_Color Charge     {236, 168, 52,  255};
    inline constexpr SDL_Color Xp         {126, 196, 122, 255};
    inline constexpr SDL_Color Mana       {84,  132, 214, 255};
    inline constexpr SDL_Color ManaBack   {22,  30,  52,  255};
    inline constexpr SDL_Color Stamina    {214, 176, 56,  255};
    inline constexpr SDL_Color StaminaBack{50,  40,  18,  255};
    inline constexpr SDL_Color Shadow     {0,   0,   0,   160};
}

class UI {
public:
    bool Init(SDL_Renderer* renderer);
    void Shutdown();

    // --- text ---------------------------------------------------------------
    void Text(const string& text, float x, float y, TextSize size = TextSize::Body,
              SDL_Color color = Palette::Text, Align align = Align::Left);
    // Drop-shadowed, for text sitting over the world rather than a panel.
    void TextShadowed(const string& text, float x, float y, TextSize size = TextSize::Body,
                      SDL_Color color = Palette::Text, Align align = Align::Left);
    // Returns the height used, so callers can lay out flowing blocks.
    float TextWrapped(const string& text, float x, float y, float wrap_width,
                      TextSize size = TextSize::Body, SDL_Color color = Palette::Text,
                      bool draw = true);
    // The height TextWrapped would use, without drawing anything.
    float WrappedHeight(const string& text, float wrap_width, TextSize size = TextSize::Body);
    SDL_FPoint Measure(const string& text, TextSize size = TextSize::Body);
    float LineHeight(TextSize size = TextSize::Body) const;

    // --- shapes -------------------------------------------------------------
    void Fill(const SDL_FRect& r, SDL_Color c);
    void Outline(const SDL_FRect& r, SDL_Color c, float thickness = 1.0f);
    // Framed panel with a subtle inner highlight.
    void Panel(const SDL_FRect& r, bool raised = true);
    void Bar(const SDL_FRect& r, float fraction, SDL_Color fill,
             SDL_Color back, bool bordered = true);
    // The same bar in a brass frame, for the vitals in the corner of the HUD:
    // a bevelled band around a sunk track, with quarter ticks across it so a
    // glance reads roughly how much is left without reading the numbers.
    void FramedBar(const SDL_FRect& outer, float fraction, SDL_Color fill,
                   SDL_Color back, int ticks = 4);
    // Full-screen dim, for menus over the world.
    void Dim(float amount);

    // A menu row; the caller owns selection state.
    void MenuItem(const SDL_FRect& r, const string& label, bool selected,
                  bool enabled = true, const string& right_text = "");

    void SetViewport(float w, float h) { view_w = w; view_h = h; }
    float ViewWidth() const { return view_w; }
    float ViewHeight() const { return view_h; }

    // --- looking for text that runs off -------------------------------------------
    // While auditing, every piece of text drawn is checked against the panel it
    // was drawn on -- the last one opened that it starts inside -- and against
    // the window. Text that begins on a panel and ends off it, or hangs off the
    // edge of the window, is written down: what it said, and by how much. It is
    // how `--audit` walks every menu and says which ones overflow, instead of
    // somebody having to notice.
    struct Overflow { string text; float over_right = 0, over_left = 0, over_bottom = 0; bool off_window = false; };
    void BeginAudit() { auditing = true; audit_panels.clear(); audit_found.clear(); }
    vector<Overflow> EndAudit() { auditing = false; return std::move(audit_found); }

    bool Ready() const { return fonts[0] != nullptr; }
    // Which font file was actually opened, for the startup log.
    const string& FontPath() const { return font_path; }

private:
    struct CachedText { SDL_Texture* texture = nullptr; float w = 0, h = 0; int last_used = 0; };
    CachedText& GetText(const string& text, TextSize size, SDL_Color color);
    void EvictIfLarge();

    SDL_Renderer* renderer = nullptr;
    TTF_Font* fonts[static_cast<int>(TextSize::COUNT)] = {nullptr};
    string font_path;

    unordered_map<string, CachedText> cache;
    int frame = 0;

    float view_w = 1280.0f, view_h = 720.0f;

    bool auditing = false;
    vector<SDL_FRect> audit_panels;
    vector<Overflow>  audit_found;
};
