# DreamQuest

A top-down adventure RPG in C++ and SDL3, in the Dragon Quest Monsters mould:
a scrolling overworld with several biomes, a village you can walk into and out
of, houses and a guild hall you can enter, mountain mines and a barrow to raid,
Old School RuneScape-style skills, weighted loot tables, and a three-attack
melee system built around a hold-to-charge heavy swing.

Built on [SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template),
and the maps are authored in the format exported by
[LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus).

---

## Building

### Windows (MSYS2 UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-sdl3-image mingw-w64-ucrt-x86_64-sdl3-ttf
```

```powershell
.\tools\import_assets.ps1 -GameAssets "E:\Game Assets"   # once, to build assets/
.\build.ps1 -Run
```

### Linux / macOS

Install SDL3, SDL3_image and SDL3_ttf, then:

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

`./compile.sh test`, `./compile.sh maps` and `./compile.sh tools` do the same on
Linux.

---

## Assets

The art is [CraftPix](https://craftpix.net) free content. That licence permits
using the assets in a game but not redistributing the files, so **no art is
committed here**. `tools/import_assets.ps1` rebuilds `assets/` from the `.zip`
packs you downloaded:

1. Unpacks each pack into `assets/_raw/`
2. Copies the character animation sheets under short, stable names
3. Cuts the flat ground fills out of the packed tilesets with `tilecut`
4. Cuts buildings, decorations and item icons out of the packed sheets
5. Copies the individually-shipped props (trees, rocks, bushes)
6. Regenerates `data/sprites.json` and `data/asset_manifest.json` to match

Everything lands on the exact paths the committed data and maps refer to, so
the game runs as soon as it finishes. The packs used are listed in
[docs/ASSETS.md](docs/ASSETS.md).

---

## Controls

Both schemes are live at once by default; the game switches to whichever you
last touched. Options → Input Device pins it to one if you would rather.

| Action | Keyboard & mouse | Controller |
| --- | --- | --- |
| Move | WASD / arrows | Left stick or d-pad |
| Light attack | `Z` or left mouse | X (west) |
| Strong / charged attack | `X` or right mouse | Y (north) |
| Interact | `E` or space | A (south) |
| Inventory | `I` or tab | LB |
| Skills | `K` | RB |
| Quest journal | `Q` | Back |
| Pause / back | `Esc` | Start / B |

### The three attacks

- **Light** — fast, cheap, and it chains. Three hits in a row, each a little
  slower and a little harder than the last.
- **Strong** — tap the heavy button. Slower, hits considerably harder.
- **Charged** — *hold* the heavy button. Past about a fifth of a second the
  swing starts charging and a meter appears under your feet; it turns bright
  when it is full. Release to fire. A full charge is worth roughly three times
  a normal strong hit and reaches further, but it roots you while it winds up.

---

## Skills

Ten skills on the Old School RuneScape XP curve — the real one, so level 92 is
half the experience of 99, and the self-test checks the table against known
values.

| Skill | Trained by |
| --- | --- |
| Attack | Landing light attacks |
| Strength | Landing strong and charged attacks |
| Defence | Taking hits |
| Hitpoints | All damage dealt |
| Woodcutting | Chopping trees in the greenwood |
| Mining | Working ore seams in the foothills and the Mire |
| Cooking | Using a fire with something raw in your pack |
| Crafting | Workbenches in the forge and the guild hall |
| Ranged | Equipment bonuses and quest rewards |
| Magic | Equipment bonuses and quest rewards |

Combat level uses the OSRS formula across the melee/ranged/magic triangle.

> **Honest scope note:** Ranged and Magic are implemented as far as levels,
> XP, equipment bonuses and combat level go, and bows and staves carry real
> bonuses — but there is no projectile or spell system yet, so all *combat* is
> melee. Everything else in this table is played, not just tracked.

---

## Quests

Quests reach you three ways, all of them live:

- **The mission board** in Havenbrook — four contracts, gated on level and on
  what you have already finished.
- **NPC conversations** — Elder Maren runs the main chain (a letter, a missing
  surveyor, and what is gathering the orcs under Emberfell). The innkeeper,
  the smith, the watchman and the hunter have their own.
- **Notes left in the world** — a water-stained note at the edge of the Mire
  starts the barrow chain, and a torn survey page on the Sunken Road advances
  Maren's.

Dialogue options are gated on quest state, inventory and skill level, so the
same NPC says different things before, during and after a quest.

---

## The world

`maps/overworld.mx` is 4096 × 3072 pixels — about eight screens across — and
the camera scrolls it as a viewport on the player. Biomes: meadow, greenwood,
northern foothills, the Mire, and the Cursed Reach, joined by the Sunken Road.

| Map | What it is |
| --- | --- |
| `overworld` | The Hollowmarch |
| `town_havenbrook` | The village, with four enterable buildings |
| `guild_hall`, `house_elder`, `house_inn`, `house_smith` | Interiors |
| `dungeon_emberfell_1` / `_2` | The mine, upper and lower workings; the lower level is locked until you find the rusted key, and the Warchief holds the last room |
| `dungeon_barrow` | Beneath the Mire |

Maps are big enough to grow: the base layer is bucketed into chunks and culled
against the camera, so adding another biome costs load time and nothing else.

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
which chests you have opened and which notes you have read — so loading puts
you back exactly where you left off. Saves are written to a temporary file and
renamed, so an interrupted write cannot destroy the previous one.

---

## Self-test

```powershell
.\build.ps1 -Test
```

Screenshots prove the game runs; they do not prove that the mission board names
a quest that exists, that every dialogue option leads somewhere, or that a loot
table only drops real items. `tools/selftest.cpp` links the game's own systems
and checks all of it — currently **2177 checks** covering:

- every sprite sheet and item icon exists on disk
- every loot table drops real items, and quest-critical drops are guaranteed
- every quest objective, prerequisite and reward resolves, and every quest has
  a giver somewhere in the world
- the dialogue graph is fully connected
- all nine maps load; portals point at real maps; every enemy, NPC and object
  resolves
- the OSRS XP table matches known values
- a starting character can actually win the first fight the level 1 board quest
  sends them into
- inventory, equipment, skills and quest progress survive a save round-trip

It exits with the number of failures, so CI can use it directly.

---

## Layout

```
src/
  game.cpp/h            state machine, window, main loop
  input.cpp/h           one action vocabulary for keyboard, mouse and gamepad
  camera.cpp/h          dead-zone follow camera with zoom and bounds clamping
  sprite.cpp/h          4-direction animation, data-driven
  texturecache.cpp/h    path -> texture
  world/
    map.cpp/h           .mx loader, chunked render, collision, portals
    world.cpp/h         entities, combat resolution, interaction, loot
  entity/               player, enemies, NPCs
  systems/              skills, items, loot, combat, quests, dialogue, saves
  ui/                   drawing helpers and every screen
tools/
  import_assets.ps1     rebuilds assets/ from the CraftPix zips
  tilecut.cpp           cuts atlases into individual tiles and sprites
  genmaps.cpp           builds the world into maps/*.mx
  selftest.cpp          content and systems validation
  make_sprites_json.ps1 / make_manifest.ps1
data/                   items, enemies, loot tables, quests, dialogue, sprites
maps/                   generated .mx maps, editable in LevelEdit-Plus
```

About 7,400 lines of C++, excluding the vendored `nlohmann/json`.

---

## Credits

- Art: [CraftPix](https://craftpix.net) free asset packs — see
  [docs/ASSETS.md](docs/ASSETS.md)
- JSON: [nlohmann/json](https://github.com/nlohmann/json) (MIT)
- Base template: [Dexsidius/SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template)
- Map format: [TheSardonicals/LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus)

Code is MIT ([LICENSE](LICENSE)). The art is not covered by that licence and is
not distributed here.
