#! /bin/sh
# DreamQuest - package.ps1's zip, made on Linux.
#
# The same zip anyone on Windows unpacks and plays -- DreamQuest.exe, the
# headless DreamQuestServer.exe, the DLLs they need, every file under assets/,
# art/, maps/ and data/ that git tracks, and PLAY.txt -- but built here, with
# MinGW-w64 cross-compiling for Windows, for when the Windows machine is not
# to hand. What ships, and the note in it, are package.ps1's: the file list is
# git's, and PLAY.txt is read out of package.ps1 so there is one copy of it.
#
#   ./package.sh               cross-build, then package into dist/
#   ./package.sh --skip-build  package what bin/win64/ already holds
#
# Needs git, curl, zip and the MinGW-w64 cross compiler with POSIX threads
# (Debian/Ubuntu: apt install g++-mingw-w64-x86-64-posix zip). The Windows
# libraries are fetched the first time into deps/win64/ (not committed): the
# SDL projects' own MinGW builds of SDL3, SDL3_image and SDL3_ttf, whose DLLs
# need nothing but Windows, and ENet, built here as a static library the way
# build.ps1 links it. The C++ runtime is linked in statically, so the three
# SDL DLLs are all that ship beside the exe.
set -e
cd "$(dirname "$0")"

SKIP_BUILD=0
[ "${1:-}" = "--skip-build" ] && SKIP_BUILD=1

TRIPLE=x86_64-w64-mingw32
CXX=${CXX_WIN:-$TRIPLE-g++-posix}
command -v "$CXX" >/dev/null 2>&1 || CXX=$TRIPLE-g++
CC=${CC_WIN:-$TRIPLE-gcc-posix}
command -v "$CC" >/dev/null 2>&1 || CC=$TRIPLE-gcc
WINDRES=$TRIPLE-windres
OBJDUMP=$TRIPLE-objdump
AR=$TRIPLE-ar
for tool in "$CXX" "$CC" $WINDRES $OBJDUMP $AR git zip curl; do
    command -v "$tool" >/dev/null 2>&1 || { echo "$tool not found. See the top of package.sh."; exit 1; }
done

SDL_VER=3.4.8
IMG_VER=3.2.4
TTF_VER=3.2.2
ENET_TAG=v1.3.18
DEPS=${DEPS:-deps/win64}
OUT=bin/win64
OBJ=obj/win64
JOBS=$(nproc 2>/dev/null || echo 2)

# --- the Windows libraries --------------------------------------------------------
fetch() {   # project, tarball name, version
    dir="$DEPS/$2-$3"
    [ -d "$dir" ] && return
    echo "  GET  $2-devel-$3-mingw"
    mkdir -p "$DEPS"
    # Whole to a file first, and again if GitHub's servers stumble: a stream
    # cut off halfway would leave a directory that looks fetched and is not.
    curl -sSfL --retry 4 --retry-all-errors -o "$DEPS/$2.tar.gz" \
        "https://github.com/libsdl-org/$1/releases/download/release-$3/$2-devel-$3-mingw.tar.gz"
    tar xzf "$DEPS/$2.tar.gz" -C "$DEPS" || { rm -rf "$dir"; exit 1; }
    rm -f "$DEPS/$2.tar.gz"
}
if [ $SKIP_BUILD -eq 0 ]; then
    fetch SDL       SDL3       $SDL_VER
    fetch SDL_image SDL3_image $IMG_VER
    fetch SDL_ttf   SDL3_ttf   $TTF_VER
    if [ ! -f "$DEPS/enet/lib/libenet.a" ]; then
        echo "  GET  enet $ENET_TAG"
        rm -rf "$DEPS/enet-src"
        git clone -q --depth 1 --branch $ENET_TAG https://github.com/lsalzman/enet.git "$DEPS/enet-src"
        mkdir -p "$DEPS/enet/lib" "$DEPS/enet/obj"
        for c in callbacks compress host list packet peer protocol win32; do
            $CC -O2 -I"$DEPS/enet-src/include" -c "$DEPS/enet-src/$c.c" -o "$DEPS/enet/obj/$c.o"
        done
        $AR rcs "$DEPS/enet/lib/libenet.a" "$DEPS"/enet/obj/*.o
        cp -r "$DEPS/enet-src/include" "$DEPS/enet/include"
    fi
fi
SDL_DIRS="$DEPS/SDL3-$SDL_VER/$TRIPLE $DEPS/SDL3_image-$IMG_VER/$TRIPLE $DEPS/SDL3_ttf-$TTF_VER/$TRIPLE"

# --- the build: build.ps1's flags, libraries and source lists ----------------------
if [ $SKIP_BUILD -eq 0 ]; then
    INC="-Isrc -I$DEPS/enet/include"
    LIBDIRS="-L$DEPS/enet/lib"
    for d in $SDL_DIRS; do INC="$INC -I$d/include"; LIBDIRS="$LIBDIRS -L$d/lib"; done
    FLAGS="-std=c++20 -Wall -O2 $INC"
    # ENet static, as build.ps1 has it, with the Winsock and timer it needs;
    # the C++ runtime and its thread library static, so there is nothing of
    # the compiler's to ship.
    # libstdc++ is named before winpthread, statically, because it is what
    # needs the thread library: left to the compiler it comes last, after
    # winpthread has been passed, and the exe asked for libwinpthread-1.dll.
    LIBS="-lSDL3 -lSDL3_image -lSDL3_ttf -l:libenet.a -lws2_32 -lwinmm \
          -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic"

    GAME_SRC=$(ls src/*.cpp src/world/*.cpp src/entity/*.cpp src/systems/*.cpp src/ui/*.cpp src/net/*.cpp src/coop/*.cpp)
    # The server links what the self-test links: everything but the Game
    # class and main() (build.ps1's list, and compile.sh's).
    SERVER_SRC=$(for f in $GAME_SRC; do
        case "$f" in
            src/main.cpp|src/game.cpp|src/ui/screens.cpp|src/ui/lobby.cpp|src/ui/splitscreen.cpp|src/ui/screen_*.cpp) ;;
            *) printf '%s ' "$f" ;;
        esac
    done)
    mkdir -p "$OUT" "$OBJ"
    # One object a source, rebuilt when it or any header is newer, as
    # build.ps1 does; as many at once as there are cores.
    for s in $GAME_SRC tools/server_main.cpp; do
        o="$OBJ/$(echo "$s" | tr / _ | sed 's/\.cpp$/.o/')"
        if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$(find src -name '*.h' -newer "$o" | head -1)" ]; then
            echo "$s $o"
        fi
    done | xargs -r -P "$JOBS" -n 2 sh -c 'echo "  CC  $0"; '"$CXX $FLAGS"' -c "$0" -o "$1"'
    objs() { for s in "$@"; do printf '%s ' "$OBJ/$(echo "$s" | tr / _ | sed 's/\.cpp$/.o/')"; done; }

    # The exe's own icon, as build.ps1 compiles it in.
    ICON=""
    if [ -f tools/appicon.rc ] && [ -f art/dreamquest.ico ]; then
        echo "  RC  tools/appicon.rc"
        $WINDRES tools/appicon.rc -O coff -o "$OBJ/appicon.o"
        ICON="$OBJ/appicon.o"
    fi
    echo "  LD  $OUT/DreamQuest.exe"
    $CXX $(objs $GAME_SRC) $ICON -o "$OUT/DreamQuest.exe" $LIBDIRS $LIBS
    echo "  LD  $OUT/DreamQuestServer.exe"
    $CXX $(objs tools/server_main.cpp $SERVER_SRC) -o "$OUT/DreamQuestServer.exe" $LIBDIRS $LIBS

    # The DLLs, found by walking what each binary imports, as build.ps1 does:
    # whatever is found among the SDL builds is shipped, and whatever is not
    # is Windows' own.
    rm -f "$OUT"/*.dll
    walk() {
        for name in $($OBJDUMP -p "$1" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\r'); do
            [ -f "$OUT/$name" ] && continue
            for d in $SDL_DIRS; do
                if [ -f "$d/bin/$name" ]; then
                    cp "$d/bin/$name" "$OUT/"
                    walk "$OUT/$name"
                fi
            done
        done
    }
    walk "$OUT/DreamQuest.exe"
    walk "$OUT/DreamQuestServer.exe"
fi
[ -f "$OUT/DreamQuest.exe" ] || { echo "$OUT/DreamQuest.exe is missing. Run ./package.sh without --skip-build."; exit 1; }

# --- what ships: package.ps1's ------------------------------------------------------
FILES=$(git ls-files assets art maps data)
[ "$(printf '%s\n' "$FILES" | wc -l)" -ge 100 ] || { echo "git ls-files returned too little; is this a clone of the repository?"; exit 1; }
VERSION=$(git describe --tags --always 2>/dev/null || date +%Y%m%d)
NAME="DreamQuest-win64-$VERSION"
STAGE="dist/$NAME"
ZIP="dist/$NAME.zip"

echo "Packaging $NAME ..."
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
cp "$OUT/DreamQuest.exe" "$STAGE/"
[ -f "$OUT/DreamQuestServer.exe" ] && cp "$OUT/DreamQuestServer.exe" "$STAGE/"
DLLS=0
for dll in "$OUT"/*.dll; do [ -f "$dll" ] && cp "$dll" "$STAGE/" && DLLS=$((DLLS + 1)); done
git ls-files -z assets art maps data | xargs -0 cp --parents -t "$STAGE"
COPIED=$(printf '%s\n' "$FILES" | wc -l)

# PLAY.txt is package.ps1's here-string, with its version, in Windows' line
# ends. package.ps1 is kept with Windows' line ends itself, so they come off
# first, or no line would match.
tr -d '\r' < package.ps1 | awk '/^\$play = @"$/ {on = 1; next} /^"@$/ {on = 0} on' |
    sed "s/\$version/$VERSION/g" | sed 's/$/\r/' > "$STAGE/PLAY.txt"
[ -s "$STAGE/PLAY.txt" ] || { echo "Could not read PLAY.txt out of package.ps1."; exit 1; }

(cd "$STAGE" && zip -q -r -9 "../$NAME.zip" .)
SIZE=$(du -m "$ZIP" | cut -f1)
echo "Packaged $COPIED data files and $DLLS libraries into $ZIP (${SIZE} MB)"
echo "Send the zip. Unpack anywhere, double-click DreamQuest.exe."
