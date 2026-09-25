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

# --- the college at Fernhollow ------------------------------------------------------
# Its own tileset, and last in the file on purpose: the sequence above is one
# run of one generator, so anything added before the end would re-roll every
# tile after it and show up as a diff in the whole world's ground.
#
# Pale stone throughout, because everything else in the Hollowmarch is brown:
# wide cream flagstones for the courtyard, the same with a blue lozenge let into
# it for the avenues, polished chequer for the chambers, ashlar with a band of
# college blue and a gold line for their walls, the top of the courtyard's own
# wall seen from above, and a blue runner.
$Size = 32
$collegeBlue = @(52, 78, 146)
$collegeGold = @(214, 178, 92)

foreach ($t in @(@{ name = "college_paving"; rgb = @(202, 196, 182); mortar = @(158, 152, 142); variants = 3 })) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        $bmp = New-Masonry 32 $t.rgb $t.mortar 16 32 0.07 $true
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The avenue: the same flagstone with a blue lozenge and a gold pip at its heart.
$bmp = New-Masonry 32 @(206, 200, 186) @(158, 152, 142) 32 32 0.04 $true
$blue = [System.Drawing.Color]::FromArgb(255, $collegeBlue[0], $collegeBlue[1], $collegeBlue[2])
$gold = [System.Drawing.Color]::FromArgb(255, $collegeGold[0], $collegeGold[1], $collegeGold[2])
for ($y = 0; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) {
        $d = [math]::Abs($x - 15.5) + [math]::Abs($y - 15.5)
        if ($d -le 9.5 -and $d -gt 7.5) { $bmp.SetPixel($x, $y, (Shade $blue (-0.15))) }
        elseif ($d -le 7.5 -and $d -gt 2.5) { $bmp.SetPixel($x, $y, (Shade $blue ((Rand) * 0.10))) }
        elseif ($d -le 2.5) { $bmp.SetPixel($x, $y, $gold) }
    }
}
$bmp.Save((Join-Path $tiles "college_inlay.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# Chequer for the chambers: sixteen-pixel squares of cream and slate-blue marble,
# a hairline joint, and a vein or two in each so that it is stone and not paint.
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Tile ([System.Drawing.Color]::FromArgb(255, 226, 220, 206))
    for ($cy = 0; $cy -lt 2; $cy++) {
        for ($cx = 0; $cx -lt 2; $cx++) {
            $dark = (($cx + $cy) % 2) -eq 1
            $rgb = if ($dark) { @(104, 120, 158) } else { @(228, 222, 208) }
            $face = Shade ([System.Drawing.Color]::FromArgb(255, $rgb[0], $rgb[1], $rgb[2])) (((Rand) - 0.5) * 0.08)
            for ($y = 0; $y -lt 16; $y++) {
                for ($x = 0; $x -lt 16; $x++) {
                    $c = $face
                    if ($x -eq 15 -or $y -eq 15) { $c = Shade $face (-0.22) }
                    elseif ($x -eq 0 -or $y -eq 0) { $c = Shade $face 0.14 }
                    $bmp.SetPixel($cx * 16 + $x, $cy * 16 + $y, $c)
                }
            }
            # Veins: a short wandering line, lighter in the dark stone and darker in the light.
            for ($k = 0; $k -lt 2; $k++) {
                $vx = 2 + (RandInt 11); $vy = 2 + (RandInt 11)
                for ($n = 0; $n -lt 5; $n++) {
                    $px = $cx * 16 + [math]::Min(14, [math]::Max(1, $vx)); $py = $cy * 16 + [math]::Min(14, [math]::Max(1, $vy))
                    $veinShade = if ($dark) { 0.16 } else { -0.10 }
                    $bmp.SetPixel($px, $py, (Shade $face $veinShade))
                    $vx += (RandInt 3) - 1; $vy += 1
                }
            }
        }
    }
    $name = if ($v -eq 0) { "college_floor" } else { "college_floor_$v" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# The chambers' walls: pale ashlar, a band of college blue across the lower
# half with a gold line above and below it. A wall built of these has one
# unbroken band running round the room.
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Masonry 32 @(186, 182, 192) @(126, 122, 140) 8 16 0.10 $true
    for ($x = 0; $x -lt 32; $x++) {
        for ($y = 17; $y -le 26; $y++) {
            $c = if ($y -eq 17 -or $y -eq 26) { $gold } else { Shade $blue (((($x * 7 + $y * 3) % 5) - 2) * 0.02) }
            $bmp.SetPixel($x, $y, $c)
        }
        # A gold star every sixteen pixels along the band.
        if (($x % 16) -eq 8) {
            foreach ($d in @(@(0, 0), @(-1, 0), @(1, 0), @(0, -1), @(0, 1))) { $bmp.SetPixel($x + $d[0], 21 + $d[1], $gold) }
        }
    }
    $name = if ($v -eq 0) { "college_wall" } else { "college_wall_$v" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# The top of the courtyard's wall, seen from above: big pale capstones with a
# dark joint, a lit inner edge and a shadowed outer one.
$bmp = New-Masonry 32 @(170, 166, 176) @(104, 100, 118) 16 32 0.08 $true
for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, 0, (Shade ([System.Drawing.Color]::FromArgb(255, 170, 166, 176)) 0.22)) }
$bmp.Save((Join-Path $tiles "college_walltop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# And the face of that wall, where it is seen: coursed pale stone, darker toward its foot.
$bmp = New-Masonry 32 @(158, 154, 166) @(98, 94, 112) 8 16 0.12 $true
for ($y = 24; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) (-0.05 * ($y - 23)))) }
}
$bmp.Save((Join-Path $tiles "college_wallface.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# The runner: college blue, a weave in it, no border -- it is laid in lengths.
$bmp = New-Tile $blue
for ($y = 0; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) {
        $w = if ((($x + $y) % 4) -eq 0) { 0.10 } elseif ((($x - $y + 64) % 4) -eq 0) { -0.10 } else { 0.0 }
        $bmp.SetPixel($x, $y, (Shade $blue $w))
    }
}
$bmp.Save((Join-Path $tiles "college_carpet.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# --- the Brimstone Palace ----------------------------------------------------------
# Black basalt polished to a shine, with the fire under the palace showing
# through its joints; walls of the same stone banded in crimson and gold; a
# crimson runner with a gold border, laid a cell at a time so its edges are
# tiles of their own; and the ballroom's chequer of black and blood marble.
# Appended last: this file is one random sequence, and anything added before
# the end re-rolls every tile after it.
$pgold    = [System.Drawing.Color]::FromArgb(255, 206, 158, 66)
$pgoldDk  = [System.Drawing.Color]::FromArgb(255, 138, 96, 36)
$pcrimson = [System.Drawing.Color]::FromArgb(255, 122, 18, 30)
$pblack   = [System.Drawing.Color]::FromArgb(255, 18, 12, 14)

foreach ($t in @(@{ name = "palace_floor";      rgb = @(48, 42, 48); mortar = @(132, 38, 24); variants = 3 },
                 @{ name = "palace_floor_dark"; rgb = @(34, 30, 36); mortar = @(96, 28, 20);  variants = 2 })) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        # One great slab to a cell, laid square: a palace floor is a grid of
        # dressed stone, and a running bond of small blocks read as brick.
        $bmp = New-Masonry 32 $t.rgb $t.mortar 32 32 0.10 $true
        # Polish: a few bright flecks where the light catches the stone.
        for ($k = 0; $k -lt 5; $k++) {
            $x = RandInt 32; $y = RandInt 32
            if ((($x % 16) -ne 15) -and (($y % 16) -ne 15)) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) 0.30)) }
        }
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The back wall: dark ashlar with a crimson band across its lower half, gold
# above and below it, and a gold horn every sixteen pixels along the band.
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Masonry 32 @(60, 52, 58) @(24, 20, 24) 8 16 0.18 $true
    for ($x = 0; $x -lt 32; $x++) {
        for ($y = 17; $y -le 26; $y++) {
            $c = if ($y -eq 17 -or $y -eq 26) { $pgold } else { Shade $pcrimson (((($x * 5 + $y * 3) % 5) - 2) * 0.03) }
            $bmp.SetPixel($x, $y, $c)
        }
        if (($x % 16) -eq 8) {
            foreach ($d in @(@(-2, 1), @(-1, 0), @(0, -1), @(1, 0), @(2, 1), @(0, 0), @(0, 1), @(0, 2))) {
                $bmp.SetPixel($x + $d[0], 21 + $d[1], $pgold)
            }
        }
    }
    $name = if ($v -eq 0) { "palace_wall" } else { "palace_wall_1" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# Its top course, plain, and darker toward the foot where the band begins.
$bmp = New-Masonry 32 @(58, 50, 56) @(22, 18, 22) 8 16 0.18 $true
for ($y = 26; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) (-0.06 * ($y - 25)))) }
}
$bmp.Save((Join-Path $tiles "palace_wallface.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# The tops of the side walls, seen from above: heavy capstones with a gold
# coping along the inner edge.
$bmp = New-Masonry 32 @(72, 64, 70) @(30, 26, 30) 16 32 0.10 $true
for ($y = 0; $y -lt 32; $y++) { $bmp.SetPixel(0, $y, $pgoldDk); $bmp.SetPixel(31, $y, $pgoldDk) }
$bmp.Save((Join-Path $tiles "palace_walltop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# The runner. A damask lattice in a darker crimson over the field, a gold pip
# where the lattice crosses, and it tiles every way. _l and _r are its edges: a
# black selvedge, a gold border and a dark line inside it.
function New-Runner($edge) {
    $bmp = New-Tile $pcrimson
    for ($y = 0; $y -lt 32; $y++) {
        for ($x = 0; $x -lt 32; $x++) {
            $d = [math]::Abs((($x + 8) % 16) - 8) + [math]::Abs((($y + 8) % 16) - 8)
            $c = $pcrimson
            if ($d -eq 8) { $c = Shade $pcrimson (-0.30) }
            elseif ($d -eq 7) { $c = Shade $pcrimson 0.06 }
            elseif ($d -le 1) { $c = if ($d -eq 0) { $pgold } else { Shade $pcrimson (-0.20) } }
            elseif ((($x * 3 + $y * 7) % 11) -eq 0) { $c = Shade $pcrimson (-0.08) }
            $bmp.SetPixel($x, $y, $c)
        }
    }
    if ($edge -ne "") {
        for ($y = 0; $y -lt 32; $y++) {
            for ($i = 0; $i -lt 8; $i++) {
                $x = if ($edge -eq "l") { $i } else { 31 - $i }
                $c = switch ($i) {
                    0 { $pblack } 1 { $pblack } 2 { $pgoldDk } 3 { $pgold } 4 { $pgold } 5 { $pgoldDk }
                    6 { Shade $pcrimson (-0.40) } default { Shade $pcrimson (-0.15) }
                }
                # A gold stitch across the border every eight pixels.
                if (($i -eq 3 -or $i -eq 4) -and (($y % 8) -eq 0)) { $c = $pgoldDk }
                $bmp.SetPixel($x, $y, $c)
            }
        }
    }
    return $bmp
}
foreach ($e in @(@("", "palace_carpet"), @("l", "palace_carpet_l"), @("r", "palace_carpet_r"))) {
    $bmp = New-Runner $e[0]
    $bmp.Save((Join-Path $tiles "$($e[1]).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# The ballroom's chequer: a cell of black marble veined white, and one of
# blood-red marble veined black. Laid alternately, a cell at a time.
foreach ($m in @(@("palace_marble_a", @(28, 24, 30), 0.18), @("palace_marble_a_1", @(30, 26, 32), 0.18),
                 @("palace_marble_b", @(118, 22, 30), -0.18), @("palace_marble_b_1", @(112, 20, 28), -0.18))) {
    $base = [System.Drawing.Color]::FromArgb(255, $m[1][0], $m[1][1], $m[1][2])
    $bmp = New-Tile $base
    for ($y = 0; $y -lt 32; $y++) {
        for ($x = 0; $x -lt 32; $x++) {
            $c = Shade $base (((($x * 13 + $y * 7) % 9) - 4) * 0.012)
            if ($x -eq 31 -or $y -eq 31) { $c = Shade $base (-0.35) }
            elseif ($x -eq 0 -or $y -eq 0) { $c = Shade $base 0.16 }
            $bmp.SetPixel($x, $y, $c)
        }
    }
    # A vein or two, running on the diagonal and wandering as it goes.
    for ($k = 0; $k -lt 2; $k++) {
        $vx = 1 + (RandInt 12); $vy = 1 + (RandInt 10)
        for ($n = 0; $n -lt 26; $n++) {
            $px = [math]::Min(30, [math]::Max(1, $vx)); $py = [math]::Min(30, [math]::Max(1, $vy))
            $bmp.SetPixel($px, $py, (Shade ($bmp.GetPixel($px, $py)) $m[2]))
            if ((RandInt 3) -ne 0) { $vx += 1 }
            if ((RandInt 3) -ne 0) { $vy += 1 }
            if ($vy -gt 30 -or $vx -gt 30) { break }
        }
    }
    $bmp.Save((Join-Path $tiles "$($m[0]).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}

# --- Purgatory's Plateau ------------------------------------------------------------
# Four maps, and a ground of its own for each: pale ash and scree on the climb up
# from the Ashen Path, white salt cracked into plates on the flats, wet dark stone
# and brine on the terraces, and bone-dust and grey flagstones about the
# Stronghold -- with a road of trodden gravel through all four, and the keep's
# floor and walls. On a seed of its own, so nothing above is re-rolled.
$script:seed = 20260924
$Size = 16

# Salt crust: long straight dark cracks across a pale field, meeting at angles,
# and a pale lip along one side of each so the plates read as raised.
function Add-Cracks($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        $across = ($i % 2) -eq 0
        $len = 7 + (RandInt 9)
        for ($k = 0; $k -lt $len; $k++) {
            Set-Wrapped $bmp $x $y (Shade $base (-0.28))
            if ($across) { Set-Wrapped $bmp $x ($y - 1) (Shade $base 0.40) } else { Set-Wrapped $bmp ($x - 1) $y (Shade $base 0.40) }
            # A crack wanders: mostly straight on, now and then a step aside.
            $bend = if ((RandInt 3) -eq 0) { (RandInt 2) * 2 - 1 } else { 0 }
            if ($across) { $x += 1; $y += $bend } else { $y += 1; $x += $bend }
        }
    }
}

$plateau = @(
    @{ name = "pale_ash";     rgb = @(158, 152, 146); kind = "sand";  variants = 3 },
    @{ name = "scree";        rgb = @(116, 110, 108); kind = "earth"; variants = 3 },
    @{ name = "salt_flat";    rgb = @(218, 222, 226); kind = "salt";  variants = 3 },
    @{ name = "salt_crust";   rgb = @(192, 200, 210); kind = "sand";  variants = 2 },
    @{ name = "brine_stone";  rgb = @( 76,  96, 102); kind = "earth"; variants = 3 },
    @{ name = "brine";        rgb = @( 50,  96, 110); kind = "water"; variants = 3 },
    @{ name = "bone_dust";    rgb = @(172, 164, 146); kind = "sand";  variants = 3 },
    @{ name = "plateau_road"; rgb = @(134, 126, 116); kind = "earth"; variants = 2 }
)
foreach ($f in $plateau) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    for ($v = 0; $v -lt $f.variants; $v++) {
        $bmp = New-Tile $base
        switch ($f.kind) {
            "earth" { Add-Speckle $bmp $base 62 0.08 0.12; Add-Grit $bmp $base 9 2 }
            "sand"  { Add-Speckle $bmp $base 78 0.07 0.08; Add-Grit $bmp $base 3 1 }
            "salt"  { Add-Speckle $bmp $base 40 0.04 0.05; Add-Cracks $bmp $base 3 }
            "water" { Add-Speckle $bmp $base 30 0.05 0.10; Add-Ripples $bmp $base 6 }
        }
        $name = if ($v -eq 0) { $f.name } else { "$($f.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The Stronghold's flagstones and the keep's floor: big square slabs of the pale
# basalt; the keep's with the pale fire showing in its joints, as the palace's
# shows red.
foreach ($t in @(@{ name = "stronghold_flag";      rgb = @(104, 100, 102); mortar = @(58, 56, 60); variants = 3 },
                 @{ name = "stronghold_flag_dark"; rgb = @(84, 80, 84);    mortar = @(48, 46, 50); variants = 2 },
                 @{ name = "keep_floor";           rgb = @(70, 70, 76);    mortar = @(62, 96, 92);   variants = 3 },
                 @{ name = "keep_floor_dark";      rgb = @(54, 54, 60);    mortar = @(50, 76, 72);   variants = 2 })) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        $bmp = New-Masonry 32 $t.rgb $t.mortar 32 32 0.10 $true
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The keep's walls, face and top: grey ashlar with a band of bone across it and a
# pale eye every sixteen pixels -- the Stronghold's banner, cut in the stone.
$sbone = [System.Drawing.Color]::FromArgb(255, 214, 206, 184)
$ssoul = [System.Drawing.Color]::FromArgb(255, 150, 244, 214)
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Masonry 32 @(78, 76, 82) @(34, 32, 38) 8 16 0.18 $true
    for ($x = 0; $x -lt 32; $x++) {
        foreach ($y in @(18, 25)) { $bmp.SetPixel($x, $y, $sbone) }
        if (($x % 16) -eq 8) {
            foreach ($d in @(@(-1, 0), @(0, -1), @(1, 0), @(0, 1))) { $bmp.SetPixel($x + $d[0], 21 + $d[1], $sbone) }
            $bmp.SetPixel($x, 21, $ssoul)
        }
    }
    $name = if ($v -eq 0) { "keep_wall" } else { "keep_wall_1" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}
$bmp = New-Masonry 32 @(76, 74, 80) @(32, 30, 36) 8 16 0.18 $true
for ($y = 26; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) (-0.06 * ($y - 25)))) }
}
$bmp.Save((Join-Path $tiles "keep_wallface.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++
$bmp = New-Masonry 32 @(92, 90, 96) @(40, 38, 44) 16 32 0.10 $true
for ($y = 0; $y -lt 32; $y++) { $bmp.SetPixel(0, $y, $sbone); $bmp.SetPixel(31, $y, $sbone) }
$bmp.Save((Join-Path $tiles "keep_walltop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# --- the Hexmire ------------------------------------------------------------------
# Four maps north of the Bayou, and again a ground of its own for each: black
# loam and rust cypress-litter round black water in the Drowns; crushed shell,
# wet tide-flat and a clear lagoon on the Shellbacks' strand; red clay and
# yellow sedge in the cult's fens; packed ochre earth, chalked, in the temple's
# yard -- a trodden causeway through all four, and the sanctum's floor and
# walls. On a seed of its own, so nothing above is re-rolled.
$script:seed = 20260925
$Size = 16

# Cypress litter: short rust needles lying every way, darker and lighter.
function Add-Needles($bmp, $base, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        $dx = (RandInt 3) - 1; $dy = if ($dx -eq 0) { 1 } else { (RandInt 2) }
        $c = if ((Rand) -lt 0.5) { Shade $base 0.22 } else { Shade $base (-0.24) }
        for ($k = 0; $k -lt 3; $k++) { Set-Wrapped $bmp ($x + $k * $dx) ($y + $k * $dy) $c }
    }
}
# Crushed shell: little bright chips, some pink, each with a shadow under it.
function Add-Shells($bmp, $base, $count) {
    $pink = [System.Drawing.Color]::FromArgb(255, 236, 196, 188)
    $white = [System.Drawing.Color]::FromArgb(255, 246, 240, 228)
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        $c = if ((RandInt 3) -eq 0) { $pink } else { $white }
        Set-Wrapped $bmp $x $y $c
        if ((RandInt 2) -eq 0) { Set-Wrapped $bmp ($x + 1) $y $c }
        Set-Wrapped $bmp $x ($y + 1) (Shade $base (-0.18))
    }
}
# Chalk: a pale fleck here and there, and now and then a short stroke of one,
# where a mark on the ground has been walked half away.
function Add-Chalk($bmp, $base, $count) {
    $chalk = [System.Drawing.Color]::FromArgb(255, 226, 214, 190)
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        Set-Wrapped $bmp $x $y $chalk
        if ((RandInt 3) -eq 0) { Set-Wrapped $bmp ($x + 1) $y (Shade $chalk (-0.12)); Set-Wrapped $bmp ($x + 2) $y (Shade $chalk (-0.2)) }
    }
}

$hexmire = @(
    @{ name = "drowned_loam";   rgb = @( 58,  66,  46); kind = "earth";   variants = 3 },
    @{ name = "cypress_litter"; rgb = @( 84,  66,  44); kind = "litter";  variants = 3 },
    @{ name = "blackwater";     rgb = @( 38,  42,  32); kind = "water";   variants = 3 },
    @{ name = "shell_sand";     rgb = @(206, 194, 170); kind = "shell";   variants = 3 },
    @{ name = "tide_flat";      rgb = @(152, 144, 122); kind = "earth";   variants = 2 },
    @{ name = "lagoon";         rgb = @( 52, 108, 100); kind = "water";   variants = 3 },
    @{ name = "hex_clay";       rgb = @(128,  66,  48); kind = "earth";   variants = 3 },
    @{ name = "fen_sedge";      rgb = @(124, 118,  62); kind = "grass";   variants = 3 },
    @{ name = "temple_earth";   rgb = @(156, 108,  64); kind = "chalked"; variants = 3 },
    @{ name = "hex_road";       rgb = @(112,  92,  68); kind = "earth";   variants = 2 }
)
foreach ($f in $hexmire) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    for ($v = 0; $v -lt $f.variants; $v++) {
        $bmp = New-Tile $base
        switch ($f.kind) {
            "earth"   { Add-Speckle $bmp $base 62 0.08 0.12; Add-Grit $bmp $base 8 2 }
            "grass"   { Add-Speckle $bmp $base 54 0.10 0.10; Add-Blades $bmp $base 12; Add-Grit $bmp $base 3 1 }
            "litter"  { Add-Speckle $bmp $base 40 0.08 0.10; Add-Needles $bmp $base 9 }
            "shell"   { Add-Speckle $bmp $base 60 0.06 0.07; Add-Shells $bmp $base 6 }
            "chalked" { Add-Speckle $bmp $base 60 0.07 0.10; Add-Grit $bmp $base 5 1; Add-Chalk $bmp $base 3 }
            "water"   { Add-Speckle $bmp $base 30 0.05 0.10; Add-Ripples $bmp $base 5 }
        }
        $name = if ($v -eq 0) { $f.name } else { "$($f.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The sanctum: a floor of dark boards oiled red, and walls of ochre daub between
# black cypress posts with a band painted round them at shoulder height -- red,
# and a row of white marks along it.
$Size = 32
foreach ($t in @(@{ name = "sanctum_floor";      rgb = @(98, 54, 42); mortar = @(44, 24, 20); variants = 3 },
                 @{ name = "sanctum_floor_dark"; rgb = @(78, 44, 36); mortar = @(38, 20, 18); variants = 2 })) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        $bmp = New-Masonry 32 $t.rgb $t.mortar 8 32 0.06 $true "scatter"
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}
$hred = [System.Drawing.Color]::FromArgb(255, 150, 40, 34)
$hwhite = [System.Drawing.Color]::FromArgb(255, 226, 216, 196)
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Plaster 32 @(152, 112, 76) @(44, 32, 26)
    for ($x = 0; $x -lt 32; $x++) {
        foreach ($y in @(12, 13, 14, 15)) { $bmp.SetPixel($x, $y, $(if ($y -eq 12) { Shade $hred 0.12 } else { $hred })) }
        if (($x % 8) -eq 4) { $bmp.SetPixel($x, 13, $hwhite); $bmp.SetPixel($x, 14, $hwhite) }
    }
    $name = if ($v -eq 0) { "sanctum_wall" } else { "sanctum_wall_1" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}
$bmp = New-Plaster 32 @(140, 102, 70) @(44, 32, 26)
for ($y = 24; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) (-0.05 * ($y - 23)))) }
}
$bmp.Save((Join-Path $tiles "sanctum_wallface.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++
# The top of the wall is its thatch, seen from above: straw laid in courses.
$thatch = [System.Drawing.Color]::FromArgb(255, 132, 106, 62)
$bmp = New-Tile $thatch
for ($y = 0; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) {
        $c = $thatch
        if (($y % 6) -eq 5) { $c = Shade $thatch (-0.30) }
        elseif ((($x * 5 + $y * 3) % 7) -eq 0) { $c = Shade $thatch 0.14 }
        elseif ((($x * 3 + $y) % 5) -eq 0) { $c = Shade $thatch (-0.10) }
        $bmp.SetPixel($x, $y, $c)
    }
}
$bmp.Save((Join-Path $tiles "sanctum_walltop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# --- the Frostreach -------------------------------------------------------------------
# Four maps off the Ice Spire, and a ground each again: snow-crusted heath and
# frozen turf round the draugr barrows; the Glass Mere's lake ice, pale where it
# will bear a walker and dark where it will not; the glacier's white ice split
# with blue crevasses; the grey stone and rime about the Warlord's Howe -- a
# trodden way of slush through all four -- and the Howe's own floor and walls,
# and the trapper's log walls. On a seed of their own, as before.
$script:seed = 20260926
$Size = 16

# Heather under snow: dark tufts showing through a pale crust.
function Add-Tufts($bmp, $tuft, $count) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        Set-Wrapped $bmp $x $y $tuft
        Set-Wrapped $bmp ($x + 1) $y (Shade $tuft 0.15)
        if ((RandInt 2) -eq 0) { Set-Wrapped $bmp $x ($y - 1) (Shade $tuft 0.25) }
    }
}
# Ice seen from above: long pale streaks, and a fine dark line here and there
# -- the old cracks that have frozen over.
function Add-IceLines($bmp, $base, $count, $dark) {
    for ($i = 0; $i -lt $count; $i++) {
        $x = RandInt $Size; $y = RandInt $Size
        $len = 4 + (RandInt 6)
        $c = if ($dark) { Shade $base (-0.22) } else { Shade $base 0.30 }
        for ($k = 0; $k -lt $len; $k++) { Set-Wrapped $bmp ($x + $k) ($y + [int]($k / 4)) $c }
    }
}

$frost = @(
    @{ name = "frost_heath";  rgb = @(206, 214, 222); kind = "heath";  variants = 3 },
    @{ name = "frozen_turf";  rgb = @(176, 188, 182); kind = "grass";  variants = 2 },
    @{ name = "lake_ice";     rgb = @(156, 192, 210); kind = "ice";    variants = 3 },
    @{ name = "lake_ice_dark";rgb = @( 92, 128, 152); kind = "blackice"; variants = 2 },
    @{ name = "glacier_ice";  rgb = @(204, 230, 240); kind = "ice";    variants = 3 },
    @{ name = "blue_ice";     rgb = @(118, 172, 206); kind = "blackice"; variants = 2 },
    @{ name = "rime_stone";   rgb = @(142, 150, 160); kind = "earth";  variants = 3 },
    @{ name = "frost_road";   rgb = @(158, 154, 150); kind = "earth";  variants = 2 }
)
$heather = [System.Drawing.Color]::FromArgb(255, 92, 70, 84)
foreach ($f in $frost) {
    $base = [System.Drawing.Color]::FromArgb(255, $f.rgb[0], $f.rgb[1], $f.rgb[2])
    for ($v = 0; $v -lt $f.variants; $v++) {
        $bmp = New-Tile $base
        switch ($f.kind) {
            "heath"    { Add-Speckle $bmp $base 60 0.05 0.06; Add-Tufts $bmp $heather 5 }
            "grass"    { Add-Speckle $bmp $base 54 0.12 0.08; Add-Blades $bmp $base 8; Add-Grit $bmp $base 2 1 }
            "earth"    { Add-Speckle $bmp $base 62 0.08 0.12; Add-Grit $bmp $base 8 2 }
            "ice"      { Add-Speckle $bmp $base 36 0.06 0.05; Add-IceLines $bmp $base 3 $false; Add-IceLines $bmp $base 1 $true }
            "blackice" { Add-Speckle $bmp $base 30 0.05 0.08; Add-IceLines $bmp $base 2 $false; Add-IceLines $bmp $base 2 $true }
        }
        $name = if ($v -eq 0) { $f.name } else { "$($f.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}

# The Howe's floor and walls: dark flagstones with frost in the joints, and
# the barrow's inside walls of dry-laid stone.
$Size = 32
foreach ($t in @(@{ name = "howe_floor";      rgb = @(78, 82, 90); mortar = @(170, 196, 214); variants = 3 },
                 @{ name = "howe_floor_dark"; rgb = @(62, 66, 74); mortar = @(136, 160, 180); variants = 2 })) {
    for ($v = 0; $v -lt $t.variants; $v++) {
        $bmp = New-Masonry 32 $t.rgb $t.mortar 16 16 0.14 $true
        $name = if ($v -eq 0) { $t.name } else { "$($t.name)_$v" }
        $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $made++
    }
}
$hrime = [System.Drawing.Color]::FromArgb(255, 214, 232, 244)
for ($v = 0; $v -lt 2; $v++) {
    $bmp = New-Masonry 32 @(84, 86, 92) @(36, 38, 44) 8 12 0.22 $true "scatter"
    for ($x = 0; $x -lt 32; $x++) { if ((($x * 7 + $v * 3) % 5) -lt 2) { $bmp.SetPixel($x, 0, $hrime) } }
    $name = if ($v -eq 0) { "howe_wall" } else { "howe_wall_1" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}
$bmp = New-Masonry 32 @(80, 82, 88) @(34, 36, 42) 8 12 0.22 $true "scatter"
for ($y = 24; $y -lt 32; $y++) {
    for ($x = 0; $x -lt 32; $x++) { $bmp.SetPixel($x, $y, (Shade ($bmp.GetPixel($x, $y)) (-0.05 * ($y - 23)))) }
}
$bmp.Save((Join-Path $tiles "howe_wallface.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++
$bmp = New-Masonry 32 @(100, 104, 112) @(44, 46, 52) 16 32 0.10 $true
for ($y = 0; $y -lt 32; $y++) { $bmp.SetPixel(0, $y, $hrime); $bmp.SetPixel(31, $y, $hrime) }
$bmp.Save((Join-Path $tiles "howe_walltop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# The trapper's walls: round logs laid one on another, a dark line of chinking
# between each, and the end-grain of a log every so often.
$logc = [System.Drawing.Color]::FromArgb(255, 92, 62, 40)
$chink = [System.Drawing.Color]::FromArgb(255, 196, 184, 160)
$bmp = New-Tile $logc
for ($y = 0; $y -lt 32; $y++) {
    $band = $y % 8
    for ($x = 0; $x -lt 32; $x++) {
        # Each log round: lit along its top, dark under it, and pale chinking
        # of moss and clay between one log and the next.
        $c = $logc
        if ($band -eq 7) { $c = $chink }
        elseif ($band -eq 0) { $c = Shade $logc 0.30 }
        elseif ($band -eq 1) { $c = Shade $logc 0.16 }
        elseif ($band -ge 5) { $c = Shade $logc (-0.22) }
        if ((($x * 5 + $y * 3) % 13) -eq 0 -and $band -gt 0 -and $band -lt 7) { $c = Shade $c (-0.12) }
        $bmp.SetPixel($x, $y, $c)
    }
    # A log's cut end at the tile's edge, every other course: the corner of
    # the cabin, where the logs cross.
    if (($y % 16) -lt 7) { $bmp.SetPixel(0, $y, (Shade $logc 0.40)); $bmp.SetPixel(1, $y, (Shade $logc 0.24)) }
}
$bmp.Save((Join-Path $tiles "log_wall.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

# --- the house, undyed -----------------------------------------------------------------
# The house at Mossvale takes the colours of whatever totem stands in its ring,
# and the game does that by multiplying the floor and the walls by the totem's
# colours as it draws them. A multiply only ever darkens, so warm oak planks
# could be made redder or browner and never pale: Hoarfang's room would have
# been a muddy teal. These are the same boards and the same timber-framed
# plaster as plank_floor and plaster_wall_warm, in no colour at all -- pale
# grey, so the totem's colour is what they come out as. A little more grain in
# the boards than the oak has, because a board with no colour of its own has
# only its grain to say it is wood.
#
# Last, and on a seed of their own, as ever: nothing above moves.
$script:seed = 20260927
$Size = 32
for ($v = 0; $v -lt 3; $v++) {
    $bmp = New-Masonry 32 @(226, 224, 220) @(132, 130, 128) 8 32 0.07 $true "scatter"
    # Grain: short runs a shade darker along the boards, never across a joint.
    for ($i = 0; $i -lt 9; $i++) {
        $x = RandInt 32
        $y = 1 + 8 * (RandInt 4) + (RandInt 5)
        $len = 3 + (RandInt 6)
        for ($k = 0; $k -lt $len; $k++) {
            $c = $bmp.GetPixel((($x + $k) % 32), $y)
            if ($c.R -gt 150) { Set-Wrapped $bmp ($x + $k) $y (Shade $c (-0.07)) }
        }
    }
    $name = if ($v -eq 0) { "plank_floor_pale" } else { "plank_floor_pale_$v" }
    $bmp.Save((Join-Path $tiles "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $made++
}
# The timber is a mid grey rather than dark, so under a dark totem's colour the
# frame is still a shade darker than the plaster between it and not black.
$bmp = New-Plaster 32 @(240, 238, 234) @(112, 108, 104)
$bmp.Save((Join-Path $tiles "plaster_wall_pale.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$made++

Write-Host "$made ground tiles written to assets/tiles/" -ForegroundColor Green
