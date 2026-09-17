# Assets

**Every image the game loads is its own.** It is modelled, rendered or drawn by
the tools in `tools/`, it lives in `assets/`, and it is committed -- so a clone
of this repository runs as it stands, with nothing to download and no account
to sign up for.

It did not start that way. The game was built on free
[CraftPix](https://craftpix.net) packs, whose file licence
(<https://craftpix.net/file-licenses/>) covers using the art in a game but not
passing the files on, which meant `assets/` could not be committed and a clone
was a game with no pictures in it. Everything has since been replaced.

## What makes what

| What | Made by |
| --- | --- |
| The three playable characters, their armour layers in three cuts, and the town NPCs | `tools/blender_character.py` (`make_character.ps1`) |
| Every monster -- orcs, animals, undead, dragons and all | `tools/blender_creatures.py` (`make_creatures.ps1`) |
| Every prop -- furniture, herbs, gravestones, the forge, the well | `tools/blender_props.py` (`make_props.ps1`) |
| The scenery and buildings -- trees, rocks, bushes, mushrooms, houses, the guild hall, chests, doors, the campfire | `tools/blender_props.py` (`make_props.ps1 -Objects`) |
| Every ore, bar, weapon and armour icon, and the weapon in the hero's hand | `tools/blender_tiers.py` (`make_tiers.ps1`) |
| All 93 ground and interior tiles | `tools/make_ground.ps1` |
| Ground decals -- tufts, flowers, pebbles, cracks | `tools/make_decals.ps1` |
| The hand-drawn item icons | `tools/make_icons.ps1` from `tools/icons.txt` |
| The minimap bezel and the HUD fittings | `tools/make_ui.ps1` |
| The title painting, the window icon and the .exe icon | `art/`, see the README |

Rebuilding the modelled art needs [Blender](https://www.blender.org) 5.2; the
tiles, decals, icons and HUD are pure PowerShell and need nothing at all.

## The font

None is bundled. `src/ui/ui.cpp` falls back through Consolas, Segoe UI, Arial
and DejaVu Sans, so the game renders the same on any machine that has any of
them. Drop a `.ttf` at `assets/fonts/dreamquest.ttf` to override it; that path
is ignored by git, because a font copied out of a system folder is not ours to
pass on either.

## Optional: equipment icon packs

Four CraftPix freebies are still supported, and are the only thing
`tools/import_assets.ps1` still imports. They are **not required**: without them
the game runs exactly as it does with them, minus a wardrobe of painted armour.

| Pack | Used for |
| --- | --- |
| [Fantasy Knight Armor Pack 11](https://craftpix.net/freebies/free-game-icons-of-fantasy-knight-armor-pack-11/) | Helmets, cuirasses, tassets, gauntlets, greaves |
| [RPG Boot Icons](https://craftpix.net/freebies/free-rpg-boot-icons/) | Boots and sabatons |
| [Fantasy Mage Outfit Pack 7](https://craftpix.net/freebies/free-game-icons-of-fantasy-mage-outfit-pack-7/) | Robes and staves |
| [Fantasy Daggers Pack 2](https://craftpix.net/freebies/free-game-icons-of-fantasy-daggers-pack-2/) | Fast melee weapons |

All four need a free CraftPix account to download — the direct link returns the
sign-in page otherwise. Drop the `.zip` files in with the rest and re-run
`tools/import_assets.ps1`; it will unpack them, produce a 64px inventory icon
and a hardened 24px worn overlay for each piece, and write
`data/items_armour.json` with the matching item definitions. Without them the
importer says so and the game runs exactly as before.

The dungeon chests and the orc warchief chain to a loot table called
`armour_cache`. `data/loot_tables.json` defines it empty, and the importer
writes `data/loot_tables_armour.json`, which is loaded afterwards and replaces
it with the real contents. So the reference resolves straight from a clone and
quietly yields nothing until the packs are installed. Drop weights are derived
from each piece’s value rather than tuned by hand, so a worn leather boot turns
up far more often than a Riftblade.

### What the numbered files are

The packs ship their icons numbered, not named, so the mapping lives in the
`$catalogue` table in `tools/import_assets.ps1` and was read off the art:

- **Knight armour** — 1 mail coif, 2 plumed helm, 3 breastplate, 4 ornate
  cuirass, 5 cuisses, 6 tasseted skirt, 7 bracers, 8 gilded gauntlets,
  9 greaves, 10 gilded greaves. A plain and an improved piece per slot.
- **Mage outfits** — the odd files are robes, the even files are staves.
- **Boots** — 50 pairs; 1, 8, 3, 29 and 50 are used, as a ladder from soft
  leather to gilded plate.
- **Daggers** — 1, 4, 6, 9 and 2 are used, in that order of strength.

These are 512×512 painted inventory icons, not sprite layers. See the note in
the README about what that means for wearing them.

## Rendered props

`tools/blender_props.py` and `tools/make_props.ps1` produce `assets/props/`
from Blender, for things no pack contains. Currently that is the crossroads
signpost: the overworld waymarker had been borrowing the guild hall’s own
plaque, so a new player’s first sight of the world was a sign reading GUILD
HALL standing in an empty field. See the README for what this pipeline is and
is not good for.

The woodland zones added a set of their own:

- **`mossvale_lodge`** (192) -- the reeve's log lodge, with antlers over the door.
- **`herbalist_cottage`** (168) -- a thatched cottage. It exists because the
  pack's `building_house_b` is mis-cut on its sheet and cannot be used; it
  serves as Oona's cottage and, shut, as the woodcutters' cabin.
- **`well`**, **`market_stall`**, **`palisade`**, **`log_pile`**, **`tent`**,
  **`campfire_ring`** -- village and camp furniture. The campfire ring also
  replaced Havenbrook's cooking-range sprite outdoors.

## How the import works

The CraftPix tilesets are packed autotile sheets, and LevelEdit-Plus works with
one image file per tile. `tools/tilecut.cpp` bridges the two:

- **`--cells`** pulls named cells out of an atlas. The base ground palette comes
  out this way: the flat, fully opaque cells in a tileset are the fills the set
  is designed to be laid over.
- **`--sprites`** traces connected opaque pixels to find each separate drawing
  on a packed sheet and exports it cropped. This is how the buildings come out
  of one 448×144 sheet as individual files.
- **`--region`** pulls an explicit pixel rectangle, used for the chest, door and
  item icons where a sprite trace would merge neighbouring frames.
- **`--report`** and **`--flat`** are for exploring a sheet you have not used
  yet.

`tools/make_manifest.ps1` then records every imported image's pixel size and
sorts the loose ground decals by colour family, so the map generator can place
art at the size it was drawn at and never drop a teal decal on a green field.

## Layout produced

```
assets/
  characters/<id>/{idle,walk,run,attack,hurt,death}.png
  tiles/       16px ground fills, one file per terrain
  decor/       loose ground decals and road pieces
  objects/     buildings, trees, rocks, bushes, chests, doors, campfire
  icons/       item icons
  ui/          the RPG UI sheets
  icons/armour/  optional armour icons, 64px, with 24px worn/ copies
  fonts/       dreamquest.ttf
  _raw/        the unpacked archives; safe to delete after importing
```

## Sprite sheet convention

Every character sheet in this project is laid out the same way: a square frame,
four rows in the order **down, left, right, up**, and one column per frame. So
the frame size is the sheet height over four, and the frame count is the width
over that. `tools/make_sprites_json.ps1` derives `data/sprites.json` from the
files themselves rather than from a hand-written table, which is why the
animation data can never drift out of step with the imported art.

## Original props

Some art in this project is not from a pack. `tools/blender_props.py` models
props from primitives in code and renders them headless;
`tools/make_props.ps1` reduces the renders to pixel art (box downsample, a
flattened palette, a dark outline, a contact shadow drawn from the prop's own
silhouette) and sits each one on the bottom of its image, because that bottom
edge is where the game anchors and sorts it.

```powershell
.\tools\make_props.ps1                          # render everything, then convert
.\tools\make_props.ps1 -Only forge,anvil        # a couple
.\tools\make_props.ps1 -SkipRender              # convert existing renders only
```

**The Barley and Bell** is two floors of these. Downstairs is the taproom: a
panelled bar with a brass foot rail and stools, a keg rack, a bottle shelf, a
stone fireplace that doubles as the kitchen, round tables for two, benches, a
long table for a party, a chalk board and a flight of stairs up the left-hand
wall. Upstairs is a corridor and three guest rooms behind their own doors, with
single and double beds, nightstands, wardrobes, washstands and travelling
chests. The building outside is its own model too -- a stone taproom under a
jettied timber-framed storey, a dark shingle roof with a dormer, and a sign --
and deliberately unlike the red-tiled cottages around it.

**Maren's house** is a cottage furnished for the village elder: a bed in the
corner, a hearth with a kettle, a table laid for one, a full bookshelf, a desk
of scrolls and a spinning wheel.

**The workbench** in Havenbrook is a carpenter's bench with a vise and a tool
board. It was a rock.

The inn and cottage floors are generated floorboards and timber-and-plaster
walls from `tools/make_ground.ps1`.

**Halda's Forge** is furnished entirely from these: forge, anvil, bellows,
quenching trough, grindstone, tool rack, coal bin, ingot crate, armour stand,
weapon barrel and shop counter. Its flagstone floor, stone walls and brick
chimney breast come from `tools/make_ground.ps1`.

The workflow -- modelling, rendering, animating, judging and placing -- is
written up as a Claude Code skill in `.claude/skills/blender-pixel-assets/`,
with the failures seen along the way and the fix for each.

Two things to know before adding to the set:

- `box()` scales a unit cube by *half* the size it is given, so the older props
  are modelled at half their written dimensions. The forge set uses `blk()`,
  which means what it says. `box()` was left alone because changing it would
  resize the signpost already placed on the overworld.
- Make everything chunkier than life. At forty pixels a pair of tongs is two
  pixels wide, and a two-pixel feature is all outline once it has been reduced.
