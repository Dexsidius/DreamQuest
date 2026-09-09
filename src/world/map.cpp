#include "map.h"
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
    ambient    = dq.value("ambient", string("overworld"));
    background = ColorFromJson(dq.contains("background") ? dq["background"] : json(),
                               interior ? SDL_Color{18, 14, 20, 255}
                                        : SDL_Color{34, 48, 34, 255});

    // ---- tiles (native LevelEdit-Plus section) ------------------------------
    if (root.contains("tiles")) {
        for (auto it = root["tiles"].begin(); it != root["tiles"].end(); ++it) {
            const string& tile_name = it.key();
            const json& entry = it.value();
            if (!entry.contains("locations")) continue;

            textures.push_back(ResolveAsset(entry.value("filepath", string(""))));
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

    // ---- explicit collision boxes -------------------------------------------
    if (dq.contains("collision"))
        for (const auto& c : dq["collision"])
            if (c.is_array() && c.size() >= 4)
                AddCollider({c[0].get<float>(), c[1].get<float>(),
                             c[2].get<float>(), c[3].get<float>()});

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
            portals.push_back(portal);
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
            m.text         = o.value("text", string(""));
            m.starts_quest = o.value("starts_quest", string(""));
            m.sprite       = o.value("sprite", string(""));
            m.sprite_open  = o.value("sprite_open", string(""));
            m.skill        = o.value("skill", string(""));
            m.skill_level  = o.value("skill_level", 1);
            m.yield        = o.value("yield", string(""));
            m.yield_xp     = o.value("yield_xp", 0);
            m.gather_time  = o.value("gather_time", 2.6f);
            m.title        = o.value("title", string(""));
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
    textures.clear(); tiles.clear(); colliders.clear();
    portals.clear(); enemies.clear(); npcs.clear(); objects.clear();
    spawns.clear(); chunks.clear();
    bounds_w = bounds_h = 0;
    chunk_cols = chunk_rows = 0;
    id.clear(); display_name.clear(); source_dir.clear();
}

void Map::AddCollider(const SDL_FRect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    colliders.push_back(r);
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

void Map::RenderLayer(SDL_Renderer* r, TextureCache& cache,
                      const Camera& cam, int layer) const {
    if (!loaded || layer < 0 || layer > 2) return;
    const SDL_FRect view = cam.VisibleWorldRect(96.0f);

    // A tile can straddle a chunk border and be listed twice; skip repeats.
    static vector<int> seen_stamp;
    static int stamp_counter = 0;
    if (seen_stamp.size() != tiles.size()) seen_stamp.assign(tiles.size(), 0);
    const int stamp = ++stamp_counter;

    ForEachChunkInRect(view, [&](const Chunk& c) {
        for (int idx : c.layers[layer]) {
            if (seen_stamp[idx] == stamp) continue;
            seen_stamp[idx] = stamp;

            const TileInstance& t = tiles[idx];
            if (!RectsOverlap(t.rect, view)) continue;

            SDL_Texture* tex = cache.Get(textures[t.tex]);
            if (!tex) continue;
            const SDL_FRect dst = cam.ToScreenRect(t.rect);
            SDL_RenderTexture(r, tex, nullptr, &dst);
        }
    });
}

void Map::RenderTile(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
                     const TileInstance& t, Uint8 alpha) const {
    if (t.tex < 0 || t.tex >= static_cast<int>(textures.size())) return;
    SDL_Texture* tex = cache.Get(textures[t.tex]);
    if (!tex) return;
    const SDL_FRect dst = cam.ToScreenRect(t.rect);
    if (alpha != 255) SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(r, tex, nullptr, &dst);
    if (alpha != 255) SDL_SetTextureAlphaMod(tex, 255);
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

bool Map::Blocked(const SDL_FRect& box) const {
    if (!loaded) return false;

    // Map edges are walls, so the player cannot walk off a finished map.
    if (bounds_w > 0 && bounds_h > 0) {
        if (box.x < 0 || box.y < 0 ||
            box.x + box.w > bounds_w || box.y + box.h > bounds_h)
            return true;
    }

    bool hit = false;
    ForEachChunkInRect(box, [&](const Chunk& c) {
        if (hit) return;
        for (int idx : c.colliders)
            if (RectsOverlap(box, colliders[idx])) { hit = true; return; }
    });
    return hit;
}

// Move each axis on its own so running into a wall diagonally slides along it
// instead of stopping dead.
SDL_FPoint Map::MoveWithCollision(const SDL_FRect& box, float dx, float dy) const {
    SDL_FRect b = box;

    if (dx != 0.0f) {
        SDL_FRect test = b;
        test.x += dx;
        if (!Blocked(test)) {
            b.x = test.x;
        } else {
            // Creep up to the obstacle so the player sits flush against it.
            const float step = (dx > 0) ? 1.0f : -1.0f;
            for (float moved = 0; fabsf(moved) < fabsf(dx); moved += step) {
                SDL_FRect probe = b;
                probe.x += step;
                if (Blocked(probe)) break;
                b.x = probe.x;
            }
        }
    }

    if (dy != 0.0f) {
        SDL_FRect test = b;
        test.y += dy;
        if (!Blocked(test)) {
            b.y = test.y;
        } else {
            const float step = (dy > 0) ? 1.0f : -1.0f;
            for (float moved = 0; fabsf(moved) < fabsf(dy); moved += step) {
                SDL_FRect probe = b;
                probe.y += step;
                if (Blocked(probe)) break;
                b.y = probe.y;
            }
        }
    }

    return {b.x, b.y};
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
