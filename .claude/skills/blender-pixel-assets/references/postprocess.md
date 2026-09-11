# Post-processing and tiles

## Props: `tools/make_props.ps1`

Renders are eight times final size. Five steps, in this order, and the order
matters:

1. **Box downsample**, alpha-weighted, so edge colour comes from the pixels
   that were there rather than being dragged toward black.
2. **Flatten**: alpha cut to on/off at 128, colour stepped to ten levels. A
   render has hundreds of shades per plank; pixel art has a few.
3. **Outline**: edge pixels darkened to 62%. The single biggest thing that
   makes a render sit beside hand-drawn art. At 42% thin features went black.
4. **Contact shadow**: drawn from the prop's own silhouette, per column below
   its lowest solid pixel. A Blender shadow catcher was tried and removed -- it
   writes shadow density into alpha, so a dense shadow is indistinguishable
   from the prop.
5. **Set-OnFloor**: shifts drawn pixels to the bottom of the image. The bottom
   edge is where the game anchors and sorts a prop; empty space under the art
   made the forge stand 23px above its placement.

Characters (`make_character.ps1`) do only 1 and a hard alpha cut -- no flatten,
no outline -- because the body layer is recoloured at runtime and a baked
outline survives the tint as a black rim.

## Tiles: `tools/make_ground.ps1`

All tiles are generated, deterministic (fixed LCG seed), and **seamless because
every mark is drawn through `Set-Wrapped`**, which wraps at `$Size`. When a
section generates 32px tiles, set `$Size = 32` first or every mark wraps at 16
and repeats four times.

- **Outdoor** (16px): speckle, vertical blades, grit. Several variants per
  family; genmaps picks by hash of the cell.
- **Masonry** (`New-Masonry`, 32px): courses of units with mortar joints.
  `stagger = "half"` is running bond for stone and brick.
  `stagger = "scatter"` is for floorboards: joints land at scattered offsets
  and only about a third of courses end inside any one tile, so boards run
  long across the floor. Half-offset boards read as brick.
- **Plaster** (`New-Plaster`, 32px): limewash with a sill beam and a post per
  tile, which builds a timber-framed wall.

Flat-colour tiles are the thing to avoid -- the early overworld was one RGB
value repeated four thousand times and read as coloured paper.
