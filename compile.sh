#! /bin/sh
# DreamQuest - Linux / macOS build.
# Windows users: run build.ps1 (MSYS2 UCRT64).
#
#   ./compile.sh          build the game
#   ./compile.sh tools    also build tilecut, genmaps and selftest
#   ./compile.sh test     build and run the self-test
#   ./compile.sh maps     rebuild maps/*.mx
set -e

CXX=${CXX:-c++}
FLAGS="-std=c++20 -O2 -Isrc -Wall"
LIBS="-lSDL3 -lSDL3_image -lSDL3_ttf -lenet"

GAME_SRC="src/*.cpp src/world/*.cpp src/entity/*.cpp src/systems/*.cpp src/ui/*.cpp src/net/*.cpp src/coop/*.cpp"
# Everything except the two files that own main(), for the self-test.
TEST_SRC="src/camera.cpp src/input.cpp src/sprite.cpp src/texturecache.cpp \
          src/ui/ui.cpp src/ui/minimap.cpp src/world/*.cpp src/entity/*.cpp src/systems/*.cpp \
          src/net/*.cpp src/coop/*.cpp"

mkdir -p bin

build_tools() {
    echo "  CC  tools/tilecut.cpp"
    $CXX $FLAGS tools/tilecut.cpp -o bin/tilecut $LIBS
    echo "  CC  tools/genmaps.cpp"
    $CXX $FLAGS tools/genmaps.cpp -o bin/genmaps $LIBS
    echo "  CC  tools/selftest.cpp"
    $CXX $FLAGS -O1 tools/selftest.cpp $TEST_SRC -o bin/selftest $LIBS
}

case "${1:-}" in
    tools) build_tools; exit 0 ;;
    test)  build_tools; exec ./bin/selftest ;;
    maps)  build_tools; exec ./bin/genmaps ;;
esac

echo "  CC  game"
$CXX $FLAGS $GAME_SRC -o bin/DreamQuest.x86_64 $LIBS

[ -L DreamQuest.x86_64 ] && unlink DreamQuest.x86_64
ln -s bin/DreamQuest.x86_64 DreamQuest.x86_64
echo "Built bin/DreamQuest.x86_64"
