#include "sprite.h"
#include <fstream>

LayerSlot LayerSlotFromName(const string& name) {
    if (name == "shadow")       return LayerSlot::Shadow;
    if (name == "weapon_back")  return LayerSlot::WeaponBack;
    if (name == "head")         return LayerSlot::Head;
    if (name == "weapon_front") return LayerSlot::WeaponFront;
    if (name == "effect")       return LayerSlot::Effect;
    return LayerSlot::Body;
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

    SDL_Log("SpriteLibrary: loaded %d sprite definitions", static_cast<int>(defs.size()));
    return true;
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

    const float frame_time = 1.0f / clip->fps;
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
    return {wx - 32.0f * def->scale, wy - def->anchor_y * def->scale,
            64.0f * def->scale, 64.0f * def->scale};
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
            const SDL_FRect box = {dst.x + a.rect.x * sx, dst.y + a.rect.y * sy,
                                   a.rect.w * sx, a.rect.h * sy};

            SDL_SetTextureColorMod(tex, a.tint.r * tint.r / 255,
                                        a.tint.g * tint.g / 255,
                                        a.tint.b * tint.b / 255);
            SDL_SetTextureAlphaMod(tex, a.tint.a * tint.a / 255);
            SDL_RenderTexture(r, tex, nullptr, &box);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);
        }
    };

    bool drew = false;
    for (const AnimLayer& layer : clip->layers) {
        if (!style.show_weapon && (layer.slot == LayerSlot::WeaponBack ||
                                   layer.slot == LayerSlot::WeaponFront)) {
            draw_attachments(layer.slot);
            continue;
        }

        SDL_Texture* tex = cache.Get(layer.sheet);
        if (!tex) continue;

        float tw = 0, th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        if (tw <= 0 || th <= 0) continue;

        const float fw = tw / static_cast<float>(clip->frames);
        const float fh = th / static_cast<float>(std::max(1, def->rows));
        const SDL_FRect src = {shown * fw, row * fh, fw, fh};

        SDL_Color c = tint;
        switch (layer.slot) {
            case LayerSlot::Body:        c = blend(tint, style.body); break;
            case LayerSlot::Head:        c = blend(tint, style.head); break;
            case LayerSlot::WeaponBack:
            case LayerSlot::WeaponFront: c = blend(tint, style.weapon); break;
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
                  float wx, float wy, SDL_Color tint) const {
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
    const SDL_FRect world = {wx - (fw * def->scale) / 2.0f,
                             wy - def->anchor_y * def->scale,
                             fw * def->scale, fh * def->scale};
    const SDL_FRect dst = cam.ToScreenRect(world);

    if (DrawLayers(r, cache, dst, shown, row, tint)) return;

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
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

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}
