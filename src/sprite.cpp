#include "sprite.h"
#include <fstream>

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

void Sprite::Update(float dt) {
    if (!clip || clip->frames <= 1 || clip->fps <= 0.0f) return;

    const float frame_time = 1.0f / clip->fps;
    timer += dt;
    while (timer >= frame_time) {
        timer -= frame_time;
        if (frame + 1 >= clip->frames) {
            if (clip->loop) {
                frame = 0;
            } else {
                frame = clip->frames - 1;
                finished = true;
                return;
            }
        } else {
            ++frame;
        }
    }
}

float Sprite::Progress() const {
    if (!clip || clip->frames <= 0) return 1.0f;
    return static_cast<float>(frame) / static_cast<float>(clip->frames);
}

SDL_FRect Sprite::WorldBounds(float wx, float wy) const {
    if (!def || !clip) return {wx, wy, 0, 0};
    // Frames are square, so the row height is also the frame width.
    return {wx - 32.0f * def->scale, wy - def->anchor_y * def->scale,
            64.0f * def->scale, 64.0f * def->scale};
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

    const SDL_FRect src = {frame * fw, row * fh, fw, fh};

    // The entity's world position is its feet; the frame hangs above it.
    const SDL_FRect world = {wx - (fw * def->scale) / 2.0f,
                             wy - def->anchor_y * def->scale,
                             fw * def->scale, fh * def->scale};
    const SDL_FRect dst = cam.ToScreenRect(world);

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
    const SDL_FRect src = {frame * fw, row * fh, fw, fh};

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderTexture(r, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}
