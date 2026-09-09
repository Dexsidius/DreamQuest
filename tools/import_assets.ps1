# =============================================================================
#  DreamQuest - asset import
#
#  The art is CraftPix free content. Its licence allows use in a game but not
#  redistribution of the raw files, so the repository ships no art: it ships
#  this script, which rebuilds assets/ from the .zip packs you downloaded.
#
#  Usage:
#     .\tools\import_assets.ps1                     # looks in "E:\Game Assets"
#     .\tools\import_assets.ps1 -GameAssets "D:\packs"
#     .\tools\import_assets.ps1 -Clean              # start from scratch
#
#  What it does:
#     1. Unpacks each craftpix zip into assets/_raw/
#     2. Copies the character animation sheets under short, stable names
#     3. Cuts the flat ground fills out of the packed tilesets with tilecut
#     4. Cuts the loose decorations and buildings out of the packed sheets
#     5. Copies the individually-shipped props (trees, rocks, bushes)
#
#  Everything lands on the exact paths that data/sprites.json and the maps in
#  maps/*.mx refer to, so the game runs straight after this finishes.
# =============================================================================

param(
    [string]$GameAssets = "E:\Game Assets",
    [switch]$Clean,
    [string]$Msys = "C:\msys64\ucrt64"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot\..
$root = (Get-Location).Path

$raw    = Join-Path $root "assets\_raw"
$assets = Join-Path $root "assets"
$tilecut = Join-Path $root "bin\tilecut.exe"

if ($Clean -and (Test-Path $assets)) {
    Write-Host "Removing existing assets/ ..." -ForegroundColor Yellow
    Remove-Item $assets -Recurse -Force
}

if (-not (Test-Path $GameAssets)) {
    throw "Asset folder not found: $GameAssets`nPass -GameAssets <path to the craftpix zips>."
}

# --- 0. tilecut ---------------------------------------------------------------
if (-not (Test-Path $tilecut)) {
    Write-Host "Building tilecut ..." -ForegroundColor Cyan
    if (-not (Test-Path "$Msys\bin\g++.exe")) {
        throw "g++ not found at $Msys\bin. Install MSYS2 UCRT64, or pass -Msys <path>."
    }
    $env:PATH = "$Msys\bin;$env:PATH"
    New-Item -ItemType Directory -Force -Path (Join-Path $root "bin") | Out-Null
    & g++ -std=c++20 -O2 -static-libgcc -static-libstdc++ `
        (Join-Path $root "tools\tilecut.cpp") -o $tilecut -lSDL3 -lSDL3_image
    if ($LASTEXITCODE -ne 0) { throw "Could not build tilecut." }
}
$env:PATH = "$Msys\bin;$env:PATH"

function New-Dir($p) { New-Item -ItemType Directory -Force -Path $p | Out-Null }

# --- 1. unpack ----------------------------------------------------------------
New-Dir $raw
$zips = Get-ChildItem (Join-Path $GameAssets "*.zip") |
        Where-Object { $_.Name -notmatch '\(\d+\)' }      # skip duplicate downloads

if ($zips.Count -eq 0) { throw "No craftpix .zip files found in $GameAssets" }

Write-Host "`nUnpacking $($zips.Count) asset packs ..." -ForegroundColor Cyan
foreach ($zip in $zips) {
    $name = $zip.BaseName -replace '^craftpix-net-\d+-free-', ''
    $dest = Join-Path $raw $name
    if (Test-Path $dest) { continue }
    try {
        Expand-Archive -LiteralPath $zip.FullName -DestinationPath $dest -Force
        Write-Host "  + $name"
    } catch {
        Write-Warning "  ! could not unpack $($zip.Name): $($_.Exception.Message)"
    }
}

function Pack($name) { Join-Path $raw $name }

# --- 2. characters ------------------------------------------------------------
# Each entry maps a clip name to the sheet that holds it. Frame counts live in
# data/sprites.json; here we only give the files short, predictable names.
Write-Host "`nCopying character sheets ..." -ForegroundColor Cyan

function Copy-Sheets($destName, $sourceDir, [hashtable]$clips) {
    $dest = Join-Path $assets "characters\$destName"
    New-Dir $dest
    $copied = 0
    foreach ($clip in $clips.Keys) {
        $src = Join-Path $sourceDir $clips[$clip]
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $dest "$clip.png") -Force
            $copied++
        } else {
            Write-Warning "  ! missing $destName/$clip  ($($clips[$clip]))"
        }
    }
    Write-Host ("  {0,-16} {1} clips" -f $destName, $copied)
}

foreach ($who in @(
    @{ id = "player_male";   pack = "base-4-direction-male-character-pixel-art" },
    @{ id = "player_female"; pack = "base-4-direction-female-character-pixel-art" }
)) {
    Copy-Sheets $who.id (Join-Path (Pack $who.pack) "PNG\Sword\With_shadow") @{
        idle   = "Sword_Idle_with_shadow.png"
        walk   = "Sword_Walk_with_shadow.png"
        run    = "Sword_Run_with_shadow.png"
        attack = "Sword_attack_with_shadow.png"
        hurt   = "Sword_Hurt_with_shadow.png"
        death  = "Sword_Death_with_shadow.png"
    }
}

$orcPack = Pack "top-down-orc-game-character-pixel-art"
foreach ($n in 1, 2, 3) {
    Copy-Sheets "orc$n" (Join-Path $orcPack "PNG\Orc$n\With_shadow") @{
        idle   = "orc${n}_idle_with_shadow.png"
        walk   = "orc${n}_walk_with_shadow.png"
        run    = "orc${n}_run_with_shadow.png"
        attack = "orc${n}_attack_with_shadow.png"
        hurt   = "orc${n}_hurt_with_shadow.png"
        death  = "orc${n}_death_with_shadow.png"
    }
}

$guild = Join-Path (Pack "top-down-pixel-art-guild-hall-asset-pack") "PNG"
foreach ($n in "Citizen1", "Citizen2", "Fighter2") {
    Copy-Sheets $n.ToLower() $guild @{
        idle = "${n}_Idle.png"
        walk = "${n}_Walk.png"
    }
}

$animals = Join-Path (Pack "top-down-hunt-animals-pixel-sprite-pack") "Tiled"
foreach ($a in @(
    @{ id = "boar";  prefix = "Boar";  attack = $true },
    @{ id = "deer";  prefix = "Deer";  attack = $false },
    @{ id = "fox";   prefix = "Fox";   attack = $false },
    @{ id = "hare";  prefix = "Hare";  attack = $false }
)) {
    $clips = @{
        idle  = "$($a.prefix)_Idle_with_shadow.png"
        walk  = "$($a.prefix)_Walk_with_shadow.png"
        run   = "$($a.prefix)_Run_with_shadow.png"
        hurt  = "$($a.prefix)_Hurt_with_shadow.png"
        death = "$($a.prefix)_Death_with_shadow.png"
    }
    if ($a.attack) { $clips["attack"] = "$($a.prefix)_Attack_with_shadow.png" }
    # The fox sheet uses a lower-case walk; accept either spelling.
    if (-not (Test-Path (Join-Path $animals $clips["walk"]))) {
        $clips["walk"] = "$($a.prefix)_walk_with_shadow.png"
    }
    Copy-Sheets $a.id $animals $clips
}

# --- 3. base ground tiles -----------------------------------------------------
# Flat, fully opaque cells picked out of the packed tilesets. These are the
# palette fills the CraftPix sets are designed to be laid over.
Write-Host "`nCutting base ground tiles ..." -ForegroundColor Cyan
$tiles = Join-Path $assets "tiles"
New-Dir $tiles

function Cut-Cells($sheet, [string[]]$cells) {
    if (-not (Test-Path $sheet)) { Write-Warning "  ! missing $sheet"; return }
    & $tilecut $sheet --cells @cells --out $tiles --cell 16 | Out-Null
}

Cut-Cells (Join-Path (Pack "path-and-road-top-down-pixel-tileset") "PNG_Tiled\Ground_grass.png") @(
    "2,2,grass_light", "8,3,grass", "20,3,grass_dark", "26,3,grass_olive",
    "14,3,water", "2,10,dirt", "8,10,dirt_dark", "20,10,sand",
    "14,10,snow", "26,10,moss"
)
Cut-Cells (Join-Path (Pack "path-and-road-top-down-pixel-tileset") "PNG_Tiled\Road1_grass.png") @(
    "2,2,road"
)
Cut-Cells (Join-Path (Pack "cursed-land-top-down-pixel-art-tileset") "PNG\Ground.png") @(
    "8,31,cursed_ground", "8,27,cursed_sand"
)
Cut-Cells (Join-Path (Pack "undead-tileset-top-down-pixel-art") "PNG\Ground_rocks.png") @(
    "20,19,marsh_stone", "3,30,marsh_ground", "16,19,marsh_dark"
)
Cut-Cells (Join-Path (Pack "2d-top-down-pixel-dungeon-asset-pack") "PNG\walls_floor.png") @(
    "6,1,dungeon_floor", "10,1,dungeon_floor_dark",
    "3,1,dungeon_wall", "1,1,dungeon_void"
)
Write-Host ("  {0} ground tiles" -f (Get-ChildItem $tiles -Filter *.png).Count)

# --- 4. decorations cut from packed sheets ------------------------------------
Write-Host "`nCutting decorations and buildings ..." -ForegroundColor Cyan
$decor   = Join-Path $assets "decor"
$objects = Join-Path $assets "objects"
New-Dir $decor
New-Dir $objects

function Cut-Sprites($sheet, $outDir, $prefix, $gap = 3, $minSize = 10) {
    if (-not (Test-Path $sheet)) { Write-Warning "  ! missing $sheet"; return 0 }
    & $tilecut $sheet --sprites --gap $gap --min-size $minSize `
        --out $outDir --prefix $prefix | Out-Null
    return (Get-ChildItem $outDir -Filter "$prefix*.png").Count
}

$n = Cut-Sprites (Join-Path (Pack "path-and-road-top-down-pixel-tileset") "PNG_Tiled\Ground_grass.png") $decor "patch" 2 12
Write-Host "  $n ground patches"

foreach ($i in 1, 3) {
    Cut-Sprites (Join-Path (Pack "path-and-road-top-down-pixel-tileset") "PNG_Tiled\Road${i}_grass.png") $decor "roadpiece$i" 2 20 | Out-Null
}
Write-Host ("  {0} road pieces" -f (Get-ChildItem $decor -Filter "roadpiece*.png").Count)

# Buildings. The indices are the drawings tilecut finds on each sheet, ordered
# top-to-bottom then left-to-right; they are stable for these files.
$buildings = Join-Path $assets "_cut_buildings"
New-Dir $buildings
Cut-Sprites (Join-Path $guild "Exterior.png") $buildings "guild" 4 12 | Out-Null
Cut-Sprites (Join-Path (Pack "glassblowers-workshop-top-down-pixel-art-asset") "PNG\Exterior_house.png") $buildings "house" 4 12 | Out-Null

$buildingMap = @{
    "guild_01" = "building_guild"
    "house_00" = "building_house_a"
    "house_03" = "building_house_b"
    "house_04" = "building_shop"
    "guild_03" = "sign_guild"
}
foreach ($k in $buildingMap.Keys) {
    $src = Join-Path $buildings "$k.png"
    if (Test-Path $src) { Copy-Item $src (Join-Path $objects "$($buildingMap[$k]).png") -Force }
    else { Write-Warning "  ! building piece $k not found" }
}
Remove-Item $buildings -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ("  {0} buildings" -f (Get-ChildItem $objects -Filter "building_*.png").Count)

# Chests and doors come off one animation strip, so take the exact frames
# rather than letting the sprite finder merge a chest with the lever beside it.
$chestSheet = Join-Path (Pack "2d-top-down-pixel-dungeon-asset-pack") "PNG\doors_lever_chest_animation.png"
if (Test-Path $chestSheet) {
    & $tilecut $chestSheet --region `
        "0,128,32,32,chest" `
        "128,128,32,32,chest_open" `
        "0,32,32,32,door" `
        "64,96,32,32,door_open" `
        --out $objects | Out-Null
}

# One frame of the fire loop makes a serviceable cooking range.
$fireSheet = Join-Path (Pack "2d-top-down-pixel-dungeon-asset-pack") "PNG\fire_animation.png"
if (Test-Path $fireSheet) {
    & $tilecut $fireSheet --region "0,0,48,48,campfire" --out $objects | Out-Null
}
Write-Host ("  {0} interactables" -f (Get-ChildItem $objects -Filter "chest*.png").Count)

# --- 5. individually shipped props --------------------------------------------
Write-Host "`nCopying props ..." -ForegroundColor Cyan

function Copy-Props($sourceDir, $pattern, $prefix, $limit) {
    if (-not (Test-Path $sourceDir)) { Write-Warning "  ! missing $sourceDir"; return 0 }
    $files = Get-ChildItem $sourceDir -Filter $pattern -File |
             Where-Object { $_.Name -notmatch 'no_shadow' } |
             Sort-Object Name | Select-Object -First $limit
    $i = 0
    foreach ($f in $files) {
        Copy-Item $f.FullName (Join-Path $objects ("{0}_{1:d2}.png" -f $prefix, $i)) -Force
        $i++
    }
    return $i
}

$treeDir = Join-Path (Pack "top-down-trees-pixel-art") "PNG\Assets_separately\Trees_texture_shadow"
$n = Copy-Props $treeDir "*tree1.png" "tree" 14
$n += Copy-Props $treeDir "*tree2.png" "treesmall" 10
Write-Host "  $n trees"

$n = Copy-Props (Join-Path (Pack "rocks-and-stones-top-down-pixel-art") "PNG\Objects_separately") "Rock?_1.png" "rock" 10
$n += Copy-Props (Join-Path (Pack "rocks-and-stones-top-down-pixel-art") "PNG\Objects_separately") "Rock?_3.png" "rocksmall" 8
Write-Host "  $n rocks"

$bushDir = Join-Path (Pack "top-down-bushes-pixel-art") "PNG\Assets_shadow"
$n = Copy-Props $bushDir "*bush*1.png" "bush" 10
$n += Copy-Props $bushDir "*bush*2.png" "bushsmall" 10
Write-Host "  $n bushes"

$forestDir = Join-Path (Pack "forest-objects-top-down-pixel-art") "PNG\Assets"
$n = Copy-Props $forestDir "*mushroom*.png" "mushroom" 6
$n += Copy-Props $forestDir "Chanterelles*.png" "fungus" 3
Write-Host "  $n forest objects"

# --- 6. UI --------------------------------------------------------------------
Write-Host "`nCopying UI art ..." -ForegroundColor Cyan
$ui = Join-Path $assets "ui"
New-Dir $ui
$uiSrc = Join-Path (Pack "basic-pixel-art-ui-for-rpg") "PNG"
if (Test-Path $uiSrc) {
    Get-ChildItem $uiSrc -Filter *.png -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $ui $_.Name.ToLower()) -Force
    }
    Write-Host ("  {0} UI sheets" -f (Get-ChildItem $ui -Filter *.png).Count)
}

# --- 6b. item icons -----------------------------------------------------------
# The RPG UI pack ships one sheet of item icons. Cutting the specific cells the
# item database refers to keeps the inventory readable without shipping the
# whole sheet as a lookup at runtime.
Write-Host "`nCutting item icons ..." -ForegroundColor Cyan
$icons = Join-Path $assets "icons"
New-Dir $icons

$iconSheet = Join-Path $ui "icons.png"
if (Test-Path $iconSheet) {
    & $tilecut $iconSheet --region `
        "0,0,16,16,skull"          "64,0,16,16,key"          "16,16,16,16,star" `
        "48,16,16,16,scroll"       "64,16,16,16,seal"        "16,64,16,16,page" `
        "0,144,16,16,gem_red"      "16,144,16,16,coin"       "32,144,16,16,gem_blue" `
        "48,144,16,16,meat_raw2"   "64,144,16,16,meat_cooked" "80,144,16,16,meat_raw" `
        "0,160,16,16,meat_red"     "32,96,16,16,scissors"    "48,96,16,16,spark" `
        "80,96,16,16,armour"       "0,128,16,16,sword"       "16,128,16,16,shield" `
        "32,128,16,16,leather"     "48,128,16,16,bow"        "80,128,16,16,ring" `
        "16,80,16,16,amulet"       "64,240,16,16,bar_silver" "80,240,16,16,bar_gold" `
        "64,256,16,16,bar_copper"  "0,272,16,16,bar_grey"    "48,288,16,16,rune" `
        "0,288,16,16,sword_rune"   "80,288,16,16,armour_blue" `
        --out $icons | Out-Null

    # The larger two-cell icons read better for the headline equipment.
    & $tilecut $iconSheet --region `
        "0,176,32,32,helm_big"     "32,176,32,32,shield_big" `
        "64,208,32,32,sword_big"   "0,208,32,32,potion_red" `
        --out $icons | Out-Null
}
Write-Host ("  {0} item icons" -f (Get-ChildItem $icons -Filter *.png -EA SilentlyContinue).Count)

# --- 7. font ------------------------------------------------------------------
# The game falls back to a system font, but a bundled one keeps it identical
# on every machine. Nothing is downloaded; this only copies what is local.
$fonts = Join-Path $assets "fonts"
New-Dir $fonts
if (-not (Test-Path (Join-Path $fonts "dreamquest.ttf"))) {
    foreach ($candidate in @("C:\Windows\Fonts\consola.ttf", "C:\Windows\Fonts\arial.ttf")) {
        if (Test-Path $candidate) {
            Copy-Item $candidate (Join-Path $fonts "dreamquest.ttf") -Force
            Write-Host "`nBundled font from $candidate" -ForegroundColor Cyan
            break
        }
    }
}

# --- done ---------------------------------------------------------------------
$total = (Get-ChildItem $assets -Recurse -File -Filter *.png |
          Where-Object { $_.FullName -notlike "*\_raw\*" }).Count
Write-Host "`nImport complete: $total images under assets/" -ForegroundColor Green
Write-Host "assets/_raw/ holds the unpacked archives and can be deleted." -ForegroundColor DarkGray
