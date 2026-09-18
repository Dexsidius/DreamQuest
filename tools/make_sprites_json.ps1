# =============================================================================
#  Writes data/sprites.json from whatever is actually in assets/characters/.
#
#  Every CraftPix character sheet in this project is laid out the same way: a
#  square frame, four rows (down, left, right, up), one column per frame. So
#  the frame size is the sheet height over four, and the frame count is the
#  width over that.
#
#  Except the rows are not always the same length. Several sheets are padded to
#  the width of their longest row: the player's idle has twelve frames facing
#  down, left and right, but only four facing up, and the remaining eight cells
#  of that row are empty. Playing all twelve makes the character vanish for two
#  thirds of the loop. So this also measures how many leading frames of each
#  row actually contain pixels and records that per row.
#
#  Run by tools/import_assets.ps1; safe to run on its own afterwards.
# =============================================================================

param([string]$Root = "")

$ErrorActionPreference = "Stop"
if ($Root -eq "") { $Root = Join-Path $PSScriptRoot ".." }
Set-Location $Root

Add-Type -AssemblyName System.Drawing

$charDir = "assets\characters"
if (-not (Test-Path $charDir)) { throw "No $charDir - run tools/import_assets.ps1 first." }

# Frame size, column count, and the number of non-empty leading frames per row.
function Get-SheetLayout($path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $frame = [int]($bmp.Height / 4)
        if ($frame -le 0) { return $null }
        $cols = [int]($bmp.Width / $frame)
        if ($cols -le 0) { return $null }

        $data = $bmp.LockBits(
            (New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height),
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)

            $rowFrames = @()
            for ($r = 0; $r -lt 4; $r++) {
                $last = -1
                for ($c = 0; $c -lt $cols; $c++) {
                    $any = $false
                    # Every other pixel is plenty to tell a drawn frame from an
                    # empty one, and it keeps this quick over ~70 sheets.
                    for ($y = $r * $frame; $y -lt ($r + 1) * $frame -and -not $any; $y += 2) {
                        $rowBase = $y * $data.Stride
                        for ($x = $c * $frame; $x -lt ($c + 1) * $frame; $x += 2) {
                            if ($bytes[$rowBase + $x * 4 + 3] -gt 16) { $any = $true; break }
                        }
                    }
                    if ($any) { $last = $c }
                }
                # A row with nothing in it still needs one frame to draw.
                $rowFrames += [math]::Max(1, $last + 1)
            }
            return @{ frame = $frame; cols = $cols; rowFrames = $rowFrames }
        } finally { $bmp.UnlockBits($data) }
    } finally { $bmp.Dispose() }
}

# Playback speed and whether a clip repeats. Anything not listed loops at 10fps.
$clipRules = @{
    idle   = @{ fps =  8; loop = $true  }
    walk   = @{ fps = 10; loop = $true  }
    run    = @{ fps = 13; loop = $true  }
    sprint = @{ fps = 16; loop = $true  }
    attack = @{ fps = 16; loop = $false }
    # A spear's strike is a one-shot swing like any other. It was missing here
    # and fell through to the default, which loops.
    thrust = @{ fps = 16; loop = $false }
    # Rushing Strike's leap: eight frames over roughly half a second.
    rush   = @{ fps = 15; loop = $false }
    # The combos, each a one-shot swing of its own.
    crush    = @{ fps = 15; loop = $false }
    cleave   = @{ fps = 16; loop = $false }
    backhand = @{ fps = 18; loop = $false }
    spin     = @{ fps = 16; loop = $false }
    # A held guard breathes slowly.
    block  = @{ fps = 6;  loop = $true  }
    chop   = @{ fps = 10; loop = $true  }
    mine   = @{ fps = 9;  loop = $true  }
    fish   = @{ fps = 6;  loop = $true  }
    gather = @{ fps = 8;  loop = $true  }
    hurt   = @{ fps = 12; loop = $false }
    death  = @{ fps =  9; loop = $false }
    jump   = @{ fps = 12; loop = $false }
}

# Filename slot -> the slot the engine knows about. The engine decides what to
# tint or hide by slot, so a weapon layer can be recoloured or dropped without
# touching the body.
$slotNames = @{
    'shadow'      = 'shadow'
    'sword_back'  = 'weapon_back'
    'body'        = 'body'
    'head'        = 'head'
    'sword_front' = 'weapon_front'
    'swing'       = 'effect'
}

$out = [ordered]@{}
$ragged = 0
$layered = 0

foreach ($dir in (Get-ChildItem $charDir -Directory | Sort-Object Name)) {
    $sheets = Get-ChildItem $dir.FullName -Filter *.png | Sort-Object Name
    if ($sheets.Count -eq 0) { continue }

    $clips = [ordered]@{}
    $frameSize = 0

    foreach ($sheet in $sheets) {
        $layout = Get-SheetLayout $sheet.FullName
        if ($null -eq $layout) { continue }
        $frameSize = $layout.frame

        $rule = $clipRules[$sheet.BaseName]
        if ($null -eq $rule) { $rule = @{ fps = 10; loop = $true } }

        $clip = [ordered]@{
            sheet  = "$($sheet.Name)"
            frames = $layout.cols
            fps    = $rule.fps
            loop   = $rule.loop
        }

        # Only record per-row counts when a row is actually short, so the data
        # stays readable and the common case carries no extra noise.
        if (($layout.rowFrames | Where-Object { $_ -ne $layout.cols }).Count -gt 0) {
            $clip["row_frames"] = $layout.rowFrames
            $ragged++
            Write-Host ("  {0}/{1}: rows {2} of {3}" -f
                        $dir.Name, $sheet.BaseName,
                        ($layout.rowFrames -join ','), $layout.cols) -ForegroundColor DarkYellow
        }

        # If this character was imported as separate layers, list them in draw
        # order. The combined sheet stays on the clip as a fallback and for the
        # menu previews, which do not need a paperdoll.
        $layerDir = Join-Path $dir.FullName "layers"
        if (Test-Path $layerDir) {
            $parts = Get-ChildItem $layerDir -Filter "$($sheet.BaseName)_*.png" -File |
                     ForEach-Object {
                         if ($_.BaseName -match "^$([regex]::Escape($sheet.BaseName))_(\d+)_(.+)$") {
                             [pscustomobject]@{ order = [int]$Matches[1]; slot = $Matches[2]; name = $_.Name }
                         }
                     } | Sort-Object order

            if ($parts) {
                $stack = @()
                foreach ($part in $parts) {
                    $slot = $slotNames[$part.slot]
                    if (-not $slot) { $slot = $part.slot }
                    $stack += [ordered]@{ slot = $slot; sheet = "layers/$($part.name)" }
                }
                $clip["layers"] = $stack
                $layered++
            }
        }

        $clips[$sheet.BaseName] = $clip
    }
    if ($clips.Count -eq 0) { continue }

    # The character stands about four fifths of the way down its frame.
    $anchor = [math]::Round($frameSize * 0.845, 0)

    $out[$dir.Name] = [ordered]@{
        dir      = "assets/characters/$($dir.Name)/"
        rows     = 4
        anchor_y = $anchor
        scale    = 1.0
        clips    = $clips
    }
}

New-Item -ItemType Directory -Force -Path "data" | Out-Null
[IO.File]::WriteAllText((Join-Path (Get-Location) "data\sprites.json"),
                        ($out | ConvertTo-Json -Depth 6) + "`n")

Write-Host ("data/sprites.json: {0} sprite definitions, {1} clip(s) with short rows, {2} layered" -f
            $out.Count, $ragged, $layered) -ForegroundColor Green
