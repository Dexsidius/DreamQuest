# DreamQuest

A top-down adventure RPG in C++ and SDL3, in the Dragon Quest Monsters mould:
a scrolling overworld with several biomes, a village you can walk into and out
of, houses and a guild hall you can enter, mountain mines and a barrow to raid,
Old School RuneScape-style skills, weighted loot tables, three melee attacks
built around a hold-to-charge heavy swing, bows that fire real arrows, a
four-element spell system with an effectiveness triangle, and a soundscape
synthesised entirely in code.

Built on [SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template),
and the maps are authored in the format exported by
[LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus).

![DreamQuest](docs/screenshots.png)

---

## Building

### Playing without building

Nobody has to compile anything to play. `package.cmd` builds the game and packs
it into a zip in `dist\` -- the exe, the eighteen runtime libraries it needs,
and every data, map and art file the repository tracks, about 20 MB. Send the
zip. **Unpack it anywhere and double-click `DreamQuest.exe`.** The game finds
its data beside the exe, so it runs from a Desktop folder, a USB stick or a
network share, and writes its saves and settings next to itself. `PLAY.txt`
inside the zip says the same and lists the controls.

The first run shows Windows' "protected your PC" screen, because the program is
not signed: *More info*, then *Run anyway*, once. To update, unpack the newer
zip over the old folder; saves are not in the zip, so they are kept.

### Building it yourself, on Windows

The game is plain C++20 on SDL3, built with the MSYS2 UCRT64 toolchain. Step by
step, on a machine that has never seen either:

1. Install **MSYS2** from [msys2.org](https://www.msys2.org) (the default
   `C:\msys64` is where `build.ps1` looks; pass `-Msys` if it is elsewhere).
2. Open the **MSYS2 UCRT64** shell from the Start menu -- not the MSYS or
   MINGW64 one -- and install the compiler, the three SDL libraries and ENet:

   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-sdl3-image mingw-w64-ucrt-x86_64-sdl3-ttf mingw-w64-ucrt-x86_64-enet
   ```

   ENet is the networking library [playing together](#playing-together) runs
   on. If you built the game before co-op began, that last package is the one
   you are missing, and `build.ps1` says so.

   Only to change the shaders (`src/shaders`), also
   `pacman -S mingw-w64-ucrt-x86_64-shaderc` for `glslc`. The compiled ones
   are in the repository, so the game builds and draws them without it.

   That is the only time the MSYS2 shell is needed; the build itself runs from
   Windows.
3. Clone the repository. Everything the game loads is in it: `assets/` is the
   game's own art and travels with the clone.
4. **Double-click `build.cmd`**, or from any prompt in the folder:

   ```powershell
   .\build.cmd -Run
   ```

   The exe lands in `bin\` with its libraries beside it, and `-Run` starts it.

If you ran `.\build.ps1` directly and Windows said *running scripts is disabled
on this system*, that is PowerShell's execution policy refusing every `.ps1`
on a fresh machine, and it is the wall most people hit first. `build.cmd`
exists to get past it: it runs the same script with the policy bypassed for
that one command. The equivalent by hand is
`powershell -ExecutionPolicy Bypass -File build.ps1 -Run`.

`tools\import_assets.ps1` is only for regenerating `assets/` from the art
packs and is not part of building; a clone already has everything it produces
that the game needs.

### Linux / macOS

Install SDL3, SDL3_image, SDL3_ttf and ENet (`libenet-dev`, `enet`), then:

```bash
./compile_and_run.sh
```

### Other build targets

| Command | What it does |
| --- | --- |
| `.\build.ps1` | Build the game |
| `.\build.ps1 -Run` | Build and launch |
| `.\build.ps1 -Debug` | Unoptimised build with symbols |
| `.\build.ps1 -Test` | Build and run the self-test |
| `.\build.ps1 -Maps` | Regenerate `maps/*.mx` |
| `.\build.ps1 -Tools` | Build `tilecut`, `genmaps` and `selftest` |
| `.\package.cmd` | Build, then zip a playable copy into `dist\`, the co-op server included |
| `.\build.ps1 -Server` | Also build `DreamQuestServer.exe`, the headless co-op server |

`./compile.sh test`, `./compile.sh maps` and `./compile.sh tools` do the same on
Linux.

### Running it outside MSYS2

`bin\DreamQuest.exe` can be double-clicked. Two things make that work, and both
had to be dealt with explicitly:

- **The runtime libraries travel with the exe.** Nothing is on the PATH outside
  an MSYS2 shell, so the build walks the DLL dependency tree with `objdump` and
  copies everything that resolves inside MSYS2 next to the executable — 18
  libraries, because SDL3_ttf pulls in FreeType and HarfBuzz, which between them
  pull in libpng, zlib, bzip2, Brotli, GLib, PCRE2 and Graphite. A hand-written
  list of these was wrong, and the symptom is Windows refusing to start the
  program with no message at all.
- **The game finds its own data.** `data/`, `assets/` and `art/` are opened by
  relative path, so started from Explorer the working directory is `bin\` and
  every file fails to open. On startup it locates the directory holding
  `data/sprites.json` — beside the exe, then one above it — and moves there, so
  saves and settings land in the project root wherever it was launched from. If
  it genuinely cannot find them it says so in a message box rather than closing
  silently.
- **The icon is in two places, because Windows needs it in two places.** The
  window and the taskbar entry get theirs at startup from `art/app_icon.png`;
  Explorer never runs the program, so the shortcut and the file itself take the
  `.ico` compiled into the executable as a resource. Both come out of the same
  painting — see [Title art](#title-art).

### What it draws with

The game draws through **SDL's GPU renderer on Vulkan**, which is the same
`SDL_Render` calls it always made -- no line of the drawing code changed to
move -- and which lets a few shaders of the game's own ride on top:

- **Water runs.** A river's own tile art slides along its current a pixel at a
  time, swaying across it, with glints riding the stream and a broken line of
  foam where it meets the bank. A pond, a lake and the Bayou's open water only
  sway and glitter. Which way water runs is worked out once per map from the
  shape of it: a long, narrow run of water is a river and runs along its
  length (south if it stands upright on the map, east if it lies across); a
  run wide both ways is still. The Whisperwood's stream runs; Fernhollow's pond
  does not.
- **Lava churns.** The crust drifts downstream and bends, the hot specks run
  under it, a slow wave of heat passes over, and now and then a bubble swells.
- **The air wavers** over lava, over forges, hearths and braziers, and behind
  a fireball -- a pixel sideways, row by row, more above the heat than beside
  it -- so the drawbridge's chains and anyone at the edge waver with it.

With **Visual Effects** on (Options, then Visual Effects), a good deal more:

- **Blows and what they leave.** A struck monster flashes white, or the
  colour of the spell that struck it; the player flashes red. What is on a
  monster is drawn on it rather than tinted over it: flames lick up off a
  burning one, a frozen one is cracked ice with glints in it, sparks crawl
  round an electrified one, green bubbles rise through a poisoned one, a sheen
  slides down a wet one, blood runs down a bleeding one. A leader winding up
  its heavy glows red round its outline. And they die the way they would: the
  dead crumble to dust, demons (and the Cinder King) burn out in embers,
  wraiths and shades go up into the air, and the rest fade.
- **Things that land hard.** A meteor, a dropped slab, a bolt from the sky and
  a leader's heavy blow send a shockwave out through the picture; the big ones
  shake the screen, and lightning from the sky flashes the view white and
  lights up the night around it. Spells in flight shine through the dark.
- **The Mana Shield** is a force field -- its skin brightest where it is seen
  edge-on, a honeycomb faint in it, and a ripple across it from every blow it
  takes. An **Electro-Node** is a glass orb with the charge swirling in it.
- **Towns.** Windows are glass by day and light up at dusk; lamps and fires
  throw a halo into the air after dark; banners, tapestries and tents stir in
  the wind; the College fountain and the wells have running water; a woken
  waystone's runes breathe, and waking one sends a ring and a rise of motes
  off it.
- **The land.** Grass, reeds, herbs, bushes and trees sway, with gusts
  rolling across a field as a wave. Things in the water leave rings -- a lurker
  waiting under the Bayou, a swimming duck's wake -- in place of the drawn
  rings there were. Whatever stands by water is seen upside down in it, and
  the palace's towers show dark in their moat with their windows lit. Mist
  lies on the Bayou's water, over Hollowrest and on the crypt's floors,
  thicker at night. Lava lights what is round it and stays lit at night.
  Every place has its own colours -- the Ashen Path and the palace red-orange,
  the Ice Spire cold, the golden hour warm, the dead of night drained -- and
  the Reverie's edges swim and its lights bloom, more the deeper you go. The
  foot of every bank sits in a soft shadow, and in the palace the high
  windows lay stained-glass light on the floor.

The Visual Effects page turns all of that off at once, for the plain look,
and has its own switches for the parts some people would rather not have:
**screen shake**, **flashes**, **colour fringing** and **screen distortion**
(the heat, the shockwaves and the dream's swim).

Everything moves in whole art pixels, never smeared: a tile's pixel is two of
the world's, and it moves two at a time. The shaders are GLSL in
`src/shaders`, compiled to SPIR-V in `assets/shaders` by the build; the code
that feeds them is `src/systems/shaders.*`, and what the world tells them each
frame -- and the passes it draws with them -- is `src/world/world_screen.cpp`.
Which scenery sways, lights or runs is decided from its art's name
(`Shaders::ArtOf`), so a new tree or banner joins in without being listed.

None of it is needed to play. Without Vulkan the game makes the renderer SDL
would have picked (Direct3D 11 on Windows) and draws plain: the water and lava
still, as they were before, and none of the rest. To compare the two, or to get round a driver that
misbehaves, set `SDL_RENDER_DRIVER=direct3d11` (or `opengl`, ...) before
starting it; `SDL_GPU_DRIVER=direct3d12` would want the shaders as DXIL, which
are not built, so it draws without them.

---

## Assets

**Clone it and run it.** Every image the game loads is its own — modelled,
rendered or drawn by the tools in `tools/` — so `assets/` is committed and
there is nothing to download first.

| What | Made by |
| --- | --- |
| Characters, their armour layers and the town NPCs | `blender_character.py` |
| Every monster | `blender_creatures.py`, and the twenty-five that fill the level ladder `blender_bestiary.py` |
| Props, scenery, buildings, chests, doors | `blender_props.py`, and the Brimstone Palace's `blender_palace.py` |
| Ores, bars, weapons, armour icons, the weapon in hand | `blender_tiers.py` |
| 93 ground and interior tiles | `make_ground.ps1` |
| Ground decals, item icons, the HUD | `make_decals.ps1`, `make_icons.ps1`, `make_ui.ps1` |
| Spells in the air: fireball, water orb, stone shard, gust | `make_effects.ps1` |

The game began on free [CraftPix](https://craftpix.net) packs, whose licence
permits using the art in a game but not passing the files on — which meant the
repository could not carry its own art and a clone was a game with no pictures
in it. Everything has since been replaced, piece by piece; `docs/ASSETS.md`
records what makes what, and what each replacement had to get right.

Four optional CraftPix icon packs are still supported for painted equipment
art, and `tools/import_assets.ps1` exists to import those and nothing else. The
game plays identically without them.

No font is bundled either: `src/ui/ui.cpp` falls back through Consolas, Segoe
UI, Arial and DejaVu Sans, so text renders the same anywhere.

### Title art

`art/` holds the cover painting and the icons made from it — see
[Title art](#title-art-1) below.

| File | What it is |
| --- | --- |
| `art/dreamquest_cover.png` | the cover painting: a sword under a crescent moon, between ruined pillars |
| `art/app_icon.png` | 256px square off the middle of it, loaded at startup for the window and taskbar |
| `art/dreamquest.ico` | the same square at six sizes, compiled into the `.exe` for Explorer |

`.	ools\make_titleart.ps1` makes the two icons from the painting; run it again
after replacing the painting. There is no pre-scaled background file to keep in
step with anything, because the menu crops and scales the painting itself at
whatever size the window happens to be.

---

## Playing together

**Up to four friends in one Hollowmarch, over a tailnet -- or two of you on
one couch, in [split screen](#split-screen-two-at-one-machine).** One machine runs
the world -- a friend's own game, or the headless server on a machine that is
always on -- and the others are windows onto it. You fight the same monsters,
open the same chests and fell the same trees, go your separate ways across
maps, keep your own character, and share one clock. This is the *Hollowmarch
Co-op* plan, built milestone by milestone:

| Milestone | What it adds | State |
|---|---|---|
| **M0** Skeleton | ENet, the transport, `--host` / `--join`, the greeting with version and data hashes, a chat line | **done** |
| **M1** Two bodies | several players in one `World`, inputs by the step, prediction and correction, remote players drawn in their own clothes | **done** |
| **M2** One fight, shared | monsters, shots, burning ground and loot told to every machine; monsters go for whoever is nearest; a friend's swing rolled by the host, against where the monster was when they saw it | **done** |
| **M3** Everything you can press E on | E, a thing dropped, a bed chosen are sent as actions; panels, the bag, experience and journal lines come back as deltas; chests and trees are everyone's, journals are each player's | **done** |
| **M4** Splitting up | one world per occupied map; doors move a friend between them; a map nobody is on is let go after a minute | **done** |
| **M5** Keeping it | a friend's character kept on their own machine; their place kept by the host; a stand-in for a dropped line; dawn when everyone is abed or dreaming | **done** |
| **M6** The tailscale box | `DreamQuestServer.exe`: the same world with nobody at the keyboard; a password at the door | **done** |
| **M7** Polish | name tags, the party strip, sounds passed on | **done**; tuning against a real relayed line is yours to do |

### Hosting and joining

**Play Together** is on the title screen and the pause menu.

- **Host a world**: start or load your own game first, then host from the
  pause menu -- whoever joins walks into that game. It listens on UDP 7777. The
  screen says what your friends should type: this machine's name and its
  tailnet address (the `100.x.y.z` one). Windows Firewall asks once -- allow
  DreamQuest on **private networks**, which is what the Tailscale adapter is. A
  light under the address turns green the first time anybody reaches the
  machine from outside; if a friend cannot connect and it stays dark, the
  firewall is eating the port.
- **Join**, from the title screen, takes a name or an address, with `:port` if
  the host is not on 7777. `Ctrl+V` pastes. Left and right step through the
  last five hosts dialled. Nobody answering is given up on after eight seconds.
- **Character** is who you arrive as the first time. After that your kept
  character arrives as who they are.
- **Password**: hosting, a word friends must give at the door; joining, the
  word the host gave you. Empty, and the tailnet is the door.
- **Say** types a line to everyone; away from this screen what is said arrives
  as a toast. **Name** is what friends see you as, remembered in
  `settings.json`.

From a shortcut or a terminal: `DreamQuest.exe --host`,
`DreamQuest.exe --join subzero:7777 --name Oona --password barley`.

### Split screen: two at one machine

**Esc, then *Player Two joins*.** The screen splits in two, side by side, and a
second player plays on a controller: their own character, their own bag and
journal, their own half of the screen. With two controllers plugged in,
pressing **Start on the second one** does the same without the menu.

- **Who holds what.** With one controller, it is Player Two's and the keyboard
  is Player One's. With two, each has a controller and the keyboard stays with
  Player One. Neither hears the other's.
- **Who Player Two is.** Left and right on the row choose who they arrive as
  the first time -- the hero, the warden or the wayfarer. After that they are
  their own kept character, in `saves/characters/<p2_name>.json`, written
  whenever the game saves and when they leave. `p2_name`, `p2_look` and
  `split_stacked` (one half above the other instead of side by side) are in
  `settings.json`.
- **It is the same co-op.** Player Two is a seat in the same realm friends
  across the wire sit in, with no wire: the same monsters, shared chests and
  trees, each to their own journal with kills counted in both, things changing
  hands by being dropped, one night for both -- and **you can go your separate
  ways**: through a door on your own, and the halves show different maps. It
  works while hosting, too: friends online see Player Two like anyone else.
  (A guest in someone else's world plays alone: their window is the host's.)
- **Panels are whoever opened them.** Their bag, skills, journal, the shops
  and conversations are the same panels, served to Player Two, with their
  controller's prompts and a line saying whose it is. A panel takes the whole
  screen and stops the game for both, as a panel always has.
- **Falling.** Player Two, fallen, reads the same screen and is got up in
  Havenbrook; Player One is wherever they were.

How: the game serves one seat at a time. Everything in `Game` is written for
"the player" -- `world->player`, `quests`, `input`. `ServeSeat(1)` points
those at Player Two (the world they are on, acting as them through
`World::BeginActing`; their journal; their controller through `Input::Borrow`)
and everything written for the player works for them, the HUD and every panel
included. Each half is drawn into a texture of its own size and then placed,
rather than through a viewport on the window, because the night's light map,
the dream's stars and the HUD all ask how big the output is. It lives in
`src/ui/splitscreen.cpp`; `--p2` sits Player Two down at launch without a
controller and `--hold2 left 2 3.5` holds one of their buttons, for checking
the halves without a second pair of hands.

### What is shared, and what is yours

- **The world is the host's**, and only the host rolls dice over it: monsters
  and their health, shots in the air, burning ground, what lies on the floor,
  chests opened, levers thrown, trees felled and seams worked out, the clock,
  and what the traders have sold today. A chest opened is opened for everyone;
  first come, first served. Monsters go for whoever is nearest, with a margin
  so two friends either side of a boar do not have it spinning. A kill counts
  in the journal of everyone on the map. No friendly fire.
- **Your character is yours**: bag, equipment, skills, talents, journal,
  recipes and spells learned, your storage chest. It lives on your own machine.
  The host keeps a copy (your *sheet*, sent when it changes) so that its rolls
  use your numbers, and tells you what the world added or took -- a coin picked
  up, logs chopped, raw meat cooked, experience, a line in your journal. So
  every panel in the game works for a guest exactly as it does alone, with no
  round trip: the bag, the skill trees, the journal, shops, crafting, the
  enchanting table, storage, dialogue, the boards.
- **Things change hands by being dropped.** Whoever drops a thing must step
  clear of it before it can be theirs again; a friend standing by can pick it
  straight up.
- **Dropping out and coming back.** Your character is written to
  `saves/characters/<name>.json` on the autosave, when you save, when you quit,
  and when the line drops. Join again, today or next month, and you have your
  bag, your levels and your journal, and the host puts you back on the map and
  the spot where you left off -- not beside the host. If the line drops mid-fight
  your character stands where it was, out of the fight and unhurt, for thirty
  seconds in case you come straight back. The host also keeps a copy of every
  friend's character as it last saw it, under `saves/characters/kept/`, in case
  their own machine loses theirs. A world made with `bring_your_own: false` in
  `settings.json` (or `--start-here` on the server) keeps its own characters:
  one made there stays there, in `<name>@<world>.json`.
- **Splitting up.** Every map someone is on keeps running, on the host; a door
  moves you between them. When the host walks out of a map friends are on, they
  stay, in a world of their own; when the host walks into one, it finds the
  place as they have it -- the boar half dead, the coins on the ground. A map
  nobody is on is let go after a minute; what happened there that matters is
  in the world's flags.
- **The night.** A bed asks how you would spend it, and each of you answers
  for yourself. *Go into the Reverie* takes you to the dream, which is a map
  like any other, while the others keep the evening. *Sleep through the night*,
  in company, is lying down: out of the fight, nothing can hurt you, and the
  clock keeps its pace until **everyone is abed or dreaming** -- then it is
  dawn for all at once, and dreamers wake where they lay down. Any key gets up.
- **Falling.** A guest who falls reads the same screen and is got up in
  Havenbrook, whole, wherever everyone else is.

### The server on the tailscale box

`DreamQuestServer.exe` is the same `World`, the same door and the same co-op
host with no window, no renderer and no sound, so nobody has to host. It has no
player of its own: everyone who joins is a guest. `build.ps1 -Server` builds
it, `package.cmd` puts it in the zip, and it needs `data/` and `maps/` beside
it, not `assets/`.

```bash
DreamQuestServer.exe --name "The Hollowmarch" --password barley
```

`--port 7777`, `--map town_havenbrook` (where newcomers arrive), `--world
saves/server_world.json` (the world's one-shots, the clock and the traders'
day, written every two minutes and on Ctrl+C), `--kept <dir>`, `--start-here`.
It prints the addresses friends should dial and who is here as they come and
go. Run it as a scheduled task or a service on the always-on machine.

### How it works

1. **Hands, not the keyboard.** `Player::Update` reads a `PlayerInput`: the
   move axis and three bytes of buttons -- held, just pressed, just released.
   The seat at this machine has it filled from the device; a friend's
   character on the host has it filled from what their machine sent.
2. **The step carries its own clock.** The game's loop runs at the display's
   rate (72 Hz on the machine this was written on), so rather than put a fixed
   tick under single-player, a step is sent with its own `dt` in whole
   microseconds and a quantised axis, the way Quake's `usercmd` is. A guest
   steps with those numbers the instant the keys are read and the host steps
   its copy with the very same ones: the self-test walks six hundred uneven
   steps both ways and the two agree to the last bit. Every packet repeats the
   last eight steps, seven bytes each, so a lost one costs nothing.
3. **Acting as.** Everything the single-player game does is written for "the
   player". `World::ActAs` swaps a friend's `Player`, and the world's state
   about them (`SeatState`: who they are fighting, the log they are chopping,
   the door they are halfway through, their dream, their journal), into the
   place of the host's -- so their swing landing, their axe biting, the coin at
   their feet, the orc that hits them and the chest they open are the same
   lines of code, unchanged. `World::Update` is `UpdateSeat` for whoever is at
   this machine and `UpdateShared` for the place; `StepGuest` is `UpdateSeat`
   acting as a friend, to their clock.
4. **Twenty times a second** the host tells each friend who and what is within
   a thousand pixels of them: players, monsters (twelve bytes each -- a monster
   is the nth of the map's own list, so nothing else about it need be said),
   loot, shots, burning ground. A window poses them a tenth of a second in the
   past, between the two snapshots that bracket that moment.
5. **Putting right, not rolling back.** When the host says where a step really
   ended and it differs by more than two pixels, the difference is added to
   where the character is now and to the remembered path: at once if small,
   over a few snapshots if it would show, as a snap if it is a teleport. A
   `Player` carries its bag and skills as well as its feet, so it cannot be
   rewound without undoing a level gained in between.
6. **Actions up, deltas down.** A guest's window decides nothing: E, a thing
   dropped, a bed chosen, food eaten, a purchase from a shared shelf are sent
   as `Action`s; what comes of them comes back in a `Delta` -- floating text,
   flags, a panel to open, bag changes, experience, journal events, the chain
   counter, sounds, the traders' ledger.
7. **The rewind.** A friend sees monsters a little in the past, so while their
   swing is resolved the monsters stand where they were 150 ms ago.

| File | What it is |
|---|---|
| `src/entity/player_input.h` | A player's hands for one step. |
| `src/world/world.*` | `guests`, `SeatState`, `ActAs` / `AsSeat`, `UpdateSeat` / `UpdateShared`, `StepGuest`, `HandOver`, and `visiting`: a guest's window. |
| `src/net/protocol.*` | Protocol version 3: `InputFrames`, `Snapshot`, `Enter`, `Outfit`, `Sheet`, `Action`, `Delta`. The net layer hands these to the game whole. |
| `src/coop/coop.*` | Where the wire meets the world: `coop::Host` (the realm), `coop::Guest` (the window), and the character file. |
| `tools/server_main.cpp` | The headless server. |

Flags for checking all this without a second pair of hands: `--scratch hero`
starts a game that is never written anywhere, `--hold D 2 3.5` holds a key
down between two moments, `--say "a line"`, and `--shot file.png 5` (with
`--frames 8 0.1`, eight pictures a tenth of a second apart). A scratch
game can start anywhere and in anything: `--map brackenwood from_westwold`,
`--wear steel_hide_head,steel_hide_body,steel_hide_legs`, `--level 40`,
`--hour 22`, `--learn trail_legs,broadheads,arrow_rain`, `--quest q_marens_letter`,
`--charge 1.0` (how full [the lightning's battery](#the-lightning-and-the-battery)
starts), `--learn zap:call_of_thunder` (which of the lightning is on the fifth
key), `--screen controls` (or `skills:magic`, `shop:havenbrook_tannery`,
`craft:workbench`, `orders:npc_nessa`, `journal`, `map`, `tree`, and the rest).

**`--audit`** opens every menu in the game at three window sizes, walks each
list's cursor down every row of it, and prints any line of text drawn outside
the panel it belongs to or off the edge of the window. It is how the skill
tree's cost line ("Attack 47, a point a rank, after War Cry"), the fishing
milestones and the storage chest's description were found running off the side;
run it after touching a panel, because a long name somewhere down a list is not
something anybody notices by looking. The play HUD goes through it as well, posed
with a sword, with a staff and as each half of a split screen, for the other way
a screen goes wrong: each piece at the foot of it names the room it takes
(`UI::Claim`), and any two that meet are printed -- which is what would have
caught the spell's name being written over the key hints.

What is still plain: a friend's chopping shows the swing without the axe in
hand on other screens; camps are the host's to pitch; and a line relayed
through Tailscale's DERP has not been tried -- `tailscale ping` says which you
have, and the constants at the top of `coop.h` are the ones to tune.

### The door

A friend on yesterday's build has an orc with different hit points and a map
with a wall somewhere else, and nothing about that shows until a fight goes
differently on two screens. So the first thing said on a new line is a
greeting carrying the protocol version and a hash each of `data/*.json` and
`maps/*.mx`, and the server checks, in order: that it is DreamQuest knocking
at all, the protocol version, the data, the maps, and whether a seat is free.
The first failure is sent back as a sentence a player can act on -- *"Your
data/ folder differs from the host's (9f3a61c2 against 1b7d02e4). Both of you
need the same build."* -- and the line is dropped once that has gone out. The
same eight digits are in the corner of the Play Together screen, to read to
each other. Carriage returns are skipped by the hash, so a clone with git's
`autocrlf` on and a zip from one with it off still agree. Someone who connects
and never says hello is dropped after five seconds.

### How it is built

The wire is in `src/net/`, and none of it includes SDL or the game, so the
headless server of M6 can use it as it stands. (`src/coop/`, above, is where
it meets the world.)

| File | What it is |
|---|---|
| `transport.h` | The wire and nothing else: `Connect`, `Disconnect`, `Send`, `Poll`, `Peers`, over two channels -- reliable-ordered and unreliable-sequenced. |
| `transport_enet.cpp` | ENet over UDP. The only file that includes ENet, and with it `<windows.h>`. Linked statically, so co-op adds no DLL to ship. |
| `transport_loopback.*` | The same promises with no sockets: one process, one thread. The host's own client reaches its server through one, so playing as host runs the same code a friend's machine does; the self-test plays a server and a handful of clients against each other on one. It can be made slow and lossy on purpose. |
| `protocol.*` | `ByteWriter` / `ByteReader` -- little-endian, explicit widths, every read checked, every string limited -- and one `Encode` / `Decode` per message: `Hello`, `Welcome`, `Refuse`, `Roster`, `Say`, `Chat`. |
| `datahash.*` | The FNV-1a hashes of `data/` and `maps/`. |
| `server.*` | The door, the seats and the chat line. Listens on any number of transports at once. |
| `client.*` | Knocking, being seated or refused, the roster, the chat log. |
| `session.*` | What the game holds: offline, hosting (a server on ENet plus its own client on a loopback) or a guest (a client on ENet). |

The screen itself is `src/ui/lobby.cpp`. Typing is the one thing in the game
that is not an `Action`: while a field is being typed into, key presses go to
it instead of the input map, or `J` would confirm, `K` would back out and WASD
would walk the cursor away in the middle of a name.

---

## Controls

The game is played on the keyboard or a controller; the mouse does nothing.
Both are live at once by default and the game switches to whichever you last
touched. Options → Input Device pins it to one if you would rather.

On the keyboard the left hand steers, sprints, jumps and interacts, and the
right rests on `J` `K` `L` for the fight, with the panels on the row above.

| Action | Keyboard | Controller |
| --- | --- | --- |
| Move | WASD / arrows | Left stick or d-pad |
| Light attack | `J` | X (west) |
| Heavy / charged attack | `K` (hold to charge) | Y (north) |
| Lock on / next target | `L` | Right trigger |
| Block (hold, with a shield) | `H` | B (east) |
| Abilities, once learned in the skill tree | `H`+`J`, `H`+`K`, `H`+`L` | B + X, B + Y, B + right trigger |
| Sprint (hold) | `Shift` | Left trigger |
| Jump / climb | `Space` | Left stick click |
| Interact | `E` | A (south) |
| **Menu**: inventory, skills, spellbook, quests, map | `Tab` | Select (Back) |
| **Abilities** (hold, with light, heavy or lock on) | `H`, or `F` | **RB** |
| Inventory | `I` | LB |
| Skills | `O` | in the menu |
| Quest journal | `P` or `Q` | in the menu |
| Select element | `1` `2` `3` `4` | — |
| The lightning, and its next spell | `5` | — (it is a stop of *cycle element*) |
| The ancient magic, and its next page | `6` | — (it is the last stop of *cycle element*) |
| Cycle element, the ancient magic included | `R` | Right stick click |
| **The spell in the slot chosen**, back and on | `[` `]` | **Right stick pushed left / right** |
| The spellbook: what is on every slot | `O`, then `O` twice more | RB, then RB twice more |
| Drop what the cursor is on (in the bag) | `G` | Y (north) |
| Lift a thing and put it down elsewhere (in the bag) | `F` | RB |
| **Reorganize** the bag, or the chest's side the cursor is on | `R` | Right stick click |
| Pause | `Esc` | Start |

**Changing a slot's spell mid-fight.** Push the right stick left or right
(or press `[` / `]`) and the slot that is chosen -- the lit box in the bar --
steps to the spell before or after it, out of what that slot has on offer: for
fire, water, earth and air, what the weapon in hand reaches of the element that
the Magic level casts (a grimoire's fire is Ember, Pyre, Flame Ring and Wall of
Fire; a wand's trades the Wall for the Flamethrower); on an element's own
staff, its four; on the
lightning and the ancient magic, what is known of them. It is the spellbook
page's choice, made without the trip to the menu, and it is kept the same way
-- in the save, and on a friend's host. The name under the bar has a `<` and a
`>` round it wherever there is more than one to choose, and lights up for a
moment when it changes. Landing back on the weapon's first bolt lets go of it,
so it goes on growing with the Magic level as it always did. A push is one
step: the stick has to come most of the way back before it steps again. Both
are on the Controls page, where they can be moved like anything else.

In menus the fighting keys double up the way a controller's face buttons do:
`J`, `E`, `Space` or `Enter` confirms, and `K`, `Backspace` or `Esc` backs out.
A panel's own key closes it again. The death screen ignores input for its first
moment, so the last swing of a lost fight does not skip straight past it.

### Changing the keys and the buttons

That table is how the game ships, and none of it is fixed. **Options ->
Controls** lists every action with its key and its button in two columns;
`Left`/`Right` picks the column, confirm on a cell listens for whatever is
pressed next, and that is the action's key from then on. The choices are kept
in `settings.json` (`"controls"`, by SDL's own names for keys and buttons, so
the file can be read and edited) and Player Two's controller follows the same
buttons. Two rows at the bottom put either column back as it shipped.

Three rules keep it from being a way to break the game:

- **A change is a swap.** Give the light attack the heavy attack's key and the
  heavy attack takes the light attack's. Nothing is ever left with no key, and
  no key ever does two things. The screen says who moved where.
- **What gets you out of a mistake cannot be moved.** `Esc` and Start pause,
  `Enter` confirms and `Backspace` backs out, and the arrow keys and the d-pad
  steer a menu, whatever else has been done. (`Esc`, or Start, is also how
  listening for a key is called off.) A controller's left stick always steers.
- **The menus follow the actions, not the keys.** On the keyboard the light
  attack's key confirms -- with Interact's and Jump's -- and the heavy attack's
  backs out; on a pad Interact's button confirms, Block's backs out, and the
  heavy attack's drops things in the bag. Move the attack to `F` and every
  prompt under every menu says `F`: prompts are read off the bindings
  (`Input::PromptFor`), never written down.

The triggers are axes, but they are held like buttons and are bound like them,
so sprint and the lock can be moved off them and anything else onto them. Any
button SDL reports can be used, which includes a controller's back paddles:
worth knowing on a **Steam Deck**, where the Guide button is Steam's own and
never reaches the game, so the map wants putting somewhere else -- `L4`, say,
once Steam Input is passing the paddles through. The three spare keys (`Tab`
for the bag, `Q` for the journal, the right `Shift` for sprint) stay spare until
something else asks for them.

None of the game's own text names a key -- the trainers say "press use" and
"hold the heavy swing" -- so nothing anybody says goes out of date.

`Bindings` (in `src/input.h`) is the whole of the model; `Input::Listen` and
`TakeHeard` are how the screen hears a key without the game hearing it too, and
everything held is let go when listening starts or the bindings change, because
the key that was holding it may be about to mean something else.

### Blocking

Hold `H` -- or B on a controller, which only means Back in menus -- with a
**shield** in the off hand to raise your guard. A lantern is not a shield. The
guard stops blows from in front of you, melee swings and shots alike; a blow from
behind finds your back. While it is up you step slowly, cannot swing and cannot
sprint, and with a monster targeted you keep facing it as you move.

A shield turns aside a share of every blow it takes, and each one costs stamina:

    stamina = the blow's damage x the root of the attacker's level x 1.6 x the shield's multiplier

so a rat's nip costs next to nothing and a dragon's bite empties the bar. It
used to be the level outright and not its root, which priced a wooden shield out
of any fight past the meadow: an average blow from the Orc Warchief asked for 168
of a 100-point bar, so the shield turned a sliver of it and broke. The root keeps
a dragon dearer to stop than a boar without making a beginner's shield a thing
that only works on boars. If a
blow costs more than you have left, the shield stops only the share you could pay
for, the bar empties, and the **guard breaks**: it will not come up again until
the bar has refilled to the same point a winded sprint waits for. The stamina bar
turns steel blue while the guard is up and pulses red while it is broken.

Everything a shield stops trains **Defence**, at 4 XP a point -- the rate a hit
trains the skill it was made with -- on top of the XP any blow that gets through
already gives.

Every tier of shield blocks more and costs less:

| Shield | Stops | Stamina | | Shield | Stops | Stamina |
| --- | --- | --- | --- | --- | --- | --- |
| Wood | 50% | x1.00 | | Orichalcum | 75% | x0.26 |
| Bronze | 54% | x0.80 | | Diamond | 79% | x0.21 |
| Iron | 58% | x0.64 | | Platinum | 83% | x0.17 |
| Steel | 62% | x0.51 | | Demonite | 87% | x0.13 |
| Azuryte | 66% | x0.41 | | Dracon | 91% | x0.11 |
| Damascus | 70% | x0.33 | | Enchanted | 95% | x0.09 |

What an average hit costs to block, out of a bar of 100:

| Monster | Highest stat | Wood | Bronze | Steel | Azuryte | Diamond | Enchanted |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Orc Grunt | 5 | 5 | 4 | 3 | 2 | 1 | 0 |
| Lizardman | 10 | 13 | 10 | 6 | 5 | 3 | 1 |
| Lizardman Chief | 15 | 22 | 17 | 11 | 9 | 5 | 2 |
| Orc Warchief | 28 | 51 | 41 | 26 | 21 | 11 | 5 |
| Ice Troll | 29 | 52 | 41 | 26 | 21 | 11 | 5 |
| Hoarfang | 66 | 221 | 177 | 113 | 91 | 46 | 20 |

(The level a block is priced by is the attacker's highest combat stat --
`CombatLevelOf` -- which is what the column gives. Under the old rule the
Warchief's row began 168 and Hoarfang's 1122.)

The rule is `ResolveBlock` in `src/systems/combat.h`, and every blow that lands on
the player goes through `World::HitPlayer`, so no monster or projectile can skip
the shield. The guard pose is its own clip, `block`, rendered for every character,
armour cut and tier weapon.

### Parrying

A **dagger with no shield behind it** does not block: the same button raises it
to **parry**. A pair of daggers parries too, since a second dagger is not a
shield. A dagger with a shield in the other hand blocks with the shield.

The parry's first quarter of a second is **the moment**. Every blow from in
front that lands in it is caught outright: nothing gets through, not even a
leader's heavy blow that shatters a shield's guard. Each catch costs 6 breath
and trains Defence the way a block does. Whoever threw the blow **reels** for six
tenths of a second, or a full second if it was a leader's heavy blow, which a
leader braced in its wind-up cannot shrug off.

Held past the moment, the parry is only a **poor guard**. It turns aside 35% of
a blow at a wooden shield's price in breath, and breaks like any guard when the
bar runs out. Once you let go, the next moment is ready after about half a
second. Raise it again sooner and you get the guard, not the moment, so tapping
B over and over is not a wall. A blow from behind still finds your back.

Everything else is the block's: you step slowly while it is up, and you cannot
swing or sprint. It plays the `block` clip. The HUD reads *Parry!* during the
moment and *Parrying* after it. The rules are `Player::TryParry` and
`World::Parried`. A blow comes through `World::HitPlayer` (a leader's heavy
through `HeavyHitPlayer`), which asks the dagger before it asks a shield.

### Sprinting

Hold `Shift` (or the left trigger) while moving to sprint at 1.6 times running
speed, for crossing the world rather than for fighting: it cannot start during
a swing or a charge, and taking a hit knocks you out of it for most of a second.
The camera leads a sprint by a few steps so you can see what you are running
into, footfalls come further apart, and each one kicks up dust outdoors. The
hero has a sprint animation of its own; the CraftPix characters, which do not,
play their run faster instead.

Sprinting costs **stamina**, the amber bar with the lightning bolt under health
and mana. A full bar is 100 and a sprint spends 22 a second, so about four and a
half seconds flat out. It starts coming back 0.8 seconds after you stop --
30 a second standing still, 60% of that on the move -- so a breather is worth
taking. Run it dry and you are **winded**: the bar pulses red, you hear the
character catch their breath, and you cannot sprint again until it has refilled
to 35%. That threshold is what stops an empty bar being tapped into a
stuttering sprint. Respawning restores it; it is not saved, because it is back
to full within seconds of any load anyway.

### Dropping things

In the bag, `G` -- Y on a controller, which only swings outside it -- drops
what the cursor is on at your feet. A single thing goes at once; a stack asks
for a second press ("Press G again to drop all 30 Logs"), so a purse of coins
is not one slip of a finger from the floor. What is dropped lies where it fell
and can be picked back up, but not by the feet that dropped it: you have to
step clear of it first, or the bag would scoop it straight back. It lies there
for three minutes, and leaving the map loses it sooner -- which is as close to
destroying a thing as the game gets, and all the destroying a thing needs.

Keys, letters and seals cannot be dropped ("You had better hold on to that").
Each was handed over exactly once by somebody who could not hand it over
again, and a house key on the floor of a map you have left is a house you can
never get into. An item says so with `"keep": true` in `data/items.json`. A
strange thing found in the world is kept the same way until the quest it
started is finished, and after that it is yours to drop.

### Moving things, and tidying the bag

`F` -- RB on a controller -- **lifts** what the cursor is on. It shows faint in
its own square with a gold edge, and in your hand over whichever square the
cursor moves to; `F` again, or `J`, puts it down there. Onto an empty square it
goes there; onto something else the two change places; onto more of the same
thing they become one stack. `K` puts it back where it came from.

`R` -- a click of the right stick -- is the **Reorganize** button beside the
bag's title. Every stack of a thing that stacks is joined into one, everything
is sorted by what it is, and the gaps all go to the end: coins first, then what
is worn and wielded (weapons, then the rest by where it is worn), tools and
lights, what is eaten and drunk, recipes and tomes, what things are made of,
trophies and gems, and a quest's things last. Within each, by tier and then by
name, so the same bag always tidies the same way and tidying a tidy bag
changes nothing. Nothing is added and nothing is lost (`Inventory::Reorganize`,
with the order in `Inventory::SortRank`).

The storage chest has the same button: `R` tidies whichever side the cursor
is on -- the pack after a round of stowing, or the chest itself.

### A bigger bag

The bag is four rows of seven, and twenty-eight slots stops being enough at
about the time there are three kinds of hide to carry. It grows: there are four
bags, each of which adds a row when it is used from the inventory, to eight rows
and fifty-six slots with all of them.

| Bag | Crafting | Made of |
| --- | --- | --- |
| Hide Satchel | 12 | 14 raw hide, 4 bolts of cloth, 8 waxed thread |
| Wolfskin Pack | 28 | 12 wolf pelts, 6 lizard scales, 5 bolts of cloth, 10 thread |
| Bearskin Rucksack | 46 | 12 bear hides, 4 troll hides, 10 spider silk, 12 thread |
| Greatwolf Haversack | 68 | 8 greatwolf pelts, 6 dire bear hides, 4 wyvern scales, 12 dream shards |

They are dear on purpose. Each is made at a workbench -- nothing in one is
metal or brewed -- out of the same hides the ranged armour wants, a good deal
more of them than a jerkin takes, so a bag is a decision about what not to make
instead. The other way to come by one is luck: every chest table has a bag or
two in it at a few chances in a hundred, the satchel in the barrels by the road
and the haversack only at the far end of the world (`data/loot_tables.json`;
the self-test holds every one of them under six in a hundred a roll). A bag
found is worth having at any level, since wearing one asks nothing; a bag
found twice is worth what it sells for.

One of each, in any order: a second satchel is refused ("You already carry a
Hide Satchel") and stays in the bag. They are not worn in a slot and are never
taken off, which is what lets the bag only ever grow -- nothing has to decide
where a row's worth of things goes when the row does.

In the data a bag is `"use": "bag"` with `"bag_slots": 7`. `Player::WearBag`
takes it out of the inventory and adds its id to `Player::bags`, which is saved
with the character as `"bags"`; the size of the inventory follows from that
list (`Player::BagSlots`) and is set before the inventory is read back, so the
last row of a save has somewhere to go. It is the character's and not the
world's, so it travels in the sheet a guest sends their host, and the host's
copy of a friend has the same room in it as the friend does. A save from before
there were bags has no list and loads at twenty-eight, as it always did.

The inventory screen grows downwards a row at a time, and at eight rows the
squares shrink a little -- by however much the window's height asks for -- so
the whole of it still fits. The storage chest's panel already laid the bag out
by rows and needed nothing. `--wear bag_satchel,...` with `--scratch` shoulders
bags for a look at it.

### What the attack buttons do

The buttons never change; the weapon in your hand decides what comes out of
them. A sword swings, a bow shoots, a staff casts. All three run through the
same attack state machine, so the charge mechanic works for every style.

### The three attacks

- **Light** — fast, cheap, and it chains. Three hits in a row, each a little
  slower and a little harder than the last.
- **Strong** — tap the heavy button. Slower, hits considerably harder.
- **Charged** — *hold* the heavy button. Past about a fifth of a second the
  swing starts charging and a meter appears under your feet; it turns bright
  when it is full. Release to fire. A full charge is worth roughly three times
  a normal strong hit and reaches further, but it roots you while it winds up.

### RB, and the menu of menus

Abilities were guard + light, heavy or lock on -- on a pad, **B + X**, which is
one thumb asked to be in two places. **RB is the abilities' shift now: RB + X,
Y and the right trigger**, and RB + A eats what is to hand. The keys keep the
guard for it (`H` and `J` are two fingers) and have `F` besides.

RB was the skills panel, and a pad had no button left to move it to. So
**Select opens one menu with every panel on it** -- inventory, skills,
spellbook, quests, map -- and the skills and the journal, which gave their
buttons up, are reached from there. `Tab` does the same on the keys, where
`I`, `O`, `P` and `M` still work as well. A pad layout saved before this had
RB on the skills panel and would have taken it straight back, so a saved
layout says which set of defaults it was made over (`"layout"`) and an old
one's buttons are not applied; its keys are.

### The armoury

There were four weapons -- sword, spear, bow, staff -- in twelve tiers. There
are thirteen now, in the same twelve: every one forged or carved from the same
bars and logs, by the same skills, with a picture of its own, a model of its
own in the hand, and a strike of its own. None of them is a line of code: what
a mace *is* is what its line in `data/tiers.json` says, and the engine reads
the same dozen fields off every weapon (`ItemDef`, "the armoury").

| Weapon | Hands | Speed | What it is for |
| --- | --- | --- | --- |
| **Dagger** | one, or **one in each** | 0.72 | Short, quick, and all point: **a third of a target's Defence does nothing against it** (`armour_pierce`). Strikes with the thrust. [A second goes in the other hand](#a-dagger-in-each-hand). |
| **Mace** | one | 1.12 | The hardest one-handed blow, and one in five **concusses**. Its own overhead `bash`. |
| **Greatsword** | two | 1.45 | Half again a sword's reach and sixty per cent wider: the two-handed `sweep`, which is mostly wind-up and recovery, because that is what heavy looks like. 28% **bleed**. |
| **Greataxe** | two | 1.6 | The slowest and the hardest. The same sweep -- and **held and let go it is a chop** (`hew`): longer down the line, half as wide, a third harder. 32% **bleed**. |
| **Crossbow** | two | 0.55 | **It goes off the moment it is asked** -- no draw -- at 2.7 times the tier's power where a bow is 2.0, and the bolt goes through two bodies and past 40% of their Defence. Then it is **spanned again** for a second and a bit (`reload`), and nothing can be let off until it is. No combos: nothing chains off a weapon that has to be reloaded. |
| **Throwing knives** | one | 0.6 | A shield on the other arm. Quick and close: a light throw is one knife, a **heavy one a fan of three**. |
| **Wand** | one | 0.66 | Half again as many casts as a staff, each worth 0.72: about even over time, and much hungrier for mana. |
| **Grimoire** | one | 0.8 | 0.8 of a staff's cast, and **every spell costs a sixth less**. |
| **Orb** | one | 0.76 | 0.78 of a staff's cast, and **what it throws turns after its target**. |
| **Fire, Water, Earth, Air staff** | one | 1.05 | [Four spells instead of four elements](#an-elements-own-staff). |

**A thrown knife does not twang.** Throwing knives used to share the bow's
sound, which is a bowstring on a weapon that has no string: they carry
`thrown` in `data/tiers.json` now and get a sound of their own -- air, and a
thin edge turning in it.

**Combos.** The grammar is the sword's -- light-heavy, light-light-heavy,
heavy-light, both at once -- and each melee weapon has its own four, with its
own twist (`"combos"` on the piece): a dagger's *Gut Stab* goes past 70% of
Defence and always opens a wound, its *Backstab* is a quarter harder; a mace's
*Skull Crack* and *Ground Slam* always concuss; a greatsword's *Reaping Sweep*
always bleeds and its *Pommel Strike* concusses; a greataxe's *Hew* and
*Maelstrom* bleed and its *Haft Check* concusses. The HUD and the word over
your head say the weapon's name for it.

**Drawn.** `tools/blender_tiers.py` has a builder for each, cut from the same
per-tier tables as the sword, so a bronze mace is a bronze sword's cousin;
`tools/blender_character.py` has eight new clips -- `bash`, `sweep`, `hew`,
`shoot`, `reload`, `throw`, `flick`, `invoke` -- for all three characters, and
six that are versions of older ones: `offstab`, and the five `_2h` combos. A
great weapon is carried **over the shoulder** in every clip that does not swing
it: a blade as long as the man is tall, hung from the hand like a sword, drags
its point through the floor.

#### A dagger in each hand

A dagger is the one weapon the other hand will hold. With one in the right
hand, **equipping a second puts it in the left**, where a shield would be (the
shield goes into the bag); the bag, the shops and the anvil say *In your other
hand: speed, not bonuses* where they would say *Instead of* -- and show its
numbers without a change against them, because there will be none -- and the
worn list calls the row *off hand*.

| | One dagger | A pair |
| --- | --- | --- |
| Time between blows | 0.72 | **0.36** -- twice as fast |
| What each blow is worth | 1 | **0.75** |
| The second dagger's bonuses | -- | **not counted** |
| Shield | yes | no |

So a pair is about half again one dagger over time, for the price of the
shield arm -- roughly what the two-handed weapons pay for theirs. Twice the
speed *and* the second blade's Attack and Strength on top would have been three
times a dagger, and nothing else in the armoury would have been worth holding.
All of it is one block on the piece in `data/tiers.json`:
`"dual": {"speed": 0.5, "damage": 0.75, "clip": "offstab"}` -- set `damage` to 1
and a pair is simply two daggers' worth.

- **Hand after hand.** Light attacks go right, left, right: `thrust`, then
  `offstab` -- the thrust mirrored (`mirrored()` in
  `tools/blender_character.py`) -- then `thrust`. Combos, the charged heavy and
  everything else are the right hand's.
- **The rules of the left hand** (`Player::EquipFromInventory`). A third dagger
  replaces the right hand's. A shield takes the left hand back. Any other
  weapon in the right sends the left's dagger to the bag -- it is only ever
  held beside another -- and so does anything that takes both hands; with no
  room in the bag for it, the swap is refused and says why. Put the right
  hand's away and the left's changes hands.
- **Drawn.** The left hand has a grip of its own on the rig (`grip_l`), and
  every dagger is rendered a second time built on it:
  `layers/<clip>_4_weapon_off_<model>.png`, for every clip a dagger is carried
  through, drawn straight after the right hand's (`LayerStyle::offhand_model`).
  Each hand's is its own tier's: an iron dagger and a wooden one look it.
- **Shared.** A guest's outfit says what is in each slot and the host used to
  keep only what *belonged* in it, which would have thrown the left hand's
  dagger away; `ItemDef::FitsSlot` is the one place that says what fits where.

**The dagger itself was redrawn.** It was a cone a third the length of a
sword, and a cone that size is three pixels of blade and then nothing: the thin
half never survives being reduced to game size, so what was in the fist read as
a nail. It is a broad leaf with a short point now, two thirds of a sword's
length -- it keeps its width nearly to the tip, and that is what reads as a
blade at forty pixels.

#### Both hands on it

The first pass set the arms of the two-handed clips by eye, and the left hand
floated beside the hilt more often than it held it. The reason is the rig: an
arm is 0.235 long from a shoulder 0.19 out from the middle, so two hands can
meet only in a small pocket in front of the chest, and angles set by eye miss
it. A two-handed pose does not give angles now. It says

- **where the right hand is** (`hold_x/y/z`) -- from the point between the
  shoulders, *in the chest's own axes*, so a lean or a twist carries the hands
  with it and what could be reached still can be;
- **where the thing held points** (`aim_x/y/z`) and which way its edge faces
  (`edge_x/y/z`), in the character's axes -- so a sweep's blade is flat and
  leading with its edge, a chop's bit is into the blow, a crossbow is level
  and dead ahead with its prod square, and a book's pages are up;
- **how far along it the left hand sits** (`left_on`: toward the pommel, or,
  negative, toward the nose -- under a crossbow's stock),

and three small searches in `apply_pose` find the arm angles, the wrist and the
left arm that make it so (`_place_right_hand`, `_aim_grip`, `_place_left_hand`).
Where the mark is a little further than the left arm is long, the arm is let
out to meet it, up to 1.3 -- a pixel of forearm at this size, where a hand
floating beside a hilt is the thing you notice. `sweep`, `hew`, `shoot`,
`reload`, `flick` and `invoke` were all re-posed on it: the great weapons wait
with the blade out over the right shoulder (not up in front of the face, where
the head's sheet covers it), the crossbow no longer spins about its stock while
it is spanned, the greataxe's bit is turned a little to the side as it comes
down so that it is a blade from the front and not a line. The grimoire has a
dark cover showing round pale pages, and the orb is twice the size, clear of
the palm, and the violet of a spell with no element instead of a white ball.

**The combos, too.** A greatsword's Crushing Blow was the sword's, swung with
one hand, because the combos' clips are the sword's and there was one set. The
five that swing -- `rush`, `crush`, `cleave`, `backhand`, `spin` -- have a
second version each (`rush_2h` ... `spin_2h`) that is *the same pose, to the
frame*, run through the same solver: `two_handed(pose_fn)` measures on the rig
where the sword's pose put the right hand and where its blade pointed, brings
the hand in to where the left can reach the hilt too, and keeps the blade
pointing exactly where it did. The swing keeps its shape and its timing, and
nobody swings four feet of steel about one-handed. `Player::BothHands(clip)`
picks it, for a melee weapon that takes both hands, on a rig that has it.

#### A clip that lasts what its attack lasts

The armoury's strikes had no playback rule in `tools/make_sprites_json.ps1`, and
a clip with no rule loops at ten frames a second. The sword's clips play at a
fixed rate and are cut off where the attack ends, which suits six quick frames;
an eight-frame swing that is half wind-up does not survive it -- a greatsword
showed its wind-up, and the attack was over before the blade came round.

So `bash`, `sweep`, `hew`, `shoot`, `throw`, `flick` and `invoke` are marked
**`"fit"`** in `data/sprites.json`, and `Player::FitSwing` plays a fitted clip to
last exactly as long as the attack now under way: a greataxe's light and its
charged chop are the same eight frames over a third of a second and over a
whole one, and the weapon's slowness -- which was already in the attack -- is
in the swing. With that, every one of them was **re-timed so the blow is on the
screen while it is live**: an attack's hit is live from about a fifth of the
way through to about a half, so the frame that shows it landing is the fourth of
eight, the third of six, the second of five; and what is thrown or cast leaves
the hand on the second frame, which is when it leaves the world's. The first
pass had every blow in the middle of its clip, after the monster had been hit.
(`offstab` and the `_2h` clips keep the rate of the swing they are a version
of, and `reload` loops: spanning a crossbow goes on for as long as it takes.)

**And a bug the armoury found.** Every weapon sheet ever rendered had a suit of
plain plate armour drawn into it under the weapon: the renderer held the body
and the head out of the picture and had never been told about armour, which
came later. It was invisible on anyone wearing a full suit, which is most
people -- and a character with nothing on their legs grew steel greaves the
moment they picked up a sword. All 1,900 sheets, old and new, were rendered
again with the armour held out (a steel sword's sheet went from 3,841 opaque
pixels to 531).

### An element's own staff

Any staff chooses among the four elements with `1` to `4`, and throws each
one's bolt. **A Fire Staff casts nothing but fire -- and `1` to `4` are fire's
four spells.** Twelve new spells, three an element, at Magic 12, 24 and 36:

| | 1 | 2 (Magic 12) | 3 (Magic 24) | 4 (Magic 36) |
| --- | --- | --- | --- | --- |
| **Fire** | Ember, Pyre | **Flamethrower** -- light is five tongues across sixty degrees at arm's length; heavy is three, close together, that reach three times as far. Each is a breath, not a volley: a second flight follows a moment later in the gaps of the first, and the two together are worth what the one was | **Flame Ring** -- twelve patches of burning ground round where you stand | **Wall of Fire** -- seven across the way you face, for five seconds |
| **Water** | Spray, Torrent | **Hydro Cannon** -- one great ball: soaked, and thrown twice as far as a gust throws | **Tidal Wave** -- seven abreast, slowly, through everything | **Whirlpool** -- four seconds of water that drags what is in it to the middle |
| **Earth** | Sharpstone, Upheaval | **Slabstrike** -- a square of the ground torn up and swung at what is in front. Twenty pixels of it on a light, thirty-two on a heavy, and [held and let go the big one is dropped on your quarry](#slabstrike) and breaks on them. What it hits is thrown, and often concussed | **Sedimentary Rain** -- the Arrow Rain's numbers, in stone | **Mineral Burst** -- eight sharp stones, one after another, from wherever you have got to |
| **Air** | Gust, Galewind | **Tornado** -- it walks the way it was sent, throwing what it catches in any direction it likes, and **the further it is thrown the more it is hurt**. A light cast is a dust devil; **held and let go it lasts four seconds** | **Air Slash** -- an edge of air as wide as a doorway, through everything | **Turbulence** -- three seconds of your own weather, that goes where you go |

The bar over the hands shows the four spells in the
staff's colour, the spellbook page lists them, the pad's *next element* steps
through them, and the fifth slot is still the ancient magic. What the big ones
leave on the ground can **pull** (`GroundEffect::pull`), **throw**
(`fling`), **walk** (`drift`) and **follow** its caster, and is drawn as what
it is -- rings of water turning inward, a funnel of rings stacked off the
ground -- on a friend's machine as on yours.

Each of the big ones has art of its own, at the size it is drawn
(`tools/make_effects.ps1`): the Flamethrower breathes **billows of flame** with
no ball at the head of them (`flame_billow`), the Hydro Cannon is a ball of
water drawn that big rather than a smaller one scaled up (`water_orb_cannon`),
the Tidal Wave is seven lengths of **a wave** -- a bowed line of foam, the
glassy face behind it, churned water thinning out behind that -- that read as
one front (`wave_crest`), and the Mineral Burst's stones are small stones, not
big ones shrunk. The Sedimentary Rain's stones are lit and shaded, land in a
puff of dust, and come down onto **their own shadows closing in under them**;
Turbulence is streaks of air at two heights with the dirt and leaves it has
picked up, not rings of dots.

#### Slabstrike

The one spell that is three moves rather than one, because a square of ground
can be swung or dropped:

| | What it does |
| --- | --- |
| **Light** | A **twenty-pixel** square torn up and swung through an arc in front of you -- half a character's height. |
| **Heavy** | The same swing with a **thirty-two-pixel** one: further, wider, harder -- and a heavy's own slowness is the price. |
| **Heavy, held** | The big one is carried over whoever you are fighting and **dropped on them**, where it lands for a third again and **breaks into chunks on top of them**. |

Both sizes are `SLAB_LIGHT` and `SLAB_HEAVY` in `src/world/world.h`, and
everything the drawing does is worked out from them -- how thick the sod on top
is, how deep the corners are chipped, how coarse the mottling is, how far it
falls, how big the chunks it breaks into are -- so changing a number changes
the slab and not the look of it.

Seen edge-on, so what shows is the earthy side of it, with the turf still along
its top edge and its shadow on the ground under it, drawn tight and dark as it
comes down. It was a bar four pixels wide, and a bar that shape read as a sawn
plank: straight edges its whole length, one unbroken highlight down the leading
side, cracks at regular intervals across it. So the outline is bitten into and
lumped out, the highlight is in pieces, and the stone is three tones scattered
rather than one with a stripe. What each pixel is made of comes from where it
is *on the slab*, not on the screen, so the stone does not crawl as the slab
travels.

Two things a square needs that a bar did not. It rides round the **middle** of
what it strikes rather than the far edge, or it reads as a rock flying past
instead of something swung at what is in front of you; and it leaves **three
fading copies of itself** behind, because one small square in one place is a
rock sitting in the air and four in a row are a swing. The dropped one is its
own warning -- it is above you, falling, with its shadow drawing in and
darkening under it -- so the ground effect that carries the damage is marked
`quiet` and draws none of the usual disc over the top of it.

**A friend sees it.** It was the caster's alone, because it lived in the
caster's world and nothing on the line spoke of it. It goes as a patch of a
kind of its own (`PatchState::SLAB` and `SLAB_DROP`: where it is swung about or
comes down, how far out it goes or how big it is, and its facing in the byte a
patch keeps its age in), and the guest's world makes the one swing or the one
drop from that, however many snapshots go on saying so. `PROTOCOL_VERSION` 7.

### Combos

With a melee weapon, **what the last swing was decides what the next press
means.** A light attack leaves a window of about four tenths of a second open;
so does a plain strong attack. Inside it:

| Pressed | Comes out as | What it does |
| --- | --- | --- |
| Light, Light, Light | the chain | three cuts, each a little harder than the last |
| Light, **Heavy** | **Crushing Blow** | an overhead, out on the press with no hold: 1.6x a light's damage, and what it lands on **reels for a second** -- no moving, no swinging |
| Light, Light, **Heavy** | **Cleave** | a level sweep more than twice as wide as the finisher, 1.9x, that throws everything in it back and ends the chain |
| Heavy, **Light** | **Backhand** | an instant cut off the heavy's follow-through, 1.0x, that stands in for the first two links: the next light is the finisher and the next heavy the Cleave |
| **Light + Heavy together** | **Cross Cut** | a turn on the spot that strikes everything round you as far as the blade reaches, 1.25x, for 25 stamina |

Each is a swing of its own with its own shape, timing and gap, and its own
sound, and says its name over the player's head as it comes out. Each has its
own clip too -- `crush`, an overhead; `cleave`, a level sweep with the chest
turning through it; `backhand`, a cut straight back out from across the body;
and `spin`, a full turn on the spot, the one pose that yaws the whole rig --
rendered for every character, armour cut, and tier sword and spear by
`make_character.ps1` and `make_tiers.ps1`, like the leap's. The HUD prints what the buttons would do while a window is open
("K: Crushing Blow", "J: Backhand"), on the line where a held heavy's
technique is named. A heavy pressed inside the chain never charges: it is the
combo, on the press. A heavy pressed from nothing is the plain strong, or the
charge if held.

Two things fell out of building it:

- **The finisher now ends the chain.** It used to leave the window open, so a
  fourth light was another finisher, and a fifth. Now a light after it opens
  a new chain from the top, which is what "a three-hit chain" always meant.
- **Presses inside a swing are kept** for a quarter of a second and used the
  moment the next swing may start, so a chain no longer hangs on a
  frame-perfect tap. The two buttons are kept apart, so two presses inside one
  swing still read as together.

**The same grammar with every weapon.** A bow and a staff read the presses
the same way and put their own move at the end of them:

| Pressed | Sword | Bow | Staff |
| --- | --- | --- | --- |
| Light, Heavy | Crushing Blow | **Split Shot**: three arrows in a narrow fan, 0.7x each | **Surge**: one bolt at 1.6x that throws, for half again the mana |
| Light, Light, Heavy | Cleave | **Barbed Shot**: one heavy arrow at 1.6x that passes through two bodies and throws hard | **Cascade**: three bolts in a fan, 0.8x each |
| Heavy, Light | Backhand | **Snap Shot**: an arrow at once, as good as a drawn one | **Flicker**: a bolt at once |
| Light + Heavy | Cross Cut | **Twin Shot**: two arrows at once, 0.9x each | **Pulse**: a ring of six bolts, 0.5x each, for twice the mana |

A bow or a staff plays its own draw or cast rather than the sword's combo
clips, and sounds when the shot leaves. The name still floats up, and the HUD
names the move for the weapon in hand.

**The swing is drawn.** The character's swing is sixty-four pixels of arm;
what a blow actually covers is its hitbox, and nothing used to show it. Now a
pale crescent is swept through the arc the profile describes -- as far out as
the reach, as wide as the width -- faint through the wind-up, bright and
advancing through the active frames, and gone with the recovery. A spear's
thrust is a line driven out instead; the Crushing Blow adds a streak down the
middle; the Cross Cut's crescent is the whole circle; and each combo has its
own tint, so what came out can be told from across the room
(`World::DrawSwing`).

**And every combo leaves marks.** With Visual Effects on, the swing is drawn by
the fx shader rather than in lines: a soft, glowing sweep that brightens toward
its leading edge and throws sparks off it, or a driven line for a thrust. Each
of the twelve melee combos, the four shots and the staff's four casts also
leaves marks of its own, in the swing and on whatever it strikes. Each weapon
family has its own four colours, so no two of the twenty melee combos look
alike:

| | Light, Heavy | Light, Light, Heavy | Heavy, Light | Both together |
| --- | --- | --- | --- | --- |
| Sword, spear | **Crushing Blow**: a streak falls, the ground breaks in a starburst, dust | **Cleave**: the crescent, then its wind a beat later, wider; sparks | **Backhand**: the sweep runs back the other way; a snap of light | **Cross Cut**: a ring cut on the ground round the feet; an X on every body |
| Dagger | **Gut Stab**: a long red thrust; it comes out red | **Flurry**: three quick cuts across what it hit | **Backstab**: a violet thrust in from behind, a star where it lands | **Fan of Steel**: a ring of glints thrown out all round |
| Mace | **Skull Crack**: the ground breaks; stars round the head it rang | **Sweeping Blow**: bronze crescent and wind | **Backswing**: a snap | **Ground Slam**: the floor goes out from under everything, dust, a big shock |
| Greatsword | **Overhead Cleave**: a wider, harder ground break | **Reaping Sweep**: in red, and it bleeds | **Pommel Strike**: stars | **Whirlwind**: two rings, one after the other |
| Greataxe | **Hew**: the widest break, blood | **Felling Sweep** | **Haft Check**: stars | **Maelstrom**: two rings; blood-red crosses |
| Bow | **Split Shot**: three gold lines off the string | **Barbed Shot**: a heavy red line and a kick; blood where it lands | **Snap Shot**: a white line, gone at once | **Twin Shot**: two blue lines side by side; a cross where each lands |
| Staff | **Surge**: a casting circle under the caster; a big flash where it lands | **Cascade**: three lines fanned from the head | **Flicker**: a flash at the head | **Pulse**: a circle and a ring going out all round |

The staff's marks take the colour of what it casts: fire orange, frost pale
blue, and so on. A **parry** is a white flash with rays, a cross and sparks
thrown back. A **riposte** is a gold thrust, then a gold cross and flash on what
it strikes. The heavy ones shock the air, and they shake the screen only for
whoever made them.

The five shapes are in `src/shaders/fx.frag`, kinds 4 to 8: a **slash** (a
sweep with a bright leading edge), an **impact** (a flash, rays and a ring,
which can be flattened onto the ground), a **thrust**, a **cross** and a
**casting circle** (two rings and runes turning between them). Each is drawn per
pixel and snapped to the world's pixels, so it stays pixel art. The table of
what each combo gets is `src/world/world_strikes.cpp`, and it is all for show:
it touches no hit, number or dice roll. With Visual Effects off, or on a
machine without Vulkan, the old line crescent is drawn and the marks are not.

**The chain counter.** Under the target frame, every melee swing that lands
one after another is counted -- the number large, and beneath it what each
swing was: "Light > Light > Light > Crushing Blow". It shows from the second
hit, holds for a moment and a half after the last and fades, and turns amber at
five and ember at eight. A swing that meets nothing ends it, and so does a blow
taken -- so a shield raised at the right moment keeps a run alive. A whirlwind
or a Cross Cut that strikes three monsters is one hit of the chain, named for
what it was. It is `Player::ChainHits` and `ChainTrail`, counted by the world
where a swing is resolved.

"Together" is the two buttons within about five frames of each other, either
way round: a light already started is taken back before its active frames,
and a heavy's hold is taken back before it has begun to charge. It costs
stamina, so winded there is no Cross Cut to be had and the presses mean what
they mean alone. Leaders braced in their heavy's wind-up shrug the Crushing
Blow's reel off; everything else stops where it stands. The profiles are
`kCrush`, `kCleave`, `kBackhand` and `kCrossCut` in `src/systems/combat.cpp`,
the grammar is `Player::HandleAttackInput`, and what they do beyond a swing
is `World::ApplyPlayerAttack`.

### Where a blow lands

**On the ground, as a sector out from whoever swings it** -- as far as the
swing's reach, as wide either side of the facing as its width makes it at that
reach -- against **where the other stands**: the middle of their feet, and half
their width round it (`StrikeArc`, `ArcFor`, `ArcHits` in `combat.h`;
`Entity::GroundCentre`). And what is drawn is that sector, because the arc on
screen and the test both ask `AttackProfile::HalfAngle` and cannot disagree.
Everything round -- Whirlwind, the Cross Cut, Ground Slam, a war cry, a repulse,
burning ground, a Meteor -- is a circle on the same ground (`CircleHits`).

It was not, and a friend playing it said so: wide swings did not hit what they
looked like they hit. Three things were wrong at once.

- **The swing was a rectangle; the arc drawn was squashed to six tenths of its
  height.** Ground is drawn square here -- a tile is as tall as it is wide, and
  a character walks as fast up the screen as across it -- so a reach is as long
  up the screen as across it, and squashing the arc only made it lie. A Cleave,
  eighty-eight wide, struck forty-four pixels above and below a character
  facing east while the arc on screen covered seventeen; facing north it
  stopped fifteen pixels short of what it hit.
- **It was tested against the box a sprite fills**, which is as tall as the art
  and hangs *up* from the feet. So a swing reached further south than north,
  and a Meteor's fifty-eight pixel square caught something standing eighty
  pixels south of it because its head was in the square. Shots still test the
  sprite's box: an arrow flies at chest height and what it looks like it
  touches is what it touches.
- **A monster's swing reached thirty-two pixels whatever its attack range was.**
  Anything that swings from further than about forty-five -- a wyvern (54), a
  demon (46), an ankou (52), the Pit Lord (56), the frost dragon (78) -- began
  its attack in range, and then could not reach whoever it was swinging at.
  Only their heavies ever landed. A swing now reaches the range it was begun
  from, so one begun in range lands on whoever stands still, and stepping back
  out of it is still the way to make it miss.

The Cleave is a true sweep now, ninety-five degrees either side -- shoulder to
shoulder, and only what is behind is out of it -- with its reach lengthened to
what its old rectangle's half-width was, so it still catches what it caught at
the character's side. A width is a chord, and the angle a chord subtends stops
short of a right angle however long it is, so a profile may say its sweep in
degrees (`sweep_deg`). And a blade does not reach **up or down a cliff two
levels high**, either way.

### Targeting

Nothing is aimed by hand. Where a shot goes depends on whether you are in a
fight:

- **Out of combat**, an arrow or a spell flies straight the way the character
  is facing. A deer grazing off to one side is not something the game decides
  you meant to shoot.
- **In combat**, shots go to the monster you are fighting, and the character
  turns to loose them. Combat starts when a monster is chasing or swinging at
  you, or once you have struck it -- so the first arrow at a deer goes where
  you face, and the ones after it follow the deer. A monster that gives up and
  walks home drops out of the fight.

When several monsters are in the fight, the target is the nearest with a clear
line to it, weighted toward the way you face: turning toward one is how you
choose between two. A pale arrow over its head marks it, and its name, level
and health appear at the top of the screen.

`L` (or the right trigger) **locks on**. The first press takes the monster the
fight is already with, or the nearest one in reach; each press after steps to
the next one further out, and a press past the last lets go. A lock can be put
on anything within reach, fighting or not, and it holds while you move and turn
away -- the target gets a red arrow and a red ring at its feet, the frame at the
top says LOCKED, and standing still you face it. It lets go by itself when the
monster dies or gets far enough away.

Arrows **steer** after their target, so a shot at something moving still
arrives; fire bolts and air bolts turn a little, and water and earth fly
straight. A shot never circles back for a target it has already passed. With a
sword, a swing turns to face a target within reach, but not one across the
field. The numbers live in `src/world/targeting.h` and the `homing` field of
`data/projectiles.json`.

---

## Projectiles and magic

Arrows and spells are the same system. Everything that separates a bowshot
from a firebolt — speed, reach, how many bodies it passes through, what it
leaves on the ground — is data in `data/projectiles.json`, and the art is
drawn turned along its direction of travel, so a single arrow image covers
every angle. An arrow is a still; a spell is a strip of frames, and
[looks like what it is](#what-a-spell-looks-like-in-the-air).

### The four elements

You select an **element**, not a spell. Your Magic level decides which tier of
that element actually comes out, so training Magic upgrades what the same
button does instead of adding another thing to remember.

| Element | Signature behaviour | Feel |
| --- | --- | --- |
| **Fire** | Leaves the ground burning where it lands, ticking damage on anything standing in it | Area denial |
| **Water** | Runs straight through a line of bodies | Piercing |
| **Earth** | Lands heavy and bursts a moment later, after a visible wind-up | Slow, high commitment |
| **Air** | Very fast, long range, and it carries what it hits backwards | Kiting |

There is a fifth on `5` that does not work like any of them: see
[the lightning](#the-lightning-and-the-battery).

### The lightning, and the battery

`5` is **lightning**, between the four elements and the ancient magic on `6`.
It is the only school with a **resource of its own**: a battery that starts
empty, that one spell fills and the other four are spent out of. Nothing in it
flies -- an arc is a jagged line drawn between two points for a fifth of a
second and then gone, because lightning that has to travel to its target is
not lightning.

| | What it does | The battery |
| --- | --- | --- |
| **Zap** (Magic 12) | One thread to one thing: whatever is locked on, or the nearest thing in front | **+5%** on every hit |
| **Discharge** (Magic 20) | The whole battery at once, in every direction | **all of it**, and needs 10% to go at all |
| **Electrocute** (Magic 32) | Three rays in a thirty-degree cone; anything standing in it takes all three as one blow | **10%** |
| **Electro-Node** (Magic 44) | A translucent orb set down where your quarry stands, that chains to whatever is near it for five seconds and does not care whether your quarry is still there | **10%**; a charged one 18%, and it stands longer and reaches one more |
| **Call of Thunder** (Magic 58) | A bolt out of the sky, that breaks the ground round where it lands and takes everything standing beside your quarry with it | **30%**; a charged one **50%**, for half again the ground |

**Discharge is worth what was in the bar.** An empty one is worth a third of
the spell's damage and a full one two and a half times it, and the ring it goes
out in grows with the charge too: ninety pixels empty, two hundred and twenty
full. Zap six times and a Call of Thunder is paid for; zap twenty and a
Discharge is worth having.

**Five spells and five number keys is one key too many**, so lightning is
chosen the way the ancient magic is: `5` picks the school, and `5` again steps
to the next of the five your Magic reaches. The spellbook's **Lightning** row
does the same thing from the menu, and says what each one costs of the bar. A
pad has *next element* on the right stick, and the lightning is a stop on it
once any of it is reached.

**The bar is on the glass beside the health and the mana**: a green cell
standing on its end with a terminal on top, filling from the bottom, brightest
and breathing when it is full. It is only drawn for a character whose Magic
reaches some of the lightning -- nobody else has a way to put anything in it,
and a bar that can only ever be empty is a question with no answer.

It is **not a pool**. Resting does not fill it and does not empty it; dying
empties it, because what was stored is lost with the fight it was stored for.
It is in the save, and in co-op it is the **host's** to say: a zap fills the
bar by landing, and whether a zap landed is settled where the monsters are.
(Mana is the guest's own, because mana is spent at the moment of casting.)

**Water on the target, not water in the cycle.** The cycle is four long and a
fifth thing cannot be put in it without changing what all four already do, so
lightning stands outside it exactly as the ancient magic does. What it answers
to instead is **soaking**: anything wet takes lightning a **quarter harder**
and is **twice as easy** to leave arcing. Both of those are one line each in
`data/statuses.json`, on the soaking -- `weak_to` was already there and
`invites` is its pair, the one about what is left behind rather than about the
damage. Water bolt, then lightning, is a real opening rather than a note in
the README.

### What a spell looks like in the air

The four elements were thrown as whatever icon was nearest: fire was the spark
off the tinderbox, water a blue gem, earth a small rock from the scenery and
air a rune -- each eleven pixels across, tinted its element's colour, and turned
or spun as it flew. They could be told apart by colour and by nothing else.
Each is drawn now as the thing it is (`tools/make_effects.ps1`, into
`assets/effects/`), as a strip of eight frames:

| Key | Element | In the air | Behind it | Where it lands |
| --- | --- | --- | --- | --- |
| 1 | Fire | **A fireball**: a round white-hot head, banded out through yellow and orange to a dark red rim, and a tail of flame streaming back off it in tongues. It lights the ground under it, by day as well. | embers, going up and going out, and a little smoke | embers thrown back off whatever it met, smoke, a ring of light -- and the ground it leaves burning **stands in tongues of flame** for as long as it burns, where it was an orange disc |
| 2 | Water | **A ball of water**, seen as glass is -- a dark rim, a highlight top-left, the light it has gathered bottom-right, two bubbles going round inside -- that wobbles as it flies, with a wake streaming off the back of it | drops, falling | a splash: drops up and out and down again, and a ring opening |
| 3 | Earth | **A shard of stone**, cut in facets and tumbling | dust, and chips falling out of it | chips and dust; and where it breaks the ground open a moment later, the ground comes up |
| 4 | Air | **A gust**: three lines of moving air that curl over at the front, the brightness running along them toward the curl. Galewind, which "has an edge on it", has one: a crescent ahead of the lines. | streaks of it left hanging either side | streaks thrown outward, and a ring |

The greater spell of each is the same thing larger -- a second strip, not the
first one scaled, so both are pixel for pixel. The ancient magic keeps its own
pictures and sheds violet sparks.

How it is done, and why:

- **Fields, not drawings.** A flame, a wake and a gust are worked out as one
  number a pixel -- how hot, how wet -- and cut into four or five flat colours,
  which is what makes them pixel art and not a blur. The noise scrolled through
  them repeats, so the eighth frame runs into the first.
- **Held by a point, turned about it.** A strip names its `pivot`: the head of
  the fireball, not the middle of its tail, is on the projectile's position and
  is what it turns about.
- **What has a light side is not turned.** A ball of water has its highlight
  top-left flying east or west, and a stone is lit from the top-left in every
  frame of its tumble -- which is why the tumble is eight drawn frames and not
  one picture spun: a spun picture carries its own shadow round with it. They
  are `upright`. What says which way the water is going is its `tail`, a second
  strip under the first that *is* turned.
- **No bigger than what it hits.** The first gust was three times as tall as
  the circle it strikes with, and sailed through things it looked to have hit.
- **Motes.** What a spell sheds and throws up (`Mote` in
  `src/systems/projectile.h`; `World::ShedFromShots`, `ShedFromGround`,
  `BurstOf`) is only ever for show. Nothing in the game asks where an ember is;
  the dice they are thrown with are their own, so a fireball over a field
  leaves the game's dice exactly where an arrow would; and there are never more
  than seven hundred, which a practice hall full of mages will reach.
- **A friend's machine makes its own.** A guest is told where the host's shots
  are and nothing else. It sheds a trail from that, shot by shot (by number:
  its shots are handed to it anew with every word from the host, so nothing can
  be kept on the shot), and a shot it stops hearing of has met something and
  breaks where it last was.

### What a magic weapon reaches of an element

A plain staff, a wand, a grimoire and an orb all choose an element with `1` to
`4` -- and each of them **reaches a different part of that element**. Every
list comes from the element's own staff, which is where an element's four
spells live; what differs is which of them a weapon can be held to.

| | Reaches | And its own trick |
| --- | --- | --- |
| **Staff** | the **whole** element: all four | none -- reach is what a plain staff has |
| **Wand** | the bolt, the direct strike and the wide one | casts half again as often |
| **Grimoire** | the bolt, the wide one and the **great working** | every spell costs a sixth less |
| **Orb** | the bolt, the direct strike and the **great working** | what it throws turns after your quarry |

So for fire a staff can hold Ember, Pyre, Flamethrower, Flame Ring or Wall of
Fire; a wand everything but the Wall; a grimoire the Flame Ring and the Wall but
not the Flamethrower; an orb the Flamethrower and the Wall but not the Ring. No
two of the four offer the same list for any element.

**Slot one is every element's bolt and every weapon has it**, so nothing that
could be cast before this can no longer be cast, and **with nothing held to, a
weapon still throws the element's strongest bolt** -- widening what is on the
menu never quietly changes what the button does. Choosing is the spellbook's
job, as it always was.

An element's own staff is still the only thing that puts an element's four on
the keys `1` to `4` and lets you change between them mid-fight: that is what it
is for, and it is why it gives up the other three elements to do it.

It is one line of data per weapon --
`"spells": { "fire": [1, 2, 3, 4], ... }` on the piece in `data/tiers.json`,
read into `ItemDef::spell_slots` -- so a tenth magic weapon declares what it
reaches and needs no code.

### The spellbook

What each button does was spread over three places, and one of them was nowhere.
An ability was moved from slot to slot by pressing confirm on it in the tree
until it came round. An ancient spell was chosen by pressing `5` until it came
round. And which of an element's spells was cast was not a choice at all: it
was the strongest, at the strongest's price.

**The third tab of the skills panel** (`O`, or RB, then `O` again) is one page
for all of it -- **Spellbook** on a wayfarer, **Abilities** on the other two,
who have the same page with the abilities first. A row for every slot, and on
each row everything this character has that could go in it: up and down choose
the row, left and right (or confirm) change what is on it, and it is changed as
it is chosen. Under the rows, what the chosen thing is and does.

| Row | What can go on it |
| --- | --- |
| `1`-`4`, the elements | **Strongest** (the default: whatever your Magic reaches, and the next tier when you reach that), or **held** to any one spell of that element you can cast. Holding fire to Ember at Magic 25 casts Ember for 4 mana where Pyre is 9 -- for a mage who is out of mana more often than out of damage. A held spell is the plain cast's; Arcane Pulse and Repulse have their own price and still throw the strongest. |
| `5`, the lightning | whichever of [the five](#the-lightning-and-the-battery) your Magic reaches, with what each costs of the battery beside it. |
| `6`, the ancient magic | whichever of the ancient spells you have learned. One out of your Magic's reach is shown in red with the level it wants. |
| guard + light / heavy / lock on | nothing, or any ability learned. One that is already in another slot **changes places** with what was here, so choosing never drops an ability off the bar. |
| hold heavy | the plain charged attack, or any technique learned |

The tree still does what it did (confirm on a learned ability steps it through
the slots), `5` again still turns the ancient magic's page, and what is held is
the character's: it is in the save (`held_spells`), and in the sheet a friend's
machine sends the host, who does the casting.

**The fifth slot could not be reached from a pad.** A pad has no `5`; it has
*next element* on the right stick, which went fire, water, earth, air and round
again. The ancient magic was only in that round once a spell had been put on
the slot, and the only thing that ever put one there was pressing `5`. *Next
element* is told which ancient spells are known now, puts the first of them on
the slot if nothing is, and steps onto it (`Player::CycleElement`). The
self-test had only ever pressed `5`.

### Status effects

A blow can leave something on a monster besides the damage. It does not always:
what throws it says how likely (`"status": {"id", "chance"}` on a projectile in
`data/projectiles.json`, `"on_hit"` on a weapon in `data/tiers.json` and
`data/items.json`), and what each one does while it lasts is data too, in
`data/statuses.json` (`src/systems/status.h`).

| Status | Left by | While it lasts |
| --- | --- | --- |
| **Burning** | Ember 30%, Pyre 40%; standing in what they leave burning, a third of that a tick; the Ember Blade 25%; Hellish Rebuke always | Half the blow again, over three seconds. Water puts it out, and nothing soaked can be set burning. |
| **Soaked** | Spray 40%, Torrent 55% | Six seconds. It cannot burn, the wind and [the lightning](#the-lightning-and-the-battery) bite it a quarter harder, it is twice as easy to leave arcing -- and an Ice Touch freezes it. |
| **Concussed** | Shardshot 20%, Upheaval 30%, and the same again when the stone bursts | It reels as it takes it, and for four seconds its Attack and its Defence are down a quarter and it is a little slow. |
| **Bleeding** | any sword, 18% a cut (and Open Wounds, as before) | Half the blow again over four seconds. A second wound adds to the first. |
| **Poisoned** | Acid Spray, 35% a gout | Four fifths of the blow again over six seconds, and its Defence is down a fifth: its hide gives way. |
| **Chilled** | Ice Touch, always | Four seconds at six tenths of its pace and a third longer between its swings. |
| **Frozen** | a chill on something soaked | Held fast for a second and a half; then it thaws into a chill, and is dry. |
| **Arcing** | any of the lightning: a Zap 22%, an Electro-Node's chain 30%, a Call of Thunder 45% -- and twice that on anything soaked | Four seconds. A third of the blow again over them, its Attack down a sixth and a fifth longer between its swings, and it reels the moment it takes. |

**The wind leaves nothing.** A gust's thing is that it throws what it hits --
further than anything else any element throws -- and that is all of it.

**Defence is where they meet the rest of the fight.** Every monster has always
had a Defence level and a Defence bonus (all sixty-one, in `data/enemies.json`),
and a blow lands or misses by the attacker's Attack against it, the way Old
School RuneScape rolls it. So a concussed or a poisoned monster is one that is
hit more often -- by everybody, a friend's arrows included -- which is what
makes them worth the mana beyond their own damage.

What else is so:

- **A burn is not a second fire.** Two burns are the greater of the two, not the
  sum: fast hands would otherwise stack one without end. A bleed is the one
  that adds, because it always did.
- **The great ones shake things off** in half the time, and are never held: a
  boss soaked and chilled is only chilled.
- **Some things cannot take some things.** Nothing made of fire can be set
  burning. The dead -- skeletons, wraiths, banshees, the ankou, the Wight -- do
  not bleed and cannot be poisoned (`"immune"` in `data/enemies.json`); a zombie
  cannot be poisoned and a slime has nothing to bleed.
- **It shows.** The name floats up once as it takes; a pip in its colour stands
  over the health bar for as long as it lasts; the monster's own colours are
  pulled toward it (a throb of orange, blue for the wet and the cold, green for
  poison, and nearly all the way to ice when frozen); and it sheds what it is
  made of -- embers, drops, blood, bubbles, frost, and stars going round a head
  that has been rung. Damage over time comes up in the status's colour, and
  does *not* flash the monster red: a burn ticks six times a second, and
  flashed for each the monster was simply red, which hid what was on it.
- **A friend sees the same.** What is on a monster goes down the wire as one
  byte of bits (protocol 5), and a guest's machine tints, pips and sheds from
  that.
- **Nothing is rolled without the statuses loaded**, so every test written
  before them is a test of the same game it was.

### What the monsters leave on you

It goes both ways now. **Every monster that fights can leave something on the
player** -- `"on_hit"` in `data/enemies.json` for its blows, `"status"` on its
shot in `data/projectiles.json`, and a brute's heavy slam can carry its own
(`"heavy": {"status": ...}`; without one, a heavy is the monster's own `on_hit`
at twice the chance). The deer, the hares and the farmyard leave nothing.

| Who | What |
| --- | --- |
| Spiders, the Broodmother, the Gloom Spider; zombies, slimes, ghouls, rot shamblers, plague corpses; the Mire Croaker's spit | **Poisoned** -- a spider a quarter of the time |
| Rats, foxes, boars, wolves, bears, bats, hounds, gators, fen stalkers, bone knights, the Nosferatu and his thralls; orc blades and the monsters' **barbed arrows** | **Bleeding** |
| Imps, demons, the Pit Lord, the Warchief, the Cinder King | **Burning** |
| Ice trolls, wyverns, wraiths, revenants, cryptbound, the Crypt Warden, tomb shades | **Chilled** -- and a frost dragon's slam, or a rime revenant's, can **freeze** |
| Bog lurkers, the drowned, the Thing in the Spring, the Lizard Shaman's bolt | **Soaked** |
| A witchlight's spark | **Arcing** |
| Orc slingers' stones; the heavy slams of bears, trolls, chiefs and bosses | **Concussed** |
| Banshees, the Night Terror, the Wailing Dream, a tomb shade's wail | **Confused** |

**The Swamp Hags cast.** A hag stands off and throws her hexes in turn -- no
dice choose which (`"spells"` in `data/enemies.json`), and each is its own
colour so you can see what is coming:

- the **rot hex**, sickly green, poisons (35%);
- the **beguiling hex**, rose, does little harm and **charms** (55%): for two
  and a half seconds your feet take you to her, slowly, and you cannot bring
  yourself to strike, guard or jump. The next blow that lands on you breaks it.
- the **befuddling hex**, violet with a gold light in it, **confuses** (60%):
  for four seconds, which way is which is backwards -- push right and you go
  left.

Witchlights have a lure among their sparks that charms as well; a
will-o'-the-wisp is the light a traveller follows into the water.

On the player a status does what it does to a monster -- a poison, a burn or a
bleed hurts over time (in its colour, without the flash or the lost sprint of a
blow), a chill slows your feet, a concussion or a poison lowers your Defence,
arcing your Attack -- except that **a frost holds you for six tenths as long**
and a concussion lasts seven tenths: a player held as long as a monster is is
a player watching themselves die. A chip beside your bars names each one and
how long it has left; a charmed player flushes pink with hearts rising off
them, and stars go round the head of a confused one. **Dying, waking and a
night's rest clear the lot.** A shield that catches a blow catches what it
carries with it.

A friend's machine is told what is on them, and where a charm is drawing them,
in the snapshot (protocol 11), and steers them as the host does -- or every
step would be pulled back. The dice for all this are the world's own, never the
fight's, so a fight that leaves nothing throws exactly the numbers it did.

**Monsters' shots land now.** A monster's combat sheet never had a Ranged
level, so every hex, spit, bolt and arrow a monster loosed was rolled as a
level-1 archer's: nearly all missed, and the rest did a point. A shot is now as
sure and as hard as the monster's own blows -- which makes the orc slingers and
bowmen on the Hollowmarch, and every caster in the Bayou, what they were meant
to be.

### The effectiveness cycle

```
Water  →  Fire  →  Earth  →  Air  →  Water
```

Each element beats the next: water douses fire, fire scorches earth, earth
smothers air, air disperses water. Hitting a creature with the element that
beats it does **1.6×** damage and the damage number comes up in that element's
colour with an exclamation mark; being on the wrong end of the cycle does
**0.6×** and reads grey. An element resists itself at **0.75×**, and anything
untyped — most wildlife — takes normal damage from everything, so the matchup
is a reward for paying attention rather than a tax for not.

Monsters are aligned in `data/enemies.json`: orcs and boar are earth, foxes
are air, the Warchief is fire.

### The ancient magic

The four elements are what the land lends a caster. **The ancient magic is
what people wrote down before they had the elements to lean on**, and it is
taught at the **[College at Fernhollow](#the-college-at-fernhollow)**, north of
the water and older than the hamlet beside it, in the great hall on the north
side of its court, where a circle is cut into the floor (`spell_circle` in
`tools/blender_props.py`; the hall is `fernhollow_college`).
Magister Orrin keeps it, on the hero's rig in a blue robe (`magister` in
`LOOKS`), and teaches the first spell to anyone who asks; the rest are
**tomes** he sells across the council's table, the last two only once the pond has
spoken to the player. A tome is read from the pack like a recipe scroll, and
what is learned lives in the flags as `recipe:spell:<id>`.

The spells are a fifth school, **arcane**, beside the elements rather than
among them: it neither beats nor is beaten by any of them. `5` chooses it once
any of it is known, and `5` again turns the page to the next spell learned, so
the school is chosen by name where an element is chosen by strength. `R`
cycles round to it too -- as does a pad's right stick, which is the only way a
pad has -- and [the spellbook](#the-spellbook) puts any of them on the slot
outright. The old books, and D&D's, are where the names come from:

| Spell | Magic | Mana | Shape |
| --- | --- | --- | --- |
| Eldritch Blast | 10 | 8 | one bolt of force at 1.3x that passes through three bodies and throws the rest back |
| **Acid Spray** | 13 | 8 | five gouts of acid in a fan at 0.45x each, at arm's length and a little more; each can leave it **poisoned** |
| Magic Missile | 16 | 9 | three darts at 0.55x that turn after the target; they do not miss |
| **Ice Touch** | 20 | 7 | a hand's reach of cold at 1.2x that always **chills** -- and **freezes** what is soaked |
| Scorching Ray | 24 | 12 | three rays of heat in a fan at 0.8x, faster than anything the elements throw |
| **Vampiric Touch** | 28 | 12 | a hand's reach at 1.3x, and **half of what it takes comes back as health** |
| Hail of Blades | 32 | 15 | a moment later, blades come down on the target and everything beside it, at 1.5x |
| **Hellish Rebuke** | 36 | 14 | fire where the target stands, at once, at 1.6x, and it is left **burning** -- half as hard again if you were hurt in the last four seconds |
| Cloud of Daggers | 40 | 16 | a slow orb that bursts into a cloud of knives where it lands and cuts for four seconds |
| Thunderwave | 48 | 18 | a ring of force out of the caster in every direction, at 0.7x, that throws everything it touches |

Every spell has a shape in `data/spells.json` -- `bolt`, `darts`, `rays`,
`rain`, `ring`, `spray`, `rebuke` -- and the world casts by shape, so a new
spell is a line of data and a projectile. The two touches are bolts that live a
quarter of a second. The four in bold came with the [status
effects](#status-effects), and are the ancient magic's way into them: all four
are arcane, so none is any creature's weakness -- the Rebuke is the ancient
magic's and only *looks* like the fire it is (`GroundEffect::look`). The combos work with the ancient magic as they do with
the elements.

### Mana

Casting costs mana, which comes from the Magic level (`12 + level × 2`) and
refills on its own. The bar only appears once you have some, so a pure melee
character is never told about a resource they do not spend.

---

## Skills

Fourteen skills on the Old School RuneScape XP curve — the real one, so level 92 is
half the experience of 99, and the self-test checks the table against known
values.

| Skill | Trained by |
| --- | --- |
| Attack | Landing light attacks |
| Strength | Landing strong and charged attacks |
| Defence | Taking hits |
| Hitpoints | All damage dealt |
| Woodcutting | Chopping trees, with an axe |
| Mining | Working ore seams, with a pickaxe: the foothills and the Mire, the mines and barrow, and the dreamworld |
| Fishing | Fishing the Fernhollow pond, the Whisperwood stream and the Hollowmarch lake, with a rod |
| Cooking | Using a fire with something raw in your pack |
| Crafting | Workbenches in Havenbrook and Mossvale: wood, leather and thread |
| Smithing | Smelting and smithing at the anvils in Halda's forge and Mossvale |
| Foraging | Picking herbs and plants, by hand; see [Foraging](#foraging) |
| Brewing | Brewing potions at a cauldron; see [Brewing](#brewing) |
| Ranged | Landing arrows with a bow equipped |
| Magic | Landing spells with a staff equipped -- [a wall teaches nothing](#a-wall-teaches-nothing) |

Combat level uses the OSRS formula across the melee/ranged/magic triangle.

A potion can lift a combat level above its base. The Skills panel then shows the
level it is working at over the real one in green ("47/40"), and a boost wears
off a point every 45 seconds.

Ranged and Magic read their own level and their own equipment bonus for both
accuracy and damage, exactly as OSRS does, so a bow does nothing for a
character who never trained Ranged and Strength does nothing for a bow. The
self-test checks that.

### How a swing resolves

Two rolls, in `src/systems/combat.cpp`. First accuracy: `(level + 8) x (bonus +
64)` for the attacker against the same for the defender's Defence, and the
larger of the two wins more often. Note the `+64` -- at low levels the gear
bonus is most of that number, which is why a starting character feels the lack
of armour more than the lack of levels. Then damage: an integer rolled up to
`floor(0.5 + (strength + 8) x (strength bonus + 64) / 280)`, the swing's own
multiplier applied to whatever came up.

Two details of that are the difference between "unlucky" and "unfair", and both
were got wrong first time:

- **The player's damage die starts at 1, monsters' start at 0.** A hit of your
  own that lands for nothing reads as the game ignoring you; a monster rolling
  low is just a quiet moment. Symmetrical flooring was tried and it raised
  every early monster's average damage by half, which is the opposite of the
  point.
- **The multiplier scales the roll, not the die.** The light chain's links are
  x0.72, x0.82 and x1.10. Scaling the die and truncating it to an int collapsed
  a level 1 character's 1-2 range to 1-1 on the opening links, so two thirds of
  every combo were quietly worse than they read. Above about level 20 the two
  orderings agree.

### What a level is for

Beside the level list, a second column: **everything the selected skill opens,
and the level it opens at.** Walk into it with right (left comes back), walk
down it with up and down, and the line under it says what the thing the cursor
is on still costs -- `Magic 47 -- 37903 xp to go`. It opens on the first
milestone not yet reached, because what is already had is behind you.

The list is gathered from the things themselves rather than written down
anywhere, so a tier, a spell or a recipe added tomorrow appears in it the same
day and cannot be forgotten:

| Where it comes from | What it reads as |
| --- | --- |
| the character's own tree | `30 Ground Slam`, `47 Riposte (2 ranks)` |
| what a piece asks to be held or worn | `40 Damascus bows and hides` |
| what a station asks to make it | `40 Smith Damascus gear`, `24 Cook Traveller's Pie` |
| a spell, and an enchantment | `40 Mana Shield`, `50 Work Wind into a piece` |
| an ore seam, a fish, a herb | `40 Mine Damascus Ore`, `45 Catch Raw Salmon` |
| the chance of a second and a third fish | `40 Two fish in a cast, 20% of the time` |

Only the character's **own** tree is in it: a hero has no use for a row of the
wayfarer's they will never be offered.

Three things the gathering has to do to be readable rather than merely
complete. A tier's seven pieces all ask the same level, so they are one line
(`Mithril bows and hides`), and past three nouns it is the whole tier
(`Smith Mithril gear`). A tier is not all *made* at one level -- a wooden
shield comes after a wooden bow -- so each noun is said once, at the lowest
level it is true at, instead of four near-identical lines running. And the
enchanted twin of every piece is skipped: it asks nothing the piece did not,
and there are three hundred and thirty-six of them.

**Strength and Hitpoints open nothing.** They are the two that pay at every
level rather than at a few of them, and the column says so.

`--screen skills:magic` opens the page on a given skill, for looking at it.

### Skill trees

**A character has one path, and one tree: their path's.** The hero's is the
blade's (Melee), the warden's the bow's (Ranged), the wayfarer's the staff's
(Magic) -- the same affinity that gives them their starting weapon and their
edge with it. The other two trees belong to other characters and cannot be
learned from, whatever the levels. It is opened from the Skills panel (`O`);
`I` and `O`, or the shoulder buttons on a pad, step between the level list and
the tree. A save from before the paths keeps what it had bought in its own tree
and loses the rest.

| Tree | Whose | Earned by | Branches |
| --- | --- | --- | --- |
| Melee | the hero | Attack | Blade, Brawn, Guard, and Footwork |
| Ranged | the warden | Ranged | Marksman, Skirmisher, Hunter |
| Magic | the wayfarer | Magic | Evoker, Channeler, Warden |

A tree is three branches, eight rows deep, and every branch has the same shape:

| Row | Level | What it is | Ranks |
| --- | --- | --- | --- |
| 1 | 5 | a passive | 3 |
| 2 | 15 | a passive | 3 |
| 3 | 30 | a **technique**: a move that replaces the charged heavy attack | 1 |
| 4 | 40 | an **ability**: a move of its own, on its own buttons | 1 |
| 5 | 47 | a passive that asks *when*: a chain, a block, a long shot | 2 |
| 6 | 54 | a second **ability** | 1 |
| 7 | 62 | a second passive that asks when | 2 |
| 8 | 70 | the branch's capstone | 1 |

**The rows come closer together as they go down**, because the levels come
slower: a level in the fifties is several times the work of one in the teens,
and the tree used to go from 40 to 55 to 70 with nothing between. Past the
first ability no row is more than eight levels after the one before.

**Every third level of the tree's skill is a point**, a rank costs one, and a
node needs at least one rank of the node above it. That is **thirty-three
points by level 99 against forty-two ranks a tree** (forty-three for the hero,
with Rushing Strike): two branches to the bottom and a little of the third, or
all three most of the way. A build, not a checklist. `J` on a
node buys its next rank; `L` twice unlearns the whole tree and gives every point
back, for anyone who wants to fight another way -- at **sixty coins a point
taken back**. Free, a build was whatever the next fight wanted; thirty-three
points against forty-two ranks is only a choice if changing your mind has a
price. The first few points cost next to nothing to rethink and a finished tree
about two thousand, and the first press says the sum before the second takes it.

**Techniques** are as they were: once learned, `J` on one makes it the charged
attack, so holding and releasing `K` comes out as the technique, and the HUD
says what a held heavy attack will do ("Hold K: Whirlwind").

| Style | Technique | What it does |
| --- | --- | --- |
| Melee | **Whirlwind** | Spins, striking everything around you |
| Melee | **Ground Slam** | Slams the ground, hitting and throwing back everything nearby |
| Melee | **Lunge** | Dashes forward, striking everything in the way |
| Ranged | **Piercing Shot** | One fast, heavy arrow that passes through everything in its path |
| Ranged | **Volley** | A fan of five arrows |
| Ranged | **Arrow Rain** | Arrows rain on a wide circle at your target for over two seconds, a volley every beat |
| Magic | **Nova** | A ring of eight bolts of your element, for twice the mana |
| Magic | **Barrage** | Four seeking bolts at once, for twice the mana |
| Magic | **Meteor** | Your element crashes down on your target, for three times the mana |

A strike from above -- Meteor, the ancient rain -- **lands once**, the moment
it goes off. It used to land a second time a frame later, on the effect's first
tick, so each was quietly two; the numbers above are now what they say.

**Arrow Rain is a rain.** It was one of those strikes: a circle, one hit, and a
disc that was gone in a third of a second, which is a thump. Now the circle is
seen coming for a third of a second and then it rains on it for **2.4
seconds**: seven volleys, one every 0.4, each worth about a third of the
charged shot it was and each its own roll to hit on whatever is under it *then*
-- so something that walks out gets out, and something that walks in catches
the rest. Stood in from first to last it is a little over twice the shot; most
things do not stand in it. Arrows pin rather than throw: a volley staggers for
a tenth of a second and barely pushes, where a shove from the middle would have
cleared the circle on the first one. Take Aim makes the first volley its sure
hit, not all seven. The numbers are `GroundEffect::RAIN_TIME`, `RAIN_EVERY`,
`RAIN_SHARE` and `RAIN_RADIUS`.

It is drawn as one, too: a shadow on the ground with a dashed rim walking
slowly round it, and over the fighters forty-odd arrows at a time coming in
steep from up and to the left, standing in the ground where they struck for
half a second, and fading; every volley kicks up dust and is heard. Nothing
about an arrow is stored -- each is worked out from the clock and its own
number (`World::DrawArrowRain`) -- so a friend's screen, which is only told
that a patch is a rain and how long it has left (`PatchState::kind`, and the
protocol goes to 4 for it), draws its own. When the last volley has landed the
circle goes and the arrows already standing get their half second to fade.
`--learn trail_legs,broadheads,arrow_rain` with `--scratch warden --level 40`
is a character who can loose one.

#### A claw, not a bolt

The **Vampiric Touch** and the **Ice Touch** threw a small bolt that flew a
hand's reach and vanished, which is a strange way to draw something whose whole
description is *touch*. Both are now a **claw conjured at the end of the arm**
and raked across whatever is in front of you -- the Vampiric Touch's a thing of
blood, four talons and a thumb in dark red with pale points; the Ice Touch's the
same hand grown in ice. Nothing leaves the hand.

It reaches exactly as far as the bolt it replaces (`speed * life`, 88 pixels),
so the range is unchanged, and it carries what the bolt carried -- the leeching
and the chill are still the projectile's line in `data/projectiles.json`, read
by the new `"claw"` shape. Each talon is walked from the knuckle to its point,
turning and thinning as it goes, so it is a hooked claw rather than a spike; the
hand thrusts out, rakes through the arc, and is drawn back, and the gashes it
opens are drawn at the far end once the rake is under way. `PatchState::CLAW`
carries it to a friend's screen.

#### A swarm, not a hail

The **Hail of Blades** was the same delayed strike every other spell of its kind
is, drawn as a disc on the floor. It is now a **swarm of conjured blades turning
over the spot** -- the Tornado's column of rings, with a blade on every ring of
them instead of a speck of dust. Each blade is a short bar drawn along the way
it is travelling, pale down its edge and dark down its spine so it reads as a
blade and not a dash; they lie flat at the bottom of the column and stand up as
they rise, and the whole swarm turns. What it does is unchanged: one hit, when
they arrive.

#### A meteor you can see coming

The Meteor brought the element down as a circle on the floor that tightened and
then went off, with nothing overhead. There is **an actual meteor** now, and it
is **as wide across as the ground it covers** -- a hundred and sixteen pixels,
because the strike is fifty-eight in radius -- so what is falling is the size of
what is about to be hit. Nothing else on the screen says how big a meteor is.

It comes in at an angle over about its own width and a half, accelerating, with
a tail of fire strung out behind it and its shadow drawing in and darkening on
the ground under it. The corona is six-and-twenty tongues that wander rather
than a ring -- one disc behind another is a flat annulus, and an annulus does
not burn. The circle on the ground stays: it is the warning, and it is where
the damage lands.

**The rock** is shaded as a ball rather than drawn as a disc, a pixel at a time
on the sprites' own grid: the light comes from the upper left as it does
everywhere else in the game, the far side falls away into shadow, and the
lambert is quantised into five bands of stone so it reads as pixel art and not
as a gradient. Seven **craters** are cut into it, fixed in the rock's own frame
so they do not crawl as it falls. What makes a crater read as a hollow rather
than as a stain is which side is dark: inside the bowl the ground tilts toward
the middle, so the wall *nearer* the light turns away from it and goes dark
while the far wall catches it, with a rim standing proud of both. The face that
leads is blended toward the element's colour, hottest at the very edge -- it is
burning up on the way in. The silhouette wobbles by a few per cent so it is not
a compass circle.

Each row is emitted as **runs of one colour** rather than a fill per pixel: at
a hundred and sixteen across that would be ten thousand draw calls a frame, and
with the runs it holds 72 fps with the meteor filling a third of the screen.

`World::Falling` holds it (`AddFalling`, `HearOfFalling`, `UpdateFalling`), and
it goes to a friend's screen as `PatchState::FALLING` -- where it lands, how
wide, what it is made of -- with the guest timing the fall itself.

#### A dome while the shield is up

The Mana Shield was a word in the corner of the screen and nothing else. It is
**a translucent light purple dome** over the player now, brighter along its
skin where it is seen edge-on, breathing gently, closed at the foot so it reads
as a dome and not an arch. You can still see yourself and the fight through it,
which is the point. It **draws in over the last half second** rather than
blinking out, so the shield ending is something you see rather than something
you notice afterwards.

**It is the size of whoever is under it.** It was a pair of numbers -- 31 tall
against a body of 42 -- so it sat at the shoulders and left the head out in the
weather. `World::ShieldDome` takes it from the character's own body box now,
their height and a little over: 54 tall and 56 across for the rig as it stands,
and right again for any rig that ever replaces it. The self-test holds it to
clearing the head rather than to those numbers. It is anchored at their feet
every frame and lifted with `draw_lift`, so it goes where they go and rises
with a jump.

A friend's shield shows too: `PlayerState::Shielded` is one bit of the wire, and
`Player::shield_shown` is what a puppet is told -- how long is left is their own
machine's business, not something this one guesses at.

#### Abilities

**Hold the abilities' shift (RB on a pad; the guard, `H`, on the keys) and press light, heavy or lock on.** A tree
teaches six abilities and **three are carried at once**: slot one on guard +
light (`H`+`J`), slot two on guard + heavy (`H`+`K`), slot three on guard +
lock on (`H`+`L`, or B + the right trigger). The guard button is the shift key
whether or not there is a shield to raise, and the press is the ability's: not
a swing, and with an ability in the third slot not a change of target either.
Each has a cooldown and a cost, shown on the HUD at the bottom left with a bar
that refills as it comes back, and what is running -- a frenzy, a held breath,
an overload -- is named beside them. A newly learned ability goes straight
into a free slot; `J` on a learned one in the tree moves it on to the next slot
(changing places with whatever is there) and from the last puts it away; and
[the spellbook](#the-spellbook) has a row for each slot, with every ability
learned on it to choose from. Which three of the six to carry is part of the
build.

| Whose | Ability | Every | Costs | What it does |
| --- | --- | --- | --- | --- |
| Hero | **Sunder** (Blade) | 14 s | 20 stamina | A hard blow on what you are fighting, and for ten seconds its defence is down by a third -- for everyone who strikes it |
| Hero | **War Cry** (Brawn) | 30 s | 25 stamina | Staggers everything near you; you hit 25% harder for eight seconds |
| Hero | **Bash** (Guard) | 8 s | 15 stamina | A light blow on whatever is in front that staggers for over a second |
| Hero | **Frenzy** (Blade) | 25 s | 20 stamina | For six seconds melee attacks are 30% faster, and the chain does not lapse between blows |
| Hero | **Shockwave** (Brawn) | 16 s | 25 stamina | A line of force straight ahead, three swings long: a heavy blow on everything in it, thrown back and left reeling. The blow can miss; the throw cannot |
| Hero | **Stand Fast** (Guard) | 35 s | 20 stamina | For six seconds you take 40% less and nothing moves you -- and in co-op everything near turns on you and leaves your friends alone |
| Warden | **Hunter's Mark** (Marksman) | 20 s | 10 stamina | Marks what you are fighting for twelve seconds: it takes 25% more from every blow, yours or a friend's |
| Warden | **Tumble** (Skirmisher) | 6 s | 20 stamina | A roll the way you are moving, or back if you are standing still. Nothing can touch you until you are up |
| Warden | **Caltrops** (Hunter) | 18 s | 15 stamina | Iron on the ground for six seconds: what crosses it is cut and stopped short |
| Warden | **Take Aim** (Marksman) | 18 s | 15 stamina | Your next shot within six seconds always strikes critically and hits half as hard again -- every arrow of it, if it is a Volley |
| Warden | **Rapid Fire** (Skirmisher) | 25 s | 20 stamina | For five seconds the bow is 40% faster |
| Warden | **Snare** (Hunter) | 22 s | 15 stamina | A trap at your feet for twenty seconds. The first thing to step in it is held for three seconds, and hurt; then it is sprung |
| Wayfarer | **Arcane Pulse** (Evoker) | 14 s | 8 mana | Ten bolts of your chosen element, thrown outward in a ring |
| Wayfarer | **Blink** (Channeler) | 8 s | 5 mana | A short step through the air, past anything that is not a wall. With nowhere to land it does not happen and costs nothing |
| Wayfarer | **Mana Shield** (Warden) | 25 s | 6 mana | For ten seconds half of every blow is paid in mana instead of blood, two mana a point |
| Wayfarer | **Overload** (Evoker) | 20 s | 4 mana | Your next spell within six seconds costs nothing and hits twice as hard -- a Meteor included |
| Wayfarer | **Invoke** (Channeler) | 40 s | 20 stamina | Half of all your mana comes back over four seconds. With nothing to draw back it does not happen and costs nothing |
| Wayfarer | **Repulse** (Warden) | 16 s | 6 mana | A wall of force in your element: everything near is struck, thrown back hard and left reeling |

An ability is a decision, not a cancel: it does not come out of the middle of a
swing -- except the roll and the blink, which getting out is what they are
for. Sunder and Hunter's Mark are on the monster, not on whoever made them, so
in co-op a friend's blows gain from them too.

#### Passives that ask when

The first two rows are the steady kind -- damage, speed, defence, stamina, mana
-- three ranks each. The fifth and seventh rows, two ranks each, and the
capstones change how the style is played:

| Whose | Passive | What it does |
| --- | --- | --- |
| Hero | **Momentum** (2) | Every hit in an unbroken chain adds 1.5% a rank to the next, up to ten hits: the chain counter is damage now |
| Hero | **Brute Force** (2) | Charged attacks and techniques hit 10% harder a rank |
| Hero | **Riposte** (2) | For three seconds after a block, the next hit lands 30% harder a rank. "Riposte ready" shows on the HUD |
| Hero | **Open Wounds** (2) | Once a chain is three deep, every blow bleeds for 10% a rank of itself again, over four seconds |
| Hero | **Punish** (2) | +12% a rank against anything staggered: after a Bash, a War Cry, a Ground Slam, a Shockwave |
| Hero | **Bulwark** (2) | A blow caught on the shield costs 20% less breath a rank |
| Hero | *Executioner* | +10% critical, and anything below a quarter of its health is always struck critically |
| Hero | *Titan* | +15% damage; every kill gives back fifteen stamina |
| Hero | *Last Stand* | Below a third of your health: +25% damage, and 5% of what you deal comes back as health |
| Warden | **Long Shot** (2) | +8% a rank against anything more than six paces off |
| Warden | **Hit and Run** (2) | For three seconds after a shot lands you move 6% faster a rank |
| Warden | **First Blood** (2) | +20% a rank against anything at full health |
| Warden | **Weak Point** (2) | Each shot in a row on one target adds 5% a rank, up to four shots; another target starts again |
| Warden | **Slippery** (2) | While you are moving, 6% a rank of blows miss you outright |
| Warden | **Trapper** (2) | +12% a rank against anything staggered or held: on your caltrops, in your snare |
| Warden | *Deadeye* | Critical shots do half as much again, 6% more often |
| Warden | *Hail* | Every arrow goes through one more enemy; the bow is 5% faster |
| Warden | *Predator* | +10% damage; 5% of it comes back as health |
| Wayfarer | **Attunement** (2) | Each cast of the same element in a row adds 3% a rank, up to five; changing element starts again |
| Wayfarer | **Surge** (2) | A critical cast gives back 2 mana a rank |
| Wayfarer | **Siphon** (2) | 2.5% of every spell that lands comes back as health, a rank |
| Wayfarer | **Spell Echo** (2) | 8% a rank that a plain bolt is followed by a second, for nothing |
| Wayfarer | **Deep Well** (2) | Maximum mana +10% a rank |
| Wayfarer | **Resolve** (2) | A blow that draws blood gives back 3 mana a rank |
| Wayfarer | *Archmage* | Spells cost 15% less and hit 10% harder |
| Wayfarer | *Overflow* | +10% critical; mana returns 20% faster |
| Wayfarer | *Mastery* | A spell of the element a creature fears does 25% more again |

Critical strikes are half again the damage, marked with a `*`. A few effects
apply whatever is in hand -- defence, stamina, move speed, mana regeneration,
faster charging -- and the rest only to attacks of the tree's own style, so a
hero's Momentum does nothing for a bow in the hero's hand. Every one of them is
decided where every hit is: `World::HitEnemy`, and what lands on the player in
`World::HitPlayer`. A held breath and an overload are spent by the shot they go
into at the moment it is let go, in `Player::UpdateAttack` rather than in the
world, so a friend's window -- where the world decides nothing -- spends them
when the host does. The whole tree is data, in
`data/skill_trees.json` (`ranks`, `technique`, `ability` with `cooldown`,
`stamina` and `mana`, and `effects` per rank), and ranks and slots are saved
with the character -- so in co-op they travel in the sheet, a friend's machine
predicts their own roll and blink, and the host does what an ability does to
the world. `--level 60` starts a `--scratch` character with their path's skill
at that level, for looking at a tree without playing forty hours first.

#### Rushing Strike

The melee tree has a fourth branch, **Footwork**, for moves made on the run. Its
first is **Rushing Strike**, at Attack 15, with nothing above it to learn first.

With it learned and a melee weapon in hand, **a light attack made at a sprint
is a leap** -- `Shift` held and the character actually running, with breath to
spend. It used to ask only that the stick was pushed past the run threshold,
which a controller's walk is not and every step on a keyboard is, so once the
move was learned any light attack made while walking leapt whenever its three
seconds were up. The leap: the character springs at whatever they are fighting -- if it is within
leaping distance, otherwise on along the way they were running -- and brings the
weapon down as they land. It covers about 86 pixels through a short arc, hits for
**1.4 times a light attack's damage**, and trains Attack the way a light attack
does. It is only an opener: a light attack in the middle of a chain stays the next
link. Then it **rests for 3 seconds**, during which a running light attack is an
ordinary swing; a hairline under the stamina bar fills back up over those three
seconds and turns amber when the next one will leap.

It is a move of its own, not a technique, so it does not take the charged
attack's place and can be used alongside Whirlwind, Ground Slam or Lunge. The leap
has its own clip, `rush`, rendered for every character, armour cut, and tier sword
and spear -- a bow or a staff never makes it.

#### Counter

Footwork's second node is **Counter**, at Attack 30, with two ranks. It needs
Rushing Strike above it, and it is for daggers: it works on what a
[parry](#parrying) catches.

- **First rank: an opening.** A parried monster reels 0.8 seconds longer. For
  the next two and a half seconds, your next blow on *that* monster lands 30%
  harder, and "Opening!" rises off it.
- **Second rank: the riposte.** For one second after a parry, a light attack
  becomes a **riposte**, even with the guard still up. You lunge up to 40 pixels
  at whoever you parried (or your target, or straight ahead) and drive the dagger
  in, fast. It hits for 1.4 times a light's damage and **always crits**. It is
  drawn as a gold thrust and lands as a gold cross. The HUD shows
  *Riposte: J* (or your key) while one is owed.

Both ranks stack with the opening, so a riposte at the second rank lands 30%
harder on top of the crit. The guard branch's older **Riposte** passive (the
next hit 30% harder for three seconds after a block) also counts a parry as a
block. The rules are `Player::NoteParry` and `StartRiposte`, and the damage is in
`World::HitEnemy`.


---

## Material tiers

Weapons and armour come in twelve tiers, in this order:

| Tier | Needs | Worked from | Mined at | Found |
| --- | --- | --- | --- | --- |
| **Wood** | -- | logs, hide, thread | -- | trees everywhere |
| **Bronze** | -- | copper ore | Mining 1 | the foothills |
| **Iron** | 10 | iron ore | Mining 5 | the Mire, the Cursed Reach, the upper mine |
| **Steel** | 20 | iron ore and coal | Mining 20 | coal in the high foothills, the Cursed Reach and the mines |
| **Azuryte** | 30 | azuryte ore and coal | Mining 30 | the highest foothills and the barrow |
| **Damascus** | 40 | damascus ore and coal | Mining 40 | the lower mine |
| **Orichalcum** | 50 | orichalcum ore and coal | Mining 50 | the Ice Spire |
| **Diamond** | 60 | rough diamond | Mining 60 | the barrow |
| **Platinum** | 70 | platinum ore and coal | Mining 70 | the lower mine |
| **Demonite** | 80 | demonite ore and dream shards | Mining 80 | only in the dreamworld, around the Nightmare Brute |
| **Dracon** | 88 | demonite bars, a dragon's fang and coal | -- | not mined: beaten out of what a dragon leaves behind |
| **Enchanted** | 95 | dracon bars and dream shards | -- | not mined: quenched in the Reverie |

The last two tiers have no ore of their own. They are smelted from the tier
below and something the player already brings back, so the end of the game asks
for a trophy rather than another vein of rock.

### Three kinds of armour, one for each way of fighting

Every tier has **three sets**, and each helps only its own style:

| Set | Pieces | Needs | Adds to | Keeps out | Made from |
| --- | --- | --- | --- | --- | --- |
| **Plate** (the metal tiers' own) | helm, cuirass, greaves, shield | Defence | Attack and Strength | the most | the tier's bars, at an anvil |
| **Hides** | coif, jerkin, chaps | Ranged | Ranged | about seven tenths of plate | the tier's hide and thread, at a workbench |
| **Robes** | hat, robe, skirt | Magic | Magic -- the most of the three | under half of plate | bolts of cloth, the tier's dye and thread, at a workbench |

A set piece's defence is the tier's armour power times the piece's share, and
what it adds to its style is the tier's *weapon* power times its share -- a full
set of plate is about four tenths of a sword's Attack and a third of its
Strength, a full set of hides a little under half a bow, a full set of robes a
little over half a staff -- so
the sets line up with the twelve tiers of weapon and keep pace with them. None
of them adds anything to either of the other two styles, and a hide or a robe
needs its tier's level in **Ranged** or **Magic** where plate asks for Defence:
the warden's wardrobe is the warden's. They are in `data/tiers.json` beside the
metal (`style_bonus` on the plate pieces, and a `sets` block), built by the
same loader, so there are seventy-two new pieces and no list of them anywhere.

**Hides have to be killed for**, and each tier's is a different animal's:

| Tier | Set | Hide | Off |
| --- | --- | --- | --- |
| Wood | Rawhide | raw hide | deer, boar, fox, hare |
| Bronze | Wolfskin | wolf pelt | the Westwold's wolves |
| Iron | Lizardscale | lizard scale | the Mire's lizardmen |
| Steel | Bearskin | bear hide | the Brackenwood's bears |
| Azuryte | Trollhide | troll hide | ice trolls |
| Damascus | Wyvernscale | wyvern scale | frost wyverns |
| Orichalcum | Demonhide | demon hide | imps, rarely; demons |
| Diamond | Greatwolf | greatwolf pelt | the greatwolves of the Howling Fells |
| Platinum | Direbear | dire bear hide | the Old Growth's dire bears |
| Demonite | Dreadhide | dread hide | dread boars and the Nightmare Brute, in the Reverie |
| Dracon | Dragonhide | dragonhide | the frost dragon |
| Enchanted | Dreamhide | dragonhide, and dream shards | -- |

Two hides make a coif, four a jerkin, three a pair of chaps. The last tier has
no beast of its own, the way the last two metals have no ore: it is dragonhide
steeped in the Reverie.

**Robes are cloth, dyed.** A **bolt of cloth** is three stalks of **flax** --
foraged at Foraging 3 from the headlands of the Westwold's fields, or bought
from Isolde -- or two skeins of spider silk, at a workbench. A **dye** is two of
a herb and a vial in a cauldron, at the Brewing level the herb is foraged at,
and it is the one thing brewed that nobody has to be taught (`untaught`): a dye
is a flower boiled in water. The herb decides the grade -- marigold, brookmint,
nettle, bogbean, mountain sage, glowcap, emberbloom, moonpetal, starlily, and
then the three that are not only a herb: moonpetal and dream shards, a dragon's
fang and emberbloom, starlily and dream shards. A hat is a bolt and a dye, a
robe three bolts and a dye, a skirt two and a dye, and thread for all of them.

On the character they are two more **cuts** of the armour layers (see
[Worn equipment](#worn-equipment)), painted in the set's own colour at that
tier, and the icons are modelled and rendered by `make_tiers.ps1 -What sets` in
the same colours.

Every tier makes the same eight pieces -- a **sword, spear, bow, staff, shield,
helm, cuirass and greaves** -- and every piece needs its tier's level in the
skill it is used with: Attack for a sword or a spear, Ranged for a bow, Magic
for a staff, Defence for the rest. Every tier also makes two tools, an **axe** and a **pickaxe**,
which need the tier's level in Woodcutting or Mining; see
[Gathering](#gathering). Each mined tier has an **ore** and a **bar**. Ore is smelted into
bars at an anvil, and bars are smithed into the pieces there too; wooden pieces
are made at a workbench. Mining a tier's ore asks for **Mining at the tier's
level**, the same as smelting and smithing it, except for bronze and iron,
which stay below theirs so the first ores come out of the rock before the gear
made from them can be worn.

**Smithing** is its own skill, and it follows the tier milestones exactly:
smelting a tier's bar and smithing anything from it needs **Smithing at the
tier's level** -- the same number that wearing or wielding the result asks for.
Bronze is Smithing 1, iron 10, steel 20, and so on up to enchanted at 95. Before
Smithing existed every bar and blade trained Crafting, so a save from then starts
its Smithing where its Crafting stood and loses nothing it could already make. The item panel names an item's tier and says what it
needs, in red-letter "needs" when you do not have it yet.

The tiers are one data file, `data/tiers.json`: for each tier its level, its
power, its colour and its ore and bar, and for each piece its slot, its skill
and how its stats and recipe scale. The game builds every item and recipe from
that when it loads (`ItemDatabase::LoadTiers`), so a piece is always exactly as
strong as its tier says. The items that existed before -- the bronze and iron
swords, the Steel Longsword, the Oak Shortbow, the Wooden Shield, the iron
shield, helm and cuirass -- are the tier pieces now, under their old ids, so
saves, quests and loot tables still find them. Recipes that are not an item's
own "craft" (one bar makes seven things) are kept alongside the items, and the
crafting panel scrolls, with icons, now that the anvil alone makes sixty-odd
things. There is a second anvil in Mossvale.

### Every ore is its own rock

Every seam and outcrop in the realm used to be **the same grey boulder**, told
apart only by the word printed over it -- so a new miner walked up to iron they
could not touch with nothing on screen to say which of the rocks around them
was the copper they could. There are nine rocks now, one per ore, two of each so
a hillside is not one boulder stamped out, in both sizes: 36 in all, and all 471
rocks the maps place are drawn as the ore they hold.

Two things do the telling, because one is not enough at forty pixels across --
the colour of the **host stone**, and **what is growing out of it**:

| Ore | Host | What shows |
| --- | --- | --- |
| **Copper** | grey | green malachite veins, with flecks of the raw metal |
| **Iron** | rust-brown | dull red nodules, half buried |
| **Coal** | grey | jet chunks that break square and catch the light |
| **Azuryte** | pale | blue crystals, **lit from inside** |
| **Damascus** | grey | pale bands wrapping the rock, one over the other |
| **Orichalcum** | warm tan | gold nuggets |
| **Diamond** | pale | clear crystals standing proud |
| **Platinum** | cool blue-grey | pale silver nodules |
| **Demonite** | near black | purple shards with a red heart, **glowing** |

A shape survives being reduced to pixel art where a tint alone washes out:
copper is green veins and coal is black glass even where the two host rocks are
the same grey. `ORES` and `scenery_ore` in `tools/blender_props.py` hold all of
it, and `PlaceRock` in `tools/genmaps.cpp` picks the art from what the rock
yields rather than at random.

**Where the first pass went wrong**, for whoever adds a tenth: the ore was
placed a fraction of the way out from the middle of each lump, which is *inside
the rock*, and nine ores came out as nine grey boulders. It is placed on the
surface the camera can see now -- up and toward -Y -- by taking a point on the
lump's own ellipsoid.

### The tier art

Every ore, bar, weapon and armour piece is modelled in
`tools/blender_tiers.py`, from the same rounded parts, cel shading, majority
reduction and outline as the player hero, and rendered headlessly:

```powershell
.\tools\make_tiers.ps1                          # icons and weapon layers
.\tools\make_tiers.ps1 -What icons              # just the 112 icons
.\tools\make_tiers.ps1 -What layers -Only attack -Models sword_iron
```

Tiers are told apart three ways at once, because at game size colour alone is
not enough: each has its own **palette**, its own **silhouette** -- a wooden
sword is short and blunt, bronze a leaf blade, damascus a heavy cleaver,
orichalcum broad and ridged, diamond a faceted crystal, platinum long with a
winged guard, demonite jagged and horned, dracon a scaled fang, enchanted
slender and lit -- and the top tiers carry **something that glows**: azuryte's
cyan edge, diamond's white sparks, platinum's gold halo, demonite's red heat,
dracon's ember, enchanted's violet.

Damascus is watered steel now rather than the green it wore under its old name,
and the three tiers added beside it are red gold (orichalcum), hot bronze
(dracon) and the Reverie's violet (enchanted).

The same models are what the hero holds. For every tier's sword, spear, bow and staff
the script poses the weapon in the hero's hand for every frame of every clip and
renders it as a layer, cut by the body and head the way the hero's own sword
is -- `layers/<clip>_4_weapon_<model>.png`, 288 sheets -- and the game draws the
one for whatever is equipped in place of the default sword. A bow and a staff
are carried out and forward of the arm, stood up straighter than the hand
hangs; held where a sword is, their upper half vanished behind the sleeve and a
bow read as a blue sword. Armour has no layer on the character and shows, as
before, as its tier's colour over the body. The icons are 32 pixels, framed
automatically so a sword and a lump of ore both fill their square.

The same script draws every tier's axe and pickaxe, the fishing rod and the ten
fish, raw and cooked, and poses the tools in the hero's hands through the chop,
mine and fish clips (`layers/chop_4_weapon_axe_<tier>.png` and so on). In an icon
a tool has a shorter haft and a bigger head, turned side-on, or at 32 pixels an
axe and a pick were both a stick with a speck on the end. Cooked fish keep a
little of their own colour, so a roast pike and a roast salmon differ in the bag.

### Where things are made

Each recipe is made at one station, decided by its materials, and trains that
station's skill. **Anything brewed -- anything with a herb or a vial in it -- is
brewed at a cauldron**, with Brewing. **Anything that
needs metal is smithed at an anvil**, with Smithing -- in Halda's forge in Havenbrook, or
beside the workbench in Mossvale: every bar, every metal tier's pieces, and the
Copper Ring. **Cloth is woven at a loom and leather cut on a tanning rack** (see below), and
**everything else is made at a workbench**, in Havenbrook or
Mossvale, with Crafting: the wooden tier, the bows, the Fishing Rod and the Dreamcatcher.
The two used to share one list, so a village workbench could smith an iron
shield.

Nothing declares its station. Materials carry `"metal": true` -- every ore and
bar made by `data/tiers.json` does -- and a recipe with any metal input
belongs at the anvil, so a new recipe cannot be filed in the wrong place. A
crafting object in a map names the station it is with `"station"`; the
self-test checks every recipe against its materials, and that every station in
the world is drawn as what it works as. Each station's screen says what is made
at the other, so a missing recipe reads as elsewhere rather than gone.

### Hide boots

The first thing worth making from hide after the jerkin: **Hide Boots**, two
hides and a waxed thread at a workbench (Crafting 4), also sold by Hunter Ivo.
Worn on the feet they turn a little aside and **you walk a twentieth quicker**
in them; the bag prints it as "Walk +5%" beside the bonuses. That is a field
any worn item can carry, `move_speed`, a fraction added to walking speed and
summed over everything worn, so an enchantment adds to it (see Enchanting).
The Drowned King's boots keep their own Marshstride instead. An item carries
one `craft`, and hide already made the jerkin, so its second recipe is listed
under `crafts`; anything that makes several things can do the same.

---

## Gathering

**Chopping needs an axe, mining a pickaxe, and fishing a fishing rod**, carried
in the bag. Without one, the prompt says so before the button is pressed ("Chop
oak  -  needs an axe") and pressing it does nothing. With several, the fastest
one the player has the level for is used; one they carry but cannot yet use is
named in the refusal ("Your Iron Pickaxe needs Mining 10").

How long a tree, a seam or a cast takes is its base time divided by the level
and the tool together: every level is 2% quicker, and every tier of axe and
pickaxe is quicker than the one below. Measured on the same oak at Woodcutting
70, a demonite axe takes 0.58 seconds a log and bronze 1.08.

| Tier | Axe and pickaxe speed | Needs |
| --- | --- | --- |
| Wood | 1.00x | -- |
| Bronze | 1.15x | -- |
| Iron | 1.30x | 10 |
| Steel | 1.45x | 20 |
| Azuryte | 1.60x | 30 |
| Damascus | 1.75x | 40 |
| Diamond | 1.90x | 50 |
| Platinum | 2.05x | 60 |
| Demonite | 2.25x | 70 |

Axes and pickaxes are made like the rest of their tier: wooden ones from three
logs at a workbench, metal ones from two bars and a log at an anvil. The
**fishing rod** is one rod, made at a workbench from two logs and a waxed thread;
the Fishing level alone decides how quickly things bite. A new character starts
with none: tools are bought from a general store or the forge, or made. A
character from a save made before tools were needed is handed a bronze axe, a
bronze pickaxe and a rod the first time it loads.

While the work goes on the hero **plays its own animation** and holds the tool
instead of the weapon: a two-handed swing round from the shoulder into the
trunk, a pick lifted high and driven down into the rock, and the rod held out
over the water with a slow bob and the odd twitch of the wrist. Walking,
attacking or jumping stops the work.

### Trees come down and seams give out

A tree does not stand there giving logs for ever. **On every log there is a
chance the tree comes down**, and on every ore a chance the seam gives out;
then the work stops, a stump stands where the tree was (a seam is the same
rock, drawn dark and dull), the prompt offers nothing, and after a while it is
back. The roll is a plain one against the node's own chance, so a better axe
fells a tree no sooner; it only gets the logs out faster.

| Node | Chance on each | Back after |
| --- | --- | --- |
| Oak | 1 in 8 | 1.5 game hours |
| Sapling | 1 in 4 | 1 game hour |
| Ore seam | 1 in 6 | 1 game hour, a little more for deeper ore |
| Ore outcrop | 1 in 3 | 0.75 of a game hour, likewise |
| Dream crystal | 1 in 4 | 0.5 of a game hour |

A game hour is half a real minute, so an oak is back in under a minute of play.
What is down is written into the save beside the picked herbs, so a stump is
still a stump after a reload, and sleeping through the night brings everything
back. A node's chance and its time are `deplete` and `regrow` on the object in
the map, set by `tools/genmaps.cpp`; a tree's stump is its `sprite_open`,
rendered by `tools/blender_props.py` (`stump`, `stumpsmall`) with the rest of
the scenery.

### Fishing

**Fishing** is its own skill. A fishing spot is drawn as rings spreading on the
water with the odd bubble: three on the Fernhollow pond (one off the end of the
jetty), three on the Whisperwood stream, and four along the Hollowmarch lake.

| Fish | Fishing | Caught in | Cooked, heals | Cooking |
| --- | --- | --- | --- | --- |
| Minnow | 1 | pond, stream, lake | 4 | 1 |
| Trout | 15 | pond, stream | 9 | 15 |
| Pike | 30 | pond, lake | 13 | 30 |
| Salmon | 45 | stream | 17 | 45 |
| Eel | 60 | lake | 22 | 60 |

Each spot gives up the best fish the level allows about a third of the time,
more often the further past it the level is, and something lesser otherwise.
Raw fish cook at any fire into food.

The **milestones** are the chance a cast brings up more than one fish. With
Fishing selected in the Skills panel they are listed along the bottom:

| Fishing | Two fish | Three fish |
| --- | --- | --- |
| 20 | 10% | -- |
| 40 | 20% | -- |
| 60 | 30% | -- |
| 80 | 30% | 10% |
| 99 | 30% | 20% |

A cast that lands more than one says so in gold ("+ 2 Raw Trout").

### Cooking, and what a dish is worth

A fire used to cook whatever was nearest the top of the bag when the button was
pressed. It opens **a menu** now, the same panel the anvil and the cauldron use:
every raw thing you are carrying that can be cooked, listed by the Cooking it
asks for, and past those the **dishes**.

A dish is worth more than the hit points in it. Eat one and it sits with you for
twenty minutes or so and lifts something while it does:

| Dish | Cooking | What it does |
| --- | --- | --- |
| Honeyed Oats | 6 | +6% max health, +10% max breath |
| Hunter's Skewers | 12 | +4 Ranged |
| Frog Legs in Butter | 16 | +15% max breath, +2 Ranged |
| Hearty Stew | 18 | +10% max health |
| Traveller's Pie | 24 | +25% max breath |
| Fisherman's Broth | 28 | +12% max mana |
| Moonpetal Tea | 34 | +18% max mana, +3 Magic |
| Farmer's Supper | 40 | +15% max health, +3 Attack |

The pools are **shares** of what they already are, so a good dinner is worth
cooking at fifty as well as at five, and the levels are held steady for as long
as the meal lasts rather than draining a point at a time the way a potion's do.
**One dish at a time**: a second replaces the first, so which one you cook before
a fight is the whole of the decision. What you are on, and how long is left of
it, is under the vitals.

Burning is still possible, and still falls away as the cook's level climbs past
the dish's. `ItemDef::dish_*` is the whole of the data; `Player::Meal`,
`SetMeal` and `HoldMeal` are the whole of the code, and the max-pool shares are
read where the pools are worked out (`SyncHitpoints`, `SyncMana`, `MaxStamina`).

### Foraging

**Foraging** is picking herbs and plants. It needs no tool: stand at a plant and
press `E`, and the hero kneels and picks it with the weapon put away. A picked
plant is left as cut stubs and **grows back** after a few game hours -- three for
a marigold, nearly nine for a starlily -- and what has been picked is saved. Past
a plant's level, each level adds a one in a hundred chance it gives two, up to
half the time.

| Herb | Foraging | Grows best |
| --- | --- | --- |
| Marigold | 1 | the meadow round Havenbrook |
| Brookmint | 6 | wherever land meets water: the lake shore, the Whisperwood stream, the Fernhollow pond |
| Stinging Nettle | 12 | the greenwood, and along the Whisperwood trail |
| Bogbean | 20 | the Mire |
| Mountain Sage | 28 | the foothills, thicker the higher up |
| Glowcap | 36 | the shade just inside the Whisperwood's trees |
| Emberbloom | 46 | the burnt ground of the Cursed Reach |
| Moonpetal | 56 | the Reverie's islands |
| Starlily | 68 | only the Reverie's crystal field |

Each is scattered thinly over its own ground, and on the overworld each has
**one patch where it grows thick**, found by `tools/genmaps.cpp` by looking out
from a rough spot for somewhere its whole round is the right biome. Oona keeps a
garden of the three beginner herbs beside her cottage in Mossvale. The plants'
levels and XP come from their items in `data/items.json` (`"forage"`), so the maps
and the items cannot disagree.

### Brewing

**Brewing** turns herbs and a glass vial into potions at a **cauldron**: in
Havenbrook by the cooking fire, in Oona's cottage, at the Fernhollow camp and by
the candles in the Reverie. Vials are sold at every general store.

A recipe has to be **learned** before it can be brewed. The cauldron lists every
brew, but one not yet learned shows as "Unknown recipe" and says where to learn
it. Oona teaches the first -- ask her "Could you teach me to brew?" -- and the
rest are **recipe scrolls**, read from the pack, sold by traders around the
world. Learned recipes are saved as world flags (`recipe:<id>`).

| Potion | Brewing | Ingredients | Effect | Recipe from |
| --- | --- | --- | --- | --- |
| Healing Draught | 1 | 2 marigold | 20 hitpoints | Oona teaches it |
| Mana Tonic | 6 | 2 brookmint | 40 mana | Oona |
| Nettle Brew | 12 | 2 nettle | Strength +3 and a tenth | Tobin's General Store |
| Fen Bitters | 20 | 2 bogbean, marigold | all stamina, 12 hitpoints | Hob the Pedlar |
| Stoneskin Draught | 28 | 2 mountain sage | Defence +3 and an eighth | Garrow's Smithy |
| Hunter's Focus | 36 | 2 glowcap, nettle | Ranged +4 and an eighth | Ivo's Bows and Hides |
| Emberfire Elixir | 46 | 2 emberbloom | Attack and Strength +4 and an eighth | the Collector |
| Moonlit Draught | 56 | 2 moonpetal, brookmint | 80 mana, Magic +5 and an eighth | the Night Pedlar |
| Starlily Panacea | 68 | 2 starlily, moonpetal, marigold | 40 hitpoints, 100 mana, all stamina, every combat level a little | the Collector, after Lights on the Pond |

Every brew also takes one vial, and each is brewed at the Foraging level of its
rarest herb, so the two skills climb together. A boost is the OSRS kind: a flat
amount plus a share of the level, never stacking past its own ceiling, and a
second potion of the same kind is refused -- not wasted -- while the first holds.
Food and potions alike are refused when they would do nothing. Oona buys brews,
and every brew is worth at least 1.8 times its herbs, like anything crafted.

The art is original. The plants (each growing and picked) and the cauldron are
modelled in `tools/blender_props.py` and rendered with `make_props.ps1`; the
herb, vial, potion and recipe-scroll icons are built in `tools/blender_tiers.py`
beside the fish (`.\tools\make_tiers.ps1 -What brewing`); the kneel-and-pick
`gather` clip is in `tools/blender_character.py`.

### Enchanting

**Enchanting** works a charm into a worn piece -- a ring, an amulet, boots, or
a piece of armour -- at an **enchanting table**, for Magic. There is one by
Mira's stones in Fernhollow and one by the candles in the Reverie. Weapons take
nothing.

A charm has to be **learned** first, like a brew. The table lists every charm,
but one not yet learned shows as "Unknown enchantment" and says where to learn
it. Mira teaches the first -- ask her "Could you teach me the shrine's craft?"
-- and the rest are **charm scrolls**, read from the pack, sold by traders
around the world. Learned charms are saved as world flags
(`recipe:enchant:<id>`); a charm scroll's `learn` reads `"enchant:<id>"`, and
a dialogue line teaches one the same way.

| Charm | Magic | Fits | Does | Costs | Scroll from |
| --- | --- | --- | --- | --- | --- |
| Swiftness | 5 | boots | walk an eighth quicker | 1 dream shard, 2 brookmint | Mira teaches it |
| Warding | 10 | helm, body, gloves, legs, boots | Defence +8 | 1 dream shard, 2 nettle | Oona |
| Keenness | 15 | ring, amulet | Attack +8 | 2 dream shards, glowcap | Tobin's General Store |
| Might | 20 | ring, amulet, gloves | Strength +8 | 2 dream shards, 2 nettle | Garrow's Smithy |
| Hawk's Eye | 25 | amulet, gloves, helm | Ranged +10 | 2 dream shards, 2 glowcap | Ivo's Bows and Hides |
| Insight | 30 | ring, amulet, helm | Magic +10 | 3 dream shards, moonpetal | the Night Pedlar |
| Fortitude | 40 | body, legs, shield | Defence +14 | 3 dream shards, 2 mountain sage | the Collector |
| Wind | 50 | boots | walk a fifth quicker | 4 dream shards, starlily | the Collector, after Lights on the Pond |

Every charm costs shards of dream, mined from the crystals in the Reverie, and
a herb, so Magic, Foraging and the nights spent asleep climb together. At the
table, up and down pick the charm, left and right pick which piece in the bag
it goes into (a bag can hold three rings), and use works it: the materials and
the piece go, the enchanted piece comes back in the same slot, and Magic is
paid. A piece takes one charm and no more, and a lantern, worn in the shield
hand, takes none.

**An enchanted piece is an item like any other.** The game builds, at load, an
enchanted twin of every piece each charm fits -- `copper_ring+keenness`, the
Copper Ring of Keenness -- with the piece's bonuses plus the charm's, the
piece's picture, tint and armour layer, and both their worths added, so it is
carried, worn, sold, stored and saved by its id and nothing else in the game
had to learn what an enchantment was. 168 twins come out of the eight
charms; none is a recipe and no shop sells one ready made. The charms are
`data/enchantments.json`; see `ItemDatabase::LoadEnchantments`. The table is
`enchanting_table` in `tools/blender_props.py`, an object of type `altar` in
the maps; the charm scroll and the hide boots are drawn beside the potions in
`tools/blender_tiers.py`.

---

## Trading

Talk to a trader and pick the trade line ("Let me see what is for sale.", "Buying
any fish?") and the shop opens. Anyone who trades says so in the prompt: "Talk to
Smith Halda  -  trades". The trade line is always there, whatever else the
conversation is gated on.

The shop has two tabs, switched with `A`/`D` (left and right on a controller).
**Buy** lists what is on the shelf with its price and how many are left today;
**Sell** lists everything in the pack with what this trader pays, dimmed when they
do not deal in it, and names the trader who would pay more. `J` (A) buys or sells
one; hold `Shift` (left trigger) to buy ten or sell the whole stack. `K` leaves.

### Who is where

Every town and significant place has a general store and at least one other
kind of shop.

| Place | General store | Other shops |
| --- | --- | --- |
| Havenbrook | Tobin's General Store, a stall on the square | **Halda's Forge**; the Inn Kitchen (Bess); Ivo's Bows and Hides (Hunter Ivo); **Nessa's Tannery**, with the order book |
| Mossvale | Pell's Stall | **Garrow's Smithy**, at the village anvil; Oona's Remedies; **Wynn's**, a draper's up the north-west lane, with the order book |
| Fernhollow | Nell's Cart, by the path to the jetty | Wendel's Jetty, a fishmonger |
| Whisperwood camp | Hob's Pack, a pedlar resting at the camp | Bram's Woodpile |
| The Reverie | The Night Market (the Night Pedlar) | Curios of the Deep Dream (the Collector) |
| Hidewater, in the Westwold | -- | **Orla's Tannery**: the best price for a hide anywhere, thread, and the Rawhide and Wolfskin sets; **Isolde's Loom**: flax, cloth, vials, the first two dyes, and the Homespun and Novice's robes |
| The Brackenwood | -- | Hale's Packs, at the trapper's camp: hides bought, and the Lizardscale set |

### Nessa's Tannery, and learning a trade

Havenbrook had nowhere inside its walls to learn a craft with. The Westwold's
tannery is out of the west gate and past the wolves, which is no use at
Crafting 1, and the workbench by the forge had nobody standing at it who wanted
anything made. So the south-west corner of the town, which was empty grass, is a
tannery yard: frames of hide drying along the wall -- which are what is worked
at: see [the tanning rack](#the-tanning-rack-and-the-sixth) -- a log pile, and
**Nessa the Tanner**, who buys hide, pelt and cloth, sells thread and
boots, and keeps **an order book**.

An order is a daily delivery like Halda's ore or Wendel's pike, with one
difference that is the whole point of it: **every order asks for something that
is made, not something that is found.** Three are posted a day out of a pool of
fourteen, and an order asking more of the crafter than they can do is passed
over for one they can, so the book always has work in it:

| Crafting | The order | What it is |
| --- | --- | --- |
| 1 | Rawhide Coifs, Bedrolls for the Gate, Bedrolls | the first things anybody makes |
| 3-4 | Bolts of Cloth, Rawhide Jerkins, Hide Boots, Wolfskin Jerkins | |
| 8-12 | Leather Jerkins, Boiled Chaps, Hide Satchels | |
| 20-46 | Scaled Coifs, Wolfskin Packs, Banded Jerkins, Bearskin Rucksacks | the bags, and the upper hide sets |

What an order asks for in Crafting is the recipe's own level, worked out from
the recipe rather than written down twice, and the self-test holds every one of
them to it -- an order for something nobody can make is an order nobody can
fill. It also holds each to the older rule that a repeatable order must pay
**less in coin than buying the same goods would cost**, or an order book is a
way to turn coins into coins; so the coin is modest and the Crafting XP is the
reward. Nothing she sells is anything she orders, for the same reason.

### Wynn's, at Mossvale

The tannery is the ranger's trade and this is the mage's. Wynn kept a stall on
the north side of Mossvale's square with her loom standing out in the weather
beside it, in earshot of Garrow's anvil. **She has a house now**, up its own
lane in the quiet north-west of the village, a long way from the forge: a
timbered shop with a blue door between two windows of small panes, gowns stood
in each under a striped awning, and a spool on the sign
(`prop_clothier_shop`; the first front had the door to one side of one wide
window, and the way in was through the glass).

Inside (`mossvale_weavers`) it is a draper's: racks of bolts end-on along the
back wall in every colour she dyes, hangings between them, the counter she
sells over, a cutting table in the middle of the floor with a length of blue
across it and the shears beside, tubs of rolls, five dressmaker's forms down
the front of the shop in a robe, a gown and a travelling cloak -- and **her
loom and her wheel**, which are the point. A board where the stall was says
where she has gone. She buys flax, fleece and silk. Her **order book**
works the way Nessa's does, three a day out of nine, and asks for what a mage
wears: bolts of cloth at Crafting 3, homespun hats and robes at 1 and 4, novice
skirts and robes, apprentice's hats and robes at 10, a journeyman's robe at 20,
an adept's at 30.

Between the two of them every piece of soft armour in the game now has somebody
who wants it: hides and bags at Havenbrook, hats, robes and skirts at Mossvale,
and the cloth for both. Cloth itself has a third source now -- **a fleece off
Havenbrook's farm spins into two bolts** (Crafting 6), beside flax at 3 and
spider silk at 8 -- so a town can keep the loom going without walking to the
riverbank.

### The loom, and the fifth station

Weaving used to happen at a carpenter's bench, which is where everything soft
happened. It has its own station now -- `CraftStation::Loom` -- and Wynn's shed
has the only one in the Hollowmarch:

| Station | Trains | What is made there |
| --- | --- | --- |
| Workbench | Crafting | Wood, leather, thread: bows, hides, bags, a bedroll *(the leather has since gone to the tanning rack: see the next section)* |
| **Loom** | **Crafting** | **Cloth from any fibre, and all 36 pieces of the mage's sets** |
| Anvil | Smithing | Anything with metal in it |
| Cauldron | Brewing | Potions, and the robes' dyes |
| Cooking fire | Cooking | Plain food, and the dishes |

The loom and the bench both train Crafting, because both are the same trade:
Nessa's order book and Wynn's pay into the same number. What separates them is
what they make, and that is decided by **what comes off the recipe, not what
goes into it** -- if the result is tagged `cloth` it is woven, and everything
else falls through to the older rules. That one line is what carries the whole
robe set across, since the tiers already tag the mage's pieces `cloth` where the
ranger's are `leather`, and it is also why:

- a **bag** is part cloth and still sewn at a bench, because a bag is mostly hide;
- a **dye** is cloth's business, carries the cloth tag, and is still boiled --
  brewing is asked first, since a dye is a pot of liquid and not a length of
  anything.

Nessa used to post an order for a bolt of cloth. She does not any more: cloth is
woven three miles away at somebody else's loom, and Wynn's book already asks for
it at the same level. Her book is thirteen orders of leather now.

### The tanning rack, and the sixth

Both tanneries had frames of hide standing all round a carpenter's bench, and
the bench was where the hide was worked: the frames were scenery. **The frames
are the station now** -- `CraftStation::Rack`, "Use the tanning rack" -- and the
bench has gone from both yards. There are three at Nessa's in Havenbrook, four
along the north side of Hidewater, and the one at Hale the trapper's camp in the
Brackenwood, where bear hide can be cut where the bear was. (Wynn had two
at her stall doing duty as a drying line for cloth; they went when she moved
indoors.)

| Station | Trains | What is made there |
| --- | --- | --- |
| Workbench | Crafting | Wood: the wooden tier, bows, the fishing rod, the dreamcatcher |
| **Tanning rack** | **Crafting** | **Everything of leather: all 36 pieces of the ranger's hides, the Leather Jerkin, Hide Boots, the four bags, the bedroll** |
| Loom | Crafting | Cloth from any fibre, and all 36 pieces of the mage's sets |
| Anvil | Smithing | Anything with metal in it |
| Cauldron | Brewing | Potions, and the robes' dyes |
| Cooking fire | Cooking | Plain food, and the dishes |

It is the loom's rule over again, one line later: **if what comes off the
recipe is tagged `leather`, it is cut on a rack.** The tiers already tag the
ranger's pieces `leather`, so the whole of the hide armour went across on that
line. Asked of the result, and before the metal, which is why:

- a **banded jerkin** has iron in it and is still a jerkin, cut by a tanner and
  not beaten out by a smith;
- a **Barkwood Helm** has a hide in it and is still wood, so Halda's lesson is
  still done at a bench;
- a **bag** and a **bedroll** went to the rack with the rest. The loom's section
  above says a bag is "still sewn at a bench, because a bag is mostly hide":
  being mostly hide is now the reason it is not. The bedroll is tagged `leather`
  for the same purpose -- it is two hides and the thread to sew them.

That last one is the judgement call. The alternative was hide *armour* only on
the rack, with the bags and bedrolls left at a bench -- but five of the thirteen
orders in Nessa's book are bags and bedrolls, and a tannery whose own order book
cannot be filled in its own yard is the thing the loom was built to stop. The
self-test holds her book to it: every order in it is made on her frames. If the
bags belong back at the bench it is one tag on five items.

Three stations, one skill: the rack, the loom and the bench all train Crafting,
because all three are the same trade. Orla, Nessa and Halda say where hide is
cut now, where they used to say "any workbench".

### Things that were quietly broken

A review of the game's systems against its own data turned these up. None of
them crashed anything, which is how they had lasted.

**Strength never trained.** Attack decides whether a blow lands and Strength
decides how hard it can -- `HitChance` reads one and `MaxHit` the other, and
always did. A light swing is meant to train Attack, a heavy one Strength, and a
charged one both. But the one call that awards the experience said every blow
was a light one, so nothing in combat ever fed Strength: the number that sets
the top of the damage roll sat at 1 for the whole game, moved only by quest
rewards. `World::HitEnemy` takes the swing now and passes it on, and Watchman
Corrin's lesson says which swing teaches what, because nothing else in the game
did. A monster's `xp_mult`, which was read from the file and then by nothing,
is applied on the way.

**A full bag made any tree an endless one.** Chopping and mining paid their
experience *before* trying to put the log in the pack, and the refusal that
followed returned before the dice that fell the tree were rolled. With no room,
every swing paid in full, produced nothing, and the tree never came down.
Fishing and foraging had it right; now all four add first and pay for what was
added.

**The Cross Cut was free on an empty bar.** It costs 25 breath and asked only
for more than none, with the spend clamped at nothing.

**A healer took your potions off you.** The `heal` a conversation gives reset
every level to its base, which mends what was drained and also pours out an
Emberfire Elixir. `Skills::RestoreDrained` puts back only what was lost.
(Dying still costs the boost; that is the price of dying.)

**Closing the window cost up to two minutes.** Quitting from the pause menu
saved; the window's own close button did not. Both save now -- except a scratch
game, a guest's world, and anybody lying dead, for whom the last autosave
stands.

**A new game did not start with a new world.** Play one save, start a new game
in the same sitting, and the first character's things were in the second
character's storage chest at Mossvale -- and the new save wrote them down as
its own, so from then on the chest was "shared". Each save always kept its own
chest and loading always replaced it; what was missing was the *new game*,
which cleared what the world remembers a line at a time and had not been told
about the two newest things a save had learned to keep: the storage chests, and
which bosses have been killed today (so a new character could also find a boss
dead on day one). `World::StartAfresh` is the one list now, and the self-test
saves two games from one world and loads each over the other.

**Saves.**

- Each slot keeps **the save before the last one** beside it (`slotN.bak`), and
  a slot whose file cannot be read is shown, and loaded, from that -- with a
  note saying so. A file that has gone bad is never allowed to become the
  backup.
- A slot nothing can read says **Damaged**, not *Empty*. Empty was an
  invitation to start a new game over whatever could still have been rescued.
- **Saving over a slot that is not the one you are playing asks first**, the
  way a new game there always did. Saving over your own is just saving.
- The load screen can **delete a slot** (the drop button, then confirm). There
  was a function for it and nothing that called it, so the only way to make
  room for a fourth character was to write over a third. What is deleted is put
  aside as `slotN.deleted` rather than destroyed.
- `SaveSystem::SetDirectory` exists so the self-test can do all of the above
  somewhere that is not `saves/`.

**Experience is shown as it is earned.** Every gain was worked out, banked, and
thrown away unseen -- the comment said the HUD showed totals, and the HUD showed
none. There is a line a skill down the left edge now, `+48 Strength  62%`,
counting up while the gains keep coming and fading a moment after they stop;
the percentage is how far through the level that leaves you. Options has a
switch for it.

**The interface has a size.** `ui_scale` was saved, loaded, clamped -- and read
by nothing. It works now (Options -> Interface Size: 100 / 110 / 125 / 150%):
the renderer draws the interface's units larger and the fonts are *opened*
larger, so text is set at its real size rather than stretched and blurred.
Every panel is laid out to fit 1024 by 600 of its own units, so the scale is
held to what the window has room for -- on a Steam Deck's 1280 by 800 that is
125%, and Options says so when it has had to. The world is not scaled; that is
what Camera Zoom is for. Two at one machine get 100%.

**And the audit had never tested what it said it tested.** `--audit` listed
three window sizes and set the interface's viewport for each -- and `Render()`
resets that viewport from the real window on its first line, so all three
passes were 1280 by 720. It sets the window's size now, and runs eight passes:
four sizes, including the Deck's, at every scale each has room for. The first
honest run found the **Skills panel did not fit a 1024 by 600 window at all**
(640 and 690 tall, the melee tree 1032 wide -- title and close prompt both off
the glass), which is also what a Deck at 125% would have shown. It is cut to
the window now, like the bag and the options always were.

Also: enemies marked `boss` say so on the target frame, which is the first
thing that flag has ever done; a herb's experience comes from `items.json`
where it is written rather than from a copy in the map; and the note in
`combat.cpp` that said a maxed character "tops out around 20" now says 148,
which is what twelve tiers of gear made it.

### What the same review changed about playing it

The second half of that review was not bugs. It was things that worked and were
worse than they needed to be.

**A quick item, and food that cannot be spammed.** Healing in a fight meant
opening the bag. One thing that can be eaten or drunk can be set as the quick
item now -- the target button on it in the bag, where it wears a gold corner --
and it sits in a box at the bottom left of the HUD with how many are left.
**Block + Interact** uses it and **Block + Sprint** steps to the next thing in
the pack that could be it, so nothing new had to be bound and both hands stay
where a fight has them. With nothing chosen it is the first food in the pack.
Alongside it, anything that heals starts a **1.5-second wait** before the next
thing that heals (`Player::EAT_COOLDOWN`): before, a full pack of trout was a
second health bar at the speed of a button. A potion that only boosts or
restores mana is not held up by it.

**Make all, and put it all away.** At any station, **Sprint + Confirm** makes
the recipe until the materials, the bag or ninety-nine run out; the footer says
so. Every station remembers the row it was left on. At a chest, **Drop** from
the pack stows everything the chest already has some of -- the ore goes with
the ore -- and **Sprint + Drop** stows the whole pack, never coins and never
anything marked `keep`. From the chest's side, Drop takes the lot, as far as
there is room. Emptying a full pack into a chest was fifty-six presses.

**Defence is worth having against the blows that matter.** A leader's heavy
attack ignores the shield on purpose, and used to ignore armour as well, so the
one blow in the game that kills people was the one blow Defence did nothing
about. `SoakHeavy` takes `armour / (armour + 300)` off it, capped at 60%, where
armour is the Defence level plus the worn bonus -- little in a beginner's
leathers, a real share by the middle tiers, and never all of it. It is still
the blow to step out of. Blocking is re-priced with it -- see
[Blocking](#blocking).

**Skilling curves.**

- Every tree paid 65 experience, so the hundred oldest trees in the Brackenwood
  taught exactly what the ones by the sawpit did. What a tree is worth follows
  what it asks for now, on the slope the seams are on -- 65, 135 for an oak at
  fifteen, 210 for the old growth at thirty -- and each a little longer in the
  cutting.
- Herbs took three to nearly nine game hours to grow back, which with one pick
  a plant left a forager standing in a picked field nineteen minutes in twenty:
  about five hundred hours to 99 Foraging against eight for a miner. They grow
  back in `1.8 + level / 21` hours -- a marigold in under two, a starlily in
  five -- and a herb's experience, which lives in `items.json`, is two and a
  half times what it was.
- The quickest a swing of an axe or a pick can be is **0.42 seconds, down from
  0.6**. A platinum axe reached 0.6 on the day it could first be held, so the
  three tiers above it cut no faster than it did.
- Fish heal 5 / 14 / 21 / 28 / 36 by tier and say so. Fish were worse than the
  meat off a boar, which made Fishing and the Cooking it feeds a worse way to
  eat than killing things.

**Kills pay like their level, and the economy has somewhere to go.**

- A level-6 dream boar dropped a nine-hundred-coin hide three times in ten: 274
  coins a kill beside an ordinary boar's 17. It is one in twenty now, and the
  Nightmare Brute leaves one rather than two or three. Bats, slimes and hounds,
  which fought like their level and paid like vermin, pay; the Den Mother, a
  boss with three hundred hit points, pays 500-900 on top of what she carried.
  (A skeleton still leaves bones and nothing else. That one is on purpose, and
  the self-test says so.)
- **A boss killed today stays killed until dawn.** Every map load stood every
  monster back up, the Pit Lord and the dragons with the rest: out of the door
  and in again, and a hundred and forty thousand coins of demonite was on its
  feet waiting. `World::NoteSlain` remembers a boss by its map, its post and
  the quest day; it is spawned lying dead rather than left out, because friends
  in co-op count monsters by their place in the list; and it is in the save.
- **Sinks:** the inn's beds, unlearning a tree, and the middle tiers on the
  shelves -- steel at Mossvale's forge, steel and azuryte bows at the bowyer and
  staves at the college, azuryte and a damascus sword at the Reverie's
  curio-seller -- one of each a day, so smithing your own stays the cheap way
  and buying the quick one. The dearest thing a shop sold was 3,168 coins.
- Oona has an **order book**: eleven orders, six of herbs and five of what is
  brewed from them, so Foraging and Herblore have the repeatable work the other
  trades already had. The fishing orders were re-scaled to what the fish are
  worth, under the old rule that an order pays less than buying its goods off a
  shelf would cost.

### A wall teaches nothing

Casting paid a spell's own Magic experience as the bolt left the staff --
the comment beside it said "whether or not the bolt finds anything" -- and mana
comes back by itself. So the best teacher of Magic in the game was any wall:
no risk, no cost, twelve experience a press at level 1 and seventy-two at
forty-eight, for as long as anybody cared to stand in Havenbrook square. Forty
presses at the waystone took a new wayfarer from nothing to 480 experience,
which is level 5. Nothing else in the game trained that way: a sword swung at the air
and an arrow shot into a field have always taught nothing.

A cast is **owed** its experience now, and is **paid the first time anything
it threw takes something off a monster**:

- **Once a cast**, however many things it hits. A Nova is eight bolts and one
  spell; a Cascade is three.
- **Whatever of it lands.** The bolt; any one of a fan of them; the ground a
  fire bolt leaves burning, if something walks into it; a Meteor coming down.
  They all carry the number of the cast that made them
  (`Projectile::cast_id`, `GroundEffect::cast_id`).
- **Never for a miss.** A bolt that reaches a monster and does nothing pays
  nothing. Paying for a miss would only have made a monster that cannot be hit
  into the same wall with a name -- which is the oldest trick there is.
- **A cast with nothing of it left in the air is forgotten**
  (`World::ForgetSpentCasts`): the bolt broke on a wall, or ran out of sky, or
  the fire went out.

In a fight nothing has changed: a spell that lands pays exactly what it always
paid, the spell's own experience and four a point of damage on top. The mana is
still spent on a cast that finds nothing; that was never the price of the
experience, only of the bolt.

How: `World::OpenCast` where the experience used to be granted,
`World::PayCast` in `HitEnemy` beside the line that pays for the damage -- the
one place every blow a player lands goes through -- with the cast handed to it
the way a sure critical is, by whatever carries it, just before it lands. It is
paid with the caster acting, so in co-op a friend's spell pays the friend.

### A lesson is a quest, not a button

Three people used to hand out experience for being asked how something was
done: Watchman Corrin (180 Attack), Smith Halda (220 Crafting) and Hunter Ivo
(160 Cooking). A line of dialogue has no memory, so each could be asked again,
and again -- three clicks was a level, and an afternoon was twenty.

Each is a **tutorial quest** now, on the journal's Tutorials tab beside the
sawpit, the gravel pit and the mill pond, and built the way those always were:
asked for once, done with your hands, paid for on the way back.

| Who | Quest | The doing | Pays |
| --- | --- | --- | --- |
| Watchman Corrin, at the gate | Fighting: The Gate Meadow | Put down 3 boar | 220 Attack, 110 Strength |
| Smith Halda, at the forge | Crafting: A Bad First Helm | Make a Barkwood Helm at a workbench, from the two logs and the hide she hands over | 220 Crafting |
| Hunter Ivo | Cooking: Meat and Fire | Cook 3 pieces of meat at a fire, from the four he hands over | 200 Cooking |

The lesson itself -- the three swings, how a bench works, meat and fire -- can
still be asked for afterwards, as often as anybody likes. It is only words.

Two of those needed something the journal could not say. "Hold three cooked
meat" is finished by the three a new character starts with, and "hold a helm"
by buying one. So there is a new objective, **`craft`**: make N of a thing, at a
bench, an anvil, a cauldron, a loom or a fire, counted as it is made and only
when it is made -- a burnt dinner does not count, and a fleece that spins into
two bolts counts as two. The quest tracker points it at the nearest station of
the kind the recipe wants.

**The rules that came out of it**, which hold for every conversation and not
only these three:

- **A conversation cannot give experience.** The action is gone from the engine
  rather than merely unused, because the capability was the bug: whatever a
  line does, it does every time it is chosen. A data file that still asks for
  it is told so at load. Experience is a quest's to give; a quest ends once.
- **What comes with a quest comes with the quest, or not at all.** Meat handed
  over to learn cooking on is given by the same action that starts the quest,
  and if the quest does not start -- already taken, already done -- nothing is
  handed over, however the conversation was got into. It is the difference
  between a gift and a tap. (`ApplyDialogueAction`, in the dialogue layer, so
  the test that checks the rule runs the code that keeps it.)
- **"I lost it" is said once.** This one was found on the way: the sawpit's
  lent axe, the pit's pick, the angler's rod and Oona's tonic were each replaced
  *whenever they were missing* -- and the forge pays forty coins for a bronze
  axe. Sell it, ask, sell it, ask. A replacement now sets a world flag
  (`set_flag`) and is offered only to somebody who lacks the thing and has not
  had one before (`lacks_item` + `no_flag`), so a genuinely careless apprentice
  is rescued once and nobody is paid a wage for it. Halda offers no refill at
  all: she says where wood and hides come from instead.
- And a line that hands something over with neither a quest nor a memory
  behind it **fails the self-test**, so the next one cannot be added by accident.

### What a thing is worth, before you own it

A shop used to tell you an item's name, its description and what it needed to
be worn -- and not one of its numbers. A sword on a smith's shelf said nothing
about being better than the one in your hand, which is the only question
anybody was asking of it. The same was true at an anvil: you could spend five
bronze bars finding out that the cuirass you made was worse than the one you
had on.

Every shop row and every recipe now shows the piece's bonuses, and beside each
one **the change against what is worn in that slot**:

```
Novice's Robe
Instead of Barkwood Cuirass
Attack       +0    (-1)
Strength     +0    (-2)
Defence     +10    (-4)
Magic        +5    (+5)
```

Green is better, red is worse, `( -- )` is no change. The rules it follows:

- a stat that is nothing on **both** pieces is left off, so a helmet's block is
  one line and not five zeroes -- but a stat the worn piece has and this one
  does not is shown, because losing it is the point;
- the slot decides what it compares against, so a robe is weighed against the
  body armour and not against the sword;
- an empty slot compares against nothing, and the whole bonus reads as the gain
  it is; the piece you are already wearing compares against itself and reads
  `( -- )` all the way down;
- block, walk speed, swing speed, reach and a lamp's throw are shown the same
  way, with swing speed turned round first -- the stored number is a multiplier
  on swing *time*, so smaller is faster, and a stat where less is better has to
  be flipped before anybody reads it;
- a potion says what it would do **for you**: a boost is a flat amount plus a
  share of the level, so `+3 and 12%` is printed as the `+8` a level-45
  character would actually get. Both the panel and the draught read
  `ItemDef::BoostGain`, and the self-test drinks every potion in the game to
  check the promise against the result;
- and a piece with a passive prints it. The bag always did; the shop and the
  anvil never had, which meant the one line that makes a legendary worth having
  was the one line you could not read until you owned it.

### The card beside the cursor

The bag and the storage chest have no room for that block: their lower halves
are already the name, the tier and the description, and the panel is sized to
the grid above it. So in those two the numbers come to the cursor instead -- a
small card beside whatever square is lit, with the same header and the same
rows:

```
   +--------------------------+
   | Steel Helm               |
   | Instead of Iron Helm     |
   | Attack       +4    (+2)  |
   | Strength     +4    (+1)  |
   | Defence     +22    (+6)  |
   +--------------------------+
```

**Only for what can be worn.** A card over every rock and bar would be noise
covering the grid it is trying to explain, so anything with no slot of its own
has none -- which also means the card is a signal in itself: if one appears,
that is a thing you could put on.

It is sized from its own longest line, measured rather than guessed, and placed
beside the square on whichever side it fits, nudged back on-screen rather than
allowed to hang off an edge. It is drawn last of all, over the panel, because a
card under the panel it belongs to is a card nobody can read. The worn list has
one too, which is how you find out what the sword you are holding is actually
worth.

### One set of numbers, three panels

Which rows an item has is **not** the drawing code's decision. `ItemStatLines`
in the item layer builds them -- label, value, change, and a verdict of better,
worse or no change -- and the panels only choose what a verdict looks like.
That is what stops a shop and a bag from disagreeing about the same sword, and
it is what lets the self-test check the rules directly rather than by
screenshot: that a stat which is nothing on both pieces is dropped, that one
the worn piece has and this one does not is kept, that an empty slot shows the
whole bonus as the gain, that a piece weighed against itself changes nothing,
and that a quicker weapon reads as better although the number stored for it is
smaller.

`--audit` sweeps every recipe at all six stations, every shop row, every bag
square and every chest square at three window sizes, with a character wearing a
full set and carrying a piece with a passive and two enchanted ones, so the
longest line any of this can draw is checked rather than assumed.

There is a `--bag a,b,c` switch beside `--wear a,b,c` now, because `--wear`
equips anything with a slot and there was otherwise no way to ask for a helmet
sitting *in* the bag -- which is the one case the card exists for.

### Prices

Prices come from an item's value. A shop charges its markup on the value --
1.0 at a forge, up to 1.3 for a pedlar out on the trail -- and pays a fraction of
it for the things it deals in:

- **Forges** pay 75% for ore, bars and metal, and 70% for weapons, armour and tools.
- **Specialists** pay well for their own trade: Wendel 80% for fish, Bram 80% for
  timber, the Inn Kitchen 70% for anything raw, Ivo 75% for bows, Oona 80% for
  herbs, the Collector 85% for dream shards.
- **General stores** take anything with a price, at 40% (45% in the Reverie).
- Coins and quest items are never bought or sold.

Every rate is below every markup, so nothing can be bought in one shop and sold
in another for more than it cost. The profit is in work:

- **Gathering pays.** Copper ore fetches 8 coins at a forge, a raw trout 19 at
  Wendel's, an oak log 14 at Bram's.
- **Working it pays more.** Anything crafted is worth at least 1.8 times its
  materials (`ItemDatabase::CRAFT_VALUE_ADD`), and cooked food is worth twice
  the raw: a cooked trout sells for 38.
- **Smithing bought bars pays too**, as far as the day's stock goes. Two bronze
  bars and a log cost 84 coins and make a sword Halda buys back for 107; an iron
  sword is 196 in and 248 out, and a steel cuirass 2415 in and 3042 out.

### Limited stock

What a shop sells runs out: ten bronze bars a day at Halda's, two steel bars,
one iron pickaxe. **Every shop restocks at dawn**, when the quest day turns over,
and the dawn message says so: "New notices are up, and the traders have
restocked." What has been sold today is saved.

Better stock is gated on the story, the same way dialogue is:

- Halda has only bronze bars, iron ore and a sword or shield until her forge has
  its copper (Ore for the Forge); then iron bars, coal, steel and an iron
  pickaxe go on the shelf.
- Garrow's azuryte waits on the Trail Wardens, Bram's iron and steel axes on
  clearing the trail, Oona's tonics on Wendel's remedy.
- The Collector opens the better cases after The Water Remembers and Lights on
  the Pond: damascus, platinum and demonite ore, and a dreamcatcher.

**No shop sells what a quest asks you to gather or deliver**, unless that quest
is already finished -- and never what a daily asks for. Nobody sells copper ore,
logs, hides, minnows or dream shards, so the dailies and the early quests stay
work rather than a purchase.

Shops are data, in `data/shops.json`: the keeper, the town, the markup, what they
buy (by item tag -- `ore`, `bar`, `weapon`, `fish`, `raw`, `tier:wood`, or `*` for
anything; see `Trade::Tags`), and the shelf, with a daily stock and optional
`after` quests for each line.

---

## Worn equipment

The character is rendered as a stack of layers rather than a flat sheet —
shadow, the weapon, the body, the head, and five pieces of plate — each its own
sheet, frame-aligned, with a number in the filename giving the draw order.

### Armour is armour, not a colour wash

Every piece of worn plate is **modelled on the rig and rendered as its own
layer**, for every clip and every facing: greaves, cuirass, gauntlets, helm and
shield. The game draws the layer for a slot only when something is worn there,
and paints it the metal of that particular piece — so a bronze helm over an
iron cuirass over steel greaves is drawn as exactly that, three metals at once.
Armour used to be a tint over the whole character, which meant a full set of
damascus and a full set of bronze were the same silhouette in different
colours.

| Layer | Worn on | What it is |
| --- | --- | --- |
| `armour_legs` | legs | knee cops, greaves, sabatons over the boots |
| `armour_body` | body | breastplate with a raised ridge, gorget, fauld, pauldrons, vambraces |
| `armour_hands` | hands | cuffs and gauntlet shells |
| `armour_head` | head | a skullcap with a brow band, nose guard and crest |
| `armour_shield` | shield | a round shield on the off arm |

The sheets are rendered in pale steel and multiplied by the item's own colour
at draw time, so twelve tiers of the same plate cost one render. A piece drives
its layer through `layer` on the tier piece in `data/tiers.json`, so adding a
slot to the paperdoll is a one-line change there and a group in
`tools/blender_character.py`.

### Three cuts, so a tier is not just a colour

Colour alone would still have made a bronze jerkin and a demonite warplate the
same silhouette, so the plate is modelled in **three cuts**, chosen by `cut` on
the tier:

| Cut | Tiers | What changes |
| --- | --- | --- |
| `light` | wood, bronze, iron | a leather cap with no crest or nasal, a strap and a bracer instead of pauldrons, no knee cops, a small buckler, everything a shade darker |
| `plate` | steel to platinum | the full harness: crested helm, pauldrons, poleyns, a round shield |
| `ornate` | demonite, dracon, enchanted | horns off the brow band, a taller crest, a swept wing and a spike on each pauldron, a knee spike, a deeper fauld, a spiked shield |
| `hide` | every tier's hides | a fur-lined hood with a drape and a peak, a jerkin with a fur collar and shoulders and a quiver slung behind, bracers, tassets, wrapped legs with a fur cuff, soft boots |
| `robe` | every tier's robes | a pointed hat with a brim, a mantled robe with bell sleeves and a sash, and a skirt to the ankle that hangs from the hips, so a walk swings the feet out from under the hem instead of bending the cloth at the knee |

The last two are chosen by the *piece*, not the tier, and are head, body and
legs only -- there is no hide gauntlet -- so only those three groups are
rendered for them (`make_character.ps1 -Style hide,robe`, about ten seconds a
clip). The hat is the hard one: a helm is the one layer the head does not cut,
and seen from the side the near half of anything round the head drops down the
screen by most of its radius, so a wide-brimmed hat was a purple ball where the
face should be. The dome and brim are kept close to the skull and set high,
and the height is all in a narrow cone leaning back.

Plate is the cut the sheets are named after, so it carries no suffix; the other
two are rendered as `<clip>_<n>_armour_<slot>_<cut>.png` beside them, and the
engine swaps the sheet for the piece's cut at draw time the way it already
swaps the weapon sheet for the model in hand. Those sheets are listed in
`data/sprites.json` like any other, under `LayerSlot::ArmourAlt`, which is
skipped unless something worn asks for it — the same trick that keeps the
character from holding a sword, a spear, a bow and a staff at once.

Because the cut is per slot, the mixing still works both ways: a bronze helm
over a demonite cuirass is drawn as a leather cap over a horned breastplate, in
two metals.

```powershell
.\tools\make_character.ps1                          # three looks, three cuts
.\tools\make_character.ps1 -Style light -Only idle   # just that cut's armour
```

An alternate cut renders the armour groups only — the body, head and weapon
underneath are the same sheets whatever is worn over them — so each one costs
about a third of a full pass.

Four things that had to be got right, all of them found by looking at the
result rather than by reasoning about it:

- **Plate has to sit outside the silhouette.** At sixty-four pixels a cuirass
  modelled the size of the torso disappears into it. Every piece is fifteen to
  twenty per cent larger than the part it covers.
- **The helm is the one layer the head does not cut.** It is worn over the hair,
  which is as wide as the helm is, so using the head as a holdout sliced the
  crown off and left a steel bowl over the face. Nothing in that group may be
  modelled at or below eye level, because with no holdout it draws straight
  through the face.
- **A capsule hangs from its top cap.** The first cuirass was given a generous
  shoulder and its collar climbed over the character's chin. The same thing
  decides a horn: a capsule points *down*, so the rotation that aims it is
  about Y. Turning it about Z only spun it on its own axis, and the first
  ornate horns stayed buried inside the helm.
- **Every other layer keeps both holdouts**, so a forearm crossing the chest
  still passes in front of the breastplate.

### Weapons and overlay pieces

- **Weapons are real layers.** What you are holding is drawn from its own
  layers and coloured to match the item, so a bronze sword, a steel longsword,
  a bow and a staff all look different in your hand. An empty hand hides the
  weapon layers entirely. Every tier's weapon sheet sits at the weapon's own
  index and only the one in hand is drawn -- they were all being drawn at once,
  and the character went about holding a sword, a spear, a bow and a staff
  together.
- **One set of weapon sheets, three characters.** Every tier's sword, bow,
  staff and spear is rendered once, in the hero's hand, and the warden and the
  wayfarer hold the same sheets: the three are one rig in three sets of
  clothes, and their weapon layers differ by a few pixels in a hundred.
  `"weapons_from": "player_hero"` in `data/sprites.json` says so
  (`SpriteDef::WeaponSheet`), written by `make_sprites_json.ps1`. They used to
  be asked for sheets of their own, which do not exist, so the warden's bow and
  the wayfarer's staff were both drawn as the rig's plain tinted blade, and the
  terminal said `could not load` once for every weapon and clip.
- **Pack pieces can still overlay.** A worn piece may carry a `worn` overlay:
  art drawn on top of the character, positioned by a rectangle given in **frame
  pixels** so it lands on the rig correctly at any camera zoom. A piece with
  neither plate nor overlay falls back to colouring the body and head layers,
  so plain items still read as armour.

```json
"plumed_helm": {
  "worn": {
    "sprite": "assets/icons/armour/worn/plumed_helm.png",
    "after": "head",
    "rect": [26.2, 21.5, 10.5, 13.0],
    "facings": [true, true, true, true]
  }
}
```

There are nine slots — weapon, shield, head, body, hands, legs, feet, amulet,
ring — drawn from the feet up, so a helmet ends up over a gorget and a gauntlet
over a sleeve.

A bow takes both hands. Equipping one puts the shield in the bag, and taking up
a shield puts the bow away; if the bag has no room for what would come off, the
swap is refused rather than the item being lost. The first playtests had a
character loosing arrows with a shield on the other arm. A weapon that brings its own overlay hides the rig’s built-in
sword layers, and is mirrored when the character faces right so it is not held
backwards.

### Affinities

Each of the three characters favours one way of fighting, and says so on the
card at character select: **the hero the blade, the warden the bow, the
wayfarer the staff.** Attacks of that style hit a tenth harder and carry eight
points more accuracy, from the first swing and for good. It is who they are
rather than something learned, so it sits under the skill trees and the
equipment rather than among them (`Player::Affinity`, `AFFINITY_DAMAGE`,
`AFFINITY_BONUS`). It is also **which skill tree is theirs**: the
hero's is Melee, the warden's Ranged, the wayfarer's Magic, and the other two
stay shut (see [Skill trees](#skill-trees)).

### On using icon packs as armour

CraftPix icon packs (fantasy knight armour, RPG boots, mage outfits, daggers)
are 512×512 painted inventory art. Three things follow, and they are different
problems:

- **Proportion is solved, by measurement.** The rig was measured rather than
  guessed: in an idle frame the head layer covers x25..38 y22..35, the torso
  x25..38 y32..44, the shadow under the feet y40..47. Each slot gets a box in
  those coordinates, every icon is trimmed to its drawn pixels at import, and
  the art is fitted inside its box the way a picture fits a frame. Sizing by
  height alone would make a broad pair of gauntlets narrower than an arm;
  sizing by width alone would run a tall tasseted skirt up to the chin.
- **Facing is solved, by testing it.** A single painted view means that in
  profile the character wears a front-on breastplate. Restricting armour to the
  facings it "really" reads in was the obvious answer and the wrong one: at
  twenty-odd pixels tall the front-on view does not read as wrong, whereas a
  character who strips naked the moment they walk sideways very much does. So
  everything is worn from every angle.
- **Style is not solved.** A smooth, anti-aliased 512px icon shrunk to 24px
  reads as a soft blob against 16px-grid pixel art. The importer hardens the
  worn copy — alpha cut to on-or-off, colours stepped to eight levels per
  channel — which gives it a defined edge and a flatter palette, and helps a
  great deal. It still will not pass for hand-drawn pixel armour.

So icons are used where they are strongest: full size in the inventory, and as
a worn overlay you can judge for yourself. Proper pixel-art armour layers drop
into the same slots with no code change.

---

## Projectiles and walls

An arrow or a bolt is stepped in slices no longer than half its own radius, so
nothing passes through a wall between frames. When a step would end inside
geometry, `Map::SweepPoint` answers two questions instead of one:

- **Where it actually stopped** — the last position that was clear, so an
  impact is drawn on the surface and a fire patch burns in front of a wall
  rather than half inside it.
- **Which way that wall faces** — worked out by trying each axis on its own,
  the same way `MoveWithCollision` decides which axis to stop a walking
  character on. If moving in X alone is still clear, it was the Y movement that
  hit something, so the surface is horizontal. Both axes blocked means a
  corner.

The normal is what makes a response possible. Air bolts ricochet — twice for
`gust`, three times for `galewind` — each bounce reflecting about the surface
and costing some speed, so a shot rattling down a corridor settles rather than
pinging forever. Air is the element that gets this because it is the one with
no ground effect: glancing off a wall is what makes it read as air rather than
as a weaker fire bolt.

Everything else stops, and leaves a mark: a flash in the element's colour and
three shards thrown back off the face. Those shards are fixed rather than
random — a spray that reshuffles every frame reads as noise. Without any of
it, a bolt that hit a wall and one that ran out of range looked identical.

A projectile spawned inside geometry, which happens if you fire with your back
against a wall, reports a zero normal rather than an invented one and simply
stops.

---

## Original assets

Every pack in this project is someone else’s art, and the packs do not cover
everything. `tools/blender_props.py` builds props from primitives in code and
renders them headlessly:

```powershell
.\tools\make_props.ps1                    # render, then convert
.\tools\make_props.ps1 -SkipRender        # convert existing renders
.\tools\make_props.ps1 -Only signpost     # one prop
```

Blender renders each prop eight times larger than needed; `make_props.ps1` then
box-downsamples it, flattens the palette, adds a dark rim around the
silhouette, and draws a contact shadow from the prop’s own base. The outline is
the single biggest thing separating a render from the hand-drawn art it sits
beside.

Three things were learned the hard way and are worth knowing before adding a
prop:

- **The camera angle is measured, not chosen.** The CraftPix interior tables
  show their front edge, their legs, and only a sliver of the top — a little
  over thirty degrees above the floor. Rendering at fifty-five hid every table
  leg behind its own top.
- **Model everything far thicker than life.** At fifty-six pixels across a
  two-metre frame, a realistic signpost is two pixels wide and vanishes under
  the outline pass.
- **Buildings need more height than feels right.** The first Mossvale lodge
  read as a lawn on a box: from above, a roof is most of what you see. Taller
  walls in alternating log tones and a dark shingle roof with only patches of
  moss fixed it.
- **Big renders need Cycles tiling.** Rendering a 192-pixel building at eight
  times scale ran Blender out of memory until `setup_render` turned on
  auto-tiling with 256-pixel tiles.
- **This does not beat hand-drawn art at these sizes.** Simple, chunky shapes
  — a signpost, a barrel, a strongbox — come out well. A bookshelf full of
  books does not. Use it for what the packs genuinely lack.

### The player characters

`tools/blender_character.py` builds the three playable characters, poses them
and renders every clip -- idle, walk, run, sprint, attack, thrust, jump, hurt,
death and the four gathering clips, in all four facings -- straight into the
layered sheets the game reads:

```powershell
.\tools\make_character.ps1                      # all three, every clip
.\tools\make_character.ps1 -Look player_warden  # one of them
.\tools\make_character.ps1 -Only walk,sprint    # a couple of clips
```

There is **one rig in three sets of clothes**: a `LOOKS` table of palette
overrides plus two shape switches -- how far the hair locks stretch from their
roots, and whether the character wears the scarf or a rolled collar.

| Character | Looks like |
| --- | --- |
| **Hollow-born** | auburn, cream tunic, the red scarf streaming behind |
| **Greenwarden** | cropped black hair, forest green, a collar instead of a scarf |
| **Wayfarer** | long ash-blond hair, slate blue, a deep blue scarf |

The two that used to stand beside the first were a CraftPix male and female
character. That licence covers using the art in a game but not passing the
files on, which is what made the repository undistributable; these are the
game's own. A save naming a character this build no longer has falls back to
the Hollow-born rather than loading as an invisible player.

The first version was bevelled boxes and read as boxes -- a crate of a head on
planks. The second is modelled the way a sprite is drawn rather than the way a
3D model is built:

- **Rounded forms.** Ellipsoids and tapered capsules only, so the silhouette
  curves and limbs join without seams: spiky hair with bangs and side locks,
  big eyes, a flared tunic with a belt and buckle, round-toed boots, and a red
  scarf whose tail streams out behind at speed.
- **Proportions matched to the CraftPix rigs**, so worn armour overlays authored
  against them still land: the head sits in the same band of the frame.
- **Cel shading.** Diffuse light through a three-step constant ramp, the shadow
  band shifted cool, from one fixed sun at the upper left so every facing is lit
  from the same side as the scenery.
- **Majority reduction, not averaging.** The render is four times game size;
  each game pixel takes the most common colour of the sixteen under it, so the
  bands stay flat and a two-pixel eye stays two pixels. Averaging turned the
  same render to mush.
- **A selective outline**: one pixel round each layer in a darkened version of
  the colour it borders, rather than flat black.
- **Occlusion between layers.** Each layer renders with the always-drawn layers
  in front of it as holdouts, so the scarf tail is cut away where the body hides
  it, and the sword is cut where the body stands in front of it -- in all four
  facings, from one weapon layer. Optional layers never cut others, or an empty
  hand would leave a hole.

The animation is poses written as functions of time rather than keyframes: a
shared gait drives walk and run (knees bending most on the passing step, the
body lowest at footfall, arms against legs, shoulders against hips), and the
sprint exaggerates it until it reads as a different gear at twenty-five pixels
-- a hard lean, the trailing leg driven out straight, the leading knee high,
both feet off the ground between strides, fists pumping. Leaning toward or away
from a camera above only slides the head down over the body, so the up and down
rows keep a fraction of the lean that the side rows show in full. Idle blinks.

---

## Elevation

The ground has height. A map may carry a coarse grid under its `dreamquest`
key, one level per cell:

```json
"elevation": {
  "cell": 64, "cols": 64, "rows": 48,
  "levels": [ 0, 0, 1, 2, ... ],
  "ramps":  [ [x, y, w, h], ... ],
  "face":   "assets/tiles/dirt_dark.png"
}
```

Level 0 is the ground everything used to sit on, so a map with no elevation
block behaves exactly as it did before. Each level lifts what stands on it by
`ELEVATION_RISE` pixels; `Map::LevelAt` and `HeightAt` answer for any world
point, and the world tells every entity its lift once a frame rather than
looking it up inside each draw call.

**Movement.** Stepping between cells of different level is blocked — that is
what makes a cliff a cliff. `ramps` are rectangles where the rule is suspended,
and the overworld puts one along the full length of the road and a clearing
around every place you can enter, because a raised map without them is a set of
islands. The self-test checks exactly that: every portal and the starting spawn
sit on flat ground, and every row of the map has a crossing somewhere on it.

**Drawing.** Ground and scenery are lifted by the terrain under them — scenery
by the ground under its base, not its middle, so a tree at the lip of a bank
belongs to the ground its trunk is on. Then the exposed banks are drawn:
soil texture down the face, grass rolling over the lip, a shadow thrown on the
ground below, and a dark line down the east and west edges so a plateau has an
outline all the way round rather than on one side only.

Four things were got wrong first, and all four are the same mistake — assuming
something would read that did not:

- **A face on its own is a brown bar.** Flat-filled faces looked like a stripe
  painted across the grass, because every other surface in view had grain and
  that one did not. They are textured now.
- **Higher ground has to look higher.** With the same grass above and below,
  the face is just a line between two identical fields. `LevelShade` darkens
  the ground floor slightly and gives it back a level at a time, so the top
  terrace is the texture as drawn. Colour modulation cannot brighten past the
  source, so it has to work in that direction.
- **The grid has to be coarser than the tiles.** At one level per 32px tile the
  terraces came out small and their edges fragmented into two- and three-tile
  bars. At 64 the plateaus are broad and their edges run far enough to read.
- **Height from smooth noise is a staircase.** A clean function of latitude
  terraces the whole map into straight bands from edge to edge. The slope is
  broken up with noise stretched along the east-west axis so each contour
  wanders.

Still to do: the faces are drawn procedurally, and dedicated cliff-edge art
would look considerably better than a textured rectangle with a lip on it.

### Everything on the ground is lifted by it

Characters, monsters and scenery were lifted by the terrain under them. Nothing
else was: an arrow loosed on the third terrace flew forty-two pixels below the
archer's hands and "hit" a monster whose feet it never came near; damage numbers
appeared at the knees of whatever took the damage, or under it; loot lay in
mid-air below the ledge the monster died on; burning ground burned a level down
from the fire. That is what read as the elevation throwing the y-axis off.
`World::LiftAt` is asked by everything drawn in the world now -- shots (each at
the height it was loosed from, all the way, so it does not drop a level crossing
a bank), floating text, pickups, ground effects, impacts and dust -- and what is
lifted is only ever the picture: where things *are*, for collision and for a
blow, is the ground plane it always was.

Walking up a flight of steps crosses from one level's cell to the next in a
single pixel, and the lift used to go with it, a whole level at once. It closes
on the ground's height instead, a level in about a tenth of a second
(`World::UpdateElevation`), so a climb is a climb -- for the player, friends,
monsters and townsfolk alike.

### Getting up and down

**Jump** (`Space`, or left-stick click) is a hop in the direction you are
steering, or facing if you are not. Into a ledge up to `Player::CLIMB_LEVELS`
(two) high it carries you up onto it; off one it drops you down; on flat ground
it is a short hop. It plans the whole jump before leaving the ground -- every
sample along the path must be clear of walls and within two levels of where you
started, so it never passes through a tree or over a sheer cliff to land on the
far side -- and then owns the player until it lands: no steering, no attacks,
no knockback. Airborne, the draw lift is blended between the two terrain
heights plus an arc, so a climb rises smoothly instead of snapping up at the
edge.

A climbable ledge is otherwise indistinguishable from a wall you cannot pass --
you walk into either and stop -- so pushing against one puts **Climb up** or
**Drop down** on screen.

**Stairs.** Where a ramp crosses a level change the face is drawn as a flight
of stairs with stone cheeks, not as a cliff. Ramps used to be invisible
rectangles: walkable, but indistinguishable from the cliff either side.

Without jumping, **923 of the overworld's 3,072 height cells -- 30% of the
map -- could not be reached from the spawn.** The self-test now floods the
height grid from the spawn, allowing walking, ramps and jumps of up to two
levels, and fails if anything is left over.

### A bug worth remembering

The guild hall could not be entered, and it looked like the doorway was on a
cliff. It was: the overworld's. `Map::Unload()` cleared everything except the
height grid, and the parser only ever writes that grid when a map has one, so
a building never overwrote it -- walk from the overworld into any interior and
the interior inherited the hills outside. Every other self-test loads each map
into a fresh `Map`, which can never see that; the new check loads the overworld
and each building into the *same* one, the way the game does, and fails with
the fix reverted.

---

## Ground tiles

`tools/make_ground.ps1` generates them:

```powershell
.\tools\make_ground.ps1
```

They used to be one flat colour each, which was not an accident — they are cut
from CraftPix tilesets by looking for cells that are fully opaque with zero
variance, because those are the palette swatches a tileset is designed to be
laid over. It works, and it is why the overworld read as coloured paper: a
screen of grass was one RGB value repeated four thousand times.

The generated ones carry speckle, blades and grit. Every mark is placed with
wrapped coordinates, which is what makes them seamless — a blade running off
the right edge continues at the left, so there is no seam to line up. Several
variants per family, because one perfect tile repeated across a 4096-pixel map
is still a visible grid; `genmaps.cpp` picks between them with a hash of the
cell coordinates, and asks the asset manifest how many exist rather than being
told.

The road is generated too. The pack's cobbles are a cool blue-grey, and laid
three tiles wide through green grass the Sunken Road and Havenbrook's street
read as a river. It is warm grey setts in running bond now, a full 32-pixel cell
of sixteen stones in three variants, so it no longer shows as a two-by-two
check.

The other half of the coloured-paper problem was the biome boundaries. The
colour drift is smooth noise, and thresholding smooth noise draws a clean
contour — which on a 32px grid is a staircase of squares. The threshold is
jittered per cell now, which dissolves that edge into a scatter of cells from
both families.

---

## Day and night

A day lasts twelve real minutes: half a minute to the hour. The light starts to
go at six in the evening, it is fully dark by half past eight, dawn begins at
five and full daylight is back at seven. The clock only runs while you are
playing, and it is saved, so a night is still a night after a reload. A new
game starts at nine in the morning of day one.

Night is drawn as a light map multiplied over the finished scene
(`src/world/lighting.cpp`): a screen-sized target is cleared to the colour of
the light -- white at noon, amber at sunset, a cold blue at midnight -- and
every light is added onto it as a soft glow before it is laid over the world.
Cooking fires, hearths, the forge and camp fires throw flickering pools of
firelight, fire and air bolts light their way, burning ground glows, and the
player carries a faint light of their own so nobody is lost in the dark. Houses
only dim half as much and are lit by their hearths; the mines keep their own
darkness at any hour. At noon nothing is drawn at all.

Outdoors the birds fall quiet as it gets dark and the crickets start.

### What comes out at night

Night changed the light and nothing else: the same boars stood in the same
meadow, only darker. Now the wilds have **visitors after dark** -- things that
live somewhere worse, a few of them, off the roads.

| Where | By day | After dark |
| --- | --- | --- |
| The Hollowmarch: the meadow | hares, deer, a boar | a **wolf** off the Westwold, or a **bat** out of the well |
| ...the greenwood | deer, foxes, boars, footpads | **wolves**, sometimes two, or the **walking dead** |
| ...the foothills | orc grunts and raiders | **wraiths** and the **walking dead** (not bats: a raider is worse than a bat) |
| ...the Mire | frogs, lizardmen | **wraiths**, and the **hounds** from the bottom of the well |
| ...the Cursed Reach | orc raiders | **hounds**, and the hellgate's **imps** |
| The Whisperwood trail | foxes, boars, footpads | **wolves** and the **walking dead**, back among the trees |
| The Westwold, east of the Wend | hares, deer, foxes | **bats** over the downs, and the **walking dead** |
| ...west of it | wolves | a **bear** down from the Brackenwood, or a **wraith** |
| The Brackenwood | wolves, bears | **wraiths**, the deep well's **grave-walkers**, and **banshees** |
| The Bayou | lizardfolk, hags, the drowned | **blood thralls** and **grave hounds** up out of Hollowrest, and a **nosferatu** or two in the deep south |
| The Ice Spire | ice trolls, wyverns | the white **greatwolves** off the Fells, and one of the Spirewatch's **shades** |
| The Ashen Path | imps, demons | **Greater Demons**, down the road from the plateau |
| Purgatory's Plateau, all four | dragons, Greater Demons | **Revenants** and **Abyssal Demons** |
| The Hexmire, all four | the cult, the Shellbacks | the dead its drums call up: **nosferatu**, **crypt wardens**, **Revenants**, an **Abyssal Demon** |
| The Frostreach, all four | draugr, trolls, warlords | the frozen dead: **crypt wardens**, **Rime Revenants**, **Revenants** and an **Abyssal Demon** |

The rules it was built to:

- **What does not live there.** No visitor is of a kind that stands within
  twenty cells of its post by day. The Westwold gets no wolves at night,
  because it has wolves.
- **Stronger, by a step or two.** Every visitor is above the average of what
  lives round its post, and none is more than about twenty levels above it: a
  level-10 wolf on a meadow of level-3 boars, a level-30 banshee among level-22
  bears. Nothing is sent that a character who belongs in the region cannot
  fight, or at least get away from: the dead are slow, and wolves and bats are
  quick but weak. Only the Mire's hounds and the Reach's imps are both, and
  neither of those was ever a place to stroll.
- **Few.** About eighty posts across four regions, and **half of them kept on any
  one night**, by the day's hash, a pack coming or staying away together: about
  eighteen visitors in the whole Hollowmarch, against a hundred and seventy
  things that live there. Not the same half two nights running.
- **Keep to the road.** No post is within five or six cells of a road or a
  trail, within eleven of a way in, a camp, a bed, a chest, a sign or a person,
  or within eighteen of Havenbrook's gate, where a new character is finding out
  which end of the sword to hold. Towns, buildings, dungeons and the Reverie
  have none. The road is the way to travel after dark, and nightfall says so:
  *"Night falls, and things are abroad that are not by day. Keep to the road,
  or find a bed."*
- **Killed is killed, until tomorrow night.** A visitor does not respawn, and
  going out of a door and in again does not bring it back: it is remembered by
  its post and the day, the way a boss is. It leaves what its kind leaves --
  it is a real wolf.
- **At dawn they go to ground.** Not a death: no cry, no loot, nothing
  counted. It stands as it was and fades. One in the middle of a fight
  finishes the fight first.

The high country's visitors -- everything from the Bayou up -- are chosen by
`genmaps` itself, by these same rules: `PlaceNightVisitors` reads
`data/enemies.json`, works out every post's shown level with the game's own
formula, and for each open spot on a lattice takes the first of a short list
of candidates that is a stranger there, stronger than its neighbours, and not
by too much. So a new region gets its nights right the first time, and the
self-test checks the result the same way it checks the lowlands. Ten a map on
the plateau, twelve on the Ashen Path, fourteen in the Bayou, six on the Spire.

How: a spawn in a map can be marked `"night": true` with a `"chance"`
(`EnemySpawnDef::night`, `chance`). It is **always in the monster list, up or
not**, because friends in co-op count monsters by their place in it; by day it
lies the way a boss killed today does. `World::Abroad` asks, each frame: is it
night, is tonight one of this post's nights (`World::KeptTonight`, a salted
twin of the hash that picks a pool's monster), and has it been killed tonight?
`genmaps` writes the posts **after everything else a map has**
(`MapBuilder::NightEnemy`), so every post that was there before keeps its
number; keeps them clear of havens (`MapBuilder::NearestHaven`); and leaves
them out of the quest-waypoint index, so a contract for wolves points at where
wolves live and not at where two might be after dark.

### Sleep and the dreamworld

From **seven in the evening until four in the morning** you can sleep, and a
bed asks how you would spend the night. It is a two-row panel on the parchment
a note is read on, headed with what you are lying down on and how far off dawn
is:

| Choice | What happens |
|---|---|
| **Sleep through the night** | The screen fades on "You sleep the night through...", the clock goes to five in the morning, and you wake where you lay down: "Dawn breaks." No dream. |
| **Go into the Reverie** | Sleeping is a journey: the screen fades, "You drift off to sleep...", and you wake up somewhere else -- **the Reverie**, the dreamworld, for as long as the night lasts. |

`Esc` stays up and nothing has happened. The cursor stays on whichever you chose
last. Either way you lie down rested -- health, mana and breath are full -- and
either way dawn turns the quest day over, so the boards post new notices and
the traders restock. A night slept through finds the room as any arrival
would: the monsters of the map are back where they live, and anything left
lying on the floor is gone. A bed used to do only the second of these, which
made the dream the price of a night's rest.

Earlier than seven, a bed tells you it is for after dusk. You cannot sleep with
a hostile monster nearby, and the bed does not ask.

There are three kinds of place to sleep:

- **Beds** -- Elder Maren's, the three guest rooms at the Barley and Bell,
  Oona's in Mossvale and the ferry cottage's double bed in Fernhollow. **The
  inn's are paid for by the night** -- 15 coins a single, 25 the double, said
  on the prompt and taken only if you do sleep. Everybody else's bed is theirs
  to lend, and a camp costs nothing.
- **Campsites** -- the tents at Bram's camp on the Whisperwood trail and at the
  traveller's camp in Fernhollow.
- **Your own camp.** A **Bedroll** is sold at the general stores and made at a
  workbench from 2 waxed thread and 2 raw hide. Use it from the bag
  under open sky and it pitches a tent and a fire where you stand. After dusk it
  offers "Sleep at your camp" and asks the same question a bed does; by day,
  "Pack up your camp" puts the bedroll back
  in your bag. There is one camp at a time -- pitching another packs the first
  away -- and it stays where you left it, on its map, across saves. Not indoors,
  not in the mines, not on uneven ground and not on top of a way out.

Going to sleep restores health, mana and stamina, whichever way the night is
spent.

**The Reverie** is cloud islands over a starry void, joined by plank
bridges. It is lit a dream's violet, wisps of light
drift up out of the void, and the ambience is a slow shimmering chord with
chimes far off. A voice on the arrival island explains the rules:

- **Dawn ends the dream.** At five in the morning you wake exactly where you lay
  down, rested. The HUD counts down to it ("Dreaming  dawn in 3:12").
- **The waking stone** beside where you arrive wakes you straight away, with the
  night still going, if you would rather.
- **You cannot die in a dream.** A nightmare that bests you throws you awake, in
  your bed, whole -- but the rest of the night goes with it.

The islands are where the night's work is: a grove to the north, a meadow to
the west, a field to the east with **dream crystals** to mine (Mining 1), and
to the south the plateau where the **Nightmare Brute** guards a chest. What
lives on them is the waking world's monsters in a bad night's colours, and what
they drop is real: **dream shards** come back with you, and six of them with
two thread make a **Dreamcatcher** at a workbench, an amulet worth +10 Magic,
+8 Ranged and +4 Defence.

#### It goes down

The Reverie used to be those five islands and nothing else. It is nine now --
four shelves further out, each at the end of two bridges, so the five are a
ring and not a star -- and behind the brute's plateau, on a spur only his
plateau leads to, there is **a ladder down**.

| Depth | Map | | Advised | Kept by |
| --- | --- | --- | --- | --- |
| 1 | `dreamworld` | **The Reverie** -- 9 islands, 11 bridges | -- | the Nightmare Brute |
| 2 | `dreamworld_2` | **The Deep Reverie** -- 11 islands, 16 bridges | Combat 25 | the Sleepless |
| 3 | `dreamworld_3` | **The Dreaming Dark** -- 13 islands, 20 bridges | Combat 50 | the Unwaking |

Each is bigger than the one above it, darker -- the violet goes out of the
light a ladder at a time, and the cloud underfoot goes from snow to storm --
and harder: the easiest thing at a depth is stronger than the hardest thing a
ladder up. What guards the way on does not move: the brute has the first
ladder behind him, **the Sleepless** (a troll that has never slept) the second,
and at the far end of the bottom is **the Unwaking**, which is a dragon, and is
what the rest of them are dreams of. Each of the three stands over a chest.

**One more shard a ladder.** Everything that leaves a dream shard leaves one
more for each ladder climbed down: a kill, a crystal, a chest. A nightmare that
leaves one in the Reverie leaves two in the Deep Reverie and three in the
Dreaming Dark; a crystal there gives three at a swing. It is once for whatever
it was, not once a stack -- a kill that drops two stacks of shards has one of
them made bigger. The map says how deep it is (`"dream_depth"`), and
`World::DreamBonus` is the whole of the rule. The deeper crystals ask more of
the miner: Mining 20, then 45.

Everything that is true of a dream is true of all of it, because all of that
asks whether the map's ambience is a dream's and not which map it is: dawn
wakes you from the bottom as it does from the top, where you lay down; a
nightmare that bests you throws you awake and cannot kill you; there is a
waking stone at the foot of each ladder; and a save made two ladders down
carries on two ladders down. The ladders are climbed on purpose (`E`), and the
ones going down say what they are going down to.

#### It is never the same dream twice

Shades in the grove and boars in the meadow was every night. It is some nights
now. Every platform's posts share a **pool** of the depth's monsters and a
**group**, and which of the pool keeps them is settled as the map is walked
into, by the day:

| Depth | Who might be there |
| --- | --- |
| 1 | Nightmare Shade, Dread Boar, Gloom Spider, Pale Stag, Dusk Wolf |
| 2 | Dreamfang (wolf), Sleepwalker (lizardman), Night Terror (wraith), Gloomwing (bat), Hollow Sleeper (skeleton) |
| 3 | Dread Bear, Nightmare Hound, Dream Fiend (demon), Wailing Dream (banshee), Night Wyvern, Pale Reaper (ankou) |

A platform agrees with itself, so it holds a pack of one kind and not one of
each, and no two platforms need agree; their levels vary a little as well. It
is the same all night -- up and down the ladders, across a reload, before and
after midnight, because the day that settles it is the quest day, which turns
over at dawn -- and a different dream the next night. All of them count as
nightmares to the Dreamer's Slate, so "Nightmares Undone" can be done whatever
came; the brute keeps his post every night because two quests send people to
him by name.

`World::ResolveSpawn(post, map, day, index)` is the whole of it: a hash of the
map, the day and the post's group picks from the pool, and another of the
post's place in the file picks the level. It is pure, so the self-test can ask
it about any night of any month; and it takes nothing a guest does not already
have. That matters: a guest's machine builds its own monsters out of the map
file and is only ever told *where* they are, by number, so host and guest have
to come to the same answer separately. They are both told the day. (The guest
is now told it before it loads the map rather than after: `Guest::SetTheDay`.)
A post with no pool is exactly what the map file says it is, as it always was,
so nothing outside the dream has changed -- but anywhere could be given one.

Below the first depth the light is low enough to lose a dark thing in, so what
lives there is lit from inside, faintly: a Gloomwing is a shape with a glow
round it and not a hole in the floor.

The ladders are `prop_dream_ladder_down` -- a hole worn through the cloud, the
top of a ladder standing out of it -- and `prop_dream_ladder_up`, the same
ladder from underneath, climbing until there is no more of it to see. The three
builders in `tools/genmaps.cpp` share a `DreamField`: the islands, the bridges
between any two of them, and the questions every builder asks of those.
`--hour 22` starts a `--scratch` game at night, which a look at
`--map dreamworld_3 from_above` needs: a dream walked into by daylight is over.

A save made in a dream remembers where you are sleeping, so loading it carries
on the same dream and wakes you in the same place.

---

## The HUD

Health and mana sit in the top left in brass plates: a bevelled frame lit from
the top left, a sunk track with quarter ticks, rivets at the corners of the
health bar, and a heart or droplet glyph at the left end so the two bars are
told apart at a glance rather than by colour alone.

Under them, a slimmer amber bar with a lightning bolt is stamina, spent by
sprinting; it pulses red and reads "winded" when it has been run dry.

Under those, a sun or a moon and the time: "Day 2  21:40  Night". It turns
blue once it is late enough to sleep, and in a dream it counts down to dawn.

With a technique chosen in a skill tree, the line under the prompts says what
holding the heavy attack will do; with a staff it is on the spell line.

In a fight, a frame at the top centre names the target with its level and
health, and says LOCKED in a red border while the lock is on. The line along the
bottom lists the keys: attack, heavy, target, sprint, bag, skills, quests, map,
menu.

The top right is a round minimap under a brass bezel with rivets, cardinal
notches and an amber pip at north. It is north-up and centred on the player:
terrain in the average colour of each ground tile, portals green, NPCs blue,
living monsters red, and the player gold with a nose pointing the way they
face. The map's name sits under the dial. Toasts and the quest tracker stack
below it, and the FPS counter moved to the bottom right.

The terrain is baked once per map into a small image -- one pixel per 8 world
pixels outdoors, per 4 in houses so a room is not a thumbnail -- and drawn as a
stack of one-pixel strips, each as wide as the circle at that height. That
keeps the map round without a mask or a shader and pixel-for-pixel crisp; the
game still holds its 72 fps cap.

The **chain counter** sits under the target frame while melee swings are
landing one after another; see "Combos" above.

### The map

**M** opens **the map of wherever you are** -- Fernhollow in Fernhollow, the
Ashen Path on the Ashen Path, the lower workings in the lower workings -- and
`J` (or left and right) turns the page to the whole Hollowmarch and back. It
used to be the Hollowmarch and nothing else, with a line of text to say "you
are in Mossvale", which is not much of a map of Mossvale.

A page is baked the way the minimap's dial is, but from the map's own file
rather than from whatever is loaded, so any page can be drawn from anywhere;
the `Map` it bakes from is thrown away and only the picture kept. Ground first;
then whatever stands on it -- every tree, rock, house and fence -- as a smudge of
its own colour where its foot is, which is what makes a wood a wood and a
village a village at a few pixels to the tile. Underground, where the rock and
the floor cut out of it are much the same grey, what cannot be walked on is
drawn dark, so the rooms and passages are what is left.

Every mark is a lettered tile in its own colour with its name beside it, and
the legend down the right lists only what is on the page in front of you:

| | |
| --- | --- |
| **T** Town | on the Hollowmarch, with a row of chips under it for the trades it keeps |
| **D** Dungeon | a way down: the mine, the barrow, the well, the pit, the next floor |
| **>** Way to another land | labelled, with the Combat level advised beyond it |
| **^** Building | a door you can go in by |
| **$** Trader | by name, with a chip for their trade |
| **=** Bench, anvil or cauldron | somewhere to make things |
| **+** Graveyard, **!** Enemy camp, **\*** Landmark | Hollowrest, the lizardmen, a mission board, a campsite, the storage chest |

**Where you are** is a white dot: on the page itself when you are out on it, and
any friends on the same map are dots in blue with their names. **A room is not
given a page**: inside the Barley and Bell the map is Havenbrook with the dot on
the inn's door, and upstairs at the inn is still the inn's door. And on the
Hollowmarch from somewhere else, the dot is on **the road that starts towards
wherever that is** -- the Whisperwood trailhead from Fernhollow, Havenbrook's
gate from the Brackenwood -- unless no road does: the Reverie.

Names are written where they do not lie on each other -- beside the mark, on its
other side, or a line lower -- since a well, a town and a gate within a few
pixels of each other were three names in one smear.

What is marked on the Hollowmarch is written down, in `data/worldmap.json`,
which `genmaps` emits as it places things: it knows which portal is a dungeon
mouth and which is the road out to another zone, where a runtime scan could
only guess. What is marked on every other page is read off that map itself
(`WorldMapPanel::MarksOf`): its ways out, told apart by what the far side is;
its traders; its benches, boards and camps. What the far side *is* comes from a
list in the same file, one line a map -- its name, whether it is country, a
dungeon or a room, and where its ways out lead -- gathered by `genmaps` as each
map is written, so a new map has a page by being built. A town's chips --
**F** forge, **G** general store, **I** inn kitchen, **B** bowyer, and so on --
are never written down anywhere: they are read from the shop database by the
town each shop says it belongs to, so a trader added to a town appears on the
map without anything being written down twice.

The bezel is generated by `tools/make_ui.ps1` and the glyphs live in
`tools/icons.txt`; both run as part of `import_assets.ps1`.

### The title screen

The front end -- the main menu, character select, the slot lists -- stands on
the cover painting rather than on a flat colour, with a night sky moving over
it (`src/ui/titlescreen.cpp`).

The painting is drawn **straight from `art/dreamquest_cover.png`**, cropped to
whatever shape the window is and scaled to fill it, so there is no background
file at some guessed resolution to keep in step with the window: at 16:9 it
takes a band, at a tall window a column, and either way it covers. The band is
taken a little above centre, because the composition is a sword under a
crescent moon and a centred crop off a square canvas cuts the top off the moon.

Over it, about 120 stars breathe on their own phases, a fifth of them drawn as
the four-pointed sparkles the painting itself is full of, and a meteor crosses
the upper sky every five to twelve seconds. Three things keep it from becoming
wallpaper noise:

- **The sky is the same sky every time.** The scatter comes off a fixed seed, so
  it is part of the picture rather than a different arrangement each run.
- **Stars go where sky is.** Not in the middle column, which is the moon, the
  blade and the menu panel, and not over the outer sixth below a third of the
  way down, which is the ruins. A star crawling over a stone pillar reads as a
  bug.
- **Nothing moves quickly.** Twinkles are a quarter to one a second and a meteor
  is gone inside a second, fading in and out rather than appearing.

Everything on top of it -- the title, the headings, the key prompts -- is drawn
shadowed, because flat text over a moonlit sky is hard to read.

---

## Sound

There are no audio files. `src/systems/audio.cpp` synthesises every effect at
start-up -- 39 of them, from tones, filtered noise, struck-metal partials and
Karplus-Strong plucked strings -- and plays them through one SDL3 audio stream
with a small mixer.

- **Combat:** swings (pitched up through a light chain, heavier for strong and
  charged attacks), hits, critical hits, blocked hits, deaths, the bow's twang,
  a cast pitched by element, arrows striking walls, and a monster's swing as it
  winds up. A killing blow is heard as the death rather than a hit on top of it.
- **The world:** footsteps by distance walked -- earth outdoors, planks indoors,
  stone in the mines -- chopping and mining strikes while you work, cooking and
  burning, chests, doors, locked doors, portals, pickups and coins, eating and
  equipping.
- **Progress:** level-up arpeggio, quest start and quest complete fanfares.
- **Sleep:** a slow falling arpeggio as you drift off, and a rising one with a
  bell when you wake.
- **Fishing:** a line going into the water, and the same splash, higher, when
  something bites.
- **Menus:** cursor ticks, confirm, back and error, handled once in
  `Game::Update` rather than in every screen.

Sounds in the world are panned and fade with distance from the player, and
anything but a menu sound varies its pitch a few percent so a run of hits does
not sound mechanical.

**Ambience** is generated live, per map kind, and cross-fades on every map
change: wind with slow swells and birdsong in the forest, groves and fields;
a low breathing drone and echoing drips in the mines; a hearth's rumble and
crackle indoors; a quiet wind on the title screen. At night the birds give way to crickets, and
the dreamworld has three detuned sines drifting against each other under
far-off chimes.

Options has Master, Effects and Ambience volume. With no playback device the
game runs silently rather than failing.

---

## Item icons

Most inventory icons are cut from the CraftPix RPG UI icon sheet, but that
sheet has no log, bow, staff, hide, ore or roast, and those items had been
given the nearest cell in spirit. Playing the game showed how far off that was:
the Training Bow was a blue sword, Raw Hide and the Leather Jerkin were a boot,
both staves were an eye on a green tile, Roast Boar was a blue lump, and logs
and ore were metal ingots.

Those thirteen are drawn by hand instead, as text in `tools/icons.txt` — one
character per pixel, with a small palette per icon — and painted by:

```powershell
.\tools\make_icons.ps1
```

Text rather than image files so they can be read, diffed and touched up without
an image editor. `import_assets.ps1` runs this and `make_ground.ps1` at the end
of an import, because both overwrite or add to what the import cuts; the import
also runs `make_decals.ps1` for the ground decals.

---

## Attack speed and cooldown

Weapons declare a `speed`, a multiplier on swing time, so **lower is faster**:
a dagger is 0.70, a bronze sword 1.00, a steel longsword 1.30. It scales every
phase of the swing and the cooldown after it, so a weapon’s whole rhythm moves
together rather than just the part you can see — and the animation is played at
a matching rate, or the character is still following through when the hitbox
has gone. Reach, width and knockback are deliberately left alone: those are
properties of the weapon’s shape, not of how fast it moves.

The property had been in `data/items.json` since the beginning and nothing ever
read it. `Equipment::AttackSpeed()` existed and was never called.

### Spears

A spear is the other melee weapon every tier makes, beside the sword: a fire-
hardened wooden one, then bronze through demonite, smithed at the anvil from
two bars and two logs at the same level as that tier's sword. It is for keeping
a fight at the end of the shaft. A weapon can declare its **shape** as well as
its speed -- `reach`, `sweep` and `push` in `data/tiers.json`, multipliers on
each attack's own reach, width and knockback -- and a spear's are 1.75, 0.6 and
1.35: every strike reaches three-quarters as far again as a sword's down a line
not much more than half as wide, and shoves what it hits further back. A light
jab lands on a monster about 50 pixels away, where a sword swings short. The
target lock turns a spear toward a target from further off to match. The reach
is paid for: a spear is slower than a sword of the same metal (speed 1.12), a
little weaker, and useless against something beside you rather than in front.

It strikes with its own clip. The hero's **thrust** (`pose_thrust` in
`tools/blender_character.py`) draws the hand back to the hip and drives it
straight out along the facing with a step in behind; the grip is counter-turned
against the arm and the lean every frame, and the chest does not twist, so the
shaft stays level -- a turning chest swung it across the body. A weapon names
the clip it strikes with (`"clip": "thrust"`), and a rig without that clip
swings its ordinary attack. Out of a fight a spear is carried upright beside the
shoulder, the way a staff is. Halda's forge sells a bronze spear and Mossvale's
smith an iron one, Halda posts orders for iron spears, and lizardmen now and
then leave a bronze one behind.

## Starting out

The first thing a new character sees, before the first step, is **a note of
welcome** on the parchment a sign is read on: where they are standing, where
the town, the mine and the trail are, what every key does -- named for the
device in use, so a pad shows its buttons -- and that the night will take
them somewhere else. Once, on a new game only; a load puts the player back
mid-story.


A new character starts with **25 coins, three cooked meat, and the wood tier's
weapon of their affinity, worn with the wood tier's armour of their own kind**:

| | Weapon | Armour | And |
| --- | --- | --- | --- |
| The hero | Wooden Sword | Barkwood Cuirass | Wooden Shield |
| The warden | Oak Shortbow | **Rawhide** Coif, Jerkin and Chaps | Hide Boots -- a bow takes both hands, and a shield they could not raise is no use to them |
| The wayfarer | Wooden Staff | **Homespun** Hat, Robe and Skirt | Wooden Shield -- a staff is held in one hand |

All three used to set out in the hero's cuirass, which is plate: it does
nothing for a bow or a staff, and the first thing the other two learned about
armour was that theirs was the wrong sort. Hide and cloth turn less than wood
a piece, so it is the whole set, and it comes out even -- Defence 26, 26 and
25 -- with the set's own small push on top: +5 Ranged for the warden, +7 Magic
for the wayfarer. The character card draws each in what they wear. Nothing
else: a bedroll and anything better are bought from the traders, found or made. The tools are lent, by the three people in Havenbrook
who work with them. Every character used to start with the sword, which sent
two of the three into their first fight with the one weapon their affinity
does nothing for.

The armour is not generosity, it is the accuracy formula.
Defence is `(level + 8) x (bonus + 64)`, so at level 1 the bonus from what you
are wearing is most of the number: with an empty body slot a boar hits a new
character 60% of the time and an orc 65%, while they hit back at about 42%.
Twenty-six points of defence bonus brings that to 44% and 50%, and the first
hour stops feeling arranged against you.

### Learning a trade

Havenbrook has three working places inside its fence, and each one teaches a
gathering skill to someone who has never done it and lends the tool to do it
with. None of the three asks for anything first, and all three can be walked to
from the south gate.

| Where | Who | Lends | Asks for | Teaches |
| --- | --- | --- | --- | --- |
| **The sawpit**, north-west | Sawyer Jessa | a bronze axe | 10 logs | Woodcutting |
| **The gravel pit**, north-east | Pitmaster Dorn | a bronze pickaxe | 8 copper ore | Mining |
| **The mill pond**, south-east | Angler Sula | a fishing rod | 6 minnow | Fishing |

Each conversation has a **"How does it work?"** line before the work is taken
on, and the same lesson again from the reminder afterwards, so the tutorial is
never a wall of text you have to read before you can say yes. The lesson says
the things the game never says out loud: that the tool only has to be in the
pack, not in hand; that you stand by a tree, a seam or the water until the
prompt names it and press use; that the work repeats until you move away or
press use again; what the skill's level actually changes; and, for each trade,
where to go next and who buys what you bring back. If the tool is lost, the
reminder has a line that hands over another.

The work is gathered from the camp itself -- a stand of young oak behind the
sawpit, copper in the pit face, minnow off the jetty -- and carried back. On
the hand-in the tool stops being lent: **it is kept**, along with the coins and
the experience. The three places are built in `tools/genmaps.cpp` with three
props of their own (`sawmill`, `ore_cart` and `rowboat` in
`tools/blender_props.py`): a log up on trestles with the saw still in the cut,
a tipper cart on a length of rail, and a boat drawn up on the bank beside a
plank jetty over the pond.

**Cooldown** is separate from recovery. Recovery is part of the swing and you
are committed during it; the cooldown is the gap *after* it, and it is what
stops the attack button being something you hold down. Mid-chain light attacks
have almost none, which is what makes continuing a combo quicker than starting
one; the finisher, the strong attack and the charged attack each cost more.
Bare-handed that works out at a sustained 2.2 hits a second, which the
self-test measures rather than assumes.

It is shown, because a gate the player cannot see is just an unresponsive
button: a thin bar under the feet that drains, gone inside a fifth of a second
between light attacks. The inventory states the speed as a rate — "1.14x
(fast)" — rather than as the raw multiplier, because a stat where smaller is
better needs explaining every time it is read.

Ranged and magic go through the same state machine, so a bow’s speed is its
rate of fire and a stave’s is its cast rate, with no extra code.

---

## Quests

Quests reach you three ways, all of them live:

- **The mission board** in Havenbrook — four contracts, gated on level and on
  what you have already finished, plus the day's two daily notices.
- **Innkeeper Bess** has something living in her cellar: *Rats in the Cellar* sends
  a new character down the hatch behind the bar to kill six rats, four spiders and
  the broodmother, and back up to tell her. She also remembers when the well in
  the square still ran: *The Dry Well* is hers.
- **NPC conversations** — Elder Maren runs the main chain (a letter, a missing
  surveyor, and what is gathering the orcs under Emberfell). The innkeeper,
  the smith, the watchman and the hunter have their own.
- **The woodland villages** -- Mossvale's notice board and the people of
  Mossvale, Fernhollow and the Whisperwood camp give the woodland chain.
- **Notes left in the world** — a water-stained note at the edge of the Mire
  starts the barrow chain, and a torn survey page on the Sunken Road advances
  Maren's.
- **The dreamworld** — Mira at the Fernhollow shrine and Hesper the ferryman's
  widow send you to sleep with a purpose, and the Dreamer's Slate in the
  Reverie posts its own notices.
- **Elder Vask**, in the guild hall, has been waiting fifty years for somebody
  who could climb the Ice Spire and kill what is sitting on it.
- **Side quests in the new country** -- seven people in the towns want
  something done in the Westwold, the Brackenwood, the Bayou, Hollowrest Crypt
  or on the Ashen Path, and eight strange things lie out there that start a
  quest of their own when they are picked up. See *Side quests in the new
  country*, below.

### Waypoints: where the quest is

A quest says "take Maren's letter to the guild hall", and the game used to leave
it there: which building that was, in which town, down which road, was the
player's to remember. Now **the quest being followed is pointed at**:

- **In the world**, a gold chevron bobs over whoever or whatever the stage wants
  when it is in sight, and when it is not, a gold arrow sits at the edge of the
  view pointing at it, with how many paces off it is.
- **On the minimap**, a gold diamond, which rides the rim of the dial in the
  right direction when the thing is further off than the glass can see.
- **In the tracker**, the followed quest leads, in gold, with a line under its
  objective saying where: *Wild Boar, 12 paces north-east*, or *in Havenbrook
  Guild Hall - Enter Havenbrook*.
- **On the map screen** (`M`), a pulsing gold mark with the quest's name: on the
  thing if it is on that page, on the door of the room it is in if it is in one,
  and otherwise on the way off the page that leads towards it.

If the thing is on another map, all four point at **the way out of this map that
starts towards it** -- the first door of the shortest road, counted in maps --
so the marker is always something that is actually here to walk to. From the
Hollowmarch, Maren's letter points at Havenbrook's gate; inside the gate, at the
guild hall's door; inside that, at the guild master.

| The stage says | It points at |
| --- | --- |
| Talk | the person -- where they are now, if they walk a round |
| Deliver | whoever wants it, once the bag holds enough; until then, wherever it comes from |
| Collect | wherever it comes from: what yields it (trees for logs, a seam for ore), water to fish in, or the nearest thing that drops it |
| Kill | the nearest one still standing, on the map the quest names if it names one |
| Interact | the thing |
| Reach | the road to the place |

When there is no road it says why rather than pointing at nothing: the Reverie
is *"In the Reverie: sleep in a bed after dusk, and choose to dream"*, and a
villager who has gone in for the night is said to have. What is only bought or
made has nowhere to point, and says nothing.

**Which quest** is followed is the newest taken, until one is chosen: confirm on
a quest in hand in the journal follows it (the row says *following*), and it
stays followed until it is done or confirm is pressed on it again. A finished
one hands over to the newest still in hand. The choice is kept in the save.
**Options -> Quest Waypoints** turns the whole thing off.

It works on maps the player has never loaded, because it does not ask the maps.
`genmaps` writes `data/waypoints.json` as it builds them -- who stands where,
what is where and what it yields, what lives where (every monster a post might
be kept by, for the Reverie's nightly rosters), and which way out leads to which
map -- and `WaypointIndex` (`src/systems/waypoint.h`) answers from that: the
spots a stage could be done at, the road to each by the fewest doors, and the
nearest. On the map the player is standing on it looks at what is alive instead,
so a watchman is where his round has taken him and "the nearest boar" is one
that is still a boar. Worked out four times a second, and at once when the map,
the quest, its stage or what the bag holds changes; each half of a split screen
has its own.

`--quest q_marens_letter,q_thin_the_herd` takes quests for a `--scratch`
character, and `--screen controls|options|map|journal` opens a screen, for
looking at all this.

### The journal's three tabs

The journal is split three ways:

| Tab | What is on it |
| --- | --- |
| **Story** | the line the world is actually about: Maren's chain, the road beneath the leaves, the barrow, the two dream quests, the well, the dragon |
| **Tutorials** | the three trades, each named for what it teaches -- *Woodcutting: The Sawpit*, *Mining: The Gravel Pit*, *Fishing: The Mill Pond* |
| **Side quests** | board contracts, daily orders, and the favours people ask |

Left and right step between them, each keeps its own place in its list, and the
headings carry the counts (`Story 2/8`: two in hand out of eight the tab knows
about). A quest lands on a tab by what `data/quests.json` says -- `"major":
true` for the story, `"tutorial": true` for a trade -- and the loader refuses
both marks to anything off a board and anything repeatable, so an errand cannot
end up in the main line by a typo.

**Every line is coloured by its state**, which is what makes the tab readable
without being read: **red** for a quest not started, **blue** for one in hand,
**green** for one finished. The detail panel says the same word -- *Not
started*, *In progress*, *Completed* -- in the same colour.

Quests **not yet taken are listed too**, under whatever is in hand and in the
order they are meant to be met, which is the point of the story tab: what is
still ahead is as much a part of a journal as what is in it. A quest not yet
taken shows its first step in place of an objective, and its Combat requirement
if it has one, in green once you meet it. Dailies are the exception: there are
dozens of them and they come back every morning, so they are listed only while
one is actually taken. A list longer than nine rows scrolls with the cursor and
says where you are in it.

### The Drowned King

The barrow's second chamber, and the one **legendary** item in the game. Once
the barrow has been opened, Guild Master Orlend will show you the guild's oldest
ledger: forty years ago a party went down, came out four short, and wrote that
the far chamber was under water and that the man they left behind was wearing
the king's own boots. Go back down, open his chest, and tell Orlend what was in
it.

Inside are the **Boots of the Drowned King** (Defence 24, a little Strength,
Defence 15 to wear), and they carry a **passive**: *Marshstride* -- you walk a
seventh quicker, and ground that burns takes half as much out of you. A passive
is a named effect an item has while it is worn (`"passive"` and
`"passive_text"` in `data/items.json`); anything that cares asks for it by name,
so the boots' speed lives in the player's movement and their half-damage in the
hazard tick, and the bag prints what they do under the description.

**They exist in exactly one place.** They are in no loot table, on no shelf and
in no quest's reward list -- the chest holds them *by name* (`"item"` on the
object, which no table roll can produce) -- and the chest itself carries
`"needs_quest"`, so it is not drawn, not lit and cannot be opened except while
that quest is being done. Before you take the quest the far chamber is empty;
after you hand it in, it is empty again. The self-test checks all four of those
things, because "unique" is a claim that rots the moment somebody adds a drop
table.

### The Dragon of the Ice Spire

The first quest with a **Combat requirement you have to have earned**: Combat
35, and it is the giver, not the quest log, who enforces it. Elder Vask sits in
the guild hall in a rocking chair, a blanket over his knees and a stick across
them, and to anyone who could not survive the climb he offers exactly one line
-- `*grunt*` -- and goes on rocking. At Combat 35 he looks up, and the
conversation he has been saving for fifty years comes out: eleven of the
Spirewatch went up, one came down, and **Hoarfang** has held the summit since.
Climb the Ice Spire, kill it, and bring him back a tooth.

He has **art of his own** rather than a townsfolk sheet (`build_vask` in
`tools/blender_creatures.py`): white-bearded, bald on top, stooped so far
forward that the stoop is most of what reads as age, with a shawl on his
shoulders and boots that have not been outside in a while. The **rocking chair
is part of the sprite rather than a prop beside him** -- a chair he is not
actually sitting in reads as furniture he happens to be standing next to, and a
prop cannot rock. Everything in the rig hangs off one joint down at the
rockers, so tilting that joint rocks the man and the chair together, which is
the entire animation; the floor his chair stands on is blocked, so he cannot be
walked through.

**Hoarfang** is its own creature (`build_dragon` in `tools/blender_creatures.py`),
not a bigger wyvern: four legs, a heavier body, a short thick neck and wings
that fold along the flank at rest and are thrown wide when it rears. 760 hit
points, an attack level in the sixties, and the widest reach of anything in the
game. It stands on its own ground above the matriarch's nests, at the top of
the peak where the ice spires ring a hollow, and it does not respawn. It leaves
dragon fangs, wyvern scales by the handful, diamond and platinum, and now and
then a diamond spear or a piece of diamond plate.

### Prerequisites

A quest can need other quests finished first, a Combat level (`"req":
{"Combat": 12}`) and skill levels. **No one offers a quest you cannot start**:
an offer is gated on the quest being *available* -- not started, every
prerequisite done, every level met -- rather than merely not started, which is
what let Maren ask about the Sunken Road before the letter had been carried.

| Quest | Needs |
| --- | --- |
| The Sunken Road | Maren's letter, Combat 5 |
| Orc Trouble | Thin the Herd, Combat 6 |
| The Barrow Seal | Combat 10 (started from its note) |
| Trail Wardens | Clear the Trail |
| Emberfell Depths | The Sunken Road, Combat 12 |
| The Water Remembers | The Old Offering |
| Lights on the Pond | The Water Remembers, Combat 18 |

Dialogue conditions can ask for a quest's state (`not_started`, `available`,
`locked`, `active`, `complete`), a stage, an item, a skill or Combat level,
quests finished (`"after": [...]`), a world flag or its absence (`"flag"`,
`"no_flag"`), and the time of day. A condition that needs the player's state
and is asked without it fails rather than passing, so a line never shows by
default. Some flags are set by the world itself: `visited:<map>` the first
time a map loads, so Hesper talks about the lights differently once you have
seen the Reverie.

When there is nothing to give yet, the NPC says so instead of going quiet:
Maren, asked for more work between chapters, tells you to come back stronger.

A Talk objective is only met by the option that advances it, and a Deliver
objective by the option that takes the items. Opening a conversation used to
tick every Talk stage for that NPC, so greeting Oona before fetching the herb
finished the errand.

### Daily notices

Repeatable quests are posted in **pools**, one per board, and each pool puts up
**two a day**. The day turns over **at dawn**, not midnight, so a night's sleep
is what brings new notices, and the traders restock, and the HUD says "New
notices are up, and the traders have restocked." when it happens. Which two are up is decided by the pool and the day
alone, so it is the same after a reload. A daily can be done once per posted
day; the journal counts how many times.

| Pool | Notice | Task | Needs |
| --- | --- | --- | --- |
| Havenbrook | Barley Watch | 5 boar | -- |
| Havenbrook | Road Patrol | 5 orcs | Combat 6 |
| Havenbrook | Fish for the Inn | 5 raw minnows to the cook | -- |
| Havenbrook | Kindling | 10 logs to the cook | -- |
| Mossvale | Fox Patrol | 6 foxes on the Whisperwood trail | Clear the Trail |
| Mossvale | Tannery Stock | 4 hides to Hadley | -- |
| Mossvale | Oak for the Lodge | 5 oak logs to Pell | Woodcutting 12 |
| The Reverie | Nightmares Undone | 8 nightmares, asleep | The Water Remembers |
| The Reverie | Shards for the Shrine | 8 dream shards to Mira | The Water Remembers |
| The Reverie | The Brute Returns | the nightmare brute, asleep | Lights on the Pond, Combat 18 |

Dailies are never *collect* stages, which would count what is already in the
bag: they are hunts or hand-ins, so each one is work done that day. The boards
mark them "daily" with their level, and the detail pane says when the next
ones go up.

### Order books

**Smith Halda** and **Old Wendel** take orders, every day. Ask Halda "Any
orders today?" (or Wendel "Any fish wanted?") and the day's orders open on the
board panel, with what each asks for, how many you carry, what it needs and
what it pays. Accept one, bring the goods, and pick "I have an order for you."
-- that line only appears when an order of theirs can be filled from the pack,
and it hands in every order it can at once.

Orders are dailies in a pool of their own. **Halda posts three a day, Wendel
two**, new at dawn, and each can be filled once a day. A posted order is never
one the player cannot take yet: an order needing Smithing 20 is passed over for
the next in the day's order, so a new character always has at least two to
choose from, and the book fills out as the skills rise.

Halda's orders pay **Mining XP and coins**, and the smelted and smithed ones
**Smithing XP** as well:

| Kind | Order | Needs | Mining XP | Smithing XP | Coins |
| --- | --- | --- | --- | --- | --- |
| Ore | 10 copper ore | -- | 450 | -- | 143 |
| Ore | 8 iron ore | Mining 5 | 520 | -- | 187 |
| Ore | 6 coal | Mining 20 | 900 | -- | 583 |
| Ore | 4 azuryte ore | Mining 30 | 1200 | -- | 864 |
| Ingots | 6 bronze bars | -- | 600 | 360 | 216 |
| Ingots | 5 iron bars | Smithing 10, Mining 5 | 900 | 540 | 432 |
| Ingots | 3 steel bars | Smithing 20, Mining 20 | 1400 | 840 | 1304 |
| Weapons | 2 bronze swords | -- | 500 | 400 | 275 |
| Weapons | 1 iron sword | Smithing 10 | 700 | 560 | 319 |
| Weapons | 1 steel longsword | Smithing 20 | 1600 | 1280 | 2273 |
| Hide | 8 hides | -- | 250 | -- | 94 |
| Armour | 2 bronze helms | -- | 450 | 360 | 375 |
| Armour | 1 iron cuirass | Smithing 10 | 1100 | 880 | 1124 |
| Armour | 1 steel greaves | Smithing 20 | 2200 | 1760 | 3392 |

Wendel's pay **Fishing XP and coins**:

| Order | Needs | Fishing XP | Coins |
| --- | --- | --- | --- |
| 10 raw minnows | -- | 300 | 78 |
| 8 cooked minnows | -- | 200 | 108 |
| 6 raw trout | Fishing 15 | 450 | 188 |
| 5 cooked trout | Fishing 15, Cooking 15 | 400 | 252 |
| 3 raw pike | Fishing 30 | 520 | 176 |
| 3 raw salmon | Fishing 45 | 600 | 273 |
| 2 raw eels | Fishing 60 | 650 | 260 |

Filling an order always pays more than selling the same goods to any trader.
Some orders ask for things a shop stocks -- Halda sells bronze bars, and the
inn sells cooked minnows -- and for those the coins are set below the shelf
price, so buying the goods to fill the order costs more than it pays: the XP
can be bought, at a loss, but the money is in mining, smelting, smithing and
fishing it yourself. The two old dailies that were really orders, the forge's
copper and Wendel's pike, are now in these books.

### Quests in the dreamworld

- **The Water Remembers** (Mira): sleep, find the voice in the Reverie and
  listen to it, and tell Mira what it said. Magic and Hitpoints XP, coins and
  dream shards.
- **Lights on the Pond** (Hesper): hunt the nightmare brute in the Reverie and
  bring Hesper six dream shards. Attack and Defence XP, coins and a
  dreamcatcher.

The **Dreamer's Slate** stands on the Reverie's central island and carries the
reverie dailies. The hunts can only be finished asleep, before the dream ends
at dawn; the shards for Mira are gathered in the Reverie and handed in awake.

### Side quests in the new country

Fifteen of them, and **none asks anything first** -- no level, no quest before
it. Each has a level it is *advised* at, shown in the journal, and that is all:
a new character can take the crypt from Watchman Brask on their first morning,
and find out on the stairs what it means.

Given in town, from the first thing the person says:

| Quest | Who | What | Advised |
| --- | --- | --- | --- |
| The Toll at the Bridge | Hollis the Carter, Havenbrook | four highwaymen on the Westwold road, then back to Hollis | 8 |
| White Pelts | Sorrel, Havenbrook | three greatwolf pelts off the Westwold's high ground | 20 |
| Bears in the Brackenwood | Warden Sela, Mossvale | eight bears off the Brackenwood's paths | 22 |
| The Den Mother | Hale the Trapper, the Brackenwood | the Den Mother herself | 26 |
| The Singers in the Bayou | Warden Ilse, Fernhollow | four Swamp Hags | 35 |
| Shut the Crypt | Watchman Brask, Havenbrook | eight of the dead in the Vaults, six shades on the floor below | 36 |
| Horns for the Forge | Garrow the Smith, Mossvale | four demon horns from the Ashen Path | 50 |

Found lying in the world -- each drawn on the ground as its own icon, a little
off the grass with a faint light under it and a glint every few seconds:

| Thing | Where | Starts | Ends with | Advised |
| --- | --- | --- | --- | --- |
| Bloodied Collar | the Westwold, among the wolves | The Bloodied Collar | six wolves, and the collar to Farmer Aldous | 8 |
| Antler Circlet | the Brackenwood | The Antler Circlet | six wolves, and the circlet to Hale | 22 |
| Drowned Locket | the Bayou | The Drowned Locket | six of the drowned dead, and the locket to Mira | 35 |
| Bell Clapper | Hollowrest graveyard | The Tongueless Bell | down to the crypt's second floor, six dead, and the clapper to Old Perrin | 38 |
| Reed Doll | the Bayou | The Doll in the Reeds | six lizardfolk, then the Bayou Matriarch | 45 |
| Ashcroft's Letter | Hollowrest Crypt, second floor | A Letter Sealed in Black | Lord Ashcroft, and the letter to Guild Master Orlend | 60 |
| Rime Key | the Palace Dungeon | The Rime Key | the Rime Revenant, and the key to Elder Vask | 75 |
| Cinder Invitation | the Ashen Path | An Invitation in Cinders | the palace, its dining hall, and the Cinder King | 80 |

**Picking one up says so.** The thing's own line comes up in gold -- *"The
brass tag says JUNIPER, and somewhere across the Westwold a wolf howls and
another answers. Picking it up has started something."* -- held for seven
seconds and wrapped to fit beside the map on half a split screen, and the
journal's *Quest started* follows it. Whoever the thing belongs to knows it on
sight: take it to them before the work is done and they say what it is and
what to do; after, and they take it.

How it works: an item with `"starts_quest"` in `data/items.json` starts that
quest by being **in the bag**, however it got there
(`QuestLog::StartFromFinds`, called each frame by `Game::NoticeFinds`), and its
`"found"` is the line shown. The thing on the ground is a map object of type
`curio` placed by `PlaceCurios` in `tools/genmaps.cpp`, from a table of which
monster's post it lies beside -- walked out in rings to the nearest open
ground. It lies there until **you** have it: while its quest is not begun and
it is not in your bag (`World::ObjectPresent`), so it does not come back after
it has been handed over, and in co-op each player finds their own. The quest's
giver is the curio's id, which is what the self-test's "every quest has a giver
somewhere" counts.

Two smaller changes came with them. A kill stage can name a boss by its own id
(`"den_mother"`) as well as by the family it belongs to (`"bear"`): the Den
Mother counts for Hale as herself and for Sela as one of the eight bears. And a
found thing is kept in the bag only while its quest wants it -- once that is
over it is a keepsake, and can be dropped or stowed like anything else.

---

## The world

`maps/overworld.mx` is 4736 × 3968 pixels — about nine screens across and seven down — and
the camera scrolls it as a viewport on the player. Biomes: meadow, greenwood,
northern foothills, the Mire, and the Cursed Reach, joined by the Sunken Road,
with the Whisperwood trail leaving from the east.

| Map | What it is |
| --- | --- |
| `overworld` | The Hollowmarch |
| `town_havenbrook` | The village, with four enterable buildings |
| `guild_hall`, `house_elder`, `house_inn`, `house_smith` | Interiors |
| `dungeon_emberfell_1` / `_2` | The mine, upper and lower workings; the lower level is locked until you find the rusted key, and the Warchief holds the last room |
| `dungeon_barrow` | Beneath the Mire |
| `whisperwood_trail` | The forest path east of the Hollowmarch: a woodcutter's camp, a stream with a plank bridge, and a fork |
| `mossvale` | A logging village behind a palisade at the trail's east end |
| `mossvale_lodge_hall`, `mossvale_herbalist` | The reeve's lodge and Oona the herbalist's cottage |
| `mossvale_cottage` | The tanner's empty house at the bottom of the village -- yours, once you find the key |
| `fernhollow` | A hamlet on a pond at the north fork, with a shrine and a ferry cottage |
| `fernhollow_cottage` | The ferryman's widow's cottage |
| `college_grounds` | The College at Fernhollow: through the gatehouse on the hamlet's north side, a great court with the hall across the north of it |
| `fernhollow_college`, `college_training`, `college_classroom` | The college's three chambers: the great hall where the council sits (north), the practice hall (west), the lecture room (east) |
| `mossvale_weavers` | Wynn's, up the north-west lane at Mossvale: a draper's, with her loom in it |
| `dreamworld` | The Reverie, reached only by sleeping: five cloud islands over the void |
| `house_inn_cellar` | Under the Barley and Bell, down a hatch behind the bar: rats, spiders and a broodmother |
| `ice_spire_peak` | North off the foothills, Combat 30: a climb through trolls to the wyverns' summit |
| `ashen_path` | East off the Hollowmarch below the Cursed Reach, Combat 40: a burnt road across rivers of lava, and north of it the Brimstone Palace behind its moat |
| `palace_foyer` | The Brimstone Palace's hall, Combat 75: the runner, the balconies and the walkways over it |
| `palace_ballroom`, `palace_dining`, `palace_chambers`, `palace_dungeon` | The palace's rooms: three off the balconies, and the dungeon down a stair |
| `palace_throne` | The Cinder King's throne room, at the head of the runner |
| `dungeon_infernal` | The Infernal Pit, through the hellgate at the Ashen Path's end: imps, demons and the Pit Lord |
| `westwold` | The Westwold, out of Havenbrook's west gate, Combat 5: open downs, Hidewater steading, the river Wend, wolves, and the Howling Fells in the west |
| `brackenwood` | The Brackenwood, north off the Westwold's fork, Combat 20: old forest, bears, the Den Mother, and the Old Growth |

### The College at Fernhollow

It was a tower in the south-east corner of the hamlet with one room in it. It is
a place of its own now, north of the water, and it is meant to be the grandest
thing in the Hollowmarch: everything else is brown timber, and this is pale
stone under blue slate with gold on it.

**The hamlet has the gatehouse** -- two round towers under blue spires and an
arch between them, at the head of a paved walk up from the jetty road, with a
porter, the college's colours and a pair of lamps. A way into the college is a
door and not a road out of the hamlet, so it is walked up to and gone through
like one (and the rule that every road out of a town is a gate with a warden
is not asked about it).

**Through it is the great court** (`college_grounds`, 60 by 46 cells -- bigger
than Fernhollow itself): an avenue of blue-lozenged flagstones from the gate to
the hall, a cross-walk from the west door to the east, a three-tiered fountain
where they meet, four lawns edged in box with **a founder in stone on each**,
benches, the colours up both sides of the avenue, a colonnade down each side
wall, and lamp standards that are lit after dark. Across the north side, its
roofs against the wall, is **the hall with a wing either side of it**: six
columns under a pediment with the college's star in it, tall lit windows, a
round tower at each end -- one front twenty-five cells wide. The first court
had the hall alone, which was a fifth of the width of the place it was meant to
preside over.

Off the court, a chamber in each of the three walls, and each a different kind
of room:

| Door | Map | What it is |
| --- | --- | --- |
| West | `college_training` | **The practice hall.** Four lanes with a straw man at the end of each, and an apprentice or an adept at the head of each throwing what they are learning at him -- fire, the old bolt, water, air. Crystals in the corners, the staves in a rack, a duelling ring, Battlemaster Ysolde watching. |
| East | `college_classroom` | **The lecture room.** A board across the back wall chalked with a working, the lectern, an orrery, shelves, ten desks either side of a blue runner with a class at half of them, and Lector Maud on the four elements and what each one fears. |
| North | `fernhollow_college` | **The great hall, where the council sits.** The council's table under a blue cloth with six high chairs turned to it, and the council *sitting* in the three behind it -- Magister Orrin in the middle, Councillors Ferris and Wren either side, facing the table and the room across it. (They stood north of their chairs at first, and all that showed of a councillor was the top of a head over the back of an empty chair. Each is a few pixels south of the chair now, so they are drawn over its tall back and under the table, which hides them from the chest down: somebody sitting at a table, with no sitting sprite.) Behind them the library along the back wall, a founder either side -- and south of the table the circle cut in the floor, which is older than all of it. The hall keeps the id it always had, so everything that knew the way to the Magister still does. |

The side doors are doors in side walls: a gap in the wall with the runner laid
through it and a pair of columns either side, walked into sideways. The rooms
behind them have their own door in the *opposite* side wall, so going west out
of the court brings you in at the east end of the practice hall.

**The mages really do practise.** An NPC in a map can be given
`"casts": {"bolt": ..., "at": [x, y], "every": seconds}`: they turn to the
point, play the cast -- the rig's attack clip with nothing in the hand, rendered
for the `magister` and for two new looks, `apprentice` and `adept` -- and let
go one of `data/projectiles.json`'s bolts at it. It is a **practice bolt**
(`Projectile::show`): it flies exactly as far as the dummy and bursts there,
and on the way it touches nobody -- not a monster, not a player stood in the
lane -- and a fire bolt leaves nothing burning. The straw men are scenery, not
monsters, so nothing in the room can be hit for experience either; the
instructor says so, and says why ([a wall teaches nothing](#a-wall-teaches-nothing)).

**Its own tileset**, from `tools/make_ground.ps1`: `college_paving` and
`college_inlay` for the court, `college_floor` (chequer marble) and
`college_wall` (ashlar with a band of blue and gold) for the chambers,
`college_walltop` and `college_wallface` for walls seen from above and from the
front, `college_carpet` for the runners. They are appended at the very end of
the generator on purpose: it is one run of one random sequence, so anything
added earlier would re-roll every tile after it. Two things learned laying
them: a carpet laid *over* a floor is under it, because the ground is drawn a
tile name at a time in alphabetical order (`CollegeRoom` takes its runners as a
predicate and lays them *instead*); and a banded wall tile run up a side wall
is a ladder, so only the back wall wears the band.

### Waystones

There are three **waystones**, one in each town -- Havenbrook's by the
crossroads, Mossvale's in the square, Fernhollow's on the green -- and none
anywhere else. Not in the wilds, not at a dungeon's door, not in the Reverie.

A stone is **asleep until somebody puts a hand on it**. The first touch wakes
it, and that is all the first touch does: the eye in its face lights, the
runes down its courses with it, and it says what it is for. A woken stone,
touched again, opens a panel of all three; any other *woken* one can be chosen,
and you come out standing beside it. One that is still asleep is listed, dark,
and refuses: it has to be walked to and woken by hand. There is no fare. The
price of a waystone is having got there.

So the road to a town is walked once, and everything that is not a town is
always walked. The long errands in this game are town to town (an order for
Wynn, a notice from Havenbrook's board, a bar Halda wants), and the walks that
are *the game* -- out to the Mire, up to the Spire, down a mine -- are
untouched.

How: a `waystone` map object whose id is its flag. `World::TryInteract` sets
the flag the first time and raises `WorldRequest::Type::Travel` after; an
object whose id is flagged is drawn as its `sprite_open`, which is how a chest
stays open, so the lit stone cost no new drawing code. The panel is
`Game::UpdateTravel` / `DrawTravel`, and going is an ordinary
`RequestTransition` to the far map's `waystone` spawn -- so a guest in co-op is
told the host leads the way, as at any other door. Woken stones are world flags
and are saved with the rest. The stone is `_waystone(lit)` in
`tools/blender_props.py`, rendered twice; `PlaceWaystone` in `tools/genmaps.cpp`
stands one up. `--screen travel` opens the panel for a screenshot.

### The Westwold and the Brackenwood

Havenbrook's cross street always ran west into the fence and stopped. It runs
out of a **west gate** now, onto the largest two maps in the game after the
Hollowmarch itself.

**The Westwold** (4800 x 3328) is open downs. Just outside the gate is
**Hidewater steading** -- drying frames with hides laced into them, Orla the
Tanner, who pays more for a hide than anyone and sells the first two hide sets,
and Isolde the Weaver at her wheel, who sells flax, cloth and the first two
robes; a workbench, a dye vat and a fire, so both new kinds of armour can be
made where their makings are sold. Past it the road is a cart track between
ploughed fields with **flax** along their headlands and Farmer Aldous walking
between them, down to the river **Wend** and its plank bridge, where the
highwaymen wait. East of the river it is a walk in the fields: hares, deer,
foxes, boar. **West of it the wolves run in twos and threes.** Then a fork:
north to the Brackenwood, and west up onto the **Howling Fells**, where the
ground turns to rock and then to snow and the wolves are **greatwolves**, the
size of a pony, Combat 55 and not alone. A sign says so. Seven standing stones
south of the road have a chest among them.

**The Brackenwood** (4160 x 3520) is old forest with a trail through it, the
way the Whisperwood is, and **bears**. Wolves on the way in; Hale the Trapper's
camp at the second bend, with a fire, a bed for the night and a fair price for
hides; bears along the inner trails and in the glades the side trails end in;
and in the middle of it **the den** -- a mouth of dark under fallen slabs,
claw-raked trunks either side -- where the **Den Mother** and her two grown cubs
are. She is a leader: she rears up, and all of it comes down at once. North of
her the trail climbs into **the Old Growth**, where the **dire bears** are,
twice the size with a hide that turns a spear, Combat 65.

Between them the two maps give a hide for four of the twelve tiers, which is
why they go where they go: wolf for bronze, bear for steel, greatwolf for
diamond, dire bear for platinum.

### The Whisperwood

A dirt trail leaves the overworld's east edge ("To the Whisperwood") and
winds through a forest dense enough that the path is the way through. Bram the
woodcutter camps where it crosses a stream; past him it forks, east to Mossvale
and north to Fernhollow. The trail is a map of its own rather than more
overworld because that is what makes it read as *a journey to somewhere* -- you
leave one place, travel, and arrive in another, the way DragonFable and
AdventureQuest Worlds stitch their zones together.

Mossvale and Fernhollow each have their own people, dialogue and quests: a
five-quest woodland chain (clear the trail, carry word to Fernhollow, hides for
Mossvale, the trail wardens, and an offering at the shrine), with kill stages
tied to the map they belong on so a wolf in the Mire does not count toward the
Whisperwood.

### People with somewhere to be

Havenbrook's streets had people standing in them. Eight of them walk now:
Wenna fetches water up from the mill pond, the well being what it is; Old
Perrin does the rounds of the stall, the board and the fire; Watchman Brask
walks the streets gate to gate; Tam carries logs from the sawpit to the forge;
Dace fishes off the end of the jetty; Pip runs the guild's notices; Hollis the
Carter comes in at the south gate and goes out at the west; and Sorrel, a
ranger in off the downs, sells her pelts to Ivo and leaves again. Each has a
line or two to say, and Aldous does the same between his fields.

**Where someone is on their round is worked out from the world's clock and
nothing else** (`Npc::PlaceAt`): a round is a list of stops with a wait at
each, laid out in time, and the clock says how far into it they are. No state
is carried from frame to frame, so every machine in a co-op game puts the same
villager in the same place without a word being said about it -- and the host,
asked whether a friend could really have spoken to them, looks in the right
spot. A round may begin only inside the villager's hours and always ends where
it began, at a door or a gate, so **at night everyone but the watch goes in**
and nobody appears or vanishes in the middle of the road. Spoken to, they stop;
let go, they hurry along their round until they are back where the clock has
them, rather than being put there. The generator keeps the village's greenery
off every round, and the self-test walks each one a quarter of a second at a
time and fails if any step of it is inside a wall -- which is how Old Perrin
was found walking through Tobin's stall.

A round is data on the NPC in the map: `"path": [[x, y, seconds, facing], ...]`,
`"ping_pong"`, `"speed"`, `"phase"`, `"hours": [from, to]` and a `"tint"`, since
there are only so many villagers' faces.

### Havenbrook's gate

The Sunken Road used to stop in the middle of a field, and the way into the
town was a rectangle of grass at the end of it. It ends at a gate now: two
stacked-log towers either side of the road with a lintel and the town's board
across them, lamps on the inner faces, the leaves of the gate swung back
against the towers, and a run of palisade either side that gives out after a
few lengths the way a village's does. The road runs through it (`prop_town_gate`
in `tools/blender_props.py`), and the portal sits in the opening, so you walk
through a gate rather than onto a patch of grass.

The waymarker that used to stand there now stands on the verge a little north
of it, saying which way is which.

### If it is the way into a town, it is a gate

That gate was the only one. Inside Havenbrook the same road left through a gap
in a fence nobody could see, with Watchman Corrin stood four cells short of it
in the middle of the road; Mossvale's warden and -- once there was one --
Fernhollow's were the same: somebody standing in a field beside a rectangle.
Every road out of a town now goes through a gate, and the warden is at it.

| Town | Way out | Gate | Kept by |
| --- | --- | --- | --- |
| Havenbrook | south, to the Hollowmarch | gatehouse | Watchman Corrin |
| Havenbrook | west, to the Westwold | two towers | Watchman Edda |
| Mossvale | west, to the Whisperwood | two towers | Warden Sela |
| Fernhollow | south, to the Whisperwood | gatehouse | Warden Ilse |

There are two kinds because there are two ways a road can meet a wall. One that
runs north or south goes through a gate seen from the front, which is the
gatehouse Havenbrook already had on its Hollowmarch side: one picture, the road
under its lintel (`PlaceFrontGate` in `tools/genmaps.cpp`). One that runs east
or west goes through a gate seen from the side, and that cannot be one picture
-- whoever is on the road is in front of the tower north of it and behind the
tower south of it, and a picture is sorted once -- so it is one tower
(`prop_gate_tower`, the gatehouse's tower log for log, with its lamp and its
leaf swung back), stood twice (`PlaceSideGate`). The fence is a fence now too:
a palisade round Havenbrook and Mossvale, along the open south side of
Fernhollow, and down the east and west sides a palisade seen along its length
(`prop_palisade_side`).

Inside a town the gatehouse stands two cells in from the edge of the map and
not on it. From the town side everything just north of a gatehouse is behind
it, and a warden posted in the gateway showed as a pair of boots under the
lintel. Set in, there is ground south of it -- outside it, and in front of it
-- and that is where Corrin and Ilse stand, at the foot of the lamp-side tower,
where they can be seen and spoken to from the gateway without anyone having to
leave to do it. Whoever arrives is put down in the road just inside, between
the towers' roofs. The palisade starts hard against the towers; on the
Hollowmarch it used to start a stride clear of them, with an invisible wall in
the gap.

Mossvale's gate is as wide as its street: the gap in the fence there was five
cells for a road of three. Fernhollow's path used to arrive a cell and a half
to one side of where the gate was going to be, so it straightens for its last
few rows. Watchman Brask, who walks Havenbrook gate to gate, turns round in the
road short of the gatehouse.

The self-test has a list of every road out of a town, and fails on a road that
is not on it: a new way out has to come with a gate. For each it checks the
gate is there (a gatehouse across the road, or a tower either side of it), that
a warden or a watchman with no round to walk stands within a few strides and
not inside a tower, that the middle of the road is open all the way through,
and that the gateway is never narrower than two people.

**The manifest.** `genmaps` places a prop at the size `data/asset_manifest.json`
says its picture is, and takes 32x32 for one the manifest has never heard of.
The towers first came out the size of a fence post, and so -- it turned out --
had everything new in the Westwold since it was built: the bear's den, the
tanning racks, the hay ricks and the rail fences were all placed at 32x32.
After rendering a new prop, run `tools/make_manifest.ps1` before `build.ps1
-Maps`.

### The farm at Havenbrook

Havenbrook is sixteen columns wider than it was. Everything in it is placed from
the west wall or from the crossroads and the fence, the gates and the south road
are drawn from the town's width and height, so the town simply has a field on
the end of it: past the mill pond, a yard of beaten earth with a farmhouse, a
barn, hay and **four fenced pens**.

| Pen | What is in it | What it leaves |
| --- | --- | --- |
| The hen run | Hens | Raw chicken, and eggs |
| The sty | Farm pigs | Raw pork, sometimes a hide |
| The fold | Ewes | Raw mutton **and a fleece** |
| The paddock | Dairy cows | Raw beef and a hide, sometimes a pail of milk |

None of them fights. Their aggro range is zero -- a hen is a hen -- so they are
killed on purpose or not at all, and each is worth a supper. The fleece is what
ties the farm to the loom at Mossvale, and the eggs and milk are what the
dishes are made of. **Farmer Marrow** stands in the yard and will say which pen
is which.

And the mire has **frogs** in it now, sitting by the water among the lizardmen,
as passive as anything on the farm. Frog legs fry into one of the better dishes,
which is more than the bog's reputation would suggest.

The five of them -- cow, sheep, pig, hen, frog -- are built on the same rig the
boar and the deer are, in `tools/blender_creatures.py`: a cow is a barrel on
short legs with the patches doing the work at thirty pixels, a sheep is a cloud
with a dark face, a hen is two legs and an opinion, and a frog is a wide mouth
with its back legs folded beside it.

### The pond at Fernhollow, and the one thing that swims

Six mallards and three geese live on the water at Fernhollow. They are posted
on the bank, not on the pond, because getting in is something they decide to
do: every few seconds a bird picks somewhere to be -- a patch of grass, or a
bit of open water -- waddles there in a straight line, and pokes about until it
thinks of somewhere else. A bird on the water usually comes out; a bird on the
bank is as likely to go in. Left alone for five minutes, **about half the flock
is afloat at any moment** and all nine of them are seen both wet and dry.

Neither of them starts anything -- their aggro is zero, like the farm's -- but
either will come out of the pond after somebody who takes a swing at it.

Water is the new idea. Until now every pond in the game was collision, the same
as a wall, and it still is for everything that walks:

- a map may mark some of its collision as **water** (`m.Water()` in genmaps,
  a `"water"` array in the `.mx`), which is a wall to everything as before;
- `Map::Blocked(box, swims)` asks the same question with the water left out,
  and `Map::InWater(x, y)` says whether a point is over it;
- an enemy with `"swims": true` moves with `swims` set, so the pond is the one
  obstacle that is not there for it.

Fernhollow's pond is the only water in the world marked this way, and the drake
and the goose are the only things in the game with `swims` set, so nothing can
suddenly cross the sea at the edge of the overworld. Both of those are held by
the self-test.

Sitting on the water is drawn rather than faked: the two rigs have a **swim
clip** -- legs folded up out of sight, body dropped until the belly is the
waterline, neck up -- which the engine plays whenever the bird is over water
and the rig has one. Anything else asked for it quietly keeps walking, which is
how `run` has always worked. The clip goes over the wire by index like any
other, so a friend watching from the far bank sees the same birds swimming.

### A house of your own

The tanner's house at the bottom of Mossvale has stood empty since he went to
the coast. Bess at the Barley and Bell will tell you so, and where he kept the
key: under a loose stone at the gable end, because he was not a man who changed
his habits. The door is locked (`locked_by` on the portal) until you have it.

Inside is a hearth, a bed, a workbench, and the only container in the world that
keeps what is put in it: a **storage chest of a hundred slots, ten by ten**. It
is the answer to having nowhere to put anything down -- the bag is
twenty-eight slots and everything else in the world is either a shop or a chest
you loot once.

A storage chest is a map object of type `storage` with a `capacity`, and what is
in it lives in the save beside the world flags rather than in the map: the maps
are regenerated from `tools/genmaps.cpp` whenever anything changes, and a chest
whose contents lived in the map would be emptied every time. So the chest
belongs to the character, and a second one placed anywhere else would be a
second chest with its own contents.

The panel is two grids side by side -- the bag and the chest, the same squares
as the inventory screen -- and the button moves a stack from whichever side the
cursor is on to the other, one at a time or the whole stack with sprint held,
which is the hand the shop screen already uses. Nothing is ever destroyed: a
move that will not fit moves what fits and says so.

### Making zones feel like places

- **Arrival banners.** Entering an outdoor zone or dungeon fades in its name
  and a one-line subtitle over the screen for a few seconds. Walking in and out
  of a house does not re-announce the town you were already in.
- **Ambience.** Each map declares an `ambient` kind and `src/world/ambience.cpp`
  drifts particles through the world to match: falling leaves and fireflies
  under a darkened vignette in the forest, fewer of both in the village groves,
  pollen over the fields, dust in the mines. Particles live in world space and
  respawn on the far edge of the view, so they scroll with the ground rather
  than sliding across the screen.
- **Exit markers.** Near the edge of an outdoor map, each way out is labelled
  with an arrow and its destination -- "To the Whisperwood >" -- so the edges
  of a zone are signposted rather than discovered by walking into them.
- **Warnings at dangerous doors.** A portal can carry the Combat level its
  far side is meant for. Below it, the door prompt says so -- "Enter the
  Emberfell mine - dangerous: Combat 6 advised" -- where a new character used
  to find out by dying in the first room.
- **Legible banners.** The zone name sits on a feathered dark band; gold text
  over the foothills' sand was close to unreadable.
- **Clear streets.** Trees and tall fungus are drawn up from their base, so the
  generator keeps them two tiles back from every road; bushes may still line it.
- **A mine that is a hole in a hill.** The Emberfell mine used to be a door
  sprite standing on open dirt. It is a hillside now (`prop_mine_adit`): terraces
  of broken rock with scrub on the ledges, a timber-framed tunnel mouth with a
  lantern on the lintel, rails running out of the dark to where the Sunken Road
  now ends, an ore cart and a spoil heap, and loose rock either side where it
  runs back into the foothills. The portal is the dark of the tunnel itself.
  A piece of scenery can now sort against people above its base
  (`"sort_lift"` in a map): the hill sorts at the back of the tunnel, so someone
  standing in the mouth is drawn in front of the rock around them rather than
  ghosting the whole hillside out.
- **A barrow that is a grave.** The barrow in the Mire was a door sprite on the
  grass beside a mushroom. It is a long turf mound now (`prop_barrow_mound`),
  its passage framed by two standing stones and a capstone with a facade of
  slabs curving out either side, flagstones up to the door, a faint green light
  far down the passage and a skull on a stake. The portal is in the doorway.
- **Stairs, not doors, underground.** Inside every dungeon the way out was a
  door standing in the middle of the first room and the way down another in the
  last. The way out is a stone flight now, climbing into the first room's top
  wall toward daylight (`prop_dungeon_stairs_up`, set against solid rock so it
  never blocks a corridor), and the way down a stairwell in the floor with a
  kerb, a parapet and a torch (`prop_dungeon_stairs_down`).
- **Things lying on the ground.** The overworld was scattered with round blobs,
  squares with holes in them, chevrons and keyholes, each a flat colour. They
  were cut from the CraftPix road pack's `Ground_grass` sheet by the importer,
  and they are not decorations: they are that sheet's stencils, the masks its
  autotiles use to blend grass into a path. `tools/make_decals.ps1` draws what
  a field actually has in it instead, on transparent ground -- clumps of meadow
  and shade grass, wildflowers, fallen leaves, pebbles, straw-coloured hill grass
  and cracked stones, scorch cracks on the Cursed Reach, and sedge and puddles
  in the Mire -- and genmaps lays each only on its own ground.
- **Room to the west.** The Mire ran into the edge of the world. The Hollowmarch
  has grown twenty cells west and twenty-eight south, out of the same noise, so the
  swamp, its bog pools and lizardmen, the river and the foothills carry on
  instead of stopping. The north and east edges, where the ways out are, did not
  move. So nothing already built had to be renumbered, the old cells kept their
  coordinates and the new ones are negative: `MapBuilder::ox` adds an offset to
  every x a map places. Saves are version 2; a position saved on the overworld
  by an older build is moved the same 640 pixels east when it is loaded, so a
  character stands on the same ground they saved on.

Maps are big enough to grow: the base layer is bucketed into chunks and culled
against the camera, so adding another biome costs load time and nothing else.

### The Mire

The swamp in the west of the Hollowmarch was laid from three tiles cut out of
the cursed-land pack, and one of them, `marsh_dark`, turned out to be a patch of
black cliff face: a third of the Mire was a streaked black void with rust-coloured
dirt decals on it. It is generated ground now (`tools/make_ground.ps1`) -- sedge,
peat and mud -- with **pools of bog water** that cannot be walked through, reeds
and bulrushes round their edges, lily pads on them, and drowned trees hung with
moss. The dirt decals are no longer laid on it. The pools keep clear of the
barrow, the chest, the bogbean patch and the camp.

### Hollowrest

South of Havenbrook, out where the meadow runs into the Mire, the burying
ground: **its own biome**, dead grass and turned earth inside an iron fence,
thirty-six cells across and twenty-two deep. A lych gate at the north with a
lantern somebody still lights, a second gap in the east wall where the railing
has come down, two aisles crossing the field, a hundred-odd headstones and
leaning crosses with grass between them, dug graves with the spade still in
them, drowned trees left where they stood, and the crypt at the south end with
the family's wight in front of its door. There is a chest in the yard for
anyone who walks the whole of it.

The dead are laid out by where they lie rather than in a knot: **skeletons** out
along the fence, **shamblers** among the newer graves in the east, and
**wraiths** in the old sunken western half. They stand three cells apart, on a
lattice, and notice you late, which is the point -- the first version of the
yard was a plot the size of a room, and opening the gate woke every grave in it
at once. A graveyard should be a place you walk into, not a fight you fall into.

| What it is | What it leaves |
| --- | --- |
| **Shambler** (zombie) | its own rotten flesh, and whatever money was in its pockets |
| **Skeleton** | bones, and nothing else: there is nothing else left of it |
| **Wraith** | what it was buried in -- grave candles, tarnished rings, mourning lockets: oddments for a trader |
| **The Hollowrest Wight** | lockets, rings, candles by the handful, coins, and a piece of good steel |

Each of the three is its own creature in `tools/blender_creatures.py`, built to
be told apart across a dark field: the zombie thick and stooped with its arms
out and its jaw hanging, the skeleton thin and bright with a rusted sword and a
broken buckler, and the wraith a hooded robe with two lights in it and a wisp
where its feet should be. The yard's own props -- headstone, cross, dug grave,
railing, lych gate and crypt -- are in `tools/blender_props.py`, and the ground
is two new tiles from `tools/make_ground.ps1`.

### The well of Havenbrook

There is a well on the corner of the square, paved round, with a board nailed
over the mouth and the bucket left on the rim. It has been dry eleven years.
Ask **Innkeeper Bess** about it and she puts the cloth down: four hundred
buckets a day it gave, and a queue from dawn, until the year everything else
went wrong; the town has carted water from the brook ever since, and nobody has
been down it because nobody was mad enough.

Say you will go and she writes out what the old well-crews carried: an **unlit
lantern**, two bars and a length of wood, beaten at any anvil. The recipe is
`needs_recipe`, so it is not in the smithing list until she gives it -- the same
lock brews have always had, now on anything taught rather than worked out.
Carry the lantern and **use a tinderbox** from the pack and it becomes a lit
one, worn in the **off hand**, in the shield slot.

It matters because the well is **dark**. A map can now carry `"dark": true`,
which means it has no light of its own: ambient drops to almost nothing and the
only thing lighting the room is what the player is carrying. An item can carry
`light`, a radius in world pixels, and `Equipment::LightRadius()` returns the
best one worn -- so the lit lantern throws a warm, faintly guttering pool about
seven tiles across, and a player who climbs down without one gets an arm's
length of grey and no more.

Two floors, both big, both laid out the same way: a hub at the stairs, four
chambers around it at the corners, an L of corridor to each. **One kind of thing
to a chamber**, so a fight is with the slimes or with the bats and never with
both, and they stand three cells apart on a lattice with short leashes -- a room
is crossed a fight at a time rather than in one running battle.

| Floor | What holds it |
| --- | --- |
| **The Upper Workings** (92 x 72 cells) | slimes, cellar rats, well bats at 8-12; copper in the walls, two chests |
| **The Deep Cut** (96 x 78 cells) | pit hounds at 20, ankous at 27, banshees at 30; coal, standing water, and the spring at the end of a long passage south |

Bess will not hand it over below **Combat 20**, and the journal suggests 30:
the top of the shaft is a beginner's fight but the bottom of it is not.

At the bottom of that passage is the **spring itself**, in a room of its own with
a basin, a plug of fallen stone in the outflow, and the thing that has been
sitting in it. Clear it, pull the stone, and the water goes. Tell Bess and she
sends the boy to the square with a bucket before you have finished talking.

Three things the playtest changed. The warden first stood on the basin with the
basin's own collision between it and the player: a boss fight fought through a
fence, neither able to reach the other. The basin is a low kerb now and the
warden waits beside it, with floor to circle. It also sat four tiles from the
stairs, so arriving on the floor *was* the fight; it is at the far end of a
passage now, with a pair of hounds kennelled along the way. And the cave growth
used the full mushroom set, including the 128px ones -- tree-sized, and a player
standing behind one in a map where the player is the light source simply
disappears. Only the small fungus grows down there.

### The lizardmen

The first ones were built at a townsfolk's proportions and read as something a
hero could step over. They are **heavy reptilian warriors** now, half again the
hero's height and twice the width: shoulders wider than their hips, a slab of a
chest with belly plates down it, a jawed head carried on a thick neck under a
crest of five backswept spines, a yellow throat frill, a pelt over one shoulder,
a loincloth and belt, three-clawed hands and feet, a plated tail, and a
bone-headed spear bound with cord and hung with a red rag that they carry
planted upright until they lunge with it. The build is in
`tools/blender_creatures.py` (`build_lizardman`), rendered at 80px a frame like
the trolls, with hit boxes to match.

Two things learned making them: a creature modelled at the size the numbers
suggest comes out *smaller* than the hero, because a frame spans several world
units -- the rig is built at arm's length and scaled at the end. And a forward
lean that looks like a warrior's hunch in the file reads, from this camera, as a
crocodile crouching; the head has to come up and clear the shoulders before the
thing stands like a fighter.

The Mire belongs to the **lizardmen**. They are scattered through it, and in its
south their camp stands round a fire: three huts up on stilts, painted totems,
and their chief.

### The Bayou

West of the lizardmen's camp a causeway of packed earth runs off the edge of
the Hollowmarch, and past it is the swamp they came out of: **the Bayou**, the
widest map in the game (226 cells by 88), advised at **Combat 30** at the way in
and **56** at the far end of it. A signpost at the edge says, in lizardman red
and a scratched hand under it, not to walk the edge.

It was **laid out from a guide the user drew in LevelEdit-Plus** --
`exports/Bayou/Bayou.mx` in that tree -- in which none of the art is used; it
says where things go. `tools/bayou_guide.py` turns it into the tables at the
head of `BuildBayou` in `tools/genmaps.cpp`, moved into the game's frame, and
nothing else reads the guide:

| The guide's | What it became |
| --- | --- |
| bushes | the map's four corners |
| dark ground | the track: in from the east, round a loop, a road west and a spur north |
| rings of puddles | the shores of eight bodies of water, filled inside each ring |
| pillars | the corners of four decks raised on stilts over two lakes |
| rising posts | the ramps up to the northern decks |
| medium ground | palisades of sharpened stakes round two camps, with a gate wherever it left a gap |
| flowers | herbs: bogbean by the water, glowcap where it is dry |
| dark grass | the thicket down the east side, dull olive, full of spiders |
| enemy icons | posts -- all ninety of them, each given what the place asked for |

**What lives in the water does not show itself.** The Drowned, the Fen Gators
and the Bog Lurkers wait *under* it: not drawn, not targetable, not struck by
anything -- a pair of rings on the surface and a bubble now and then is all there
is of one. Walk within **104px** of it and it comes up, in a splash, over six
tenths of a second in which nothing can touch it, and then it is at you. Back
off out of sight and it lets you go and sinks again after a few seconds at home,
whole; strike it and run and it follows like anything else, gives up, and sinks
whole all the same. A post is marked `"lurk"` in the map (`EnemySpawnDef::lurk`,
`Enemy::Hidden`), and how far out of the water it is travels to a friend's
machine as its **alpha** -- which is what alpha already meant for a body
fading -- so co-op needed nothing new on the wire.

Two rules the generator keeps about where they wait. **At the edge:** one drawn
in the middle of a lake could never be woken by somebody who cannot walk on
water, so every lurker is moved to the nearest water a cell or two out from a
bank, a ramp or a deck. **Not under a deck's lifted edge:** a deck is raised
four levels, nearly two cells, so it lies over the water up to two rows north of
it, and anything there draws over the boards. The self-test holds both, for
every lurking post in the world.

Lurkers swim to hunt, not to potter: `EnemyDef::paddles` (default: whatever
`swims` is) is what the ducks do, and the Bayou's swimmers turn it off and wait
still at their post.

**The stilt villages** stand on **real elevation**: each deck is four levels up,
so its edge is a drop into the lake and the only way on is its ramp, which steps
up one level at a time. Everything raised in the Bayou is decking, so its
elevation block names **a face of dark planks and a wooden lip** instead of a
bank of soil with grass over it (`"face"` and the new `"lip"` in the map's
elevation block). Pilings stand under each deck's front edge -- the only edge
of a deck on stilts anybody sees from here; the ones behind are under the
boards. The huts are **`bayou_hut`**, the lizardmen's hut with its own legs and
ladder taken away, because a hut on stilts stood on a deck on stilts is a hut
on stilts on stilts. The northern village has two decks with a ramp each, as the
guide drew them; the southern one's far deck is reached only across a raised
bridge from the near one, and on it, in front of the great hut, waits **the
Mother of the Fen**.

| Where | What waits there |
| --- | --- |
| the crescent lake by the way in | Bog Lurkers under the water, Mire Croakers on the bank |
| round the great camp | Rot Shamblers and Bog Lurkers, by the day |
| the two camps | Swamp Hags and Lizard Shamans behind the stakes, lizardmen on the gate |
| the east thicket | Fen Stalkers |
| the south-east lake | Fen Gators under it, croakers and a stalker on its banks |
| the witch ring, at the loop's south-west corner | Witchlights, and the dead they keep dancing |
| the ponds by the west road | the Drowned, a gator, a lurker |
| the northern village | Lizard Shamans on the decks, hags on the shore, the Drowned under it, witchlights over its water |
| the southern village | shamans on the near deck, gators and the Drowned all round, and **the Mother of the Fen** (56) |

Seven chests: one in each camp, one on each village's near deck, the Mother's
hoard beside her hut, and one at the end of each track that runs off into the
reeds.

### Hollowrest Crypt

The mausoleum at the head of Hollowrest was always **barred** -- the four
"columns" on its front were an iron grille. It is open now: the art is
`crypt_open`, with the bars gone, the doorway cut dark and the grille left
leaning against the wall beside it, because somebody took it off. The sign at
the gate always said to shut the gate.

**Three floors, and no keys**: the way down is fighting.

| Floor | Its dead | Combat |
| --- | --- | --- |
| **The Vaults** | Grave Ghouls, Bone Archers, Cryptbound | 26-34 |
| **The Ossuary** | Bone Knights, Plague Corpses, Tomb Shades, Grave Hounds | 39-47 |
| **The Black Vault** | Blood Thralls, Bone Colossi, Nosferatu, a Crypt Warden -- and **Lord Ashcroft** | 52-71 |

Each floor's chests are better than the floor above's, and behind Lord Ashcroft
is his own: **Ashcroft's Signet**, a ring that gives back 8% of what a blow takes.
That needed `ItemDef::leech` -- worn gear can leech now, folded into the leech
the Vampiric Touch already had -- because otherwise the ring would have been a
stat block with a vampire's name on it.

(A blender note: `blk()` stacks solids and does not cut holes. The first open
doorway was set back into the building and the wall in front of it simply hid
it; a doorway has to stand proud of the face to be seen.)

### The Brimstone Palace

North of the Ashen Path's burnt road, where two of its three rivers of fire
come from, stands **the Brimstone Palace**. It was laid out from two sketches of
the user's -- its front, and its hall -- and the Ashen Path was grown forty-four
rows north to hold it (everything the path had is that much further down the
map than it was, and none of it is any harder).

**Outside.** A dirt road leaves the burnt one and runs straight north to a
**drawbridge** over the front of the palace's **moat**: a U of lava round its
forecourt whose two arms are the path's middle and eastern rivers, straight
along the forecourt's sides and only beginning to wander once they are south of
it. The forecourt stands on a **platform three levels up** -- 42 pixels, over
the 36 the user asked for, so the palace has some depth to it -- with a face of
the palace's dark stone and a gold lip down into the moat, and a stair up its
front from the bridge, the only way on. Everything in the forecourt stands up
there with it: the palace's front and its towers, braziers at the corners, a
demon in stone either side of the stair's head, and two Abyssal Demons and two
Revenants on guard. Up a flight of grey steps,
between two torches, is the door -- crimson outside, gold inside, a horned skull
over it -- with a round tower at each corner standing in the head of the moat.
On the road up to it and all round it, what lives on the Ashen Path lives here
too, grown bigger for living this close (imps and demons at 49-71, none of them
nearer the burnt road than nine rows); and down the palace's east side lies a
field of embers, where the ground has opened in a hundred places. The two
streams out of the moat are **bridged twice each** on their way south -- basalt
slabs with a parapet, seen from above like the drawbridge -- because the first
build walled the road off from the land either side of it, and on those far
banks are platinum and demonite in the rock and emberbloom in the ash. A post where
the road leaves the burnt one says whose road it is. The door is warned about
at Combat 75, and shut below 60.

**The hall**, `palace_foyer`, is the sketch: a crimson runner with a gold border
from the doors to the throne room's; **balconies** down both sides on real
height -- three levels up, with a narrower runner of their own, a rail along
their edges and stairs down at their foot; and **three walkways crossing
overhead** from one balcony to the other. The walkways are lengths of bridge
laid end to end on the layer drawn over everyone and lifted to the balconies'
height, so whoever walks up the runner passes under them, and the floor under
each is in its shadow. They cannot be walked on: a map has one height to a
cell, and a bridge over a floor somebody walks on would be two. Columns stand
along the balconies' edges, braziers down the runner, and the palace's sigil is
woven into the runner twice.

Off the balconies, through doors in the side walls, are the rooms; a stair in
the floor goes down, and the doors at the head of the runner open on the king:

| Room | Off | What is in it | Who keeps it |
| --- | --- | --- | --- |
| the Ballroom | the west balcony | a chequer of black and blood marble, three chandeliers overhead, an organ with a red light in it, mirrors | two Revenants still dancing, an Abyssal Demon, demons |
| the Dining Hall | the west balcony | a banquet laid on two tables under two chandeliers, high-backed chairs, a hearth of lava | demons at the table, Bone Knights serving, an Abyssal Demon at its head |
| the Chambers Wing | the east balcony | a corridor of six bedchambers with four-posters, the king's in the middle of the south side | Revenants in the corridor, Bone Knights and a demon in the rooms |
| the Dungeon | a stair from the floor | cells behind bars (one broken open, with a chest in it), and a room with a rack in it | Bone Knights for jailers, a Revenant, and a Rime Revenant that got out |
| the Throne Room | the doors at the head of the runner | a dais two levels up with the throne on it, a channel of lava either side of the runner that burns to walk through, columns, banners | **the Cinder King** |

Everything in it is at the top of the ladder -- Abyssal Demons (76-78),
Revenants (73-75), the Rime Revenant (78), and demons and Bone Knights grown to
match them (69-75) -- which is where the three monsters written to stand past
the crypt finally stand, and what closes 75-79.

**The Cinder King** (84, a boss) is the palace's master and the highest thing
in the game: a demon lord in black plate trimmed with gold, ram's horns and a
crown of burning gold between them, a mantle of black lined with crimson, and a
greatsword with an edge that glows. He waits at the foot of his dais. Behind him,
beside the throne, is the **Heart of Cinders**, an amulet: a coal on a chain that
has not gone out. His first fall is a skill point and a boon (**Cinderheart**, 4%
more damage), his fifteenth his totem (**Crown of Cinders**: 10% more damage and
10% more health until dawn). Chests in the ballroom, the dining hall, the king's
bedchamber and the broken cell hold platinum and demonite -- the last two the
better -- and he himself drops demonite gear and, now and then, a Dracon bar.

Its art is its own. `tools/blender_palace.py` has the front, the towers, the
torches, the drawbridge and all of the hall's and the rooms' furniture -- twenty-five
props, registered into `blender_props.py`'s table the way the bestiary is into
the creatures' -- and the palace's tiles are appended to `tools/make_ground.ps1`:
basalt with the fire showing in its joints, the banded wall, the runner and its
gold edges, the ballroom's marble. The Cinder King is in
`tools/blender_bestiary.py` with the monsters that fill the ladder.

Two notes for whoever renders next. The drawbridge lies on the lava like the
floor does, so it is rendered from straight above (`TOP_DOWN`) and laid as an
overlay. And `make_props.ps1` steps colour to a coarse ladder: a dark brown seen
from straight above landed on olive, and the first drawbridge was green, so its
timber is lighter and redder than the palace's own.

### Purgatory's Plateau

Past the Bayou, the crypt and the Pit, the waking world had almost nothing to
fight from **50 to 70** (see the level survey). **Purgatory's Plateau** is that
band: four maps round a square, climbed onto from the north-west corner of the
Ashen Path, where a road leaves the burnt one, runs north past the first river
of fire and goes up under an arch of bones.

```
    the Scoured Flats  --  the Stronghold  (and its keep)
          |                     |
    the Pale Ascent    --  the Brine Terraces
          |
    the Ashen Path
```

Each of the four has a **ground of its own**, and nothing laid under all four
but the road that joins them (`make_ground.ps1`, appended on a seed of its own
so no other tile changed):

| Map | Ground | What lives there | Levels |
| --- | --- | --- | --- |
| **The Pale Ascent** | pale ash and scree, bone spires, a dragon's skull | Basalt and Pyre Dragons, Demons, a few Greater Demons toward the north | 50-60 |
| **The Scoured Flats** | white salt cracked into plates, salt pillars, a ring of them round a chest | Gale and Pyre Dragons, Greater Demons | 57-63 |
| **The Brine Terraces** | wet dark stone, brine pools with steam off them | Brine and Gale Dragons, Greater Demons | 58-64 |
| **The Stronghold** | bone-dust outside, flagstones within | Storm and Gale Dragons round it, Greater Demons in the courtyard | 63-70 |
| **the Keep** | the fort's own hall: pillars, pale braziers, a vault | the Stronghold's best Greater Demons, and a Storm Dragon on the vault | 67-70 |

**The monsters.** The five **elemental dragons** are one dragon -- Hoarfang's
frame, heavier or lighter -- dressed five ways, so that what tells them apart at
forty pixels is never only the colour: the Pyre Dragon is split along its sides
to the fire inside it; the Brine Dragon has fins where the others have spikes,
and barbels; the Basalt Dragon carries slabs of rock on its back with amber in
the cracks; the Gale Dragon is pale and feathered; the Storm Dragon is
thunderhead-dark with a bolt drawn down each flank. Each breathes its element
from a distance and leaves its element's mark: burning, soaked and chilled,
concussed, bleeding, electrified. The **Greater Demon** is the Demon grown to
what the Demon is afraid of: blood-red under obsidian plate, four horns, a mane
of fire, a cleaver. **Cerberus** is the boss of it: three heads that bite in
turn, fire in the throats, a snake for a tail -- and on some days (six in ten)
it walks round the Stronghold's walls, outside them. It has its own boon and
its own totem, the *Collar of Cerberus*. All seven are Blender rigs of their
own (`tools/blender_plateau.py`); the Stronghold, the arch, the plateau's
scenery, Cerberus's totem and the dream's mirror are props
(`tools/blender_stronghold.py`).

**Posted by the level they show.** `genmaps` reads `data/enemies.json` and
posts every monster on the plateau by the level it will be *shown* at
(`SpawnToShow`), not by a nudge to its stat block, so a map's spread is what
was asked for.

### The Hexmire

The Bayou tops out in the fifties; Purgatory's Plateau starts at fifty and
climbs to seventy by way of dragons. The **Hexmire** is the step between, at
**55 to 65**: four maps round a square north of the Bayou, reached where the
Bayou's west spur used to run off the top of the map and stop. The spur's
chest is still at its end; past it now stands the cult's **gateway** -- two
black posts and a beam hung with skulls, bottles and jars of green light.

```
    the Candle Fens    --  the Hexmire Temple  (and its sanctum)
          |                        |
    the Cypress Drowns --  Shellback Strand
          |
    the Bayou
```

| Map | Ground | What lives there | Levels |
| --- | --- | --- | --- |
| **The Cypress Drowns** | black loam and cypress litter round black water, bald cypress hung with moss | cultists, blowgunners, Shellback Clawfighters, the drowned, hags | 55-58 |
| **Shellback Strand** | crushed shell and tide-flat down to a lagoon; the Shellbacks' village of shell-roofed huts round a cook fire | Clawfighters and Snappers, two Elders at the fire, cult raiders on the road | 57-64 |
| **The Candle Fens** | red clay and yellow sedge; three rings of fetish poles round candle shrines, bottle trees | Voodoo Shamans at the rings, cultists, blowgunners, Zealots | 59-63 |
| **The Hexmire Temple** | the cult's stockade on chalked ochre earth, the thatched temple in it | Zealots and Shamans in the yard; outside, the cult, and a war-band of Shellback Elders come up from the strand | 61-65 |
| **the Sanctum** | red boards, daub walls, the painted post everything turns round, the altar | the cult's best, and **the Voodoo High Priest** | 63-65 |

**The cult.** Humans: the **Voodoo Cultist** with a machete (you bleed), the
**Cultist Blowgunner** (poisoned darts), the **Voodoo Shaman** -- whose pins
make you bleed and whose other hexes charm you toward it or turn you round --
and the **Cultist Zealot**, masked, with a two-handed cleaver and a heavy blow
that leaves you reeling. Over them all, in the sanctum, the **Voodoo High
Priest**: a boss at 65 with hexes of his own, a boon, and a totem to earn,
*The Priest's Poppet* (+5% crit, and 5% of blows miss you on the move).

**The Shellbacks.** An old race of the swamp -- upright tortoises, as tall as a
man, a shell no blade goes through and claws like billhooks. Their defence is
far above their attack and far above a cultist's of the same level, so most
swings glance off; what gets through their guard is paid back in bleeding. The
**Clawfighter**, the **Snapper** (a heavy lunge with its beak that opens you
up) and the **Elder**, huge and mossy, whose slam knocks you back and leaves
you reeling. They are at war with the cult, and do not ask whose side you are
on. (Tortle-like, from the tabletop; the name is the game's own.)

Each of the four has a boss walking it on some days (see *Bosses abroad*), the
temple's round its stakes, and the dead come out at night. All eight monsters
are Blender rigs of their own (`tools/blender_hexmire.py`); the gateway,
cypress, huts, poles, shrines, stockade, temple, sanctum furniture and the
totem are props (`tools/blender_hexmire_props.py`); the ten grounds and the
sanctum's floor and walls are in `make_ground.ps1` on a seed of their own.

### The Frostreach

The Ice Spire was one track up one mountain. Now, a third of the way up, a gap
opens in the cliffs on the west side between two runestones, and past it is
the **Frostreach**: four maps round a square at **60 to 75** -- the step between
the Hexmire and the Brimstone Palace, under Hoarfang's 79 at the summit.

```
    the Warlord's Howe  --  the Rimefall Glacier
          |                        |
    the Glass Mere      --  the Draugr Barrows  --  Ice Spire Peak
```

| Map | Ground | What lives there | Levels |
| --- | --- | --- | --- |
| **The Draugr Barrows** | snow-crusted heath, frozen turf; long sealed mounds, rings of runestones | draugr and draugr bowmen out of the mounds, ice trolls, white greatwolves | 60-64 |
| **The Glass Mere** | a frozen lake -- pale ice, darker patches -- and the trapper's cabin on an islet in the middle; a hunters' camp of igloos on the west shore | ice trolls and Frostbacks on the shores, draugr, wolves; two draugr bowmen on the islet | 62-68 |
| **The Rimefall Glacier** | white ice split with blue crevasses nothing crosses; a ring of snowmen nobody built | Frostback and ice trolls, frost wyverns, greatwolves | 64-70 |
| **The Warlord's Howe** | grey stone and rime; the great barrow of the barrow-kings, runestones and pale blue fires up to its door | **Undead Warlords**, their draugr, Frostbacks | 68-74 |
| **the Howe** | its hall: frost-rimed flagstones, the barrow-kings laid in stone down both sides, the eldest's seat at its head | the warlords and their dead | 72-75 |
| **Old Harl's Cabin** | the trapper's: a hearth, a bed, his journal -- and his chest | nobody, now | |

**The monsters.** The **Draugr** are the mountain's old dead -- blue-grey,
ring-mailed, a notched sword and a rimed shield -- and they leave a chill; the
**Draugr Bowmen** shoot frost-headed arrows that do the same. Their lords, the
**Undead Warlords**, are no bosses but past seventy, in blackened plate under a
crown of antlers, with a greataxe whose heavy blow leaves you chilled. The
**Frostback Troll** is the Ice Troll grown old and huge, ice growing out of its
back, and it throws lumps of the glacier. And on some days -- one in five on
the glacier, rarer round the Mere -- **the Abominable Snowman** walks: a boss at
74, with a first-kill boon and a totem, *Pelt of the Abominable*, and every kill
**rolls the rare-drop table six times** over, on top of its coins and platinum.

**Snowmen and igloos** stand about the mountain now: at the Spire's camp, on the
heath, in the hunters' camp by the Mere, and in a ring on the glacier.

**The Glass Mere's ice.** The whole lake is thin ice (`"thin_ice"` in a map,
`World::UpdateThinIce`). It bears a walker. It does not bear a runner:

- **Sprint** on it and it cracks behind you -- the crack drawn on the ice,
  following you and forking as it goes -- and the ice groans ("The ice
  cracks!", then "It won't hold!"). Keep sprinting for about **two and a half
  seconds** and it gives way.
- **Stop, or walk**, and it settles. A jump come down on it strains it too.
- The **darker patches** are weaker: they strain a little even under a walker.
- **Falling in** costs **a third of your health** (never all of it), leaves you
  soaked and chilled -- and a chill on the soaked is a frost -- and after a
  moment in the black water you **climb out on the last dry ground you stood
  on**, to try again. The hole stays open a while, and the cracks for longer.
- There is nothing to do about the two draugr bowmen shooting at you from the
  islet but walk. Old Harl's journal says as much.

In co-op a friend who goes through gets exactly what the host would: the host
steps every player's footing on the ice -- each their own strain, kept in their
seat, so one player's sprint never cracks it under another -- and deals the
fall, the water's bite and the chill to a friend as to itself, as it does lava.
A friend's own window only foresees it (the crack, the going under, the shore)
so it feels at once, and is told what it cost. Whoever is under the water is not
drawn on anyone's screen (`net::PlayerState::Under`); split-screen Player Two is
a friend like any other.

**Flurries.** Snow fell on the mountain; now the wind gets up. Every fifteen to
thirty seconds a gust blows for five to nine: the snow driven sideways, streaks
of it low and fast over the ground, and the whole view whitened a little, rising
and dying away (`Ambience::Gust`). Anywhere with `"ambient": "snow"`.

### Havenbrook, dreaming

At the bottom of the Reverie, down the dead end in the east of the Dreaming
Dark where the starlilies grow, there is a **standing mirror**. Stepped through,
it is **Havenbrook** -- the same streets, roofs, well and pond -- as a nightmare
has it: nobody in it, nothing that opens, sells or answers, the doors only
pictures of doors and the gates opening on the dark, and a violet fog over all
of it. It is a fourth depth of the dream, so it pays three extra shards a kill.

- **What walks its streets** is the orcs and the dead, dreamt: Nightmare
  Grunts, Slingers, Bowmen and Raiders, the Sleepwalking Dead, Hollow Ghouls,
  Nightmare Knights, Hollow Sleepers and Night Terrors -- each a tinted
  nightmare of its kind that answers to the Dreamer's Slate and leaves a shard,
  and **every one of them shown at fifty or more**, whatever the night makes
  of it (a post says the level it is to look, and the game scales whatever
  the night put there to it).
- **The bosses of the waking world** stand in it every night -- the Orc Warchief
  in the square, the Hollowrest Wight on the guild hall's steps, the
  Broodmother in the farmyard -- and **one more walks the town every night**
  (a Vampire Lord, the Pit Lord, the Den Mother, the Lizardman Chief or the
  Thing in the Spring), with a second on half the nights. They are the real
  bosses, not dreams of them: **every kill counts toward that boss's totem**,
  so the fifteen kills a totem takes can be had at night as well as by day.
- It was made from the town itself: `BuildDreamHavenbrook` copies the finished
  Havenbrook map, takes its people, doors and beasts out and puts its dream
  in, so the one cannot drift from the other.

## Monsters

### Filling the ladder

A survey of every post in the game against `Enemy::ShownLevelOf` found fifteen
of the seventy-nine levels with **no ordinary monster at all** -- 36, 38, 42-44,
57, 68, 70-72, 75-79 -- the waking world thinning out past 50, and gear running
twenty levels past the hardest thing to wear it against. Twenty-five monsters
were added to fill it, each **solved backwards from the level it had to show**:
`ShownLevelOf` inverted, given a target and a shape (a sack of hit points, a
hard hitter, something armoured) and searched for the numbers that land on it
exactly. Each is **its own creature**, modelled and animated for it -- see
[their art](#their-art).

| Where | Who | Levels |
| --- | --- | --- |
| [the Bayou](#the-bayou) | Bog Lurker, Mire Croaker, Swamp Hag, Rot Shambler, Fen Gator, Fen Stalker, Drowned One, Witchlight, Lizard Shaman, **the Mother of the Fen** | 30-56 |
| [Hollowrest Crypt](#hollowrest-crypt) | Grave Ghoul, Bone Archer, Cryptbound, Bone Knight, Plague Corpse, Tomb Shade, Grave Hound, Blood Thrall, Bone Colossus, Nosferatu, Crypt Warden, **Lord Ashcroft** | 26-71 |
| [the Brimstone Palace](#the-brimstone-palace) | Revenant, Abyssal Demon, Rime Revenant -- with demons and Bone Knights grown to match them -- and **the Cinder King** | 69-84 |

Every hole is closed -- 36, 38, 42-44, 57, 68, 70-72 and, now that the last
three stand in the Brimstone Palace, 75-79 -- and the Cinder King at 84 is the
top of it.

**A monster cannot leave anything on you.** The first draft gave these a status
their blows could leave and a leech; the player has no `StatusSet` -- statuses
are a thing the player does to monsters -- so both were data nothing reads, and
went. What a monster *shrugs off* (`immune`) is real and kept.

**A new boss costs more than a stat block**, and the self-test holds all of it:
a boon for every path (the count of boons each path can be given has to be at
least the count of bosses), a totem in `data/skill_trees.json`, the totem's item,
and the totem's art (`_totem(post, band, cap)` in `tools/blender_props.py`).

#### Their art

They first went out in other monsters' sheets -- a lizardman tinted green for
the Bog Lurker, a frog at 2.3 times its size for the Mire Croaker, the ankou in
red for Lord Ashcroft -- and each now has **its own**: a rig of joints and
rounded parts in `tools/blender_bestiary.py`, built from the same parts, cel
shading and outline as every monster in `blender_creatures.py` and registered
into its table, so `.\tools\make_creatures.ps1 -Only fen_gator` renders one like
any other -- idle, walk, attack, hurt and death, four facings each.

| Who | Drawn as |
| --- | --- |
| Bog Lurker | a hunched hulk off the lake bottom, all back and arm, moss on the hump and the eyes on top of the head |
| Mire Croaker | a toad the size of a pig, whose throat fills before it spits |
| Swamp Hag | bent over a crooked staff with a skull and a green light hung off the crook |
| Rot Shambler | peat on two stumps of root, a root club for one arm and somebody's skull in the front of it |
| Fen Gator | long, low, splayed, and mostly jaw |
| Fen Stalker | a mantis as tall as a man, the colour of dry reeds, scythes folded |
| Drowned One | swollen and grey-blue, wound in chain, dragging the anchor that took it down |
| Witchlight | a green flame with a face in it and three motes going round |
| Lizard Shaman | robed and feathered, a bird's skull over the snout, a light caught in a three-pronged staff |
| the Mother of the Fen | a lizardwoman grown into something nearer a crocodile, crowned in bone and reed |
| Grave Ghoul | crouched on long legs with longer arms, ears like a bat's and a mouth too wide |
| Bone Archer | a skeleton in an archer's hood, a longbow and a quiver |
| Cryptbound | wound in grave linen, chained over it, shackled at both wrists |
| Bone Knight | a skeleton in a great helm, breastplate and red tabard, behind a kite shield |
| Plague Corpse | swollen with the sickness, boils that glow and the air round it green |
| Tomb Shade | a shadow come off a tomb wall, with a pale mask for a face |
| Grave Hound | half hound and half skeleton, in a spiked collar with the chain snapped |
| Blood Thrall | a servant of the house in the rags of its livery, hands up and blood on its chin |
| Bone Colossus | every spare bone in the vault, skulls packed in its ribcage and a horned skull for a head |
| Nosferatu | bald, pointed and rat-toothed, in a black coat to the chin, all fingers |
| Crypt Warden | a tall knight in plate gone green, a halberd, and a lantern at its belt |
| Lord Ashcroft | dressed for dinner: crimson doublet, white stock, a cape red inside with its collar up, a rapier |
| Revenant | blackened plate with fire in its cracks, and half a greatsword |
| Abyssal Demon | long and thin and the colour of the dark between stars, seams of fire, a scorpion's tail |
| Rime Revenant | a giant frozen into its armour, icicles off every edge, and an axe of ice |
| the Cinder King | a demon lord in black plate trimmed with gold, ram's horns and a burning crown, a mantle lined in crimson, and a greatsword with an edge that glows |

Three things the first monsters did not need, so that twenty-five could be
written without guessing:

- **Each is fitted to a height on screen.** `Rig.fit` measures the rig through
  the camera, stood in its idle, and scales it to so many pixels -- set from what
  it was drawn as in the borrowed sheet, so none of them changed size in the
  world by much.
- **A pose's offsets are in the model's own units**, scaled with it: a lunge
  written for a ghoul is the same lunge on the Bone Colossus.
- **A swimmer is drawn sunk in the water.** The Bog Lurker, the Fen Gator and
  the Drowned One have a `swim` clip rendered with a disc of holdout at the
  waterline (`WATERLINE` in `blender_creatures.py`), so what shows of one coming
  across the lake at you is the hump, the head and the eyes.

A staff only stands up straight when the chest's lean, the arm's swing and the
hand's turn add up to nothing, and the first Swamp Hag held hers out like a
lance; the builders now say the sum in a comment beside each idle. Their **hit
boxes** had been one size for all twenty-five, whatever was drawn; each is now
measured off its own idle, the way every older monster's is about the size of
what you see -- the Mother of the Fen and the Bone Colossus are something a
sword can find.

### A monster's level is what it fights like

The number over a monster's head used to be the spawn's own -- the 1-to-8 nudge
in the map file that scales a stat block a little -- and it was never a measure
of anything. A dire bear that hits like Combat 62 said **Lv 1**; the Brackenwood,
advised at Combat 20, was full of things calling themselves level 1 to 3; every
Ice Spire wyvern read as level 1 to 4. It read as though the whole world were
the same difficulty and the player were simply getting worse at it.

`Enemy::ShownLevel` works it out from the stats the thing actually fights with,
in the same shape as the player's own Combat level -- a quarter of defence and
hit points, plus a third of the two attacking stats, with hit points read back
through the player's curve so a 320-hitpoint bear counts for what it is. What
comes out:

| | reads as | | | reads as |
| --- | --- | --- | --- | --- |
| Hare | 1 | | Bear | 24 |
| Boar | 4 | | Ice Troll | 31 |
| Orc Grunt | 6 | | Frost Wyvern | 37 |
| Grey Wolf | 10 | | Demon | 45 |
| Orc Raider | 14 | | Greatwolf | 58 |
| Lizardman | 12 | | Dire Bear | 73 |

**No damage changed.** The stat block, the hit points, the hit chance and the
numbers that come off a swing are exactly what they were: the spawn level still
does what it always did, and is still what the map file says. Only the number
shown is honest now, which is what was asked for. A stronger spawn of the same
creature reads higher than a plain one, so the four ranks of wolf in the
Westwold still read as four ranks.

Two areas were advised at a level that no longer matched what was in them, and
say what they hold now: the **Emberfell Mine** at Combat 10 rather than 6, and
**the Barrow** at 20 rather than 10. The self-test holds every advised area to
the middle of what actually lives beyond the door, within a dozen levels, and
holds the Hollowmarch to being mostly things a new character can fight.

### Harder to kill

Monsters died too fast, and nowhere faster than high up, where one charged
blow took a quarter of a bar. So every monster now has **more hit points than
its stat block says**, by how strong it shows: three tenths as many again at
level 1, rising in a straight line to **twice as many from level 50 up**
(`Enemy::Toughness`). A cellar rat is barely tougher; a Greater Demon, Cerberus
or the Cinder King takes twice the killing.

| Shown level | 1 | 10 | 20 | 30 | 40 | 50 and up |
| --- | --- | --- | --- | --- | --- | --- |
| Hit points, times | 1.30 | 1.43 | 1.57 | 1.71 | 1.86 | 2.00 |

It is only the pool. The number over every head is worked out from the stat
block exactly as before, so nothing's level moved and the level ladder -- and
every advised area -- still means what it did. A friend on an older build would
draw every bar wrong, so the co-op protocol went up to 12.


| Monster | Where | Effective level | Leaves |
| --- | --- | --- | --- |
| Grey Wolf | the Westwold west of the Wend, the Brackenwood's south | 8-12 | bones, a **wolf pelt** |
| Brown Bear | the Brackenwood | 22-24 | bones, meat, **bear hide** |
| The Den Mother | the den, in the middle of the Brackenwood | 34, a leader | four to six bear hides, and better |
| Greatwolf | the Howling Fells, in the Westwold's west | 56-58 | bones, a **greatwolf pelt** |
| Dire Bear | the Old Growth, in the Brackenwood's north | 72-73, a leader's heavy | bones, meat, **dire bear hide** |
| Cellar Rat | the inn's cellar | 1 | bones, raw meat, a few coins |
| Cellar Spider | the inn's cellar | 3-4 | spider silk |
| Broodmother | the back of the inn's cellar | 7 | silk, coins, a tonic or a copper ring |
| Orc Grunt | the Sunken Road, the mine, the barrow | 5-9 | bones, coins, raw meat, the odd bronze piece |
| Orc Raider | deeper in the mine and the barrow | 15-20 | bones, coins, iron ore and iron gear |
| **Orc Warchief** | the mine's last room | 28 | his totem, a warchief's purse, ember shards, steel and iron gear |
| Lizardman | the Mire | 10-13 | lizard scales, bogbean, iron ore, hides |
| Lizardman Chief | the camp in the Mire | 16 | scales, iron bars, a steel sword, Fen Bitters |
| Ice Troll | the Ice Spire's slopes | 26-29 | troll hide, damascus ore, azuryte gear |
| Frost Wyvern | round the Ice Spire's summit | 33-36 | wyvern scales, platinum ore, damascus gear |
| Wyvern Matriarch | the summit | 40 | scales, platinum gear, diamond ore |
| **Hoarfang** | its own ground above the summit | 62 | dragon fangs, scales, diamond and platinum, diamond gear |
| Cellar Slime | the well's upper workings | 8-9 | empty vials, bones, coins |
| Well Bat | the well's upper workings | 11-12 | bones, coins |
| Pit Hound | the well's deep cut | 20-21 | bones, hides, raw meat, coins |
| Ankou | the well's deep cut | 27 | bones, grave candles, tarnished rings, iron bars |
| Banshee | the well's deep cut | 30-31 | mourning lockets, grave candles, vials, coins |
| **The Thing in the Spring** | the spring at the bottom of the well | 32 | a purse of coins, a locket, steel, azuryte, the odd diamond |
| Shambler | Hollowrest, among the graves | 12-14 | rotten flesh, coins |
| Skeleton | Hollowrest, along the fence | 15-17 | bones |
| Wraith | Hollowrest, the old western half | 17-18 | grave candles, tarnished rings, mourning lockets |
| The Hollowrest Wight | in front of the crypt | 26 | lockets, rings, candles, coins, steel |
| Imp | the Ashen Path and the pit | 30-34 | coins, emberbloom, platinum ore, the odd horn |
| Demon | the hellgate and the pit | 40-43 | demon horns, demonite ore, platinum gear |
| The Pit Lord | the pit's last room | 54 | horns, demonite bars and gear |

Each place is a step up from the one before, and none is a wall: monsters in the
new areas are spaced along the way, do not chase far, and the Ice Spire has a camp
at its foot to rest, cook and sleep at. The drops trade: Ivo buys silk, scales and
hides, and the Collector in the Reverie pays best for trophies.

The **ways in** to the harder places are closed until a character could survive
them. A portal can carry `min_combat`: below it, the prompt says "needs Combat 30"
and a step-through edge turns the player back with a message rather than letting
them in to die. The Ice Spire needs Combat 30, the Ashen Path and the hellgate
Combat 40.

**Hazards.** A map can carry `hazards`: ground that burns while it is stood on,
a bite every half second with the number over the player's head. The Ashen Path
is crossed three times by rivers of lava, fordable only where the path crosses
them, and those fords burn; the Infernal Pit has lava vents in its corridors.
Jumping over a vent is safe.

A monster's `scale` in `data/enemies.json` now actually draws it bigger -- it was
read and never used -- so a broodmother, a chief, the matriarch and the Pit Lord
are the same art as their kin, only larger and tinted.

### On a map the host is not on

Each map anyone is on keeps running, so a map only friends are on is a world of
its own, and on it the host's own `Player` is a **stand-in**: it stands at the
arrival point with `absent` set, so every line written for "the player" has
something to point at. Two things about that stand-in used to make the monsters
there ignore whoever was actually standing in front of them -- Player One goes
into the inn, and the boar outside stops caring about Player Two.

- **It held a real seat number.** Seats are handed out from zero and the host is
  not one of them, so the first friend to sit down at the host's own machine is
  **seat 0** -- and so was the stand-in, by default construction. A monster that
  had picked Player Two by seat number matched the stand-in just as well.
- **It was thought about.** `UpdateShared` decides who each monster is after and
  then thinks as each player in turn, running every monster that is after them.
  With both of them answering to seat 0, the boar was thought about twice a
  frame: once as Player Two, three strides away, which put it into a chase, and
  once as a stand-in at the other end of the map, on which it gave up. It
  changed its mind on **every frame of every second** -- eighteen hundred times
  in the thirty seconds the self-test now watches it for -- and in all that time
  never closed the distance or swung once.

So: a stand-in holds `Player::NO_SEAT`, which no real seat can be; nothing is
ever thought *as* an absent player, because it is nowhere and everything it
thinks is that whoever it is after has gone; and each monster is thought about
once a frame, with anything left over -- the ones after nobody, on a map with no
one on it -- swept up afterwards, since a monster nobody thinks about never
wanders, rots or comes back.

What the player sees: on a map the others have left, the monsters come for you,
reach you and hit you, exactly as they do when everyone is together.

### Orcs that stand back

Not every orc closes. **A slinger throws rocks and a bowman looses arrows**, and
both hold their distance instead of charging: inside their own range, no nearer
than a swing's, backing off if you walk in. They are the orcs you already knew
in other colours -- pale green and pale blue -- because what has to read at a
glance is *that one is shooting*, not that it has a different silhouette.

| | Throws | From | Every | Softer by |
| --- | --- | --- | --- | --- |
| Orc Slinger | a rock | 170 px | 2.4 s | 3 hp, 1 defence |
| Orc Bowman | an arrow | 215 px | 2.0 s | 6 hp, 3 defence |

A shooter is meant to be got at, so it gives up some of the hit points and
armour its melee twin has, and it has no heavy attack: it is not a brawler.
Its aggro range is set **above** its shooting range, or it would stand in range
of something it cannot see and never loose.

**There are no more orcs than there were.** The realm holds the same
eighty-five orc posts it held before a single shooter was written -- the
self-test counts them -- and the mixture comes from the posts themselves. On
the overworld a post is one of a pool with no group, so each answers for itself
(see `World::ResolveSpawn`): two melee to one shooter, settled by the day's
hash rather than a roll, which means both machines in a co-op game agree
without a word and a band that was all axes yesterday has two slingers behind
it today. In the mines and the barrow the roster is settled when the map is
generated, and about a third of it shoots.

In the engine it is three fields on a monster -- `shoots`, `shoot_range`,
`shoot_cooldown` -- and anything in `data/enemies.json` can have them. The
throw itself rides on the swing that was already there: at the same point in
the wind-up where a claw would land, a projectile leaves instead, and is
resolved where every other shot is.

### What starts a fight, and what ends one

**Two things start one.** Someone inside a monster's aggro range: it has seen
them. Or **taking damage, from any distance and by anything** -- an arrow from
across a field, a Meteor, a wound still bleeding (`Enemy::Provoke`). It used to
answer only what it could see, so anything could be shot to death from a step
outside its range while it grazed. In co-op it comes for whoever drew the
blood, and stays angriest with them for a few seconds after, unless someone has
Stood Fast.

**What ends one is the ground it has covered with nothing happening.** It used
to be how far it had got from its post: a monster fought at the edge of that
ring turned in the middle of a swing and walked home, and one led a little way
off simply stopped. Now every stride of a chase is counted, and anything that
is a fight -- **a blow taken, or a swing begun** -- starts the count again. When
the count reaches its budget, half as far again as the spawn's `leash`, and
nothing has happened, it gives up, walks home, and turns on anyone who steps
close on the way, however far from home that is. So a monster can be led
anywhere by someone who keeps fighting it, and cannot be led far by someone who
only runs. A monster that only ever *saw* someone still loses them when they
get well out of sight; one that has been hurt does not need to see them. The
stride counted is the one it meant to take, not the one the map allowed, so
something walking into the foot of a cliff after an archer on top of it tires
of that as fast as of a long run.

### Bosses abroad

In the high country a boss does not only keep its lair. On some days **one of
a pool of bosses walks the map** -- starting somewhere different on it each
day, going round a loop laid about the map, and coming for anybody who strays
within its reach. When it loses them it goes back to where it left the loop
and walks on.

| Where | Who (one a day) | Shown at | Out |
| --- | --- | --- | --- |
| The Bayou | Lizardman Chief, Broodmother, Den Mother, the Thing in the Spring | 52 | 6 days in 10 |
| The Ice Spire, on the track | Den Mother, Hollowrest Wight, Broodmother, Lizardman Chief | 50 | 6 in 10 |
| The Ashen Path, on the track worn round the north | Pit Lord, Orc Warchief, Lord Ashcroft, Hollowrest Wight | 62 | 6 in 10 |
| The Pale Ascent | Orc Warchief, Lizardman Chief, Den Mother, Broodmother | 58 | 5 in 10 |
| The Scoured Flats | Pit Lord, Lord Ashcroft, Wyvern Matriarch | 64 | 5 in 10 |
| The Brine Terraces | the Mother of the Fen, the Thing in the Spring, the Sleepless | 64 | 5 in 10 |
| The Stronghold, round its walls | Cerberus | 70 | 6 in 10 |
| The Cypress Drowns | the Mother of the Fen, Lizardman Chief, the Thing in the Spring, Den Mother | 58 | 5 in 10 |
| Shellback Strand | Wyvern Matriarch, the Mother of the Fen, Broodmother | 60 | 5 in 10 |
| The Candle Fens | Lord Ashcroft, Hollowrest Wight, the Sleepless | 62 | 5 in 10 |
| The Hexmire Temple, round its stakes | Pit Lord, Lord Ashcroft, the Mother of the Fen, Orc Warchief | 64 | 6 in 10 |
| The Draugr Barrows | Hollowrest Wight, Lord Ashcroft, Den Mother | 66 | 5 in 10 |
| The Warlord's Howe | Pit Lord, Lord Ashcroft, Wyvern Matriarch, the Sleepless | 72 | 5 in 10 |
| The Rimefall Glacier, along its roads | the Abominable Snowman | 74 | 2 in 10 |
| The Glass Mere, round the shore | the Abominable Snowman | 74 | 15 in 100 |
| The Dreaming Dark, over the bridges | the Sleepless, Hollowrest Wight, Lord Ashcroft | 62 | 7 in 10 |
| Havenbrook, dreaming | (see above) | 62 and 58 | every night, and half |

They are the bosses themselves: a first kill is a skill point and a boon, and
every kill counts toward the totem, the same as in their lairs. Killed, one
stays dead until the next day.

How: a post can carry a **`route`** -- a loop of points -- and a **`shown`**
level (`EnemySpawnDef`). With `shown`, whatever the day picks from the pool is
scaled to look that strong (`Enemy::LevelToShow`), so a Broodmother and a Pit
Lord come out alike. `World::RoamDraw` is a hash of map, day and post that says
whether it is out and where on its loop it starts, so a friend's machine agrees
without being told. The monster walks at half its pace a point at a time, its
home the point it is making for (`Enemy::Roam`); a point it cannot reach it
gives up after three seconds and makes for the next. `genmaps` lays the loops
-- round an ellipse, along a track, over the bridges between the dream's
islands, or, where no loop will go, out and back along the map's own posts --
and only ever with legs a monster can walk in a straight line.

### What a boss leaves, the first time

Killing a boss paid what it dropped and nothing else: the Pit Lord was a long
fight for a loot roll. **The first time a character brings one down** it leaves
them two things, for good:

- **A skill point** for their tree, over and above the one every third level
  earns. There are eleven bosses, so eleven points against the nine a finished
  tree is short of (ten, for the hero): someone who has killed everything in the game can finish
  their tree, and nobody else can. The tree's header says how many of your
  points came that way.
- **A boon**, by the dice: one of fifteen small permanent things, of those the
  character's path can use and they do not already have -- so no first kill
  repeats one, a hero is never handed mana, and two characters who kill the
  same bosses do not end up the same.

| Boon | | Boon | |
| --- | --- | --- | --- |
| Vigour | +8% maximum health | Sure Feet | on the move, 3% of blows miss you |
| Stoneblood | +5 Defence, whatever you wear | Swift Hands | attacks 2% faster |
| Long Wind | +10% maximum breath | Heavy Hitter | charged attacks +6% |
| Second Breath | breath returns 12% faster | Shield Arm *(hero)* | a block costs 8% less breath |
| Fleetness | walk 3% quicker | True Flight *(warden)* | arrows fly 8% faster |
| Keen Eye | +2% critical chance | Deep Reserves *(wayfarer)* | +10% maximum mana |
| Might | +3% damage, with anything | Wellspring *(wayfarer)* | mana returns 12% faster |
| The Leech's Gift | 2% of damage dealt returns as health | | |

Each is about one rank of a talent, of a kind any path can use -- and maximum
health, which no tree teaches at all. **Once each:** a boss is back the next
dawn and leaves its loot again, but this is kept count of by who it was. The
game says so when it happens, in two short lines, and the Skills panel has a
third tab, **Boons**, that lists them and names who has been brought down.

Everyone who was there gets theirs, each once: a fight shared is a kill
shared. It is part of the character, kept with the tree's ranks
(`Talents::SlayBoss`, in the `talents` of a save), so it goes wherever they go:
the save, the character a friend keeps on their own machine, the sheet their
host rolls with. In co-op a friend's boon is rolled **on their own machine**,
where their character is: the kill is relayed to it with which boss it was (a
kill event's `secondary` -- a chief's kill target is "lizardman", for the
contracts' sake, so the target cannot say), and comes back to the host on
their next sheet. Unlearning a tree gives a boss's point back with the rest
and does not touch a boon. A save cannot hold more boons than bosses, or a
boon nobody made.

The boons are data: `"boons"` in `data/skill_trees.json`, each an `effects`
map in the names the trees already use (plus `max_health`), with an optional
`paths`. `--slay a,b,c` with `--scratch` starts a character who has already
killed those, and `--at x y` stands them at a point on the map, for looking at
somewhere no door leads to.

### The fifteenth time: a totem, and the ring at home

A boss is back every dawn, and after the first kill it was only its loot. **The
fifteenth time a character brings one down it leaves its totem** -- a carved
post a hand high, with the boss's head on it: a spider for the Broodmother,
horns for the Warchief, a skull for the Wight. One for each boss, one each per
character, straight into the bag (at your feet, if the bag is full). It cannot
be sold, dropped or lost. The Boons page of the Skills panel keeps the count:
*"Broodmother 7/15"*.

There is a **ring set in the floor in the middle of your house at Mossvale**.
Touch it and a panel lists your totems; stand one in the ring and **it gives
its blessing for the rest of that day** -- until dawn, wherever you go, and
through a death. The next day it is a carving in a ring, drawn dull, until a
hand is put on it again. **One at a time:** standing another in the ring puts
the first back in your pack, and its blessing goes with it. It can be lifted
out altogether.

| Totem of... | Blessing, until dawn | | Totem of... | Blessing, until dawn |
| --- | --- | --- | --- | --- |
| the Broodmother | walk 8% quicker | | the Sleepless | +20% maximum health |
| the Lizardman Chief | breath returns 30% faster | | the Wyvern Matriarch | attack 6% faster |
| the Hollowrest Wight | 5% of damage dealt returns as health | | the Pit Lord | +6% critical chance, criticals 25% harder |
| the Warchief | +10% damage, with anything | | Hoarfang | +12 Defence and +12% maximum health |
| the Thing in the Spring | on the move, 8% of blows miss you | | the Unwaking | +8% damage, charged attacks +15% more |
| the Den Mother | +15 Defence, whatever you wear | | | |

A totem's blessing is two or three times what a first kill leaves for good,
because it is one at a time, for a day, earned over a fortnight, and has to be
gone home for -- which is what the waystone at Mossvale is for. It is on top
of the boons, not instead of them. The self-test holds every totem to being
more than any boon of the same kind.

How: the count, the totem in the ring and the day it was last touched are in
`Talents` with the boons (`boss_kills`, `totem`, `totem_day`), so they go where
the character goes -- the save, a friend's own machine, the sheet their host
rolls with. `Talents::SlayBoss` reports the fifteenth; `World::AwardBoss` hands
the totem over; `Talents::PlaceTotem` is the ring, and returns what was
standing in it. The blessing is read through the same `BoonEffect` the boons
are, and is awake only while the day it was touched is the world's quest day
(`World::TellTheDay`, at dawn and whenever someone arrives, which also puts
health and mana back to what they now are). The ring is a `totem_circle` map
object with no picture of its own -- it is laid in the boards as an overlay,
under everybody's feet -- and what stands in it is drawn from the item's own
picture, for whoever is looking: a totem is the character's, not the room's.
The totems are data: `"totems"` in `data/skill_trees.json`, and an item each.
`--screen totems` opens the panel; `--bag totem_orc3` puts one in the pack.

One thing found on the way: a panel sent to a friend's machine is a number,
and the number was held to `Sleep`, which was the last kind there was when that
line was written. A friend who touched a woken **waystone** was asked how they
would like to spend the night. It is held to the last there is now.

### Highwaymen

The forest paths have bandits on them. **Highwaymen loiter in twos at the
trailside** -- two pairs before the fork on the Whisperwood Trail and two past
it, one either side of the path, and four more pairs along the trail east of
the Sunken Road under the Hollowmarch's trees -- where a cart has to pass them. They are people, drawn on the hero's own rig in
dark leathers with a red neckerchief pulled up for a mask and a sword in hand
(`highwayman` in `LOOKS`, `tools/blender_character.py`, rendered with only the
five clips a monster plays and no plate).

They are made to be handled by whoever is already handling the trail: 18
hitpoints, Attack and Strength 6 with a bonus of 6, Defence 5 -- quicker on
their feet than an orc grunt and a little tougher, and well short of an orc
raider -- at levels two to four on the trail and three on the road. The
self-test fights one with a level 12 sword-and-no-shield character and checks
they win it with most of their health. They drop coins, and now and then what
a bandit would have on him: thread, a hide, a meal, a tinderbox, a vial, or
the sword. The Mossvale board posts a daily, **Road Toll**, to drive four of
them off the trail, once the trail has been cleared the first time.

### Heavy attacks

Leaders -- the Orc Warchief, the Broodmother, the Lizardman Chief, the Wyvern
Matriarch and every Frost Wyvern, the Hollowrest Wight, the Thing in the Spring,
the Pit Lord, Hoarfang, and the Nightmare Brute -- have a second attack besides
their swing: **a heavy, telegraphed blow that no shield stops.**

A few seconds into a fight, when the player is within its reach, the leader
plants its feet and winds up. **A bar over its head fills** from yellow to red
over the wind-up, its frame flashing as it nears full, and **the monster glows
red**: a red halo round its silhouette, its own colours pulled towards red, and
at night or underground a red light around it. It cannot be knocked about or
hit out of the charge.

For most of the wind-up it turns to follow the player. For the last stretch it is
committed to where the player was, and that is the chance: **step out of the line
and the blow lands on nothing.** If it connects it hits for two to three times
the monster's biggest ordinary hit.

**Blocking it is a mistake.** A raised shield stops none of it; the blow lands
half as hard again, the guard shatters, the stamina bar empties, and the wait
before stamina comes back is doubled.

| Leader | Wind-up | Damage | Rests |
| --- | --- | --- | --- |
| Broodmother | 1.2s | x2.2 | 8s |
| Lizardman Chief | 1.3s | x2.3 | 9s |
| Orc Warchief | 1.4s | x2.4 | 9s |
| Nightmare Brute | 1.4s | x2.2 | 9s |
| The Hollowrest Wight | 1.4s | x2.4 | 9s |
| Frost Wyvern | 1.5s | x2.2 | 12s |
| Wyvern Matriarch | 1.5s | x2.5 | 9s |
| The Thing in the Spring | 1.5s | x2.5 | 9s |
| The Pit Lord | 1.6s | x2.6 | 9s |
| Hoarfang | 1.8s | x2.8 | 10s |

A heavy attack is a `heavy` block on a monster in `data/enemies.json` --
`windup`, `damage`, `cooldown`, and optionally `reach`, `width`, `opening` and
`knockback` -- so any monster can be given one.

### The art

Every new monster is original, modelled and animated in
`tools/blender_creatures.py` (`.\tools\make_creatures.ps1 [-Only wyvern]`) from the
hero's parts, cel shading and reduction: a small tree of joints per creature with
rounded meshes hung on them, and an idle, walk, attack, hurt and death clip each,
four facings, the shadow composited in. Frames are sized per creature at the
hero's scale -- 48px for a rat, spider or imp, 80 for a lizardman or a troll
or demon, 112 for a wyvern -- with the feet the same fraction of the way down the
frame, so `data/sprites.json`'s anchor stands them on their position.

Two things worth knowing before adding one. About a joint's X axis a positive
pitch leans a limb built upward *forward* and swings a hanging one *back*; the
first wyvern had its neck and tail the wrong way round and read as a sitting
blob. The second deer made the same mistake and nobody caught it for a long
time: its neck leaned back over its shoulders, so its head sat on top of its
body looking at the sky, and on a shoebox of a body over legs three times as
long that read from the front as a brown pillar and from the side as a table
with a stick on it. The red deer stag that replaced it is built in the order
things matter at this size -- the head out in front of the chest with daylight
under the jaw, ears that stick out sideways, a rack wider than the shoulders so
it breaks the silhouette from the front as well, a body longer than it is
tall, legs dark toward the hoof, and the pale rump -- in a 64-pixel cell, since
there is more of it to draw than of a boar. And from this camera, anything behind a head draws above it on screen: the
first ice troll's mane hid its face.

Anything that runs -- the orcs, the boar, the deer, the fox and the hare -- has
its run made out of its own walk (`running()`): the same cycle with longer
strides, more lift and a lean into it. A creature that runs already has a walk
that says how it moves, and a second gait authored separately would not match
the first.

**Three render bugs found by looking at the sheets rather than at the code**,
all of them worth knowing about because none of them showed up as an error:

- *Every hurt clip in the game was rendered at the wrong scale.* A hurt is
  three frames against four facings, so its sheet is taller than it is wide,
  and the render camera's `ortho_scale` was left on `AUTO` -- which means "the
  longer side of the image". Every other clip has at least four frames and came
  out right; the three-frame ones were scaled to their height, so each row was
  drawn lower in its cell than the one above it until the bottom row's feet hung
  out of the frame. `sensor_fit = "HORIZONTAL"` is the whole fix. The self-test
  now measures where the feet land in each row of every clip and compares that
  pattern between clips of the same creature, so a whole class of grid
  misalignment fails loudly.
- *A fall has to be in world space.* The humanoids' deaths tipped the body over
  its own backwards axis, which looks right from the side and nowhere else: in
  the row where the creature faced the camera it fell away from it, in the row
  where it faced away it fell towards it, and both foreshortened into a standing
  lump that never seemed to drop. `_wroll` is applied outside the facing turn
  (Euler order `ZYX`, which puts the turn innermost), so every row topples the
  same way across the frame -- the one direction a camera looking down at
  forty-six degrees can read. `Rig.apply` measures the rig to know how long the
  body is, pulls the fall back by half of that to keep it in the middle of its
  cell, and eases off the angle for a creature too long to lie flat in one frame.
  The Euler's order lives on the *object*, not on the Euler handed to it: an
  object keeps its own `rotation_mode` and reads only the three numbers, so the
  first attempt silently applied them in the wrong order and moved the problem
  to the other two rows.
- *A weapon arm has to stay down.* Flinging both arms forward is right for empty
  hands; a spear or a club held that way ends up pointing straight at the camera,
  where a sideways fall cannot lay it down, and it stands in the frame like a
  planted pole while the body under it goes flat. `topple(armed=True)` lays that
  arm straight along the body -- rest angles and all, through `_straighten`,
  because what matters is the arm's *total* angle and every creature's rest angle
  is different.
- *Nothing may be drawn across a cell's edge.* The engine draws exactly one cell
  of a sheet, so a pose that crosses an edge is cut off in its own frame and
  leaves a scrap in the next one. `keep_in_cell` measures each posed rig in the
  camera's screen axes and slides it back inside by just enough; a rig that
  already fits is not touched. The Warchief, a head taller than the other orcs,
  is also stood lower in his frame (`FRAME_DROP`), into the empty strip every rig
  has under its feet. The self-test checks every edge pixel of every creature
  sheet.
- *A roll about the body's own spine is right for an animal, but which side it
  lands on matters.* Side on, a deer rolled away from the camera ended with its
  legs straight up the screen, balanced on its back; `_roll_to_camera` turns the
  two side rows so the legs fall down the screen under it. The bat no longer
  rolls at all -- side on, rolling stood its wings up the screen and it never
  seemed to land -- it drops and lies where it falls.

The swamp, peak and pit props -- reeds, lily pads, swamp trees, stilt huts,
totems, ice spires and crystals, snowy pines, wyvern nests, charred trees,
obsidian, the hellgate, the cellar hatch and cobwebs -- are in
`tools/blender_props.py`; the drops' icons in `tools/blender_tiers.py`. The peak
has falling snow and a hard wind, and the Ashen Path rising embers, as their own
kinds of ambience.

---

## Level editor integration

Maps are the `.mx` format LevelEdit-Plus exports:

```json
{ "name": "...",
  "tiles": { "grass": { "filepath": "assets/tiles/grass.png",
                        "locations": [[cx, cy, w, h], ...] } } }
```

Placements are centre-anchored, matching `GameTile::Render` in the editor, so a
map drawn here lines up pixel-for-pixel with the editor view.

Everything a game needs on top of that — draw layers, collision, portals, spawn
points, enemies, NPCs, objects — lives under a separate `"dreamquest"` key.
**The editor ignores keys it does not recognise**, so these maps open in
LevelEdit-Plus, can be edited by hand, saved, and still run.

`tools/tilecut.cpp` exists because of the editor: the CraftPix tilesets ship as
packed autotile sheets, and the editor works with one image file per tile. It
cuts the atlases into individual tiles and finds each separate drawing on the
packed object sheets, so the same art is usable in both programs.

To rebuild the world from scratch: `.\build.ps1 -Maps`.

---

## Saving

Three slots, plus an autosave every two minutes and one on quitting to the main
menu. A save records the map, your exact position and facing, HP, every skill's
XP, inventory, worn equipment, quest progress, and the one-shot world flags —
which chests you have opened, which notes you have read, and what you have
learned to brew and to enchant — plus the day and the hour, where your camp is
pitched, what every trader has sold today, which herbs are picked and which
trees are down and seams worked out, and, for a save made asleep, where you are
dreaming from — so loading puts you back exactly where you left off. What lies
on the ground is not saved: a dropped item is gone when you leave. Saves are written to a temporary file and
renamed, so an interrupted write cannot destroy the previous one.

---

## Self-test

```powershell
.\build.ps1 -Test
```

Screenshots prove the game runs; they do not prove that the mission board names
a quest that exists, that every dialogue option leads somewhere, or that a loot
table only drops real items. `tools/selftest.cpp` links the game's own systems
and checks all of it — currently **36826 checks** covering:

- every sprite sheet and item icon exists on disk
- every loot table drops real items, and quest-critical drops are guaranteed
- every quest objective, prerequisite and reward resolves, and every quest has
  a giver somewhere in the world
- the dialogue graph is fully connected
- no NPC offers a quest that is not yet available, and every Talk and Deliver
  stage has a hand-in option reachable from that NPC, shown only while the quest
  is at that stage; Maren, Hesper, the smith and Wendel open with the right
  lines for a new character and for one further on
- the quest day turns over at dawn; dailies are never collect stages, every
  pool posts two a day and rotates, a daily cannot be repeated the same day but
  can on a later day it is posted, completions are counted, and all of it
  survives a save
- the dream quests played through the world at night: sleeping into the
  Reverie, reading the voice, reporting to Mira, Hesper's hunt, and the reverie
  dailies posted on the Slate
- order books: Halda's orders cover ore, bars, weapons, hides and armour, and
  pay Mining XP and coins; Wendel's are fish and pay Fishing XP; every order
  pays more than selling the goods, needs the Crafting or Fishing level its item
  takes, and costs more to fill from a shelf than it pays; on every one of forty
  days a new character is posted at least two orders they can take and none they
  cannot, and a skilled one sees more of the book; the order line appears only
  when an order can be filled, hands in exactly what it asks and leaves the rest,
  pays once, cannot be repeated the same day, and never takes another NPC's
  delivery
- traders: every shop is kept by an NPC standing in its own town, who can be
  walked up to and offers to trade from the first line; every town has a general
  store and another shop; every shelf item exists, has a price and a limited
  stock; forges sell materials and buy ore, bars and metalwork but not fish; no
  shop sells a quest's items before that quest is done, or a daily's ever
- prices: nothing bought anywhere sells anywhere for what it cost; every recipe
  makes something worth 1.8 times its materials; everything gathered sells
  somewhere and sells for more cooked; every piece smithed from a forge's bars
  sells for more than the bars cost
- trading: buying takes the price and the stock, never more than is left or
  affordable, and a full pack is not charged; stock stays sold out the same day,
  survives a save and comes back at dawn; gated stock appears once its quest is
  done; selling pays and refuses what a trader does not deal in; choosing a
  trader's trade line closes the conversation and opens their shop
- the grown Hollowmarch and its ways in: the overworld is 4736 by 3968 with its
  height grid covering all of it and every arrival on the map; the Mire carries
  on into the new west with lizardmen in it, and the land carries on south; none
  of the road pack's grass stencils are on the ground and every kind of new
  decal is; the barrow is a mound with its portal in the doorway and no door
  sprite; every dungeon is left by a stone flight and gone deeper into by a
  stairwell; and an overworld save from before the map grew loads with the
  player and their camp on the same ground
- monsters and the new places: every new monster has its sprite, all five clips
  drawn and a loot table; lizardmen and their chief hold the Mire, rats, spiders
  and the broodmother the cellar, trolls, wyverns and the matriarch the Ice Spire,
  imps the Ashen Path and demons and the Pit Lord the pit, none spawned inside a
  wall, with the cellar a beginner's fight, the peak between 24 and 42 and the pit
  harder still; the Mire has no black cliff tile, and is sedge, peat and mud with
  bog pools, reeds, lily pads, drowned trees, huts and totems; the Ice Spire is
  closed below Combat 30 and the Ashen Path and the hellgate below 40, and in the
  world a new character walking into the Ice Spire's path stays in the Hollowmarch
  while a veteran walks on up; the pit has lava vents that burn to stand in and
  the Ashen Path fords that burn to cross; the cellar quest can be taken, is
  finished by the cellar's own rats, spiders and broodmother but not by rats
  anywhere else, and by telling Bess
- all twenty-three maps load; portals point at real maps; every enemy, NPC and object
  resolves
- the OSRS XP table matches known values
- a starting character can actually win the first fight the level 1 board quest
  sends them into
- every projectile has art on disk, actually moves, is slow enough that its
  sub-steps cannot carry it through a wall, and is not drawn larger than the
  character firing it
- attack speed orders swings correctly, a zero speed is clamped rather than
  swinging instantly, every attack leaves a gap, and a bare-handed chain is
  neither a machine gun nor so slow that combat drags
- a sweep into a wall stops clear of it and reports a normal that sends a
  bounce back the way it came, while open floor reports no contact at all
- no portal or spawn sits on a cliff edge, every row of the overworld has a
  walkable crossing, and no raised ground is sealed off from the spawn once
  ramps and two-level jumps are counted
- no building inherits the overworld's height grid when loaded after it
- every NPC and usable object in every building can be walked up to from the
  door
- every portal in the woodland zones can be walked to from where you arrive,
  and the Whisperwood, Mossvale and Fernhollow journey round-trips
- every portal arrives at a spawn that exists, not inside a wall, and not on a
  step-through portal that sends the player straight back -- which is what a
  flight of stairs between two floors would otherwise do
- every way back through a portal arrives beside the way in, so leaving a
  building puts you on its doorstep rather than in the middle of town
- real fights, run frame by frame through the world update with the buttons
  pressed through the game's own input: a new character fighting back beats a
  fox and a boar, and the monster never stands on top of the player
- a monster's health bar stays hidden until it is attacked, shows for as long
  as it is hurt, and never shows more than it has; its fill is within a pixel
  of hp / max_hp, never empty while alive and never full while wounded
- every corpse despawns within three seconds of death, and a revived monster
  comes back whole with its bar hidden
- the HUD art is on disk, and the minimap clips every row to the edges of the
  map without ever sampling outside it
- the title art: the cover painting, the window icon and the .exe's .ico are all
  in the repository rather than in the art that is not committed; the window
  icon is square and big enough for a hi-dpi taskbar; the .ico is a real icon
  file carrying the 16, 32 and 256px sizes Windows asks for; and the resource
  script the build compiles names the icon that is actually built
- a collect objective counts what the player is carrying
- every spell fires a projectile of its own element, all four elements are
  castable, and a level 1 character has the mana to cast one
- the elemental cycle closes and the multipliers point the right way
- Ranged and Magic read their own levels rather than Strength
- ragged animation rows declare a frame count for all four facings, and every
  paperdoll layer is on disk
- every worn overlay has art on disk and a rectangle that lands on the
  character rather than in empty frame, with helmets on the head and boots at
  the feet
- worn plate: all three playable characters have all five armour layers for
  every clip with the sheets on disk; the plate is drawn after the body and the
  head; a tier weapon sheet is recognised as an alternate rather than as a layer
  of its own; every metal tier's helm, cuirass and greaves paints the layer for
  its own slot and no weapon paints any; and a bronze helm over an iron cuirass
  over steel greaves turns on three layers in three different metals, with the
  character's own colouring left alone underneath
- three cuts of armour: every tier is cut light, plate or ornate, plate carries
  no suffix so it never looks for a sheet that was not rendered, every character
  has every light and ornate sheet on disk, an alternate cut is recognised as an
  alternate rather than as a second cuirass, and a bronze helm over a demonite
  cuirass over steel greaves is drawn as a cap over a horned breastplate over
  plain plate
- a house of your own: Mossvale's empty house has a locked door, the key is
  under a stone round the side rather than on the step, the quest points at both,
  and the whole thing plays through -- walk to the stone, look under it, let
  yourself in, open the chest, put twenty-five logs in it, walk out and back and
  find them still there
- the storage chest: a hundred slots; a stack goes in whole; a full chest takes
  nothing and says so by taking nothing, because the panel leans on that to
  decide whether to remove what it was moving; a chest made smaller gives back
  what was in the slots that went away; and what is in it survives a save
- every creature stands at the same height in all four facings of every clip it
  is still standing up in, measured off the sheets themselves
- blocking: a blow costs its damage times the attacker's level times the
  shield's multiplier in stamina; running short stops only the share that was
  paid for, empties the bar and breaks the guard; every tier's shield stops more
  and costs less; a lantern is not a shield. Played through: no guard without a
  shield, a raised guard holds the shield up and cannot swing, a wooden shield
  stops half of a blow from in front and trains Defence for it, a blow from
  behind gets through, a dragon's blow breaks the guard and it comes back once
  the bar refills, an enchanted shield takes the same blow for a fraction, and a
  guarded step is slow
- Rushing Strike: the melee tree's Footwork branch has it at Attack 15, and the
  ranged and magic trees keep three branches. Played through: a running light
  attack is ordinary without it and standing still with it; with it, a running
  light attack leaps, plays the leap, hits for 1.4 times a light attack, leaves
  the ground and covers the distance; inside three seconds it does not leap
  again and after three it does; a bow never leaps; and with a monster targeted
  the leap turns to it and lands with the monster inside the blow
- leaders' heavy attacks: the Warchief, the wyverns, the dragon and every other
  leader have one and ordinary monsters do not. Played through: the Warchief
  winds up close to the player, the bar fills steadily over the whole wind-up and
  nothing lands until it is full, then a blow lands for well over an ordinary hit
  and it rests before the next; against a raised enchanted shield it stops none
  of it, lands half as hard again and shatters the guard with all the stamina;
  it turns to follow early in the wind-up and is committed late in it, and a
  player who steps out of the line takes nothing; and a charging leader is braced
  against knockback and keeps charging when hit
- trees come down and seams give out: every tree and seam on every map can
  run out and says how long for, a felled tree has a stump to be drawn as, and
  played through an oak comes down about every eight logs, the work stops, the
  stump offers nothing, it is written down for the save, and it is back once
  its time has passed; a copper outcrop gives out sooner
- dropping things: G and Y are the drop key, keys and letters cannot be
  dropped, and a dropped stack lies at the feet without being scooped straight
  back up, comes back once the player has stepped clear, and is gone after
  three minutes, while a monster's drop is not
- hide boots: made at a workbench from hide and thread, sold by Ivo, a
  twentieth quicker on the feet -- measured as ground covered in a second --
  and the Drowned King's boots keep their own stride
- enchanting: eight charms listed cheapest first that between them cover rings,
  amulets, boots and armour and never a weapon; every one but the first is a
  scroll someone sells and Mira teaches the first once; the Copper Ring of
  Keenness is the ring's bonuses plus the charm's and worth both, takes no
  second charm, and is drawn as the ring; a shield takes Fortitude and a lantern
  does not; every enchanted twin is neither recipe nor scroll; working a charm
  takes the materials and the piece and gives the enchanted piece back, and
  refuses without them; a worn charm counts and survives a save; and standing
  at the table by Mira's stones offers it and opens the panel
- combos: the profiles order as they should and the hero has a clip and
  every tier's sword and spear for each; played through with a bronze sword
  against pinned orcs: the chain ends at three and a fourth light opens a new
  one; a heavy after one light is the Crushing Blow, out on the press, and the
  orc reels for about a second and does not swing back; the Cleave's sweep
  reaches a monster off to the side the finisher does not; a light after a
  strong is the Backhand and the chain goes on from it; both buttons on one
  frame, or two frames apart either way round, are the Cross Cut, which costs
  its stamina and strikes the monster behind as well as in front; winded there
  is none; a bow has no combos; a press inside a swing comes out the moment
  the swing ends; a hold past its window is a charge that ignores a light; and
  a braced Warchief shrugs a stagger off where a plain orc reels on the spot
- the chain counter: a fresh fight has none, three lights that land are
  three with the trail saying so, a combo is named in it, a long run keeps the
  last six for the trail, and a blow taken, a swing that meets nothing, or a
  pause ends it; a Cross Cut that strikes two counts once
- highwaymen: a monster with art of their own and every clip a monster plays,
  the size of a person, quicker than an orc grunt and well short of a raider;
  at least six loiter by the path on the Whisperwood Trail at levels two to
  four, on open ground, and more along the Sunken Road; the Mossvale board
  posts a daily against them; and a level 12 fighter with a bronze sword beats
  one with most of their health left
- the quest tracker counts what is carried on a gathering stage, says
  Complete once it is, and says only to bring the goods back on a deliver
  stage, with no count
- affinities: the hero favours the blade, the warden the bow and the wayfarer
  the staff, a tenth harder and eight points truer with it
- the ancient magic: six spells that come in order and take every shape,
  outside the elements' cycle; Eldritch Blast is taught by the magister once
  and the rest are tomes the copying room sells; the college's hall loads with
  the magister and the circle in it and Fernhollow has a door into it; 5 does
  nothing until the magic is known, then chooses it and turns its pages; the
  Eldritch Blast is one bolt that passes through three bodies, Magic Missile
  three darts, and a spell above the caster's level is refused; the page
  chosen survives a save
- the combos at range: with a bow a heavy after a shot is a Split Shot of
  three arrows and both buttons a Twin Shot of two; with a staff a heavy after
  two casts is a Cascade of three bolts, for more mana
- inventory, equipment, skills and quest progress survive a save round-trip
- the hero has every clip including sprint, each split into shadow, body and
  head with its sheets on disk, and its head and feet sit where the CraftPix
  rig's do so worn armour lines up
- a sprint, run through the real world update with Shift held, covers about 1.6
  times the ground of a run, plays the sprint clip, kicks up dust and leads the
  camera; a rig without a sprint clip runs faster instead; an attack stops a
  sprint, and a hit breaks it until the lockout passes
- stamina runs out after the expected number of seconds of sprint, leaves the
  player winded and unable to sprint however the key is held, clears only past
  the recovery threshold, waits for a breather before refilling, refills faster
  standing still than walking, and is full again on respawn
- `J` `K` `L` attack, heavy-attack and lock on, `I` `O` `P` open the panels,
  `J` and `K` confirm and back out of menus, `Z` `X` and the mouse do nothing,
  and the prompts name the new keys
- out of combat an arrow flies straight the way the character faces, with a
  hare nearby or facing up; striking a deer starts a fight with it; in combat
  an arrow is loosed at the monster and the character turns to shoot it
- an arrow aimed at a monster off the line steers into it, and the same arrow
  with no target flies past
- `L` locks the nearest, steps to the next, lets go past the last, and a locked
  deer is shot at although it is not fighting; a standing player faces the
  lock; the lock lets go out of range, on death and on a map change; a sword
  turns to a monster beside the player but not to one out of reach
- a bow and a shield cannot be worn together, the swap works both ways, and a
  full bag refuses a swap rather than losing an item -- including the case where
  the only room is the slot the new item leaves
- every recipe that needs metal is smithed at the anvil and every other recipe
  is made at a workbench, and every crafting object in the world is drawn as
  the station it works as
- the doors to the mine and the barrow warn a new character, and the way to
  town and the Whisperwood do not
- the drowned king's boots: worn on the feet, carrying a passive, in no loot
  table, no shop and no reward list; the chest that holds them names them
  itself, is gated on the quest, and is the only place in the world they exist;
  and the quest is Orlend's, only after the barrow, with opening that chest as
  its middle stage
- the world map: its data is written beside the maps and drawn to the size of
  the overworld; every mark is named and lies on it; the dungeons, every way out
  to another land, the town, the graveyard and the camp are all marked; each
  dungeon mark stands at a dungeon door and each path mark at a real way out;
  every trade in a marked town has an icon and a legend line
- Hollowrest: it is ground of its own with its fence, gate, crypt and a field
  of markers; zombies, skeletons and wraiths walk in it and the wight holds the
  crypt; none of them has wandered outside the fence; each has its own table,
  and those tables say what they should -- a zombie leaves flesh and money and
  nothing else, a skeleton leaves bones and nothing else, and everything a
  wraith leaves is worth carrying to a trader
- the dragon and its quest: Hoarfang is a boss, stands above everything else on
  the Ice Spire, holds its own ground there, exists exactly once in the world
  and has all five clips drawn; the hunt is a story quest from Elder Vask that a
  new character cannot take and a Combat 40 one can, made of a climb, a kill and
  a fang carried back; Vask is in the guild hall, offers it only at Combat 35,
  and grunts at anyone else, in art of his own with both its clips drawn and the
  floor under his chair blocked
- the journal's three tabs: every story quest is off neither a board nor a
  repeat, Maren's chain and the dragon are on the story tab, the three trades
  are on the tutorial tab and named for the skill each teaches, the contracts
  and the daily orders are on the side tab, and nothing off a board or anything
  repeatable can reach either of the other two
- the well under Havenbrook: the square has a well with a way down it, both
  floors are marked dark and neither is lit by anything but what is carried;
  the upper workings hold only slimes, rats and bats and the deep cut only
  hounds, ankous and banshees, one kind to a chamber, none spawned in a wall,
  none with an aggro range over 175 or a leash that would drag it out of its
  own room; the lantern chain hangs together -- the unlit one needs a recipe
  and is smithed from bars at an anvil, the tinderbox turns it into the lit one,
  the lit one is worn in the off hand and is the only worn thing in the game
  that carries a light; and the quest is Bess's, made of the lantern, the climb,
  the thing in the spring, the stone in the outflow and the walk back
- the three trades taught in Havenbrook: the sawpit's stand of oak, the pit's
  copper and the pond's casts are all worked at level 1; the camps have their
  props and the pond is water; each teacher stands in the town, gives a quest
  that needs nothing first, lends a tool a beginner may actually use, asks for
  a load gathered and carried back, and hands it over only when the whole load
  is in the bag -- and the quest completes with the tool kept
- twelve tiers in order, each making all eight pieces with a recipe at the right
  station; every piece stronger and dearer than the same piece a tier down and
  needing its tier's level in the right skill; every metal tier with an ore and
  a bar and a smelting recipe; every ore mineable somewhere at its tier's Mining
  level; the old item ids still resolving as tier pieces
- all 112 tier icons are different pictures, every tier weapon has a layer sheet
  for every hero clip it can play, and all 48 look different in the hero's hand;
  every tier has a spear that reaches over one and a half times as far as a sword
  down a narrower line, shoves harder, is slower, strikes with the thrust clip,
  hits a monster a spear's length away that a sword cannot reach, and is smithed
  from its tier's bars; a new
  character cannot wield an iron sword, can at Attack 10, and then holds its model
- each style's tree is three branches five deep with rising milestones and three
  techniques; points come every five levels, nodes need their level, the one
  above and a point, and unlearning gives them back; a melee node helps a sword
  and not a bow, and a global one helps both
- in real fights: a whirlwind strikes all four deer round the player where a
  plain charged swing strikes the ones in front, a lunge carries the player
  forward, a volley looses five arrows, a piercing shot passes through a crowd,
  arrow rain and meteor call strikes down; an arrow rain is seen coming, comes
  down for two seconds and more in seven volleys, hits what stands under it
  again and again and what stands outside it not at all, catches what walks in
  half way with the rest of it and only the rest, stops when the arrows stop,
  and is gone; Take Aim is its first volley and not the other six; a rain
  crosses the line as a rain; a nova bursts into eight bolts for
  twice the mana less Focus, flurry quickens a sword and not a bow, and learned
  nodes survive a save
- the clock's dusk only darkens, dawn is half light and warm, half a minute is
  an hour, midnight turns the day, a bed takes you from 19:00 to 04:00 and a
  dream is over at 05:00, and skipping to dawn lands on the right morning
- there are beds indoors and campsites outdoors, the dreamworld has an arrival
  point, one waking stone, dream crystals, tinted nightmares and no portals, and
  the bedroll, dream shard and dreamcatcher resolve with icons and recipes
- by day the inn's bed refuses and does not ask; at night it asks, and backing
  out leaves the evening as it was; choosing the Reverie puts the player to
  sleep, the dream remembers exactly where, health is restored, dawn wakes them
  in that spot, dying in the dream wakes them alive and costs the night, and the
  waking stone wakes them in the dark
- choosing to sleep the night through never leaves the room: the player wakes
  where they lay down, at dawn of the next day (or the same day, after
  midnight), with the quest day turned over, health and mana whole, and the
  waking reported once as a night slept through; nobody sleeps with an orc
  nearby, either way, and a camp asks the same question without packing up
- a bedroll pitches a tent and a fire, the camp stays on its own map, offers
  sleep at night and packing up by day, and cannot be pitched indoors
- noon is untinted, midnight dark and blue, sunset warm; the player carries a
  light at night and nothing is lit by day; houses are brighter than outside and
  lit by their hearths, and the mine ignores the hour
- a save made in a dream loads still dreaming, at the same hour, with the way
  back and the camp remembered
- Smithing, Foraging and Brewing: workbench, anvil and cauldron train Crafting,
  Smithing and Brewing; every bar and metal piece is smithed at exactly its
  tier's level; an old save's Smithing starts at its Crafting; every herb has its
  own level, world art growing and picked, and grows in at least a dozen places;
  on the overworld every herb stands on its own ground and has a thick patch;
  moonpetal and starlily grow only in the Reverie and glowcaps mostly in the
  Whisperwood; picking a marigold in the world plays the gather clip, gives the
  herb and XP, leaves it bare until it regrows, and is saved; a glowcap refuses a
  beginner; a master picks two about half the time; every brew is a potion that
  does something, takes one vial, is brewed at its rarest herb's Foraging level,
  and has a recipe someone teaches or sells; Oona teaches the first brew once;
  every general store sells vials; a Nettle Brew lifts Strength, a second is
  refused, the boost wears off; food and potions are kept when they would do
  nothing
- every tier makes an axe and a pickaxe, each faster than the tier below and
  needing that tier's level in Woodcutting or Mining, at the right station, with
  its own model; the fishing rod is made at a workbench; the hero has chop, mine
  and fish clips, with every axe, pickaxe and the rod drawn in hand through them
- the gathering arithmetic: level and tool both speed the work, which never
  goes under a floor; fishing milestones start at 20 and only improve, a catch
  below 20 is always one fish, and at Fishing 40 about one in five is two; a
  level 1 fisher only lands minnows, a level 30 one gets pike, trout and minnows
  and nothing above the level; every fish cooks into food
- there are at least eight fishing spots across the pond, the stream and the
  lake, each with real fish and reachable from dry land
- in the world: a tree with no axe says so and gives nothing; a bronze axe
  gives logs while the hero chops and holds the axe, and stops when told; a
  demonite axe fells the same tree much faster than bronze; an iron pickaxe is
  no use at Mining 1, a bronze one mines copper with the mining animation, and
  walking away stops it; the pond wants a rod, a level 1 fisher with one lands a
  minnow and trains Fishing, and at Fishing 99 some casts land more than one fish
- every one of the 39 sounds is audible, short, finite and under clipping;
  each ambience, the dream's and a night outdoors included, is audible, stays
  in the background and fades out when cleared; forty hits at once are
  voice-capped and never exceed full scale
- co-op M0, bytes: a writer writes exactly the widths asked for, little-endian;
  reading past the end fails and yields zero; a string past its limit is
  refused, not truncated, and a length that lies is caught; every message
  round-trips, no truncation of any of them decodes, nor does one with a byte
  left over, and none decodes as another; a greeting from another protocol
  version still reads far enough to be refused by number; a chat line is
  trimmed, loses its control characters and is never cut through a UTF-8
  character; addresses split into a host and a port, and nonsense is refused
- co-op M0, data: `data/` and `maps/` are found and hash the same twice; line
  endings do not change a hash, and one hit point, a renamed file or an extra
  file does
- co-op M0, the loopback transport: dialling nobody is answered with a
  disconnect; packets arrive in order, from who sent them, on their channel; a
  disconnect arrives after what was sent before it and both ends are told; a
  delayed hub holds packets back and a lossy one loses unreliable packets and
  never reliable ones
- co-op M0, the door: the host's own client knocks like anyone else and is
  given the first seat, marked as host; a friend is seated beside them and
  both screens say who is connected, the same as the server does; a typed line
  appears on the other screen with the right name, and comes back to its
  writer as the server saw it; another protocol version, different data,
  different maps and something that is not DreamQuest are each refused by
  name and seen by nobody inside; speaking before the greeting is refused and
  then dropped; a silent connection is dropped after five seconds and not
  before; four friends get four seats, two Sams are Sam and Sam 2, a fifth is
  told the world is full, and a seat given up is the next one given; chat
  survives a delayed, lossy line; when the host stops every friend is told;
  a door that never answers is given up on after eight seconds
- co-op M1, hands: a player's hands are read off the device, and a tap shorter
  than a frame is pressed and released with nothing held; an axis and a step
  cross the wire as exactly the numbers they were taken with; six hundred
  uneven steps of wandering, sprinting, swinging and jumping leave a guest
  stepped by a host and the same character played locally in the same place,
  facing, clip and frame, to the last bit, and the host, whose hands were
  empty, where he was; the world says who is nearest a point; a guest is kept
  across a map change and arrives where the host does; a guest's window has
  the map and its people and no monsters, does not go through doors on its
  own and does not sleep the host's night away
- co-op M1, messages: input frames (seven bytes a step, clamped on arrival),
  snapshots, enters and outfits round-trip exactly, and no truncation of any
  of them decodes
- co-op M1, a host and a guest: a friend who is seated is told which map to
  load and loads it; the host's world has her character in it and hers has
  him as a puppet, each with the right look and the right weapon in hand; she
  walks on her own screen the frame she presses the key, every step is sent,
  taken and acknowledged, and the host's copy ends exactly where she did with
  nothing to put right; the host's puppet trails him by about a tenth of a
  second and comes to rest where he is; a swing crosses the wire, and so does
  a jump tapped inside one frame; on a line that loses a third of its packets
  and delays the rest, the repeats carry every step across; a small
  disagreement is put right at once and a larger one closed over a few
  snapshots; what she wears and carries reaches the host's copy through her
  sheet, with nothing sent back as a gift
- co-op M3 and M4, in the same town: what she drops lies in the host's world
  and her window, she does not scoop it straight back up, and the host walking
  over it has it; E on someone opens the conversation on her screen and not
  the host's; when the host goes through a door she stays, in a world of her
  own, stepped as before, and he leaves her window; walking into a way out
  takes her through to a third map; the host following finds her where she
  stood, in one world again; when the host leaves the world she is sent to the
  lobby and brought back when he returns
- co-op M5, dropping out: a friend whose line drops stands where they were,
  out of the fight, and the host keeps a copy of her character and her place;
  coming back she is put where she left off, not beside the host, and the
  stand-in goes; if she does not come back it is let go when the grace is up,
  as is a map nobody is on after a minute
- co-op M2 to M5, out on the road: a world with a password turns away whoever
  does not have it; the host's copy of her has her levels; the host's boar is
  in her window where the host has it, goes for her who is nearest, and
  nothing beyond the relevance radius is told; her swings, made on her
  machine, kill the host's boar, aimed by her window's targeting; the numbers
  were on her screen, the experience is on her own character, and the kill
  counts in her journal and the host's; how hurt she is is what the host says,
  a wound the host deals shows on her screen and what she eats heals the
  host's copy; what the boar dropped lies in both and walking over it puts it
  in her real bag; an arrow she looses is the host's arrow, marked as hers; she
  is seen to chop, with the bar filling, and the logs and the Woodcutting are
  hers; a chest she opens is open for everyone; if the host's copy of her falls
  she falls, and is got up in Havenbrook, whole; in company sleeping through is
  lying down and dawn does not come while a friend is up, comes for both at
  once when she lies down too, and comes when one is abed and one dreaming, the
  dreamer waking where she lay down; a bored sleeper gets up with one press; a
  character is written to its own file and read back with its levels, bag,
  clothes and journal, a travelling one in one file and a world's own in
  another, and the sheet leaves out where she stands and how hurt she is

- split screen: the keyboard is Player One's and Player Two's input does not
  hear it; a press on Player Two's controller is theirs alone, with a
  controller's prompts; borrowing answers every question from the other
  player's hands and giving them back restores it; a controller that is neither
  player's is heard by neither; someone on the host's couch has a seat in the
  roster with no line, a friend across the wire sees who they are, and the seat
  is free again when they get up; Player Two arrives beside Player One as a new
  character, dressed and provisioned, with a seat that is looked through and a
  real journal; their hands move them and nobody else, and their own camera
  follows them within their half; serving Player Two, `player` is Player Two,
  and handing back everyone is themselves again; a recipe they learn is theirs
  and not Player One's, a chest they open is open for both; a kill counts in
  both journals; Player One going into the inn leaves Player Two in Havenbrook
  on a map of their own, walking on with their camera; when both lie down it is
  dawn for both; fallen, Player Two is got up in Havenbrook, whole; and sitting
  down again with their kept character they are who they were

- every character holds the weapon they are holding: the warden and the
  wayfarer take the weapon in hand from the hero's renders, and every sheet the
  game will ask for -- each starting weapon and what they might pick up,
  through every clip played with it -- is on disk
- skill trees, the path: each tree is three branches eight deep -- two passives
  of three ranks, a technique, an ability with a cooldown and a cost, a passive
  of two, a second ability, a second passive of two and a capstone, forty-two
  ranks against thirty-three points by level 99; past the first ability no row
  is more than eight levels after the one before; every effect a node names is
  one the game reads; a rank is a point, two ranks are twice one, a full node takes no more,
  ranks survive a save and a save from before ranks has one of each; a
  character's path opens one tree and nothing can be learned from another's,
  and a save from before the paths keeps what is its path's and loses the rest;
  a learned ability goes into the first slot with room, then the second, the
  third, then away, survives a save, and unlearning the tree empties the slots
- abilities in the world: guard and light bashes what is in front, costs breath
  and starts its cooldown, the press is the ability's and not a swing, pressed
  again too soon nothing is spent, and without the guard held the button still
  swings; Sunder leaves a monster's defence down by a third; a war cry staggers
  what is near and adds a quarter to melee damage for eight seconds; Riposte is
  owed for three seconds after a block, and only to a hero who has learned it;
  a tumble is untouchable and a blow mid-roll lands on nothing, and standing
  still it goes back; Hunter's Mark marks what is in reach; caltrops lie for
  six seconds stopping what crosses them; a blink lands somewhere that can be
  stood on, and with nowhere to land does not happen and costs nothing; an
  arcane pulse is ten bolts of the wayfarer's own; a mana shield pays half a
  blow in mana, two a point, and with none to pay all of it is blood;
  Attunement deepens to five and starts again with another element
- the deeper rows: a strike from above lands once on what it lands on; frenzied,
  the blade is a third faster and the chain does not lapse, for six seconds; a
  wound bleeds what it owes over four seconds, and a chain three deep opens one
  and not before; a shockwave reaches what is straight ahead, near and far, and
  nothing beside or behind; guard and lock on is the third slot and the target
  stays who it was; feet set, a blow of twenty is a blow of twelve and moves
  nobody, for six seconds; what is called out is after whoever called it until
  it wears off; a held breath goes into the next shot -- critical, half as hard
  again -- is spent by it, and the one after is plain; Weak Point counts to four
  and starts again on another target; rapid fire is two fifths faster for five
  seconds; standing still nothing slips past and on the move about one blow in
  eight does; a snare holds the first thing into it and is sprung; overloaded,
  the next spell costs nothing and hits twice as hard; with no Spell Echo a bolt
  is one bolt and with it about one in six is two; Invoke with nothing to draw
  back costs nothing, and with mana spent returns half of it over four seconds;
  Deep Well is a fifth more mana; a repulse leaves everything near reeling and
  thrown back, and nothing far; Resolve gives back three mana a rank
- where a blow lands: a swing reaches as far as its reach and half the width of
  what it meets, and no further; not what stands beside the character, or
  behind; as far up the screen as down it and as far as across; the Cleave from
  shoulder to shoulder and not behind; the arc drawn and the arc struck ask the
  same question; a full turn reaches behind; every monster's swing lands on
  whoever stands still at the range it swung from; a burst on the ground
  strikes what stands inside its circle and not the corners of a square round
  it; a blade does not reach up or down a cliff two levels high
- the ground lifts everything on it: a number over someone's head, and what is
  drawn there; stepping up a level the lift closes on the ground's rather than
  jumping to it, and is there within a third of a second; a friend's window is
  told where the host already put it
- people with somewhere to be: Havenbrook has people walking its streets; every
  round ends where it began, and no step of any round is inside a wall; two
  machines put the watchman in the same place, and he walks; spoken to he
  stands still, let go he is not flung to where the clock has him but hurries
  until he is back on his round; at night the streets are the watch's, and
  nobody who has gone in can be spoken to
- three kinds of armour: every tier has a ranger's hides and a mage's robes,
  seventy-two pieces; plate adds to a blade, hides to a bow and robes to a
  staff, and none of them to anything else; plate keeps out the most and robes
  the least; each has its icon and its own cut; hides are cut from the tier's
  hide and robes from cloth and the tier's dye, at a workbench, at the tier's
  level; eleven hides and something drops every one; a bolt of cloth from flax
  or from spider silk; there is a dye for every tier of robe, brewed at the
  level of its herb with no teaching and not for drinking; and both cuts are
  drawn on all three characters
- the Westwold and the Brackenwood: they load, take every check every other map
  takes, and neither is small; wolves on the downs and greatwolves in the Fells;
  bears, one Den Mother, and dire bears in the Old Growth; Havenbrook has a west
  gate and the road comes back to it
- what starts a fight and what ends one: a boar does not notice someone three
  hundred pixels off, and hurt from out of its sight it comes for whoever did
  it; someone inside its range is a fight without a blow struck; a chase does
  not end for being further from its post than its leash is long, and does end
  when it has covered its budget with nothing happening; walking home it turns
  on anyone who steps close; hurt along the way it keeps coming for twice as
  long; and a swing begun starts the count again
- a page for wherever you are: every map says what it is called and whether it
  is country, a dungeon or a room; open country and dungeons each have a page
  of their own; a room's page is the place the room is in, with the dot on its
  door, and upstairs at the inn is still the inn's door; the Hollowmarch is the
  other side of every page but its own; from the Hollowmarch the way to
  anywhere is the road that starts towards it, and no road leads to the
  Reverie; everything marked on a page has a name and is on the page;
  Fernhollow's page has its two traders by their trades, its two doors and the
  way back, Havenbrook's its four doors, the well, both gates, the benches and
  the traders, and the Ashen Path's the way back and the pit
- bags: four of them, a row of seven each, twenty-eight slots to fifty-six;
  each is a bag and not worn, eaten or stacked, has a picture, is made at a
  workbench out of twenty-five things or more that all exist, and asks more
  than the one before it; each is in a chest somewhere at under six in a
  hundred, the best not by the road and the least not at the end of the world;
  a sword is not a bag; a satchel goes on and the bag is a row bigger and a
  satchel lighter; a second satchel stays where it is and says why; the new row
  holds things; a save says which bags and holds every slot, loads as big as it
  was with the last row intact, and so does the copy a friend's host keeps;
  another character loaded over them does not inherit it; a save from before
  bags loads as it did; all four go on in any order and stop at eight rows; a
  bag named twice in a save counts once
- the Reverie goes down, and is never the same twice: three depths that each
  know how deep they are, a ladder each way between them that is climbed on
  purpose and says what it leads to, a waking stone, a chest and crystals at
  each; nine platforms, then more, then more again, each map bigger than the
  last; every post with a pool has a choice, a group and a fallback, and
  everything in a pool is a real monster, tinted, hostile, a nightmare to the
  slate, and always leaves a shard; the easiest thing at each depth is harder
  than the hardest thing above it, what guards the way on is worse than
  anything on the way to it, and no monster belongs to two depths; asked twice
  on one night a post gives one answer, from its pool, at a level it allows;
  the posts on a platform agree; it is a different dream nearly every night of
  a month and every platform is kept by more than one kind of thing in it; a
  post never given a pool is what the map says; two machines that agree on the
  day agree who is there, before midnight and after, and not the night after;
  a guest's dream is kept by the same things as the host's copy of it; a
  nightmare leaves one, two and three shards by depth, once a kill and not once
  a stack, and nothing else is multiplied; awake there is no bonus; a sleeper
  climbs to the bottom still asleep upstairs at the inn, where it is darker,
  and dawn or the stone wakes them in their own bed
- a monster's level is what it fights like: sixteen creatures read within the
  band their stats put them in, a bear outranks a boar and a dire bear a bear, a
  stronger spawn reads higher than a plain one, every creature in the game reads
  as a level a character could be, and a dire bear keeps its own hit points and
  profile while saying Combat 73; every advised area is within a dozen levels of
  the middle of what lives there, and the Hollowmarch is mostly things a new
  character can fight
- a tannery, and a way to train Crafting: Nessa keeps a yard in Havenbrook with
  a bench in it and a shop; her book holds a dozen orders, each a daily delivery
  to her of something that can be made at a workbench, asking the Crafting its
  own recipe asks and paying in Crafting; there is work in it at Crafting 1 and
  work at 46; three are posted a day, the beginner is posted three they can do
  and the master three others; and she will show the book, the shelf, take an
  order in and say how the trade is learned
- cooking, and what a dish is worth: the fire has a menu with every raw thing
  and every dish on it, each cooked at a fire for Cooking XP out of real
  materials; raw meat still cooks as it always did; a dish lasts minutes rather
  than seconds, is worth eating for something, and is a dinner and not a potion;
  between them they lift health, mana, breath and the three ways of fighting;
  eating a stew makes the health pool bigger and nothing else, tea after a stew
  replaces it and lifts mana and Magic, what it lifts does not drain away while
  it lasts, and when it wears off the pool and the levels are what they were; a
  dish is worth eating at full health and plain food is not
- a clothier, a farm, and frogs in the mire: Wynn keeps a shed in Mossvale with
  her loom and a shop that buys cloth and what cloth is made of; her book holds
  nine orders for robes, hats, skirts and cloth, each asking the Crafting its
  own recipe asks and paying in Crafting, from the first bolt to the upper sets,
  and none of them is the ranger's; Havenbrook is wider than it was and its pens
  hold hens, ewes, pigs and cows, none of which comes for anybody, each worth a
  supper, a fleece or a hide, with a farmer in the yard; a fleece spins into
  cloth for the shed and everything the farm gives cooks; and there are frogs in
  the mire, which sit there
- the loom and the tanning rack: every recipe belongs to exactly one of the six
  stations; what is woven is what comes off the loom and not what goes in, so a
  bolt of cloth is only ever woven, all thirty-six pieces of the mage's sets go
  with it, and a dye is boiled; what is leather is cut on a rack the same way
  -- all thirty-six pieces of the ranger's hides whatever else is in them, the
  jerkin, the boots, the bags and the bedroll, and no plate and no robe -- while
  a Barkwood Helm has a hide in it and is still made at a bench; both tanners
  have frames to work at, no carpenter's bench, and no frame that is only
  scenery; every order in Nessa's book is made on her own frames; the rack
  trains Crafting, a map that says "rack" gets one, and it is drawn as one;
  the loom trains Crafting the way the bench does; a map that says "loom" gets
  one, it is drawn as one, and there is one standing in the world
- what each blow trains, and other things that were quietly broken: Attack
  moves whether a blow lands and nothing else, Strength the top of the damage
  roll and nothing else; a light swing trains Attack, a heavy one Strength, a
  charged one both, and in a real fight from the button to the skill; a
  monster worth double pays double; a minute's chopping with a full pack
  teaches nothing, with the axe seen to swing, and with room the log and the
  experience both come; the Cross Cut needs the breath it spends; being healed
  leaves a potion's boost and puts back what was drained; a save keeps the one
  before it, is shown and loaded from that when its own file goes bad, never
  lets a bad file become the backup, says Damaged rather than Empty, and is
  put aside rather than destroyed when deleted -- all of it in a temporary
  directory, with the real saves where they were
- a lesson is a quest, not a button: no line of dialogue anywhere hands out
  experience; the fighting, crafting and cooking lessons are tutorials, given
  by who teaches them, done once, that ask for the skill to be used and end
  back where they started; each is played through -- asked for, taken, asked
  for five more times for nothing, refused early, done, handed in, and asked
  about three times afterwards for nothing but words -- and is completed
  exactly once; an action that starts a quest hands over what comes with it
  once and the same action ten more times hands over nothing; the lent axe,
  pick and rod and Oona's tonic are each replaced once however often they go
  missing, and not at all while still in the bag; every line that gives
  something is a quest's to give or is given once; a Barkwood Helm is made at a
  bench by a beginner out of exactly what Halda hands over, meat is cooked at a
  fire out of what Ivo does, meat already in the bag is not meat cooked, and
  both lessons point at somewhere in Havenbrook to do them
- what the menus say a thing is worth: every potion in the game gives exactly
  the boost the panel promises, at the level the drinker is -- the number is
  worked out rather than stored, and the panel and the draught read one
  expression; a plain helm is one row and not five zeroes, a stat you would
  lose is shown even though the new piece has none of it, an empty slot reads
  the whole bonus as the gain, a piece weighed against itself changes nothing,
  and a quicker weapon reads as better although its stored number is smaller;
  and every piece that can be worn makes at least one row, so a card is never
  drawn empty
- what the same review changed about playing it: a heavy blow is softened by
  armour, never past the cap and never to nothing, and an average blow from the
  Warchief no longer costs more to block than a bar holds; a quick item is the
  first food in the pack until one is chosen, steps through what could be it,
  is eaten from the pack, and is in the save; two healing things taken one
  after the other are one eaten and one refused until the wait is over, and a
  potion that does not heal is not held up; the inn's beds have a price, nobody
  else's does, and the price reaches the prompt; a boss killed today is lying
  dead in its place in the list when its map is loaded again, an ordinary
  monster is not, the boss is up again the day after, and all of that is true
  across a save
- a spell is paid for when it lands, and a wall teaches nothing: a new wayfarer
  stood in Havenbrook square casts into the waystone forty times with the mana
  put back each time -- the bolts are seen to fly and to break on the stone --
  and gains no Magic experience and no level; bolts and novas into an empty
  field teach nothing and are forgotten once what they left has burnt out; a
  bolt in the air says which cast it is and the cast is owed; every bolt that
  lands on a cow pays the spell's experience once on top of what the damage
  pays, to the point, and one that reaches the cow and does nothing pays
  nothing; a fire bolt and the ground it leaves burning hurt the cow several
  times and pay for the spell once; a nova that hurts several cows is one cast;
  a meteor carries its cast down with it, pays once on a cow and nothing on an
  empty field; and an arrow owes nothing, because a bow never paid for an arrow
  that hit nothing
- what comes out at night: only the four wild maps have night posts, written
  after everything else in the list; each is a pack's post of real monsters
  that fight, kept some nights, never respawning; nothing of a visitor's kind
  stands within twenty cells of it by day; every visitor is stronger than the
  average of what lives round its post, and none by more than a couple of
  dozen levels; posts stand on open ground, eleven cells from every way in,
  camp and person, and none within a screen of Havenbrook's gate; what is
  abroad is never more than a fifth of what lives there; half the posts are
  kept on a night, not the same half two nights running, and a pack comes or
  stays away together; played through -- by day every one is in the list and
  none is there, at eight those that are due are up with the list no longer,
  nightfall says to keep to the road, one killed is remembered, does not come
  back in forty seconds or through a door, at dawn the rest go to ground and
  leave nothing, and on another night of its own it is back; one in a fight at
  dawn finishes it; a town's nightfall is as it was; and no contract points at
  a post that is only kept after dark
- what a boss leaves, the first time: every boon names effects the game reads,
  and every path has one for every boss; the first kill is a point and a boon,
  the second nothing; the point buys a rank and comes back when the tree is
  unlearned, the boon is kept; eleven bosses leave eleven different boons, none
  for another path, and thirty heroes are never once given mana or arrows; it
  survives a save, a boss killed before the save is not a first kill after it,
  and a forged save gets a boon for each boss and no more; Vigour, Stoneblood,
  Long Wind, Deep Reserves and Might each move the number they say by what
  they say; in the cellar a rat leaves nothing, the Broodmother a point and a
  boon and two short lines saying which, and the day after she is only a
  fight; and in company the host who had killed her gets nothing, the friend
  on the couch gets theirs and is the one told, the friend down the wire gets
  nothing on the host's copy, is sent the kill with which boss it was, is given
  it on their own machine, and the next sheet tells the host
- a totem for the fifteenth, and the ring it stands in: every boss has a totem
  and there are no others; each is a thing in the bag that cannot be sold,
  dropped or eaten, has a picture, and says where it goes; every effect is one
  the game reads and is more than any boon of the same kind; twenty Warchiefs
  leave one totem, on the fifteenth, and one boon and one point, on the first;
  the count is in the save, and an older save's bosses count as killed once;
  only a totem will stand in the ring; stood in it, the Warchief's is a tenth
  more damage with anything, all that day, and at dawn is a carving giving
  nothing until a hand wakes it; another stood in its place sends the first
  back and leaves one blessing, never two; it is on top of the boons, is in the
  save, and lifted out takes its blessing with it; fifty health is sixty under
  the Sleepless's; in the world fourteen Broodmothers leave nothing, the
  fifteenth one totem and two short lines saying where it goes, ten more no
  second, and a full pack has it put at its owner's feet; only the house at
  Mossvale has a ring, in the middle of the room with clear floor all round
  and over it, which asks to be touched and asks the game for its panel; the
  blessing goes out of the door, is still there at midnight, is over at dawn
  with the health it lent given back, the totem still standing at home and
  saying it is asleep; and all of it survives a save
- a new game starts with a new world: with forty bars in the chest, a chest
  looted, a boss dead and nine days gone, a new game finds the storage chest
  empty, no boss dead, nothing opened, no camp, and nine in the morning of the
  first day; two saves made from one world in one sitting each have their own
  chest and none of the other's, in either order, and loading one over the
  other leaves nothing behind
- what a spell looks like in the air: each of the eight elemental bolts is
  drawn as the thing it is (fireball, water orb, stone shard, gust) from
  `assets/effects/`, as a strip of whole frames held at a point inside one,
  in its own colours with no element's tint over it, pixel for pixel, the
  greater of each pair the bigger, and what is upright no bigger than about
  what it hits; water has a wake and a fireball a glow; an arrow is a still
  and sheds nothing; every bolt sheds a trail, and leaves it behind it; on the
  waystone each breaks a dozen motes at once; burning ground stands in tongues
  of flame; a fireball over one field and an arrow over another leave the
  game's dice in the same place; seven hundred and twenty fireballs are seven
  hundred motes; and a friend's machine sheds its own trail from the shots it
  is told of, and breaks one it stops hearing of where it last was
- the armoury: every tier has all nine new weapons and the four elements'
  staves (156), each with its picture, its model in the hand and a recipe; a
  dagger is quicker and shorter than a sword and goes past armour, and against
  something well guarded its blows land more often; a mace concusses; the great
  weapons take both hands, are slower, reach further, sweep wider and bleed,
  and a greataxe's charged heavy is a chop of its own; a greatsword takes the
  shield off and knives and a shield go together; a crossbow throws a bolt at
  once, is then being spanned, and nothing can be let off until it is; a light
  throw is one knife and a heavy one three; the casters' weapons are quicker
  than a staff for less, about even over time; each melee weapon has four
  combos by name; every element has a spell on each of four slots; a fire
  staff's four keys are four spells and it casts nothing else; the Flame Ring
  is a ring, the Wall of Fire a line across the way faced, the Flamethrower
  five wide or three far; the Hydro Cannon throws, the Tidal Wave is seven, the
  Whirlpool drags to its middle; the Slabstrike hits what is in front and
  not behind, the Mineral Burst is eight, the Sedimentary Rain a rain of stone;
  a Tornado walks, lasts four seconds held, throws and hurts, and Turbulence
  goes where its caster goes; RB is the abilities' shift and Select the menu,
  an old saved pad layout is not applied, the guard and the light attack is no
  longer an ability and the shift and the light attack is, and on the keys the
  guard is still the shift; and every one of the 156 weapons looks different in
  the hero's hand, striking
- statuses: all seven load with a name; fire can leave a burn, water soaks,
  stone concusses and the wind leaves nothing but throws further than any of
  them, the greater of each more often and neither always; every tier's sword
  can open a wound (by what is in the hand: the steel one is a longsword and an
  enchanted sword is a sword), no spear, bow or staff can, and the Ember Blade
  burns; every monster has a Defence, and a blow that lands on a soft one
  misses a hard one; a burn is half the blow over three seconds, two burns are
  the greater and two bleeds the sum; water puts a burn out and keeps one from
  taking; the wind bites what is soaked; a chill on something soaked is frozen,
  held, thaws into a chill, slow in leg and arm, and wears off; a concussed
  skeleton is hit more often by the same hand; the dead do not bleed, fire does
  not burn, a boss is poisoned for half as long and never held; of sixty bolts
  of each element some leave their status and sixty gusts none, every one of
  them throwing what it hits; seventy cuts open a few wounds and seventy
  thrusts none; with no statuses loaded nothing is rolled; Acid Spray is five
  gouts that mostly poison, every Ice Touch that lands chills, freezes the
  soaked and does not cross a field, half of what the Vampiric Touch takes
  comes back, the Rebuke is arcane drawn as fire, leaves its target burning and
  is half as hard again as an answer; and the byte of what is on a monster
  crosses the wire and a friend's machine draws the same
- the spellbook, and the fifth slot on a pad: with no ancient magic known four
  steps of *next element* are fire again, and with a spell learned and `5`
  never pressed the fourth step is the fifth slot, with that spell on it, and
  it casts; the spell the book put on the slot is the one stepped to; fire at
  Magic 25 is Pyre, and held to Ember it is Ember, cast as an Ember for an
  Ember's mana, with water none the weaker; an element cannot be held to
  another's spell, an ancient one, or one out of its Magic's reach, and the
  fifth slot holds nothing that way; a save keeps what is held and so does a
  friend's sheet; an ability is put in a slot outright, changes places with one
  already carried, and a passive, an unlearned one and a fourth slot are
  refused; the charged attack is set from one technique to another without
  going by way of plain, and not to an ability or an unlearned one; and the
  council is seated -- each in a chair, in front of its back, north of the
  table and facing it
- the College at Fernhollow, and Wynn's at Mossvale: the college is through a
  gatehouse on the hamlet's north side with a porter at it, and the tower is
  gone; the court is out of doors, bigger than the hamlet, and paved in its own
  tiles with no dirt or planks; there is a door in the west wall, the north and
  the east, facing each other, to three different rooms, and the gate south; the
  hall has a wing either side, there is a fountain, four founders, and lamps;
  each chamber is indoors, has the three things that make it what it is, is
  floored in the college's chequer, and lets back onto the court; there is a
  lector, a class and more desks than pupils, and a council of three; in the
  practice hall four of them cast, the bolts fly, every bolt in the room is a
  practice bolt, twenty seconds stood in a lane costs nothing, nothing is left
  burning, watching teaches no Magic and there is nothing in the room to fight;
  Wynn and her loom are not on the square, her door is a long way from the
  anvil, and she is inside selling, with forms, hangings, shelves of bolts, a
  cutting table, her wheel and a counter
- waystones: each of the three towns has exactly one, with its dark and its lit
  sprite on disk and somewhere clear to arrive beside it, and no other map in
  the game has one; the first touch wakes the stone and asks for nothing else,
  the second asks for the panel and says which stone you are at; waking one
  does not wake another; travelling is a transition that ends beside the far
  stone, on open ground, with the stone in reach; and the woken stones are in
  the save
- ducks and geese, and the one pond they can get into: Fernhollow's pond is
  marked as water, a walker cannot stand in it and a swimmer can, and it is the
  only water in the world anything may enter; the birds are posted on dry land
  on a leash long enough to reach it; both swim, neither starts anything, and
  nothing else in the game swims at all; left for five minutes every one of
  them goes in and every one of them comes out, about half the flock is afloat
  at any moment, none of them ends up inside the scenery or wanders out of the
  hamlet, and a hare beside the same water never gets into it; a bird that has
  been provoked leaves the water and is never drawn swimming once it is out;
  both of them cook, both leave supper, and only those two rigs have a swim
  sheet
- a monster on a map the host is not on: Player Two is left in Havenbrook when
  Player One goes into the inn, the world they are left on has a stand-in for
  the host that is absent and holds a seat no real seat can have, and the boar
  beside them comes for them, gets within reach, swings at them over and over,
  takes hit points off them, does not touch Player One indoors, and does not
  change its mind every frame; and with two of them on that map the boar turns
  to whichever it is standing beside and gets them too
- keys and buttons can be moved, and cannot be lost: every action ships on a key
  and, if a pad can do it, a button, its own, with a name for the menu and one
  for the file; Esc, Enter, Backspace, the arrows, Start and the d-pad are kept;
  the prompts and the keys do what they always did; giving one action another's
  key swaps them; a reserved key cannot be given away; four hundred changes at
  random leave every action with a key and a button of its own; bindings survive
  being written down, a file of nonsense is the defaults, and a file that gives
  three things one key still ends with a key each; a moved key does the new
  thing and not the old, the menus and every prompt follow it, a claimed spare
  stops being a spare; a key held across a change is let go; on a pad the menus
  follow the action and not the button, a trigger is bound like a button, and
  the map can go on a back paddle; listening lets go of everything, hears the
  next key once and nothing else does, Esc or Start calls it off without pausing
  the game; and settings keep it, with settings from before it loading as shipped
- quest waypoints: every map is in the index and every way out in it leads to a
  map in it; the roads between maps are the shortest, there is none into or out
  of a dream, and the Reverie's ladders are roads; every Talk, Kill, Interact and
  Deliver stage of every quest has somewhere to point, on a map that exists, and
  a delivery with the goods in the bag points at who wants them; from the
  Hollowmarch the guild master is two doors away and the thing to walk to is
  Havenbrook's gate, inside it the guild hall's door, inside that the man
  himself where he stands; a kill points at the nearest boar and, that one dead,
  at the next, and a kill that names its map is only ever there; no logs is
  trees and ten logs is Jessa, a fish is water to cast at, a hide is what wears
  one; a quest in a dream points at nothing and says to go to bed; the newest
  quest is followed until one is chosen, the choice survives a save and is not
  mistaken for a quest, asking again lets go, and a finished one hands over
- a town entrance is a gate, with someone at it: every road out of Havenbrook,
  Mossvale and Fernhollow is on the test's list; a gatehouse stands across a
  road that leaves by the south and a tower either side of one that leaves by
  the west; a warden or a watchman with no round to walk stands at it, and not
  inside a tower; the middle of the road is open all the way through, and the
  gateway is never narrower than two people
- lightning, and the battery that pays for it: there is an element called
  electric, it stands between the four and the ancient magic where its key is,
  and it neither beats nor is beaten by any of them; five spells, each on a
  place of its own, each carrying the element it throws and a chance -- never a
  certainty -- of leaving what it hits arcing; Zap is the one that fills the bar
  and the only one that does, and each of the five either fills it or spends it.
  Soaking is a weakness to lightning worth a quarter again, and it invites the
  arcing twice over. Played through: the fifth key chooses the school and the
  fifth key again steps through every one of the five and comes round; Magic 12
  reaches Zap and 11 reaches none of it; a zap takes something off what it is
  aimed at, puts five per cent in the battery and draws a bolt between the two;
  zapping on fills the bar and a full one does not overflow; a Discharge empties
  it and is worth what was in it, and on an empty bar goes off neither at all
  nor for any mana; an Electrocute wants its tenth and draws three rays; an
  Electro-Node is left standing, chains without another word from the caster and
  runs down; a Call of Thunder wants three tenths and a charged one half. Forty
  zaps into a soaked orc take more off than forty into a dry one. Resting
  neither fills the battery nor empties it and dying does; a save keeps the
  charge and which of the five is on the key; and a key layout saved before the
  lightning had one gets the new arrangement unless it was moved by hand
- orcs that stand back, and a knife that does not twang: every tier's throwing
  knives are thrown and every bow and crossbow is still loosed, and the two
  sounds are not the same buffer; the slinger and the bowman each throw
  something the projectile table knows, shoot from further off than they can
  reach, notice a player from further off still, have no heavy and no machine
  gun's cooldown. Played through: a slinger looses at a player it can see and
  never closes to a swing's reach to do it. And the ranks are the ranks they
  were -- exactly eighty-five orc posts in the realm, thirty-five of them left
  for the day to settle, and about a third of the mines' and the barrow's
  standing back
- the Bayou, and what waits in its water: the Hollowmarch leads there and back
  and you arrive on dry ground; all ten of its monsters live in it and more than
  fifteen wait under the water; every lurking post in the world is in water,
  swims without paddling about, waits near enough to a bank, a ramp or a deck to
  be woken, and not under a deck's lifted edge; four decks and a bridge stand
  four levels up with the Mother of the Fen on hers, and stepping off a deck's
  edge is blocked. Played through: a Bog Lurker under the water cannot be seen
  or locked on to; somebody passing a little way off does not wake it and it
  does not stir; somebody at the edge wakes it, nothing can touch it while it
  comes up, then it is out and coming for them; somebody who backs off is let go
  and it goes back under, in its water; one that was struck follows, gives up,
  and comes up whole; and a friend's machine draws it under or out by the alpha
  it is told. Two camps behind palisades, huts on the decks on pilings, and herbs
  where the guide drew flowers
- the twenty-five monsters that fill the level ladder are each drawn from a
  sheet of their own, with all five clips and no tint left over it, and the
  three that wait under the water swim low in it
- the Brimstone Palace: the Ashen Path has its door, warned about and shut
  below Combat 60, and its hall leads back out; the moat is crossed on the
  drawbridge and nowhere else; Abyssal Demons and Revenants hold the
  forecourt, on a platform at least 36px up with a stair from the bridge; each
  stream out of the moat is bridged twice, with ore and emberbloom on its far
  banks; the palace's six rooms are in the sweep that walks to everything
  usable; the hall's balconies stand three levels up over a floor-level
  runner, three walkways (thirty-three lengths) cross overhead, and the three
  side rooms' doors open off the balconies; every room leads back to the hall;
  only the palace's own keep it, all of them past the crypt's end; and the
  Cinder King, the top of the ladder, waits in the throne room with the Heart
  of Cinders on his dais two levels up

It exits with the number of failures, so CI can use it directly.

---

## Layout

### Two files that had grown too big

`world.cpp` was 4,331 lines and `ui/screens.cpp` 4,630: a fifth of the game in
two files, which is a long wait every time either is touched and a long scroll
to find anything in them. Both already had banners down them -- *Combat
resolution*, *Interaction*, *Rendering*; *HUD*, *Inventory*, *Shops* -- so they
were cut along those, and nothing else was done to them: every function is the
function it was, to the line, in a file named for what it is about. (It was
done by a script that cuts a file into its top-level paragraphs and sends each
whole to one place, and then counts: every line of the original is in exactly
one of the new files.) A class in C++ does not have to live in one file, and
`World` and `Game` no longer do.

An outside audit of the repository asked for that, and for five other things.
What was found for each, since the next audit will ask again:

| Asked for | What is there |
| --- | --- |
| Split the god-modules | Done, as above. |
| One authority for each behaviour | Mostly so already: `Player` is intent and owned state, `systems/combat` rolls the blow, `World` simulates, `Game` draws and takes commands, `coop/` replicates. `World` never calls the UI: it leaves `WorldRequest`s and the `Game` takes them. |
| Typed gameplay events | The fan-out that matters is one already: a kill is a `QuestEvent` in `World::kill_log`, and `FlushKills` tells every journal at the table, awards the boss, and relays it down the wire. A general event bus would be a rewrite of working code to move the same calls behind a subscription, so it was not done. |
| Static data apart from runtime state | It is. Every `*Def` is loaded once from `data/` and held `const`; an `Enemy`, a `Projectile`, an item in a bag hold a pointer or an id and their own changing state; a save holds ids. |
| Versioned saves | They are: `SAVE_VERSION` in `systems/save.cpp`, read back as `version`, with the one migration there has been (the overworld's layout moved) keyed on it. |
| Tests | 30,000 checks of exactly the list asked for -- damage, loot rolls, inventory, quests, shop prices, save round trips, the wire protocol, the data hashes -- in `tools/selftest.cpp`, which an audit looking for a `tests/` folder does not find. `.\build.ps1 -Test`. |

```
src/
  game.cpp/h            state machine, window, main loop
  input.cpp/h           one action vocabulary for keyboard and gamepad
  camera.cpp/h          dead-zone follow camera with zoom and bounds clamping
  sprite.cpp/h          4-direction animation, data-driven
  texturecache.cpp/h    path -> texture
  world/
    map.cpp/h           .mx loader, chunked render, collision, portals
    world.cpp/h         the World class: the map, the seats at it, sleep and the frame
    world_combat.cpp      ...what a swing, a shot, a cast and an ability do; what a hit is
    world_projectiles.cpp ...what flies, what it strikes, what it leaves on the ground
    world_effects.cpp     ...what spells shed, and sprint dust: only ever for show
    world_interact.cpp    ...reach, open, gather, cook, loot and pick up
    world_render.cpp      ...everything that is drawn, and the light it is drawn in
    targeting.cpp/h     who the player is fighting: combat target and lock-on
    lighting.cpp/h      night as a multiplied light map, with fires cut out of it
  entity/               player, enemies, NPCs
  systems/              skills, items, loot, combat, quests, dialogue, saves,
                        projectiles, elements (element.h) and statuses (status.h),
                        spells, the clock,
                        material tiers (items.cpp), skill trees (talents.cpp),
                        tools, fishing and foraging (gathering.cpp), and traders (shop.cpp)
  ui/                   drawing helpers and every screen; lobby.cpp is Play Together,
                        splitscreen.cpp is two at one machine. The Game class's
                        screens are screens.cpp (the menus) and screen_hud,
                        screen_inventory, screen_skills, screen_journal,
                        screen_talk and screen_trade; screens_shared.h is what
                        two of them use
  coop/                 co-op: where the wire meets the world -- the host's half
                        and the guest's
  net/                  co-op: the transport (ENet, and an in-process loopback),
                        the protocol, the data hashes, the server's door and
                        seats, the client, and the session the game holds.
                        Includes nothing of SDL's or the game's.
tools/
  server_main.cpp       DreamQuestServer: the co-op world with no window
  import_assets.ps1     rebuilds assets/ from the CraftPix zips
  tilecut.cpp           cuts atlases into individual tiles and sprites
  genmaps.cpp           builds the world into maps/*.mx
  selftest.cpp          content and systems validation
  make_ground.ps1       generated ground and interior tiles
  make_decals.ps1       grass tufts, flowers, leaves, pebbles and the like for the overworld's ground
  make_effects.ps1      what each element's spell looks like in the air, as strips of frames
  make_icons.ps1        paints the hand-drawn item icons in icons.txt
  blender_tiers.py      models and renders every tier's ore, bar, weapon and armour,
                        as icons and as weapon layers in the hero's hand
  make_tiers.ps1        runs blender_tiers.py headless (`-What armoury,icons` for the nine new weapons' icons alone)
  make_titleart.ps1     the window icon and the .exe's .ico, off the cover painting
  appicon.rc            the resource that compiles the .ico into the executable
  make_sprites_json.ps1 / make_manifest.ps1
data/                   items, enemies, loot tables, quests, dialogue, sprites,
                        projectiles, spells, shops
maps/                   generated .mx maps, editable in LevelEdit-Plus
art/                    the game's own art: the cover painting and its icons
```

About 8,600 lines of C++, excluding the vendored `nlohmann/json`.

---

## Credits

- Art: [CraftPix](https://craftpix.net) free asset packs — see
  [docs/ASSETS.md](docs/ASSETS.md)
- JSON: [nlohmann/json](https://github.com/nlohmann/json) (MIT)
- Base template: [Dexsidius/SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template)
- Map format: [TheSardonicals/LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus)

Code is MIT ([LICENSE](LICENSE)). The art is not covered by that licence and is
not distributed here.
