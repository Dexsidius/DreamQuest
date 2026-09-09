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
