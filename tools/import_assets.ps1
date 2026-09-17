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

# --- 2. everything the game now makes for itself ------------------------------
# What used to sit here -- the character sheets, the ground tiles, the buildings
# and guild interior cut out of packed sheets, the trees, rocks and bushes, the
# UI and the item icons -- is all generated now, by the tools listed at the end
# of this script and in docs/ASSETS.md. Importing them would overwrite the
# game's own art with a pack's, so the steps are gone rather than skipped.
#
# What is left below is the one thing that still comes out of a pack, and it is
# optional: the painted equipment icons.


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

# --- 7. no font is bundled ----------------------------------------------------
# A font used to be copied out of the system's font folder so every machine
# rendered text identically. That file is not ours to pass on, and src/ui/ui.cpp
# already falls back through Consolas, Segoe, Arial and DejaVu, so the game
# looks the same on any machine that has any of them. Drop a .ttf at
# assets/fonts/dreamquest.ttf to override it.


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
