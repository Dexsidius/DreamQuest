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
folder keeps them. There are three single-player and three multiplayer slots;
each keeps a backup, the game saves itself every two minutes and when the
window is closed, and the load screen can delete a slot.


THE PROLOGUE

A new game begins with the prologue, "The Town That Wouldn't Wake": you are
found on the road at the end of a night and carted into Havenbrook, where
people have been falling asleep and not waking. It is cutscenes and short
stretches of play between them; hold Esc (B on a pad) to skip a scene.
Signs at the foot of the screen say how to do each new thing as it comes up.
First you choose who you are, one of four: the Lucid Knight (heavy melee), the
Shade Ranger (agile, ranged and melee), the Dreamweaver (ranged magic) or the
Lantern Warden (tank and support: a mace, and a lit lantern that guards like a
shield; its skill tree heals and wards those near it). You choose your weapon
and armour from a chest partway through.

When it is over Havenbrook is asleep, all of it, until the story wakes it: its
shops and trades will keep. Its gates are open and the rest of the world is
awake. Play Together and Player Two open once the prologue is done.


ACT I -- LEARNING THE RULES

Vigil gave you a Dreamcatcher. Somebody in Havenbrook with a story of theirs
to finish has a little bell over them; walk up and the button says Use
Dreamcatcher, and it takes you into their dream. Elder Vask, rocking on the
guild hall's porch, is the first. In a sleeper's dream you cannot die: if it
goes badly you wake beside them with everything you changed still changed,
and a Waking Stone lets you out sooner.

Then, in any order: the smith Halda (the forge's chimney is cold), Bess (the
inn is emptier than you left it) and the Tanner (the woodcutter heard wolves).
Each of them wakes with their trade open to you again. The woodcutter, the
angler and the miner teach you their gathering trades once Vask is awake --
until then you have no idea how. Ten other sleepers can be woken with a Dawn
Chime: three waves of Hushed in their dream, always about as strong as you.

When all three are saved, the Mayor's Hall asks if you are ready. Once in, you
cannot leave until it is done; fall, and you wake at his desk, and the note
takes you back in. The town's gifts are a house in Havenbrook and your pick of
three pieces the Guild made for its finest.

Bosses glow red before a big move: step out of the glow. The Forge Demon glows
gold for a moment after its flame or its spin -- hit it then, and it hurts.
A nightmare spider's web can root you where you stand for a second or two.


ACT II -- WWDD

The dragon's shadow passes over Havenbrook, and Elder Vask will not talk about
it until you are stronger (Combat 35). Four pages are missing from Apocolo's
recipe book, and each torn stub says where its page went; the cure they make
wakes Mossvale and Fernhollow one sleeper at a time, and then the college. The
Magister there has lessons in the old magic, and a talisman to send you after
at the bottom of Hollowrest Crypt: worn in its own place, T (RB + LB on a pad)
steps into the Reverie where you stand, in the places it dreams, and back.

Some choices in Act II cannot be backed out of: Esc does not answer them.

THE GUILD'S LEDGER

When the dragon's shadow has passed over Havenbrook, Guild Master Orlend has a
bell over him at his desk in the Guild Hall. (A character from before the
prologue finds him with it straight away.) He opens the Guild's ledger to you:
a board beside his desk with a bounty on every named beast in the game but one
-- seventeen of them, from the spider under the inn to the Quintessence. Each
page shows the beast, says where it lairs and what it does, and is taken once.
The arrow leads to its lair, but it counts wherever it falls, and the Guild
pays the moment it does. Levels in red are still past you; L (RT on a pad) on
the board shows only what is within ten levels of you.


CONTROLS

Keyboard and controller both work; the game follows whichever you touch.
Every one of these can be moved in Options, Controls.

    WASD / arrows   move                     Tab        the menu of menus
    J               light attack             I          bag
    K               heavy attack; hold it    C          character
                    to charge                O          skills, the skill tree,
    L               lock on                             the spellbook, boons
    H (hold)        guard (see below)        P or Q     journal
    H or F, held,   the three abilities      M          map; J on it turns to
      + J, K or L   from your skill tree                the whole Hollowmarch
    Shift (hold)    sprint                   G          drop (in the bag)
    Space           jump / climb             Esc        pause
    T               the death talisman, once you wear it
    E               talk, open, pick, work
    1 2 3 4         fire, water, earth, air  5          lightning
    6               the ancient magic        R          step through elements
    [ and ]         the spell before / after in the element chosen

    In menus J (or Enter) confirms and K (or Backspace) goes back.

    On a controller: X light, Y heavy, B guard, A use, RT lock on, LT sprint,
    click the left stick to jump; hold RB with X, Y or RT for the abilities.
    LB is the bag, Select the menu of menus, Start pauses. Click the right
    stick to step through the elements, and push it left or right for the
    spell. On a Steam Deck put the map on a button other than Guide, which is
    Steam's.

The guard depends on what is in your hands: a shield blocks, and so does the
Lantern Warden's lit lantern; a dagger with no shield, or a greatsword, parries
(catch a blow in the first moment and it does nothing); the Shade Ranger with
nothing to guard with rolls instead; and a Dreamweaver who has learned the
Aegis from the magic tree wards with magic.

The quest you are following is pointed at -- a gold arrow, a mark on the
minimap and on the map -- and the journal chooses which quest that is.


FIGHTING

Light attacks chain three blows. Mix a heavy into the chain, or press light
and heavy together, for a combo: most weapons have five of their own, and
the journal's tutorials show them. Each way of fighting has a skill tree (O): a
point every three levels of its skill, three abilities carried at once, and
techniques that replace the charged heavy attack. Unlearning a tree costs 60
coins a point.

Attack decides whether a blow lands and Strength how hard; heavy swings train
Strength. Ranged and Magic train by hitting: a spell pays its experience the
first time it hurts something, so casting at a wall teaches nothing. Defence
is not trained at all -- it follows your combat level.

A monster's level is what it fights like: "Lv 13" means a Combat 13 character
has a fight on their hands. A door, a ladder or a stair down that leads
somewhere stronger than you says so, and so does the label at the edge of a
map, in red. If you fall, you wake in Havenbrook with everything you carried.

Magic: 1 to 4 choose the four elements, 5 lightning and 6 the ancient magic,
and [ and ] step through an element's spells as your Magic level opens them.
A staff, a wand, a grimoire and an orb each reach a different part of every
element. Lightning has its own battery, which only Zap fills. The ancient
magic is learned from tomes; the College at Fernhollow sells them.

Your weapon opens the six spell slots from the left: a wooden staff two,
and each finer tier one more -- bronze three, iron four, steel five, azuryte
all six. A shut slot shows a padlock. An element's own staff fills its slots
with that element's spells, then the ancient magic, and never casts lightning.


THE BAG, FOOD AND POTIONS

The bag is 28 slots. Four bags -- satchel, pack, rucksack, haversack -- add a
row of seven each: they are cut at a tanning rack from a great many hides, or
found in a lucky chest. Use one from the bag to put it on.

Anything you could wear shows a card beside the cursor: what it gives, and
what it would change against what you have on -- green better, red worse.

In the bag, L on any food or potion puts it to hand, at the bottom left. Hold
the guard and press E to use it, and hold the guard and tap sprint to step to
the next (RB + A, and RB + LT, on a controller). Anything that heals takes a
second and a half to get down before the next one.

A fire's menu has plain food and dishes. A dish sits with you for fifteen to
forty minutes and lifts your health, mana, breath or a way of fighting; one
dish at a time. A potion lifts a level for a while, or keeps a status off you.


GETTING ABOUT

Waystones: ten of them. Four are on the Towns tab -- Havenbrook, Mossvale,
your own house there, and Fernhollow -- and six on the Wilds tab, from the
Bayou and the Ice Spire to the Primordium. Touch one (E) to wake it; touch a
woken one again and it takes you to any other you have woken, for nothing.
A few of the wild ones will not take a character too weak to live there.

Every way into a town is a gate with a warden. A room at the inn is 15 coins
(25 for the double); your own bed at Mossvale and a camp are free. After dusk
a bed asks how you would spend the night: sleep through it, or dream. The
dream -- the Reverie -- is different every night and goes down a ladder at a
time, Combat 25 and then 56 advised; nothing in a dream can kill you.

At the bottom, through a mirror, is Havenbrook as a nightmare has it, and
beside that mirror four more: the College, the Ashen Path, the Plateau and the
Bayou, dreaming, overrun by monsters from 70 to 95. The Dreamer's Slate in the
Reverie posts bounties on whatever is out on them that night, three a land;
one not finished by dawn lapses. On the Slate, L (RT on a pad) shows only what
is within ten levels of your Combat, and shows everything again.

The wilds are different after dark: things come out at night that do not live
there by day, a step stronger than the neighbours, never on the roads, never
near a gate or a camp, and gone at dawn. In the Bayou and the Hexmire,
gators and worse wait under still water and come up when you come too close
to its edge.


BOSSES, BOONS AND TOTEMS

The first time you kill a boss it leaves you a skill point for your tree and a
boon: one of twenty-two, chosen by the dice from those your character can use,
and lasting a day of the game's clock. Everyone on the map when it falls gets
their own. A boss you have killed stays dead until the next dawn.

The fifteenth time you kill a boss it leaves you its totem. There is a ring in
the floor of your house at Mossvale: stand a totem in it and touch it, and it
gives its blessing until dawn, wherever you go -- one totem at a time. The
Skills panel's Boons tab lists your boons and keeps count of your kills.


TRADES

Every trade is a level to reach and something new every few levels: Mining,
Woodcutting, Fishing and Foraging gather; Smithing (an anvil), Crafting (wood,
bows and jewellery, at a workbench), Tanning (hides, boots, bags and bedrolls,
at a tanning rack), the Clothier's robes (a loom; Wynn's is in her shop at
Mossvale), Brewing (a cauldron) and Cooking (a fire) make; and Enchanting
works charms into gear at an enchanting table. At any station, hold sprint and
confirm to make as many as you have the materials for.

Fishing is earned. Cast with E at a spot and watch the bobber: it dips once --
not yet -- and then goes right under. Press E then. With the fish on, hold E to
reel and keep the line inside the green band as the fish drags it about; the
bar under it fills as the fish comes in. Out of the green too long and the line
snaps. The better the fish, the narrower the green and the harder it pulls.

Halda the smith, Nessa the tanner and Innkeeper Bess in Havenbrook, Wynn and
Oona at Mossvale, and Old Wendel at Fernhollow keep order books: things to
make or bring, which pay coins and train the trade they ask for. Anyone who
offers to teach you something gives you a short tutorial quest -- go and do
the thing, then come back.

At a storage chest the drop button stows everything the chest already has some
of; with sprint held, the whole pack (never coins, never quest things). From
the chest's side it takes the lot.


PLAYING TOGETHER, up to four of you, over Tailscale

    The host chooses Play Together, then Host a world: from the title screen,
    one of their multiplayer worlds or a new one; from a game in progress, that
    game, carried to a multiplayer slot first (its single-player save stays as
    it is). The screen says what the others should type -- the host's machine
    name on your tailnet, or its 100.x address. The others choose Play Together
    on the title screen, pick a character, choose Join, type that and press
    Enter, and walk into the host's game. Windows Firewall asks the host once:
    allow DreamQuest on private networks. Everyone needs the same zip; the door
    says so if not.

    You fight the same monsters and share the chests and the trees, and can go
    your separate ways: each map someone is on keeps running. Your character
    is your own, kept on your own machine in saves\characters\ -- drop out,
    come back another day, and you are where you left off, with your bag, your
    skills and your journal. Dawn comes at once when everyone is abed or
    dreaming.

    Two of you at one machine: plug in a controller, Esc, Player Two joins.
    The screen splits, and Player Two plays on the controller with their own
    character.

    DreamQuestServer.exe is the same world with nobody at the keyboard, for a
    machine that is always on. Run it and everyone joins it; nobody hosts.

Options has an Interface Size for small screens such as a Steam Deck's.
Options, then Visual Effects, has the Art Style: Cozy (painted ground, soft
outlines, warm days and dusky nights) or Classic (the tiles as drawn).
Turning Visual Effects off takes the cloud shadows, birds and wind away too.
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
