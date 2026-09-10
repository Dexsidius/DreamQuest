# =============================================================================
#  make_character.ps1 - renders the original player character and reduces the
#  sheets to the size the game loads.
#
#      .\tools\make_character.ps1                  # render, then convert
#      .\tools\make_character.ps1 -SkipRender      # convert existing renders
#      .\tools\make_character.ps1 -Only walk,run   # a couple of clips
#
#  tools/blender_character.py renders each clip as five layer sheets at four
#  times the final size. Here they are box-downsampled and their alpha cut to
#  on-or-off.
#
#  Deliberately no colour flattening and no outline pass, unlike the furniture
#  in make_props.ps1. Those exist to stop a smooth render looking out of place
#  beside hand-drawn scenery. A character is different: it is the thing the eye
#  follows, it is recoloured at runtime by whatever armour is worn, and an
#  outline baked into the body layer would survive that tint as a black rim.
# =============================================================================

param(
    [switch]$SkipRender,
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string[]]$Only = @(),
    [string]$Name = "player_hero"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root    = Split-Path $PSScriptRoot -Parent
$renders = Join-Path $root "assets\_render\character"
$outDir  = Join-Path $root "assets\characters\$Name\layers"

# Frame counts have to match CLIPS in blender_character.py; the check below
# says so out loud rather than silently producing a squashed sheet.
$clipFrames = @{
    idle = 6; walk = 6; run = 8; attack = 6; jump = 6; hurt = 4; death = 6
}

if (-not $SkipRender) {
    if (-not (Test-Path $Blender)) {
        throw "Blender not found at $Blender. Pass -Blender, or -SkipRender to convert existing renders."
    }
    Write-Host "Rendering character in Blender ..." -ForegroundColor Cyan
    $args = @("--background", "--python", (Join-Path $PSScriptRoot "blender_character.py"))
    if ($Only.Count -gt 0) { $args += @("--") + $Only }
    # Blender writes progress to stderr, which Windows PowerShell turns into
    # error records that "Stop" treats as fatal. Only the exit code decides.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Blender @args | Where-Object { $_ -match "^sheet " } | Write-Host
    } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Convert-Sheet($src, $dest, $cols, $rows) {
    $bmp = [System.Drawing.Bitmap]::FromFile($src)
    try {
        $w = 64 * $cols
        $h = 64 * $rows
        if ($bmp.Width % $w -ne 0) {
            throw ("{0}: render is {1}px wide, not a whole multiple of {2}" -f
                   (Split-Path $src -Leaf), $bmp.Width, $w)
        }
        $factor = [int]($bmp.Width / $w)

        # Read the render.
        $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $src_bytes = New-Object byte[] ($data.Stride * $bmp.Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $src_bytes, 0, $src_bytes.Length)
            $stride = $data.Stride
        } finally { $bmp.UnlockBits($data) }

        # Box downsample, weighting colour by alpha so a mostly-empty edge
        # block takes the colour of the pixels that were actually there
        # instead of being dragged toward black by the transparent ones.
        $out = New-Object byte[] ($w * 4 * $h)
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $b = 0.0; $g = 0.0; $r = 0.0; $a = 0.0; $wsum = 0.0
                for ($sy = 0; $sy -lt $factor; $sy++) {
                    $rowBase = ($y * $factor + $sy) * $stride
                    for ($sx = 0; $sx -lt $factor; $sx++) {
                        $i = $rowBase + ($x * $factor + $sx) * 4
                        $sa = [double]$src_bytes[$i + 3]
                        $a += $sa
                        if ($sa -gt 0) {
                            $b += [double]$src_bytes[$i]     * $sa
                            $g += [double]$src_bytes[$i + 1] * $sa
                            $r += [double]$src_bytes[$i + 2] * $sa
                            $wsum += $sa
                        }
                    }
                }
                $o = ($y * $w + $x) * 4
                if ($wsum -gt 0) {
                    $out[$o]     = [byte][math]::Min(255, [int]($b / $wsum))
                    $out[$o + 1] = [byte][math]::Min(255, [int]($g / $wsum))
                    $out[$o + 2] = [byte][math]::Min(255, [int]($r / $wsum))
                }
                # Hard alpha. A character with soft edges reads as blurred
                # against pixel-art ground, and the half-transparent fringe
                # doubles up wherever two layers overlap.
                $mean = $a / ($factor * $factor)
                if ($mean -ge 110) {
                    $out[$o + 3] = 255
                } else {
                    $out[$o] = 0; $out[$o + 1] = 0; $out[$o + 2] = 0; $out[$o + 3] = 0
                }
            }
        }

        $dst = New-Object System.Drawing.Bitmap -ArgumentList $w, $h,
               ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $drect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $w, $h
        $ddata = $dst.LockBits($drect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                               [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $row = New-Object byte[] ($w * 4)
            for ($y = 0; $y -lt $h; $y++) {
                [Array]::Copy($out, $y * $w * 4, $row, 0, $w * 4)
                [Runtime.InteropServices.Marshal]::Copy(
                    $row, 0, [IntPtr]($ddata.Scan0.ToInt64() + $y * $ddata.Stride), $row.Length)
            }
        } finally { $dst.UnlockBits($ddata) }
        $dst.Save($dest, [System.Drawing.Imaging.ImageFormat]::Png)
        $dst.Dispose()
    } finally { $bmp.Dispose() }
}

$done = 0
foreach ($file in (Get-ChildItem $renders -Filter *.png -File -EA SilentlyContinue)) {
    # Files arrive named "<clip>_<order>_<layer>.png".
    if ($file.BaseName -notmatch '^([a-z]+)_(\d)_(.+)$') { continue }
    $clip = $matches[1]
    if ($Only.Count -gt 0 -and $Only -notcontains $clip) { continue }
    if (-not $clipFrames.ContainsKey($clip)) {
        Write-Warning "  ! $clip has no frame count in make_character.ps1; skipped"
        continue
    }
    Convert-Sheet $file.FullName (Join-Path $outDir $file.Name) $clipFrames[$clip] 4
    $done++
}

# --- flattened sheets ---------------------------------------------------------
# One combined image per clip alongside the layers. tools/make_sprites_json.ps1
# discovers characters by their top-level sheets and only then looks for a
# layers/ folder, and the menu preview draws the flat version because a
# character-select screen has no business running a paperdoll.
$flatDir = Join-Path $root "assets\characters\$Name"
$flat = 0
foreach ($clip in $clipFrames.Keys) {
    if ($Only.Count -gt 0 -and $Only -notcontains $clip) { continue }
    $parts = Get-ChildItem $outDir -Filter "${clip}_*.png" -File -EA SilentlyContinue |
             Sort-Object Name
    if (-not $parts) { continue }

    $first = [System.Drawing.Bitmap]::FromFile($parts[0].FullName)
    $w = $first.Width; $h = $first.Height
    $first.Dispose()

    $sheet = New-Object System.Drawing.Bitmap -ArgumentList $w, $h,
             ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    foreach ($part in $parts) {
        $im = [System.Drawing.Bitmap]::FromFile($part.FullName)
        $g.DrawImage($im, 0, 0, $w, $h)
        $im.Dispose()
    }
    $g.Dispose()
    $sheet.Save((Join-Path $flatDir "$clip.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    $flat++
}

Write-Host ("`n{0} layer sheets and {1} flattened sheets written to assets/characters/{2}/" -f
            $done, $flat, $Name) -ForegroundColor Green
