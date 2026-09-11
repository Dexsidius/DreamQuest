# Props, furniture and buildings

`tools/blender_props.py`. One function per asset, returning how wide a square
the camera should frame -- or `(span, elevation)` to override the angle.

## Contents
- Helpers
- Writing a prop
- Framing
- Buildings
- Failures seen, and the fix for each

## Helpers

All sizes are true world units except `box()`.

| Helper | Use |
| --- | --- |
| `blk(name, (w,d,h), (x,y,z), colour, rot=, rough=, metal=, emit=, bev=)` | A bevelled box of exactly that size. The default choice. |
| `cyl(name, radius, depth, loc, colour, rot=, verts=24, ...)` | Cylinder along Z. `rot=(0, radians(90), 0)` lays it along X; `(radians(90), 0, 0)` along Y (end toward camera). |
| `sphere(name, radius, loc, colour, emit=)` | Flames, knobs, coal lumps, leaves. |
| `bevel(ob, width)` | Already applied by `blk`; call on other objects if edges vanish. |
| `turn_all(degrees)` | Rotates everything built so far about Z. Model square-on, then turn -- e.g. a grindstone or spinning wheel whose wheel is edge-on otherwise. |
| `tankard`, `lying_barrel`, `bed`, `window`, `gable_roof` | Reusable compound pieces; read them before re-inventing. |
| `box(...)` | Legacy. Half-size. Do not use for new work. |

`name` must be unique within a prop when it matters for debugging but Blender
tolerates duplicates. Materials are cached by name+colour, so reuse is cheap.

## Writing a prop

```python
def prop_workbench():
    blk("top", (1.80, 0.72, 0.14), (0, 0, 0.84), "oak_light")
    for i, (x, y) in enumerate(((-0.78, -0.28), (0.78, -0.28), (-0.78, 0.28), (0.78, 0.28))):
        blk("leg_%d" % i, (0.14, 0.14, 0.78), (x, y, 0.39), "oak")
    ...
    return 2.6            # frame a 2.6-unit square
```

- Stand everything on Z=0. The post-process sits the prop on the bottom of its
  image anyway, but a floating model gets a floating shadow.
- Things against a wall (shelves, wardrobes, fireplaces, tool boards) put their
  back at **+Y**; the camera sees the -Y face.
- Identity comes from one or two strong details, not many small ones. A table
  with a row of hanging tools behind it is a workbench; without them it is a
  table with things on it.
- Scale: a doorway is about 1.0 wide, a table top 0.74 high, a chair seat 0.46,
  a bed 0.92 x 1.9. The player character is about 0.4 wide, 1.1 tall.

## Framing

`make_props.ps1` warns `touches the top of its frame and may be cut off`. That
check runs before the bottom-align shift, so treat it seriously: widen the
returned span by 10-15% and re-render. Tall things (chimneys, armour stands,
bookshelves, stairs) hit it most.

Rough final sizes that worked: chair/stool 32-40, barrel/chest 40-44,
table 56, bed 64-72, wardrobe/shelf 64-72, counter/fireplace 96-112,
building 192.

## Buildings

Seen from ~46 degrees because the CraftPix exteriors are roof-first. Return
`(span, BUILDING_ELEVATION)`.

- **Put the door at the image's horizontal centre.** `PlaceBuilding` in
  genmaps cuts the doorway and portal at the centre of the art.
- **The door must be visible from above.** A porch roof on posts hid the inn's
  door completely; a shallow canopy on brackets does not.
- **Anything on the back slope of a roof floats.** The camera never sees the
  back slope, so a chimney based there hangs in the air. Put it on the front
  slope and start it inside the roof.
- Make it look unlike the neighbours on purpose: a different roof material,
  storey count, or wall treatment. Timber framing is the dark lattice of posts,
  rails and braces on the face -- most of the effect.
- Warm lit windows (`glass_lit`, emit ~0.5) read as inhabited; one dark window
  keeps it from looking like a lantern.

## Failures seen, and the fix for each

| Looked like | Cause | Fix |
| --- | --- | --- |
| Props tiny in a big empty frame | `box()` half-size | `blk()` |
| Everything twice as pale as intended | sRGB fed as linear | Palette is sRGB; `material()` converts |
| A grey concrete slab | One big block for stonework | Courses of individual stones in 2-3 greys (see `prop_inn_fireplace`) |
| A flat yellow disc for fire | Emission 3+ | Emission 1-2 |
| A grey bar between two posts | Wheel edge-on | `turn_all(55)` |
| A box with no water | Trough walls hide the surface from above | Lower the front wall, raise the water |
| Brown table, not bellows | Leather same value as wood | Darker/redder and fatter body |
| Weapons invisible in a barrel | Blades thin and grey on grey | Wider blades, clear of the rim, span widened |
| Black rectangle with a fence | Real stair depth is black from above | Pale steps, shallow drops (cheat the depth) |
| A gate, not a door | Door leaf flat against the wall | Swing the leaf ~58 degrees so its face shows |
| Legs detached from a table top | Camera too high, legs hidden | 34 degree camera; thicker legs |
| Floating 20px above placement | Empty space under art | Handled by `Set-OnFloor`; do not remove it |
