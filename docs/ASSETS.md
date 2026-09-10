# Assets

Every image the game loads comes from a free [CraftPix](https://craftpix.net)
asset pack. The CraftPix file licence
(<https://craftpix.net/file-licenses/>) allows using these assets in a game but
not redistributing the files themselves, so **none of them are committed to
this repository**. `tools/import_assets.ps1` rebuilds `assets/` from the `.zip`
packs on your own machine.

## Packs used

| Pack | Used for |
| --- | --- |
| Base 4-Direction Male Character | The male player character |
| Base 4-Direction Female Character | The female player character |
| Top-Down Orc Game Character | Orc Grunt, Orc Raider, Orc Warchief |
| Top-Down Hunt Animals Sprite Pack | Boar, deer, fox, hare |
| Top-Down Pixel Art Guild Hall | The guild hall, its sign, and the town NPCs |
| Glassblower's Workshop Top-Down | The houses, the inn and the forge |
| Path and Road Top-Down Tileset | Ground palette fills, roads, ground decals |
| 2D Top-Down Pixel Dungeon | Dungeon floors and walls, chests, doors, fire |
| Cursed Land Top-Down Tileset | The Cursed Reach ground |
| Undead Tileset Top-Down | The Mire ground |
| Top-Down Trees | Woodcutting nodes and scenery |
| Rocks and Stones Top-Down | Mining nodes and scenery |
| Top-Down Bushes | Scenery |
| Forest Objects Top-Down | Mushrooms and undergrowth |
| Basic Pixel Art UI for RPG | Item icons |

Three more packs (bridges, dungeon props, dungeon objects) are unpacked by the
importer and are available to build on, but nothing in the current maps uses
them yet.

## Optional: equipment icon packs

Four further CraftPix freebies are supported but not required:

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
