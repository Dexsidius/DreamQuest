# DreamQuest - Windows build (MSYS2 UCRT64 toolchain)
#
#   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 `
#             mingw-w64-ucrt-x86_64-sdl3-image mingw-w64-ucrt-x86_64-sdl3-ttf
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
$libs = @('-lSDL3', '-lSDL3_image', '-lSDL3_ttf')

# Any header change rebuilds everything; the tree is small enough that this is
# cheaper than tracking real dependencies.
$newestHeader = (Get-ChildItem -Path src -Recurse -Include *.h, *.hpp -File |
                 Sort-Object LastWriteTimeUtc -Descending |
                 Select-Object -First 1).LastWriteTimeUtc

$gameSources = @(
    Get-ChildItem -Path src -Filter *.cpp -File
    Get-ChildItem -Path src\world, src\entity, src\systems, src\ui -Filter *.cpp -File
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
        (Split-Path $_ -Leaf) -notin @('main.cpp', 'game.cpp', 'screens.cpp')
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

Write-Host "  LD  bin/DreamQuest.exe"
& g++ $objects -o bin\DreamQuest.exe @libs
if ($LASTEXITCODE -ne 0) { throw "Link failed." }

# The exe needs SDL and the GCC runtime beside it to run outside an MSYS2 shell.
$runtime = @(
    'SDL3.dll', 'SDL3_image.dll', 'SDL3_ttf.dll',
    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll'
)
foreach ($dll in $runtime) {
    $from = Join-Path "$Msys\bin" $dll
    if ((Test-Path $from) -and -not (Test-Path (Join-Path 'bin' $dll))) {
        Copy-Item $from bin\ -Force
    }
}

Write-Host "Built bin\DreamQuest.exe" -ForegroundColor Green
if ($Run) { & .\bin\DreamQuest.exe }
