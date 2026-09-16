#include "worldmap.h"
#include "../world/world.h"
#include "../systems/shop.h"
#include <fstream>

namespace {
constexpr SDL_Color VOID_COLOR{16, 14, 20, 255};
// World pixels to one picture pixel. The Hollowmarch is 4736 x 3968, so at
// eight it bakes to 592 x 496 -- larger than it is ever drawn, which keeps the
// coastlines from turning to mush when it is scaled down to fit a panel.
constexpr int SCALE = 8;
}

WorldMapPanel::~WorldMapPanel() { Forget(); }

void WorldMapPanel::Forget() {
    if (terrain) SDL_DestroyTexture(terrain);
    terrain = nullptr;
    baked = bake_failed = false;
}

const char* WorldMapPanel::KindName(const string& kind) {
    if (kind == "dungeon")  return "Dungeon";
    if (kind == "path")     return "Way to another land";
    if (kind == "town")     return "Town";
    if (kind == "camp")     return "Enemy camp";
    if (kind == "grave")    return "Graveyard";
    return "Landmark";
}

const char* WorldMapPanel::KindGlyph(const string& kind) {
    if (kind == "dungeon")  return "D";
    if (kind == "path")     return ">";
    if (kind == "town")     return "T";
    if (kind == "camp")     return "!";
    if (kind == "grave")    return "+";
    return "*";
}

SDL_Color WorldMapPanel::KindColour(const string& kind) {
    if (kind == "dungeon")  return {206, 108, 92,  255};
    if (kind == "path")     return {120, 196, 232, 255};
    if (kind == "town")     return {242, 200, 96,  255};
    if (kind == "camp")     return {214, 96,  72,  255};
    if (kind == "grave")    return {186, 190, 200, 255};
    return {196, 186, 150, 255};
}

// The traders, told apart by trade rather than by name: a forge is an anvil's
// worth of use to somebody carrying ore, whatever the smith is called.
const char* WorldMapPanel::ShopGlyph(const string& type) {
    if (type == "forge")       return "F";
    if (type == "general")     return "G";
    if (type == "provisioner") return "I";
    if (type == "bowyer")      return "B";
    if (type == "herbalist")   return "H";
    if (type == "fishmonger")  return "W";
    if (type == "mill")        return "L";
    if (type == "dream")       return "?";
    return "S";
}

const char* WorldMapPanel::ShopName(const string& type) {
    if (type == "forge")       return "Forge";
    if (type == "general")     return "General store";
    if (type == "provisioner") return "Inn kitchen";
    if (type == "bowyer")      return "Bowyer";
    if (type == "herbalist")   return "Herbalist";
    if (type == "fishmonger")  return "Fishmonger";
    if (type == "mill")        return "Lumber mill";
    if (type == "dream")       return "Dream trader";
    return "Trader";
}

bool WorldMapPanel::Load(const string& path, const ShopDatabase& shops) {
    marks.clear();
    std::ifstream in(path);
    if (!in) {
        SDL_Log("WorldMapPanel: cannot open '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("WorldMapPanel: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    world_w = root.value("width", 0.0f);
    world_h = root.value("height", 0.0f);
    for (const auto& j : root.value("marks", json::array())) {
        WorldMark m;
        m.kind  = j.value("kind", string("landmark"));
        m.label = j.value("label", string(""));
        m.x     = j.value("x", 0.0f);
        m.y     = j.value("y", 0.0f);
        m.town  = j.value("town", string(""));
        // What a town sells, from the shop database rather than from this file:
        // adding a trader to a town should not mean editing the map as well.
        if (!m.town.empty())
            for (const auto& kv : shops.All())
                if (kv.second.town == m.town) m.shops.push_back(kv.second.type);
        std::sort(m.shops.begin(), m.shops.end());
        m.shops.erase(std::unique(m.shops.begin(), m.shops.end()), m.shops.end());
        marks.push_back(m);
    }
    return !marks.empty();
}

bool WorldMapPanel::Bake(SDL_Renderer* r, TextureCache& cache) {
    baked = true;
    Map ow;
    if (!ow.Load("maps/overworld.mx")) {
        bake_failed = true;
        return false;
    }
    world_w = ow.Width();
    world_h = ow.Height();
    img_w = std::max(1, static_cast<int>(ceilf(world_w / SCALE)));
    img_h = std::max(1, static_cast<int>(ceilf(world_h / SCALE)));

    SDL_Surface* s = SDL_CreateSurface(img_w, img_h, SDL_PIXELFORMAT_RGBA32);
    if (!s) {
        bake_failed = true;
        return false;
    }
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, VOID_COLOR.r, VOID_COLOR.g,
                                                       VOID_COLOR.b, 255));
    // Ground first, then what is laid over it, the same order the world draws.
    for (int pass = 0; pass < 2; ++pass)
        for (const TileInstance& t : ow.Tiles()) {
            if (t.layer != LAYER_GROUND || t.overlay != (pass == 1)) continue;
            const string& path = ow.TexturePath(t);
            if (path.empty()) continue;
            const SDL_Color c = cache.AverageColor(path);
            const SDL_Rect cell = {static_cast<int>(t.rect.x / SCALE), static_cast<int>(t.rect.y / SCALE),
                                   std::max(1, static_cast<int>(t.rect.w / SCALE)),
                                   std::max(1, static_cast<int>(t.rect.h / SCALE))};
            SDL_FillSurfaceRect(s, &cell, SDL_MapSurfaceRGBA(s, c.r, c.g, c.b, 255));
        }

    terrain = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!terrain) {
        bake_failed = true;
        return false;
    }
    SDL_SetTextureScaleMode(terrain, SDL_SCALEMODE_NEAREST);
    return true;
}

void WorldMapPanel::Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
                         const string& close_prompt) {
    ui.Dim(0.62f);

    const float view_w = ui.ViewWidth(), view_h = ui.ViewHeight();
    const SDL_FRect panel = {24.0f, 20.0f, view_w - 48.0f, view_h - 40.0f};
    ui.Panel(panel);
    ui.Text("The Hollowmarch", panel.x + 22.0f, panel.y + 14.0f, TextSize::Large, Palette::Highlight);

    if (!baked) Bake(r, cache);

    // The legend down the right, the map in what is left.
    const float legend_w = 250.0f;
    const SDL_FRect area = {panel.x + 20.0f, panel.y + 54.0f,
                            panel.w - legend_w - 56.0f, panel.h - 96.0f};
    ui.Fill(area, {14, 13, 18, 255});

    if (bake_failed || !terrain || world_w <= 0.0f) {
        ui.Text("The map has not been drawn yet.", area.x + area.w / 2.0f, area.y + area.h / 2.0f,
                TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    // Fit the whole of it, letterboxed, so nothing is cropped and the shape of
    // the land is the shape on the parchment.
    const float fit = std::min(area.w / static_cast<float>(img_w), area.h / static_cast<float>(img_h));
    const SDL_FRect dst = {roundf(area.x + (area.w - img_w * fit) / 2.0f),
                           roundf(area.y + (area.h - img_h * fit) / 2.0f),
                           roundf(img_w * fit), roundf(img_h * fit)};
    SDL_RenderTexture(r, terrain, nullptr, &dst);
    ui.Outline(dst, Palette::BorderDim, 1.0f);

    const auto to_screen = [&](float wx, float wy) {
        return SDL_FPoint{dst.x + (wx / world_w) * dst.w, dst.y + (wy / world_h) * dst.h};
    };

    // Every mark: a lettered tile in its own colour, its name beside it, and
    // for a town the trades it keeps.
    for (const WorldMark& m : marks) {
        const SDL_FPoint p = to_screen(m.x, m.y);
        const SDL_Color c = KindColour(m.kind);
        const SDL_FRect box = {roundf(p.x - 8.0f), roundf(p.y - 8.0f), 16.0f, 16.0f};
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w, box.h}, {0, 0, 0, 150});
        ui.Fill(box, {28, 24, 20, 235});
        ui.Outline(box, c, 1.0f);
        ui.Text(KindGlyph(m.kind), box.x + box.w / 2.0f, box.y + 1.0f, TextSize::Small, c, Align::Center);

        // The label goes left of the mark when the mark is near the right
        // edge, so a name never runs off the parchment.
        const bool flip = p.x > dst.x + dst.w * 0.72f;
        const float lx = flip ? box.x - 6.0f : box.x + box.w + 6.0f;
        ui.Text(m.label, lx, box.y + 1.0f, TextSize::Small, Palette::Text,
                flip ? Align::Right : Align::Left);

        if (!m.shops.empty()) {
            float sx = flip ? box.x - 6.0f - ui.Measure(m.label, TextSize::Small).x : lx;
            const float sy = box.y + box.h + 2.0f;
            for (const string& type : m.shops) {
                const SDL_FRect chip = {roundf(sx), roundf(sy), 13.0f, 13.0f};
                ui.Fill(chip, {24, 22, 18, 230});
                ui.Outline(chip, Palette::BorderDim, 1.0f);
                ui.Text(ShopGlyph(type), chip.x + chip.w / 2.0f, chip.y - 1.0f, TextSize::Small,
                        Palette::Highlight, Align::Center);
                sx += 15.0f;
            }
        }
    }

    // Where the player is, when the player is out in it.
    if (world.MapId() == "overworld") {
        const SDL_FPoint p = to_screen(world.player.x, world.player.y);
        const SDL_FRect dot = {roundf(p.x - 3.0f), roundf(p.y - 3.0f), 6.0f, 6.0f};
        ui.Fill({dot.x - 2.0f, dot.y - 2.0f, dot.w + 4.0f, dot.h + 4.0f}, {0, 0, 0, 170});
        ui.Fill(dot, {255, 255, 255, 255});
        // Over the dot: a mark's own name is written level with it and to the
        // right, and standing in a town put the two on top of each other.
        ui.Text("you are here", p.x, p.y - 22.0f, TextSize::Small, {255, 255, 255, 255}, Align::Center);
    }

    // --- the legend ------------------------------------------------------------
    float lx = panel.x + panel.w - legend_w - 16.0f;
    float ly = panel.y + 54.0f;
    ui.Text("Legend", lx, ly, TextSize::Body, Palette::Highlight);
    ly += 26.0f;
    for (const char* kind : {"town", "dungeon", "path", "grave", "camp", "landmark"}) {
        const SDL_Color c = KindColour(kind);
        const SDL_FRect box = {lx, ly, 16.0f, 16.0f};
        ui.Fill(box, {28, 24, 20, 235});
        ui.Outline(box, c, 1.0f);
        ui.Text(KindGlyph(kind), box.x + box.w / 2.0f, box.y + 1.0f, TextSize::Small, c, Align::Center);
        ui.Text(KindName(kind), lx + 24.0f, ly + 1.0f, TextSize::Small, Palette::Text);
        ly += 22.0f;
    }

    ly += 10.0f;
    ui.Text("Trades", lx, ly, TextSize::Body, Palette::Highlight);
    ly += 26.0f;
    // Only the trades that are actually somewhere on this map.
    vector<string> types;
    for (const WorldMark& m : marks)
        for (const string& t : m.shops)
            if (std::find(types.begin(), types.end(), t) == types.end()) types.push_back(t);
    std::sort(types.begin(), types.end());
    for (const string& type : types) {
        const SDL_FRect box = {lx, ly, 13.0f, 13.0f};
        ui.Fill(box, {24, 22, 18, 230});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        ui.Text(ShopGlyph(type), box.x + box.w / 2.0f, box.y - 1.0f, TextSize::Small,
                Palette::Highlight, Align::Center);
        ui.Text(ShopName(type), lx + 22.0f, ly - 1.0f, TextSize::Small, Palette::TextDim);
        ly += 20.0f;
    }

    // Where you are now, in words, for when you are not on the overworld at all.
    ly += 12.0f;
    const string here = world.MapId() == "overworld"
        ? string("You are out in the Hollowmarch.")
        : "You are in " + world.CurrentMap().DisplayName() + ".";
    ui.TextWrapped(here, lx, ly, legend_w - 8.0f, TextSize::Small, Palette::TextDim);

    ui.Text(close_prompt, panel.x + panel.w / 2.0f, panel.y + panel.h - 26.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}
