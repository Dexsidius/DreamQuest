#!/usr/bin/env python3
# =============================================================================
#  make_town_tiles.py - the ground for the rebuilt towns.
#
#      python tools/make_town_tiles.py
#
#  Two families, three variants each, in assets/tiles/:
#
#    plaza       Havenbrook's square: big pale flagstones, a size up from the
#                road's setts, so the square reads as a place and the streets
#                as the ways into it.
#    moss_stone  Mossvale's lanes: grey-green flags laid irregularly, with moss
#                in every joint and creeping over the edges -- stone the forest
#                is taking back, where Havenbrook's is swept.
#    civic_floor The mayor's hall: a chequer of cream and slate squares.
#    mine_floor, mine_rock, rail_ew, rail_ns
#                The dwarves' mine under Mossvale: trodden earth, the rock
#                round it, and track to lay over the floor.
#
#  The same masonry as tools/make_ground.ps1's New-Masonry -- courses whose
#  joints sit at fixed places modulo the tile, so a stone that runs off one
#  edge carries on at the other and the tiles are seamless -- written in
#  Python because it was made on a machine with no PowerShell. Needs Pillow.
# =============================================================================

import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TILES = os.path.join(ROOT, "assets", "tiles")
SIZE = 32


def shade(rgb, amount):
    if amount >= 0:
        return tuple(int(c + (255 - c) * amount) for c in rgb)
    return tuple(int(c * (1 + amount)) for c in rgb)


def masonry(rng, rgb, mortar, course, unit, jitter, stagger=0.5):
    """Running-bond stones with a lit top-left edge and a shaded foot."""
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()

    def put(x, y, c):
        px[x % SIZE, y % SIZE] = c + (255,)

    stones = []
    for c in range(SIZE // course):
        y0 = c * course
        offset = int(unit * stagger) if c % 2 else 0
        for u in range(SIZE // unit):
            face = shade(rgb, (rng.random() - 0.5) * jitter)
            x0 = u * unit + offset
            stones.append((x0, y0, face))
            for yy in range(course):
                for xx in range(unit):
                    if yy == course - 1 or xx == unit - 1:
                        put(x0 + xx, y0 + yy, mortar)
                    elif yy == 0 or xx == 0:
                        put(x0 + xx, y0 + yy, shade(face, 0.12))
                    elif yy == course - 2:
                        put(x0 + xx, y0 + yy, shade(face, -0.10))
                    else:
                        put(x0 + xx, y0 + yy, face)
            for _ in range(4):
                put(x0 + 1 + rng.randrange(max(1, unit - 3)), y0 + 1 + rng.randrange(max(1, course - 3)),
                    shade(face, -0.14))
    return img, stones


def plaza(rng):
    img, _ = masonry(rng, (170, 160, 144), (112, 102, 90), 16, 16, 0.18)
    return img


def moss_stone(rng):
    # Smaller, rougher flags than the plaza's, in a bond that shifts by a
    # third so the joints do not line up into a grid.
    img, stones = masonry(rng, (124, 126, 112), (70, 96, 50), 8, 16, 0.26, stagger=0.35)
    px = img.load()
    moss = [(86, 116, 58), (104, 134, 66), (72, 98, 48)]
    # Moss creeping out of the joints over the stone's edges, in clumps.
    for _ in range(80):
        x, y = rng.randrange(SIZE), rng.randrange(SIZE)
        r, g, b, _a = px[x, y]
        if g > r + 10:          # already moss or a joint: grow from there
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1)):
                if rng.random() < 0.6:
                    px[(x + dx) % SIZE, (y + dy) % SIZE] = moss[rng.randrange(3)] + (255,)
    for _ in range(6):
        # And a few patches out on the stone itself.
        x, y = rng.randrange(SIZE), rng.randrange(SIZE)
        for dx in range(-1, 2):
            for dy in range(-1, 1):
                if rng.random() < 0.7:
                    px[(x + dx) % SIZE, (y + dy) % SIZE] = moss[rng.randrange(3)] + (255,)
    return img


def civic_floor(rng):
    """The mayor's hall: a chequer of cream and slate squares, polished, with
    a hairline of grout -- a floor that says the town paid for it."""
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    # Low contrast: at full strength a chequer is all anyone sees of a room.
    cream, slate, grout = (206, 196, 172), (152, 150, 152), (112, 106, 98)
    for y in range(SIZE):
        for x in range(SIZE):
            dark = ((x // 16) + (y // 16)) % 2 == 1
            base = slate if dark else cream
            c = shade(base, (rng.random() - 0.5) * 0.05)
            if x % 16 == 15 or y % 16 == 15:
                c = grout
            elif x % 16 == 0 or y % 16 == 0:
                c = shade(base, 0.10)
            px[x, y] = c + (255,)
    # A polished sheen: a lighter streak across each square.
    for sq in range(4):
        ox, oy = (sq % 2) * 16, (sq // 2) * 16
        for k in range(3, 9):
            x, y = ox + k, oy + 11 - k
            r, g, b, a = px[x, y]
            px[x, y] = shade((r, g, b), 0.12) + (255,)
    return img


def mine_floor(rng):
    """The dwarves' tunnels: packed earth and gravel, trodden flat."""
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    base = (104, 88, 72)
    for y in range(SIZE):
        for x in range(SIZE):
            px[x, y] = shade(base, (rng.random() - 0.5) * 0.10) + (255,)
    for _ in range(40):
        x, y = rng.randrange(SIZE), rng.randrange(SIZE)
        c = shade((140, 132, 124), (rng.random() - 0.5) * 0.3)
        px[x, y] = c + (255,)
        if rng.random() < 0.4:
            px[(x + 1) % SIZE, y] = shade(c, -0.25) + (255,)
    return img


def mine_rock(rng):
    """Solid rock round the tunnels: dark, lumpy and cracked -- not coursed,
    or the mine reads as a cellar built of brick."""
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    # Wrapped value noise at two scales, so the tile is seamless.
    def lattice(n):
        return [[rng.random() for _ in range(n)] for _ in range(n)]
    coarse, fine = lattice(4), lattice(8)
    def sample(grid, n, x, y):
        gx, gy = x * n / SIZE, y * n / SIZE
        x0, y0 = int(gx) % n, int(gy) % n
        x1, y1 = (x0 + 1) % n, (y0 + 1) % n
        fx, fy = gx - int(gx), gy - int(gy)
        top = grid[y0][x0] * (1 - fx) + grid[y0][x1] * fx
        bot = grid[y1][x0] * (1 - fx) + grid[y1][x1] * fx
        return top * (1 - fy) + bot * fy
    base = (76, 68, 62)
    for y in range(SIZE):
        for x in range(SIZE):
            v = sample(coarse, 4, x, y) * 0.65 + sample(fine, 8, x, y) * 0.35
            px[x, y] = shade(base, (v - 0.5) * 0.55) + (255,)
    # Cracks: short wandering dark lines, lit on their lower edge.
    for _ in range(5):
        x, y = rng.randrange(SIZE), rng.randrange(SIZE)
        for _ in range(rng.randrange(5, 11)):
            px[x % SIZE, y % SIZE] = (36, 30, 28, 255)
            px[x % SIZE, (y + 1) % SIZE] = shade(px[x % SIZE, (y + 1) % SIZE][:3], 0.12) + (255,)
            x += rng.choice((-1, 0, 1, 1))
            y += rng.choice((-1, 0, 1))
    for _ in range(14):
        x, y = rng.randrange(SIZE), rng.randrange(SIZE)
        px[x, y] = shade(px[x, y][:3], 0.25) + (255,)
    return img


def rails(horizontal):
    """A length of track over the floor, transparent between the sleepers,
    laid over the ground as an overlay: two iron rails on oak sleepers."""
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    for k in range(4):
        for a in range(4, 28):
            for b in range(k * 8 + 2, k * 8 + 6):
                x, y = (b, a) if horizontal else (a, b)
                edge = b == k * 8 + 5
                px[x, y] = ((74, 50, 32) if edge else (110, 76, 48)) + (255,)
    for rail in (9, 22):
        for t in range(SIZE):
            for w in range(2):
                x, y = (t, rail + w) if horizontal else (rail + w, t)
                px[x, y] = ((168, 172, 180) if w == 0 else (96, 98, 106)) + (255,)
    return img


def main():
    made = 0
    for name, horizontal in (("rail_ew", True), ("rail_ns", False)):
        rails(horizontal).save(os.path.join(TILES, name + ".png"))
        made += 1
    for name, make, seed in (("plaza", plaza, 20270601), ("moss_stone", moss_stone, 20270602),
                             ("civic_floor", civic_floor, 20270603), ("mine_floor", mine_floor, 20270604),
                             ("mine_rock", mine_rock, 20270605)):
        rng = random.Random(seed)
        for v in range(3):
            img = make(rng)
            out = name if v == 0 else "%s_%d" % (name, v)
            img.save(os.path.join(TILES, out + ".png"))
            made += 1
    print("  %d tiles drawn into assets/tiles" % made)


if __name__ == "__main__":
    main()
