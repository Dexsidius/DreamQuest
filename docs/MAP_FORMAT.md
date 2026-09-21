# Map format

DreamQuest maps are the `.mx` files LevelEdit-Plus exports, with one extra
top-level key. The editor ignores keys it does not recognise, so a map written
by the game's generator opens in the editor, can be edited by hand, saved, and
still runs.

## The editor's half

```json
{
  "name": "The Hollowmarch",
  "tiles": {
    "grass": {
      "filepath": "assets/tiles/grass.png",
      "locations": [[cx, cy, w, h], ...]
    }
  }
}
```

Each entry is a tile name, the image to draw, and every placement of it.
Placements are **centre-anchored**: `cx, cy` is the middle of the quad, matching
`GameTile::Render` in the editor, which draws at `x - w/2, y - h/2`. The loader
converts to top-left corners once at load time.

`w` and `h` are per placement, so one image can be laid at its authored size in
one place and stretched in another. The generator uses this: flat single-colour
ground fills are laid as 32px quads because stretching a flat colour is
invisible and quarters the placement count, while textured tiles like the
cobbled road are tiled at their authored 16px so their detail stays the same
scale as the characters standing on them.

## The game's half

Everything a game needs beyond pictures lives under `"dreamquest"`.

```json
"dreamquest": {
  "version": 1,
  "tile_size": 16,
  "bounds": [4096, 3072],
  "interior": false,
  "ambient": "overworld",
  "background": [38, 52, 40, 255],

  "layers":   { "grass": 0, "bush_00": 1, "roof": 2 },
  "solid":    ["wall"],
  "solid_box": { "tree": [-9, -9, 18, 9] },
  "collision": [[x, y, w, h], ...],

  "spawns": { "default": [x, y], "from_town": [x, y] },

  "portals": [
    { "rect": [x, y, w, h], "target": "town_havenbrook", "spawn": "from_field",
      "label": "Enter Havenbrook", "interact": false, "locked_by": "rusted_key" }
  ],

  "enemies": [
    { "type": "orc1", "x": 0, "y": 0, "level": 3, "respawn": 28.0, "leash": 260.0 }
  ],

  "npcs": [
    { "id": "npc_maren", "name": "Elder Maren", "sprite": "citizen1",
      "x": 0, "y": 0, "dialogue": "maren_root", "facing": 0, "wanders": false }
  ],

  "objects": [ ... ]
}
```

### Fields

| Key | Meaning |
| --- | --- |
| `bounds` | Map size in world pixels. Omitted, it is derived from the tiles, so a map exported straight from the editor with no extension block still scrolls correctly. |
| `layers` | Tile name → draw layer. `0` ground, `1` decor (sorted against entities by its base), `2` overhead (drawn above everything). |
| `solid` | Tile names whose whole quad blocks movement. |
| `solid_box` | Tile names with a collision box smaller than the art, relative to the quad's top-left — so a tree trunk blocks and its canopy does not. |
| `collision` | Extra blocking rectangles in world pixels. |
| `spawns` | Named points. A portal names the spawn it arrives at. `default` is the fallback. |
| `portals` | `interact: false` steps through on contact; `true` needs the Interact button. `locked_by` names an item the player must be carrying. |
| `enemies` | `level` scales the stat block. `respawn` is seconds, `0` means it stays dead. `leash` is how far it chases from its post. `pool`, `group` and `spread` make a post that is not kept by the same thing every day. `"night": true` makes it one that is only kept after dark, on `chance` (0-1) of the nights, never respawning: put these **last** in the list, because a post is known by its place in it. |
| `objects` of type `totem_circle` | The ring a boss's totem is stood in. It needs no `sprite` -- lay the ring's art as a floor overlay at the same point -- because what stands in it is the character's and is drawn by the game. There is one, in `mossvale_cottage`, and the self-test says so. |
| `npcs` | `dialogue` is a root node id in `data/dialogue.json`. |

### Objects

One list covers everything interactive. `type` decides the behaviour:

| `type` | Behaviour | Fields it uses |
| --- | --- | --- |
| `chest` | Opens once, spawns loot, sets a save flag | `loot`, `sprite`, `sprite_open` |
| `note`, `sign` | Shows a panel; a note can start a quest and leave an item | `title`, `text`, `starts_quest`, `loot` |
| `board` | Mission board | `title`, `quests` |
| `tree`, `rock` | Gathering node | `skill`, `skill_level`, `yield`, `yield_xp`, `gather_time` |
| `range` | Cooks one raw item per press | `title` |
| `workbench` | Opens the crafting panel | `title` |

Common to all of them: `id` (unique across the save — it is what the world
flags key on), `x`, `y` (the base the sprite stands on), `sprite`, and an
optional `solid` rectangle.

## Regenerating

`tools/genmaps.cpp` builds all nine maps deterministically from a seed, so the
world can be rebuilt after a layout change without invalidating anything else:

```powershell
.\build.ps1 -Maps
```

It reads `data/asset_manifest.json` for every image's real pixel size, which is
what lets it place art at the scale it was drawn at and pick ground decals whose
palette matches the terrain underneath.
