#include "shaders.h"
#include "../world/map.h"
#include "../camera.h"

namespace Shaders {
namespace {

constexpr float kTexelPx = 16.0f;         // world pixels per field texel
constexpr const char* kShaderFiles[3] = {
    "assets/shaders/water.frag.spv",
    "assets/shaders/lava.frag.spv",
    "assets/shaders/heat.frag.spv",
};
enum { S_WATER = 0, S_LAVA = 1, S_HEAT = 2 };

// The uniform block every shader here declares as `View`: three vec4s.
struct View {
    float cam[4];      // camera xpos, ypos, zoom; seconds
    float field[4];    // world px per texel; the field's width and height in texels
    float target[4];   // the size of the view being drawn
};

// One map's field, and the render states that read it: a render state is
// made with its textures bound, so each field has its own three.
struct Slot {
    const Map* map = nullptr;
    string id;
    size_t tile_count = 0;
    float width = 0, height = 0;

    int cols = 0, rows = 0;
    vector<Uint8> lava;                    // one per texel, for "is any in view"
    SDL_Texture* texture = nullptr;
    SDL_GPURenderState* state[3] = {};
    Uint64 used = 0;

    bool Holds(const Map& m) const {
        return map == &m && id == m.Id() && tile_count == m.Tiles().size() &&
               width == m.Width() && height == m.Height();
    }
};

SDL_Renderer*   g_renderer = nullptr;
SDL_GPUDevice*  g_device = nullptr;
SDL_GPUShader*  g_shader[3] = {};
SDL_GPUSampler* g_sampler = nullptr;
bool            g_enabled = false;

// Split screen can have the two seats on two maps; one field each.
Slot     g_fields[2];
Slot*    g_current = nullptr;              // the view being drawn, if it has fluid
Surface  g_surface = PLAIN;                // what the renderer is set to now
Uint64   g_views = 0;

SDL_Texture* g_scene = nullptr;
int g_scene_w = 0, g_scene_h = 0;

bool IsNumbered(const string& stem, const string& base) {
    if (stem == base) return true;
    if (stem.size() <= base.size() + 1 || stem.compare(0, base.size() + 1, base + "_") != 0)
        return false;
    for (size_t i = base.size() + 1; i < stem.size(); ++i)
        if (stem[i] < '0' || stem[i] > '9') return false;
    return true;
}

void Release(Slot& f) {
    if (g_renderer) SDL_FlushRenderer(g_renderer);
    for (SDL_GPURenderState*& s : f.state) { if (s) SDL_DestroyGPURenderState(s); s = nullptr; }
    if (f.texture) SDL_DestroyTexture(f.texture);
    f = Slot{};
}

// Which way the fluid runs at each texel, from the shape of the fluid: a run
// that is long one way and narrow the other is a river, and runs along its
// length -- south if it is upright, east if it lies across. A run wide both
// ways is a pond, or the Bayou's open water, and does not run at all. Then
// the directions are smoothed along the fluid, so a river that wanders bends
// its current with it instead of snapping from south to east.
void Currents(const vector<Uint8>& kind, int cols, int rows, vector<float>& fx, vector<float>& fy) {
    const int n = cols * rows;
    vector<int> across(n, 0), down(n, 0);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols;) {
            const Uint8 k = kind[y * cols + x];
            int end = x + 1;
            while (end < cols && kind[y * cols + end] == k) ++end;
            for (int i = x; i < end; ++i) across[y * cols + i] = end - x;
            x = end;
        }
    for (int x = 0; x < cols; ++x)
        for (int y = 0; y < rows;) {
            const Uint8 k = kind[y * cols + x];
            int end = y + 1;
            while (end < rows && kind[end * cols + x] == k) ++end;
            for (int i = y; i < end; ++i) down[i * cols + x] = end - y;
            y = end;
        }

    fx.assign(n, 0.0f); fy.assign(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        if (kind[i] == PLAIN) continue;
        const int narrow = std::min(across[i], down[i]);
        const int length = std::max(across[i], down[i]);
        if (length < narrow * 2) continue;
        // Up to six texels (a hundred world pixels) across is a river; it
        // stills by fourteen.
        const float strength = std::clamp((14.0f - narrow) / 8.0f, 0.0f, 1.0f);
        if (down[i] > across[i]) fy[i] = strength; else fx[i] = strength;
    }

    for (int pass = 0; pass < 3; ++pass) {
        vector<float> nx(fx), ny(fy);
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
                const int i = y * cols + x;
                if (kind[i] == PLAIN) continue;
                float sx = 0, sy = 0; int count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= cols || yy >= rows) continue;
                        const int j = yy * cols + xx;
                        if (kind[j] != kind[i]) continue;
                        sx += fx[j]; sy += fy[j]; ++count;
                    }
                nx[i] = sx / count; ny[i] = sy / count;
            }
        fx.swap(nx); fy.swap(ny);
    }

    // The bank takes the current of the water beside it, so the field's
    // blending between texels does not slow the water down at its edge.
    vector<float> nx(fx), ny(fy);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            const int i = y * cols + x;
            if (kind[i] != PLAIN) continue;
            float sx = 0, sy = 0; int count = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= cols || yy >= rows) continue;
                    const int j = yy * cols + xx;
                    if (kind[j] == PLAIN) continue;
                    sx += fx[j]; sy += fy[j]; ++count;
                }
            if (count) { nx[i] = sx / count; ny[i] = sy / count; }
        }
    fx.swap(nx); fy.swap(ny);
}

bool Build(Slot& f, const Map& map) {
    f.map = &map;
    f.id = map.Id();
    f.tile_count = map.Tiles().size();
    f.width = map.Width();
    f.height = map.Height();
    const FieldData data = FieldOf(map);
    f.cols = data.cols;
    f.rows = data.rows;
    if (!data.any) return true;   // nothing to draw; the slot stays empty
    const vector<Uint8>& kind = data.kind;
    const vector<float>& fx = data.fx;
    const vector<float>& fy = data.fy;

    const size_t n = kind.size();
    vector<Uint8> texels(n * 4);
    f.lava.assign(n, 0);
    auto encode = [](float v) {
        return static_cast<Uint8>(std::clamp(std::lround(v * 127.5f + 127.5f), 0L, 255L));
    };
    for (size_t i = 0; i < n; ++i) {
        f.lava[i] = kind[i] == LAVA;
        texels[i * 4 + 0] = kind[i] == LAVA ? 255 : 0;
        texels[i * 4 + 1] = kind[i] == WATER ? 255 : 0;
        texels[i * 4 + 2] = encode(fx[i]);
        texels[i * 4 + 3] = encode(fy[i]);
    }

    f.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, f.cols, f.rows);
    if (!f.texture || !SDL_UpdateTexture(f.texture, nullptr, texels.data(), f.cols * 4)) {
        SDL_Log("Shaders: no field for '%s': %s", f.id.c_str(), SDL_GetError());
        return false;
    }
    auto* gpu = static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
        SDL_GetTextureProperties(f.texture), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    if (!gpu) return false;

    SDL_GPUTextureSamplerBinding binding{gpu, g_sampler};
    for (int i = 0; i < 3; ++i) {
        SDL_GPURenderStateCreateInfo info{};
        info.fragment_shader = g_shader[i];
        info.num_sampler_bindings = 1;
        info.sampler_bindings = &binding;
        f.state[i] = SDL_CreateGPURenderState(g_renderer, &info);
        if (!f.state[i]) {
            SDL_Log("Shaders: render state %d for '%s': %s", i, f.id.c_str(), SDL_GetError());
            return false;
        }
    }
    return true;
}

Slot* FieldFor(const Map& map) {
    for (Slot& f : g_fields)
        if (f.Holds(map)) return &f;
    Slot& slot = g_fields[0].used <= g_fields[1].used ? g_fields[0] : g_fields[1];
    Release(slot);
    if (!Build(slot, map)) {
        // Remember the map anyway, without its states, so a failure is not
        // retried every frame.
        for (SDL_GPURenderState*& s : slot.state) { if (s) SDL_DestroyGPURenderState(s); s = nullptr; }
    }
    return &slot;
}

bool LavaNear(const Slot& f, const SDL_FRect& view) {
    if (f.lava.empty()) return false;
    // Heat rises: lava a little below the view still wavers the air in it.
    const int x0 = std::max(0, static_cast<int>(view.x / kTexelPx));
    const int y0 = std::max(0, static_cast<int>(view.y / kTexelPx));
    const int x1 = std::min(f.cols - 1, static_cast<int>((view.x + view.w) / kTexelPx) + 1);
    const int y1 = std::min(f.rows - 1, static_cast<int>((view.y + view.h + 64.0f) / kTexelPx) + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (f.lava[static_cast<size_t>(y) * f.cols + x]) return true;
    return false;
}

}  // namespace

FieldData FieldOf(const Map& map) {
    FieldData d;
    d.texel = kTexelPx;
    d.cols = std::max(1, static_cast<int>(std::ceil(map.Width() / kTexelPx)));
    d.rows = std::max(1, static_cast<int>(std::ceil(map.Height() / kTexelPx)));

    // What the ground is at each texel, in the order the ground is drawn: a
    // later tile covers an earlier one. Where a tile stands on raised ground
    // it is drawn lifted, and so is its place in the field.
    d.kind.assign(static_cast<size_t>(d.cols) * d.rows, PLAIN);
    for (const TileInstance& t : map.Tiles()) {
        if (t.layer != LAYER_GROUND || t.overlay) continue;
        const Uint8 s = map.SurfaceOf(t.tex);
        d.any = d.any || s != PLAIN;
        const float lift = map.HasElevation()
            ? map.LevelAt(t.rect.x + t.rect.w * 0.5f, t.rect.y + t.rect.h * 0.5f) * ELEVATION_RISE : 0.0f;
        const int x0 = std::max(0, static_cast<int>(std::floor(t.rect.x / kTexelPx + 0.5f)));
        const int y0 = std::max(0, static_cast<int>(std::floor((t.rect.y - lift) / kTexelPx + 0.5f)));
        const int x1 = std::min(d.cols, static_cast<int>(std::floor((t.rect.x + t.rect.w) / kTexelPx + 0.5f)));
        const int y1 = std::min(d.rows, static_cast<int>(std::floor((t.rect.y + t.rect.h - lift) / kTexelPx + 0.5f)));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) d.kind[static_cast<size_t>(y) * d.cols + x] = s;
    }
    if (d.any) {
        Currents(d.kind, d.cols, d.rows, d.fx, d.fy);
    } else {
        d.fx.assign(d.kind.size(), 0.0f);
        d.fy.assign(d.kind.size(), 0.0f);
    }
    return d;
}

Surface SurfaceOfTile(const string& path) {
    const string stem = std::filesystem::path(path).stem().string();
    if (IsNumbered(stem, "water") || IsNumbered(stem, "bog_water")) return WATER;
    if (IsNumbered(stem, "lava")) return LAVA;
    return PLAIN;
}

SDL_Renderer* CreateRenderer(SDL_Window* window) {
    // Asked for something in particular: let SDL do exactly that.
    if (SDL_GetHint(SDL_HINT_RENDER_DRIVER)) return SDL_CreateRenderer(window, nullptr);

    // SDL_GPU on Direct3D 12 would want the shaders as DXIL; on Vulkan it
    // takes the SPIR-V glslc makes, and it is Vulkan the Steam Deck has.
    const bool chose_gpu_driver = SDL_GetHint(SDL_HINT_GPU_DRIVER) != nullptr;
    if (!chose_gpu_driver) SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
    if (SDL_Renderer* r = SDL_CreateRenderer(window, "gpu")) return r;

    SDL_Log("Shaders: no GPU renderer (%s); water and lava will be still", SDL_GetError());
    if (!chose_gpu_driver) SDL_ResetHint(SDL_HINT_GPU_DRIVER);
    return SDL_CreateRenderer(window, nullptr);
}

bool Init(SDL_Renderer* renderer) {
    Shutdown();
    if (!renderer) return false;
    const char* name = SDL_GetRendererName(renderer);
    if (!name || string(name) != "gpu") {
        SDL_Log("Shaders: renderer is %s, not gpu; no water or lava effects", name ? name : "unknown");
        return false;
    }
    g_device = static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(
        SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
    if (!g_device || !(SDL_GetGPUShaderFormats(g_device) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        SDL_Log("Shaders: the GPU device takes no SPIR-V; no water or lava effects");
        g_device = nullptr;
        return false;
    }
    g_renderer = renderer;

    for (int i = 0; i < 3; ++i) {
        size_t size = 0;
        void* code = SDL_LoadFile(kShaderFiles[i], &size);
        if (!code) {
            SDL_Log("Shaders: %s: %s", kShaderFiles[i], SDL_GetError());
            Shutdown();
            return false;
        }
        SDL_GPUShaderCreateInfo info{};
        info.code = static_cast<const Uint8*>(code);
        info.code_size = size;
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = 2;             // the tile's own texture, and the field
        info.num_uniform_buffers = 1;
        g_shader[i] = SDL_CreateGPUShader(g_device, &info);
        SDL_free(code);
        if (!g_shader[i]) {
            SDL_Log("Shaders: %s: %s", kShaderFiles[i], SDL_GetError());
            Shutdown();
            return false;
        }
    }

    // The field is blended between texels: its edges are the foam line and
    // the fading of the heat.
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g_sampler = SDL_CreateGPUSampler(g_device, &sampler);
    if (!g_sampler) {
        SDL_Log("Shaders: sampler: %s", SDL_GetError());
        Shutdown();
        return false;
    }

    g_enabled = true;
    SDL_Log("Shaders: water, lava and heat on %s", SDL_GetGPUDeviceDriver(g_device));
    return true;
}

void Shutdown() {
    if (g_renderer) {
        SDL_SetGPURenderState(g_renderer, nullptr);
        SDL_FlushRenderer(g_renderer);
    }
    for (Slot& f : g_fields) Release(f);
    if (g_scene) SDL_DestroyTexture(g_scene);
    g_scene = nullptr; g_scene_w = g_scene_h = 0;
    if (g_device) {
        if (g_sampler) SDL_ReleaseGPUSampler(g_device, g_sampler);
        for (SDL_GPUShader*& s : g_shader) { if (s) SDL_ReleaseGPUShader(g_device, s); s = nullptr; }
    }
    g_sampler = nullptr;
    g_device = nullptr;
    g_renderer = nullptr;
    g_current = nullptr;
    g_surface = PLAIN;
    g_enabled = false;
}

bool Enabled() { return g_enabled; }

SDL_Texture* BeginView(SDL_Renderer* renderer, const Map& map, const Camera& camera) {
    g_current = nullptr;
    if (!g_enabled || renderer != g_renderer || !map.Loaded()) return nullptr;

    // A render state holds one copy of its uniforms, read when its draws go
    // out: the last view's draws have to go before this view's camera goes in.
    SDL_FlushRenderer(renderer);
    SDL_SetGPURenderState(renderer, nullptr);
    g_surface = PLAIN;

    Slot* f = FieldFor(map);
    f->used = ++g_views;
    if (!f->state[S_WATER]) return nullptr;   // no fluid here (or no states)
    g_current = f;

    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    // Seconds, wrapped every hour so the float keeps its precision. The water
    // jumps once an hour, which nobody standing by a river will notice.
    const float seconds = static_cast<float>(std::fmod(SDL_GetTicks() / 1000.0, 3600.0));
    const View view{
        {camera.xpos, camera.ypos, camera.zoom, seconds},
        {kTexelPx, static_cast<float>(f->cols), static_cast<float>(f->rows), 0.0f},
        {static_cast<float>(w), static_cast<float>(h), 0.0f, 0.0f},
    };
    for (SDL_GPURenderState* s : f->state) SDL_SetGPURenderStateFragmentUniforms(s, 0, &view, sizeof view);

    if (w <= 0 || h <= 0 || !LavaNear(*f, camera.VisibleWorldRect(24.0f))) return nullptr;
    if (!g_scene || g_scene_w != w || g_scene_h != h) {
        if (g_scene) SDL_DestroyTexture(g_scene);
        g_scene = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        g_scene_w = w; g_scene_h = h;
        if (!g_scene) {
            SDL_Log("Shaders: no texture for the heat: %s", SDL_GetError());
            g_scene_w = g_scene_h = 0;
            return nullptr;
        }
        SDL_SetTextureScaleMode(g_scene, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(g_scene, SDL_BLENDMODE_NONE);
    }
    return g_scene;
}

void UseSurface(SDL_Renderer* renderer, Surface surface) {
    if (!g_current || surface == g_surface) return;
    SDL_SetGPURenderState(renderer, surface == WATER ? g_current->state[S_WATER]
                                  : surface == LAVA  ? g_current->state[S_LAVA]
                                                     : nullptr);
    g_surface = surface;
}

void DrawHeat(SDL_Renderer* renderer, SDL_Texture* scene) {
    if (!scene) return;
    const bool shimmer = g_current && g_current->state[S_HEAT];
    if (shimmer) SDL_SetGPURenderState(renderer, g_current->state[S_HEAT]);
    SDL_RenderTexture(renderer, scene, nullptr, nullptr);
    SDL_SetGPURenderState(renderer, nullptr);
    g_surface = PLAIN;
    SDL_FlushRenderer(renderer);
}

}  // namespace Shaders
