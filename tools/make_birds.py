"""The ambient birds: assets/effects/birds.png.

Three birds, a row each -- a sparrow, a crow, an egret -- and six frames each,
16 px square, facing right (the game flips them):

    0 standing   1 pecking   2 mid-hop   3 wings up   4 wings level   5 wings down

Drawn as pixel art from a handful of shapes per frame -- a body, a head, a
beak, a tail, legs, wings -- in each bird's colours, then given a one-pixel
outline a shade of whatever it borders. Original art; nothing traced.

    python tools/make_birds.py
"""
import os

from PIL import Image, ImageDraw

CELL = 16
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "effects", "birds.png")

BIRDS = {
    # body, belly, head, wing, beak, legs; size scale; neck
    "sparrow": dict(body=(150, 104, 60), belly=(214, 186, 140), head=(120, 78, 44), wing=(108, 72, 42),
                    beak=(70, 54, 34), legs=(110, 86, 60), scale=0.8, neck=0),
    "crow": dict(body=(46, 46, 60), belly=(62, 62, 78), head=(38, 38, 50), wing=(30, 30, 42),
                 beak=(34, 34, 42), legs=(40, 40, 46), scale=1.0, neck=0),
    "egret": dict(body=(238, 238, 232), belly=(226, 226, 220), head=(246, 246, 240), wing=(216, 220, 226),
                  beak=(232, 176, 56), legs=(60, 60, 54), scale=1.0, neck=2),
}


def bird(cols, frame):
    im = Image.new("RGBA", (CELL * 4, CELL * 4), (0, 0, 0, 0))   # drawn at 4x, then reduced
    d = ImageDraw.Draw(im)
    s = cols["scale"]
    k = 4.0

    def P(x, y):
        # Shrink round the middle of the cell's foot for a smaller bird.
        return ((8 + (x - 8) * s) * k, (13 + (y - 13) * s) * k)

    def ell(x0, y0, x1, y1, c):
        a, b = P(x0, y0), P(x1, y1)
        d.ellipse([a[0], a[1], b[0], b[1]], fill=c)

    def poly(pts, c):
        d.polygon([P(x, y) for x, y in pts], fill=c)

    flying = frame >= 3
    lift = -2 if frame == 2 else 0
    neck = cols["neck"]
    if not flying:
        # Legs, tail, body, belly, head, beak.
        legs = cols["legs"]
        if frame != 2:
            for lx in (7, 9):
                poly([(lx, 11), (lx + 0.8, 11), (lx + 0.8, 13), (lx, 13)], legs)
        tail = [(2, 9 + lift), (5, 8 + lift), (5, 10 + lift)]
        poly(tail, cols["wing"])
        ell(4, 6.5 + lift, 11, 11.5 + lift, cols["body"])
        ell(5.5, 9 + lift, 10.5, 11.5 + lift, cols["belly"])
        ell(5, 7 + lift, 9.5, 10 + lift, cols["wing"])
        if frame == 1:   # pecking: head down to the ground
            ell(9.5, 8.5, 13, 12, cols["head"])
            poly([(12.6, 10.6), (14.6, 11.6), (12.6, 11.8)], cols["beak"])
        else:
            hy = 3.5 - neck + lift
            if neck:
                poly([(9.5, 8 + lift), (11, 8 + lift), (11.5, hy + 2), (10.5, hy + 2)], cols["head"])
            ell(9.5, hy, 13, hy + 3.5, cols["head"])
            poly([(12.6, hy + 1.4), (14.8, hy + 2.0), (12.6, hy + 2.6)], cols["beak"])
    else:
        # From above: a body, a head, a fanned tail, and the two wings out to
        # either side -- swept back and short on the up-stroke, spread wide
        # level, short and reaching forward on the down-stroke. The far wing
        # (below) a shade darker.
        y = 8.0
        far = tuple(int(c * 0.82) for c in cols["wing"])
        tip = {3: (4.2, 3.8), 4: (5.6, 1.2), 5: (8.4, 4.4)}[frame]
        poly([(5.6, y - 0.6), (9.2, y - 0.6), (tip[0] + 2.8, tip[1]), (tip[0], tip[1] + 0.6)], cols["wing"])
        poly([(5.6, y + 0.6), (9.2, y + 0.6), (tip[0] + 2.8, 2 * y - tip[1]), (tip[0], 2 * y - tip[1] - 0.6)], far)
        poly([(1.2, y - 1.6), (4.6, y), (1.2, y + 1.6)], cols["wing"])
        ell(3.5, y - 1.5, 11.5, y + 1.5, cols["body"])
        ell(10, y - 1.6, 13.4, y + 1.6, cols["head"])
        poly([(13.1, y - 0.5), (15.2, y), (13.1, y + 0.5)], cols["beak"])
    small = im.resize((CELL, CELL), Image.BOX)
    # Snap to solid pixels: anything more than half there is there.
    px = small.load()
    for yy in range(CELL):
        for xx in range(CELL):
            r, g, b, a = px[xx, yy]
            px[xx, yy] = (r, g, b, 255) if a > 110 else (0, 0, 0, 0)
    # A one-pixel outline, a dark shade of what it borders.
    out = small.copy()
    po = out.load()
    for yy in range(CELL):
        for xx in range(CELL):
            if px[xx, yy][3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = xx + dx, yy + dy
                if 0 <= nx < CELL and 0 <= ny < CELL and px[nx, ny][3]:
                    r, g, b, _ = px[nx, ny]
                    po[xx, yy] = (int(r * 0.35) + 10, int(g * 0.32) + 8, int(b * 0.35) + 14, 255)
                    break
    return out


def main():
    sheet = Image.new("RGBA", (CELL * 6, CELL * len(BIRDS)), (0, 0, 0, 0))
    for row, (name, cols) in enumerate(BIRDS.items()):
        for frame in range(6):
            sheet.alpha_composite(bird(cols, frame), (frame * CELL, row * CELL))
    sheet.save(OUT)
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
