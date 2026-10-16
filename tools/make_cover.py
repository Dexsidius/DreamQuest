"""
Solace/Reverie -- the title painting, the logo and the store cover.

tools/blender_cover.py renders the painting's foreground: the sword in its
cairn, the ruined pillars, the grass. This paints everything behind it, in the
painting's own pixels (480x270), and puts the two together:

  - the night sky in dithered bands, stars, and four-pointed sparkles;
  - the crescent moon behind the blade, with its halo;
  - purple cloud banks, the far spires of the dream country, and two islands
    floating over it -- the Reverie, where the game goes when it dreams;
  - the foreground brought down to painting pixels, its colours gathered into
    a small palette, with a dark outline round it, as the sprites have;
  - the blade's glow, and sparks drifting off it.

The logo is drawn here too, letter by letter, from glyphs defined below (no
font file: every letter is this game's own). Nothing is drawn by an image
model; every pixel comes from this file or the Blender scene.

Usage, from the repository root (Pillow is the only requirement):
    python tools/make_cover.py
writes
    art/dreamquest_cover.png   the title painting at x4 (1920x1080), which the
                               main menu draws behind everything
    art/logo.png               the logo at one pixel per logo pixel; the main
                               menu scales it up by a whole number
    docs/itch/cover.png        itch.io's cover image, 630x500, logo and all
    docs/itch/banner.png       the page's banner, 960x240, logo on the night sky
    docs/itch/background.png   the page's background, the painting dimmed
then run tools/make_titleart.ps1 for the window and .exe icons.
"""

import math
import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER = os.path.join(ROOT, "assets", "_render", "cover_foreground.png")
OUT_PAINTING = os.path.join(ROOT, "art", "dreamquest_cover.png")
OUT_LOGO = os.path.join(ROOT, "art", "logo.png")
OUT_ITCH = os.path.join(ROOT, "docs", "itch", "cover.png")
OUT_BANNER = os.path.join(ROOT, "docs", "itch", "banner.png")
OUT_BACKGROUND = os.path.join(ROOT, "docs", "itch", "background.png")

W, H = 480, 270                  # the painting, in its own pixels
SUPERSAMPLE = 4                  # as blender_cover.py rendered it
PX_PER_UNIT = 50.0
GROUND_Y = 258.0
ELEVATION = 7.0

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bayer(x, y):
    return (BAYER[y % 4][x % 4] + 0.5) / 16.0


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def screen_y(z, depth=0.0):
    """Where a point of the Blender scene lands, in painting pixels down."""
    e = math.radians(ELEVATION)
    return GROUND_Y - (z * math.cos(e) + depth * math.sin(e)) * PX_PER_UNIT


def screen_x(x):
    return W / 2 + x * PX_PER_UNIT


# --- the sky ------------------------------------------------------------------

SKY = [(10, 9, 40), (16, 13, 58), (24, 18, 76), (34, 24, 94), (46, 30, 110),
       (60, 38, 124), (76, 48, 138), (96, 60, 152)]
SKY_FOOT = 236                   # the last band is reached here


def paint_sky(img):
    px = img.load()
    for y in range(H):
        f = min(1.0, y / SKY_FOOT) ** 1.15 * (len(SKY) - 1)
        i = min(int(f), len(SKY) - 2)
        frac = f - i
        for x in range(W):
            px[x, y] = SKY[i + 1] if frac > bayer(x, y) else SKY[i]


MOON = (240, 112)
MOON_R = 64
MOON_CUT = (MOON[0] + 27, MOON[1] - 15)
MOON_CUT_R = 57


def in_moon(x, y):
    dx, dy = x - MOON[0], y - MOON[1]
    cx, cy = x - MOON_CUT[0], y - MOON_CUT[1]
    return dx * dx + dy * dy <= MOON_R * MOON_R and cx * cx + cy * cy > MOON_CUT_R * MOON_CUT_R


def paint_moon(img):
    px = img.load()
    # The halo first, round the whole disc: two dithered steps lighter.
    for y in range(max(0, MOON[1] - MOON_R - 30), min(H, MOON[1] + MOON_R + 30)):
        for x in range(MOON[0] - MOON_R - 30, MOON[0] + MOON_R + 31):
            d = math.hypot(x - MOON[0], y - MOON[1]) - MOON_R
            if d < 0 or d > 28:
                continue
            t = (1.0 - d / 28.0) ** 1.6
            if t * 0.9 > bayer(x, y):
                px[x, y] = mix(px[x, y], (150, 120, 200), 0.22 + 0.18 * t)
    craters = [(-38, 18, 7), (-44, -6, 5), (-26, 38, 6), (-12, 50, 4), (-50, 10, 3), (-34, -24, 4)]
    for y in range(MOON[1] - MOON_R, MOON[1] + MOON_R + 1):
        for x in range(MOON[0] - MOON_R, MOON[0] + MOON_R + 1):
            if not in_moon(x, y):
                continue
            # How close to the shadowed edge: the inside of the crescent is
            # deeper gold, the outside lit pale.
            inner = math.hypot(x - MOON_CUT[0], y - MOON_CUT[1]) - MOON_CUT_R
            outer = MOON_R - math.hypot(x - MOON[0], y - MOON[1])
            c = (250, 226, 150)
            if inner < 5:
                c = (226, 190, 108)
            elif inner < 11 and bayer(x, y) > 0.5:
                c = (238, 208, 128)
            if outer < 2.2 and x < MOON[0] + 10:
                c = (255, 244, 206)
            for (cx, cy, r) in craters:
                if math.hypot(x - (MOON[0] + cx), y - (MOON[1] + cy)) <= r:
                    c = mix(c, (214, 176, 98), 0.75)
                    break
            px[x, y] = c


def sparkle(px, x, y, colour, arm=1, core=(255, 255, 255)):
    for i in range(1, arm + 1):
        fade = mix(colour, (40, 30, 90), 0.35 * (i - 1))
        for (dx, dy) in ((i, 0), (-i, 0), (0, i), (0, -i)):
            if 0 <= x + dx < W and 0 <= y + dy < H:
                px[x + dx, y + dy] = fade
    if 0 <= x < W and 0 <= y < H:
        px[x, y] = core


def paint_stars(img, rng):
    px = img.load()
    for _ in range(190):
        x, y = rng.randrange(W), rng.randrange(0, 190)
        if math.hypot(x - MOON[0], y - MOON[1]) < MOON_R + 20:
            continue
        c = rng.choice([(70, 66, 140), (110, 104, 180), (160, 156, 220), (236, 236, 255), (255, 226, 160)])
        px[x, y] = c
    for (x, y, arm, c) in ((86, 34, 2, (255, 222, 140)), (402, 30, 2, (255, 222, 140)), (150, 100, 1, (255, 236, 180)),
                           (338, 82, 1, (255, 236, 180)), (40, 120, 1, (220, 220, 255)), (446, 128, 1, (220, 220, 255)),
                           (182, 22, 1, (255, 236, 180)), (302, 18, 1, (220, 220, 255)), (120, 62, 1, (220, 220, 255))):
        sparkle(px, x, y, c, arm)


# --- clouds, spires, isles --------------------------------------------------

def cloud_bank(img, rng, x0, x1, y_mid, height, colours, puffs):
    """Puffs, flat along the bottom: a lit top, a body, a shaded underside."""
    top, body, under = colours
    px = img.load()
    circles = []
    for _ in range(puffs):
        r = rng.uniform(height * 0.45, height)
        circles.append((rng.uniform(x0, x1), y_mid - rng.uniform(0, height * 0.35), r))
    bottom = y_mid + height * 0.35
    inside = set()
    for (cx, cy, r) in circles:
        for y in range(int(cy - r), int(min(bottom, cy + r)) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if 0 <= x < W and 0 <= y < H and (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    inside.add((x, y))
    for (x, y) in inside:
        if (x, y - 1) not in inside or (x, y - 2) not in inside and bayer(x, y) > 0.4:
            px[x, y] = top
        elif y > bottom - 3 or (y > bottom - 6 and bayer(x, y) > 0.5):
            px[x, y] = under
        else:
            px[x, y] = body


def paint_clouds(img, rng):
    far = ((70, 50, 136), (52, 36, 112), (40, 28, 94))
    near = ((128, 96, 196), (92, 64, 160), (66, 44, 128))
    for (x0, x1, y, h, n) in ((0, 120, 40, 12, 7), (380, 480, 34, 11, 7), (0, 190, 150, 20, 12),
                              (290, 480, 140, 22, 12)):
        cloud_bank(img, rng, x0, x1, y, h, far, n)
    for (x0, x1, y, h, n) in ((-20, 150, 186, 22, 12), (330, 500, 180, 24, 12), (150, 330, 206, 14, 10)):
        cloud_bank(img, rng, x0, x1, y, h, near, n)


def paint_spires(img, rng):
    """The dream country's towers, far off on the right, a few windows lit."""
    px = img.load()
    dark, rim = (36, 25, 84), (58, 40, 116)
    base = 236
    for (x, w, h) in ((338, 7, 34), (350, 10, 58), (364, 6, 40), (373, 12, 74), (389, 8, 48), (400, 6, 30),
                      (409, 9, 42), (96, 8, 30), (108, 6, 44), (118, 10, 26)):
        roof = w // 2 + 4
        for y in range(base - h - roof, base):
            for xx in range(x, x + w):
                if y < base - h:
                    # A spire narrowing to its point.
                    k = (base - h - y) / roof
                    if abs(xx - (x + w / 2 - 0.5)) > (w / 2) * (1 - k):
                        continue
                if 0 <= xx < W:
                    px[xx, y] = rim if xx == x else dark
        for _ in range(2):
            wy = rng.randrange(base - h + 4, base - 6)
            wx = x + rng.randrange(1, max(2, w - 1))
            px[wx, wy] = (255, 206, 120)


def paint_isles(img, rng):
    """Islands of earth floating over the dream country: a grassy top with a
    bush or two, a jagged rock underside, and from the larger a thread of
    water falling away into the cloud."""
    px = img.load()
    for (cx, cy, rw, depth, fall) in ((104, 96, 22, 22, True), (394, 78, 15, 15, False)):
        jag = {}
        for x in range(cx - rw, cx + rw + 1):
            if (x - cx) % 3 == 0 or x not in jag:
                step = rng.uniform(-2.5, 2.5)
            jag[x] = step
        for x in range(cx - rw, cx + rw + 1):
            k = abs(x - cx) / rw
            bottom = int(depth * (1 - k ** 1.7) + jag[x] * (1 - k))
            for y in range(cy, cy + max(2, bottom)):
                d = (y - cy) / max(1, depth)
                c = (54, 40, 112) if d < 0.45 else (40, 30, 90)
                if x <= cx - rw + 1 or (y > cy and abs(x - cx) > rw * (1 - d) - 1.5):
                    c = (74, 56, 140)
                px[x, y] = c
            px[x, cy - 1] = (70, 116, 104)
            px[x, cy - 2] = (92, 146, 120) if (x - cx) % 5 else (70, 116, 104)
        for (dx, r) in ((-rw * 0.55, 3), (rw * 0.25, 4), (rw * 0.62, 2)):
            bx = int(cx + dx)
            for y in range(cy - 2 - r, cy - 1):
                for x in range(bx - r, bx + r + 1):
                    if (x - bx) ** 2 + (y - (cy - 2)) ** 2 <= r * r:
                        px[x, y] = (96, 150, 120) if y == cy - 2 - r + 1 or x == bx - r + 1 else (64, 108, 96)
        if fall:
            wx = cx + rw // 2
            for y in range(cy + depth // 2, cy + depth // 2 + 52):
                t = (y - cy - depth // 2) / 52.0
                for x in (wx, wx + 1):
                    if 1 - t > bayer(x, y) * 1.1:
                        px[x, y] = (214, 226, 255) if (y + x) % 3 == 0 else (150, 172, 234)


def paint_mist(img):
    px = img.load()
    for y in range(206, H):
        t = min(1.0, (y - 206) / 30.0)
        for x in range(W):
            if t * 0.7 > bayer(x, y):
                px[x, y] = mix(px[x, y], (128, 100, 182), 0.35)


# --- the foreground -----------------------------------------------------------

OUTLINE = (10, 8, 30)


def foreground():
    """The Blender render at painting size: box-averaged (premultiplied, so
    the transparent sky does not grey the edges), cut at half coverage, its
    colours gathered into a small palette, outlined."""
    big = Image.open(RENDER).convert("RGBA")
    small = big.convert("RGBa").resize((W, H), Image.BOX).convert("RGBA")
    px = small.load()
    solid = [[px[x, y][3] >= 128 for x in range(W)] for y in range(H)]
    rgb = Image.new("RGB", (W, H), (0, 0, 0))
    rp = rgb.load()
    fill = None
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                rp[x, y] = px[x, y][:3]
                fill = fill or px[x, y][:3]
    for y in range(H):
        for x in range(W):
            if not solid[y][x]:
                rp[x, y] = fill
    pal = rgb.quantize(colors=64, method=Image.Quantize.MAXCOVERAGE, dither=Image.Dither.NONE).convert("RGB")
    pp = pal.load()
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    op = out.load()
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                op[x, y] = pp[x, y] + (255,)
            elif any(0 <= x + dx < W and 0 <= y + dy < H and solid[y + dy][x + dx]
                     for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                op[x, y] = OUTLINE + (255,)
    return out


def paint_glow(img, rng, fg):
    """Light off the blade into the air round it, and sparks."""
    px = img.load()
    fp = fg.load()
    x0, x1 = screen_x(-0.17), screen_x(0.17)
    y0, y1 = screen_y(3.02), screen_y(1.46)
    for y in range(int(y0) - 8, int(y1) + 2):
        for x in range(int(x0) - 9, int(x1) + 10):
            dx = max(x0 - x, 0, x - x1)
            dy = max(y0 - y, 0, y - y1)
            d = math.hypot(dx, dy)
            if d <= 0.5 or d > 8:
                continue
            if fp[x, y][3] and fp[x, y][:3] != OUTLINE:
                continue      # the stones have their own light from Blender
            t = (1 - d / 8) ** 1.4
            if t > bayer(x, y) * 0.9:
                px[x, y] = mix(px[x, y], (120, 190, 255), 0.25 + 0.4 * t)
    for _ in range(22):
        x = int(rng.uniform(x0 - 34, x1 + 34))
        y = int(rng.uniform(y0 - 6, y1 + 10))
        if x0 - 3 <= x <= x1 + 3:
            continue
        sparkle(px, x, y, (110, 180, 255), arm=rng.choice((0, 1, 1, 2)), core=(220, 240, 255))
    for _ in range(14):
        x, y = rng.randrange(20, W - 20), rng.randrange(196, 236)
        px[x, y] = rng.choice(((200, 255, 150), (255, 240, 140)))


def painting():
    rng = random.Random(1015)
    img = Image.new("RGB", (W, H))
    paint_sky(img)
    paint_stars(img, rng)
    paint_clouds(img, rng)
    paint_isles(img, rng)
    paint_spires(img, rng)
    paint_moon(img)
    paint_mist(img)
    fg = foreground()
    img.paste(fg, (0, 0), fg)
    paint_glow(img, rng, fg)
    return img


# --- the logo -----------------------------------------------------------------
# Two faces, both drawn here. The title's capitals are nine font pixels tall
# with two-pixel strokes, each font pixel two logo pixels across; the
# subtitle's are seven tall, one pixel each.

TITLE = {
    "S": [".######.", "##....##", "##......", "##......", ".######.", "......##", "......##", "##....##", ".######."],
    "O": [".######.", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##", ".######."],
    "L": ["####....", ".##.....", ".##.....", ".##.....", ".##.....", ".##.....", ".##.....", ".##...##", "########"],
    "A": ["...##...", "..####..", ".##..##.", "##....##", "##....##", "########", "##....##", "##....##", "###..###"],
    "C": [".######.", "##....##", "##......", "##......", "##......", "##......", "##......", "##....##", ".######."],
    "E": ["########", ".##....#", ".##.....", ".##.....", ".######.", ".##.....", ".##.....", ".##....#", "########"],
    "R": ["#######.", ".##...##", ".##...##", ".##...##", ".######.", ".##.##..", ".##..##.", ".##...##", "###...##"],
    "V": ["##....##", "##....##", "##....##", "##....##", ".##..##.", ".##..##.", "..####..", "..####..", "...##..."],
    "I": ["######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "######"],
    "/": [".....##", "....##.", "....##.", "...##..", "...##..", "..##...", "..##...", ".##....", ".##...."],
}

SUB = {
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "I": ["###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"],
    "N": ["#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
}

GOLD_RAMP = [(255, 250, 222), (255, 236, 160), (255, 214, 104), (246, 184, 70), (228, 150, 50), (196, 112, 40)]
SUB_RAMP = [(240, 234, 255), (226, 216, 255), (204, 190, 248)]
LOGO_INK = (26, 12, 56)
LOGO_RING = (64, 36, 120)


def set_text(face, text, scale, spacing, space):
    """The letters' filled pixels, in logo pixels, and the width they take."""
    cells, x = set(), 0
    for ch in text:
        if ch == " ":
            x += space
            continue
        rows = face[ch]
        for gy, row in enumerate(rows):
            for gx, c in enumerate(row):
                if c == "#":
                    for sy in range(scale):
                        for sx in range(scale):
                            cells.add((x + gx * scale + sx, gy * scale + sy))
        x += len(rows[0]) * scale + spacing
    return cells, x - spacing


def logo():
    title, tw = set_text(TITLE, "SOLACE/REVERIE", 2, 2, 6)
    sub, sw = set_text(SUB, "QUEST INTO THE DREAMSCAPE", 1, 1, 4)
    title_h, sub_h = 9 * 2, 7
    pad = 3
    gap = 5
    width = max(tw, sw + 16) + pad * 2
    height = pad + title_h + gap + sub_h + pad + 2
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    px = img.load()
    tx, ty = (width - tw) // 2, pad
    sx, sy = (width - sw) // 2, pad + title_h + gap

    filled = {}
    for (x, y) in title:
        k = y / (title_h - 1)
        c = GOLD_RAMP[min(len(GOLD_RAMP) - 1, int(k * len(GOLD_RAMP)))]
        if (x, y - 1) not in title:
            c = GOLD_RAMP[0]
        filled[(tx + x, ty + y)] = c
    for (x, y) in sub:
        filled[(sx + x, sy + y)] = SUB_RAMP[min(2, y * 3 // sub_h)]
    # A gold star at each end of the subtitle.
    for cx in (sx - 8, sx + sw + 7):
        cy = sy + sub_h // 2
        for (dx, dy) in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0)):
            filled[(cx + dx, cy + dy)] = GOLD_RAMP[1] if (dx, dy) != (0, 0) else GOLD_RAMP[0]

    near = lambda x, y, r: [(x + dx, y + dy) for dx in range(-r, r + 1) for dy in range(-r, r + 1)]
    # The drop shadow, then the outer ring, then the ink, then the letters.
    for (x, y) in filled:
        for (qx, qy) in near(x, y + 2, 1):
            if 0 <= qx < width and 0 <= qy < height:
                px[qx, qy] = (8, 4, 20, 255)
    for (x, y) in filled:
        for (qx, qy) in near(x, y, 2):
            if 0 <= qx < width and 0 <= qy < height and px[qx, qy][3] == 0:
                px[qx, qy] = LOGO_RING + (255,)
    for (x, y) in filled:
        for (qx, qy) in near(x, y, 1):
            if 0 <= qx < width and 0 <= qy < height:
                px[qx, qy] = LOGO_INK + (255,)
    for (x, y), c in filled.items():
        px[x, y] = c + (255,)
    return img


# --- out ------------------------------------------------------------------------

def main():
    paint = painting()
    os.makedirs(os.path.dirname(OUT_PAINTING), exist_ok=True)
    paint.resize((W * 4, H * 4), Image.NEAREST).save(OUT_PAINTING)
    mark = logo()
    mark.save(OUT_LOGO)

    # itch.io's cover: 630x500, the middle of the painting at x2, the logo at
    # x2 across the sky above the moon.
    cw, ch = 315, 250
    crop = paint.crop(((W - cw) // 2, 0, (W - cw) // 2 + cw, ch)).resize((cw * 2, ch * 2), Image.NEAREST)
    big = mark.resize((mark.width * 2, mark.height * 2), Image.NEAREST)
    crop.paste(big, ((crop.width - big.width) // 2, 10), big)
    os.makedirs(os.path.dirname(OUT_ITCH), exist_ok=True)
    crop.save(OUT_ITCH)

    # The page's banner, 960x240 -- itch's default page width -- a band of
    # sky across the moon's crown with the logo on it; and its background, the
    # painting dimmed so the page's column reads over it.
    band = paint.crop((0, 0, W, 120)).resize((W * 2, 240), Image.NEAREST)
    band.paste(big, ((band.width - big.width) // 2, 16), big)     # above the pommel, which is at 104
    band.save(OUT_BANNER)
    dim = Image.blend(paint, Image.new("RGB", paint.size, (8, 6, 22)), 0.55)
    dim.resize((W * 4, H * 4), Image.NEAREST).save(OUT_BACKGROUND)

    print("painting %dx%d -> %s" % (W * 4, H * 4, OUT_PAINTING))
    print("logo %dx%d -> %s" % (mark.width, mark.height, OUT_LOGO))
    print("itch cover %dx%d -> %s" % (crop.width, crop.height, OUT_ITCH))
    print("itch banner %dx%d -> %s" % (band.width, band.height, OUT_BANNER))
    print("itch background %dx%d -> %s" % (W * 4, H * 4, OUT_BACKGROUND))


if __name__ == "__main__":
    main()
