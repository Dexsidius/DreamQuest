#include "map.h"
#include "../systems/shaders.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static SDL_Color ColorFromJson(const json& j, SDL_Color fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {static_cast<Uint8>(j[0].get<int>()),
            static_cast<Uint8>(j[1].get<int>()),
            static_cast<Uint8>(j[2].get<int>()),
            static_cast<Uint8>(j.size() > 3 ? j[3].get<int>() : 255)};
}

// The editor writes paths relative to the exported map folder; the game runs
// from the project root. Accept either so a map exported straight out of
// LevelEdit-Plus drops in without editing.
string Map::ResolveAsset(const string& rel) const {
    std::error_code ec;
    if (fs::exists(rel, ec)) return rel;
    if (!source_dir.empty()) {
        const string joined = source_dir + "/" + rel;
        if (fs::exists(joined, ec)) return joined;
    }
    return rel;
}

bool Map::Load(const string& path) {
    Unload();

    std::ifstream in(path);
    if (!in) {
        SDL_Log("Map: cannot open '%s'", path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("Map: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    std::error_code ec;
    source_dir = fs::path(path).parent_path().string();
    id = fs::path(path).stem().string();
    display_name = root.value("name", id);

    // ---- gameplay extension (read first: layer/solid tables drive the tiles)
    const json dq = root.contains("dreamquest") ? root["dreamquest"] : json::object();

    map<string, int> layer_of;
    if (dq.contains("layers"))
        for (auto it = dq["layers"].begin(); it != dq["layers"].end(); ++it)
            layer_of[it.key()] = it.value().get<int>();

    // How far above its bottom edge a piece of scenery sorts against people.
    // Normally zero: a tree sorts at its base. A hillside with a tunnel cut in
    // it sorts at the back of the tunnel, so someone standing in the mouth is
    // drawn in front of the rock around them rather than behind it.
    map<string, float> sort_lift;
    if (dq.contains("sort_lift"))
        for (auto it = dq["sort_lift"].begin(); it != dq["sort_lift"].end(); ++it)
            sort_lift[it.key()] = it.value().get<float>();

    map<string, bool> solid_tile;
    if (dq.contains("solid"))
        for (const auto& s : dq["solid"]) solid_tile[s.get<string>()] = true;

    // Tiles whose collision box is smaller than the art, so a tree trunk
    // blocks but its canopy does not.
    map<string, SDL_FRect> solid_box;
    if (dq.contains("solid_box"))
        for (auto it = dq["solid_box"].begin(); it != dq["solid_box"].end(); ++it) {
            const json& b = it.value();
            if (b.is_array() && b.size() >= 4)
                solid_box[it.key()] = {b[0].get<float>(), b[1].get<float>(),
                                       b[2].get<float>(), b[3].get<float>()};
        }

    interior   = dq.value("interior", false);
    dark       = dq.value("dark", false);
    ambient    = dq.value("ambient", string("overworld"));
    dream_depth = dq.value("dream_depth", 0);
    subtitle   = dq.value("subtitle", string(""));
    background = ColorFromJson(dq.contains("background") ? dq["background"] : json(),
                               interior ? SDL_Color{18, 14, 20, 255}
                                        : SDL_Color{34, 48, 34, 255});
    // Ground fog: how thick, its colour, how much water thickens it, and
    // where -- a region with a soft edge, or everywhere.
    fog = Shaders::Fog{};
    if (dq.contains("fog") && dq["fog"].is_object()) {
        const json& f = dq["fog"];
        fog.on = true;
        fog.density = f.value("density", 0.3f);
        fog.by_water = f.value("water", 0.0f);
        fog.drift = f.value("drift", 6.0f);
        if (f.contains("colour") && f["colour"].is_array() && f["colour"].size() >= 3) {
            fog.r = f["colour"][0].get<float>() / 255.0f;
            fog.g = f["colour"][1].get<float>() / 255.0f;
            fog.b = f["colour"][2].get<float>() / 255.0f;
        }
        if (f.contains("region") && f["region"].is_array() && f["region"].size() >= 4)
            fog.region = {f["region"][0].get<float>(), f["region"][1].get<float>(),
                          f["region"][2].get<float>(), f["region"][3].get<float>()};
    }

    // ---- tiles (native LevelEdit-Plus section) ------------------------------
    if (root.contains("tiles")) {
        for (auto it = root["tiles"].begin(); it != root["tiles"].end(); ++it) {
            const string& tile_name = it.key();
            const json& entry = it.value();
            if (!entry.contains("locations")) continue;

            textures.push_back(ResolveAsset(entry.value("filepath", string(""))));
            surfaces.push_back(Shaders::SurfaceOfTile(textures.back()));
            arts.push_back(&Shaders::ArtOf(textures.back()));
            const int tex_index = static_cast<int>(textures.size()) - 1;

            const int layer = layer_of.count(tile_name) ? layer_of[tile_name] : LAYER_GROUND;
            const bool is_solid = solid_tile.count(tile_name) > 0;
            const bool has_box  = solid_box.count(tile_name) > 0;

            for (const auto& loc : entry["locations"]) {
                if (!loc.is_array() || loc.size() < 4) continue;
                const float cx = loc[0].get<float>();
                const float cy = loc[1].get<float>();
                const float w  = loc[2].get<float>();
                const float h  = loc[3].get<float>();

                TileInstance t;
                // Centre-anchored in the file, top-left in memory.
                t.rect  = {cx - w / 2.0f, cy - h / 2.0f, w, h};
                t.tex   = tex_index;
                t.layer = std::clamp(layer, 0, 2);
                t.sort_y = t.rect.y + t.rect.h;
                if (sort_lift.count(tile_name)) t.sort_y -= sort_lift[tile_name];
                t.overlay = !tile_name.empty() && tile_name[0] == '~';
                tiles.push_back(t);

                if (has_box) {
                    const SDL_FRect& b = solid_box[tile_name];
                    AddCollider({t.rect.x + b.x, t.rect.y + b.y, b.w, b.h});
                } else if (is_solid) {
                    AddCollider(t.rect);
                }
            }
        }
    }

    // ---- elevation -----------------------------------------------------------
    if (dq.contains("elevation")) {
        const json& e = dq["elevation"];
        // Which tile an exposed bank is made of. A map in a cave would want
        // stone; the overworld wants soil.
        cliff_texture = ResolveAsset(e.value("face", string("assets/tiles/dirt_dark.png")));
        cliff_lip = {108, 138, 74, 255};
        if (e.contains("lip") && e["lip"].is_array() && e["lip"].size() >= 3)
            cliff_lip = {static_cast<Uint8>(e["lip"][0].get<int>()), static_cast<Uint8>(e["lip"][1].get<int>()),
                         static_cast<Uint8>(e["lip"][2].get<int>()), 255};
        elev_cell = e.value("cell", 32.0f);
        elev_cols = e.value("cols", 0);
        elev_rows = e.value("rows", 0);
        if (elev_cols > 0 && elev_rows > 0 && e.contains("levels")) {
            const json& lv = e["levels"];
            elev.assign(static_cast<size_t>(elev_cols) * elev_rows, 0);
            const size_t n = std::min(elev.size(), lv.size());
            for (size_t i = 0; i < n; ++i)
                elev[i] = static_cast<uint8_t>(
                    std::clamp(lv[i].get<int>(), 0, ELEVATION_MAX));
        } else {
            elev_cols = elev_rows = 0;
        }
        if (e.contains("ramps"))
            for (const auto& rr : e["ramps"])
                if (rr.is_array() && rr.size() >= 4)
                    ramps.push_back({rr[0].get<float>(), rr[1].get<float>(),
                                     rr[2].get<float>(), rr[3].get<float>()});
    }

    // ---- explicit collision boxes -------------------------------------------
    if (dq.contains("collision"))
        for (const auto& c : dq["collision"])
            if (c.is_array() && c.size() >= 4)
                AddCollider({c[0].get<float>(), c[1].get<float>(),
                             c[2].get<float>(), c[3].get<float>()});

    // ---- water ---------------------------------------------------------------
    // Collision that a swimmer may pass. A map with none of this behaves
    // exactly as it did.
    if (dq.contains("water"))
        for (const auto& c : dq["water"])
            if (c.is_array() && c.size() >= 4)
                AddCollider({c[0].get<float>(), c[1].get<float>(),
                             c[2].get<float>(), c[3].get<float>()}, true);

    // ---- portals -------------------------------------------------------------
    if (dq.contains("portals"))
        for (const auto& p : dq["portals"]) {
            Portal portal;
            const json& r = p.value("rect", json::array());
            if (r.is_array() && r.size() >= 4)
                portal.rect = {r[0].get<float>(), r[1].get<float>(),
                               r[2].get<float>(), r[3].get<float>()};
            portal.target_map       = p.value("target", string(""));
            portal.target_spawn     = p.value("spawn", string("default"));
            portal.label            = p.value("label", string("Enter"));
            portal.requires_interact= p.value("interact", true);
            portal.locked_by        = p.value("locked_by", string(""));
            portal.danger_level     = p.value("level", 0);
            portal.min_combat       = p.value("min_combat", 0);
            portals.push_back(portal);
        }

    // ---- hazards -------------------------------------------------------------
    if (dq.contains("hazards"))
        for (const auto& h : dq["hazards"]) {
            Hazard hz;
            const json& r = h.value("rect", json::array());
            if (r.is_array() && r.size() >= 4)
                hz.rect = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
            hz.dps  = h.value("dps", 4.0f);
            hz.kind = h.value("kind", string("fire"));
            hazards.push_back(hz);
        }

    // ---- spawn points --------------------------------------------------------
    if (dq.contains("spawns"))
        for (auto it = dq["spawns"].begin(); it != dq["spawns"].end(); ++it) {
            const json& v = it.value();
            if (v.is_array() && v.size() >= 2)
                spawns[it.key()] = {v[0].get<float>(), v[1].get<float>()};
        }

    // ---- enemies -------------------------------------------------------------
    if (dq.contains("enemies"))
        for (const auto& e : dq["enemies"]) {
            EnemySpawnDef d;
            d.type    = e.value("type", string("orc1"));
            d.x       = e.value("x", 0.0f);
            d.y       = e.value("y", 0.0f);
            d.level   = e.value("level", 1);
            d.respawn = e.value("respawn", 25.0f);
            d.leash   = e.value("leash", 220.0f);
            if (e.contains("pool") && e["pool"].is_array())
                for (const auto& t : e["pool"]) if (t.is_string()) d.pool.push_back(t.get<string>());
            d.group   = e.value("group", string(""));
            d.spread  = std::max(0, e.value("spread", 0));
            d.night   = e.value("night", false);
            d.lurk    = e.value("lurk", false);
            d.chance  = std::clamp(e.value("chance", 1.0f), 0.0f, 1.0f);
            d.shown   = std::max(0, e.value("shown", 0));
            if (e.contains("route") && e["route"].is_array())
                for (const auto& p : e["route"])
                    if (p.is_array() && p.size() >= 2)
                        d.route.push_back({p[0].get<float>(), p[1].get<float>()});
            enemies.push_back(d);
        }

    // ---- NPCs ----------------------------------------------------------------
    if (dq.contains("npcs"))
        for (const auto& n : dq["npcs"]) {
            NpcDef d;
            d.id       = n.value("id", string(""));
            d.name     = n.value("name", string("Villager"));
            d.sprite   = n.value("sprite", string("citizen1"));
            d.x        = n.value("x", 0.0f);
            d.y        = n.value("y", 0.0f);
            d.dialogue = n.value("dialogue", string(""));
            d.facing   = static_cast<Facing>(n.value("facing", 0));
            d.wanders  = n.value("wanders", false);
            d.shop     = n.value("shop", string(""));
            if (n.contains("path"))
                for (const auto& stop : n["path"]) {
                    if (!stop.is_array() || stop.size() < 2) continue;
                    NpcStop s;
                    s.x = stop[0].get<float>();
                    s.y = stop[1].get<float>();
                    if (stop.size() > 2) s.pause = stop[2].get<float>();
                    if (stop.size() > 3) s.facing = static_cast<Facing>(stop[3].get<int>());
                    d.path.push_back(s);
                }
            d.ping_pong = n.value("ping_pong", false);
            d.speed     = n.value("speed", 30.0f);
            if (n.contains("casts") && n["casts"].is_object()) {
                const json& c = n["casts"];
                d.cast_bolt  = c.value("bolt", string(""));
                d.cast_every = c.value("every", 3.0f);
                if (c.contains("at") && c["at"].is_array() && c["at"].size() >= 2) {
                    d.cast_x = c["at"][0].get<float>();
                    d.cast_y = c["at"][1].get<float>();
                }
            }
            d.phase     = n.value("phase", 0.0f);
            if (n.contains("hours") && n["hours"].size() >= 2) {
                d.from_hour = n["hours"][0].get<float>();
                d.to_hour   = n["hours"][1].get<float>();
            }
            if (n.contains("tint") && n["tint"].size() >= 3)
                d.tint = {static_cast<Uint8>(n["tint"][0].get<int>()), static_cast<Uint8>(n["tint"][1].get<int>()),
                          static_cast<Uint8>(n["tint"][2].get<int>()), 255};
            npcs.push_back(d);
        }

    // ---- interactable objects ------------------------------------------------
    if (dq.contains("objects"))
        for (const auto& o : dq["objects"]) {
            MapObject m;
            m.id           = o.value("id", string(""));
            m.type         = o.value("type", string("prop"));
            m.x            = o.value("x", 0.0f);
            m.y            = o.value("y", 0.0f);
            m.loot_table   = o.value("loot", string(""));
            m.loot_item    = o.value("item", string(""));
            m.loot_qty     = o.value("item_qty", 1);
            m.needs_quest  = o.value("needs_quest", string(""));
            m.text         = o.value("text", string(""));
            m.starts_quest = o.value("starts_quest", string(""));
            m.sprite       = o.value("sprite", string(""));
            m.sprite_open  = o.value("sprite_open", string(""));
            m.skill        = o.value("skill", string(""));
            m.skill_level  = o.value("skill_level", 1);
            m.yield        = o.value("yield", string(""));
            m.yield_xp     = o.value("yield_xp", 0);
            m.gather_time  = o.value("gather_time", 2.6f);
            m.regrow_hours = o.value("regrow", 6.0f);
            m.deplete      = o.value("deplete", 0.0f);
            m.title        = o.value("title", string(""));
            m.station      = o.value("station", string("workbench"));
            m.fee          = o.value("fee", 0);
            m.capacity     = o.value("capacity", 0);
            if (o.contains("fish"))
                for (const auto& f : o["fish"]) m.fish.push_back(f.get<string>());
            if (!m.sprite.empty())      m.sprite      = ResolveAsset(m.sprite);
            if (!m.sprite_open.empty()) m.sprite_open = ResolveAsset(m.sprite_open);
            if (o.contains("quests"))
                for (const auto& q : o["quests"]) m.quests.push_back(q.get<string>());
            if (o.contains("solid")) {
                const json& s = o["solid"];
                if (s.is_array() && s.size() >= 4) {
                    m.solid = {s[0].get<float>(), s[1].get<float>(),
                               s[2].get<float>(), s[3].get<float>()};
                    AddCollider(m.solid);
                }
            }
            objects.push_back(m);
        }

    // ---- bounds --------------------------------------------------------------
    if (dq.contains("bounds") && dq["bounds"].is_array() && dq["bounds"].size() >= 2) {
        bounds_w = dq["bounds"][0].get<float>();
        bounds_h = dq["bounds"][1].get<float>();
    } else {
        // Derive from content so a map exported straight from the editor,
        // with no extension block at all, still scrolls correctly.
        for (const auto& t : tiles) {
            bounds_w = std::max(bounds_w, t.rect.x + t.rect.w);
            bounds_h = std::max(bounds_h, t.rect.y + t.rect.h);
        }
    }

    BuildChunks();
    loaded = true;
    SDL_Log("Map '%s': %d tiles, %d colliders, %d enemies, %d npcs, %.0fx%.0f px",
            id.c_str(), static_cast<int>(tiles.size()), static_cast<int>(colliders.size()),
            static_cast<int>(enemies.size()), static_cast<int>(npcs.size()),
            bounds_w, bounds_h);
    return true;
}

void Map::Unload() {
    loaded = false;
    textures.clear(); surfaces.clear(); arts.clear(); tiles.clear(); colliders.clear();
    fog = Shaders::Fog{};
    collider_water.clear(); water_count = 0;
    portals.clear(); enemies.clear(); npcs.clear(); objects.clear();
    spawns.clear(); chunks.clear();
    bounds_w = bounds_h = 0;
    chunk_cols = chunk_rows = 0;
    id.clear(); display_name.clear(); source_dir.clear(); subtitle.clear();

    // The height grid has to go with everything else. It was left out when
    // elevation was added, and because the parser only ever writes it when a
    // map has an "elevation" block, a map without one never overwrote it: walk
    // from the overworld into any building and the building inherited the
    // overworld's hills. The guild hall's doorway sat on one of them, which is
    // why it could not be entered -- the cliff there was a field outside.
    elev.clear();
    ramps.clear();
    elev_cols = elev_rows = 0;
    elev_cell = 32.0f;
    cliff_texture.clear();
}

void Map::AddCollider(const SDL_FRect& r, bool water) {
    if (r.w <= 0 || r.h <= 0) return;
    colliders.push_back(r);
    collider_water.push_back(water ? 1 : 0);
    if (water) ++water_count;
}

// Bucket static geometry into a uniform grid once at load time, so drawing and
// collision both touch only the handful of chunks they overlap. This is what
// lets a map be many screens wide without the frame cost following it.
void Map::BuildChunks() {
    chunk_cols = std::max(1, static_cast<int>(ceilf(bounds_w / CHUNK)) + 1);
    chunk_rows = std::max(1, static_cast<int>(ceilf(bounds_h / CHUNK)) + 1);
    chunks.assign(static_cast<size_t>(chunk_cols) * chunk_rows, Chunk{});

    auto stamp = [&](const SDL_FRect& r, const std::function<void(Chunk&)>& put) {
        const int x0 = std::max(0, static_cast<int>(floorf(r.x / CHUNK)));
        const int y0 = std::max(0, static_cast<int>(floorf(r.y / CHUNK)));
        const int x1 = std::min(chunk_cols - 1, static_cast<int>(floorf((r.x + r.w) / CHUNK)));
        const int y1 = std::min(chunk_rows - 1, static_cast<int>(floorf((r.y + r.h) / CHUNK)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                put(chunks[static_cast<size_t>(y) * chunk_cols + x]);
    };

    for (size_t i = 0; i < tiles.size(); ++i) {
        const int layer = std::clamp(tiles[i].layer, 0, 2);
        stamp(tiles[i].rect, [&](Chunk& c) { c.layers[layer].push_back(static_cast<int>(i)); });
    }
    for (size_t i = 0; i < colliders.size(); ++i)
        stamp(colliders[i], [&](Chunk& c) { c.colliders.push_back(static_cast<int>(i)); });
}

const Map::Chunk* Map::ChunkAt(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= chunk_cols || cy >= chunk_rows) return nullptr;
    return &chunks[static_cast<size_t>(cy) * chunk_cols + cx];
}

void Map::ForEachChunkInRect(const SDL_FRect& r,
                             const std::function<void(const Chunk&)>& fn) const {
    if (chunks.empty()) return;
    const int x0 = std::max(0, static_cast<int>(floorf(r.x / CHUNK)));
    const int y0 = std::max(0, static_cast<int>(floorf(r.y / CHUNK)));
    const int x1 = std::min(chunk_cols - 1, static_cast<int>(floorf((r.x + r.w) / CHUNK)));
    const int y1 = std::min(chunk_rows - 1, static_cast<int>(floorf((r.y + r.h) / CHUNK)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            fn(chunks[static_cast<size_t>(y) * chunk_cols + x]);
}

// -----------------------------------------------------------------------------
//  Elevation
// -----------------------------------------------------------------------------

// How much brighter a terrace is than the ground floor.
//
// Colour modulation cannot brighten past what the texture already is, so this
// works the other way round: the ground floor is darkened a little and each
// level up gives some of that back, until the highest terrace is the texture
// as drawn. Seven percent is small enough that the low ground does not read as
// being in shadow, and enough that two terraces meeting are clearly two.
Uint8 Map::LevelShade(int level) {
    return static_cast<Uint8>(std::min(255, 236 + level * 7));
}

int Map::LevelCell(int cx, int cy) const {
    if (elev.empty()) return 0;
    // Clamped rather than wrapped or zeroed: a point just off the north edge
    // belongs to the terrain at the edge, so walking out of bounds does not
    // step off a cliff that only exists because the array ran out.
    cx = std::clamp(cx, 0, elev_cols - 1);
    cy = std::clamp(cy, 0, elev_rows - 1);
    return elev[static_cast<size_t>(cy) * elev_cols + cx];
}

int Map::LevelAt(float x, float y) const {
    if (elev.empty()) return 0;
    return LevelCell(static_cast<int>(std::floor(x / elev_cell)),
                     static_cast<int>(std::floor(y / elev_cell)));
}

bool Map::RampAt(float x, float y) const {
    for (const SDL_FRect& r : ramps)
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return true;
    return false;
}

void Map::RenderCliffs(SDL_Renderer* r, TextureCache& cache, const Camera& cam) const {
    if (elev.empty()) return;

    const SDL_FRect view = cam.VisibleWorldRect(96.0f);
    const int x0 = std::max(0, static_cast<int>(std::floor(view.x / elev_cell)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(view.y / elev_cell)) - 1);
    const int x1 = std::min(elev_cols - 1,
                            static_cast<int>((view.x + view.w) / elev_cell) + 1);
    const int y1 = std::min(elev_rows - 1,
                            static_cast<int>((view.y + view.h) / elev_cell) + 2);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Only the south side of a raised cell shows a face. The other three edges
    // are hidden by the terrace above them, exactly as they would be looking
    // down at a real bank of earth -- and drawing all four gives every plateau
    // an outline that reads as a box floating over the map rather than as
    // ground that is higher over there.
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const int here = LevelCell(cx, cy);
            const int below = LevelCell(cx, cy + 1);
            if (here <= below) continue;

            const float drop = (here - below) * ELEVATION_RISE;
            const SDL_FRect world = {
                cx * elev_cell,
                (cy + 1) * elev_cell - here * ELEVATION_RISE,
                elev_cell,
                drop
            };
            const SDL_FRect dst = cam.ToScreenRect(world);

            // A ramp crossing this edge is drawn as stairs, not as a cliff.
            // Before this a ramp was an invisible rectangle: you could walk up
            // it, but it looked exactly like the cliff either side of it, so
            // nobody would ever try.
            const float mid_x = world.x + world.w * 0.5f;
            if (RampAt(mid_x, world.y + world.h - 1.0f) ||
                RampAt(mid_x, world.y + world.h + 2.0f)) {
                const float step_h = std::max(3.0f * cam.zoom, dst.h / std::max(1, here - below) / 2.0f);
                int n = 0;
                for (float oy = 0.0f; oy < dst.h; oy += step_h, ++n) {
                    const SDL_FRect tread = {dst.x, dst.y + oy, dst.w,
                                             std::min(step_h, dst.h - oy)};
                    // Alternate tread and riser: the tread catches the light,
                    // the riser is in its own shadow.
                    if (n % 2 == 0) SDL_SetRenderDrawColor(r, 168, 148, 112, 255);
                    else            SDL_SetRenderDrawColor(r, 122, 104, 76, 255);
                    SDL_RenderFillRect(r, &tread);
                }
                // Stone cheeks either side, so the flight reads as built.
                const float cheek = std::max(2.0f, 3.0f * cam.zoom);
                SDL_SetRenderDrawColor(r, 96, 88, 78, 255);
                SDL_FRect lc = {dst.x, dst.y, cheek, dst.h};
                SDL_FRect rc = {dst.x + dst.w - cheek, dst.y, cheek, dst.h};
                SDL_RenderFillRect(r, &lc);
                SDL_RenderFillRect(r, &rc);
                continue;
            }

            // Earth, not a coloured bar. Flat fills were tried first and the
            // result reads as a brown stripe lying on the grass rather than as
            // a bank of soil, because every other surface in view has grain
            // and this one did not. The dirt tile is drawn down the face,
            // repeated as many times as the drop needs.
            if (SDL_Texture* soil = cache.Get(cliff_texture)) {
                float tw = 0, th = 0;
                SDL_GetTextureSize(soil, &tw, &th);
                if (tw > 0 && th > 0) {
                    const float step = th * cam.zoom;
                    for (float oy = 0.0f; oy < dst.h; oy += step) {
                        SDL_FRect band = {dst.x, dst.y + oy, dst.w,
                                          std::min(step, dst.h - oy)};
                        // Only the top part of the tile when the last band is
                        // short, so the grain is never squashed.
                        SDL_FRect src = {0.0f, 0.0f, tw, th * (band.h / step)};
                        SDL_RenderTexture(r, soil, &src, &band);
                    }
                    // Shade it, so a vertical face reads as turned away from
                    // the light while the ground on top stays lit.
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, 24, 18, 12, 90);
                    SDL_RenderFillRect(r, &dst);
                }
            } else {
                SDL_SetRenderDrawColor(r, 92, 71, 48, 255);
                SDL_RenderFillRect(r, &dst);
            }

            // The grass rolling over the lip, and a dark line where the face
            // meets the ground below. Between them these are what make the
            // terrace look like it is sitting on something.
            SDL_FRect lip = dst;
            lip.h = std::max(1.0f, 2.0f * cam.zoom);
            SDL_SetRenderDrawColor(r, cliff_lip.r, cliff_lip.g, cliff_lip.b, 255);
            SDL_RenderFillRect(r, &lip);

            // The shadow the bank throws on the ground below it. More than
            // anything else, this is what stops the face looking like a strip
            // of brown laid on top of the grass.
            SDL_FRect foot = dst;
            foot.y += dst.h;
            foot.h = std::max(1.0f, 4.0f * cam.zoom);
            SDL_SetRenderDrawColor(r, 34, 40, 28, 96);
            SDL_RenderFillRect(r, &foot);
            // With the effects on, the shadow goes on past that, softening as
            // it goes -- the ground at the foot of a bank sees less of the sky
            // -- and the face darkens toward the bottom, where it is deepest.
            if (Shaders::Effects()) {
                const float step = std::max(1.0f, 2.0f * cam.zoom);
                const int bands = std::clamp(static_cast<int>(drop / 5.0f), 3, 7);
                for (int b = 0; b < bands; ++b) {
                    const SDL_FRect soft = {dst.x, foot.y + foot.h + b * step, dst.w, step};
                    SDL_SetRenderDrawColor(r, 20, 24, 16, static_cast<Uint8>(70.0f * (1.0f - (b + 1.0f) / (bands + 1.0f))));
                    SDL_RenderFillRect(r, &soft);
                }
                for (int b = 0; b < 3; ++b) {
                    const SDL_FRect deep = {dst.x, dst.y + dst.h - (b + 1) * step, dst.w, step};
                    SDL_SetRenderDrawColor(r, 14, 10, 6, static_cast<Uint8>(42 - b * 12));
                    SDL_RenderFillRect(r, &deep);
                }
            }

            SDL_FRect lineFoot = dst;
            lineFoot.y += dst.h - std::max(1.0f, cam.zoom);
            lineFoot.h = std::max(1.0f, cam.zoom);
            SDL_SetRenderDrawColor(r, 48, 38, 26, 210);
            SDL_RenderFillRect(r, &lineFoot);
        }
    }

    // The east and west edges of a terrace, as a dark line rather than a face.
    // Looking almost straight down, those sides are barely turned toward the
    // camera, so there is no wall to see -- but without an edge there the
    // plateau has an outline on one side only and reads as a bar rather than
    // as a shape.
    const float edge = std::max(1.0f, 2.0f * cam.zoom);
    SDL_SetRenderDrawColor(r, 58, 46, 32, 190);
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const int here = LevelCell(cx, cy);
            if (here <= 0) continue;
            const float top = cy * elev_cell - here * ELEVATION_RISE;

            for (int side = 0; side < 2; ++side) {
                const int nx = side == 0 ? cx - 1 : cx + 1;
                if (LevelCell(nx, cy) >= here) continue;
                const SDL_FRect world = {
                    side == 0 ? cx * elev_cell : (cx + 1) * elev_cell - 2.0f,
                    top, 2.0f, elev_cell
                };
                SDL_FRect line = cam.ToScreenRect(world);
                line.w = edge;
                SDL_RenderFillRect(r, &line);
            }
        }
    }
}

void Map::RenderLayer(SDL_Renderer* r, TextureCache& cache,
                      const Camera& cam, int layer, int passes) const {
    if (!loaded || layer < 0 || layer > 2) return;
    const SDL_FRect view = cam.VisibleWorldRect(96.0f);

    // A tile can straddle a chunk border and be listed twice; skip repeats.
    static vector<int> seen_stamp;
    static int stamp_counter = 0;
    if (seen_stamp.size() != tiles.size()) seen_stamp.assign(tiles.size(), 0);
    const int stamp = ++stamp_counter;

    // Two passes over the ground: the floor, then anything lying on it.
    //
    // Tiles are drawn chunk by chunk, so the file order that puts a rug after
    // the floor only holds inside one chunk. A rug straddling a chunk border is
    // drawn with the first chunk, and the second chunk's floor tiles are then
    // painted straight over the rest of it -- which is how a rug in front of
    // the inn's fireplace came out with its right third missing.
    for (int pass = 0; pass < 2; ++pass) {
    if (layer == LAYER_GROUND && !(passes & (1 << pass))) continue;
    const bool want_overlay = (pass == 1);
    // The first pass only marks the tiles it draws, so overlays are still
    // unmarked when the second pass reaches them; no reset needed.
    if (pass == 1 && layer != LAYER_GROUND) break;
    ForEachChunkInRect(view, [&](const Chunk& c) {
        for (int idx : c.layers[layer]) {
            if (seen_stamp[idx] == stamp) continue;

            const TileInstance& t = tiles[idx];
            if (layer == LAYER_GROUND && t.overlay != want_overlay) continue;
            seen_stamp[idx] = stamp;
            if (!RectsOverlap(t.rect, view)) continue;

            SDL_Texture* tex = cache.Get(textures[t.tex]);
            if (!tex) continue;
            SDL_FRect world = t.rect;
            const int level = LevelAt(world.x + world.w * 0.5f,
                                      world.y + world.h * 0.5f);
            world.y -= level * ELEVATION_RISE;
            const SDL_FRect dst = cam.ToScreenRect(world);
            // Water runs and lava churns, and a tuft of grass lying on the
            // ground stirs in the wind (all no-ops off the GPU renderer).
            Shaders::UseTile(r, static_cast<Shaders::Surface>(SurfaceOf(t.tex)), ArtOf(t.tex).kind);

            if (HasElevation()) {
                const Uint8 lit = LevelShade(level);
                SDL_SetTextureColorMod(tex, lit, lit, lit);
                SDL_RenderTexture(r, tex, nullptr, &dst);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            } else {
                SDL_RenderTexture(r, tex, nullptr, &dst);
            }
        }
    });
    }
    Shaders::UsePlain(r);
}

void Map::SurfaceRects(const SDL_FRect& rect, Uint8 surface, vector<SDL_FRect>& out) const {
    if (!loaded) return;
    static vector<int> seen_stamp;
    static int stamp_counter = 0;
    if (seen_stamp.size() != tiles.size()) seen_stamp.assign(tiles.size(), 0);
    const int stamp = ++stamp_counter;
    ForEachChunkInRect(rect, [&](const Chunk& c) {
        for (int idx : c.layers[LAYER_GROUND]) {
            if (seen_stamp[idx] == stamp) continue;
            seen_stamp[idx] = stamp;
            const TileInstance& t = tiles[idx];
            if (t.overlay || SurfaceOf(t.tex) != surface || !RectsOverlap(t.rect, rect)) continue;
            SDL_FRect world = t.rect;
            world.y -= LevelAt(world.x + world.w * 0.5f, world.y + world.h * 0.5f) * ELEVATION_RISE;
            out.push_back(world);
        }
    });
}

void Map::SurfaceSpots(const SDL_FRect& rect, Uint8 surface, float cell, vector<SDL_FPoint>& out) const {
    if (!loaded || cell <= 0.0f) return;
    const int cols = std::max(1, static_cast<int>(std::ceil(rect.w / cell)));
    const int rows = std::max(1, static_cast<int>(std::ceil(rect.h / cell)));
    vector<float> sx(static_cast<size_t>(cols) * rows, 0.0f), sy(sx.size(), 0.0f), n(sx.size(), 0.0f);
    static vector<int> seen_stamp;
    static int stamp_counter = 0;
    if (seen_stamp.size() != tiles.size()) seen_stamp.assign(tiles.size(), 0);
    const int stamp = ++stamp_counter;
    ForEachChunkInRect(rect, [&](const Chunk& c) {
        for (int idx : c.layers[LAYER_GROUND]) {
            if (seen_stamp[idx] == stamp) continue;
            seen_stamp[idx] = stamp;
            const TileInstance& t = tiles[idx];
            if (t.overlay || SurfaceOf(t.tex) != surface) continue;
            const float x = t.rect.x + t.rect.w * 0.5f, y0 = t.rect.y + t.rect.h * 0.5f;
            const float y = y0 - LevelAt(x, y0) * ELEVATION_RISE;
            const int gx = static_cast<int>(std::floor((x - rect.x) / cell));
            const int gy = static_cast<int>(std::floor((y - rect.y) / cell));
            if (gx < 0 || gy < 0 || gx >= cols || gy >= rows) continue;
            const size_t i = static_cast<size_t>(gy) * cols + gx;
            sx[i] += x; sy[i] += y; n[i] += 1.0f;
        }
    });
    for (size_t i = 0; i < n.size(); ++i)
        if (n[i] > 0.0f) out.push_back({sx[i] / n[i], sy[i] / n[i]});
}

void Map::RenderTile(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
                     const TileInstance& t, Uint8 alpha) const {
    if (t.tex < 0 || t.tex >= static_cast<int>(textures.size())) return;
    SDL_Texture* tex = cache.Get(textures[t.tex]);
    if (!tex) return;
    // Scenery is lifted by the terrain under its base, not its middle: a tree
    // standing at the lip of a bank belongs to the ground its trunk is on.
    SDL_FRect world = t.rect;
    world.y -= HeightAt(world.x + world.w * 0.5f, world.y + world.h);
    const SDL_FRect dst = cam.ToScreenRect(world);
    const Shaders::PropKind kind = ArtOf(t.tex).kind;
    if (kind != Shaders::PROP_NONE) Shaders::UseTile(r, Shaders::PLAIN, kind);
    if (alpha != 255) SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(r, tex, nullptr, &dst);
    if (alpha != 255) SDL_SetTextureAlphaMod(tex, 255);
    if (kind != Shaders::PROP_NONE) Shaders::UsePlain(r);
}

void Map::CollectDecor(const Camera& cam, vector<const TileInstance*>& out) const {
    if (!loaded) return;
    const SDL_FRect view = cam.VisibleWorldRect(96.0f);

    static vector<int> seen_stamp;
    static int stamp_counter = 0;
    if (seen_stamp.size() != tiles.size()) seen_stamp.assign(tiles.size(), 0);
    const int stamp = ++stamp_counter;

    ForEachChunkInRect(view, [&](const Chunk& c) {
        for (int idx : c.layers[LAYER_DECOR]) {
            if (seen_stamp[idx] == stamp) continue;
            seen_stamp[idx] = stamp;
            const TileInstance& t = tiles[idx];
            if (RectsOverlap(t.rect, view)) out.push_back(&t);
        }
    });
}

// A step that changes terrain level is only allowed on a ramp. Checked
// separately from Blocked() because it is a property of the movement, not of
// the destination: standing on a ledge is fine, walking off it is not.
bool Map::LevelChangeBlocked(float from_x, float from_y,
                             float to_x, float to_y) const {
    if (elev.empty()) return false;
    const int a = LevelAt(from_x, from_y);
    const int b = LevelAt(to_x, to_y);
    if (a == b) return false;
    return !(RampAt(from_x, from_y) || RampAt(to_x, to_y));
}

bool Map::Blocked(const SDL_FRect& box, bool swims) const {
    if (!loaded) return false;

    // Map edges are walls, so the player cannot walk off a finished map.
    // A swimmer is no exception: the pond stops at the map's edge too.
    if (bounds_w > 0 && bounds_h > 0) {
        if (box.x < 0 || box.y < 0 ||
            box.x + box.w > bounds_w || box.y + box.h > bounds_h)
            return true;
    }

    bool hit = false;
    ForEachChunkInRect(box, [&](const Chunk& c) {
        if (hit) return;
        for (int idx : c.colliders) {
            if (swims && collider_water[idx]) continue;
            if (RectsOverlap(box, colliders[idx])) { hit = true; return; }
        }
    });
    return hit;
}

bool Map::InWater(float x, float y) const {
    if (!loaded || water_count == 0) return false;
    const SDL_FRect point{x, y, 1.0f, 1.0f};
    bool wet = false;
    ForEachChunkInRect(point, [&](const Chunk& c) {
        if (wet) return;
        for (int idx : c.colliders)
            if (collider_water[idx] && RectsOverlap(point, colliders[idx])) { wet = true; return; }
    });
    return wet;
}

// Which face was struck is worked out the way the slide below works out which
// axis to stop: by trying each axis on its own. If moving in X alone is clear,
// then it was the Y movement that hit something, so the wall is a floor or a
// ceiling and its normal points back along Y. If neither axis is clear on its
// own the box went into a corner, and both components are taken.
Map::Contact Map::SweepPoint(float x, float y, float dx, float dy,
                             float radius) const {
    auto box_at = [radius](float cx, float cy) {
        return SDL_FRect{cx - radius, cy - radius, radius * 2, radius * 2};
    };

    Contact c;
    c.x = x;
    c.y = y;
    if (!Blocked(box_at(x + dx, y + dy))) return c;

    c.hit = true;
    const bool x_clear = (dx != 0.0f) && !Blocked(box_at(x + dx, y));
    const bool y_clear = (dy != 0.0f) && !Blocked(box_at(x, y + dy));

    if (x_clear && !y_clear) {
        // Slid along in X, stopped in Y: a horizontal surface.
        c.x = x + dx;
        c.ny = (dy > 0.0f) ? -1.0f : 1.0f;
    } else if (y_clear && !x_clear) {
        c.y = y + dy;
        c.nx = (dx > 0.0f) ? -1.0f : 1.0f;
    } else {
        // A corner, or a head-on hit along a single axis. Push back along
        // whichever components were actually moving.
        c.nx = (dx > 0.0f) ? -1.0f : (dx < 0.0f ? 1.0f : 0.0f);
        c.ny = (dy > 0.0f) ? -1.0f : (dy < 0.0f ? 1.0f : 0.0f);
        const float len = Length(c.nx, c.ny);
        if (len > 0.0f) { c.nx /= len; c.ny /= len; }
    }

    // A projectile spawned inside geometry -- fired with your back to a wall,
    // say -- has no clear position to report. Say so with a zero normal rather
    // than inventing one, and let the caller simply stop it.
    if (Blocked(box_at(c.x, c.y))) {
        c.x = x;
        c.y = y;
        if (Blocked(box_at(x, y))) { c.nx = 0.0f; c.ny = 0.0f; }
    }
    return c;
}

// Move each axis on its own so running into a wall diagonally slides along it
// instead of stopping dead.
SDL_FPoint Map::MoveWithCollision(const SDL_FRect& box, float dx, float dy,
                                  bool swims) const {
    SDL_FRect b = box;

    auto step_ok = [&](const SDL_FRect& from, const SDL_FRect& to) {
        if (Blocked(to, swims)) return false;
        return !LevelChangeBlocked(from.x + from.w * 0.5f, from.y + from.h * 0.5f,
                                   to.x + to.w * 0.5f, to.y + to.h * 0.5f);
    };

    if (dx != 0.0f) {
        SDL_FRect test = b;
        test.x += dx;
        if (step_ok(b, test)) {
            b.x = test.x;
        } else {
            // Creep up to the obstacle so the player sits flush against it.
            const float step = (dx > 0) ? 1.0f : -1.0f;
            for (float moved = 0; fabsf(moved) < fabsf(dx); moved += step) {
                SDL_FRect probe = b;
                probe.x += step;
                if (!step_ok(b, probe)) break;
                b.x = probe.x;
            }
        }
    }

    if (dy != 0.0f) {
        SDL_FRect test = b;
        test.y += dy;
        if (step_ok(b, test)) {
            b.y = test.y;
        } else {
            const float step = (dy > 0) ? 1.0f : -1.0f;
            for (float moved = 0; fabsf(moved) < fabsf(dy); moved += step) {
                SDL_FRect probe = b;
                probe.y += step;
                if (!step_ok(b, probe)) break;
                b.y = probe.y;
            }
        }
    }

    return {b.x, b.y};
}

const Hazard* Map::HazardAt(const SDL_FRect& box) const {
    for (const Hazard& h : hazards)
        if (box.x < h.rect.x + h.rect.w && h.rect.x < box.x + box.w &&
            box.y < h.rect.y + h.rect.h && h.rect.y < box.y + box.h)
            return &h;
    return nullptr;
}

const Portal* Map::PortalAt(const SDL_FRect& box) const {
    for (const auto& p : portals)
        if (RectsOverlap(box, p.rect)) return &p;
    return nullptr;
}

bool Map::Spawn(const string& name, SDL_FPoint& out) const {
    auto it = spawns.find(name);
    if (it == spawns.end()) return false;
    out = it->second;
    return true;
}

SDL_FPoint Map::DefaultSpawn() const {
    SDL_FPoint p;
    if (Spawn("default", p)) return p;
    if (!spawns.empty()) return spawns.begin()->second;
    return {bounds_w / 2.0f, bounds_h / 2.0f};
}
