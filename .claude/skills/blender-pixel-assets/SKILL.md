---
name: blender-pixel-assets
description: Create and animate original pixel-art assets for DreamQuest by modelling them in code and rendering headless in Blender -- furniture and props, whole buildings, and animated character sprite sheets (walk, run, attack, jump, custom clips) -- then reducing them to game-ready PNGs and placing them in maps. Use this whenever the user wants a new sprite, prop, piece of furniture, building, interior, or character animation for DreamQuest, asks to "make it in Blender", wants an asset that "looks like a real X", complains that something is a placeholder (a rock labelled workbench, a campfire labelled forge), or wants a new or changed animation clip -- even if they never say the word Blender.
---

# Blender pixel assets for DreamQuest

DreamQuest's original art is not drawn. It is **modelled from primitives in
Python, rendered headless by Blender, and reduced to pixel art by PowerShell**,
so every asset is regenerable from source and tweakable by editing a number.
This skill is how to add to that set without relearning the dozen ways it goes
wrong.

Everything lives in the repo:

| File | What it does |
| --- | --- |
| `tools/blender_props.py` | Props, furniture and buildings. One builder function each. |
| `tools/make_props.ps1` | Renders props via Blender, then downsample / flatten / outline / shadow / sit-on-floor. |
| `tools/blender_character.py` | The rigged player character and every animation clip. |
| `tools/make_character.ps1` | Renders character sheets, reduces them, writes flattened sheets. |
| `tools/make_sprites_json.ps1` | Registers character sheets in `data/sprites.json`. |
| `tools/make_ground.ps1` | Procedural seamless tiles (grass, flagstones, planks, plaster). |
| `tools/genmaps.cpp` | Places assets in maps (`build.ps1 -Maps`). |

Blender is `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`. It is
always run `--background`; the Blender MCP connection is not needed.

## Pick the path

- **A prop or furniture** (anything that stands still) -> read
  `references/props-and-buildings.md`.
- **A building exterior** -> same reference, "Buildings" section. Different
  camera angle.
- **A character or an animation clip** -> read `references/animation.md`.
- **A floor or wall tile** -> `tools/make_ground.ps1`; see
  `references/postprocess.md`, "Tiles".
- **Putting any of it in a map** -> `references/placement.md`.

## The loop that works

Never write a whole set and render it blind. Every batch in this project that
looked right the first time was one where a single piece was rendered, looked
at, and corrected first.

1. **Model one piece** as a builder function.
2. **Render only it**: `.\tools\make_props.ps1 -Only name` (or
   `make_character.ps1 -Only walk`). Call these from the PowerShell tool, not
   via `powershell -File` -- `-File` passes `-Only a,b` as one string and
   renders nothing.
3. **Look at it at game size next to something it will stand beside**:
   `python .claude/skills/blender-pixel-assets/scripts/preview_row.py out.png 4 assets/props/new.png assets/objects/chest.png`
   and Read the PNG. For characters use `scripts/preview_character.py`.
4. **Fix what reads wrong**, re-render, look again. Then do the rest of the set.
5. **Place it** (genmaps), regenerate maps, and composite the room offline with
   `scripts/render_map.py maps/<id>.mx out.png 2` -- it draws props, collision
   (red), NPCs (blue) and portals (green), so a blocked doorway is obvious.
6. **Run the self-test** (`.\build.ps1 -Test`) and look in-game if you can.

When judging a render, ask "what would someone call this if they saw only
this?" A bench that reads as "a plank", a staircase that reads as "a ladder",
a hearth that reads as "a grey box" -- those are the failures to catch.

## Conventions that matter

These are the ones that cost real time when missed. The references explain
each in full.

- **Orthographic camera, Z up, the model's front faces -Y** (toward the
  camera). Furniture is seen from `CAMERA_ELEVATION = 34` degrees; buildings
  from ~46 (return `(span, 46.0)` from the builder). Characters use 46 with the
  head pitched back 19 degrees to show the face.
- **Use `blk()`, not `box()`, for new props.** `box()` scales a unit cube by
  *half* the size given. It is kept only because the overworld signpost
  depends on it.
- **Palette values are sRGB**; `material()` converts to linear. Adding a colour
  means adding it to `PALETTE` as the colour-picker value.
- **Chunkier than life.** At forty pixels a two-pixel feature is all outline.
  Tongs, legs, blades and spokes need to be several times thicker than real.
- **Exaggerate motion.** A realistic stride moves a foot less than a pixel.
  Walk swings ~42 degrees, run ~46 with a lean.
- **Emission low** (about 1-2). Higher blows flames and windows out into flat
  yellow discs.
- **One render per sheet, not per frame** for characters: copies offset along
  the camera's right/up vectors land on an exact grid.

## Wiring a new prop in

Four places, all required, or it silently does not appear:

1. Builder function in `tools/blender_props.py`.
2. Entry in a registry dict there (`PROPS`, `FORGE_PROPS`, `INN_PROPS`,
   `ROOM_PROPS`, `BUILDING_PROPS` -- or a new one added with `PROPS.update`).
3. Its final pixel size in `$sizes` in `tools/make_props.ps1`.
4. `.\tools\make_manifest.ps1` before regenerating maps, so genmaps knows the
   image size.

## Before calling it done

- `.\build.ps1 -Test` passes. The self-test checks every NPC and object in
  every building is reachable, portals arrive somewhere valid and do not
  bounce, and art files exist.
- Temporary hooks are gone: `grep -rn "TEMP\|DQ_MAP" src tools` finds nothing.
- The README / `docs/ASSETS.md` mention any new set, and why any non-obvious
  choice was made.
