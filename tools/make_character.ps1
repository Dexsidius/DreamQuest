# =============================================================================
#  make_character.ps1 - renders the original player character into
#  assets/characters/player_hero/.
#
#      .\tools\make_character.ps1                  # every clip
#      .\tools\make_character.ps1 -Only walk,sprint # a couple of clips
#
#  tools/blender_character.py does all of it: it models and poses the
#  character, renders each clip once per layer at four times the final size,
#  reduces the render to game pixels by majority colour, draws the selective
#  outline, and writes both the layer sheets and a flattened sheet per clip.
#  There used to be a PowerShell box-downsample here; averaging is exactly what
#  the cel-shaded bands must not go through, so the reduction moved next to the
#  render and into numpy.
#
#  data/sprites.json is regenerated afterwards, so a new clip -- sprint, when it
#  was added -- is picked up without editing anything else.
# =============================================================================

param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string[]]$Only = @(),
    # Which of the three playable characters to render. They are one rig in
    # three sets of clothes -- see LOOKS in tools/blender_character.py.
    [string[]]$Look = @("player_hero", "player_warden", "player_wayfarer"),
    # Which cuts of armour to render over the character. "plate" is the one the
    # sheets are named after; the other two are written with a suffix and are
    # picked up by the tier that wears them. Rendering only the armour for the
    # alternates keeps them to about a third of a full pass each.
    [string[]]$Style = @("plate", "light", "ornate")
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if (-not (Test-Path $Blender)) {
    throw "Blender not found at $Blender. Pass -Blender <path to blender.exe>."
}

foreach ($look in $Look) {
    $outDir = Join-Path $root "assets\characters\$look"

    # A full render replaces the character's own sheets. Layer files are
    # discovered by name, so a layer an older version wrote and this one does
    # not -- the first hero had a weapon_back sheet -- would otherwise be drawn
    # on top forever.
    #
    # Only this script's own layers are removed, and only for the cuts being
    # rendered. The tier weapon sheets in the same folder come from
    # make_tiers.ps1 and take four minutes to rebuild; wiping the whole
    # directory quietly deleted them, and the character went back to holding a
    # plain sword until someone noticed. Rendering -Style light on its own used
    # to take the plate with it for the same reason.
    # $cut, not $style: PowerShell variables are case-insensitive, so a
    # foreach over $Style with $style as the loop variable overwrites the very
    # list it is walking, and only the last cut survives to be rendered.
    $patterns = @()
    foreach ($cut in $Style) {
        if ($cut -eq "plate") {
            $patterns += "_(shadow|body|head|weapon_front|armour_[a-z]+)\.png$"
        } else {
            $patterns += "_armour_[a-z]+_$cut\.png$"
        }
    }
    $mine = { foreach ($p in $patterns) { if ($_.Name -match $p) { return $true } }; return $false }

    if ($Only.Count -eq 0 -and (Test-Path $outDir)) {
        if ($Style -contains "plate") {
            Get-ChildItem $outDir -Filter *.png -File -EA SilentlyContinue | Remove-Item -Force
        }
        Get-ChildItem (Join-Path $outDir "layers") -File -EA SilentlyContinue |
            Where-Object $mine | Remove-Item -Force
    } elseif ($Only.Count -gt 0) {
        foreach ($clip in $Only) {
            Get-ChildItem (Join-Path $outDir "layers") -Filter "${clip}_*.png" -File -EA SilentlyContinue |
                Where-Object $mine | Remove-Item -Force
        }
    }

    foreach ($cut in $Style) {
        Write-Host "Rendering $look ($cut) in Blender ..." -ForegroundColor Cyan
        $args = @("--background", "--factory-startup", "--python",
                  (Join-Path $PSScriptRoot "blender_character.py"), "--") +
                $Only + @("--look", $look, "--out", $outDir, "--style", $cut)
        # Blender writes progress to stderr, which Windows PowerShell turns into
        # error records that "Stop" treats as fatal. Only the exit code decides.
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $Blender @args | Where-Object { $_ -match "^sheet |Error|Traceback" } | Write-Host
        } finally { $ErrorActionPreference = $prev }
        if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }
    }
}

Push-Location $root
try { & (Join-Path $PSScriptRoot "make_sprites_json.ps1") } finally { Pop-Location }
