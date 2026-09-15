# =============================================================================
#  make_decals.ps1 - generates the small things lying on the overworld's ground.
#
#      .\tools\make_decals.ps1
#
#  The overworld used to be scattered with shapes cut from the CraftPix road
#  pack's "Ground_grass" sheet: round blobs, squares with a hole in them,
#  chevrons and keyholes, each one flat colour. They were never decorations.
#  They are that sheet's stencils -- the masks its autotiles use to blend grass
#  into a path -- and dropped loose on a field they read as stains and cut-outs.
#
#  So the decals are drawn here instead, as the things a field actually has
#  lying in it, on transparent ground so only the thing itself shows:
#
#    tuft_*       a clump of meadow grass          the meadow
#    tuft_dark_*  a clump of shade grass           the greenwood
#    flowers_*    wildflowers in a little tuft     meadow and greenwood
#    leaves_*     fallen leaves                    the greenwood
#    pebbles_*    a few small stones               everywhere dry
#    dry_tuft_*   straw-coloured hill grass        the foothills
#    stone_*      one flat stone, cracked          the foothills
#    crack_*      a fissure in scorched ground     the Cursed Reach
#    sedge_*      a clump of sedge                 the Mire
#    puddle_*     a muddy puddle                   the Mire
#
#  genmaps.cpp finds them by name in data/asset_manifest.json, so run
#  tools/make_manifest.ps1 after this, then rebuild the maps.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root  = Split-Path $PSScriptRoot -Parent
$decor = Join-Path $root "assets\decor"
New-Item -ItemType Directory -Force -Path $decor | Out-Null

# Its own deterministic sequence, as in make_ground.ps1, so a rerun writes the
# same pixels.
$script:seed = 20260916
function Rand {
    $script:seed = [int](($script:seed * 1664525 + 1013904223) -band 0x7FFFFFFF)
    return $script:seed / 2147483647.0
}
function RandInt($n) { return [int]([math]::Floor((Rand) * $n)) % $n }

function Rgb($r, $g, $b, $a = 255) { return [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }

function Shade($c, $amount) {
    $f = [math]::Min(1.0, [math]::Abs($amount))
    if ($amount -ge 0) {
        return [System.Drawing.Color]::FromArgb($c.A,
            [int]($c.R + (255 - $c.R) * $f), [int]($c.G + (255 - $c.G) * $f), [int]($c.B + (255 - $c.B) * $f))
    }
    return [System.Drawing.Color]::FromArgb($c.A, [int]($c.R * (1 - $f)), [int]($c.G * (1 - $f)), [int]($c.B * (1 - $f)))
}

function New-Decal($w, $h) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::Transparent) } }
    return $bmp
}

function Put($bmp, $x, $y, $c) {
    $x = [int]$x; $y = [int]$y
    if ($x -lt 0 -or $y -lt 0 -or $x -ge $bmp.Width -or $y -ge $bmp.Height) { return }
    $bmp.SetPixel($x, $y, $c)
}

# A pixel of shadow, only where nothing has been drawn yet.
function Shadow($bmp, $x, $y, $alpha = 70) {
    $x = [int]$x; $y = [int]$y
    if ($x -lt 0 -or $y -lt 0 -or $x -ge $bmp.Width -or $y -ge $bmp.Height) { return }
    if ($bmp.GetPixel($x, $y).A -eq 0) { $bmp.SetPixel($x, $y, (Rgb 20 28 12 $alpha)) }
}

function Save($bmp, $name) {
    $bmp.Save((Join-Path $decor "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $script:made++
}

# A clump of blades rising from a base line: darkest at the root, lightest at
# the tip, the middle blades tallest, each leaning a little.
function Add-Tuft($bmp, $cx, $baseY, $base, $count, $maxH) {
    for ($dx = -[int]($count * 0.7) - 1; $dx -le [int]($count * 0.7) + 1; $dx++) { Shadow $bmp ($cx + $dx) ($baseY + 1) }
    for ($i = 0; $i -lt $count; $i++) {
        $off = $i - ($count - 1) / 2.0
        $x0 = $cx + [math]::Round($off * 1.25) + (RandInt 2) - 0
        $centre = 1.0 - [math]::Abs($off) / ($count / 2.0 + 0.5)
        $h = [int](3 + ($maxH - 3) * (0.35 + 0.65 * $centre) * (0.75 + 0.25 * (Rand)))
        $lean = [math]::Sign($off) * (0.4 + (Rand) * 0.9)
        for ($k = 0; $k -lt $h; $k++) {
            $t = $k / [math]::Max(1, $h - 1)
            Put $bmp ($x0 + [math]::Round($lean * $t * $t * 2.2)) ($baseY - $k) (Shade $base (-0.30 + 0.55 * $t))
        }
    }
}

# A stone: an ellipse lit from the upper left, with a dark lower rim and a
# shadow under it.
function Add-Stone($bmp, $cx, $cy, $rx, $ry, $base) {
    for ($y = -$ry - 1; $y -le $ry + 2; $y++) {
        for ($x = -$rx - 1; $x -le $rx + 1; $x++) {
            $d = ($x * $x) / [double]($rx * $rx + 0.01) + ($y * $y) / [double]($ry * $ry + 0.01)
            if ($d -le 1.0) {
                $light = - ($x / [double]$rx) * 0.5 - ($y / [double]$ry) * 0.7
                $c = if ($light -gt 0.45) { Shade $base 0.22 } elseif ($light -lt -0.55) { Shade $base -0.28 } else { $base }
                if ($d -gt 0.7 -and $y -gt 0) { $c = Shade $base -0.38 }
                Put $bmp ($cx + $x) ($cy + $y) $c
            }
        }
    }
    for ($x = -$rx; $x -le $rx + 1; $x++) { Shadow $bmp ($cx + $x) ($cy + $ry + 1) 90 }
}

function Add-Flower($bmp, $x, $y, $petal, $stem) {
    Put $bmp $x ($y + 1) $stem
    Put $bmp $x ($y + 2) (Shade $stem -0.2)
    Put $bmp $x $y (Rgb 244 206 84)
    Put $bmp ($x - 1) $y $petal; Put $bmp ($x + 1) $y $petal
    Put $bmp $x ($y - 1) (Shade $petal 0.25)
}

$script:made = 0

# Old stencil cut-outs are removed so nothing picks them up by accident.
Get-ChildItem $decor -Filter "patch_*.png" -ErrorAction SilentlyContinue | Remove-Item

# --- grass ---------------------------------------------------------------------------
$meadow = Rgb 96 146 60
$shade  = Rgb 58 118 70
for ($v = 0; $v -lt 4; $v++) {
    $b = New-Decal 20 14; Add-Tuft $b 10 12 $meadow (5 + (RandInt 3)) (8 + (RandInt 3))
    if ($v % 2 -eq 1) { Add-Tuft $b (4 + (RandInt 3)) 13 $meadow 3 6 }
    Save $b "tuft_$v"
}
for ($v = 0; $v -lt 3; $v++) {
    $b = New-Decal 20 14; Add-Tuft $b 10 12 $shade (6 + (RandInt 3)) (9 + (RandInt 3))
    Save $b "tuft_dark_$v"
}

# --- wildflowers ----------------------------------------------------------------------
$petals = @((Rgb 240 240 232), (Rgb 236 208 72), (Rgb 150 140 222), (Rgb 232 132 168), (Rgb 240 240 232))
for ($v = 0; $v -lt 5; $v++) {
    $b = New-Decal 20 16
    Add-Tuft $b 10 14 $meadow 4 6
    $p = $petals[$v]
    for ($k = 0; $k -lt 3 + (RandInt 2); $k++) {
        Add-Flower $b (4 + (RandInt 12)) (4 + (RandInt 6)) $p (Rgb 70 118 48)
    }
    Save $b "flowers_$v"
}

# --- fallen leaves ---------------------------------------------------------------------
$autumn = @((Rgb 196 122 50), (Rgb 168 84 42), (Rgb 206 170 72), (Rgb 128 136 58))
for ($v = 0; $v -lt 3; $v++) {
    $b = New-Decal 22 14
    for ($k = 0; $k -lt 5 + (RandInt 3); $k++) {
        $x = 2 + (RandInt 17); $y = 3 + (RandInt 8)
        $c = $autumn[(RandInt $autumn.Count)]
        Put $b $x $y $c; Put $b ($x + 1) $y $c; Put $b ($x + 1) ($y - 1) (Shade $c 0.18); Put $b ($x + 2) ($y - 1) $c
        Put $b $x ($y - 1) (Shade $c -0.25)
        Shadow $b ($x + 1) ($y + 1) 60
    }
    Save $b "leaves_$v"
}

# --- pebbles ------------------------------------------------------------------------------
$greys = @((Rgb 142 138 128), (Rgb 122 116 106), (Rgb 156 148 132))
for ($v = 0; $v -lt 4; $v++) {
    $b = New-Decal 18 12
    # One stone to each slot across the decal, so they sit apart instead of
    # running together into one lump.
    $slots = @(@(3, 7), @(9, 4), @(14, 8))
    for ($k = 0; $k -lt 3; $k++) {
        if ($k -eq ($v % 3) -and $v -gt 0) { continue }
        Add-Stone $b ($slots[$k][0] + (RandInt 2)) $slots[$k][1] (1 + (RandInt 2)) 1 $greys[(RandInt 3)]
    }
    Save $b "pebbles_$v"
}

# --- the foothills -------------------------------------------------------------------------
$straw = Rgb 170 150 84
for ($v = 0; $v -lt 3; $v++) {
    $b = New-Decal 20 14; Add-Tuft $b 10 12 $straw (5 + (RandInt 3)) (7 + (RandInt 3))
    Save $b "dry_tuft_$v"
}
for ($v = 0; $v -lt 3; $v++) {
    $b = New-Decal 18 12
    $base = $greys[$v]
    Add-Stone $b 9 5 (5 + (RandInt 2)) 3 $base
    # A crack across its face.
    $x = 6 + (RandInt 2); $y = 3
    for ($k = 0; $k -lt 4; $k++) { Put $b ($x + $k) ($y + [int]($k / 2)) (Shade $base -0.35) }
    if ($v -ne 1) { Add-Stone $b (2 + (RandInt 2)) 8 1 1 $greys[2] }
    Save $b "stone_$v"
}

# --- the Cursed Reach ------------------------------------------------------------------------
$scorch = Rgb 40 32 50
$rim    = Rgb 132 116 150 200
for ($v = 0; $v -lt 4; $v++) {
    $b = New-Decal 28 16
    $walks = @(@(14, 8, 1), @(14, 8, -1))
    foreach ($w in $walks) {
        $x = $w[0]; $y = $w[1]; $dir = $w[2]
        for ($k = 0; $k -lt 8 + (RandInt 5); $k++) {
            $x += $dir
            $r = Rand
            if ($r -lt 0.3) { $y -= 1 } elseif ($r -lt 0.6) { $y += 1 }
            $y = [math]::Max(2, [math]::Min(13, $y))
            Put $b $x $y $scorch
            if ($k -lt 4) { Put $b $x ($y + 1) (Shade $scorch 0.1) }
            if ($b.GetPixel([math]::Max(0, [math]::Min(27, $x)), [math]::Max(0, $y - 1)).A -eq 0) { Put $b $x ($y - 1) $rim }
            # A short branch off the main fissure.
            if ($k -eq 4 -and $v % 2 -eq 0) {
                $bx = $x; $by = $y
                for ($j = 0; $j -lt 4; $j++) { $by += 1; $bx += $dir * ((RandInt 2)); Put $b $bx $by $scorch }
            }
        }
    }
    Save $b "crack_$v"
}

# --- the Mire ----------------------------------------------------------------------------------
$sedge = Rgb 84 100 50
for ($v = 0; $v -lt 3; $v++) {
    $b = New-Decal 20 18; Add-Tuft $b 10 16 $sedge (6 + (RandInt 3)) (12 + (RandInt 4))
    Save $b "sedge_$v"
}
for ($v = 0; $v -lt 2; $v++) {
    $b = New-Decal 20 11
    $rx = 7 + $v; $ry = 3
    for ($y = -$ry - 1; $y -le $ry + 1; $y++) {
        for ($x = -$rx - 1; $x -le $rx + 1; $x++) {
            $d = ($x * $x) / [double](($rx + 1) * ($rx + 1)) + ($y * $y) / [double](($ry + 1) * ($ry + 1))
            $wob = (Rand) * 0.12
            if ($d -le 0.62 + $wob) { Put $b (10 + $x) (5 + $y) (Rgb 52 74 62) }
            elseif ($d -le 1.0 + $wob) { Put $b (10 + $x) (5 + $y) (Rgb 70 62 44) }
        }
    }
    Put $b (7 + $v) 4 (Rgb 150 178 168); Put $b (8 + $v) 4 (Rgb 120 150 140); Put $b (12 + $v) 6 (Rgb 110 140 130)
    Save $b "puddle_$v"
}

Write-Host "$script:made decals written to assets/decor/" -ForegroundColor Green
