#! /bin/sh
# DreamQuest - Linux / macOS build.
# Windows users: run build.ps1 (MSYS2 UCRT64).
#
#   ./compile.sh          build the game
#   ./compile.sh tools    also build tilecut, genmaps and selftest
#   ./compile.sh test     build and run the self-test
#   ./compile.sh maps     rebuild maps/*.mx
#   ./compile.sh server   build the headless co-op server
set -e

CXX=${CXX:-c++}
FLAGS="-std=c++20 -O2 -Isrc -Wall"
LIBS="-lSDL3 -lSDL3_image -lSDL3_ttf -lenet"

GAME_SRC="src/*.cpp src/world/*.cpp src/entity/*.cpp src/systems/*.cpp src/ui/*.cpp src/net/*.cpp src/coop/*.cpp"
# Everything except the files that own main() and the Game class (game.cpp,
# lobby.cpp, splitscreen.cpp, and ui/screens.cpp with the screen_*.cpp it was
# cut into), for the self-test and the server -- the same list build.ps1 takes.
# Worked out from GAME_SRC rather than written out: it used to be written out,
# and a file added under src/ui (worldmap.cpp, titlescreen.cpp) was missing
# from it, so the self-test would not link on Linux.
TEST_SRC=$(for f in $GAME_SRC; do
    case "$f" in
        src/main.cpp|src/game.cpp|src/ui/screens.cpp|src/ui/lobby.cpp|src/ui/splitscreen.cpp|src/ui/screen_*.cpp) ;;
        *) printf '%s ' "$f" ;;
    esac
done)

mkdir -p bin

# The fragment shaders, as SPIR-V, when glslc is about (shaderc);
# the compiled ones are committed, so without it they are simply kept.
mkdir -p assets/shaders
for frag in src/shaders/*.frag; do
    spv="assets/shaders/$(basename "$frag").spv"
    if [ ! -f "$spv" ] || [ "$frag" -nt "$spv" ]; then
        if command -v glslc >/dev/null 2>&1; then
            echo "  GLSL  $frag"
            glslc -fshader-stage=frag -O "$frag" -o "$spv"
        else
            echo "  glslc not found; $spv is out of date with $frag"
        fi
    fi
done

build_tools() {
    echo "  CC  tools/tilecut.cpp"
    $CXX $FLAGS tools/tilecut.cpp -o bin/tilecut $LIBS
    echo "  CC  tools/genmaps.cpp"
    $CXX $FLAGS tools/genmaps.cpp -o bin/genmaps $LIBS
    echo "  CC  tools/selftest.cpp"
    $CXX $FLAGS -O1 tools/selftest.cpp $TEST_SRC -o bin/selftest $LIBS
}

case "${1:-}" in
    server) echo "  CC  tools/server_main.cpp"
            $CXX $FLAGS tools/server_main.cpp $TEST_SRC -o bin/DreamQuestServer $LIBS
            echo "Built bin/DreamQuestServer"; exit 0 ;;
    tools) build_tools; exit 0 ;;
    test)  build_tools; exec ./bin/selftest ;;
    maps)  build_tools; exec ./bin/genmaps ;;
esac

echo "  CC  game"
$CXX $FLAGS $GAME_SRC -o bin/DreamQuest.x86_64 $LIBS

[ -L DreamQuest.x86_64 ] && unlink DreamQuest.x86_64
ln -s bin/DreamQuest.x86_64 DreamQuest.x86_64
echo "Built bin/DreamQuest.x86_64"
