# =============================================================================
#  make_icons.ps1 - draws the original item icons in tools/icons.txt.
#
#      .\tools\make_icons.ps1
#
#  The inventory icons are cut from one CraftPix sheet by cell, and that sheet
#  has no log, bow, staff, hide, ore or roast on it. Those items had been given
#  whichever cell was nearest in spirit, and playing the game showed how far
#  that was: the Training Bow was a blue sword, Raw Hide and the Leather Jerkin
#  were a boot, both staves were an eye on a green tile, Roast Boar was a blue
#  lump, and logs and ore were metal ingots.
#
#  So these are drawn by hand as text -- one character per pixel -- and painted
#  here. Text rather than PNGs so they can be read, diffed and touched up
#  without an image editor, the same way the tiles and props are generated
#  rather than stored.
# =============================================================================

#  -Only draws just the icons named, so adding one does not rewrite the rest.

param([string[]]$Only = @())

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root   = Split-Path $PSScriptRoot -Parent
$source = Join-Path $PSScriptRoot "icons.txt"
$icons  = Join-Path $root "assets\icons"
New-Item -ItemType Directory -Force -Path $icons | Out-Null

# The prologue's Hearty Meal -- what Bess presses on the player in the tavern
# before they go: a loaf, a wedge of cheese and a little crock sealed with red
# wax, packed on a gingham cloth to carry. Kept here, in icons.txt's own format,
# because this batch was confined to the make_*.ps1 scripts; it can be moved
# into icons.txt as it stands.
$extra = @'
@hearty_meal
k=3a2014
w=ece2c8
r=b2402e
c=c4b28c
R=86301f
b=a8642c
B=d0904a
d=6e3c18
y=eac44e
h=f8e8a0
Y=c0942a
o=9a7426
g=8a6448
G=b48a64
s=b3302a
S=e65a4a
................
..........kkk...
.........kSssk..
.........kkkkk..
..kkkkk..kGGgk..
.kBBBbbk.kGGggk.
kBBdBbbbkkGgggk.
kBdbbbdbbkGgggk.
kbbbdbbbbkGgggk.
.kbbbbbbkkGgggk.
kwkkkkkkrkkhkkrk
krwwrrwkhhhYkwwk
kwrrkhhyoyyYkrrk
krkyyyoyyyyYkwwk
kRkkkkkkkkkkkcck
.kkkkkkkkkkkkkk.
# Act I: Vigil's dreamcatcher (vigil_dreamcatcher -- dreamcatcher.png is
# another item's), the Ashen Vanguard's scorched scale, the three brass
# tokens for Bess's cellar lock, the Mayor's house key, the Guild's finest
# (amulet, ring and hood in the Guild's blue and gold, the chevron of its
# banner on each), Bess's hot supper under its cover, and the little golden
# bell hung over a sleeper with a Dawn Chimes quest in them (marker_side).
@marker_side
k=3e2406
y=f6c71c
Y=fff3a6
o=c4860e
w=ffffff
................
.......kk.......
......k..k......
......kkkk......
.....kyyyyk.....
....kywyyyyk....
....kyYyyyok....
....kyYyyyok....
....kyYyyyok....
...kyYyyyyyok...
...kyYyyyyyok...
...kYYyyyyyok...
...kkkkkkkkkk...
......kook......
.......kk.......
................
@token_candle
k=3a2810
d=9a7020
g=d4a438
y=f2d27a
D=7a5418
e=5a3c10
l=fbe7a8
................
......kkkk......
....kkyyddkk....
...kyyggggddk...
..kyyggglgggdk..
..kyggglegggdk..
.kyggggegggggdk.
.kyggggeelgggdk.
.kdggggeelgggdk.
.kdggggeeggggDk.
..kdgggeegggDk..
..kdgeeeeeegDk..
...kddggggDDk...
....kkdddDkk....
......kkkk......
................
@token_loaf
k=3a2810
d=9a7020
g=d4a438
y=f2d27a
D=7a5418
e=5a3c10
................
......kkkk......
....kkyyddkk....
...kyyggggddk...
..kyygggggggdk..
..kyggggggggdk..
.kygggeeeegggdk.
.kyggegeggeggdk.
.kdgegeggegegdk.
.kdgeggggggegDk.
..kdgeeeeeegDk..
..kdggggggggDk..
...kddggggDDk...
....kkdddDkk....
......kkkk......
................
@token_mug
k=3a2810
d=9a7020
g=d4a438
y=f2d27a
D=7a5418
e=5a3c10
l=fbe7a8
................
......kkkk......
....kkyyddkk....
...kyyggggddk...
..kyygglggggdk..
..kygglglgggdk..
.kyggeeeeegggdk.
.kyggegggeeggdk.
.kdggegggegegdk.
.kdggegggegegDk.
..kdgegggeegDk..
..kdgeeeeeggDk..
...kddggggDDk...
....kkdddDkk....
......kkkk......
................
@vigil_dreamcatcher
k=2a1e26
b=8a6a4a
B=c09a6c
w=ece8f6
v=a882f0
V=e4d6ff
f=f2efe6
F=a8a4b4
t=c8c0d8
.....kkkkkk.....
....kBbBbBbk....
...kBbkkkkBbk...
..kBbkkwk.kBbk..
..kbk.kwk.kwBk..
.kbBwkkkwkwkbBk.
.kBbkwwvVwkkBbk.
.kbBkkkvvwwwbBk.
..kbkkwkwkkkwk..
..kBbwk.kwkBbk..
...kBbkkkwBbk...
...ktBbBtBbktk..
...kfkkkfkkkfk..
..kffk.kffkkffk.
...kFk.kfk.kFk..
....k..kFk..k...
@scorched_scale
k=1a1012
s=3a2c2c
S=5a4440
d=241a1c
e=ff8a1c
E=ffd060
r=a83a12
................
....kkkkkkk.....
...kSSSSsssk....
...kSSSsSsssk...
..kSSSssSssssk..
.kSSSsssSssssk..
.kSSsessSssssk..
.kSssseEsssssk..
.ksssssssersddk.
..kesssssssdEk..
...kssssssdrk...
...kersssddek...
....kerssdek....
.....keEdkk.....
......kkk.......
................
@house_key
k=1e1c22
i=6c707e
I=b0b4c0
D=44465a
t=b8905c
T=dcbc88
r=b03c2c
................
..kkkk..........
.kIIiik.........
kIk..kik........
kik..kik........
kiik.kik........
.kiiiiDk........
..Dkkkiik.......
.kkkk.kiik......
kTTTTk.kiik.....
ktrrtk..kiik....
krrrrk...kiikk..
ktrrtk....kiiDk.
.kkkk.....kiDk..
...........kDDk.
............kk..
@guild_amulet
k=18182a
b=2e4c9a
B=5a7ad0
g=d6a438
G=f6dc84
c=efe4c8
o=8a6420
...kogogogogk...
..kokkkkkkkkgk..
.kgk........kok.
.kok........kgk.
..kgk......kok..
...kokkkkkkgk...
...kgggggggk....
...kgGGbbbgk....
...kgbBBbbgk....
...kgbbbcbgk....
....kgbcbcgk....
.....kcbbgck....
.....kggggk.....
......kggk......
.......kk.......
................
@guild_ring
k=18182a
b=2e4c9a
B=5a7ad0
g=d6a438
G=f6dc84
c=efe4c8
o=8a6420
................
......kkkk......
.....kggggk.....
....kGbbbbgk....
....kgBbcbgk....
....kgbcbcgk....
....kgcgggck....
...kGGkkkkggk...
...kGgk..kggk...
...kgk....kok...
...kggk..kook...
...kggkkkkook...
....kgggoook....
.....kgoook.....
......kkkk......
................
@guild_hood
k=18182a
b=2e4c9a
B=5a7ad0
n=1c2e66
g=d6a438
G=f6dc84
.......kk.......
.....kkbbkk.....
....kBbbGbbk....
...kBBbGbGbbk...
...kBBGbbbGbk...
...kBBggggbbk...
..kBBBgnngbbbk..
..kBBgnnnngbbk..
..kBBgnnnngbbk..
..kbbgnnnngbbk..
..kbbgnnnngbbk..
.kbbbbgnGgbbbbk.
.kbbbbbbgbbbbbk.
.kgGggGggGggGggk
..kkkkkkkkkkkkk.
................
@bess_supper
k=2a1c14
p=a4a8ae
P=dfe2e6
d=6e727a
h=c46a2a
H=e89a4a
c=efe6d0
s=f4f4f4
S=c8ccd4
.....k....k.....
....ksk..ksk....
...ksk....ksk...
...kSk.kk.kSk...
....kSkppkSk....
.....kkddkk.....
....kkppppkk....
...kppPpppppk...
..kppPpppppppk..
..kppPpppppppk..
.kkppppppppppkk.
kcddddddddddddck
kcdkkkkkkkkkhHck
.kcck......kcck.
..kk........kk..
................
'@

function Write-Icon($icon) {
    $name = $icon.name
    if ($icon.rows.Count -ne 16) { throw "icon '$name' has $($icon.rows.Count) rows, not 16" }

    $bmp = New-Object System.Drawing.Bitmap -ArgumentList 16, 16,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        for ($y = 0; $y -lt 16; $y++) {
            $row = $icon.rows[$y]
            if ($row.Length -ne 16) { throw "icon '$name' row $y is $($row.Length) wide, not 16" }
            for ($x = 0; $x -lt 16; $x++) {
                $ch = [string]$row[$x]
                if ($ch -eq '.') {
                    $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                    continue
                }
                # Hashtables ignore case by default; the palette must not.
                if (-not $icon.palette.ContainsKey($ch)) { throw "icon '$name' uses undefined colour '$ch'" }
                $bmp.SetPixel($x, $y, $icon.palette[$ch])
            }
        }
        $bmp.Save((Join-Path $icons "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bmp.Dispose()
    }
}

$current = $null
$made = 0
$lines = @([IO.File]::ReadAllLines($source)) + @($extra -split "`r?`n")
foreach ($line in $lines) {
    if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }

    if ($line.StartsWith('@')) {
        if ($current -and ($Only.Count -eq 0 -or $Only -contains $current.name)) { Write-Icon $current; $made++ }
        $current = @{
            name    = $line.Substring(1).Trim()
            palette = New-Object 'System.Collections.Generic.Dictionary[string,System.Drawing.Color]' ([StringComparer]::Ordinal)
            rows    = New-Object System.Collections.Generic.List[string]
        }
        continue
    }
    if (-not $current) { throw "icons.txt: '$line' comes before any @name" }

    if ($current.rows.Count -eq 0 -and $line -match '^(.)=([0-9a-fA-F]{6})$') {
        $hex = $Matches[2]
        $current.palette[$Matches[1]] = [System.Drawing.Color]::FromArgb(255,
            [Convert]::ToInt32($hex.Substring(0, 2), 16),
            [Convert]::ToInt32($hex.Substring(2, 2), 16),
            [Convert]::ToInt32($hex.Substring(4, 2), 16))
    } else {
        $current.rows.Add($line)
    }
}
if ($current -and ($Only.Count -eq 0 -or $Only -contains $current.name)) { Write-Icon $current; $made++ }

Write-Host "  $made icons drawn into assets\icons"
