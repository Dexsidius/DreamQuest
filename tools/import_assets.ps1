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

# The player is the only character who equips anything, so only the player
# sheets are imported as separate layers. The packs ship them already split and
# frame-aligned -- shadow, sword behind the body, body, head, sword in front --
# with a number in each filename giving the draw order. Keeping that order is
# what makes a paperdoll possible: the weapon layers can be hidden or recoloured
# independently of the body, and the body and head can be tinted for armour.
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

foreach ($who in @(
    @{ id = "player_male";   pack = "base-4-direction-male-character-pixel-art" },
    @{ id = "player_female"; pack = "base-4-direction-female-character-pixel-art" }
)) {
    Copy-Layers $who.id (Join-Path (Pack $who.pack) "PNG\Sword\Parts") @{
        idle   = "Sword_Idle"
        walk   = "Sword_Walk"
        run    = "Sword_Run"
        attack = "Sword_attack"
        hurt   = "Sword_Hurt"
        death  = "Sword_Death"
    }
}

# The female pack ships no shadow layer. Both characters use the same rig with
# identical frame counts and poses, so the male shadow lines up exactly; without
# this one character would cast a shadow and the other would not.
$maleLayers   = Join-Path $assets "characters\player_male\layers"
$femaleLayers = Join-Path $assets "characters\player_female\layers"
if ((Test-Path $maleLayers) -and (Test-Path $femaleLayers)) {
    $borrowed = 0
    foreach ($shadow in (Get-ChildItem $maleLayers -Filter "*_shadow.png" -File)) {
        $target = Join-Path $femaleLayers $shadow.Name
        if (-not (Test-Path $target)) {
            Copy-Item $shadow.FullName $target -Force
            $borrowed++
        }
    }
    if ($borrowed -gt 0) {
        Write-Host ("  {0,-16} {1} shadow layers borrowed from player_male" -f
                    "player_female", $borrowed)
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

# --- 6c. armour icons ---------------------------------------------------------
# The fantasy-knight and RPG-boot packs are 512x512 inventory icons, not sprite
# layers. Two sizes come out of each: one for the inventory panel, and a much
# smaller one for wearing on the character. Pre-scaling the worn version here
# matters -- a 512 to 16 reduction done properly at import looks far better
# than letting the renderer do it every frame.
Write-Host "`nCutting armour icons ..." -ForegroundColor Cyan
$armourDir = Join-Path $assets "icons\armour"
$wornDir   = Join-Path $armourDir "worn"
New-Dir $armourDir
New-Dir $wornDir

# $pixelate hardens the result for art that will sit next to pixel art. The
# packs are painted, anti-aliased icons; shrunk to character size with a smooth
# filter they read as a soft blob against a 16px-grid sprite. Cutting the alpha
# to on-or-off and stepping the colours back gives them a defined edge and a
# flatter palette, which reads far better on the character. The inventory
# version is left smooth, because at 64px the original art looks best as drawn.
function Resize-Icon($src, $dest, $size, $pixelate = $false) {
    $img = [System.Drawing.Bitmap]::FromFile($src)
    try {
        $out = New-Object System.Drawing.Bitmap $size, $size
        $g = [System.Drawing.Graphics]::FromImage($out)
        try {
            $g.InterpolationMode  = 'HighQualityBicubic'
            $g.PixelOffsetMode    = 'HighQuality'
            $g.CompositingQuality = 'HighQuality'
            $g.Clear([System.Drawing.Color]::Transparent)
            # Fit inside the square, keeping the icon's proportions.
            $scale = [math]::Min($size / $img.Width, $size / $img.Height)
            $w = [int]($img.Width * $scale)
            $h = [int]($img.Height * $scale)
            $g.DrawImage($img, [int](($size - $w) / 2), [int](($size - $h) / 2), $w, $h)
        } finally { $g.Dispose() }

        if ($pixelate) {
            $data = $out.LockBits(
                (New-Object System.Drawing.Rectangle 0, 0, $size, $size),
                [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $bytes = New-Object byte[] ($data.Stride * $size)
                [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
                for ($i = 0; $i -lt $bytes.Length; $i += 4) {
                    if ($bytes[$i + 3] -lt 110) {
                        # Below the threshold it is background, not a soft edge.
                        $bytes[$i] = 0; $bytes[$i + 1] = 0; $bytes[$i + 2] = 0; $bytes[$i + 3] = 0
                    } else {
                        $bytes[$i + 3] = 255
                        # Step each channel to one of eight levels.
                        for ($c = 0; $c -lt 3; $c++) {
                            $v = [int]$bytes[$i + $c]
                            $bytes[$i + $c] = [byte]([math]::Min(255, [int]([math]::Round($v / 32.0) * 32)))
                        }
                    }
                }
                [Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
            } finally { $out.UnlockBits($data) }
        }

        $out.Save($dest, [System.Drawing.Imaging.ImageFormat]::Png)
        $out.Dispose()
    } finally { $img.Dispose() }
}

function Import-ArmourPack($packName, $prefix, $limit) {
    $dir = Pack $packName
    if (-not (Test-Path $dir)) {
        Write-Host "  - $packName not downloaded yet" -ForegroundColor DarkGray
        return 0
    }
    $files = Get-ChildItem $dir -Recurse -File -Filter *.png |
             Where-Object { $_.FullName -notmatch '__MACOSX|COUPON|preview|Preview' } |
             Sort-Object Name | Select-Object -First $limit

    $i = 0
    foreach ($f in $files) {
        $name = "{0}_{1:d2}" -f $prefix, $i
        Resize-Icon $f.FullName (Join-Path $armourDir "$name.png") 64
        Resize-Icon $f.FullName (Join-Path $wornDir  "$name.png") 24 $true
        $i++
    }
    Write-Host ("  {0,-14} {1} icons" -f $prefix, $i)
    return $i
}

$armourCount  = Import-ArmourPack "game-icons-of-fantasy-knight-armor-pack-11" "knight" 10
$armourCount += Import-ArmourPack "rpg-boot-icons" "boot" 12

if ($armourCount -eq 0) {
    Write-Host "  (no armour icon packs found - see docs/ASSETS.md)" -ForegroundColor DarkGray
    # Nothing to add, and the game treats a missing file as normal.
    Remove-Item (Join-Path $root "data\items_armour.json") -Force -ErrorAction SilentlyContinue
} else {
    # Write the armour items that use these icons. This lives in its own file
    # so data/items.json never refers to art that may not be installed.
    #
    # The worn rectangles are in frame pixels, measured against the rig: the
    # head sits at (25,22) and is 13x13 inside the 64px frame, the torso runs
    # from about y=34 to y=48, and the legs below that. A single painted icon
    # only reads from the front and back, so the side facings are left off.
    Write-Host "  writing data/items_armour.json" -ForegroundColor Cyan

    function Icon($name) {
        if (Test-Path (Join-Path $armourDir "$name.png")) { return "assets/icons/armour/$name.png" }
        return $null
    }
    function Worn($name) {
        if (Test-Path (Join-Path $wornDir "$name.png")) { return "assets/icons/armour/worn/$name.png" }
        return $null
    }

    # The knight pack ships in a fixed order: helmets, then body armour, then
    # greaves, then boots, each as a simple and an improved version.
    $pieces = @(
        @{ id='knight_helm';        name='Knight Helm';         icon='knight_00'; slot='head';  def=20; value=320;  req=10; rect=@(24,19,15,15); facings=@($true,$false,$false,$true) },
        @{ id='knight_helm_fine';   name='Fine Knight Helm';    icon='knight_01'; slot='head';  def=30; value=760;  req=20; rect=@(24,18,16,16); facings=@($true,$false,$false,$true) },
        @{ id='knight_cuirass';     name='Knight Cuirass';      icon='knight_02'; slot='body';  def=34; value=560;  req=15; rect=@(24,33,16,16); facings=@($true,$false,$false,$true) },
        @{ id='knight_cuirass_fine';name='Fine Knight Cuirass'; icon='knight_03'; slot='body';  def=46; value=1180; req=25; rect=@(23,32,18,18); facings=@($true,$false,$false,$true) },
        @{ id='knight_greaves';     name='Knight Greaves';      icon='knight_04'; slot='legs';  def=22; value=380;  req=15; rect=@(25,42,14,14); facings=@($true,$false,$false,$true) },
        @{ id='knight_greaves_fine';name='Fine Knight Greaves'; icon='knight_05'; slot='legs';  def=30; value=820;  req=25; rect=@(24,41,16,16); facings=@($true,$false,$false,$true) }
    )
    $bootPieces = @(
        @{ id='leather_boots'; name='Leather Boots'; icon='boot_00'; slot='legs'; def=8;  value=70;  req=1;  rect=@(25,47,14,12); facings=@($true,$false,$false,$true) },
        @{ id='mail_sabatons'; name='Mail Sabatons'; icon='boot_01'; slot='legs'; def=16; value=240; req=10; rect=@(25,47,14,12); facings=@($true,$false,$false,$true) }
    )

    $armourItems = [ordered]@{}
    foreach ($piece in ($pieces + $bootPieces)) {
        $iconPath = Icon $piece.icon
        if (-not $iconPath) { continue }

        $entry = [ordered]@{
            name  = $piece.name
            desc  = "Plate from the guild armoury. Heavy, and worth the weight."
            slot  = $piece.slot
            value = $piece.value
            bonus = @{ defence = $piece.def }
            icon  = $iconPath
        }
        if ($piece.req -gt 1) { $entry["req"] = @{ Defence = $piece.req } }

        $wornPath = Worn $piece.icon
        if ($wornPath) {
            $entry["worn"] = [ordered]@{
                sprite  = $wornPath
                after   = $(if ($piece.slot -eq 'head') { 'head' } else { 'body' })
                rect    = $piece.rect
                facings = $piece.facings
            }
        }
        $armourItems[$piece.id] = $entry
    }

    [IO.File]::WriteAllText((Join-Path $root "data\items_armour.json"),
                            ($armourItems | ConvertTo-Json -Depth 6) + "`n")
    Write-Host ("  {0} armour items" -f $armourItems.Count)
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

# --- done ---------------------------------------------------------------------
$total = (Get-ChildItem $assets -Recurse -File -Filter *.png |
          Where-Object { $_.FullName -notlike "*\_raw\*" }).Count
Write-Host "`nImport complete: $total images under assets/" -ForegroundColor Green
Write-Host "assets/_raw/ holds the unpacked archives and can be deleted." -ForegroundColor DarkGray
