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

---

## Assets

**Clone it and run it.** Every image the game loads is its own — modelled,
rendered or drawn by the tools in `tools/` — so `assets/` is committed and
there is nothing to download first.

| What | Made by |
| --- | --- |
| Characters, their armour layers and the town NPCs | `blender_character.py` |
| Every monster | `blender_creatures.py` |
| Props, scenery, buildings, chests, doors | `blender_props.py` |
| Ores, bars, weapons, armour icons, the weapon in hand | `blender_tiers.py` |
| 93 ground and interior tiles | `make_ground.ps1` |
| Ground decals, item icons, the HUD | `make_decals.ps1`, `make_icons.ps1`, `make_ui.ps1` |

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
down between two moments, `--say "a line"`, and `--shot file.png 5`. A scratch
game can start anywhere and in anything: `--map brackenwood from_westwold`,
`--wear steel_hide_head,steel_hide_body,steel_hide_legs`, `--level 40`.

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
| Inventory | `I` or tab | LB |
| Skills | `O` | RB |
| Quest journal | `P` or `Q` | Back |
| Select element | `1` `2` `3` `4` | — |
| The ancient magic, and its next page | `5` | — |
| Cycle element | `R` | Right stick click |
| Drop what the cursor is on (in the bag) | `G` | Y (north) |
| Pause | `Esc` | Start |

In menus the fighting keys double up the way a controller's face buttons do:
`J`, `E`, `Space` or `Enter` confirms, and `K`, `Backspace` or `Esc` backs out.
A panel's own key closes it again. The death screen ignores input for its first
moment, so the last swing of a lost fight does not skip straight past it.

### Blocking

Hold `H` -- or B on a controller, which only means Back in menus -- with a
**shield** in the off hand to raise your guard. A lantern is not a shield. The
guard stops blows from in front of you, melee swings and shots alike; a blow from
behind finds your back. While it is up you step slowly, cannot swing and cannot
sprint, and with a monster targeted you keep facing it as you move.

A shield turns aside a share of every blow it takes, and each one costs stamina:

    stamina = the blow's damage x the attacker's level x the shield's multiplier

so a rat's nip costs next to nothing and a dragon's bite empties the bar. If a
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

| Monster | Level | Wood | Bronze | Steel | Azuryte | Diamond | Enchanted |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Orc Grunt | 9 | 18 | 14 | 9 | 7 | 4 | 2 |
| Lizardman | 21 | 84 | 67 | 43 | 34 | 18 | 7 |
| Orc Warchief | 28 | 168 | 134 | 86 | 69 | 35 | 14 |
| Ice Troll | 31 | 202 | 161 | 103 | 83 | 42 | 17 |
| Hoarfang | 66 | 1122 | 898 | 574 | 460 | 236 | 96 |

The rule is `ResolveBlock` in `src/systems/combat.h`, and every blow that lands on
the player goes through `World::HitPlayer`, so no monster or projectile can skip
the shield. The guard pose is its own clip, `block`, rendered for every character,
armour cut and tier weapon.

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
never get into. An item says so with `"keep": true` in `data/items.json`.

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
leaves on the ground — is data in `data/projectiles.json`, and the art is one
sprite drawn turned along its direction of travel, so a single arrow image
covers every angle.

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
taught at the **mage college in Fernhollow**: a round stone tower south-east
of the pond, older than the hamlet round it, with a circle cut into the floor
of its hall (`mage_college` and `spell_circle` in `tools/blender_props.py`;
the hall is `fernhollow_college`, with the college's own floor and walls).
Magister Orrin keeps it, on the hero's rig in a blue robe (`magister` in
`LOOKS`), and teaches the first spell to anyone who asks; the rest are
**tomes** sold in the copying room, the last two only once the pond has
spoken to the player. A tome is read from the pack like a recipe scroll, and
what is learned lives in the flags as `recipe:spell:<id>`.

The spells are a fifth school, **arcane**, beside the elements rather than
among them: it neither beats nor is beaten by any of them. `5` chooses it once
any of it is known, and `5` again turns the page to the next spell learned, so
the school is chosen by name where an element is chosen by strength. `R`
cycles round to it too. The old books, and D&D's, are where the names come
from:

| Spell | Magic | Mana | Shape |
| --- | --- | --- | --- |
| Eldritch Blast | 10 | 8 | one bolt of force at 1.3x that passes through three bodies and throws the rest back |
| Magic Missile | 16 | 9 | three darts at 0.55x that turn after the target; they do not miss |
| Scorching Ray | 24 | 12 | three rays of heat in a fan at 0.8x, faster than anything the elements throw |
| Hail of Blades | 32 | 15 | a moment later, blades come down on the target and everything beside it, at 1.5x |
| Cloud of Daggers | 40 | 16 | a slow orb that bursts into a cloud of knives where it lands and cuts for four seconds |
| Thunderwave | 48 | 18 | a ring of force out of the caster in every direction, at 0.7x, that throws everything it touches |

Every spell has a shape in `data/spells.json` -- `bolt`, `darts`, `rays`,
`rain`, `ring` -- and the world casts by shape, so a new spell is a line of
data and a projectile. The combos work with the ancient magic as they do with
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
| Magic | Casting spells with a staff equipped |

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
back, for anyone who wants to fight another way.

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
| Ranged | **Arrow Rain** | A storm of arrows comes down on your target a moment later |
| Magic | **Nova** | A ring of eight bolts of your element, for twice the mana |
| Magic | **Barrage** | Four seeking bolts at once, for twice the mana |
| Magic | **Meteor** | Your element crashes down on your target, for three times the mana |

A strike from above -- Arrow Rain, Meteor, the ancient rain -- **lands once**,
the moment it goes off. It used to land a second time a frame later, on the
effect's first tick, so each was quietly two; the numbers above are now what
they say.

#### Abilities

**Hold the guard (`H`, or `(B)`) and press light, heavy or lock on.** A tree
teaches six abilities and **three are carried at once**: slot one on guard +
light (`H`+`J`), slot two on guard + heavy (`H`+`K`), slot three on guard +
lock on (`H`+`L`, or B + the right trigger). The guard button is the shift key
whether or not there is a shield to raise, and the press is the ability's: not
a swing, and with an ability in the third slot not a change of target either.
Each has a cooldown and a cost, shown on the HUD at the bottom left with a bar
that refills as it comes back, and what is running -- a frenzy, a held breath,
an overload -- is named beside them. A newly learned ability goes straight
into a free slot; `J` on a learned one in the tree moves it on to the next slot
(changing places with whatever is there) and from the last puts it away. Which
three of the six to carry is part of the build.

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
Copper Ring. **Everything else is made at a workbench**, in Havenbrook or
Mossvale, with Crafting: the wooden tier, the Leather Jerkin, the Hide Boots, the Fishing Rod, the Bedroll and the Dreamcatcher.
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
| Havenbrook | Tobin's General Store, a stall on the square | **Halda's Forge**; the Inn Kitchen (Bess); Ivo's Bows and Hides (Hunter Ivo) |
| Mossvale | Pell's Stall | **Garrow's Smithy**, at the village anvil; Oona's Remedies |
| Fernhollow | Nell's Cart, by the path to the jetty | Wendel's Jetty, a fishmonger |
| Whisperwood camp | Hob's Pack, a pedlar resting at the camp | Bram's Woodpile |
| The Reverie | The Night Market (the Night Pedlar) | Curios of the Deep Dream (the Collector) |
| Hidewater, in the Westwold | -- | **Orla's Tannery**: the best price for a hide anywhere, thread, and the Rawhide and Wolfskin sets; **Isolde's Loom**: flax, cloth, vials, the first two dyes, and the Homespun and Novice's robes |
| The Brackenwood | -- | Hale's Packs, at the trapper's camp: hides bought, and the Lizardscale set |

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
  Oona's in Mossvale and the ferry cottage's double bed in Fernhollow.
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

**The Reverie** is five cloud islands over a starry void, joined by plank
bridges to the one you arrive on. It is lit a dream's violet, wisps of light
drift up out of the void, and the ambience is a slow shimmering chord with
chimes far off. A voice on the arrival island explains the rules:

- **Dawn ends the dream.** At five in the morning you wake exactly where you lay
  down, rested. The HUD counts down to it ("Dreaming  dawn in 3:12").
- **The waking stone** beside where you arrive wakes you straight away, with the
  night still going, if you would rather.
- **You cannot die in a dream.** A nightmare that bests you throws you awake, in
  your bed, whole -- but the rest of the night goes with it.

The islands are where the night's work is. **Nightmare Shades** haunt the grove
to the north, **Dread Boars** graze the meadow to the west, the field to the east
has **dream crystals** to mine (Mining 1), and to the south a **Nightmare Brute**
guards a chest. They are the waking world's orcs and boars in a bad night's
colours, and what they drop is real: **dream shards** come back with you, and
six of them with two thread make a **Dreamcatcher** at a workbench, an amulet
worth +10 Magic, +8 Ranged and +4 Defence.

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

### The world map

**M** opens the whole Hollowmarch on one screen, from anywhere -- the point of
it is to be readable while you are three rooms deep in a dungeon and have lost
which way the road runs. The terrain is baked the same way the minimap's dial
is, but from `maps/overworld.mx` rather than from whatever map is loaded, so it
works underground; the `Map` it bakes from is thrown away and only the picture
kept. Where you are shows as a white dot when you are out in it, and as a line
of text when you are not ("You are in The Barrow Beneath the Mire.").

Every mark is a lettered tile in its own colour with its name beside it, and a
legend down the right says what each letter means:

| | |
| --- | --- |
| **T** Town | with a row of chips under it for the trades it keeps |
| **D** Dungeon | the Emberfell mine, the barrow |
| **>** Way to another land | labelled, with the Combat level it is closed below: the Whisperwood, the Ice Spire, the Ashen Path |
| **+** Graveyard | Hollowrest |
| **!** Enemy camp | the lizardmen |
| **\*** Landmark | the trailhead |

A town's chips -- **F** forge, **G** general store, **I** inn kitchen, **B**
bowyer, and so on -- are **not** written into the map data. They are read from
the shop database by the town each shop says it belongs to, so a trader added
to a town appears on the map without anything being written down twice. What
*is* written down is `data/worldmap.json`, which `genmaps` emits as it places
things: it knows which portal is a dungeon mouth and which is the road out to
another zone, where a runtime scan could only guess.

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
weapon of their affinity, worn with a Barkwood Cuirass**: the hero a Wooden
Sword and a Wooden Shield, the wayfarer a Wood Staff and the same shield, and
the warden an Oak Shortbow and a pair of Hide Boots, because a bow takes both
hands and a shield they could not raise is no use to them. The character card
says which. Nothing else: the rest of a set and a bedroll are bought from the
traders, found or made. The tools are lent, by the three people in Havenbrook
who work with them. Every character used to start with the sword, which sent
two of the three into their first fight with the one weapon their affinity
does nothing for.

The two pieces of armour are not generosity, they are the accuracy formula.
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
| `dreamworld` | The Reverie, reached only by sleeping: five cloud islands over the void |
| `house_inn_cellar` | Under the Barley and Bell, down a hatch behind the bar: rats, spiders and a broodmother |
| `ice_spire_peak` | North off the foothills, Combat 30: a climb through trolls to the wyverns' summit |
| `ashen_path` | East off the Hollowmarch below the Cursed Reach, Combat 40: a burnt road across rivers of lava |
| `dungeon_infernal` | The Infernal Pit, through the hellgate at the Ashen Path's end: imps, demons and the Pit Lord |
| `westwold` | The Westwold, out of Havenbrook's west gate, Combat 5: open downs, Hidewater steading, the river Wend, wolves, and the Howling Fells in the west |
| `brackenwood` | The Brackenwood, north off the Westwold's fork, Combat 20: old forest, bears, the Den Mother, and the Old Growth |

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

## Monsters

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
blob. And from this camera, anything behind a head draws above it on screen: the
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
and checks all of it — currently **24205 checks** covering:

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
  arrow rain and meteor call strikes down, a nova bursts into eight bolts for
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

It exits with the number of failures, so CI can use it directly.

---

## Layout

```
src/
  game.cpp/h            state machine, window, main loop
  input.cpp/h           one action vocabulary for keyboard and gamepad
  camera.cpp/h          dead-zone follow camera with zoom and bounds clamping
  sprite.cpp/h          4-direction animation, data-driven
  texturecache.cpp/h    path -> texture
  world/
    map.cpp/h           .mx loader, chunked render, collision, portals
    world.cpp/h         entities, combat resolution, interaction, loot
    targeting.cpp/h     who the player is fighting: combat target and lock-on
    lighting.cpp/h      night as a multiplied light map, with fires cut out of it
  entity/               player, enemies, NPCs
  systems/              skills, items, loot, combat, quests, dialogue, saves,
                        projectiles and elements, spells, the clock,
                        material tiers (items.cpp), skill trees (talents.cpp),
                        tools, fishing and foraging (gathering.cpp), and traders (shop.cpp)
  ui/                   drawing helpers and every screen; lobby.cpp is Play Together,
                        splitscreen.cpp is two at one machine
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
  make_icons.ps1        paints the hand-drawn item icons in icons.txt
  blender_tiers.py      models and renders every tier's ore, bar, weapon and armour,
                        as icons and as weapon layers in the hero's hand
  make_tiers.ps1        runs blender_tiers.py headless
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
