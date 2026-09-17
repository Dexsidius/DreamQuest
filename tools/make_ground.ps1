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

# --- interior masonry ------------------------------------------------------------
# Thirty-two pixels rather than sixteen, because a flagstone is a large flat
# thing and at sixteen pixels a floor of them is a checkerboard. Both patterns
# are built from courses whose joints sit at fixed positions modulo the tile
# size, which is the same seamlessness trick as the grass: a stone that runs off
# one edge is the same stone at the other.
function New-Masonry($size, $rgb, $mortarRgb, $courseH, $unitW, $jitter, $bevel, $stagger = "half") {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $base   = [System.Drawing.Color]::FromArgb(255, $rgb[0], $rgb[1], $rgb[2])
    $mortar = [System.Drawing.Color]::FromArgb(255, $mortarRgb[0], $mortarRgb[1], $mortarRgb[2])

    $courses = [int]($size / $courseH)
    for ($c = 0; $c -lt $courses; $c++) {
        $y0 = $c * $courseH
        # Masonry offsets alternate courses by half a unit: running bond.
        # Floorboards do not -- laid in a regular half-offset they read as
        # brick -- so their joints land at scattered positions along each run.
        $offset = if ($stagger -eq "scatter") { ($c * 13 + 5) % $unitW }
                  elseif ($c % 2 -eq 1) { [int]($unitW / 2) } else { 0 }
        $units = [int]($size / $unitW)
        for ($u = 0; $u -lt $units; $u++) {
            # Every stone is a little lighter or darker than the next. Without
            # this a floor of identical slabs reads as a grid, not as stone.
            $tone = ((Rand) - 0.5) * $jitter
            # A board usually runs on past the edge of the tile. Only about a
            # third of courses end inside any one tile, and since each floor
            # variant rolls its own, the ends fall at random across a room.
            $endJoint = ($stagger -ne "scatter") -or ((Rand) -lt 0.34)
            $face = Shade $base $tone
            $x0 = $u * $unitW + $offset
            for ($yy = 0; $yy -lt $courseH; $yy++) {
                for ($xx = 0; $xx -lt $unitW; $xx++) {
                    $px = $x0 + $xx; $py = $y0 + $yy
                    $isJoint = ($yy -eq $courseH - 1) -or (($xx -eq $unitW - 1) -and $endJoint)
                    if ($isJoint) {
                        Set-Wrapped $bmp $px $py $mortar
                    } elseif ($bevel -and ($yy -eq 0 -or ($xx -eq 0 -and $stagger -ne "scatter"))) {
                        # Lit top and left edge on each stone, so the courses
                        # have relief rather than being drawn lines.
                        Set-Wrapped $bmp $px $py (Shade $face 0.12)
                    } elseif ($bevel -and ($yy -eq $courseH - 2)) {
                        Set-Wrapped $bmp $px $py (Shade $face (-0.10))
                    } else {
                        Set-Wrapped $bmp $px $py $face
                    }
                }
            }
            # A few pits in each stone.
            for ($k = 0; $k -lt 3; $k++) {
                $sx = $x0 + 1 + (RandInt ([math]::Max(1, $unitW - 3)))
                $sy = $y0 + 1 + (RandInt ([math]::Max(1, $courseH - 3)))
                Set-Wrapped $bmp $sx $sy (Shade $face (-0.14))
            }
        }
    }
    return $bmp
}

$interiors = @(
    # Worn flagstones: big units, low contrast joints, a warm soot-stained grey.
    @{ name = "forge_floor"; rgb = @(112, 104, 96);  mortar = @(70, 62, 56);
       course = 16; unit = 16; jitter = 0.16; bevel = $true; variants = 3 },
    # Coursed rubble for the walls: smaller, darker, more relief.
    @{ name = "forge_wall";  rgb = @(96, 88, 84);   mortar = @(46, 40, 38);
       course = 8;  unit = 16; jitter = 0.22; bevel = $true; variants = 2 },
    # Brick for the chimney breast and trim.
    @{ name = "forge_brick"; rgb = @(150, 92, 70);  mortar = @(78, 58, 48);
       course = 8;  unit = 16; jitter = 0.14; bevel = $true; variants = 1 }
)
# Set-Wrapped wraps at $Size, and the outdoor tiles above are all done, so the
# size moves up for the masonry. Leaving it at sixteen tiles each stone four
# times over inside one thirty-two pixel image.
$Size = 32
# Floorboards are masonry with long units and dark gaps: the same running bond,
# one board per course, joints staggered.
$interiors += @(
    @{ name = "plank_floor";      rgb = @(152, 106, 66); mortar = @(88, 58, 36);
       course = 8;  unit = 32; jitter = 0.05; bevel = $true; variants = 3; stagger = "scatter" },
    @{ name = "plank_floor_dark"; rgb = @(118, 84, 56);  mortar = @(66, 46, 30);
       course = 8;  unit = 32; jitter = 0.05; bevel = $true; variants = 2; stagger = "scatter" }
)
foreach ($t in $interiors) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        $stagger = if ($t.ContainsKey("stagger")) { $t.stagger } else { "half" }
        $bmp = New-Masonry 32 $t.rgb $t.mortar $t.course $t.unit $t.jitter $t.bevel $stagger
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# --- timber and plaster ----------------------------------------------------------
# The inn's and the cottage's walls: limewashed plaster between dark oak. A
# beam along the foot of every tile and a post on its left edge, so a wall
# built from them shows a continuous sill and a post every tile across.
function New-Plaster($size, $plasterRgb, $beamRgb) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $plaster = [System.Drawing.Color]::FromArgb(255, $plasterRgb[0], $plasterRgb[1], $plasterRgb[2])
    $beam    = [System.Drawing.Color]::FromArgb(255, $beamRgb[0], $beamRgb[1], $beamRgb[2])
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) { $bmp.SetPixel($x, $y, $plaster) }
    }
    # Uneven limewash.
    for ($i = 0; $i -lt 70; $i++) {
        $c = if ((Rand) -lt 0.5) { Shade $plaster 0.05 } else { Shade $plaster (-0.06) }
        Set-Wrapped $bmp (RandInt $size) (RandInt $size) $c
    }
    # Sill beam along the foot, lit on its top edge.
    for ($x = 0; $x -lt $size; $x++) {
        for ($y = $size - 7; $y -lt $size; $y++) {
            $c = if ($y -eq $size - 7) { Shade $beam 0.18 } elseif ($y -eq $size - 1) { Shade $beam (-0.25) } else { $beam }
            Set-Wrapped $bmp $x $y $c
        }
    }
    # A post on the left edge, lit on its left side.
    for ($y = 0; $y -lt $size - 7; $y++) {
        for ($x = 0; $x -lt 5; $x++) {
            $c = if ($x -eq 0) { Shade $beam 0.14 } elseif ($x -eq 4) { Shade $beam (-0.22) } else { $beam }
            Set-Wrapped $bmp $x $y $c
        }
    }
    # Grain in the timber.
    for ($i = 0; $i -lt 10; $i++) {
        Set-Wrapped $bmp (1 + (RandInt 3)) (RandInt ($size - 8)) (Shade $beam (-0.14))
        Set-Wrapped $bmp (RandInt $size) ($size - 3 - (RandInt 3)) (Shade $beam (-0.14))
    }
    return $bmp
}

foreach ($t in @(@{ name = "plaster_wall"; plaster = @(222, 208, 176); beam = @(92, 62, 40) },
                 @{ name = "plaster_wall_warm"; plaster = @(214, 190, 150); beam = @(84, 56, 36) })) {
    $bmp = New-Plaster 32 $t.plaster $t.beam
    $bmp.Save((Join-Path $tiles "$($t.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# --- dungeon flagstones -----------------------------------------------------------
# The floors cut from the dungeon pack are flat swatches, and at thirty-two
# pixels a cell they made the mines a floor of pale blue squares that looked
# unfinished beside the textured walls. The same cool grey family as the walls,
# a step lighter so a room still reads as open.
#
# Last, and on a seed of their own: everything above draws from one shared
# random sequence, so slotting these in earlier would have quietly redrawn
# every floorboard and plaster wall after them.
$script:seed = 20260911
foreach ($t in @(@{ name = "dungeon_floor";      rgb = @(106, 108, 124); mortar = @(58, 58, 72) },
                 @{ name = "dungeon_floor_dark"; rgb = @(86, 88, 104);   mortar = @(48, 48, 60) })) {
    $bmp = New-Masonry 32 $t.rgb $t.mortar 16 16 0.20 $true "half"
    $bmp.Save((Join-Path $tiles "$($t.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}


# --- the road ---------------------------------------------------------------------
# The road was cut from the path-and-road pack, and that pack's cobbles are a
# cool blue-grey. Laid three tiles wide through green grass, the Sunken Road and
# Havenbrook's street read as a river -- a playtest walked around the first
# stretch of it looking for a bridge. Warm grey setts instead: small dressed
# stones in running bond, the colour of the dirt around them rather than of the
# water. A full cell of thirty-two pixels holding sixteen setts, in three
# variants: at sixteen pixels there were only four stones to vary, and the road
# was a visible two-by-two check.
$script:seed = 20260913
$Size = 32
for ($v = 0; $v -lt 3; $v++) {
    $bmp = New-Masonry 32 @(138, 124, 106) @(86, 74, 62) 8 8 0.28 $true "half"
    $name = if ($v -eq 0) { "road" } else { "road_$v" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# --- the swamp, the peak and the pit ---------------------------------------------------
# The Mire used to be laid from three tiles cut out of the cursed-land pack, and
# one of them -- "marsh_dark" -- was a patch of black cliff face, so a third of
# the swamp was a streaked black void. Generated instead, like the grass: sedge,
# peat and mud, and bog water with ripples on it. Then the tiles for the Ice
# Spire, the Ashen Path and the infernal dungeon below it.
#
# On a seed of their own, for the same reason as the setts above.
$script:seed = 20260915
$Size = 16

# Short horizontal ripples, lighter, on a darker surface: water seen from above.
function Add-Ripples($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size
        $y = RandInt $Size
        $len = 2 + (RandInt 3)
        for ($k = 0; $k -lt $len; $k++) { Set-Wrapped $bmp ($x + $k) $y (Shade $base 0.20) }
        Set-Wrapped $bmp ($x + 1) ($y + 1) (Shade $base (-0.12))
    }
}
# Long faint streaks, for ice.
function Add-Streaks($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size
        $y = RandInt $Size
        $len = 3 + (RandInt 5)
        for ($k = 0; $k -lt $len; $k++) { Set-Wrapped $bmp ($x + $k) ($y + [int]($k / 3)) (Shade $base 0.35) }
    }
}
# Molten rock: bright blobs in an orange field, crossed by dark crust.
function Add-Crust($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size
        $y = RandInt $Size
        $len = 3 + (RandInt 4)
        $dx = if ((Rand) -lt 0.5) { 1 } else { 0 }
        for ($k = 0; $k -lt $len; $k++) {
            Set-Wrapped $bmp ($x + $k * $dx) ($y + $k * (1 - $dx)) ([System.Drawing.Color]::FromArgb(255, 70, 28, 20))
        }
    }
    for ($i = 0; $i -lt 10; $i++) {
        Set-Wrapped $bmp (RandInt $Size) (RandInt $Size) ([System.Drawing.Color]::FromArgb(255, 255, 200, 80))
    }
}

$wild = @(
    @{ name = "swamp_grass"; rgb = @(104, 118,  66); kind = "grass";  variants = 3 },
    @{ name = "swamp_mud";   rgb = @( 92,  80,  54); kind = "earth";  variants = 3 },
    @{ name = "peat";        rgb = @( 88,  78,  56); kind = "earth";  variants = 2 },
    @{ name = "bog_water";   rgb = @( 56,  80,  64); kind = "water";  variants = 3 },
    @{ name = "ice";         rgb = @(176, 208, 226); kind = "ice";    variants = 3 },
    @{ name = "frost_rock";  rgb = @(128, 138, 150); kind = "earth";  variants = 2 },
    @{ name = "ash";         rgb = @( 92,  84,  82); kind = "sand";   variants = 3 },
    @{ name = "lava";        rgb = @(214,  86,  30); kind = "lava";   variants = 2 },
    # The Ice Spire's cliffs, a step darker than its trodden track, and the
    # cinders either side of the Ashen Path.
    @{ name = "crag";        rgb = @( 88,  96, 110); kind = "earth";  variants = 2 },
    @{ name = "cinder";      rgb = @( 62,  52,  50); kind = "sand";   variants = 3 },
    # Hollowrest: grass that nothing grazes, and the earth of a dug plot.
    @{ name = "grave_grass"; rgb = @( 96, 102,  80); kind = "grass";  variants = 3 },
    @{ name = "grave_earth"; rgb = @( 74,  68,  58); kind = "earth";  variants = 2 }
)
foreach ($f in $wild) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    for ($v = 0; $v -lt $f.variants; $v++) {
        $bmp = New-Tile $base
        switch ($f.kind) {
            "grass" { Add-Speckle $bmp $base 54 0.10 0.10; Add-Blades $bmp $base 11; Add-Grit $bmp $base 4 1 }
            "earth" { Add-Speckle $bmp $base 62 0.08 0.12; Add-Grit $bmp $base 8 2 }
            "sand"  { Add-Speckle $bmp $base 78 0.07 0.08; Add-Grit $bmp $base 3 1 }
            "water" { Add-Speckle $bmp $base 30 0.04 0.10; Add-Ripples $bmp $base 5 }
            "ice"   { Add-Speckle $bmp $base 40 0.08 0.06; Add-Streaks $bmp $base 3 }
            "lava"  { Add-Speckle $bmp $base 40 0.12 0.08; Add-Crust $bmp $base 5 }
        }
        $name = if ($v -eq 0) { $f.name } else { "$($f.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# Thirty-two pixel masonry: the infernal dungeon's basalt with embers glowing in
# the joints, its walls, and the flagstones of the inn's cellar.
$Size = 32
foreach ($t in @(@{ name = "hell_floor";      rgb = @(88, 62, 56);  mortar = @(150, 60, 28); course = 16; unit = 16; jitter = 0.22 },
                 @{ name = "hell_floor_dark"; rgb = @(70, 50, 46);  mortar = @(120, 46, 24); course = 16; unit = 16; jitter = 0.22 },
                 @{ name = "hell_wall";       rgb = @(62, 42, 42);  mortar = @(26, 16, 16);  course = 8;  unit = 16; jitter = 0.24 },
                 @{ name = "cellar_floor";    rgb = @(104, 96, 86); mortar = @(62, 56, 50);  course = 16; unit = 16; jitter = 0.18 },
                 @{ name = "cellar_floor_dark"; rgb = @(88, 80, 72); mortar = @(54, 48, 42); course = 16; unit = 16; jitter = 0.18 })) {
    $bmp = New-Masonry 32 $t.rgb $t.mortar $t.course $t.unit $t.jitter $true "half"
    $bmp.Save((Join-Path $tiles "$($t.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# --- the last eight ----------------------------------------------------------------
# These were the only tiles still cut from a pack: the dungeon and guild walls,
# the guild's floor, open water, two Mire fills, the cursed sand and the void
# behind a dungeon's walls. With them generated, assets/tiles/ is entirely the
# game's own.
$script:seed = 20260917

# Walls are masonry with a tight course, so a wall reads as courses of stone
# rather than as flagstones stood on end.
$Size = 32
foreach ($t in @(@{ name = "dungeon_wall"; rgb = @(74, 76, 92);   mortar = @(38, 38, 50); course = 8; unit = 16; jitter = 0.26 },
                 @{ name = "guild_wall";   rgb = @(126, 112, 92); mortar = @(74, 64, 52); course = 8; unit = 16; jitter = 0.22 })) {
    $bmp = New-Masonry 32 $t.rgb $t.mortar $t.course $t.unit $t.jitter $true "half"
    $bmp.Save((Join-Path $tiles "$($t.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# The guild hall's floor: wide boards, a shade redder than the inn's, laid the
# same way -- masonry with one long unit per course and scattered joints.
$bmp = New-Masonry 32 @(132, 96, 62) @(76, 52, 34) 8 32 0.05 $true "scatter"
$bmp.Save((Join-Path $tiles "guild_floor.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# Open water: deeper and bluer than the bog, with ripples across it. The pond,
# the mill race and the lake all draw from this one.
$Size = 16
$deep = [System.Drawing.Color]::FromArgb(255, 52, 96, 138)
for ($v = 0; $v -lt 3; $v++) {
    $bmp = New-Tile $deep
    Add-Speckle $bmp $deep 26 0.05 0.10
    Add-Ripples $bmp $deep 6
    $name = if ($v -eq 0) { "water" } else { "water_$v" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# Two more Mire fills and the sand of the Cursed Reach. "marsh_dark" was a
# patch of black cliff face in the pack it came from, which is how a third of
# the swamp ended up a streaked void; here it is simply wet peat.
foreach ($f in @(
    @{ name = "marsh_dark";  rgb = @( 62,  62,  48); kind = "earth" },
    @{ name = "marsh_stone"; rgb = @( 96,  98,  88); kind = "earth" },
    @{ name = "cursed_sand"; rgb = @(118, 104, 100); kind = "sand"  }
)) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    $bmp = New-Tile $base
    switch ($f.kind) {
        "earth" { Add-Speckle $bmp $base 62 0.08 0.12; Add-Grit $bmp $base 8 2 }
        "sand"  { Add-Speckle $bmp $base 78 0.07 0.08; Add-Grit $bmp $base 3 1 }
    }
    $bmp.Save((Join-Path $tiles "$($f.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# And the void behind a dungeon's walls: not quite black, so the edge of the
# map reads as unlit rock rather than as a hole in the screen.
$void = [System.Drawing.Color]::FromArgb(255, 18, 18, 24)
$bmp = New-Tile $void
Add-Speckle $bmp $void 20 0.04 0.06
$bmp.Save((Join-Path $tiles "dungeon_void.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

Write-Host "$made ground tiles written to assets/tiles/" -ForegroundColor Green
