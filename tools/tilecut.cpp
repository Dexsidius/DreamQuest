// -----------------------------------------------------------------------------
//  tilecut - cuts tile atlases into the individual tile images DreamQuest and
//  LevelEdit-Plus both expect.
//
//  The CraftPix tilesets ship as packed autotile sheets. The level editor works
//  with one image file per tile type, so the atlases have to be split before
//  either program can use them. Rather than hand-picking cell coordinates,
//  tilecut can find the cells worth keeping: fully opaque cells whose pixels
//  are close to a single colour are the flat ground fills, and everything with
//  transparency is an edge or a decoration.
//
//  Build:  see tools/build_tools.ps1 (or compile.sh, which builds it too)
//
//  Usage:
//    tilecut <atlas.png> --report [--cell 16]
//        Print a table of every cell: opacity, colour spread, average colour.
//
//    tilecut <atlas.png> --flat <count> --out <dir> --prefix <name> [--cell 16]
//        Export up to <count> visually distinct flat fill tiles.
//
//    tilecut <atlas.png> --cells r,c[,name] ... --out <dir> --prefix <name>
//        Export specific cells by row and column.
//
//    tilecut <atlas.png> --all --out <dir> --prefix <name> [--min-alpha 0.15]
//        Export every cell that is not effectively empty.
//
//    tilecut <sheet.png> --sprites --out <dir> --prefix <name> [--gap 3] [--min-size 8]
//        Find each separate drawing on the sheet by tracing connected opaque
//        pixels, and export it cropped to its own file. This is what turns the
//        packed object sheets (buildings, trees, rocks, props) into the one
//        file per object that LevelEdit-Plus needs.
//
//    tilecut <sheet.png> --region x,y,w,h,name --out <dir>
//        Export one explicit pixel rectangle.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

struct CellStats {
    int   row = 0, col = 0;
    float opaque_fraction = 0.0f;   // 0..1 of pixels with alpha > 200
    float spread = 0.0f;            // mean distance from the average colour
    Uint8 r = 0, g = 0, b = 0;
};

static SDL_Surface* g_atlas = nullptr;

static void GetPixel(const SDL_Surface* s, int x, int y,
                     Uint8& r, Uint8& g, Uint8& b, Uint8& a) {
    const Uint8* pixels = static_cast<const Uint8*>(s->pixels);
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(s->format);
    const Uint8* p = pixels + y * s->pitch + x * fmt->bytes_per_pixel;

    Uint32 value = 0;
    std::memcpy(&value, p, fmt->bytes_per_pixel);
    SDL_GetRGBA(value, fmt, nullptr, &r, &g, &b, &a);
}

static CellStats Analyse(const SDL_Surface* s, int row, int col, int cell) {
    CellStats stats;
    stats.row = row;
    stats.col = col;

    const int x0 = col * cell, y0 = row * cell;
    long long sum_r = 0, sum_g = 0, sum_b = 0;
    int opaque = 0, total = 0;

    for (int y = 0; y < cell; ++y)
        for (int x = 0; x < cell; ++x) {
            Uint8 r, g, b, a;
            GetPixel(s, x0 + x, y0 + y, r, g, b, a);
            ++total;
            if (a > 200) {
                ++opaque;
                sum_r += r; sum_g += g; sum_b += b;
            }
        }

    if (opaque == 0 || total == 0) return stats;

    stats.opaque_fraction = static_cast<float>(opaque) / total;
    stats.r = static_cast<Uint8>(sum_r / opaque);
    stats.g = static_cast<Uint8>(sum_g / opaque);
    stats.b = static_cast<Uint8>(sum_b / opaque);

    // Mean distance from the average tells flat fills from textured cells.
    double spread = 0.0;
    for (int y = 0; y < cell; ++y)
        for (int x = 0; x < cell; ++x) {
            Uint8 r, g, b, a;
            GetPixel(s, x0 + x, y0 + y, r, g, b, a);
            if (a <= 200) continue;
            const double dr = r - stats.r, dg = g - stats.g, db = b - stats.b;
            spread += std::sqrt(dr * dr + dg * dg + db * db);
        }
    stats.spread = static_cast<float>(spread / opaque);
    return stats;
}

static bool ExportCell(SDL_Surface* atlas, int row, int col, int cell,
                       const std::string& path) {
    SDL_Surface* out = SDL_CreateSurface(cell, cell, SDL_PIXELFORMAT_RGBA32);
    if (!out) {
        std::fprintf(stderr, "tilecut: could not create surface: %s\n", SDL_GetError());
        return false;
    }

    SDL_Rect src = {col * cell, row * cell, cell, cell};
    SDL_Rect dst = {0, 0, cell, cell};
    // Straight copy: the cell must land in the file exactly as authored.
    SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(atlas, &src, out, &dst);

    const bool ok = IMG_SavePNG(out, path.c_str());
    if (!ok) std::fprintf(stderr, "tilecut: could not write '%s': %s\n",
                          path.c_str(), SDL_GetError());
    SDL_DestroySurface(out);
    return ok;
}

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // inclusive
    int W() const { return x1 - x0 + 1; }
    int H() const { return y1 - y0 + 1; }
};

// Two boxes belong to the same drawing when they touch or nearly touch, which
// reunites a sprite with its detached shadow or highlight.
static bool BoxesNear(const Box& a, const Box& b, int gap) {
    return !(a.x1 + gap < b.x0 || b.x1 + gap < a.x0 ||
             a.y1 + gap < b.y0 || b.y1 + gap < a.y0);
}

static void MergeBox(Box& into, const Box& other) {
    into.x0 = std::min(into.x0, other.x0);
    into.y0 = std::min(into.y0, other.y0);
    into.x1 = std::max(into.x1, other.x1);
    into.y1 = std::max(into.y1, other.y1);
}

// Flood fills every run of connected opaque pixels and returns one box each.
static std::vector<Box> FindSprites(SDL_Surface* s, int alpha_threshold,
                                    int gap, int min_size) {
    const int w = s->w, h = s->h;
    std::vector<unsigned char> solid(static_cast<size_t>(w) * h, 0);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Uint8 r, g, b, a;
            GetPixel(s, x, y, r, g, b, a);
            solid[static_cast<size_t>(y) * w + x] = (a > alpha_threshold) ? 1 : 0;
        }

    std::vector<unsigned char> seen(static_cast<size_t>(w) * h, 0);
    std::vector<Box> boxes;
    std::vector<int> stack;

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t start = static_cast<size_t>(y) * w + x;
            if (!solid[start] || seen[start]) continue;

            Box box{x, y, x, y};
            stack.clear();
            stack.push_back(static_cast<int>(start));
            seen[start] = 1;

            while (!stack.empty()) {
                const int index = stack.back();
                stack.pop_back();
                const int cx = index % w, cy = index / w;

                box.x0 = std::min(box.x0, cx);
                box.y0 = std::min(box.y0, cy);
                box.x1 = std::max(box.x1, cx);
                box.y1 = std::max(box.y1, cy);

                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t n = static_cast<size_t>(ny) * w + nx;
                        if (solid[n] && !seen[n]) {
                            seen[n] = 1;
                            stack.push_back(static_cast<int>(n));
                        }
                    }
            }
            boxes.push_back(box);
        }

    // Merge neighbours repeatedly until nothing else joins up.
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size();) {
                if (BoxesNear(boxes[i], boxes[j], gap)) {
                    MergeBox(boxes[i], boxes[j]);
                    boxes.erase(boxes.begin() + j);
                    merged = true;
                } else {
                    ++j;
                }
            }
        }
    }

    // Drop specks, then order top-to-bottom and left-to-right so the numbering
    // stays stable between runs.
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(),
                               [&](const Box& b) { return b.W() < min_size || b.H() < min_size; }),
                boxes.end());
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
        if (std::abs(a.y0 - b.y0) > 16) return a.y0 < b.y0;
        return a.x0 < b.x0;
    });
    return boxes;
}

static bool ExportRegion(SDL_Surface* atlas, const SDL_Rect& src,
                         const std::string& path) {
    SDL_Surface* out = SDL_CreateSurface(src.w, src.h, SDL_PIXELFORMAT_RGBA32);
    if (!out) return false;
    SDL_Rect dst = {0, 0, src.w, src.h};
    SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
    SDL_Rect from = src;
    SDL_BlitSurface(atlas, &from, out, &dst);
    const bool ok = IMG_SavePNG(out, path.c_str());
    if (!ok) std::fprintf(stderr, "tilecut: could not write %s: %s\n",
                          path.c_str(), SDL_GetError());
    SDL_DestroySurface(out);
    return ok;
}

static int ColourDistance(const CellStats& a, const CellStats& b) {
    const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return static_cast<int>(std::sqrt(dr * dr + dg * dg + db * db));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: tilecut <atlas.png> [--report | --flat N | --all | "
                    "--cells r,c ...] [--cell 16] [--out dir] [--prefix name]\n");
        return 1;
    }

    const std::string atlas_path = argv[1];
    int cell = 16;
    std::string out_dir = ".";
    std::string prefix = fs::path(atlas_path).stem().string();
    bool report = false, flat_mode = false, all_mode = false, sprite_mode = false;
    int flat_count = 8;
    int gap = 3, min_size = 8;
    float min_alpha = 0.15f;
    std::vector<std::tuple<int, int, int, int, std::string>> regions;
    // Explicit cells, as row/col/optional-name triples.
    std::vector<std::tuple<int, int, std::string>> wanted;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--cell" && i + 1 < argc)        cell = std::atoi(argv[++i]);
        else if (arg == "--out" && i + 1 < argc)    out_dir = argv[++i];
        else if (arg == "--prefix" && i + 1 < argc) prefix = argv[++i];
        else if (arg == "--report")                 report = true;
        else if (arg == "--all")                    all_mode = true;
        else if (arg == "--min-alpha" && i + 1 < argc) min_alpha = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--flat" && i + 1 < argc)   { flat_mode = true; flat_count = std::atoi(argv[++i]); }
        else if (arg == "--sprites")                sprite_mode = true;
        else if (arg == "--gap" && i + 1 < argc)     gap = std::atoi(argv[++i]);
        else if (arg == "--min-size" && i + 1 < argc) min_size = std::atoi(argv[++i]);
        else if (arg == "--region") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                const std::string spec = argv[++i];
                int rx = 0, ry = 0, rw = 0, rh = 0;
                char label[128] = {0};
                if (SDL_sscanf(spec.c_str(), "%d,%d,%d,%d,%127s",
                               &rx, &ry, &rw, &rh, label) >= 4)
                    regions.emplace_back(rx, ry, rw, rh, label);
            }
        }
        else if (arg == "--cells") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                const std::string spec = argv[++i];
                const size_t c1 = spec.find(',');
                if (c1 == std::string::npos) continue;
                const size_t c2 = spec.find(',', c1 + 1);
                const int r = std::atoi(spec.substr(0, c1).c_str());
                const int c = std::atoi(spec.substr(c1 + 1, c2 - c1 - 1).c_str());
                const std::string name = (c2 == std::string::npos) ? "" : spec.substr(c2 + 1);
                wanted.emplace_back(r, c, name);
            }
        }
    }

    if (!SDL_Init(0)) {
        std::fprintf(stderr, "tilecut: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    g_atlas = IMG_Load(atlas_path.c_str());
    if (!g_atlas) {
        std::fprintf(stderr, "tilecut: cannot load '%s': %s\n",
                     atlas_path.c_str(), SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Work in a known layout so pixel reads are simple.
    SDL_Surface* converted = SDL_ConvertSurface(g_atlas, SDL_PIXELFORMAT_RGBA32);
    if (converted) {
        SDL_DestroySurface(g_atlas);
        g_atlas = converted;
    }

    const int cols = g_atlas->w / cell;
    const int rows = g_atlas->h / cell;

    std::error_code ec;
    if (!report) fs::create_directories(out_dir, ec);

    if (report) {
        std::printf("# %s  %dx%d px  ->  %d cols x %d rows of %dpx\n",
                    atlas_path.c_str(), g_atlas->w, g_atlas->h, cols, rows, cell);
        std::printf("# row col  opaque  spread   rgb\n");
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const CellStats s = Analyse(g_atlas, r, c, cell);
                if (s.opaque_fraction < min_alpha) continue;
                std::printf("%4d %3d  %5.2f  %6.1f   %3d,%3d,%3d\n",
                            r, c, s.opaque_fraction, s.spread, s.r, s.g, s.b);
            }
        SDL_DestroySurface(g_atlas);
        SDL_Quit();
        return 0;
    }

    int written = 0;

    if (flat_mode) {
        // Gather every solid, low-variance cell, then keep the most distinct
        // colours so the export is a palette rather than fifty near-identical
        // greens.
        std::vector<CellStats> candidates;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const CellStats s = Analyse(g_atlas, r, c, cell);
                if (s.opaque_fraction > 0.999f && s.spread < 26.0f)
                    candidates.push_back(s);
            }
        std::sort(candidates.begin(), candidates.end(),
                  [](const CellStats& a, const CellStats& b) { return a.spread < b.spread; });

        std::vector<CellStats> chosen;
        for (const CellStats& s : candidates) {
            if (static_cast<int>(chosen.size()) >= flat_count) break;
            bool too_close = false;
            for (const CellStats& k : chosen)
                if (ColourDistance(s, k) < 26) { too_close = true; break; }
            if (!too_close) chosen.push_back(s);
        }

        for (size_t i = 0; i < chosen.size(); ++i) {
            char name[256];
            SDL_snprintf(name, sizeof(name), "%s/%s_%c.png",
                         out_dir.c_str(), prefix.c_str(), static_cast<char>('a' + i));
            if (ExportCell(g_atlas, chosen[i].row, chosen[i].col, cell, name)) {
                ++written;
                std::printf("%s  <- r%d c%d  rgb(%d,%d,%d)\n", name,
                            chosen[i].row, chosen[i].col,
                            chosen[i].r, chosen[i].g, chosen[i].b);
            }
        }
    }

    if (all_mode) {
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const CellStats s = Analyse(g_atlas, r, c, cell);
                if (s.opaque_fraction < min_alpha) continue;
                char name[256];
                SDL_snprintf(name, sizeof(name), "%s/%s_r%02dc%02d.png",
                             out_dir.c_str(), prefix.c_str(), r, c);
                if (ExportCell(g_atlas, r, c, cell, name)) ++written;
            }
    }

    if (sprite_mode) {
        const std::vector<Box> boxes = FindSprites(g_atlas, 24, gap, min_size);
        int index = 0;
        for (const Box& b : boxes) {
            char name[256];
            SDL_snprintf(name, sizeof(name), "%s/%s_%02d.png",
                         out_dir.c_str(), prefix.c_str(), index);
            const SDL_Rect src = {b.x0, b.y0, b.W(), b.H()};
            if (ExportRegion(g_atlas, src, name)) {
                ++written;
                std::printf("%s  %dx%d at %d,%d\n", name, b.W(), b.H(), b.x0, b.y0);
            }
            ++index;
        }
    }

    for (const auto& reg : regions) {
        const SDL_Rect src = {std::get<0>(reg), std::get<1>(reg),
                              std::get<2>(reg), std::get<3>(reg)};
        const std::string label = std::get<4>(reg);
        char name[256];
        SDL_snprintf(name, sizeof(name), "%s/%s.png", out_dir.c_str(),
                     label.empty() ? prefix.c_str() : label.c_str());
        if (ExportRegion(g_atlas, src, name)) {
            ++written;
            std::printf("%s  %dx%d at %d,%d\n", name, src.w, src.h, src.x, src.y);
        }
    }

    for (const auto& w : wanted) {
        const int r = std::get<0>(w), c = std::get<1>(w);
        const std::string& label = std::get<2>(w);
        if (r < 0 || c < 0 || r >= rows || c >= cols) {
            std::fprintf(stderr, "tilecut: cell %d,%d is outside the atlas\n", r, c);
            continue;
        }
        char name[256];
        if (label.empty())
            SDL_snprintf(name, sizeof(name), "%s/%s_r%02dc%02d.png",
                         out_dir.c_str(), prefix.c_str(), r, c);
        else
            SDL_snprintf(name, sizeof(name), "%s/%s.png", out_dir.c_str(), label.c_str());
        if (ExportCell(g_atlas, r, c, cell, name)) ++written;
    }

    std::printf("tilecut: wrote %d tiles to %s\n", written, out_dir.c_str());

    SDL_DestroySurface(g_atlas);
    SDL_Quit();
    return 0;
}
