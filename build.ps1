# DreamQuest - Windows build (MSYS2 UCRT64 toolchain)
#
#   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 `
#             mingw-w64-ucrt-x86_64-sdl3-image mingw-w64-ucrt-x86_64-sdl3-ttf `
#             mingw-w64-ucrt-x86_64-enet
#
# Usage:
#   .\build.ps1              build the game
#   .\build.ps1 -Run         build, then launch
#   .\build.ps1 -Debug       unoptimised build with symbols
#   .\build.ps1 -Tools       also build tilecut, genmaps and selftest
#   .\build.ps1 -Test        build and run the self-test, then stop
#   .\build.ps1 -Maps        rebuild maps/*.mx from tools/genmaps.cpp

param(
    [switch]$Run,
    [switch]$Debug,
    [switch]$Tools,
    [switch]$Test,
    [switch]$Maps,
    [string]$Msys = "C:\msys64\ucrt64"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Test-Path "$Msys\bin\g++.exe")) {
    throw "g++ not found at $Msys\bin. Install MSYS2 UCRT64 or pass -Msys <path>."
}
$env:PATH = "$Msys\bin;$env:PATH"

New-Item -ItemType Directory -Force -Path bin, obj | Out-Null

$flags = @('-std=c++20', '-Isrc', '-Wall')
if ($Debug) { $flags += @('-g', '-O0') } else { $flags += @('-O2') }
# ENet is linked statically (-l:libenet.a), so co-op adds no DLL to ship; it
# needs Winsock and the multimedia timer from Windows itself.
$libs = @('-lSDL3', '-lSDL3_image', '-lSDL3_ttf', '-l:libenet.a', '-lws2_32', '-lwinmm')
if (-not (Test-Path "$Msys\include\enet\enet.h")) {
    throw "ENet is not installed. In the MSYS2 UCRT64 shell: pacman -S mingw-w64-ucrt-x86_64-enet"
}

# Any header change rebuilds everything; the tree is small enough that this is
# cheaper than tracking real dependencies.
$newestHeader = (Get-ChildItem -Path src -Recurse -Include *.h, *.hpp -File |
                 Sort-Object LastWriteTimeUtc -Descending |
                 Select-Object -First 1).LastWriteTimeUtc

$gameSources = @(
    Get-ChildItem -Path src -Filter *.cpp -File
    Get-ChildItem -Path src\world, src\entity, src\systems, src\ui, src\net -Filter *.cpp -File
) | Select-Object -ExpandProperty FullName

function Compile-Set($sources) {
    $objects = @()
    $failed = $false
    foreach ($src in $sources) {
        $rel = $src.Substring((Resolve-Path .).Path.Length + 1)
        $obj = Join-Path 'obj' (($rel -replace '[\\/]', '_') -replace '\.cpp$', '.o')
        $objects += $obj

        $needs = -not (Test-Path $obj)
        if (-not $needs) {
            $needs = ((Get-Item $src).LastWriteTimeUtc -gt (Get-Item $obj).LastWriteTimeUtc) -or
                     ($newestHeader -gt (Get-Item $obj).LastWriteTimeUtc)
        }
        if ($needs) {
            Write-Host "  CC  $rel"
            & g++ @flags -c $src -o $obj
            if ($LASTEXITCODE -ne 0) { $failed = $true }
        }
    }
    if ($failed) { throw "Compilation failed." }
    return $objects
}

# --- tools --------------------------------------------------------------------
# tilecut and genmaps are standalone; selftest links the game's own systems so
# it exercises exactly the code the game runs.
if ($Tools -or $Maps) {
    foreach ($tool in 'tilecut', 'genmaps') {
        Write-Host "  CC  tools/$tool.cpp"
        & g++ @flags -O2 -static-libgcc -static-libstdc++ "tools\$tool.cpp" -o "bin\$tool.exe" @libs
        if ($LASTEXITCODE -ne 0) { throw "Could not build $tool." }
    }
}

if ($Maps) {
    Write-Host "`nRegenerating maps ..." -ForegroundColor Cyan
    & .\bin\genmaps.exe
    if ($LASTEXITCODE -ne 0) { throw "genmaps failed." }
    if (-not ($Run -or $Test -or $Tools)) { return }
}

if ($Test -or $Tools) {
    # The self-test drives the game's systems directly, so it links everything
    # except the files that own main() and the Game class's own screen code.
    $testSources = $gameSources | Where-Object {
        (Split-Path $_ -Leaf) -notin @('main.cpp', 'game.cpp', 'screens.cpp', 'lobby.cpp')
    }
    Write-Host "  CC  tools/selftest.cpp"
    & g++ @flags -O1 tools\selftest.cpp @testSources -o bin\selftest.exe @libs
    if ($LASTEXITCODE -ne 0) { throw "Could not build selftest." }
}

if ($Test) {
    Write-Host "`nRunning self-test ..." -ForegroundColor Cyan
    # The self-test writes its progress to stderr. Windows PowerShell turns each
    # stderr line from a native program into an error record, which "Stop" then
    # treats as fatal, so a passing run reported itself as a failure. Only the
    # exit code decides here.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & .\bin\selftest.exe } finally { $ErrorActionPreference = $prev }
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "$code self-test failure(s)." }
    Write-Host "Self-test passed." -ForegroundColor Green
    return
}

# --- game ---------------------------------------------------------------------
$objects = Compile-Set $gameSources

# The .exe's own icon. Explorer never runs the program, so the window icon set
# at startup does nothing for the file or a shortcut to it: the picture has to
# be compiled in as a resource. Skipped where windres is missing rather than
# failing the build, since it costs nothing but the icon.
$rc = 'tools\appicon.rc'
$ico = 'art\dreamquest.ico'
if ((Test-Path $rc) -and (Test-Path $ico) -and (Get-Command windres -ErrorAction SilentlyContinue)) {
    $res = 'obj\appicon.o'
    $stale = (-not (Test-Path $res)) -or
             ((Get-Item $rc).LastWriteTimeUtc  -gt (Get-Item $res).LastWriteTimeUtc) -or
             ((Get-Item $ico).LastWriteTimeUtc -gt (Get-Item $res).LastWriteTimeUtc)
    if ($stale) {
        Write-Host "  RC  $rc"
        & windres $rc -O coff -o $res
        if ($LASTEXITCODE -ne 0) { throw "Could not compile the icon resource." }
    }
    $objects += $res
}

Write-Host "  LD  bin/DreamQuest.exe"
& g++ $objects -o bin\DreamQuest.exe @libs
if ($LASTEXITCODE -ne 0) { throw "Link failed." }

# --- runtime DLLs -------------------------------------------------------------
# Outside an MSYS2 shell nothing is on the PATH, so every non-system library has
# to sit beside the exe. A fixed list of these was wrong: SDL3_ttf pulls in
# FreeType, which pulls in libpng, bzip2, Brotli and zlib, and SDL3 pulls in
# libiconv -- miss one and Windows refuses to start the program with no useful
# message. So the tree is walked instead of listed.
#
# Anything that does not resolve inside the MSYS2 bin directory is a Windows
# system DLL (kernel32, the api-ms-win-crt-* UCRT stubs, and so on) and is left
# where it is.
function Get-DllDependencies($binary, $searchDir, $seen) {
    $found = & objdump -p $binary 2>$null |
             Select-String -Pattern '^\s*DLL Name:\s*(.+)$' |
             ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }

    foreach ($name in $found) {
        $key = $name.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }

        $path = Join-Path $searchDir $name
        if (-not (Test-Path $path)) { continue }   # a system DLL; not ours to ship

        $seen[$key] = $path
        Get-DllDependencies $path $searchDir $seen
    }
}

$needed = @{}
Get-DllDependencies 'bin\DreamQuest.exe' "$Msys\bin" $needed

$copied = 0
foreach ($src in $needed.Values) {
    $dest = Join-Path 'bin' (Split-Path $src -Leaf)
    # Copy when it is missing or older, so an MSYS2 update is picked up rather
    # than leaving a stale library beside a freshly built exe.
    if ((-not (Test-Path $dest)) -or
        ((Get-Item $src).LastWriteTimeUtc -gt (Get-Item $dest).LastWriteTimeUtc)) {
        Copy-Item $src $dest -Force
        $copied++
    }
}
Write-Host ("  DLL {0} runtime libraries beside the exe{1}" -f
            $needed.Count, $(if ($copied) { ", $copied copied" } else { "" }))

Write-Host "Built bin\DreamQuest.exe" -ForegroundColor Green
if ($Run) { & .\bin\DreamQuest.exe }
