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
Add-Type -AssemblyName System.Drawing
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
    # Newer downloads drop the "net-" segment, so both spellings are handled.
    $name = $zip.BaseName -replace '^craftpix-(net-)?\d+-free-', ''
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

# The playable characters are not imported from anything: the game has its own,
# three looks off one rig, rendered by tools/make_character.ps1. The two
# CraftPix ones that used to stand beside it were dropped so that assets/ can
# be redistributed with the game -- that pack's licence covers using the art,
# not passing the files on.
#
# Copy-Layers stays because it is what splits a pack character into the shadow,
# body, head and weapon sheets a paperdoll needs; nothing calls it now, and the
# next pack character to be imported will want it.
function Copy-Layers($destName, $partsDir, $clipMap) {
    if (-not (Test-Path $partsDir)) { Write-Warning "  ! missing $partsDir"; return }
    $dest = Join-Path $assets "characters\$destName\layers"
    New-Dir $dest

    $copied = 0
    foreach ($clip in $clipMap.Keys) {
        $prefix = $clipMap[$clip]
        # e.g. Sword_Idle3_body.png -> order 3, slot "body"
        $parts = Get-ChildItem $partsDir -Filter "$prefix*.png" -File |
                 ForEach-Object {
                     if ($_.BaseName -match "^$([regex]::Escape($prefix))(\d+)_(.+)$") {
                         [pscustomobject]@{
                             order = [int]$Matches[1]
                             slot  = $Matches[2].ToLower()
                             file  = $_
                         }
                     }
                 } | Sort-Object order

        foreach ($part in $parts) {
            # The red overlay is a hit flash the engine does with a colour mod.
            if ($part.slot -eq 'red') { continue }
            $name = "{0}_{1}_{2}.png" -f $clip, $part.order, $part.slot
            Copy-Item $part.file.FullName (Join-Path $dest $name) -Force
            $copied++
        }
    }
    Write-Host ("  {0,-16} {1} layer files" -f "$destName/layers", $copied)
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

# The ground decals used to be cut from this pack's Ground_grass sheet, but
# what that sheet holds are flat-colour stencils for blending grass into a
# path, not things lying on the ground. They are drawn by make_decals.ps1.
& (Join-Path $PSScriptRoot "make_decals.ps1")

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

# --- the guild hall's insides -------------------------------------------------
# The guild pack ships an interior set that matches the exteriors: a plank
# floor, a plastered wall with a blue stone course, and the furniture to put in
# front of it. The floor and wall come out as exact rectangles because they
# have to tile; the furniture is traced, because each piece is a separate
# drawing with space around it.
$wallsSheet = Join-Path $guild "Walls_interior.png"
if (Test-Path $wallsSheet) {
    & $tilecut $wallsSheet --region `
        "288,96,32,32,guild_floor" `
        "160,96,32,32,guild_wall" `
        --out $tiles | Out-Null
    & $tilecut $wallsSheet --region `
        "224,96,32,32,guild_door" `
        --out $objects | Out-Null
}

$interior = Join-Path $assets "_cut_interior"
New-Dir $interior
Cut-Sprites (Join-Path $guild "Interior_objects.png") $interior "i" 3 10 | Out-Null

# Traced pieces come out numbered top-to-bottom then left-to-right, which is
# stable for this file. The names are what maps/*.mx and genmaps.cpp refer to.
$interiorMap = @{
    "i_02" = "guild_cabinet"      # potions and ledgers
    "i_04" = "guild_bookshelf"
    "i_05" = "guild_bookshelf_b"
    "i_07" = "guild_noticeboard"  # the mission board
    "i_11" = "guild_rack"
    "i_12" = "guild_armour_rack"
    "i_13" = "guild_rug"
    "i_15" = "guild_bench"
    "i_16" = "guild_desk"
    "i_22" = "guild_table"
    "i_23" = "guild_weapon_rack"
    "i_24" = "guild_settle"       # bench with a back
    "i_25" = "guild_couch"
    "i_26" = "guild_banner"
    "i_27" = "guild_plant"
    "i_33" = "guild_chair"
    "i_44" = "guild_chest"
}
$interiorCount = 0
foreach ($k in $interiorMap.Keys) {
    $src = Join-Path $interior "$k.png"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $objects "$($interiorMap[$k]).png") -Force
        $interiorCount++
    } else {
        Write-Warning "  ! interior piece $k not found"
    }
}
Remove-Item $interior -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "  $interiorCount guild interior pieces"

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

# A single arrow, drawn pointing down in the source. The projectile renderer
# rotates it, so one sprite covers every direction.
$arrowSheet = Join-Path (Pack "pixel-dungeon-props-and-objects-asset-pack") "PNG\Arrow.png"
if (Test-Path $arrowSheet) {
    & $tilecut $arrowSheet --region "52,0,7,25,arrow" --out $objects | Out-Null
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

# --- 6c. armour, robes, staves and blades -------------------------------------
# Four CraftPix icon packs, all 512x512 painted inventory art rather than sprite
# layers. Two images come out of each file: a square one for the inventory
# panel, and a much smaller one to wear on the character. Pre-scaling the worn
# copy here matters -- a 512 to 24 reduction done properly at import looks far
# better than letting the renderer do it every frame.
#
# The source files are numbered, not named, so the catalogue below was read off
# the art itself rather than guessed from filenames. docs/ASSETS.md records what
# each number depicts.
Write-Host "`nCutting armour, robe and weapon icons ..." -ForegroundColor Cyan
$armourDir = Join-Path $assets "icons\armour"
$wornDir   = Join-Path $armourDir "worn"
New-Dir $armourDir
New-Dir $wornDir

# The icons are drawn on wildly different amounts of empty space -- a helmet
# fills its canvas, a pair of gauntlets sits in a thin band across the middle.
# Scaling the raw canvas would therefore make identical rectangles produce
# wildly different apparent sizes, so every icon is first trimmed to the pixels
# that are actually drawn.
function Get-AlphaBounds($bmp) {
    $data = $bmp.LockBits(
        (New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
        [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        $l = $bmp.Width; $t = $bmp.Height; $r = -1; $b = -1
        for ($y = 0; $y -lt $bmp.Height; $y++) {
            $rowStart = $y * $data.Stride
            for ($x = 0; $x -lt $bmp.Width; $x++) {
                if ($bytes[$rowStart + $x * 4 + 3] -lt 8) { continue }
                if ($x -lt $l) { $l = $x }
                if ($x -gt $r) { $r = $x }
                if ($y -lt $t) { $t = $y }
                if ($y -gt $b) { $b = $y }
            }
        }
    } finally { $bmp.UnlockBits($data) }
    if ($r -lt $l -or $b -lt $t) { return $null }
    return @($l, $t, ($r - $l + 1), ($b - $t + 1))
}

# Flattens an image for use next to pixel art. The packs are painted and
# anti-aliased; shrunk to character size with a smooth filter they read as a
# soft blob against a 16px-grid sprite. Cutting the alpha to on-or-off and
# stepping the colours back gives a defined edge and a flatter palette.
function Harden($bmp) {
    $data = $bmp.LockBits(
        (New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height),
        [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
        [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        for ($y = 0; $y -lt $bmp.Height; $y++) {
            $rowStart = $y * $data.Stride
            for ($x = 0; $x -lt $bmp.Width; $x++) {
                $i = $rowStart + $x * 4
                if ($bytes[$i + 3] -lt 110) {
                    # Below the threshold it is background, not a soft edge.
                    $bytes[$i] = 0; $bytes[$i + 1] = 0; $bytes[$i + 2] = 0; $bytes[$i + 3] = 0
                } else {
                    $bytes[$i + 3] = 255
                    for ($c = 0; $c -lt 3; $c++) {
                        $v = [int]$bytes[$i + $c]
                        $bytes[$i + $c] = [byte]([math]::Min(255, [int]([math]::Round($v / 32.0) * 32)))
                    }
                }
            }
        }
        [Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    } finally { $bmp.UnlockBits($data) }
}

# Trims $src to its drawn pixels and writes it to $dest. In square mode the art
# is centred in a $size box, which is what the inventory grid wants. Otherwise
# the output is only as big as the art, its longest side $size, and the
# proportions of the drawing survive into the file -- which is what lets the
# worn rectangle below be derived rather than guessed.
# Returns the width-to-height ratio of the trimmed art.
function Cut-Icon($src, $dest, $size, $square, $harden) {
    $img = [System.Drawing.Bitmap]::FromFile($src)
    try {
        $bounds = Get-AlphaBounds $img
        if (-not $bounds) { $bounds = @(0, 0, $img.Width, $img.Height) }
        $srcRect = New-Object System.Drawing.Rectangle -ArgumentList $bounds[0], $bounds[1], $bounds[2], $bounds[3]
        $aspect = $bounds[2] / [double]$bounds[3]

        $scale = $size / [double][math]::Max($bounds[2], $bounds[3])
        $w = [math]::Max(1, [int][math]::Round($bounds[2] * $scale))
        $h = [math]::Max(1, [int][math]::Round($bounds[3] * $scale))

        if ($square) { $outW = $size; $outH = $size } else { $outW = $w; $outH = $h }
        $dst = New-Object System.Drawing.Bitmap -ArgumentList $outW, $outH

        $g = [System.Drawing.Graphics]::FromImage($dst)
        try {
            $g.InterpolationMode  = 'HighQualityBicubic'
            $g.PixelOffsetMode    = 'HighQuality'
            $g.CompositingQuality = 'HighQuality'
            $g.Clear([System.Drawing.Color]::Transparent)
            $dstRect = New-Object System.Drawing.Rectangle -ArgumentList `
                       ([int](($outW - $w) / 2)), ([int](($outH - $h) / 2)), $w, $h
            $g.DrawImage($img, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
        } finally { $g.Dispose() }

        if ($harden) { Harden $dst }
        $dst.Save($dest, [System.Drawing.Imaging.ImageFormat]::Png)
        $dst.Dispose()
        return $aspect
    } finally { $img.Dispose() }
}

# Each pack buries its icons a different number of folders down, so rather than
# hard-coding paths, find the subfolder of the given name holding the most PNGs.
# The boot pack ships the same 50 boots twice, once on a background tile, so
# naming the folder is what picks the cut-out copies.
function Find-IconDir($packName, $preferred) {
    $rootDir = Pack $packName
    if (-not (Test-Path $rootDir)) { return $null }
    $best = $null; $bestCount = 0
    foreach ($d in (Get-ChildItem $rootDir -Recurse -Directory -EA SilentlyContinue)) {
        if ($d.FullName -match '__MACOSX') { continue }
        if ($preferred -and $d.Name -ne $preferred) { continue }
        $c = (Get-ChildItem $d.FullName -Filter *.png -File -EA SilentlyContinue).Count
        if ($c -gt $bestCount) { $best = $d.FullName; $bestCount = $c }
    }
    return $best
}

# Where each piece sits, in the pixels of the 64px animation frame. These are
# measured off the rig rather than chosen: in an idle frame the head layer
# covers x25..38 y22..35, the torso x25..38 y32..44, the shadow under the feet
# y40..47, and the held sword x19..27 y38..46. So the character is about 13px
# wide and stands between y22 and y47.
#
# Each slot gets a box, and the art is fitted inside it the way an image fits a
# frame: whichever of width or height runs out first decides the scale, and the
# other stays proportional. Neither dimension alone would do -- sized only by
# width, a tall tasseted skirt would reach the character's chin; sized only by
# height, a broad pair of gauntlets would end up narrower than an arm.
#
# Pieces that hang from the shoulders are placed from the top; anything that
# rests on the ground is placed from the bottom, so a short boot and a tall one
# both stand on the same line.
$anchor = @{
    head   = @{ cx = 31.5; bottom = 34.5; w = 13.0; h = 13.0 }
    body   = @{ cx = 31.5; top    = 31.0; w = 14.0; h = 13.0 }
    hands  = @{ cx = 31.5; top    = 35.0; w = 14.0; h =  9.0 }
    legs   = @{ cx = 31.5; bottom = 46.5; w = 12.0; h = 10.0 }
    feet   = @{ cx = 31.5; bottom = 48.0; w = 12.0; h =  7.0 }
    staff  = @{ cx = 24.5; bottom = 46.0; w = 12.0; h = 20.0 }
    melee  = @{ cx = 23.5; bottom = 45.0; w = 10.0; h = 10.0 }
}

# pack + sub locate the file; file is its basename. Everything else describes
# the item it becomes.
$catalogue = @(
    # --- knight armour, pack 11: a plain and an improved piece per slot --------
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='1';  id='mail_coif';         name='Mail Coif';           slot='head';  def=14; value=190;  req=5;  desc='Riveted mail over a padded cap. Hot, and it works.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='2';  id='plumed_helm';       name='Plumed Helm';         slot='head';  def=26; value=680;  req=20; desc='Gilded, crested, and far too easy to spot across a field.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='3';  id='steel_breastplate'; name='Steel Breastplate';   slot='body';  def=30; value=520;  req=15; desc='Plain plate over a mail voider. Nothing decorative about it.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='4';  id='champion_cuirass';  name="Champion's Cuirass";  slot='body';  def=44; value=1250; req=28; desc='Guild parade armour that has seen rather more than parades.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='5';  id='steel_cuisses';     name='Steel Cuisses';       slot='legs';  def=18; value=300;  req=12; desc='Thigh plates strapped over the hose. Awkward to sit down in.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='6';  id='tasseted_skirt';    name='Tasseted Skirt';      slot='legs';  def=28; value=780;  req=24; desc='Banded tassets over a scarlet underskirt.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='7';  id='steel_bracers';     name='Steel Bracers';       slot='hands'; def=12; value=180;  req=8;  desc='Forearm plates and a pair of well-worn riding gloves.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='8';  id='gilded_gauntlets';  name='Gilded Gauntlets';    slot='hands'; def=22; value=640;  req=22; desc='Articulated fingers, gold filigree, and a grip like a vice.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='9';  id='steel_greaves';     name='Steel Greaves';       slot='feet';  def=16; value=260;  req=10; desc='Shin plates that buckle on over the boot.' },
    @{ pack='game-icons-of-fantasy-knight-armor-pack-11'; sub='PNG'; file='10'; id='gilded_greaves';    name='Gilded Greaves';      slot='feet';  def=26; value=720;  req=24; desc='The matching pair to the gauntlets, and just as loud.' },

    # --- boots ----------------------------------------------------------------
    @{ pack='rpg-boot-icons'; sub='without background'; file='1';  id='worn_boots';      name='Worn Leather Boots'; slot='feet'; def=5;  value=45;   req=1;  desc='Soft, quiet, and entirely without protection.' },
    @{ pack='rpg-boot-icons'; sub='without background'; file='8';  id='buckled_boots';   name='Buckled Boots';      slot='feet'; def=10; value=140;  req=5;  desc='Three buckles a side. Two of them still work.' },
    @{ pack='rpg-boot-icons'; sub='without background'; file='3';  id='plated_boots';    name='Plated Boots';       slot='feet'; def=18; value=340;  req=14; desc='Leather with a steel shell over the instep.' },
    @{ pack='rpg-boot-icons'; sub='without background'; file='29'; id='steel_sabatons';  name='Steel Sabatons';     slot='feet'; def=24; value=620;  req=22; desc='Full plate to the ankle. You will be heard coming.' },
    @{ pack='rpg-boot-icons'; sub='without background'; file='50'; id='gilded_sabatons'; name='Gilded Sabatons';    slot='feet'; def=32; value=1400; req=32; desc='Gold over steel. The guild master owns a pair he never wears.' },

    # --- mage robes: the odd-numbered files in the outfit pack -----------------
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='1'; id='acolyte_robe';    name="Acolyte's Robe";  slot='body'; def=6;  mag=10; value=120;  magreq=1;  desc='Guild-issue wool, dyed once and dyed badly.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='3'; id='tidecaller_robe'; name='Tidecaller Robe'; slot='body'; def=9;  mag=18; value=420;  magreq=10; desc='Layered silk that moves a moment after you do.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='7'; id='shaman_wrap';     name="Shaman's Wrap";   slot='body'; def=13; mag=24; value=760;  magreq=18; desc='Hide, fur and small bones, arranged in a deliberate order.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='9'; id='nightbound_robe'; name='Nightbound Robe'; slot='body'; def=16; mag=32; value=1350; magreq=28; desc='Black with a red lining, and it drinks the light.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='5'; id='lich_shroud';     name="Lich's Shroud";   slot='body'; def=20; mag=44; value=2600; magreq=40; desc='Whoever wore it last is, technically, still wearing it.' },

    # --- mage staves: the even-numbered files in the same pack -----------------
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='2';  id='emberwood_staff'; name='Emberwood Staff';  slot='weapon'; kind='staff'; mag=22; value=210;  magreq=1;  speed=0.95; desc='A warm amber stone set into scorched oak.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='10'; id='besom_stave';     name='Besom Stave';      slot='weapon'; kind='staff'; mag=30; value=460;  magreq=6;  speed=0.90; desc='It is a broom. It is also, unmistakably, a focus.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='4';  id='thornwood_staff'; name='Thornwood Staff';  slot='weapon'; kind='staff'; mag=40; value=980;  magreq=14; speed=0.95; desc='Still growing, slowly, in whichever direction you point it.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='8';  id='wildflower_stave';name='Wildflower Stave'; slot='weapon'; kind='staff'; mag=52; value=1700; magreq=22; speed=0.90; desc='Flowering out of season, which is the whole trick of it.' },
    @{ pack='game-icons-of-fantasy-mage-outfit-pack-7'; sub='PNG'; file='6';  id='bonecaller_staff';name='Bonecaller Staff'; slot='weapon'; kind='staff'; mag=70; value=3400; magreq=34; speed=1.00; desc='The skull is not decorative, and it is not anonymous.' },

    # --- daggers: quick melee, for when a sword is too much sword --------------
    @{ pack='game-icons-of-fantasy-daggers-pack-2'; sub='PNG'; file='daggers (1)'; id='iron_dirk';     name='Iron Dirk';     slot='weapon'; kind='melee'; att=14; str=10; value=60;   req=1;  speed=0.70; desc='Quick, cheap, and it goes where a sword cannot.' },
    @{ pack='game-icons-of-fantasy-daggers-pack-2'; sub='PNG'; file='daggers (4)'; id='curved_dagger'; name='Curved Dagger'; slot='weapon'; kind='melee'; att=24; str=18; value=280;  req=8;  speed=0.70; desc='The curve is not for show; it opens as it draws.' },
    @{ pack='game-icons-of-fantasy-daggers-pack-2'; sub='PNG'; file='daggers (6)'; id='verdant_kris';  name='Verdant Kris';  slot='weapon'; kind='melee'; att=36; str=28; value=820;  req=16; speed=0.75; desc='Green fire along the blade that never burns the hand.' },
    @{ pack='game-icons-of-fantasy-daggers-pack-2'; sub='PNG'; file='daggers (9)'; id='emberfang';     name='Emberfang';     slot='weapon'; kind='melee'; att=48; str=40; value=1900; req=26; speed=0.75; desc='Forged from something that came up out of Emberfell.' },
    @{ pack='game-icons-of-fantasy-daggers-pack-2'; sub='PNG'; file='daggers (2)'; id='riftblade';     name='Riftblade';     slot='weapon'; kind='melee'; att=62; str=52; value=4200; req=36; speed=0.80; desc='Cold to hold, and the edge is difficult to look at.' }
)

$armourItems = [ordered]@{}
$armourDrops = @()
$cut = 0
$missingPacks = [ordered]@{}

foreach ($piece in $catalogue) {
    $dir = Find-IconDir $piece.pack $piece.sub
    if (-not $dir) { $missingPacks[$piece.pack] = $true; continue }

    $src = Join-Path $dir "$($piece.file).png"
    if (-not (Test-Path $src)) {
        Write-Warning "  ! $($piece.id): no $($piece.file).png under $dir"
        continue
    }

    [void](Cut-Icon $src (Join-Path $armourDir "$($piece.id).png") 64 $true  $false)
    $aspect = Cut-Icon $src (Join-Path $wornDir  "$($piece.id).png") 24 $false $true
    $cut++

    $bonus = [ordered]@{}
    if ($piece.def) { $bonus["defence"]  = $piece.def }
    if ($piece.att) { $bonus["attack"]   = $piece.att }
    if ($piece.str) { $bonus["strength"] = $piece.str }
    if ($piece.mag) { $bonus["magic"]    = $piece.mag }

    $entry = [ordered]@{
        name  = $piece.name
        desc  = $piece.desc
        slot  = $piece.slot
        value = $piece.value
        bonus = $bonus
        icon  = "assets/icons/armour/$($piece.id).png"
    }
    if ($piece.kind)  { $entry["kind"]  = $piece.kind }
    if ($piece.speed) { $entry["speed"] = $piece.speed }

    # Armour gates on Defence, a blade on Attack, a stave on Magic, so the
    # requirement matches the skill the piece is actually for.
    $req = [ordered]@{}
    if ($piece.magreq -and $piece.magreq -gt 1)                              { $req["Magic"]   = $piece.magreq }
    elseif ($piece.slot -eq 'weapon' -and $piece.req -and $piece.req -gt 1)  { $req["Attack"]  = $piece.req }
    elseif ($piece.req -and $piece.req -gt 1)                                { $req["Defence"] = $piece.req }
    if ($req.Count -gt 0) { $entry["req"] = $req }

    # Every piece is worn from every angle. These are single painted views, so
    # in profile the character is wearing a front-on breastplate -- but at
    # twenty-odd pixels tall that does not read, whereas a character who strips
    # naked the moment they walk sideways very much does.
    $key = if ($piece.slot -eq 'weapon') { $piece.kind } else { $piece.slot }
    $a = $anchor[$key]
    $hpx = [math]::Min($a.h, $a.w / $aspect)
    $wpx = $hpx * $aspect
    $ypx = if ($a.Contains('bottom')) { $a.bottom - $hpx } else { $a.top }
    $entry["worn"] = [ordered]@{
        sprite  = "assets/icons/armour/worn/$($piece.id).png"
        after   = $(if ($piece.slot -eq 'weapon') { 'weapon_front' }
                    elseif ($piece.slot -eq 'head') { 'head' } else { 'body' })
        rect    = @([math]::Round($a.cx - $wpx / 2, 2), [math]::Round($ypx, 2),
                    [math]::Round($wpx, 2), [math]::Round($hpx, 2))
        facings = @($true, $true, $true, $true)
    }

    $armourItems[$piece.id] = $entry

    # How often a piece turns up, in inverse proportion to what it is worth.
    # One rule beats thirty hand-tuned weights, and it keeps itself honest when
    # a piece is repriced.
    $armourDrops += [ordered]@{
        item   = $piece.id
        weight = [math]::Max(1, [int][math]::Round(4000.0 / $piece.value))
    }
}

foreach ($p in $missingPacks.Keys) {
    Write-Host "  - $p not downloaded yet" -ForegroundColor DarkGray
}

# The loot table is written either way. data/loot_tables.json chains to
# "armour_cache" from the dungeon chests, and a chain to a table that does not
# exist is the kind of thing that is only noticed when a chest is opened -- so
# when the packs are absent the table is still there, and simply empty.
$lootPath = Join-Path $root "data\loot_tables_armour.json"
$lootTable = [ordered]@{
    armour_cache = [ordered]@{
        rolls = $(if ($armourDrops.Count -gt 0) { 1 } else { 0 })
        table = @($armourDrops)
    }
}
[IO.File]::WriteAllText($lootPath, ($lootTable | ConvertTo-Json -Depth 6) + "`n")

if ($armourItems.Count -eq 0) {
    Write-Host "  (no icon packs found - see docs/ASSETS.md)" -ForegroundColor DarkGray
    # Nothing to add, and the game treats a missing file as normal.
    Remove-Item (Join-Path $root "data\items_armour.json") -Force -ErrorAction SilentlyContinue
} else {
    # Written to its own file so data/items.json never refers to art that may
    # not be installed; both the game and the self-test treat it as optional.
    [IO.File]::WriteAllText((Join-Path $root "data\items_armour.json"),
                            ($armourItems | ConvertTo-Json -Depth 6) + "`n")
    Write-Host ("  {0} icons cut, {1} items written to data/items_armour.json" -f
                $cut, $armourItems.Count)
}

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

# --- 8. generated art -----------------------------------------------------------
# Some of assets/ is not cut from any pack but generated. The ground tiles
# replace the flat swatches cut in step 3 above, and the icons fill the gaps in
# the CraftPix icon sheet, so both have to run after the cutting or a fresh
# import brings back flat grass and a bow drawn as a sword. Neither needs
# anything installed. The Blender props and the player character do, and are
# rebuilt separately with make_props.ps1, make_character.ps1 and make_tiers.ps1.
Write-Host "`nGenerating ground tiles, item icons and HUD fittings ..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "make_ground.ps1")
& (Join-Path $PSScriptRoot "make_icons.ps1")
& (Join-Path $PSScriptRoot "make_ui.ps1")

# --- done ---------------------------------------------------------------------
$total = (Get-ChildItem $assets -Recurse -File -Filter *.png |
          Where-Object { $_.FullName -notlike "*\_raw\*" }).Count
Write-Host "`nImport complete: $total images under assets/" -ForegroundColor Green
Write-Host "assets/_raw/ holds the unpacked archives and can be deleted." -ForegroundColor DarkGray
