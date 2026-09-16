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
    [string[]]$Look = @("player_hero", "player_warden", "player_wayfarer")
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if (-not (Test-Path $Blender)) {
    throw "Blender not found at $Blender. Pass -Blender <path to blender.exe>."
}

foreach ($look in $Look) {
    $outDir = Join-Path $root "assets\characters\$look"

    # A full render replaces the character outright. Layer files are discovered
    # by name, so a layer an older version wrote and this one does not -- the
    # first hero had a weapon_back sheet -- would otherwise be drawn on top
    # forever.
    if ($Only.Count -eq 0 -and (Test-Path $outDir)) {
        Remove-Item -Recurse -Force $outDir
    } elseif ($Only.Count -gt 0) {
        foreach ($clip in $Only) {
            Get-ChildItem (Join-Path $outDir "layers") -Filter "${clip}_*.png" -File -EA SilentlyContinue |
                Remove-Item -Force
        }
    }

    Write-Host "Rendering $look in Blender ..." -ForegroundColor Cyan
    $args = @("--background", "--factory-startup", "--python",
              (Join-Path $PSScriptRoot "blender_character.py"), "--") +
            $Only + @("--look", $look, "--out", $outDir)
    # Blender writes progress to stderr, which Windows PowerShell turns into
    # error records that "Stop" treats as fatal. Only the exit code decides.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Blender @args | Where-Object { $_ -match "^sheet |Error|Traceback" } | Write-Host
    } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }
}

Push-Location $root
try { & (Join-Path $PSScriptRoot "make_sprites_json.ps1") } finally { Pop-Location }
