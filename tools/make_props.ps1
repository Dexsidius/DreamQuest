# =============================================================================
#  make_props.ps1 - turns the Blender renders into pixel art the game can use.
#
#      .\tools\make_props.ps1              # render then convert
#      .\tools\make_props.ps1 -SkipRender  # convert what is already rendered
#
#  tools/blender_props.py renders each prop eight times larger than it needs to
#  be. Three things happen here, in this order, and the order matters:
#
#    1. Box downsample. Averaging an 8x8 block is what turns a smooth render
#       into a small image without the stair-stepping a nearest-neighbour
#       reduction would leave on every curve.
#    2. Flatten. Alpha is cut to on-or-off and colour is stepped to a coarse
#       ladder. A render has hundreds of shades across one wooden plank; pixel
#       art has three, and keeping the render's gradient is what makes 3D
#       output look like 3D output next to hand-drawn tiles.
#    3. Outline. A dark rim around the silhouette. This is the single biggest
#       thing separating the two looks -- every CraftPix prop in this project
#       has one, and without it a rendered prop sits on the grass looking
#       washed out no matter how good the model is.
# =============================================================================

param(
    [switch]$SkipRender,
    # The second family: the scenery that stands on the ground rather than in a
    # room -- trees, rocks, bushes, fungus. Same pipeline, written to
    # assets/objects/ because that is where the maps look for it.
    [switch]$Objects,
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root    = Split-Path $PSScriptRoot -Parent
$renders = Join-Path $root "assets\_render"
$outDir  = Join-Path $root ($(if ($Objects) { "assets\objects" } else { "assets\props" }))

# How many pixels across each prop ends up. blender_props.py renders at eight
# times these, and the two lists have to agree; the check below says so out
# loud rather than silently producing something the wrong size.
$sizes = @{
    table_long  = 96; table_round = 64; bench     = 72; chair       = 40
    bookshelf   = 72; hearth      = 80; barrel    = 40; strongbox   = 44
    weapon_rack = 72; rug         = 88; banner    = 72; candlestand = 64
    lectern     = 56; signpost    = 56
    # The forge.
    forge        = 96; anvil        = 48; bellows      = 56; quench_trough = 56
    grindstone   = 48; tool_rack    = 64; coal_bin     = 44; ingot_crate   = 44
    armour_stand = 64; weapon_barrel = 48; shop_counter = 96
    # The inn, downstairs.
    bar_counter  = 112; bar_stool   = 32; keg_rack    = 72; bottle_shelf = 72
    tavern_table = 56;  tavern_bench = 56; tavern_chair = 40; inn_fireplace = 104
    stairs_up    = 88;  crates_sacks = 48; chalk_board = 44; workbench   = 80
    inn_rug      = 88
    # The inn upstairs, and Maren's cottage.
    bed_single = 64; bed_double = 72; wardrobe = 64; nightstand = 32
    washstand = 40; travel_chest = 40; stairwell_down = 88; room_door = 56
    cottage_hearth = 80; spinning_wheel = 56; writing_desk = 64
    cottage_bookshelf = 64; dining_table = 56; herb_pots = 36
    # Buildings.
    inn_building = 192
    # Mossvale and the Whisperwood.
    mossvale_lodge = 192
    herbalist_cottage = 168
    well = 56; well_dry = 56; market_stall = 80; palisade = 64; log_pile = 48; tent = 72; campfire_ring = 48
    town_gate = 160

    # Herbs for Foraging, growing and picked, and the brewing cauldron.
    cauldron = 48
    # Where charms are worked into worn pieces.
    enchanting_table = 64
    # The mage college at Fernhollow, and the circle on its floor.
    mage_college = 176; spell_circle = 96
    # The Emberfell mine's way in, on the overworld.
    mine_adit = 256
    # The swamp, the Ice Spire, the Ashen Path and the inn's cellar.
    reeds = 48; lily_pads = 40; swamp_tree = 72; lizard_hut = 128; lizard_totem = 56; ice_spire = 112; ice_crystal = 40; snow_pine = 80; wyvern_nest = 72; charred_tree = 72; obsidian_rock = 40; hellgate = 144; cellar_hatch = 48; cobweb = 40
    barrow_mound = 208; dungeon_stairs_up = 96; dungeon_stairs_down = 80
    sawmill = 144; ore_cart = 96; rowboat = 128
    gravestone = 48; gravestone_cross = 52; grave_mound = 64; grave_fence = 72
    spring_basin = 112
    lych_gate = 144; crypt = 176
    herb_marigold = 40; herb_marigold_picked = 40
    herb_brookmint = 40; herb_brookmint_picked = 40
    herb_nettle = 40; herb_nettle_picked = 40
    herb_bogbean = 40; herb_bogbean_picked = 40
    herb_mountain_sage = 40; herb_mountain_sage_picked = 40
    herb_glowcap = 40; herb_glowcap_picked = 40
    herb_emberbloom = 40; herb_emberbloom_picked = 40
    herb_moonpetal = 40; herb_moonpetal_picked = 40
    herb_starlily = 40; herb_starlily_picked = 40
    herb_flax = 40; herb_flax_picked = 40
    # The Westwold's steadings and the Brackenwood's den.
    tanning_rack = 64; hay_rick = 56; rail_fence = 64; bear_den = 144
}

# The scenery sizes, built to the same names the maps already use. Ten trees and
# ten saplings, eight rocks and eight small ones, the same for bushes, six
# mushrooms and three patches of fungus.
if ($Objects) {
    $sizes = @{}
    foreach ($i in 0..9) {
        $sizes["tree_" + $i.ToString("d2")] = 128
        $sizes["treesmall_" + $i.ToString("d2")] = 64
    }
    foreach ($i in 0..7) {
        $sizes["rock_" + $i.ToString("d2")] = 64
        $sizes["rocksmall_" + $i.ToString("d2")] = 32
        $sizes["bush_" + $i.ToString("d2")] = 64
        $sizes["bushsmall_" + $i.ToString("d2")] = 32
    }
    $mush = @(128, 64, 32, 128, 64, 64)
    foreach ($i in 0..5) { $sizes["mushroom_" + $i.ToString("d2")] = $mush[$i] }
    foreach ($i in 0..2) { $sizes["fungus_" + $i.ToString("d2")] = 32 }
    # What a felled tree leaves, at each size of tree.
    $sizes["stump"] = 48
    $sizes["stumpsmall"] = 32

    # Buildings, the guild hall's furniture, and the small things that stand on
    # the ground: the rest of what used to come out of the packs.
    $sizes += @{
        building_house_a = 144; building_house_b = 160; building_shop = 128
        building_guild   = 160; sign_guild       = 72
        chest = 32; chest_open = 32; door = 32; door_open = 32
        campfire = 48; arrow = 24
        guild_noticeboard = 48; guild_couch = 56; guild_bench = 48
        guild_settle = 48; guild_chair = 24; guild_table = 56
        guild_desk = 56; guild_cabinet = 56; guild_bookshelf = 56
        guild_bookshelf_b = 56; guild_chest = 32; guild_rug = 72
        guild_banner = 48; guild_weapon_rack = 48; guild_armour_rack = 56
        guild_rack = 48; guild_plant = 36; guild_door = 40
    }
}

# Every name the scenery family owns, so a run of one family stays quiet about
# renders belonging to the other.
$SCENERY_NAMES = New-Object System.Collections.Generic.HashSet[string]
foreach ($i in 0..9) {
    [void]$SCENERY_NAMES.Add("tree_" + $i.ToString("d2"))
    [void]$SCENERY_NAMES.Add("treesmall_" + $i.ToString("d2"))
}
foreach ($i in 0..7) {
    foreach ($fam in "rock", "rocksmall", "bush", "bushsmall") {
        [void]$SCENERY_NAMES.Add($fam + "_" + $i.ToString("d2"))
    }
}
foreach ($i in 0..5) { [void]$SCENERY_NAMES.Add("mushroom_" + $i.ToString("d2")) }
foreach ($i in 0..2) { [void]$SCENERY_NAMES.Add("fungus_" + $i.ToString("d2")) }
[void]$SCENERY_NAMES.Add("stump")
[void]$SCENERY_NAMES.Add("stumpsmall")
foreach ($n in "building_house_a", "building_house_b", "building_shop", "building_guild",
               "sign_guild", "chest", "chest_open", "door", "door_open", "campfire", "arrow",
               "guild_noticeboard", "guild_couch", "guild_bench", "guild_settle", "guild_chair",
               "guild_table", "guild_desk", "guild_cabinet", "guild_bookshelf",
               "guild_bookshelf_b", "guild_chest", "guild_rug", "guild_banner",
               "guild_weapon_rack", "guild_armour_rack", "guild_rack", "guild_plant",
               "guild_door") {
    [void]$SCENERY_NAMES.Add($n)
}

if (-not $SkipRender) {
    if (-not (Test-Path $Blender)) {
        throw "Blender not found at $Blender. Pass -Blender with the right path, or -SkipRender to convert existing renders."
    }
    Write-Host "Rendering props in Blender ..." -ForegroundColor Cyan
    $args = @("--background", "--python", (Join-Path $PSScriptRoot "blender_props.py"))
    if ($Objects -or $Only.Count -gt 0) {
        $args += @("--")
        if ($Objects) { $args += "--objects" }
        $args += $Only
    }
    # Blender chats on stderr, and Windows PowerShell turns each of those lines
    # into an error record that "Stop" treats as fatal. Only the exit code says
    # whether the render actually failed.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Blender @args | Where-Object { $_ -match "^rendered " } | Write-Host
    } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# --- pixel buffer helpers ----------------------------------------------------
# System.Drawing rather than anything installed separately, to match the rest of
# tools/. Everything works on a flat byte array in BGRA order.

function Read-Pixels($bmp) {
    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
        [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        return @{ bytes = $bytes; stride = $data.Stride; w = $bmp.Width; h = $bmp.Height }
    } finally { $bmp.UnlockBits($data) }
}

function Write-Pixels($buf, $path) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $buf.w, $buf.h,
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $buf.w, $buf.h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        # The freshly locked bitmap may have a different stride to our buffer,
        # so copy a row at a time rather than trusting them to match.
        $row = New-Object byte[] ($buf.w * 4)
        for ($y = 0; $y -lt $buf.h; $y++) {
            [Array]::Copy($buf.bytes, $y * $buf.stride, $row, 0, $buf.w * 4)
            [Runtime.InteropServices.Marshal]::Copy(
                $row, 0, [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $row.Length)
        }
    } finally { $bmp.UnlockBits($data) }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

function New-Buffer($w, $h) {
    return @{ bytes = (New-Object byte[] ($w * 4 * $h)); stride = $w * 4; w = $w; h = $h }
}

# --- 1. box downsample -------------------------------------------------------
# Alpha-weighted, so the colour of a mostly-transparent edge block comes from
# the pixels that were actually opaque rather than being dragged toward black
# by the empty ones.
function Resize-Box($src, $size) {
    $factor = [int]($src.w / $size)
    $dst = New-Buffer $size $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $b = 0.0; $g = 0.0; $r = 0.0; $a = 0.0; $wsum = 0.0
            for ($sy = 0; $sy -lt $factor; $sy++) {
                $rowBase = ($y * $factor + $sy) * $src.stride
                for ($sx = 0; $sx -lt $factor; $sx++) {
                    $i = $rowBase + ($x * $factor + $sx) * 4
                    $sa = [double]$src.bytes[$i + 3]
                    $a += $sa
                    if ($sa -gt 0) {
                        $b += [double]$src.bytes[$i]     * $sa
                        $g += [double]$src.bytes[$i + 1] * $sa
                        $r += [double]$src.bytes[$i + 2] * $sa
                        $wsum += $sa
                    }
                }
            }
            $n = [double]($factor * $factor)
            $o = $y * $dst.stride + $x * 4
            if ($wsum -gt 0) {
                $dst.bytes[$o]     = [byte][math]::Min(255, [int]($b / $wsum))
                $dst.bytes[$o + 1] = [byte][math]::Min(255, [int]($g / $wsum))
                $dst.bytes[$o + 2] = [byte][math]::Min(255, [int]($r / $wsum))
            }
            $dst.bytes[$o + 3] = [byte][math]::Min(255, [int]($a / $n))
        }
    }
    return $dst
}

# --- 2. flatten --------------------------------------------------------------
function Flatten($buf, $levels = 10) {
    $step = 255.0 / ($levels - 1)
    for ($y = 0; $y -lt $buf.h; $y++) {
        for ($x = 0; $x -lt $buf.w; $x++) {
            $i = $y * $buf.stride + $x * 4
            if ($buf.bytes[$i + 3] -lt 128) {
                $buf.bytes[$i] = 0; $buf.bytes[$i + 1] = 0
                $buf.bytes[$i + 2] = 0; $buf.bytes[$i + 3] = 0
                continue
            }
            $buf.bytes[$i + 3] = 255
            for ($c = 0; $c -lt 3; $c++) {
                $v = [double]$buf.bytes[$i + $c]
                $buf.bytes[$i + $c] = [byte][math]::Min(255,
                    [int]([math]::Round($v / $step) * $step))
            }
        }
    }
}

# --- 3. outline --------------------------------------------------------------
# Grown inwards rather than outwards: an outward rim would change the prop's
# footprint, and these are placed on maps by size.
function Add-Outline($buf, $darken = 0.62) {
    $orig = $buf.bytes.Clone()
    $opaque = { param($x, $y)
        if ($x -lt 0 -or $y -lt 0 -or $x -ge $buf.w -or $y -ge $buf.h) { return $false }
        return $orig[$y * $buf.stride + $x * 4 + 3] -ge 250
    }
    for ($y = 0; $y -lt $buf.h; $y++) {
        for ($x = 0; $x -lt $buf.w; $x++) {
            $i = $y * $buf.stride + $x * 4
            if ($orig[$i + 3] -lt 250) { continue }   # shadow, not the prop
            # An edge pixel is an opaque one with a transparent neighbour.
            if ((& $opaque ($x - 1) $y) -and (& $opaque ($x + 1) $y) -and
                (& $opaque $x ($y - 1)) -and (& $opaque $x ($y + 1))) { continue }
            for ($c = 0; $c -lt 3; $c++) {
                $buf.bytes[$i + $c] = [byte][int]([double]$buf.bytes[$i + $c] * $darken)
            }
        }
    }
}

# --- 4. contact shadow -------------------------------------------------------
# Drawn here rather than rendered. Blender's shadow catcher puts shadow density
# into alpha, which leaves a dense shadow indistinguishable from the prop
# itself; here the silhouette is known exactly, so the two can never be
# confused.
#
# The shape is taken from the prop rather than being a fixed ellipse: for each
# column, the shadow reaches from the lowest solid pixel in that column and
# fades over a few rows. A bookshelf therefore casts a bookshelf-shaped pool
# and a barrel a round one, without either being described anywhere.
function Add-ContactShadow($buf, $depth = 3, $alpha = 92) {
    $solid = $buf.bytes.Clone()
    $isSolid = { param($x, $y)
        if ($x -lt 0 -or $y -lt 0 -or $x -ge $buf.w -or $y -ge $buf.h) { return $false }
        return $solid[$y * $buf.stride + $x * 4 + 3] -ge 250
    }

    for ($x = 0; $x -lt $buf.w; $x++) {
        # Lowest solid pixel in this column is where the prop meets the floor.
        $base = -1
        for ($y = $buf.h - 1; $y -ge 0; $y--) {
            if (& $isSolid $x $y) { $base = $y; break }
        }
        if ($base -lt 0) { continue }

        # Offset a little to the lower right, away from the key light.
        for ($d = 1; $d -le $depth; $d++) {
            $sy = $base + $d
            $sx = $x + [int][math]::Floor($d / 2)
            if ($sy -ge $buf.h -or $sx -ge $buf.w) { continue }
            $i = $sy * $buf.stride + $sx * 4
            if ($solid[$i + 3] -ge 250) { continue }      # never over the prop

            $fade = [int]($alpha * (1.0 - ($d - 1) / [double]$depth))
            if ($buf.bytes[$i + 3] -ge $fade) { continue }
            $buf.bytes[$i]     = 44      # B
            $buf.bytes[$i + 1] = 36      # G
            $buf.bytes[$i + 2] = 30      # R
            $buf.bytes[$i + 3] = [byte]$fade
        }
    }
}

# --- 5. sit it on the floor --------------------------------------------------
# Props are placed by the bottom edge of their image: that line is where the
# object meets the ground, and it is what the renderer sorts against the player.
# A render framed with empty space under the object puts that line in mid-air --
# the forge stood twenty-three pixels above where it was placed, and a player
# could walk "behind" it while visibly in front. So the drawn pixels (shadow
# included) are moved down until they touch the bottom, keeping the square.
function Set-OnFloor($buf, $name) {
    $top = -1; $bottom = -1
    for ($y = 0; $y -lt $buf.h -and $top -lt 0; $y++) {
        for ($x = 0; $x -lt $buf.w; $x++) {
            if ($buf.bytes[$y * $buf.stride + $x * 4 + 3] -gt 0) { $top = $y; break }
        }
    }
    for ($y = $buf.h - 1; $y -ge 0 -and $bottom -lt 0; $y--) {
        for ($x = 0; $x -lt $buf.w; $x++) {
            if ($buf.bytes[$y * $buf.stride + $x * 4 + 3] -gt 0) { $bottom = $y; break }
        }
    }
    if ($top -lt 0) { return }
    if ($top -eq 0) {
        Write-Warning "  ! $name touches the top of its frame and may be cut off"
    }
    $shift = ($buf.h - 1) - $bottom
    if ($shift -le 0) { return }

    $moved = New-Object byte[] $buf.bytes.Length
    for ($y = $buf.h - 1; $y -ge $shift; $y--) {
        [Array]::Copy($buf.bytes, ($y - $shift) * $buf.stride, $moved, $y * $buf.stride, $buf.w * 4)
    }
    $buf.bytes = $moved
}

# --- run ---------------------------------------------------------------------
$done = 0
foreach ($file in (Get-ChildItem $renders -Filter *.png -File -EA SilentlyContinue)) {
    $name = $file.BaseName
    if ($Only.Count -gt 0 -and $Only -notcontains $name) { continue }
    if (-not $sizes.ContainsKey($name)) {
        # assets/_render holds both families; a name from the other one is not
        # a mistake, it is simply not this run's business.
        if (-not $Objects -and -not $SCENERY_NAMES.Contains($name)) {
            Write-Warning "  ! $name has no target size in make_props.ps1; skipped"
        }
        continue
    }

    $size = $sizes[$name]
    $bmp = [System.Drawing.Bitmap]::FromFile($file.FullName)
    try {
        if ($bmp.Width % $size -ne 0) {
            Write-Warning ("  ! {0}: render is {1}px, which is not a whole multiple of {2}" -f
                           $name, $bmp.Width, $size)
            continue
        }
        $buf = Resize-Box (Read-Pixels $bmp) $size
    } finally { $bmp.Dispose() }

    Flatten $buf
    Add-Outline $buf
    # After the outline, so the shadow is not itself outlined.
    Add-ContactShadow $buf
    Set-OnFloor $buf $name
    Write-Pixels $buf (Join-Path $outDir "$name.png")
    Write-Host ("  {0,-14} {1}x{1}" -f $name, $size)
    $done++
}

Write-Host ("`n{0} images written to {1}" -f $done,
            $(if ($Objects) { "assets/objects/" } else { "assets/props/" })) -ForegroundColor Green
