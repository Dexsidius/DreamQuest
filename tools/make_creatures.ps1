# =============================================================================
#  make_creatures.ps1 - renders the monsters into assets/characters/<id>/.
#
#      .\tools\make_creatures.ps1                          # every creature
#      .\tools\make_creatures.ps1 -Only rat,spider         # some of them
#      .\tools\make_creatures.ps1 -Only wyvern -Clips walk # one clip
#
#  tools/blender_creatures.py models, poses and renders each creature and
#  reduces it to game pixels; data/sprites.json is regenerated afterwards so a
#  new creature is registered without editing anything else.
# =============================================================================

param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string[]]$Only = @(),
    [string[]]$Clips = @()
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Blender)) {
    throw "Blender not found at $Blender. Pass -Blender <path to blender.exe>."
}

$args = @("--background", "--factory-startup", "--python",
          (Join-Path $PSScriptRoot "blender_creatures.py"), "--") + $Only
if ($Clips.Count) { $args += @("--clips", ($Clips -join ",")) }

Write-Host "Rendering creatures in Blender ..." -ForegroundColor Cyan
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & $Blender @args 2>&1 | Where-Object { $_ -match "^sheet |Error|Traceback|line \d+" } | Write-Host
} finally { $ErrorActionPreference = $prev }
if ($LASTEXITCODE -ne 0) { throw "Blender exited with $LASTEXITCODE." }

& (Join-Path $PSScriptRoot "make_sprites_json.ps1") | Select-Object -Last 1
