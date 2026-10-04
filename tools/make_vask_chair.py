"""Cuts Elder Vask's empty rocking chair out of its creature sheet as a prop.

blender_act1.build_vask_chair renders the chair he rocks in with nobody in it,
through the creature pipeline, so it is the same chair at the same scale and
with the same outline as the one in assets/characters/vask/. His own dream
stands him on his feet in front of it (scene 19), and a chair that never moves
is a prop: the facing frame, a contact shadow drawn the way make_props.ps1
draws one, and sat on the floor -- a prop is placed by the bottom edge of its
picture.

    .\\tools\\make_creatures.ps1 -Only vask_chair
    python tools/make_vask_chair.py
    .\\tools\\make_manifest.ps1
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEET = os.path.join(ROOT, "assets", "characters", "vask_chair", "idle.png")
OUT = os.path.join(ROOT, "assets", "props", "vask_chair_empty.png")
FRAME = 64          # blender_act1.CREATURES["vask_chair"]


def contact_shadow(im, depth=3, alpha=92):
    """make_props.ps1's Add-ContactShadow: under each column's lowest solid
    pixel, fading over a few rows, a little to the lower right."""
    px = im.load()
    w, h = im.size
    solid = [[px[x, y][3] >= 250 for y in range(h)] for x in range(w)]
    for x in range(w):
        base = next((y for y in range(h - 1, -1, -1) if solid[x][y]), -1)
        if base < 0:
            continue
        for d in range(1, depth + 1):
            sy, sx = base + d, x + d // 2
            if sy >= h or sx >= w or solid[sx][sy]:
                continue
            fade = int(alpha * (1.0 - (d - 1) / depth))
            if px[sx, sy][3] >= fade:
                continue
            px[sx, sy] = (30, 36, 44, fade)


def main():
    sheet = Image.open(SHEET).convert("RGBA")
    # Row 0 is the chair facing down the screen, as he faces the street.
    frame = sheet.crop((0, 0, FRAME, FRAME))
    # Room under it for the shadow, then trimmed to what is drawn.
    room = Image.new("RGBA", (FRAME + 4, FRAME + 4), (0, 0, 0, 0))
    room.paste(frame, (0, 0))
    contact_shadow(room)
    left, top, right, bottom = room.getbbox()
    # Even width, so its middle is a whole pixel when it is placed by its centre.
    if (right - left) % 2:
        right += 1
    out = room.crop((left, top, right, bottom))
    out.save(OUT)
    print("%s %dx%d" % (os.path.relpath(OUT, ROOT), out.size[0], out.size[1]))


if __name__ == "__main__":
    main()
