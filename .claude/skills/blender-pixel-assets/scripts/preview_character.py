"""Composites a rendered character clip's layers, reduces it to game size, and
shows every frame of every facing row beside the CraftPix player for scale.

    python preview_character.py CLIP FRAMES ZOOM OUT.png [RENDER_DIR]

Run from the DreamQuest repo root after blender_character.py has rendered the
clip (RENDER_DIR defaults to assets/_render/character). Rows are down, left,
right, up. Also prints the down-row frame-0 bounding box, so height can be
compared with the reference numerically (the CraftPix rig is about 20x25).
"""
import os
import sys
from PIL import Image

LAYERS = ["1_shadow", "2_weapon_back", "3_body", "4_weapon_front", "5_head"]
REFERENCE = "assets/characters/player_male/layers"


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)
    clip, frames, zoom, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    render_dir = sys.argv[5] if len(sys.argv) > 5 else "assets/_render/character"

    base = None
    for suffix in LAYERS:
        p = os.path.join(render_dir, "%s_%s.png" % (clip, suffix))
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGBA")
        base = im if base is None else Image.alpha_composite(base, im)
    if base is None:
        sys.exit("no layers found for clip '%s' in %s" % (clip, render_dir))

    small = base.resize((64 * frames, 64 * 4), Image.BOX)

    ref = None
    if os.path.isdir(REFERENCE):
        for n in ("idle_1_shadow", "idle_2_sword_back", "idle_3_body",
                  "idle_4_sword_front", "idle_5_head"):
            p = os.path.join(REFERENCE, n + ".png")
            if os.path.exists(p):
                im = Image.open(p).convert("RGBA").crop((0, 0, 64, 64))
                ref = im if ref is None else Image.alpha_composite(ref, im)

    cell = 64 * zoom
    canvas = Image.new("RGBA", (cell * (frames + 1) + 30, cell * 4 + 20), (86, 112, 70, 255))
    if ref is not None:
        canvas.alpha_composite(ref.resize((cell, cell), Image.NEAREST), (10, 10))
    for row in range(4):
        for col in range(frames):
            f = small.crop((col * 64, row * 64, col * 64 + 64, row * 64 + 64))
            canvas.alpha_composite(f.resize((cell, cell), Image.NEAREST),
                                   (cell + 20 + col * cell, 10 + row * cell))
    canvas.save(out)
    print("%s  bbox(down, frame 0) %s" % (out, small.crop((0, 0, 64, 64)).getbbox()))


if __name__ == "__main__":
    main()
