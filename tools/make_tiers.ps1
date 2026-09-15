# =============================================================================
#  make_tiers.ps1 - renders the material-tier art.
#
#      .\tools\make_tiers.ps1                       # icons and weapon layers
#      .\tools\make_tiers.ps1 -What icons           # just the inventory icons
#      .\tools\make_tiers.ps1 -What layers -Only attack -Models sword_iron
#
#  tools/blender_tiers.py models every tier's ore, bar, weapons and armour from
#  the same parts and cel shading as the player hero, and writes
#    assets/icons/tiers/*.png                                   inventory icons
#    assets/characters/player_hero/layers/<clip>_4_weapon_<model>.png
#                                                    the weapon in the hero's hand
# =============================================================================

param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string[]]$What = @(),
    [string[]]$Only = @(),
    [string[]]$Tiers = @(),
    [string[]]$Models = @(),
    # With -What brewing: only these herb, potion or recipe icons.
    [string[]]$Names = @()
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Blender)) {
    throw "Blender not found at $Blender. Pass -Blender <path to blender.exe>."
}

$args = @("--background", "--factory-startup", "--python",
          (Join-Path $PSScriptRoot "blender_tiers.py"), "--") + $What
if ($Only.Count)   { $args += @("--only",   ($Only -join ",")) }
if ($Tiers.Count)  { $args += @("--tiers",  ($Tiers -join ",")) }
if ($Models.Count) { $args += @("--models", ($Models -join ",")) }
if ($Names.Count)  { $args += @("--names",  ($Names -join ",")) }

Write-Host "Rendering tier art in Blender ..." -ForegroundColor Cyan
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & $Blender @args | Where-Object { $_ -match "^icons |^layers |Error|Traceback|line \d+" } | Write-Host
} finally { $ErrorActionPreference = $prev }
if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }
