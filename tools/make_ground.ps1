# =============================================================================
#  make_ground.ps1 - generates the textured ground tiles.
#
#      .\tools\make_ground.ps1
#
#  The ground tiles this replaces were each a single flat colour. That was not
#  an accident: they are cut from CraftPix tilesets by looking for cells that
#  are fully opaque with zero variance, because those are the palette swatches
#  a tileset is designed to be laid over. It works, and it is why the overworld
#  read as coloured paper -- a screen of grass was one RGB value repeated four
#  thousand times, with the only relief coming from rectangular patches of a
#  second flat colour.
#
#  So the tiles are generated instead. Every mark is placed with wrapped
#  coordinates, which is what makes them tile seamlessly: a blade that runs off
#  the right edge continues at the left, so there is no seam to line up.
#
#  Several variants per family, because one perfect tile repeated across a
#  4096-pixel map is still a visible grid. genmaps.cpp picks between them.
# =============================================================================

param([int]$Size = 16)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root  = Split-Path $PSScriptRoot -Parent
$tiles = Join-Path $root "assets\tiles"
New-Item -ItemType Directory -Force -Path $tiles | Out-Null

# One deterministic sequence, so re-running produces byte-identical tiles and a
# rebuild does not show up as a diff in every ground tile on the map.
$script:seed = 20260910
function Rand {
    # Numerical Recipes' LCG. Any decent one would do; the point is that it is
    # ours and does not change between machines or PowerShell versions.
    $script:seed = [int](($script:seed * 1664525 + 1013904223) -band 0x7FFFFFFF)
    return $script:seed / 2147483647.0
}
function RandInt($n) { return [int]([math]::Floor((Rand) * $n)) % $n }

function Shade($c, $amount) {
    # Positive lightens toward white, negative darkens toward black. Kept as a
    # ratio rather than an offset so a dark tile's highlights stay subtle.
    $f = [math]::Abs($amount)
    if ($amount -ge 0) {
        return [System.Drawing.Color]::FromArgb(255,
            [int]($c.R + (255 - $c.R) * $f),
            [int]($c.G + (255 - $c.G) * $f),
            [int]($c.B + (255 - $c.B) * $f))
    }
    return [System.Drawing.Color]::FromArgb(255,
        [int]($c.R * (1 - $f)), [int]($c.G * (1 - $f)), [int]($c.B * (1 - $f)))
}

function New-Tile($base) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $Size, $Size,
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $Size; $y++) {
        for ($x = 0; $x -lt $Size; $x++) { $bmp.SetPixel($x, $y, $base) }
    }
    return $bmp
}

# Draws a mark, wrapping at the edges. Everything goes through here, which is
# the whole seamlessness story.
function Set-Wrapped($bmp, $x, $y, $colour) {
    $bmp.SetPixel((($x % $Size) + $Size) % $Size, (($y % $Size) + $Size) % $Size, $colour)
}

function Add-Speckle($bmp, $base, $count, $lighten, $darken) {
    for ($i = 0; $i -lt $count; $i++) {
        $c = if ((Rand) -lt 0.5) { Shade $base $lighten } else { Shade $base (-$darken) }
        Set-Wrapped $bmp (RandInt $Size) (RandInt $Size) $c
    }
}

# A blade is two or three stacked pixels, brighter at the tip. Vertical rather
# than any angle: at sixteen pixels a diagonal is a staircase, and a field of
# staircases reads as noise instead of as grass.
function Add-Blades($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size
        $y = RandInt $Size
        $len = 2 + (RandInt 2)
        for ($k = 0; $k -lt $len; $k++) {
            $lift = 0.10 + 0.09 * ($len - $k)
            Set-Wrapped $bmp $x ($y + $k) (Shade $base $lift)
        }
    }
}

# Small clusters of a darker shade, for earth and stone rather than grass.
function Add-Grit($bmp, $base, $count, $spread) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size
        $y = RandInt $Size
        $c = Shade $base (-(0.10 + (Rand) * 0.14))
        Set-Wrapped $bmp $x $y $c
        for ($k = 0; $k -lt $spread; $k++) {
            Set-Wrapped $bmp ($x + (RandInt 2)) ($y + (RandInt 2)) $c
        }
    }
}

# family -> base colour and how it is textured. The base colours are the flat
# values the old tiles used, so anything already placed on a map keeps the
# colour it had and simply gains a surface.
$families = @(
    @{ name = "grass";       rgb = @(122, 173,  85); kind = "grass";  variants = 4 },
    @{ name = "grass_light"; rgb = @(160, 179,  90); kind = "grass";  variants = 3 },
    @{ name = "grass_dark";  rgb = @( 72, 148,  97); kind = "grass";  variants = 3 },
    @{ name = "grass_olive"; rgb = @(144, 163,  71); kind = "grass";  variants = 3 },
    @{ name = "dirt";        rgb = @(180, 124,  67); kind = "earth";  variants = 3 },
    @{ name = "dirt_dark";   rgb = @(138,  94,  52); kind = "earth";  variants = 2 },
    @{ name = "sand";        rgb = @(197, 185, 151); kind = "sand";   variants = 3 },
    @{ name = "moss";        rgb = @( 92, 133,  70); kind = "grass";  variants = 2 },
    @{ name = "snow";        rgb = @(226, 232, 240); kind = "sand";   variants = 2 },
    @{ name = "marsh_ground";rgb = @( 94, 110,  74); kind = "earth";  variants = 2 },
    @{ name = "cursed_ground";rgb= @( 88,  78, 104); kind = "earth";  variants = 2 }
)

$made = 0
foreach ($f in $families) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    for ($v = 0; $v -lt $f.variants; $v++) {
        $bmp = New-Tile $base
        switch ($f.kind) {
            "grass" {
                Add-Speckle $bmp $base 54 0.10 0.09
                Add-Blades  $bmp $base 9
                Add-Grit    $bmp $base 3 1
            }
            "earth" {
                Add-Speckle $bmp $base 62 0.09 0.11
                Add-Grit    $bmp $base 7 2
            }
            "sand" {
                Add-Speckle $bmp $base 78 0.06 0.06
                Add-Grit    $bmp $base 2 1
            }
        }
        # The first variant keeps the family name, so every map and every bit
        # of data that already refers to "grass" keeps working untouched.
        $name = if ($v -eq 0) { $f.name } else { "$($f.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

Write-Host "$made ground tiles written to assets/tiles/" -ForegroundColor Green
