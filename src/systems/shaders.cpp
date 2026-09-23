#include "shaders.h"
#include "../world/map.h"
#include "../camera.h"

namespace Shaders {
namespace {

constexpr float kTexelPx = 16.0f;         // world pixels per field texel

enum ShaderId { SH_WATER, SH_LAVA, SH_POST, SH_FOG, SH_REFLECT, SH_SPRITE, SH_PROP, SH_FX, SH_COUNT };
// The first five read the field as well as their own texture.
constexpr int kFieldShaders = 5;
constexpr const char* kShaderFiles[SH_COUNT] = {
    "assets/shaders/water.frag.spv",
    "assets/shaders/lava.frag.spv",
    "assets/shaders/post.frag.spv",
    "assets/shaders/fog.frag.spv",
    "assets/shaders/reflect.frag.spv",
    "assets/shaders/sprite.frag.spv",
    "assets/shaders/prop.frag.spv",
    "assets/shaders/fx.frag.spv",
};

// The uniform blocks, exactly as the shaders declare them (std140: every
// member a vec4, so an array of them is packed as it is here).
constexpr int kRipples = 12;
struct ViewBlock {                 // water, lava, reflect: `View`
    float cam[4];                  // camera xpos, ypos, zoom; seconds
    float field[4];                // world px per texel; the field's width and height in texels
    float target[4];               // the size of the view being drawn
    float ripples[kRipples][4];
};
struct PostBlock {
    float cam[4], field[4], target[4], opts[4], grade[4], grade2[4], flash[4];
    float shocks[4][4];
    float heats[8][4];
};
struct FogBlock { float cam[4], field[4], look[4], region[4], more[4]; };
struct PropBlock { float cam[4], mode[4], wind[4]; };
struct SpriteBlock { float flash[4], glow[4], status[4], status2[4], misc[4], frame[4]; };
struct FxBlock { float kind[4], colour[4], size[4], hit[4]; };

// One map's field, and the render states that read it: a render state is
// made with its textures bound, so each field has its own.
struct Slot {
    const Map* map = nullptr;
    string id;
    size_t tile_count = 0;
    float width = 0, height = 0;

    int cols = 0, rows = 0;
    vector<Uint8> kind;                    // Surface, one per texel
    SDL_Texture* texture = nullptr;
    SDL_GPURenderState* state[kFieldShaders] = {};
    Uint64 used = 0;

    bool Holds(const Map& m) const {
        return map == &m && id == m.Id() && tile_count == m.Tiles().size() &&
               width == m.Width() && height == m.Height();
    }
};

// Render states whose uniforms change from one draw to the next. A state
// keeps one copy of its uniforms, read when its draws go out -- so two draws
// that want different ones want two states. Each draw takes the next; when
// they run out, what is queued goes out and they start again.
struct Pool {
    ShaderId shader = SH_SPRITE;
    vector<SDL_GPURenderState*> states;
    size_t next = 0;
};
constexpr size_t kPoolMax = 128;

SDL_Renderer*   g_renderer = nullptr;
SDL_GPUDevice*  g_device = nullptr;
SDL_GPUShader*  g_shader[SH_COUNT] = {};
SDL_GPUSampler* g_sampler = nullptr;
bool            g_enabled = false;
Options         g_options;

// Split screen can have the two seats on two maps; one field each.
Slot     g_fields[2];
Slot*    g_current = nullptr;              // the view being drawn
SDL_GPURenderState* g_now = nullptr;       // what the renderer is set to now
Uint64   g_views = 0;

SDL_GPURenderState* g_prop[PROP_KINDS] = {};   // one per kind; [PROP_NONE] is the glow pass
Pool     g_sprites{SH_SPRITE, {}, 0};
Pool     g_shapes{SH_FX, {}, 0};
SDL_Texture* g_white = nullptr;

SDL_Texture* g_scene = nullptr;
int g_scene_w = 0, g_scene_h = 0;

// The view being drawn.
float g_cam[3] = {0, 0, 1};
float g_view_w = 0, g_view_h = 0;
float g_seconds = 0;
Frame g_frame;

bool IsNumbered(const string& stem, const string& base) {
    if (stem == base) return true;
    if (stem.size() <= base.size() + 1 || stem.compare(0, base.size() + 1, base + "_") != 0)
        return false;
    for (size_t i = base.size() + 1; i < stem.size(); ++i)
        if (stem[i] < '0' || stem[i] > '9') return false;
    return true;
}

bool StartsWith(const string& s, const char* p) { return s.rfind(p, 0) == 0; }

void Set(SDL_Renderer* r, SDL_GPURenderState* s) {
    if (s == g_now) return;
    SDL_SetGPURenderState(r, s);
    g_now = s;
}

void ReleaseSlot(Slot& f) {
    if (g_renderer) SDL_FlushRenderer(g_renderer);
    for (SDL_GPURenderState*& s : f.state) { if (s) SDL_DestroyGPURenderState(s); s = nullptr; }
    if (f.texture) SDL_DestroyTexture(f.texture);
    f = Slot{};
}

void ReleasePool(Pool& p) {
    for (SDL_GPURenderState* s : p.states) if (s) SDL_DestroyGPURenderState(s);
    p.states.clear();
    p.next = 0;
}

SDL_GPURenderState* MakeState(ShaderId id, const SDL_GPUTextureSamplerBinding* field) {
    SDL_GPURenderStateCreateInfo info{};
    info.fragment_shader = g_shader[id];
    info.num_sampler_bindings = field ? 1 : 0;
    info.sampler_bindings = field;
    SDL_GPURenderState* s = SDL_CreateGPURenderState(g_renderer, &info);
    if (!s) SDL_Log("Shaders: render state for %s: %s", kShaderFiles[id], SDL_GetError());
    return s;
}

SDL_GPURenderState* Take(Pool& p, SDL_Renderer* r) {
    if (p.next >= p.states.size()) {
        if (p.states.size() < kPoolMax) {
            if (SDL_GPURenderState* s = MakeState(p.shader, nullptr)) p.states.push_back(s);
        }
        if (p.next >= p.states.size()) {
            if (p.states.empty()) return nullptr;
            SDL_FlushRenderer(r);
            p.next = 0;
        }
    }
    return p.states[p.next++];
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

// Every map gets a field, fluid or none: the fog, the reflections and the
// post pass are made with one bound, and on a map without water it is simply
// empty.
bool Build(Slot& f, const Map& map) {
    f.map = &map;
    f.id = map.Id();
    f.tile_count = map.Tiles().size();
    f.width = map.Width();
    f.height = map.Height();
    const FieldData data = FieldOf(map);
    f.cols = data.cols;
    f.rows = data.rows;
    f.kind = data.kind;

    const size_t n = data.kind.size();
    vector<Uint8> texels(n * 4);
    auto encode = [](float v) {
        return static_cast<Uint8>(std::clamp(std::lround(v * 127.5f + 127.5f), 0L, 255L));
    };
    for (size_t i = 0; i < n; ++i) {
        texels[i * 4 + 0] = data.kind[i] == LAVA ? 255 : 0;
        texels[i * 4 + 1] = data.kind[i] == WATER ? 255 : 0;
        texels[i * 4 + 2] = encode(data.fx[i]);
        texels[i * 4 + 3] = encode(data.fy[i]);
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
    for (int i = 0; i < kFieldShaders; ++i) {
        f.state[i] = MakeState(static_cast<ShaderId>(i), &binding);
        if (!f.state[i]) return false;
    }
    return true;
}

Slot* FieldFor(const Map& map) {
    for (Slot& f : g_fields)
        if (f.Holds(map)) return &f;
    Slot& slot = g_fields[0].used <= g_fields[1].used ? g_fields[0] : g_fields[1];
    ReleaseSlot(slot);
    if (!Build(slot, map)) {
        // Remember the map anyway, without its states, so a failure is not
        // retried every frame.
        for (SDL_GPURenderState*& s : slot.state) { if (s) SDL_DestroyGPURenderState(s); s = nullptr; }
    }
    return &slot;
}

void Put(float* dst, float a, float b, float c, float d) { dst[0] = a; dst[1] = b; dst[2] = c; dst[3] = d; }

// Every uniform that stays the same for the whole of a view, for the view
// being drawn: set before anything in it is drawn, and never while it is.
void Upload() {
    if (!g_current) return;
    const Frame& f = g_frame;
    const float cols = static_cast<float>(g_current->cols), rows = static_cast<float>(g_current->rows);

    ViewBlock view{};
    Put(view.cam, g_cam[0], g_cam[1], g_cam[2], g_seconds);
    Put(view.field, kTexelPx, cols, rows, 1.0f);
    Put(view.target, g_view_w, g_view_h, 0.0f, 0.0f);
    for (int i = 0; i < kRipples && i < static_cast<int>(f.ripples.size()); ++i)
        Put(view.ripples[i], f.ripples[i].x, f.ripples[i].y, f.ripples[i].age, f.ripples[i].strength);
    for (int i : {SH_WATER, SH_LAVA, SH_REFLECT})
        if (g_current->state[i]) SDL_SetGPURenderStateFragmentUniforms(g_current->state[i], 0, &view, sizeof view);

    if (g_current->state[SH_POST]) {
        PostBlock post{};
        Put(post.cam, g_cam[0], g_cam[1], g_cam[2], g_seconds);
        Put(post.field, kTexelPx, cols, rows, 1.0f);
        Put(post.target, g_view_w, g_view_h, 0.0f, 0.0f);
        Put(post.opts, g_options.distortion ? 1.0f : 0.0f, g_options.fringing ? 1.0f : 0.0f, f.dream,
            g_options.distortion ? 1.0f : 0.0f);
        Put(post.grade, f.grade[0], f.grade[1], f.grade[2], f.grade[3]);
        Put(post.grade2, f.contrast, f.lift, 0.0f, 0.0f);
        if (g_options.flashes) Put(post.flash, f.flash[0], f.flash[1], f.flash[2], f.flash[3]);
        for (int i = 0; i < 4 && i < static_cast<int>(f.shocks.size()); ++i)
            Put(post.shocks[i], f.shocks[i].x, f.shocks[i].y, f.shocks[i].radius, f.shocks[i].strength);
        for (int i = 0; i < 8 && i < static_cast<int>(f.heats.size()); ++i)
            Put(post.heats[i], f.heats[i].x, f.heats[i].y, f.heats[i].radius, f.heats[i].strength);
        SDL_SetGPURenderStateFragmentUniforms(g_current->state[SH_POST], 0, &post, sizeof post);
    }

    if (g_current->state[SH_FOG]) {
        FogBlock fog{};
        Put(fog.cam, g_cam[0], g_cam[1], g_cam[2], g_seconds);
        Put(fog.field, kTexelPx, cols, rows, 1.0f);
        Put(fog.look, f.fog.r, f.fog.g, f.fog.b, f.fog.density);
        Put(fog.region, f.fog.region.x, f.fog.region.y, f.fog.region.x + f.fog.region.w, f.fog.region.y + f.fog.region.h);
        Put(fog.more, f.fog.by_water, f.fog.drift, 0.0f, 0.0f);
        SDL_SetGPURenderStateFragmentUniforms(g_current->state[SH_FOG], 0, &fog, sizeof fog);
    }

    // The wind comes from the west, a little north of it, and its gusts roll
    // across the ground at about a tree's height a second.
    for (int k = 0; k < PROP_KINDS; ++k) {
        if (!g_prop[k]) continue;
        float strength = 1.0f, mode = 0.0f;
        switch (k) {
            case PROP_NONE:     mode = 6; strength = 0.85f; break;   // the glow pass
            case PROP_GRASS:    mode = 1; strength = 1.0f; break;
            case PROP_TREE:     mode = 1; strength = 0.5f; break;
            case PROP_CLOTH:    mode = 2; strength = 1.0f; break;
            case PROP_STAKED:   mode = 2; strength = -0.8f; break;   // held at both ends
            case PROP_WINDOWS:  mode = 3; break;
            case PROP_FOUNTAIN: mode = 4; break;
            case PROP_PULSE:    mode = 5; break;
            default: break;
        }
        PropBlock prop{};
        Put(prop.cam, g_cam[0], g_cam[1], g_cam[2], g_seconds);
        Put(prop.mode, mode, strength, f.night, f.wind);
        Put(prop.wind, 1.0f, 0.35f, 1.6f, 260.0f);
        SDL_SetGPURenderStateFragmentUniforms(g_prop[k], 0, &prop, sizeof prop);
    }
}

}  // namespace

// --- what the art is -----------------------------------------------------------------------

const Art& ArtOf(const string& path) {
    static unordered_map<string, Art> known;
    auto it = known.find(path);
    if (it != known.end()) return it->second;

    const string stem = std::filesystem::path(path).stem().string();
    const auto any = [&](std::initializer_list<const char*> names) {
        for (const char* n : names) if (stem == n) return true;
        return false;
    };
    const auto prefix = [&](std::initializer_list<const char*> names) {
        for (const char* n : names) if (StartsWith(stem, n)) return true;
        return false;
    };

    Art a;
    if (prefix({"tuft_", "dry_tuft_", "sedge_", "flowers_", "herb_"}) || any({"reeds", "guild_plant"}))
        a.kind = PROP_GRASS;
    else if (prefix({"tree_", "treesmall_", "bush_", "bushsmall_"}) || any({"snow_pine", "swamp_tree", "charred_tree"}))
        a.kind = PROP_TREE;
    else if (prefix({"tapestry_"}) || any({"banner", "palace_banner", "guild_banner"}))
        a.kind = PROP_CLOTH;
    else if (any({"tent", "college_banner"}))
        a.kind = PROP_STAKED;
    else if (prefix({"building_"}) ||
             any({"clothier_shop", "inn_building", "herbalist_cottage", "mossvale_lodge", "mage_college",
                  "college_hall", "college_wing", "palace_keep", "palace_tower"}))
        a.kind = PROP_WINDOWS;
    else if (any({"college_fountain", "well", "spring_basin", "quench_trough"}))
        a.kind = PROP_FOUNTAIN;
    else if (any({"waystone_lit", "crystal_pylon", "ice_crystal"}))
        a.kind = PROP_PULSE;

    // Fires: the air over them wavers, and they glow and throw a halo after dark.
    a.hot = any({"campfire", "campfire_ring", "hearth", "cottage_hearth", "inn_fireplace", "forge",
                 "palace_brazier", "palace_hearth", "palace_torch", "hellgate"});
    // Lights that are not fires, and fires, have a halo.
    a.halo = a.hot || any({"candlestand", "palace_chandelier", "college_lamp", "waystone_lit"});
    // What is lit from inside and should shine through the dark: every lit
    // window, every fire and light, and a few things that glow of themselves.
    a.glows = a.kind == PROP_WINDOWS || a.halo || a.kind == PROP_PULSE ||
              any({"herb_glowcap", "herb_moonpetal", "herb_emberbloom", "spell_circle", "demon_throne",
                   "throne_door", "ice_spire", "enchanting_table", "totem_cinder_king"});
    return known.emplace(path, a).first->second;
}

Surface SurfaceOfTile(const string& path) {
    const string stem = std::filesystem::path(path).stem().string();
    if (IsNumbered(stem, "water") || IsNumbered(stem, "bog_water")) return WATER;
    if (IsNumbered(stem, "lava")) return LAVA;
    return PLAIN;
}

void SetOptions(const Options& o) { g_options = o; }
const Options& GetOptions() { return g_options; }

vector<string> ShaderFiles() { return vector<string>(std::begin(kShaderFiles), std::end(kShaderFiles)); }

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

// --- set up and taken down -----------------------------------------------------------------

SDL_Renderer* CreateRenderer(SDL_Window* window) {
    // Asked for something in particular: let SDL do exactly that.
    if (SDL_GetHint(SDL_HINT_RENDER_DRIVER)) return SDL_CreateRenderer(window, nullptr);

    // SDL_GPU on Direct3D 12 would want the shaders as DXIL; on Vulkan it
    // takes the SPIR-V glslc makes, and it is Vulkan the Steam Deck has.
    const bool chose_gpu_driver = SDL_GetHint(SDL_HINT_GPU_DRIVER) != nullptr;
    if (!chose_gpu_driver) SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
    if (SDL_Renderer* r = SDL_CreateRenderer(window, "gpu")) return r;

    SDL_Log("Shaders: no GPU renderer (%s); the game draws plain", SDL_GetError());
    if (!chose_gpu_driver) SDL_ResetHint(SDL_HINT_GPU_DRIVER);
    return SDL_CreateRenderer(window, nullptr);
}

bool Init(SDL_Renderer* renderer) {
    Shutdown();
    if (!renderer) return false;
    const char* name = SDL_GetRendererName(renderer);
    if (!name || string(name) != "gpu") {
        SDL_Log("Shaders: renderer is %s, not gpu; no effects", name ? name : "unknown");
        return false;
    }
    g_device = static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(
        SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
    if (!g_device || !(SDL_GetGPUShaderFormats(g_device) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        SDL_Log("Shaders: the GPU device takes no SPIR-V; no effects");
        g_device = nullptr;
        return false;
    }
    g_renderer = renderer;

    for (int i = 0; i < SH_COUNT; ++i) {
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
        info.num_samplers = i < kFieldShaders ? 2 : 1;   // what is drawn, and the field
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
    // the fading of the heat and the fog.
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

    for (int k = 0; k < PROP_KINDS; ++k) {
        g_prop[k] = MakeState(SH_PROP, nullptr);
        if (!g_prop[k]) { Shutdown(); return false; }
    }

    // What a shape of light and the fog are drawn over: a plain white square.
    g_white = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 4, 4);
    if (g_white) {
        const vector<Uint8> white(4 * 4 * 4, 255);
        SDL_UpdateTexture(g_white, nullptr, white.data(), 16);
        SDL_SetTextureScaleMode(g_white, SDL_SCALEMODE_NEAREST);
    }

    g_enabled = true;
    SDL_Log("Shaders: %d effects on %s", static_cast<int>(SH_COUNT), SDL_GetGPUDeviceDriver(g_device));
    return true;
}

void Shutdown() {
    if (g_renderer) {
        SDL_SetGPURenderState(g_renderer, nullptr);
        SDL_FlushRenderer(g_renderer);
    }
    g_now = nullptr;
    for (Slot& f : g_fields) ReleaseSlot(f);
    ReleasePool(g_sprites);
    ReleasePool(g_shapes);
    for (SDL_GPURenderState*& s : g_prop) { if (s) SDL_DestroyGPURenderState(s); s = nullptr; }
    if (g_white) SDL_DestroyTexture(g_white);
    g_white = nullptr;
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
    g_enabled = false;
}

bool Enabled() { return g_enabled; }
bool Effects() { return g_enabled && g_options.effects; }

// --- a view --------------------------------------------------------------------------------

SDL_Texture* BeginView(SDL_Renderer* renderer, const Map& map, const Camera& camera) {
    g_current = nullptr;
    if (!Effects() || renderer != g_renderer || !map.Loaded()) return nullptr;

    // A render state holds one copy of its uniforms, read when its draws go
    // out: the last view's draws have to go before this view's camera goes in.
    SDL_FlushRenderer(renderer);
    SDL_SetGPURenderState(renderer, nullptr);
    g_now = nullptr;
    g_sprites.next = g_shapes.next = 0;

    Slot* f = FieldFor(map);
    f->used = ++g_views;
    if (!f->state[SH_WATER]) return nullptr;   // no states: plain
    g_current = f;

    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    // Seconds, wrapped every hour so the float keeps its precision. The water
    // jumps once an hour, which nobody standing by a river will notice.
    g_seconds = static_cast<float>(std::fmod(SDL_GetTicks() / 1000.0, 3600.0));
    g_cam[0] = camera.xpos; g_cam[1] = camera.ypos; g_cam[2] = camera.zoom;
    g_view_w = static_cast<float>(w); g_view_h = static_cast<float>(h);
    g_frame = Frame{};
    Upload();

    if (w <= 0 || h <= 0) return nullptr;
    if (!g_scene || g_scene_w != w || g_scene_h != h) {
        if (g_scene) SDL_DestroyTexture(g_scene);
        g_scene = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        g_scene_w = w; g_scene_h = h;
        if (!g_scene) {
            SDL_Log("Shaders: no texture for the view: %s", SDL_GetError());
            g_scene_w = g_scene_h = 0;
            return nullptr;
        }
        SDL_SetTextureScaleMode(g_scene, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(g_scene, SDL_BLENDMODE_NONE);
    }
    return g_scene;
}

void SetFrame(const Frame& frame) {
    if (!g_current) return;
    g_frame = frame;
    Upload();
}

void UseTile(SDL_Renderer* renderer, Surface surface, PropKind kind) {
    if (!g_current) return;
    SDL_GPURenderState* s = surface == WATER ? g_current->state[SH_WATER]
                          : surface == LAVA  ? g_current->state[SH_LAVA]
                          : kind != PROP_NONE && kind < PROP_KINDS ? g_prop[kind]
                                                                   : nullptr;
    Set(renderer, s);
}

void UsePlain(SDL_Renderer* renderer) {
    if (g_current) Set(renderer, nullptr);
}

bool UseGlow(SDL_Renderer* renderer) {
    if (!g_current || !g_prop[PROP_NONE]) return false;
    Set(renderer, g_prop[PROP_NONE]);
    return true;
}

bool UseReflection(SDL_Renderer* renderer) {
    if (!g_current || !g_current->state[SH_REFLECT]) return false;
    Set(renderer, g_current->state[SH_REFLECT]);
    return true;
}

bool UseSprite(SDL_Renderer* renderer, const SpriteFx& fx, const SDL_FRect& src, float tw, float th) {
    if (!g_current || tw <= 0 || th <= 0 || !fx.Any()) return false;
    SDL_GPURenderState* s = Take(g_sprites, renderer);
    if (!s) return false;
    SpriteBlock b{};
    const float flash = g_options.flashes ? fx.flash.a : 0.0f;
    Put(b.flash, fx.flash.r, fx.flash.g, fx.flash.b, flash);
    Put(b.glow, fx.glow.r, fx.glow.g, fx.glow.b, fx.glow.a);
    Put(b.status, fx.burn, fx.cold, fx.electrified, fx.poison);
    Put(b.status2, fx.wet, fx.bleed, fx.dissolve, static_cast<float>(fx.dissolve_kind));
    Put(b.misc, g_seconds, fx.seed, fx.rim ? 1.0f : 0.0f, 0.0f);
    Put(b.frame, src.x / tw, src.y / th, src.w / tw, src.h / th);
    SDL_SetGPURenderStateFragmentUniforms(s, 0, &b, sizeof b);
    Set(renderer, s);
    return true;
}

void EndSprite(SDL_Renderer* renderer) { UsePlain(renderer); }

bool DrawShape(SDL_Renderer* renderer, const SDL_FRect& dst, const ShapeFx& fx, SDL_BlendMode blend) {
    if (!g_current || !g_white || dst.w <= 0 || dst.h <= 0) return false;
    SDL_GPURenderState* s = Take(g_shapes, renderer);
    if (!s) return false;
    FxBlock b{};
    Put(b.kind, static_cast<float>(fx.shape), fx.fade, g_seconds, fx.seed);
    Put(b.colour, fx.colour.r, fx.colour.g, fx.colour.b, fx.colour.a);
    Put(b.size, dst.w, dst.h, g_cam[2], fx.foot);
    Put(b.hit, fx.hit_x, fx.hit_y, fx.hit_age, 0.0f);
    SDL_SetGPURenderStateFragmentUniforms(s, 0, &b, sizeof b);
    Set(renderer, s);
    SDL_SetTextureBlendMode(g_white, blend);
    SDL_RenderTexture(renderer, g_white, nullptr, &dst);
    SDL_SetTextureBlendMode(g_white, SDL_BLENDMODE_BLEND);
    Set(renderer, nullptr);
    return true;
}

void DrawFog(SDL_Renderer* renderer) {
    if (!g_current || !g_frame.fog.on || !g_white || !g_current->state[SH_FOG]) return;
    Set(renderer, g_current->state[SH_FOG]);
    const SDL_FRect all = {0.0f, 0.0f, g_view_w, g_view_h};
    SDL_SetTextureBlendMode(g_white, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(renderer, g_white, nullptr, &all);
    Set(renderer, nullptr);
}

void DrawPost(SDL_Renderer* renderer, SDL_Texture* scene) {
    if (!scene) return;
    const bool shaded = g_current && g_current->state[SH_POST];
    SDL_SetGPURenderState(renderer, shaded ? g_current->state[SH_POST] : nullptr);
    SDL_RenderTexture(renderer, scene, nullptr, nullptr);
    SDL_SetGPURenderState(renderer, nullptr);
    g_now = nullptr;
    SDL_FlushRenderer(renderer);
    g_current = nullptr;
}

Surface FluidAt(float x, float y) {
    if (!g_current || g_current->kind.empty()) return PLAIN;
    const int cx = static_cast<int>(std::floor(x / kTexelPx)), cy = static_cast<int>(std::floor(y / kTexelPx));
    if (cx < 0 || cy < 0 || cx >= g_current->cols || cy >= g_current->rows) return PLAIN;
    return static_cast<Surface>(g_current->kind[static_cast<size_t>(cy) * g_current->cols + cx]);
}

}  // namespace Shaders
