# =============================================================================
#  make_light_pools.ps1 - the pools of light on the prologue's floors.
#
#      .\tools\make_light_pools.ps1
#
#    grate_light        96x64  the cold green-grey light falling into the cell
#                              from the barred grate in its ceiling, with the
#                              shadows of the bars across it
#    grate_light_dream  96x64  the same in the Reverie, in pale violet
#    glass_light        64x48  what one of the foyer's stained-glass windows
#                              throws across the floor: patches of its red,
#                              blue, gold and violet, the leading dark between
#                              them, the flat end at the wall (the left) and
#                              the pointed head of the arch out on the floor
#
#  Flat overlays, laid on the floor under everyone, and mostly transparent --
#  which is why they are drawn here rather than rendered: a prop goes through
#  make_props.ps1, which cuts alpha to all or nothing. Each is a field (how
#  much light lands on each pixel) cut into a few flat steps of alpha and two
#  colours, the way make_effects.ps1 cuts its flames, so it reads as pixel art
#  and not as a blur. Deterministic: a rerun writes the same pixels.
#
#  Written to assets/props/; run tools/make_manifest.ps1 after it.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $root "assets\props"
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Smooth($e0, $e1, $x) {
    $t = [math]::Max(0.0, [math]::Min(1.0, ($x - $e0) / ($e1 - $e0)))
    return $t * $t * (3.0 - 2.0 * $t)
}

# A little fixed grain, so an edge is not a perfect curve: pixel art's edges
# are drawn, not computed.
function Grain($x, $y, $seed) {
    $h = (([long]$x * 73856093) -bxor ([long]$y * 19349663) -bxor ([long]$seed * 83492791)) -band 0x7FFFFFFF
    $h = ($h * 1103515245 + 12345) -band 0x7FFFFFFF
    return ($h % 1000) / 1000.0
}

# How far inside a rounded rectangle a point is, as 0 (out) .. 1 (well in).
function Rect-Light($x, $y, $cx, $cy, $hw, $hh, $soft) {
    $dx = [math]::Abs($x - $cx) - ($hw - $soft)
    $dy = [math]::Abs($y - $cy) - ($hh - $soft)
    $ox = [math]::Max($dx, 0.0); $oy = [math]::Max($dy, 0.0)
    $d = [math]::Sqrt($ox * $ox + $oy * $oy)
    return 1.0 - (Smooth 0.0 $soft $d)
}

# Cut a field value to one of a few steps, and pick a colour for it.
function Put-Light($bmp, $x, $y, $v, $maxA, $core, $rim) {
    $steps = 4
    $q = [math]::Floor($v * $steps + 0.5) / $steps
    if ($q -le 0.0) { return }
    $a = [int]($maxA * $q)
    $c = if ($q -ge 0.75) { $core } else { $rim }
    $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, $c[0], $c[1], $c[2]))
}

function New-Clear($w, $h) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::Transparent) } }
    return $bmp
}

# --- the grate's light ---------------------------------------------------------------
# The grate is a grid: four bars one way and two the other. Its light is the
# same grid laid on the floor, a little blurred by the height it falls, so the
# bars' shadows are bands of less light, not black.
function Grate-Light($name, $core, $rim, $maxA) {
    $W = 96; $H = 64
    $bmp = New-Clear $W $H
    $cx = 47.5; $cy = 31.5; $hw = 40.0; $hh = 25.0; $soft = 9.0
    $left = $cx - $hw + 4; $right = $cx + $hw - 4
    $top = $cy - $hh + 3; $bottom = $cy + $hh - 3
    for ($y = 0; $y -lt $H; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $v = Rect-Light $x $y $cx $cy $hw $hh $soft
            if ($v -le 0.0) { continue }
            # The bars' shadows.
            $u = ($x - $left) / ($right - $left)
            # (Not $w: PowerShell's names ignore case, and $W is the width.)
            $gv = ($y - $top) / ($bottom - $top)
            $shade = 1.0
            for ($k = 1; $k -le 4; $k++) {
                $d = [math]::Abs($u - $k / 5.0) * ($right - $left)
                if ($d -lt 2.6) { $shade = [math]::Min($shade, 0.30 + 0.70 * (Smooth 1.0 2.6 $d)) }
            }
            foreach ($k in 1, 2) {
                $d = [math]::Abs($gv - $k / 3.0) * ($bottom - $top)
                if ($d -lt 2.2) { $shade = [math]::Min($shade, 0.36 + 0.64 * (Smooth 0.8 2.2 $d)) }
            }
            # Brighter in the middle of the pool than at its edges.
            $mid = 1.0 - 0.25 * ([math]::Pow(($x - $cx) / $hw, 2) + [math]::Pow(($y - $cy) / $hh, 2))
            $v = $v * $shade * $mid + ((Grain $x $y 7) - 0.5) * 0.035
            Put-Light $bmp $x $y $v $maxA $core $rim
        }
    }
    $bmp.Save((Join-Path $out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  {0,-18} {1}x{2}" -f $name, $W, $H)
}

Grate-Light "grate_light"       @(206, 226, 210) @(150, 176, 160) 120
Grate-Light "grate_light_dream" @(226, 208, 250) @(170, 148, 214) 120

# --- the stained glass's light -----------------------------------------------------------
# The window's lancet laid along the floor away from the wall: the flat foot
# of it at the wall on the left, the pointed head out to the right. Its panes
# land as patches -- the window's rows run along the pool, its three columns
# across it -- and the lead between them as thin bands of less light.
$W = 64; $H = 48
$bmp = New-Clear $W $H
$cy = 23.5
$x0 = 3.0; $x1 = 61.0          # the foot at the wall, the point of the arch
$half = 15.0                   # half the width of the pool
$knee = 40.0                   # where the arch begins to close
$soft = 5.0
$panes = @(
    @(@(64, 92, 222), @(222, 60, 66), @(64, 92, 222)),
    @(@(222, 60, 66), @(244, 196, 84), @(222, 60, 66)),
    @(@(150, 92, 214), @(222, 60, 66), @(150, 92, 214)),
    @(@(222, 60, 66), @(244, 196, 84), @(222, 60, 66)),
    @(@(64, 92, 222), @(150, 92, 214), @(64, 92, 222)),
    @(@(244, 196, 84), @(64, 92, 222), @(244, 196, 84)),
    @(@(150, 92, 214), @(244, 196, 84), @(150, 92, 214))
)
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        # Half-width of the lancet at this x: straight to the knee, then the
        # two arcs of the pointed head closing to the point.
        if ($x -lt $x0) { continue }
        if ($x -le $knee) { $hw = $half }
        else {
            $t = ($x - $knee) / ($x1 - $knee)
            if ($t -ge 1.0) { continue }
            $hw = $half * [math]::Sqrt([math]::Max(0.0, 1.0 - $t * $t)) * (1.0 - 0.35 * $t)
        }
        $d = [math]::Abs($y - $cy) - $hw
        $v = 1.0 - (Smooth (-$soft) 0.0 $d)
        $v = $v * (Smooth ($x0 - 1) ($x0 + 3) $x)
        if ($v -le 0.0) { continue }
        # Which pane: rows along x, columns across y.
        $row = [int][math]::Floor(($x - $x0) / (($x1 - $x0) / $panes.Count))
        $row = [math]::Max(0, [math]::Min($panes.Count - 1, $row))
        $colF = ($y - ($cy - $half)) / (2 * $half) * 3
        $col = [math]::Max(0, [math]::Min(2, [int][math]::Floor($colF)))
        # Light, not paint: each pane's colour lifted well towards white.
        $p = $panes[$row][$col]
        $c = @([int]($p[0] + (255 - $p[0]) * 0.38), [int]($p[1] + (255 - $p[1]) * 0.38), [int]($p[2] + (255 - $p[2]) * 0.38))
        # The lead between panes.
        $fx = (($x - $x0) / (($x1 - $x0) / $panes.Count)) % 1.0
        $fy = $colF % 1.0
        if ($fx -lt 0.12 -or $fy -lt 0.08 -or $fy -gt 0.94) { $v = $v * 0.62 }
        # Weaker the further it falls from the window.
        $v = $v * (1.0 - 0.35 * ($x - $x0) / ($x1 - $x0))
        $v = $v + ((Grain $x $y 11) - 0.5) * 0.035
        $steps = 4
        $q = [math]::Floor($v * $steps + 0.5) / $steps
        if ($q -le 0.0) { continue }
        $a = [int](84 * $q)
        $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, $c[0], $c[1], $c[2]))
    }
}
$bmp.Save((Join-Path $out "glass_light.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("  {0,-18} {1}x{2}" -f "glass_light", $W, $H)
Write-Host "3 light pools written to assets/props/" -ForegroundColor Green
