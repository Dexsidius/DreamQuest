# Animated characters

`tools/blender_character.py` renders; `tools/make_character.ps1` reduces;
`tools/make_sprites_json.ps1` registers.

## Contents
- The output the game expects
- How the rig works
- Writing or changing a clip
- Adding a clip end to end
- A different character
- Judging the result
- Failures seen

## The output the game expects

Every character sheet in DreamQuest:

- square **64px frames**, one column per frame
- **four rows: down, left, right, up** -- in that order
- split into **layers** drawn in order: `1_shadow`, `2_weapon_back`, `3_body`,
  `4_weapon_front`, `5_head`, named `<clip>_<n>_<layer>.png` under
  `assets/characters/<id>/layers/`
- plus a flattened `<clip>.png` beside the layers (the character-select preview
  draws that one)

Layers are what keep equipment working: the game tints `body` by armour, hides
both weapon layers when a weapon brings its own art, and never tints `shadow`.
A single flattened sheet would lose all of that.

## How the rig works

There is **no armature**. The character is a tree of empties with meshes
parented to them:

```
root -> hips -> chest -> neck -> head_tilt (fixed -19 deg lean) -> head, hair, eyes
                       -> shoulder_l/r -> elbow_l/r (-> hand_r -> sword)
             -> hip_l/r -> knee_l/r -> shin, boot
```

A **pose** is a dict of Euler angles (radians, via `rad()`) for joints that
move, plus two special keys: `root_z` (lift) and `root_pitch` (tip the whole
body, used by death). `apply_pose` clears every joint each frame, so rest
angles belong in the pose, not the build -- that is why `ARM_FLARE` is added in
every pose rather than baked into the shoulders. `head_tilt` is deliberately
not in `joints`, so its lean survives the reset.

**A whole sheet is one render per layer.** `build_sheet` builds a copy of the
character per frame and offsets it along the camera's right vector (columns)
and up vector (rows). The camera is orthographic, so those copies land on an
exact grid with no perspective error. A clip is ~30 seconds, not ~30 minutes.

The model faces **-Y** (its eyes are at negative Y), so `FACINGS` is
`down=0, left=270, right=90, up=180`. Getting this backwards renders the back
of the head across the whole down row -- a character with no face.

## Writing or changing a clip

A clip is a pose function of `t` in 0..1 plus an entry in `CLIPS`:

```python
def pose_walk(t):
    s = math.sin(t * math.tau)
    return {
        "hip_l": (rad(42 * s), 0, 0),
        "hip_r": (rad(-42 * s), 0, 0),
        "shoulder_l": (rad(-34 * s), 0, rad(ARM_FLARE)),
        ...
        "root_z": 0.038 * abs(math.cos(t * math.tau)) - 0.019,
    }

CLIPS = {"walk": (pose_walk, 6, True), ...}   # (function, frames, loops)
```

- **Loops** divide the cycle evenly and never repeat frame 0 at the end
  (`t = col / frames`). **One-shots** run to full extension
  (`t = col / (frames - 1)`).
- Write cycles as sines and one-shots as phases (`if t < 0.34: wind-up ...`).
  Hold a beat at the key pose -- the frame that lands is the one players see.
- **Exaggerate everything.** A thigh is four pixels. Real-world angles read as
  sliding, not walking. Starting points that worked: walk hips +-42, run hips
  +-46 with chest lean 12, jump root lift 0.62.
- Leaning the chest is most of what separates a run from a walk at this size.

## Adding a clip end to end

Say `climb`:

1. `pose_climb(t)` and `"climb": (pose_climb, 6, True)` in `CLIPS`.
2. `climb = 6` in `$clipFrames` in `tools/make_character.ps1` -- the frame
   counts must match or conversion throws.
3. `climb = @{ fps = 10; loop = $true }` in `$clipRules` in
   `tools/make_sprites_json.ps1` (anything unlisted loops at 10fps).
4. `.\tools\make_character.ps1 -Only climb`, then `.\tools\make_sprites_json.ps1`.
5. Play it from gameplay with `sprite.Play("climb", true)`. Characters without
   the clip fall back to `idle`, so older CraftPix rigs will not break.

To speed a clip up at runtime (e.g. attack speed) set `sprite.speed_scale`
rather than rendering a faster copy.

## A different character

Copy the build function or parameterise it: `PALETTE` holds the colours;
`build_character` holds the proportions. Keep the **body base light and
low-contrast** -- armour tint multiplies it, and a dark base turns every armour
black. Render with `make_character.ps1 -Name <id>`, register with
`make_sprites_json.ps1`, and add the id to `Game::kCharacterIds` /
`kCharacterLabels` if it is playable.

Proportions that read as a character at ~28px tall: head about 40% of height,
rounded (bevel ~0.13 on a 0.44 cube), short torso, arms held ~11 degrees out
so they exist in silhouette, eyes as two large dark blocks set well apart.
`FRAME_SPAN = 3.5` puts the figure at roughly the height of the CraftPix rigs.

## Judging the result

```
python .claude/skills/blender-pixel-assets/scripts/preview_character.py walk 6 4 out.png
```

Shows every frame of every row at 4x beside the CraftPix player for scale.
Check, in order: does the down row have a face; do arms exist in silhouette;
does each frame differ enough to animate; is it about the height of the
reference; does the up row read as the back of the same person.

## Failures seen

| Looked like | Cause | Fix |
| --- | --- | --- |
| Brown block, no face | Facings 180 degrees out | Model faces -Y; down = 0 |
| Only the top of the head | Camera sees down onto the skull | `HEAD_PITCH` lean on its own pivot |
| A head on stubs | Head ~60% of height, cubic | Shrink to ~40%, round it |
| No arms | Shoulders inside the torso silhouette | Wider shoulders plus `ARM_FLARE` |
| Sliding, not walking | Realistic angles | Exaggerate 2x |
| Hair is a helmet in profile | Long side tufts | Short tufts at the temples only |
| Blurry edges between layers | Soft alpha | Hard alpha in `make_character.ps1` (keep it) |
