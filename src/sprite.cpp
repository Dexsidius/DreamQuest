#include "sprite.h"
#include <fstream>

LayerSlot LayerSlotFromName(const string& name) {
    if (name == "shadow")        return LayerSlot::Shadow;
    if (name == "weapon_back")   return LayerSlot::WeaponBack;
    if (name == "head")          return LayerSlot::Head;
    if (name == "weapon_front")  return LayerSlot::WeaponFront;
    if (name == "effect")        return LayerSlot::Effect;
    if (name == "armour_legs")   return LayerSlot::ArmourLegs;
    if (name == "armour_body")   return LayerSlot::ArmourBody;
    if (name == "armour_hands")  return LayerSlot::ArmourHands;
    if (name == "armour_head")   return LayerSlot::ArmourHead;
    if (name == "armour_shield") return LayerSlot::ArmourShield;
    // weapon_sword_iron, weapon_bow_wood and the like: alternates for the
    // weapon layer, not layers in their own right.
    if (name.rfind("weapon_", 0) == 0) return LayerSlot::WeaponAlt;
    // armour_body_light, armour_head_ornate: the other cuts of the same piece.
    if (name.rfind("armour_", 0) == 0) return LayerSlot::ArmourAlt;
    return LayerSlot::Body;
}

int ArmourLayerOf(LayerSlot slot) {
    switch (slot) {
        case LayerSlot::ArmourLegs:   return ARMOUR_LEGS;
        case LayerSlot::ArmourBody:   return ARMOUR_BODY;
        case LayerSlot::ArmourHands:  return ARMOUR_HANDS;
        case LayerSlot::ArmourHead:   return ARMOUR_HEAD;
        case LayerSlot::ArmourShield: return ARMOUR_SHIELD;
        default: return -1;
    }
}

const AnimClip* SpriteDef::Find(const string& clip) const {
    auto it = clips.find(clip);
    return it == clips.end() ? nullptr : &it->second;
}

bool SpriteLibrary::Load(const string& json_path) {
    std::ifstream in(json_path);
    if (!in) {
        SDL_Log("SpriteLibrary: cannot open '%s'", json_path.c_str());
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("SpriteLibrary: bad JSON in '%s': %s", json_path.c_str(), e.what());
        return false;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        SpriteDef d;
        d.name = it.key();
        const json& o = it.value();

        const string dir = o.value("dir", string(""));
        d.dir        = dir;
        d.weapon_dir = o.value("weapons_from", string(""));     // a sprite id for now; a folder once all are read
        d.rows     = o.value("rows", 4);
        d.anchor_y = o.value("anchor_y", 54.0f);
        d.scale    = o.value("scale", 1.0f);

        if (o.contains("clips")) {
            for (auto c = o["clips"].begin(); c != o["clips"].end(); ++c) {
                AnimClip a;
                a.sheet  = dir + c.value().value("sheet", string(""));
                a.frames = std::max(1, c.value().value("frames", 1));
                a.fps    = c.value().value("fps", 10.0f);
                a.loop   = c.value().value("loop", true);
                if (c.value().contains("row_frames"))
                    for (const auto& n : c.value()["row_frames"])
                        a.row_frames.push_back(std::max(1, n.get<int>()));

                if (c.value().contains("layers"))
                    for (const auto& l : c.value()["layers"]) {
                        AnimLayer layer;
                        layer.slot  = LayerSlotFromName(l.value("slot", string("body")));
                        layer.sheet = dir + l.value("sheet", string(""));
                        a.layers.push_back(layer);
                    }
                d.clips[c.key()] = a;
            }
        }
        defs[d.name] = d;
    }

    // "weapons_from" named a sprite; what is wanted is that sprite's folder.
    for (auto& kv : defs) {
        if (kv.second.weapon_dir.empty()) continue;
        const auto from = defs.find(kv.second.weapon_dir);
        if (from == defs.end()) {
            SDL_Log("SpriteLibrary: '%s' takes its weapons from '%s', which is not a sprite",
                    kv.first.c_str(), kv.second.weapon_dir.c_str());
            kv.second.weapon_dir.clear();
        } else {
            kv.second.weapon_dir = from->second.dir;
        }
    }

    SDL_Log("SpriteLibrary: loaded %d sprite definitions", static_cast<int>(defs.size()));
    return true;
}

string SpriteDef::WeaponSheet(const string& generic_sheet, const string& model) const {
    const size_t at = generic_sheet.rfind("weapon_front");
    if (at == string::npos || model.empty()) return "";
    string path = generic_sheet;
    path.replace(at, 12, "weapon_" + model);
    // The warden and the wayfarer hold the hero's sheets: one rig, one set of
    // weapon renders. Asking for them in their own folders found nothing, fell
    // back to the plain tinted blade -- so a bow and a staff were both drawn as
    // a sword -- and said so in the log once for every weapon and every clip.
    if (!weapon_dir.empty() && !dir.empty() && path.rfind(dir, 0) == 0)
        path = weapon_dir + path.substr(dir.size());
    return path;
}

const SpriteDef* SpriteLibrary::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

void Sprite::Play(const string& name, bool restart) {
    if (!def) return;
    if (current == name && !restart) return;

    const AnimClip* next = def->Find(name);
    // Fall back to idle rather than blanking out when a clip is missing.
    if (!next) {
        next = def->Find("idle");
        if (!next) return;
    }
    current  = name;
    clip     = next;
    frame    = 0;
    timer    = 0.0f;
    finished = false;
}

int Sprite::FrameCount() const {
    if (!clip || !def) return 1;
    const int row = (def->rows > 1) ? static_cast<int>(facing) % def->rows : 0;
    return clip->FramesForRow(row);
}

void Sprite::Update(float dt) {
    if (!clip || clip->fps <= 0.0f) return;

    // Count against the row being faced. Turning to a shorter row mid-clip
    // would otherwise leave the frame index past the end of the art.
    const int count = FrameCount();
    if (frame >= count) frame = clip->loop ? 0 : count - 1;
    if (count <= 1) return;

    const float frame_time = 1.0f / (clip->fps * std::max(0.05f, speed_scale));
    timer += dt;
    while (timer >= frame_time) {
        timer -= frame_time;
        if (frame + 1 >= count) {
            if (clip->loop) {
                frame = 0;
            } else {
                frame = count - 1;
                finished = true;
                return;
            }
        } else {
            ++frame;
        }
    }
}

float Sprite::Progress() const {
    const int count = FrameCount();
    if (count <= 0) return 1.0f;
    return static_cast<float>(frame) / static_cast<float>(count);
}

// Side of one animation frame in source pixels. Frames are square, so the row
// height is also the frame width.
int Sprite::FrameSize() const {
    if (!def) return 64;
    return (def->rows > 1) ? 64 : 64;
}

SDL_FRect Sprite::WorldBounds(float wx, float wy) const {
    if (!def || !clip) return {wx, wy, 0, 0};
    // Frames are square, so the row height is also the frame width.
    const float k = def->scale * size_scale;
    return {wx - 32.0f * k, wy - def->anchor_y * k, 64.0f * k, 64.0f * k};
}

// Draws the parts in order, giving each the colour its slot calls for. The
// incoming tint (a hurt flash, a charge glow) multiplies through every layer so
// the character still reads as one thing.
bool Sprite::DrawLayers(SDL_Renderer* r, TextureCache& cache,
                        const SDL_FRect& dst, int shown, int row,
                        SDL_Color tint) const {
    if (!clip || clip->layers.empty() || !use_layers) return false;

    auto blend = [](SDL_Color a, SDL_Color b) {
        return SDL_Color{static_cast<Uint8>(a.r * b.r / 255),
                         static_cast<Uint8>(a.g * b.g / 255),
                         static_cast<Uint8>(a.b * b.b / 255),
                         static_cast<Uint8>(a.a * b.a / 255)};
    };

    const int facing_index = static_cast<int>(facing) % 4;

    // Worn kit anchored to the frame rather than to the sheet, so one piece of
    // art sits correctly on every clip.
    auto draw_attachments = [&](LayerSlot after) {
        for (const Attachment& a : style.attachments) {
            if (a.after != after) continue;
            if (!a.facings[facing_index]) continue;

            SDL_Texture* tex = cache.Get(a.sprite);
            if (!tex) continue;

            // Map the frame-pixel rectangle into the on-screen frame.
            const float sx = dst.w / static_cast<float>(FrameSize());
            const float sy = dst.h / static_cast<float>(FrameSize());
            // Mirroring reflects the rectangle across the middle of the frame
            // as well as the art, so the piece swaps sides rather than turning
            // over where it stands.
            const bool mirror = a.mirror_facing_right && facing_index == 2;
            const float rx = mirror ? FrameSize() - (a.rect.x + a.rect.w) : a.rect.x;
            const SDL_FRect box = {dst.x + rx * sx, dst.y + a.rect.y * sy,
                                   a.rect.w * sx, a.rect.h * sy};

            SDL_SetTextureColorMod(tex, a.tint.r * tint.r / 255,
                                        a.tint.g * tint.g / 255,
                                        a.tint.b * tint.b / 255);
            SDL_SetTextureAlphaMod(tex, a.tint.a * tint.a / 255);
            if (mirror) {
                SDL_RenderTextureRotated(r, tex, nullptr, &box, 0.0, nullptr,
                                         SDL_FLIP_HORIZONTAL);
            } else {
                SDL_RenderTexture(r, tex, nullptr, &box);
            }
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);
        }
    };

    bool drew = false;
    for (const AnimLayer& layer : clip->layers) {
        // Every tier's weapon sheet is listed; the one in hand is drawn by the
        // weapon_front swap below, and the rest are not drawn at all.
        if (layer.slot == LayerSlot::WeaponAlt) continue;
        // Likewise the light and ornate cuts of each plate piece.
        if (layer.slot == LayerSlot::ArmourAlt) continue;

        if (!style.show_weapon && (layer.slot == LayerSlot::WeaponBack ||
                                   layer.slot == LayerSlot::WeaponFront)) {
            draw_attachments(layer.slot);
            continue;
        }

        // A piece of plate is drawn only when it is worn.
        const int armour = ArmourLayerOf(layer.slot);
        if (armour >= 0 && !style.armour[armour].show) continue;

        SDL_Texture* tex = nullptr;
        bool model_sheet = false;
        if (layer.slot == LayerSlot::WeaponFront && !style.weapon_model.empty()) {
            // layers/attack_4_weapon_front.png -> layers/attack_4_weapon_sword_iron.png
            const string path = def->WeaponSheet(layer.sheet, style.weapon_model);
            if (!path.empty()) {
                tex = cache.Get(path);
                model_sheet = tex != nullptr;
            }
        }
        if (armour >= 0 && !style.armour[armour].cut.empty()) {
            // layers/idle_7_armour_body.png -> layers/idle_7_armour_body_light.png
            string path = layer.sheet;
            const size_t dot = path.rfind(".png");
            if (dot != string::npos) {
                path.insert(dot, "_" + style.armour[armour].cut);
                tex = cache.Get(path);
            }
        }
        if (!tex) tex = cache.Get(layer.sheet);
        if (!tex) continue;

        float tw = 0, th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        if (tw <= 0 || th <= 0) continue;

        const float fw = tw / static_cast<float>(clip->frames);
        const float fh = th / static_cast<float>(std::max(1, def->rows));
        const SDL_FRect src = {shown * fw, row * fh, fw, fh};

        SDL_Color c = tint;
        if (armour >= 0) c = blend(tint, style.armour[armour].tint);
        switch (layer.slot) {
            case LayerSlot::Body:        c = blend(tint, style.body); break;
            case LayerSlot::Head:        c = blend(tint, style.head); break;
            case LayerSlot::WeaponBack:
            case LayerSlot::WeaponFront: c = model_sheet ? tint : blend(tint, style.weapon); break;
            // The shadow is not part of the character, so it keeps its own
            // colour rather than glowing when the player charges an attack.
            case LayerSlot::Shadow:      c = SDL_Color{255, 255, 255, tint.a}; break;
            default: break;
        }

        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(tex, c.a);
        SDL_RenderTexture(r, tex, &src, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
        drew = true;

        draw_attachments(layer.slot);
    }
    return drew;
}

void Sprite::Draw(SDL_Renderer* r, TextureCache& cache, const Camera& cam,
                  float wx, float wy, SDL_Color tint, SDL_BlendMode blend, float grow) const {
    if (!def || !clip) return;

    SDL_Texture* tex = cache.Get(clip->sheet);
    if (!tex) return;

    float tw = 0, th = 0;
    SDL_GetTextureSize(tex, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    const float fw = tw / static_cast<float>(clip->frames);
    const float fh = th / static_cast<float>(std::max(1, def->rows));
    const int   row = (def->rows > 1) ? static_cast<int>(facing) % def->rows : 0;
    const int   shown = std::min(frame, clip->FramesForRow(row) - 1);

    const SDL_FRect src = {shown * fw, row * fh, fw, fh};

    // The entity's world position is its feet; the frame hangs above it.
    const float k = def->scale * size_scale;
    SDL_FRect world = {wx - (fw * k) / 2.0f,
                       wy - def->anchor_y * k,
                       fw * k, fh * k};
    if (grow != 1.0f) {
        const float cx = world.x + world.w / 2.0f, cy = world.y + world.h / 2.0f;
        world.w *= grow;
        world.h *= grow;
        world.x = cx - world.w / 2.0f;
        world.y = cy - world.h / 2.0f;
    }
    const SDL_FRect dst = cam.ToScreenRect(world);

    if (blend == SDL_BLENDMODE_BLEND && grow == 1.0f && DrawLayers(r, cache, dst, shown, row, tint)) return;

    SDL_SetTextureBlendMode(tex, blend);
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    // The cache hands the same texture to everything that draws this sheet.
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
}

void Sprite::DrawAt(SDL_Renderer* r, TextureCache& cache,
                    const SDL_FRect& dst, SDL_Color tint) const {
    if (!def || !clip) return;

    SDL_Texture* tex = cache.Get(clip->sheet);
    if (!tex) return;

    float tw = 0, th = 0;
    SDL_GetTextureSize(tex, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    const float fw = tw / static_cast<float>(clip->frames);
    const float fh = th / static_cast<float>(std::max(1, def->rows));
    const int   row = (def->rows > 1) ? static_cast<int>(facing) % def->rows : 0;
    const int   shown = std::min(frame, clip->FramesForRow(row) - 1);
    const SDL_FRect src = {shown * fw, row * fh, fw, fh};

    // The same stack the world draws, so a character shown in a menu is
    // holding and wearing what they would be holding and wearing out there:
    // the character-select cards used to draw the rig's own sheet, which puts
    // a sword in every hand whatever the character fights with.
    if (DrawLayers(r, cache, dst, shown, row, tint)) return;

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}
