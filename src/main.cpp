#include "game.h"
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
// The game is a windowed program (build.ps1 links it -mwindows), so a
// double-click opens no console behind it. Started from a terminal, its log
// and the command-line modes (--audit, --shot) should still print there: when
// nobody handed it an output of its own -- a pipe or a file -- it borrows the
// console it was started from.
static void BorrowConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* ignored = nullptr;
    ignored = freopen("CONOUT$", "w", stdout);
    ignored = freopen("CONOUT$", "w", stderr);
    (void)ignored;
}

// SDL's own Windows logger attaches to the parent's console and writes there,
// past any file or pipe the output was sent to -- in a windowed program the
// log went nowhere a caller could read. So the log goes through the C
// library's stderr instead, which is that file or pipe if there is one and
// the borrowed console if not.
static void SDLCALL LogToStderr(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata; (void)category; (void)priority;
    std::fprintf(stderr, "%s\n", message);
    std::fflush(stderr);
}
#endif

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
#ifdef _WIN32
    BorrowConsole();
    SDL_SetLogOutputFunction(LogToStderr, nullptr);
#endif
    if (!FindGameRoot()) {
        // Nothing has been initialised yet, so this is the only way to say so
        // to someone who started the game by clicking on it.
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, TitleScreen::kGameName,
            "Could not find the game's data files.\n\n"
            "The game looks for data/ and assets/ beside the executable or "
            "one directory above it. Both are part of the repository, so a "
            "clone has them; if this is a copy of bin/ on its own, bring the "
            "rest of the folder with it.", nullptr);
        return -1;
    }

    Game game;

    if (!game.Start(argc, args)) return -1;
    game.Loop();
    return 0;
}
