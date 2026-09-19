#include "worldmap.h"
#include "../systems/waypoint.h"
#include "../world/world.h"
#include "../systems/shop.h"
#include <fstream>
#include <deque>

namespace {
constexpr SDL_Color VOID_COLOR{16, 14, 20, 255};
// How big a page is baked: about this many picture pixels along its longer
// side, whatever the map. The Hollowmarch, 4736 across, comes out at eight
// world pixels to one; Havenbrook, 1792, at three -- larger than either is ever
// drawn, which keeps a coastline from turning to mush when it is scaled to fit.
constexpr float PAGE_PIXELS = 620.0f;
}

WorldMapPanel::~WorldMapPanel() { Forget(); }

void WorldMapPanel::Forget() {
    for (auto& kv : pages)
        if (kv.second.terrain) SDL_DestroyTexture(kv.second.terrain);
    pages.clear();
}

const char* WorldMapPanel::KindName(const string& kind) {
    if (kind == "dungeon")  return "Dungeon";
    if (kind == "path")     return "Way to another land";
    if (kind == "town")     return "Town";
    if (kind == "camp")     return "Enemy camp";
    if (kind == "grave")    return "Graveyard";
    if (kind == "door")     return "Building";
    if (kind == "trader")   return "Trader";
    if (kind == "craft")    return "Bench, anvil or cauldron";
    return "Landmark";
}

const char* WorldMapPanel::KindGlyph(const string& kind) {
    if (kind == "dungeon")  return "D";
    if (kind == "path")     return ">";
    if (kind == "town")     return "T";
    if (kind == "camp")     return "!";
    if (kind == "grave")    return "+";
    if (kind == "door")     return "^";
    if (kind == "trader")   return "$";
    if (kind == "craft")    return "=";
    return "*";
}

SDL_Color WorldMapPanel::KindColour(const string& kind) {
    if (kind == "dungeon")  return {206, 108, 92,  255};
    if (kind == "path")     return {120, 196, 232, 255};
    if (kind == "town")     return {242, 200, 96,  255};
    if (kind == "camp")     return {214, 96,  72,  255};
    if (kind == "grave")    return {186, 190, 200, 255};
    if (kind == "door")     return {226, 178, 120, 255};
    if (kind == "trader")   return {242, 200, 96,  255};
    if (kind == "craft")    return {170, 206, 150, 255};
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
    if (type == "tanner")      return "K";
    if (type == "weaver")      return "C";
    if (type == "trapper")     return "P";
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
    if (type == "tanner")      return "Tannery";
    if (type == "weaver")      return "Weaver";
    if (type == "trapper")     return "Trapper";
    if (type == "dream")       return "Dream trader";
    return "Trader";
}

bool WorldMapPanel::Load(const string& path, const ShopDatabase& shops) {
    marks.clear();
    areas.clear();
    shop_db = &shops;
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
    if (root.contains("areas"))
        for (auto it = root["areas"].begin(); it != root["areas"].end(); ++it) {
            AreaInfo a;
            a.name = it.value().value("name", it.key());
            a.kind = it.value().value("kind", string("land"));
            for (const auto& e : it.value().value("exits", json::array())) a.exits.push_back(e.get<string>());
            areas[it.key()] = a;
        }
    return !marks.empty();
}

string WorldMapPanel::PageFor(const string& map_id, string* door) const {
    // A room is a room of somewhere: out through its first way out, and again
    // for an upstairs, until what is reached is not a room.
    string at = map_id, came_by;
    for (int hop = 0; hop < 4; ++hop) {
        const auto it = areas.find(at);
        if (it == areas.end() || it->second.kind != "interior" || it->second.exits.empty()) break;
        came_by = at;
        at = it->second.exits.front();
    }
    if (door) *door = came_by;
    return at;
}

string WorldMapPanel::WayFrom(const string& map_id) const {
    if (map_id == OVERWORLD || !areas.count(OVERWORLD)) return "";
    // Breadth first out of the Hollowmarch, remembering which of its own ways
    // out each place was reached by.
    std::map<string, string> first_step;
    std::deque<string> queue;
    for (const string& next : areas.at(OVERWORLD).exits) {
        if (first_step.count(next)) continue;
        first_step[next] = next;
        queue.push_back(next);
    }
    while (!queue.empty()) {
        const string at = queue.front();
        queue.pop_front();
        if (at == map_id) return first_step[at];
        const auto it = areas.find(at);
        if (it == areas.end()) continue;
        for (const string& next : it->second.exits) {
            if (next == OVERWORLD || first_step.count(next)) continue;
            first_step[next] = first_step[at];
            queue.push_back(next);
        }
    }
    return "";
}

vector<WorldMark> WorldMapPanel::MarksOf(const Map& map) const {
    vector<WorldMark> out;
    // The ways out, told apart by what is on the far side.
    for (const Portal& p : map.Portals()) {
        WorldMark m;
        const auto far = areas.find(p.target_map);
        const string far_kind = far == areas.end() ? string("land") : far->second.kind;
        m.kind = far_kind == "dungeon" ? "dungeon" : far_kind == "interior" ? "door" : "path";
        m.label = p.label;
        if (m.label.empty()) m.label = far == areas.end() ? p.target_map : far->second.name;
        if (p.danger_level > 0) m.label += "  (Combat " + std::to_string(p.danger_level) + ")";
        m.x = p.rect.x + p.rect.w * 0.5f;
        m.y = p.rect.y + p.rect.h * 0.5f;
        out.push_back(m);
    }
    // Whoever trades, by trade.
    for (const NpcDef& n : map.Npcs()) {
        if (n.shop.empty()) continue;
        WorldMark m;
        m.kind = "trader";
        m.label = n.name;
        m.x = n.x;
        m.y = n.y;
        if (shop_db)
            if (const ShopDef* shop = shop_db->Get(n.shop)) m.shops.push_back(shop->type);
        out.push_back(m);
    }
    // And the few things worth walking to.
    for (const MapObject& o : map.Objects()) {
        WorldMark m;
        if (o.type == "workbench")     { m.kind = "craft";    m.label = o.title.empty() ? string("Workbench") : o.title; }
        else if (o.type == "board")    { m.kind = "landmark"; m.label = o.title.empty() ? string("Mission board") : o.title; }
        else if (o.type == "campsite") { m.kind = "landmark"; m.label = "Campsite"; }
        else if (o.type == "storage")  { m.kind = "landmark"; m.label = o.title.empty() ? string("Storage chest") : o.title; }
        else if (o.type == "altar")    { m.kind = "landmark"; m.label = o.title.empty() ? string("Altar") : o.title; }
        else continue;
        m.x = o.x;
        m.y = o.y;
        out.push_back(m);
    }
    return out;
}

WorldMapPanel::Page& WorldMapPanel::PageOf(const string& map_id, SDL_Renderer* r, TextureCache& cache) {
    const auto found = pages.find(map_id);
    if (found != pages.end()) return found->second;

    Page& page = pages[map_id];
    Map m;
    if (!m.Load("maps/" + map_id + ".mx")) {
        page.failed = true;
        return page;
    }
    page.world_w = m.Width();
    page.world_h = m.Height();
    page.title = m.DisplayName();
    const int scale = std::max(2, static_cast<int>(ceilf(std::max(page.world_w, page.world_h) / PAGE_PIXELS)));
    page.img_w = std::max(1, static_cast<int>(ceilf(page.world_w / scale)));
    page.img_h = std::max(1, static_cast<int>(ceilf(page.world_h / scale)));

    SDL_Surface* s = SDL_CreateSurface(page.img_w, page.img_h, SDL_PIXELFORMAT_RGBA32);
    if (!s) {
        page.failed = true;
        return page;
    }
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, VOID_COLOR.r, VOID_COLOR.g,
                                                       VOID_COLOR.b, 255));
    // Ground first, then what is laid over it, the same order the world draws.
    // Underground the rock and the floor cut out of it are much the same grey,
    // and a page of that is a grey page: there, what cannot be walked on is
    // drawn dark, so the rooms and passages are what is left.
    const auto area = areas.find(map_id);
    const bool underground = area != areas.end() && area->second.kind == "dungeon";
    for (int pass = 0; pass < 2; ++pass)
        for (const TileInstance& t : m.Tiles()) {
            if (t.layer != LAYER_GROUND || t.overlay != (pass == 1)) continue;
            const string& path = m.TexturePath(t);
            if (path.empty()) continue;
            SDL_Color c = cache.AverageColor(path);
            if (underground && m.Blocked({t.rect.x + t.rect.w * 0.5f - 2.0f, t.rect.y + t.rect.h * 0.5f - 2.0f, 4.0f, 4.0f}))
                c = {static_cast<Uint8>(c.r * 0.38f), static_cast<Uint8>(c.g * 0.38f), static_cast<Uint8>(c.b * 0.42f), 255};
            const SDL_Rect cell = {static_cast<int>(t.rect.x / scale), static_cast<int>(t.rect.y / scale),
                                   std::max(1, static_cast<int>(ceilf(t.rect.w / scale))),
                                   std::max(1, static_cast<int>(ceilf(t.rect.h / scale)))};
            SDL_FillSurfaceRect(s, &cell, SDL_MapSurfaceRGBA(s, c.r, c.g, c.b, 255));
        }
    // Then whatever stands on it, as a smudge of its own colour where its foot
    // is: a tree is a dark green fleck and a house a brown block, and a page
    // of those is a wood, or a village. Not on the Hollowmarch, which is drawn
    // from far enough up that it would be noise.
    if (map_id != OVERWORLD) {
        const auto foot = [&](float cx, float bottom, float w, float h, const string& path) {
            if (path.empty()) return;
            const SDL_Color c = cache.AverageColor(path);
            if (c.a == 0) return;
            const float fw = std::max(static_cast<float>(scale), w * 0.55f);
            const float fh = std::max(static_cast<float>(scale), h * 0.34f);
            const SDL_Rect cell = {static_cast<int>((cx - fw * 0.5f) / scale), static_cast<int>((bottom - fh) / scale),
                                   std::max(1, static_cast<int>(fw / scale)), std::max(1, static_cast<int>(fh / scale))};
            SDL_FillSurfaceRect(s, &cell, SDL_MapSurfaceRGBA(s, static_cast<Uint8>(c.r * 0.78f),
                                                             static_cast<Uint8>(c.g * 0.78f),
                                                             static_cast<Uint8>(c.b * 0.78f), 255));
        };
        for (const TileInstance& t : m.Tiles())
            if (t.layer != LAYER_GROUND)
                foot(t.rect.x + t.rect.w * 0.5f, t.rect.y + t.rect.h, t.rect.w, t.rect.h, m.TexturePath(t));
        for (const MapObject& o : m.Objects())
            if ((o.type == "tree" || o.type == "rock") && !o.sprite.empty())
                foot(o.x, o.y, 44.0f, 40.0f, o.sprite);
    }

    page.terrain = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!page.terrain) {
        page.failed = true;
        return page;
    }
    SDL_SetTextureScaleMode(page.terrain, SDL_SCALEMODE_NEAREST);

    page.marks = map_id == OVERWORLD ? marks : MarksOf(m);
    // Where each way out is, for putting the dot on a door.
    for (const Portal& p : m.Portals())
        if (!page.doors.count(p.target_map))
            page.doors[p.target_map] = {p.rect.x + p.rect.w * 0.5f, p.rect.y + p.rect.h * 0.5f};
    return page;
}

void WorldMapPanel::Draw(SDL_Renderer* r, TextureCache& cache, UI& ui, const World& world,
                         const string& close_prompt, const string& turn_prompt, bool overview,
                         const Waypoint* waypoint, const string& waypoint_label) {
    ui.Dim(0.62f);

    const float view_w = ui.ViewWidth(), view_h = ui.ViewHeight();
    const SDL_FRect panel = {24.0f, 20.0f, view_w - 48.0f, view_h - 40.0f};
    ui.Panel(panel);

    // Which page: where the player is -- or, for a room, where the room is --
    // and the Hollowmarch on the other side of it.
    string door;
    const string here_page = PageFor(world.MapId(), &door);
    const bool two_pages = here_page != OVERWORLD;
    const string page_id = (overview || !two_pages) ? string(OVERWORLD) : here_page;
    Page& page = PageOf(page_id, r, cache);

    const string title = page.title.empty() ? string("The Hollowmarch") : page.title;
    ui.Text(title, panel.x + 22.0f, panel.y + 14.0f, TextSize::Large, Palette::Highlight);
    if (two_pages) {
        const Page& other = PageOf(page_id == OVERWORLD ? here_page : string(OVERWORLD), r, cache);
        const string to = other.title.empty() ? string("the other page") : other.title;
        ui.Text(turn_prompt + "  " + to, panel.x + 40.0f + ui.Measure(title, TextSize::Large).x, panel.y + 22.0f,
                TextSize::Small, Palette::TextDim);
    }

    // The legend down the right, the map in what is left.
    const float legend_w = 250.0f;
    const SDL_FRect area = {panel.x + 20.0f, panel.y + 54.0f,
                            panel.w - legend_w - 56.0f, panel.h - 96.0f};
    ui.Fill(area, {14, 13, 18, 255});

    if (page.failed || !page.terrain || page.world_w <= 0.0f) {
        ui.Text("The map has not been drawn yet.", area.x + area.w / 2.0f, area.y + area.h / 2.0f,
                TextSize::Body, Palette::TextDim, Align::Center);
        return;
    }

    // Fit the whole of it, letterboxed, so nothing is cropped and the shape of
    // the land is the shape on the parchment.
    const float fit = std::min(area.w / static_cast<float>(page.img_w), area.h / static_cast<float>(page.img_h));
    const SDL_FRect dst = {roundf(area.x + (area.w - page.img_w * fit) / 2.0f),
                           roundf(area.y + (area.h - page.img_h * fit) / 2.0f),
                           roundf(page.img_w * fit), roundf(page.img_h * fit)};
    SDL_RenderTexture(r, page.terrain, nullptr, &dst);
    ui.Outline(dst, Palette::BorderDim, 1.0f);

    const auto to_screen = [&](float wx, float wy) {
        return SDL_FPoint{dst.x + (wx / page.world_w) * dst.w, dst.y + (wy / page.world_h) * dst.h};
    };

    // Every mark: a lettered tile in its own colour, its name beside it, and
    // for a town or a trader the trades kept there. A name is written where it
    // does not lie on another: beside its mark, on the other side of it, or
    // failing both a line or two lower -- a well, a town and a gate within a
    // few pixels of each other were three names in one smear.
    vector<SDL_FRect> written;
    const auto clashes = [&](const SDL_FRect& box) {
        for (const SDL_FRect& w : written)
            if (box.x < w.x + w.w && w.x < box.x + box.w && box.y < w.y + w.h && w.y < box.y + box.h) return true;
        return false;
    };
    for (const WorldMark& m : page.marks) {
        const SDL_FPoint p = to_screen(m.x, m.y);
        const SDL_Color c = KindColour(m.kind);
        const SDL_FRect box = {roundf(std::clamp(p.x - 8.0f, dst.x, dst.x + dst.w - 16.0f)),
                               roundf(std::clamp(p.y - 8.0f, dst.y, dst.y + dst.h - 16.0f)), 16.0f, 16.0f};
        ui.Fill({box.x + 1.0f, box.y + 1.0f, box.w, box.h}, {0, 0, 0, 150});
        ui.Fill(box, {28, 24, 20, 235});
        ui.Outline(box, c, 1.0f);
        ui.Text(KindGlyph(m.kind), box.x + box.w / 2.0f, box.y + 1.0f, TextSize::Small, c, Align::Center);

        // The label goes left of the mark when the mark is near the right
        // edge, so a name never runs off the parchment.
        const SDL_FPoint size = ui.Measure(m.label, TextSize::Small);
        const float label_h = 14.0f + (m.shops.empty() ? 0.0f : 17.0f);
        const float label_w = std::max(size.x, static_cast<float>(m.shops.size()) * 15.0f);
        bool flip = p.x > dst.x + dst.w * 0.72f;
        float drop = 0.0f;
        const auto place = [&](bool left, float down) {
            return SDL_FRect{left ? box.x - 6.0f - label_w : box.x + box.w + 6.0f, box.y + 1.0f + down, label_w, label_h};
        };
        for (int attempt = 0; attempt < 8; ++attempt) {
            const bool left = (attempt % 2 == 0) ? flip : !flip;
            const float down = static_cast<float>(attempt / 2) * 15.0f;
            const SDL_FRect want = place(left, down);
            const bool fits = want.x >= dst.x - 2.0f && want.x + want.w <= dst.x + dst.w + 120.0f;
            if ((fits && !clashes(want)) || attempt == 7) { flip = left; drop = down; break; }
        }
        written.push_back(place(flip, drop));
        written.push_back(box);
        const float lx = flip ? box.x - 6.0f : box.x + box.w + 6.0f;
        ui.TextShadowed(m.label, lx, box.y + 1.0f + drop, TextSize::Small, Palette::Text,
                        flip ? Align::Right : Align::Left);

        if (!m.shops.empty()) {
            float sx = flip ? box.x - 6.0f - size.x : lx;
            const float sy = box.y + box.h + 2.0f + drop;
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

    const auto dot = [&](float wx, float wy, SDL_Color colour, const string& label) {
        const SDL_FPoint p = to_screen(wx, wy);
        const SDL_FRect d = {roundf(p.x - 3.0f), roundf(p.y - 3.0f), 6.0f, 6.0f};
        ui.Fill({d.x - 2.0f, d.y - 2.0f, d.w + 4.0f, d.h + 4.0f}, {0, 0, 0, 170});
        ui.Fill(d, colour);
        // Over the dot, on a plate of its own: a mark's name is written level
        // with its mark, and standing on one put the two on top of each other.
        const SDL_FPoint size = ui.Measure(label, TextSize::Small);
        ui.Fill({roundf(p.x - size.x / 2.0f - 4.0f), roundf(p.y - 25.0f), size.x + 8.0f, 17.0f}, {12, 10, 14, 215});
        ui.Text(label, p.x, p.y - 24.0f, TextSize::Small, colour, Align::Center);
    };

    // Where the player is. Out on this page: there. In a room on it: at the
    // room's door. And on the Hollowmarch from somewhere else: at the way that
    // leads to wherever that is, if a road does.
    if (page_id == here_page) {
        if (door.empty()) {
            // Friends first, so the player's own dot is on top of a crowd.
            for (const auto& g : world.guests)
                if (g && !g->away && !g->absent)
                    dot(g->x, g->y, {120, 214, 255, 255}, g->name.empty() ? string("a friend") : g->name);
            dot(world.player.x, world.player.y, {255, 255, 255, 255}, "you are here");
        } else {
            const auto at = page.doors.find(door);
            if (at != page.doors.end()) dot(at->second.x, at->second.y, {255, 255, 255, 255}, "you are inside");
        }
    } else {
        const string way = WayFrom(here_page);
        const auto at = page.doors.find(way);
        if (!way.empty() && at != page.doors.end())
            dot(at->second.x, at->second.y, {255, 255, 255, 255}, "you are this way");
    }

    // Where the quest being followed is: the thing itself if it is on this page,
    // the door to the room it is in if it is in one, and otherwise the way off
    // this page that starts towards it.
    bool quest_marked = false;
    if (waypoint && !waypoint->map.empty() && (waypoint->found || waypoint->hint.empty())) {
        SDL_FPoint at{0, 0};
        bool have = false;
        if (waypoint->map == page_id) {
            have = waypoint->x != 0.0f || waypoint->y != 0.0f;
            at = {waypoint->x, waypoint->y};
        } else if (roads) {
            const vector<string> road = roads->Route(page_id, waypoint->map);
            const auto way = road.size() >= 2 ? page.doors.find(road[1]) : page.doors.end();
            if (way != page.doors.end()) { at = way->second; have = true; }
        }
        quest_marked = have;
        if (have) {
            const SDL_FPoint p = to_screen(at.x, at.y);
            const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(SDL_GetTicks()) / 1000.0f * 4.0f);
            const SDL_Color gold = {255, static_cast<Uint8>(206 + 30 * pulse), 96, 255};
            const float half = 8.0f + 3.0f * pulse;
            ui.Outline({roundf(p.x - half - 1.0f), roundf(p.y - half - 1.0f), half * 2.0f + 2.0f, half * 2.0f + 2.0f}, {0, 0, 0, 200}, 3.0f);
            ui.Outline({roundf(p.x - half), roundf(p.y - half), half * 2.0f, half * 2.0f}, gold, 2.0f);
            ui.Fill({roundf(p.x - 2.0f), roundf(p.y - 2.0f), 4.0f, 4.0f}, gold);
            const string text = waypoint_label.empty() ? waypoint->what : waypoint_label;
            const SDL_FPoint size = ui.Measure(text, TextSize::Small);
            ui.Fill({roundf(p.x - size.x / 2.0f - 4.0f), roundf(p.y + half + 5.0f), size.x + 8.0f, 17.0f}, {12, 10, 14, 225});
            ui.Text(text, p.x, p.y + half + 6.0f, TextSize::Small, gold, Align::Center);
        }
    }

    // --- the legend ------------------------------------------------------------
    float lx = panel.x + panel.w - legend_w - 16.0f;
    float ly = panel.y + 54.0f;
    ui.Text("Legend", lx, ly, TextSize::Body, Palette::Highlight);
    ly += 26.0f;
    if (quest_marked) {
        const SDL_Color gold = {255, 214, 96, 255};
        const SDL_FRect box = {lx, ly, 16.0f, 16.0f};
        ui.Outline(box, gold, 2.0f);
        ui.Fill({box.x + 6.0f, box.y + 6.0f, 4.0f, 4.0f}, gold);
        ui.Text("The quest you are following", lx + 24.0f, ly + 1.0f, TextSize::Small, gold);
        ly += 26.0f;
    }
    // Only what is on this page.
    for (const char* kind : {"town", "door", "trader", "craft", "dungeon", "path", "grave", "camp", "landmark"}) {
        bool on_page = false;
        for (const WorldMark& m : page.marks) on_page |= m.kind == kind;
        if (!on_page) continue;
        const SDL_Color c = KindColour(kind);
        const SDL_FRect box = {lx, ly, 16.0f, 16.0f};
        ui.Fill(box, {28, 24, 20, 235});
        ui.Outline(box, c, 1.0f);
        ui.Text(KindGlyph(kind), box.x + box.w / 2.0f, box.y + 1.0f, TextSize::Small, c, Align::Center);
        ui.Text(KindName(kind), lx + 24.0f, ly + 1.0f, TextSize::Small, Palette::Text);
        ly += 22.0f;
    }

    // Only the trades that are actually somewhere on this page.
    vector<string> types;
    for (const WorldMark& m : page.marks)
        for (const string& t : m.shops)
            if (std::find(types.begin(), types.end(), t) == types.end()) types.push_back(t);
    std::sort(types.begin(), types.end());
    if (!types.empty()) {
        ly += 10.0f;
        ui.Text("Trades", lx, ly, TextSize::Body, Palette::Highlight);
        ly += 26.0f;
    }
    for (const string& type : types) {
        const SDL_FRect box = {lx, ly, 13.0f, 13.0f};
        ui.Fill(box, {24, 22, 18, 230});
        ui.Outline(box, Palette::BorderDim, 1.0f);
        ui.Text(ShopGlyph(type), box.x + box.w / 2.0f, box.y - 1.0f, TextSize::Small,
                Palette::Highlight, Align::Center);
        ui.Text(ShopName(type), lx + 22.0f, ly - 1.0f, TextSize::Small, Palette::TextDim);
        ly += 20.0f;
    }

    // Where you are now, in words.
    ly += 12.0f;
    string here;
    if (world.MapId() == OVERWORLD) here = "You are out in the Hollowmarch.";
    else {
        here = "You are in " + world.CurrentMap().DisplayName();
        const Page& home = PageOf(here_page, r, cache);
        if (!door.empty() && !home.title.empty()) here += ", in " + home.title;
        here += ".";
    }
    ui.TextWrapped(here, lx, ly, legend_w - 8.0f, TextSize::Small, Palette::TextDim);

    ui.Text(close_prompt, panel.x + panel.w / 2.0f, panel.y + panel.h - 26.0f, TextSize::Small,
            Palette::TextDim, Align::Center);
}
