# =============================================================================
#  DreamQuest - application icon
#
#  The title painting, art/dreamquest_cover.png, is the game's own art rather
#  than CraftPix content, so unlike everything under assets/ it lives in the
#  repository: the main menu draws it straight from there, cropped to whatever
#  shape the window happens to be, and needs no pre-scaled copy.
#
#  What it does need is an icon, and this makes both kinds from the same
#  painting:
#
#     art/app_icon.png     loaded at startup for the window and the taskbar
#     art/dreamquest.ico   built into the .exe, so Explorer shows it too
#
#  Both are a square off the middle of the painting -- the hilt inside the
#  crescent -- rather than the whole thing, because the whole thing at 16px is
#  a blue smudge.
#
#  Usage:
#     .\tools\make_titleart.ps1
#     .\tools\make_titleart.ps1 -Source art\other.png
# =============================================================================

param(
    [string]$Source = "art\dreamquest_cover.png",
    # The square taken for the icon, as a fraction of the painting's width, and
    # how far down it starts as a fraction of its height.
    [double]$IconCrop = 0.52,
    [double]$IconTop  = 0.05
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot\..
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Source)) { throw "Source art not found: $Source" }

$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
try {
    $side = [int]([math]::Round($src.Width * $IconCrop))
    $sx   = [int](($src.Width - $side) / 2)
    $sy   = [int]([math]::Round($src.Height * $IconTop))
    if ($sy + $side -gt $src.Height) { $sy = $src.Height - $side }

    function New-Square([int]$size) {
        $bmp = New-Object System.Drawing.Bitmap($size, $size)
        $g   = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.DrawImage($script:src,
                (New-Object System.Drawing.Rectangle(0, 0, $size, $size)),
                (New-Object System.Drawing.Rectangle($script:sx, $script:sy, $script:side, $script:side)),
                [System.Drawing.GraphicsUnit]::Pixel)
        } finally { $g.Dispose() }
        return $bmp
    }

    $icon256 = New-Square 256
    $icon256.Save((Join-Path (Get-Location) "art\app_icon.png"),
                  [System.Drawing.Imaging.ImageFormat]::Png)
    $icon256.Dispose()
    Write-Host "  app_icon.png        256x256"

    # An .ico is a tiny header, one directory entry per size, and the images
    # themselves. The entries are stored as PNG, which Windows has accepted
    # since Vista and which keeps the 256px one from dominating the file.
    $sizes = @(16, 32, 48, 64, 128, 256)
    $blobs = @()
    foreach ($s in $sizes) {
        $bmp = New-Square $s
        $ms  = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $blobs += ,$ms.ToArray()
        $ms.Dispose(); $bmp.Dispose()
    }

    $out = New-Object System.IO.MemoryStream
    $w   = New-Object System.IO.BinaryWriter($out)
    $w.Write([uint16]0)              # reserved
    $w.Write([uint16]1)              # type: icon
    $w.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]
        $dim = [byte]($(if ($s -ge 256) { 0 } else { $s }))   # 0 means 256
        $w.Write($dim); $w.Write($dim)
        $w.Write([byte]0)            # palette entries
        $w.Write([byte]0)            # reserved
        $w.Write([uint16]1)          # colour planes
        $w.Write([uint16]32)         # bits per pixel
        $w.Write([uint32]$blobs[$i].Length)
        $w.Write([uint32]$offset)
        $offset += $blobs[$i].Length
    }
    foreach ($b in $blobs) { $w.Write($b) }
    $w.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path (Get-Location) "art\dreamquest.ico"), $out.ToArray())
    $w.Dispose(); $out.Dispose()
    Write-Host ("  dreamquest.ico      {0}" -f ($sizes -join ', '))
} finally { $src.Dispose() }

Write-Host "Icons written to art\" -ForegroundColor Green
