# DreamQuest - makes a zip that anyone can unpack and play. No compiler, no
# MSYS2, no PowerShell policy to change: unpack, double-click DreamQuest.exe.
#
#   .\package.ps1               build, then package into dist\
#   .\package.ps1 -SkipBuild    package what bin\ already holds
#
# What goes in the zip is exactly what a fresh clone has plus the built exe:
# the exe, every runtime DLL build.ps1 put beside it, and every file under
# assets\, art\, maps\ and data\ that git tracks. The things git does not
# track are not shipped on purpose -- the raw art packs that are not ours to
# pass on, the armour icon packs, Blender's render scratch, and this machine's
# saves and settings. The game finds its data beside the exe (see
# src/main.cpp), so the layout inside the zip is the layout it runs from.

param(
    [switch]$SkipBuild,
    [string]$Msys = "C:\msys64\ucrt64"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $SkipBuild) {
    & .\build.ps1 -Server -Msys $Msys
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "The build failed; nothing packaged." }
}
if (-not (Test-Path "bin\DreamQuest.exe")) { throw "bin\DreamQuest.exe is missing. Run .\build.ps1 first." }

# --- what ships -----------------------------------------------------------------
# git knows what is ours: it respects .gitignore, so the excluded folders never
# have to be listed here and cannot drift out of step with it.
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) { throw "git is needed to decide what ships (it reads .gitignore). Install Git for Windows." }
$files = & git ls-files assets art maps data
if (-not $files -or $files.Count -lt 100) { throw "git ls-files returned too little; is this a clone of the repository?" }

$version = (& git describe --tags --always 2>$null)
if (-not $version) { $version = Get-Date -Format "yyyyMMdd" }
$name  = "DreamQuest-win64-$version"
$stage = Join-Path "dist" $name
$zip   = Join-Path "dist" "$name.zip"

Write-Host "Packaging $name ..." -ForegroundColor Cyan
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# The program and its libraries, at the root of the package.
Copy-Item "bin\DreamQuest.exe" $stage
# The headless co-op server, for a machine that is always on. Same libraries.
if (Test-Path "bin\DreamQuestServer.exe") { Copy-Item "bin\DreamQuestServer.exe" $stage }
$dlls = Get-ChildItem "bin" -Filter *.dll -File
foreach ($dll in $dlls) { Copy-Item $dll.FullName $stage }

# The game's data, keeping the tree.
$copied = 0
foreach ($rel in $files) {
    $dest = Join-Path $stage ($rel -replace '/', '\')
    $dir  = Split-Path $dest -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Copy-Item $rel $dest
    $copied++
}

# The note that answers the questions a friend will have before playing.
$play = @"
DREAMQUEST  ($version)
==========

To play:  double-click DreamQuest.exe. That is all.

    Windows may say "Windows protected your PC" the first time, because the
    program is not signed. Click "More info", then "Run anyway". That is a
    once-only prompt for an unsigned program, not a virus warning.

    Keep this folder together. The game finds its pictures, maps and data in
    the folders beside DreamQuest.exe, so move or copy the whole folder, not
    the exe on its own.

Your saves and settings are written next to the exe, in saves\ and
settings.json. They are not in the zip, so unpacking a newer version over this
folder keeps them.

Controls (keyboard; a controller works too, and the game switches to whichever
you touch):

    WASD / arrows   move                 I or Tab   inventory
    J               light attack          O          skills
    K               heavy attack; hold    P or Q     quest journal
                    to charge             M          map of where you are;
                                                     J turns to the Hollowmarch
    J+K, and heavy  combos: see the       G          drop (in the bag)
    mixed into      journal's tutorials   1 2 3 4    choose an element
    the chain                             5          the ancient magic
    H (hold)        block, with a shield  R          cycle elements
    H + J, H + K, H + L   abilities, once learned in your skill tree (O)
    Shift (hold)    sprint                Esc        pause
    Space           jump / climb
    E               talk, open, work

Any of those can be moved: Options, Controls, for the keyboard and for a
controller. On a Steam Deck put the map somewhere other than Guide, which is
Steam's. The quest you are following is pointed at -- a gold arrow, a mark on
the minimap and the map -- and the journal (P) chooses which quest that is.

Nessa's tannery, in the south-west of Havenbrook, has a bench and an order
book: three orders a day for things you make rather than things you find,
which is how Crafting is trained. A monster's level is now what it fights
like, so "Lv 13" means a Combat 13 character has a fight on their hands.

Three kinds of armour, and each helps one way of fighting: metal plate for
a blade, hides for a bow, robes for a staff. Hides are cut from what you kill
-- wolves first, out of Havenbrook's west gate -- and robes from cloth and a
dye; Orla and Isolde at Hidewater, just outside that gate, will tell you how.

The bag is 28 slots, and four bags -- satchel, pack, rucksack, haversack --
add a row of seven each. They take a great many hides at a workbench, or a
lucky chest. Use one from the inventory to put it on; one of each.

The Reverie -- where you go if you choose to dream at a bed -- is never the
same two nights running, and it goes down: behind the brute there is a
ladder, and another below that. Each depth is harder, and everything in it
leaves one more dream shard than it would a ladder up. Combat 25, then 50,
advised; nothing in a dream can kill you.

Playing together, up to four of you, over Tailscale:

    The host starts or loads a game, then Esc, "Play Together", Host a world.
    The screen says what the others should type -- the host's machine name on
    your tailnet, or its 100.x address. The others choose "Play Together" on
    the title screen, pick a Character, choose Join, type that, and press
    Enter: they walk into the host's game. Windows Firewall asks the host
    once: allow DreamQuest on private networks. Everyone needs the same zip;
    the door says so if not.

    You fight the same monsters, share the chests and the trees, and can go
    your separate ways: each map someone is on keeps running. Your character
    is your own, kept on your own machine in saves\characters\ -- drop out,
    come back another day, and you are where you left off, with your bag,
    your skills and your journal. A bed after dusk asks how you would spend
    the night; dawn comes at once when everyone is abed or dreaming.

    Two of you at one machine: plug in a controller, Esc, "Player Two
    joins". The screen splits; Player Two plays on the controller with their
    own character, and you can go your separate ways.

    DreamQuestServer.exe is the same world with nobody at the keyboard, for
    a machine that is always on. Run it and everyone joins it; nobody hosts.

The full manual is README.md in the source repository:
https://github.com/Dexsidius/DreamQuest
"@
Set-Content -Path (Join-Path $stage "PLAY.txt") -Value $play -Encoding utf8

# --- the zip ----------------------------------------------------------------------
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ("Packaged {0} data files and {1} libraries into {2} ({3} MB)" -f $copied, $dlls.Count, $zip, $size) -ForegroundColor Green
Write-Host "Send the zip. Unpack anywhere, double-click DreamQuest.exe."
