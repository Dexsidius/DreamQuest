#include "game.h"
#include <filesystem>

namespace fs = std::filesystem;

// The game loads data/ and assets/ by relative path, which only works when it
// is started from the project root. Double-clicked from Explorer the working
// directory is bin\, every file fails to open, and the window closes before
// the message explaining why can be read.
//
// So rather than depending on how it was launched, find the data and move to
// it: the exe's own directory first, then its parent (a build in bin/), then
// wherever we happen to have started. SDL_GetBasePath is used because argv[0]
// is not reliably a full path on Windows.
static bool FindGameRoot() {
    vector<fs::path> candidates;

    if (const char* base = SDL_GetBasePath()) {
        // SDL hands back a trailing separator, which makes the last component
        // an empty filename -- parent_path() on "bin\" is "bin", not the
        // directory above it. Drop it before walking up.
        fs::path exe_dir(base);
        if (!exe_dir.has_filename()) exe_dir = exe_dir.parent_path();
        candidates.push_back(exe_dir);
        candidates.push_back(exe_dir.parent_path());
    }
    candidates.push_back(fs::current_path());

    std::error_code ec;
    for (const fs::path& dir : candidates) {
        if (dir.empty()) continue;
        // data/sprites.json rather than data/ alone: an empty directory left
        // behind by a half-finished import should not count as the root.
        if (!fs::exists(dir / "data" / "sprites.json", ec)) continue;
        fs::current_path(dir, ec);
        if (!ec) return true;
    }
    return false;
}

int main(int argc, char* args[]) {
    if (!FindGameRoot()) {
        // Nothing has been initialised yet, so this is the only way to say so
        // to someone who started the game by clicking on it.
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "DreamQuest",
            "Could not find the game's data files.\n\n"
            "DreamQuest looks for data/ and assets/ beside the executable or "
            "one directory above it. If this is a fresh checkout, run "
            "tools/import_assets.ps1 first to build assets/ from the CraftPix "
            "packs.", nullptr);
        return -1;
    }

    Game game;

    if (!game.Start(argc, args)) return -1;
    game.Loop();
    return 0;
}
