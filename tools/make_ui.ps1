# =============================================================================
#  make_ui.ps1 - draws the HUD fittings that cannot be made of rectangles.
#
#      .\tools\make_ui.ps1
#
#  Everything else in the HUD is drawn in code from rects, which is why it is
#  all square. A round minimap needs a round frame, so it is generated here:
#  a brass bezel lit from the top left, with a groove, rivets, cardinal notches
#  and an amber pip at north. The middle is left transparent for the map to
#  show through, with a soft inner shadow so the map reads as recessed into the
#  plate rather than pasted on top of it.
#
#  The palette is the one in src/ui/ui.h, so the bezel belongs to the same set
#  of furniture as the panels and menus.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$ui   = Join-Path $root "assets\ui"
New-Item -ItemType Directory -Force -Path $ui | Out-Null

$size   = 144          # whole image
$centre = 71.5         # middle of a 144-wide image, in pixel centres
$hole   = 62.0         # map shows through inside this
$brass0 = 63.5         # bezel starts
$brass1 = 70.5         # bezel ends
$edge   = 72.0         # outer dark line ends

function Mix($a, $b, $t) {
    $t = [Math]::Max(0.0, [Math]::Min(1.0, $t))
    return [int]($a + ($b - $a) * $t)
}

$bmp = New-Object System.Drawing.Bitmap -ArgumentList $size, $size,
       ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# Rivets and the north pip sit on the middle of the bezel.
$studR = 67.0
$studs = @(45, 135, 225, 315)

for ($y = 0; $y -lt $size; $y++) {
    for ($x = 0; $x -lt $size; $x++) {
        $dx = $x - $centre
        $dy = $y - $centre
        $r  = [Math]::Sqrt($dx * $dx + $dy * $dy)

        $col = $null

        if ($r -lt $hole) {
            # Inside the glass: clear, but darkened just inside the rim so the
            # map looks sunk into the plate.
            $shade = ($r - ($hole - 4.0)) / 4.0
            if ($shade -gt 0) {
                $col = [System.Drawing.Color]::FromArgb([int](110 * $shade), 8, 6, 5)
            }
        } elseif ($r -lt $brass0) {
            $col = [System.Drawing.Color]::FromArgb(255, 26, 19, 14)      # inner rim
        } elseif ($r -lt $brass1) {
            # Bevel: lit from the top left, with a groove around the middle.
            $nx = $dx / $r
            $ny = $dy / $r
            $lit = (-0.7071 * $nx) + (-0.7071 * $ny)                       # +1 lit, -1 shadow
            $t = ($lit + 1.0) / 2.0
            $rr = Mix 84 196 $t
            $gg = Mix 64 164 $t
            $bb = Mix 34 96  $t
            if ($r -gt 66.4 -and $r -lt 67.6) {                            # groove
                $rr = [int]($rr * 0.72); $gg = [int]($gg * 0.72); $bb = [int]($bb * 0.72)
            }
            $col = [System.Drawing.Color]::FromArgb(255, $rr, $gg, $bb)
        } elseif ($r -lt $edge) {
            $col = [System.Drawing.Color]::FromArgb(255, 30, 22, 15)       # outer line
        }

        if ($col) { $bmp.SetPixel($x, $y, $col) }
    }
}

# Rivets: a lit stud with a dark seat, at the corners of the compass.
foreach ($deg in $studs) {
    $a  = $deg * [Math]::PI / 180.0
    $sx = $centre + [Math]::Cos($a) * $studR
    $sy = $centre + [Math]::Sin($a) * $studR
    for ($y = [int]($sy - 4); $y -le [int]($sy + 4); $y++) {
        for ($x = [int]($sx - 4); $x -le [int]($sx + 4); $x++) {
            if ($x -lt 0 -or $y -lt 0 -or $x -ge $size -or $y -ge $size) { continue }
            $d = [Math]::Sqrt(($x - $sx) * ($x - $sx) + ($y - $sy) * ($y - $sy))
            if ($d -le 1.2) {
                $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 236, 208, 140))
            } elseif ($d -le 2.4) {
                $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 150, 118, 62))
            } elseif ($d -le 3.1) {
                $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 44, 32, 20))
            }
        }
    }
}

# Cardinal marks: a notch cut into the bezel at each quarter, and an amber pip
# at north so the map is obviously north-up.
foreach ($deg in @(0, 90, 180, 270)) {
    $a = $deg * [Math]::PI / 180.0
    for ($k = 0; $k -lt 22; $k++) {
        $rr = 63.0 + $k * 0.35
        $px = [int][Math]::Round($centre + [Math]::Cos($a) * $rr)
        $py = [int][Math]::Round($centre + [Math]::Sin($a) * $rr)
        if ($px -lt 0 -or $py -lt 0 -or $px -ge $size -or $py -ge $size) { continue }
        $c = if ($deg -eq 270) { [System.Drawing.Color]::FromArgb(255, 242, 200, 96) }
             else { [System.Drawing.Color]::FromArgb(255, 40, 29, 18) }
        $bmp.SetPixel($px, $py, $c)
        if ($deg -eq 270 -and $k -lt 10) {
            $bmp.SetPixel($px + 1, $py, $c)
        }
    }
}

$bmp.Save((Join-Path $ui "minimap_ring.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "  minimap_ring.png ($size x $size) written to assets\ui"
