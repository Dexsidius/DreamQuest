#include "datahash.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace net {

namespace {
constexpr uint64_t FNV_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;

inline void Mix(uint64_t& h, unsigned char c) { h = (h ^ c) * FNV_PRIME; }
}

uint64_t HashFolder(const std::string& dir, const std::string& extension, int* files) {
    std::vector<fs::path> paths;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == extension)
            paths.push_back(it->path());
    // By name, and by the same name everywhere: a directory is listed in
    // whatever order the filesystem likes.
    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().generic_string() < b.filename().generic_string();
    });

    uint64_t h = FNV_BASIS;
    int count = 0;
    std::vector<char> buffer(1 << 16);
    for (const fs::path& p : paths) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        for (unsigned char c : p.filename().generic_string()) Mix(h, c);
        Mix(h, 0);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = in.gcount();
            for (std::streamsize i = 0; i < got; ++i) {
                const unsigned char c = static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
                if (c != '\r') Mix(h, c);
            }
        }
        Mix(h, 0xFF);
        ++count;
    }
    if (files) *files = count;
    return h;
}

DataHashes ComputeDataHashes(const std::string& root) {
    DataHashes out;
    out.data = HashFolder((fs::path(root) / "data").string(), ".json", &out.data_files);
    out.maps = HashFolder((fs::path(root) / "maps").string(), ".mx", &out.map_files);
    return out;
}

std::string ShortHash(uint64_t h) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(h >> 32));
    return buf;
}

} // namespace net
