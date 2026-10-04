# =============================================================================
#  make_act1_overlays.ps1 - Act I's soft-edged overlays.
#
#      .\tools\make_act1_overlays.ps1
#
#    dragon_shadow   640x320  the shadow of something vast and winged passing
#                             over Havenbrook at the end of the chapter (scene
#                             50): seen from above, flying up the picture --
#                             head at the top, wings spread across it, the
#                             tail down to the bottom. Shadow only, pure black,
#                             soft at the edges; the game draws it at about
#                             half opacity and slides it across the town.
#
#  Mostly transparent and soft-edged, which a render cut to on-or-off alpha by
#  make_props.ps1 cannot be: so it is drawn here, as make_light_pools.ps1 draws
#  the prologue's pools of light. The outline is a path, filled with
#  antialiasing at twice the size; the softness is that fill taken down to a
#  quarter and back up again with a bicubic filter; and the alpha is then cut
#  to a few flat steps, so the edge reads as pixel art and not as a blur.
#  Deterministic: a rerun writes the same pixels.
#
#  Written to assets/props/; run tools/make_manifest.ps1 after it.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $root "assets\props"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# --- the dragon, seen from below its own shadow -------------------------------------
# The right half of the outline in a 640 x 320 picture, from the tip of the
# snout to the hip; the left half is its mirror. A dragon and not a bat: a
# horned head on a long neck, and a long tail swinging in an S down to the
# foot of the picture with its spade. The wing is a bat's: the arm up and out
# to the wrist, the long outer finger to the tip, and the membrane scalloped
# in between the fingers' ends back to the hip.
$W = 640; $H = 320
$right = @(
    @(320, 4), @(326, 12), @(330, 26),                    # the snout
    @(338, 22), @(350, 16), @(342, 30), @(334, 38),       # a horn, swept back and out
    @(330, 52), @(329, 72), @(331, 92),                   # the long neck
    @(346, 110),                                          # the shoulder
    @(384, 96), @(430, 76), @(474, 58),                   # the arm, to the wrist
    @(552, 52), @(634, 66)                                # the outer finger, to the tip
)
# The fingers' ends along the trailing edge, and how deep the membrane is cut
# in between each and the next (towards the wrist).
$fingers = @(@(634, 66), @(606, 132), @(538, 168), @(458, 180), @(368, 172))
$scallop = 0.32
$wrist = @(474, 58)
$hind = @(@(356, 184), @(364, 198), @(368, 210), @(356, 206), @(346, 196))   # a hind foot, tucked

function Get-Outline {
    $pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    function Add($x, $y) { $pts.Add((New-Object System.Drawing.PointF -ArgumentList $x, $y)) }
    foreach ($p in $right) { Add $p[0] $p[1] }
    for ($i = 0; $i -lt $fingers.Count - 1; $i++) {
        $a = $fingers[$i]; $b = $fingers[$i + 1]
        $mx = ($a[0] + $b[0]) / 2.0; $my = ($a[1] + $b[1]) / 2.0
        $cx = $mx + ($wrist[0] - $mx) * $scallop; $cy = $my + ($wrist[1] - $my) * $scallop
        for ($k = 1; $k -le 6; $k++) {
            $t = $k / 6.0
            Add ((1 - $t) * (1 - $t) * $a[0] + 2 * (1 - $t) * $t * $cx + $t * $t * $b[0]) `
                ((1 - $t) * (1 - $t) * $a[1] + 2 * (1 - $t) * $t * $cy + $t * $t * $b[1])
        }
    }
    foreach ($p in $hind) { Add $p[0] $p[1] }
    $front = $pts.ToArray()
    # The tail: down its right edge, round the spade, up its left edge. Its
    # middle swings in an S; it thins to the spade.
    $tailR = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    $tailL = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    for ($y = 196; $y -le 296; $y += 10) {
        $f = ($y - 196) / 100.0
        $c = 320 + 16 * [math]::Sin($f * [math]::PI * 1.5)
        $w = 14 - 10 * $f
        $tailR.Add((New-Object System.Drawing.PointF -ArgumentList ($c + $w), $y))
        $tailL.Add((New-Object System.Drawing.PointF -ArgumentList ($c - $w), $y))
    }
    $end = 320 + 16 * [math]::Sin([math]::PI * 1.5)
    $all = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($p in $front) { $all.Add($p) }
    foreach ($p in $tailR) { $all.Add($p) }
    foreach ($q in @(@(14, 302), @(0, 318), @(-14, 302))) {
        $all.Add((New-Object System.Drawing.PointF -ArgumentList ($end + $q[0]), $q[1]))
    }
    for ($i = $tailL.Count - 1; $i -ge 0; $i--) { $all.Add($tailL[$i]) }
    # And back up the left side, the mirror of the front, leaving out the
    # point on the middle line.
    for ($i = $front.Count - 1; $i -ge 0; $i--) {
        $p = $front[$i]
        if ([math]::Abs($p.X - 320) -lt 0.5) { continue }
        $all.Add((New-Object System.Drawing.PointF -ArgumentList (640 - $p.X), $p.Y))
    }
    return $all.ToArray()
}

function New-Bitmap($w, $h) {
    return New-Object System.Drawing.Bitmap -ArgumentList $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
}

function Resample($src, $w, $h) {
    $dst = New-Bitmap $w $h
    $g = [System.Drawing.Graphics]::FromImage($dst)
    try {
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $attr = New-Object System.Drawing.Imaging.ImageAttributes
        $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $w, $h), 0, 0, $src.Width, $src.Height,
                     [System.Drawing.GraphicsUnit]::Pixel, $attr)
    } finally { $g.Dispose() }
    return $dst
}

# 1. The outline, filled at twice the size with antialiasing.
$big = New-Bitmap ($W * 2) ($H * 2)
$g = [System.Drawing.Graphics]::FromImage($big)
try {
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $g.ScaleTransform(2.0, 2.0)
    $outline = Get-Outline
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddClosedCurve($outline, 0.25)
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))), $path)
} finally { $g.Dispose() }

# 2. Soft: down to a quarter of the final size and back.
$small = Resample $big ([int]($W / 4)) ([int]($H / 4))
$soft = Resample $small $W $H
$big.Dispose(); $small.Dispose()

# 3. The alpha cut to a few flat steps, the colour pure black.
$rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
$data = $soft.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
                       [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    $bytes = New-Object byte[] ($data.Stride * $H)
    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $steps = @(0, 72, 150, 216, 255)
    for ($i = 0; $i -lt $bytes.Length; $i += 4) {
        $a = [int]$bytes[$i + 3]
        $q = if ($a -lt 24) { 0 } elseif ($a -lt 96) { 1 } elseif ($a -lt 168) { 2 } elseif ($a -lt 232) { 3 } else { 4 }
        $bytes[$i] = 0; $bytes[$i + 1] = 0; $bytes[$i + 2] = 0
        $bytes[$i + 3] = [byte]$steps[$q]
    }
    [Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
} finally { $soft.UnlockBits($data) }

$path = Join-Path $out "dragon_shadow.png"
$soft.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$soft.Dispose()
Write-Host "  dragon_shadow  ${W}x${H}"
