# =============================================================================
#  Writes data/sprites.json from whatever is actually in assets/characters/.
#
#  Every CraftPix character sheet in this project is laid out the same way: a
#  square frame, four rows (down, left, right, up), one column per frame. So
#  the frame size is the sheet height over four, and the frame count is the
#  width over that. Deriving it here means the animation data can never drift
#  out of step with the imported art.
#
#  Run by tools/import_assets.ps1; safe to run on its own afterwards.
# =============================================================================

param([string]$Root = "")

$ErrorActionPreference = "Stop"
if ($Root -eq "") { $Root = Join-Path $PSScriptRoot ".." }
Set-Location $Root

$charDir = "assets\characters"
if (-not (Test-Path $charDir)) { throw "No $charDir - run tools/import_assets.ps1 first." }

function Get-PngSize($path) {
    $fs = [IO.File]::OpenRead($path)
    try {
        $b = New-Object byte[] 24
        [void]$fs.Read($b, 0, 24)
        # PNG IHDR stores width and height big-endian at bytes 16..23.
        return @([BitConverter]::ToInt32($b[19..16], 0), [BitConverter]::ToInt32($b[23..20], 0))
    } finally { $fs.Close() }
}

# Playback speed and whether a clip repeats. Anything not listed loops at 10fps.
$clipRules = @{
    idle   = @{ fps =  8; loop = $true  }
    walk   = @{ fps = 10; loop = $true  }
    run    = @{ fps = 13; loop = $true  }
    attack = @{ fps = 16; loop = $false }
    hurt   = @{ fps = 12; loop = $false }
    death  = @{ fps =  9; loop = $false }
}

$out = [ordered]@{}

foreach ($dir in (Get-ChildItem $charDir -Directory | Sort-Object Name)) {
    $sheets = Get-ChildItem $dir.FullName -Filter *.png | Sort-Object Name
    if ($sheets.Count -eq 0) { continue }

    $clips = [ordered]@{}
    $frameSize = 0

    foreach ($sheet in $sheets) {
        $size = Get-PngSize $sheet.FullName
        $fh = [int]($size[1] / 4)
        if ($fh -le 0) { continue }
        $frames = [int]($size[0] / $fh)
        if ($frames -le 0) { continue }
        $frameSize = $fh

        $rule = $clipRules[$sheet.BaseName]
        if ($null -eq $rule) { $rule = @{ fps = 10; loop = $true } }

        $clips[$sheet.BaseName] = [ordered]@{
            sheet  = "$($sheet.Name)"
            frames = $frames
            fps    = $rule.fps
            loop   = $rule.loop
        }
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
$json = $out | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path (Get-Location) "data\sprites.json"), $json + "`n")

Write-Host "data/sprites.json: $($out.Count) sprite definitions" -ForegroundColor Green
