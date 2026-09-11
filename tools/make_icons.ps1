# =============================================================================
#  make_icons.ps1 - draws the original item icons in tools/icons.txt.
#
#      .\tools\make_icons.ps1
#
#  The inventory icons are cut from one CraftPix sheet by cell, and that sheet
#  has no log, bow, staff, hide, ore or roast on it. Those items had been given
#  whichever cell was nearest in spirit, and playing the game showed how far
#  that was: the Training Bow was a blue sword, Raw Hide and the Leather Jerkin
#  were a boot, both staves were an eye on a green tile, Roast Boar was a blue
#  lump, and logs and ore were metal ingots.
#
#  So these are drawn by hand as text -- one character per pixel -- and painted
#  here. Text rather than PNGs so they can be read, diffed and touched up
#  without an image editor, the same way the tiles and props are generated
#  rather than stored.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root   = Split-Path $PSScriptRoot -Parent
$source = Join-Path $PSScriptRoot "icons.txt"
$icons  = Join-Path $root "assets\icons"
New-Item -ItemType Directory -Force -Path $icons | Out-Null

function Write-Icon($icon) {
    $name = $icon.name
    if ($icon.rows.Count -ne 16) { throw "icon '$name' has $($icon.rows.Count) rows, not 16" }

    $bmp = New-Object System.Drawing.Bitmap -ArgumentList 16, 16,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt 16; $y++) {
            $row = $icon.rows[$y]
            if ($row.Length -ne 16) { throw "icon '$name' row $y is $($row.Length) wide, not 16" }
            for ($x = 0; $x -lt 16; $x++) {
                $ch = [string]$row[$x]
                if ($ch -eq '.') {
                    $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                    continue
                }
                # Hashtables ignore case by default; the palette must not.
                if (-not $icon.palette.ContainsKey($ch)) { throw "icon '$name' uses undefined colour '$ch'" }
                $bmp.SetPixel($x, $y, $icon.palette[$ch])
            }
        }
        $bmp.Save((Join-Path $icons "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bmp.Dispose()
    }
}

$current = $null
$made = 0
foreach ($line in [IO.File]::ReadAllLines($source)) {
    if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }

    if ($line.StartsWith('@')) {
        if ($current) { Write-Icon $current; $made++ }
        $current = @{
            name    = $line.Substring(1).Trim()
            palette = New-Object 'System.Collections.Generic.Dictionary[string,System.Drawing.Color]' ([StringComparer]::Ordinal)
            rows    = New-Object System.Collections.Generic.List[string]
        }
        continue
    }
    if (-not $current) { throw "icons.txt: '$line' comes before any @name" }

    if ($current.rows.Count -eq 0 -and $line -match '^(.)=([0-9a-fA-F]{6})$') {
        $hex = $Matches[2]
        $current.palette[$Matches[1]] = [System.Drawing.Color]::FromArgb(255,
            [Convert]::ToInt32($hex.Substring(0, 2), 16),
            [Convert]::ToInt32($hex.Substring(2, 2), 16),
            [Convert]::ToInt32($hex.Substring(4, 2), 16))
    } else {
        $current.rows.Add($line)
    }
}
if ($current) { Write-Icon $current; $made++ }

Write-Host "  $made icons drawn into assets\icons"
