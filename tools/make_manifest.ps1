# =============================================================================
#  Writes data/asset_manifest.json: the pixel size of every imported image,
#  plus a colour family for the loose ground decals.
#
#  The map generator needs both. Size, so art is placed at the scale it was
#  drawn at rather than stretched to a grid. Colour, because the CraftPix
#  ground decals ship as one sheet covering six terrain palettes at once, and
#  a teal patch dropped on a green field looks exactly as wrong as it sounds.
#
#  Run by tools/import_assets.ps1; safe to run on its own afterwards.
# =============================================================================

param([string]$Root = "")

$ErrorActionPreference = "Stop"
if ($Root -eq "") { $Root = Join-Path $PSScriptRoot ".." }
Set-Location $Root

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path "assets")) { throw "No assets/ - run tools/import_assets.ps1 first." }

function Get-PngSize($path) {
    $fs = [IO.File]::OpenRead($path)
    try {
        $b = New-Object byte[] 24
        [void]$fs.Read($b, 0, 24)
        return @([BitConverter]::ToInt32($b[19..16], 0), [BitConverter]::ToInt32($b[23..20], 0))
    } finally { $fs.Close() }
}

# Average colour of the pixels that are actually drawn, ignoring transparency.
function Get-AverageColour($path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $data = $bmp.LockBits(
            (New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height),
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
            [long]$r = 0; [long]$g = 0; [long]$b = 0; [int]$n = 0
            for ($y = 0; $y -lt $bmp.Height; $y++) {
                $row = $y * $data.Stride
                for ($x = 0; $x -lt $bmp.Width; $x++) {
                    $i = $row + $x * 4
                    if ($bytes[$i + 3] -gt 200) {
                        $b += $bytes[$i]; $g += $bytes[$i + 1]; $r += $bytes[$i + 2]; $n++
                    }
                }
            }
            if ($n -eq 0) { return $null }
            return @([int]($r / $n), [int]($g / $n), [int]($b / $n))
        } finally { $bmp.UnlockBits($data) }
    } finally { $bmp.Dispose() }
}

# Which terrain a decal belongs on, from its average colour.
function Get-ColourFamily($c) {
    if ($null -eq $c) { return "none" }
    $r, $g, $b = $c[0], $c[1], $c[2]
    if ($r -gt 200 -and $g -gt 200 -and $b -gt 190) { return "snow" }
    if ($b -gt $g -and $b -gt 90)                   { return "water" }
    if ($g -gt ($r * 1.02) -and $b -lt ($g * 0.75)) { return "grass" }
    if ($r -gt $g -and $g -gt $b)                   { return "dirt" }
    return "other"
}

$manifest = [ordered]@{}
$families = [ordered]@{}

foreach ($group in @("tiles", "decor", "objects", "props", "icons/armour")) {
    $dir = Join-Path "assets" ($group -replace '/', '\')
    if (-not (Test-Path $dir)) { continue }

    foreach ($f in (Get-ChildItem $dir -Filter *.png -File | Sort-Object Name)) {
        $key = "$group/$($f.BaseName)"
        $size = Get-PngSize $f.FullName
        $entry = [ordered]@{ w = $size[0]; h = $size[1] }

        # Armour icons carry their own colour so the worn tint matches the art.
        if ($group -eq "icons/armour") {
            $avg = Get-AverageColour $f.FullName
            if ($avg) { $entry["rgb"] = @($avg[0], $avg[1], $avg[2]) }
        }

        # Only the loose decals need classifying; everything else is placed
        # explicitly by name.
        if ($group -eq "decor") {
            $avg = Get-AverageColour $f.FullName
            $family = Get-ColourFamily $avg
            $entry["family"] = $family
            if ($avg) { $entry["rgb"] = @($avg[0], $avg[1], $avg[2]) }

            if (-not $families.Contains($family)) { $families[$family] = @() }
            $families[$family] += $f.BaseName
        }

        $manifest[$key] = $entry
    }
}

$out = [ordered]@{
    assets   = $manifest
    families = $families
}

New-Item -ItemType Directory -Force -Path "data" | Out-Null
[IO.File]::WriteAllText((Join-Path (Get-Location) "data\asset_manifest.json"),
                        ($out | ConvertTo-Json -Depth 6) + "`n")

Write-Host "data/asset_manifest.json: $($manifest.Count) images" -ForegroundColor Green
foreach ($k in $families.Keys) {
    Write-Host ("  {0,-8} {1} decals" -f $k, $families[$k].Count) -ForegroundColor DarkGray
}
