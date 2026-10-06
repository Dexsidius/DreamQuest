"""Act II's icons: Hoarfang's five weapons, the death talisman, the rubbing of
the rune in the Infernal Pit, and Hoarfang's head.

The weapons are the damascus pieces they are the shape of, recoloured to the
frost dragon's ice: the outline and the darkest shading kept, the metal along
a ramp from glacier blue to white, a hint of white along the edges the light
falls on, and wood and leather gone the dark blue of the dragon's horns on
its own sheet. The other three are drawn here, at four times their size in
flat colours, then cut to 32 pixels by the colour most of each block is and
outlined, so they sit with the rest of the bag.

    python tools/make_act2_icons.py              # writes assets/icons/*.png
    python tools/make_act2_icons.py preview.png  # and a picture of all eight, large
"""
import math
import random
import sys
from collections import Counter

from PIL import Image, ImageDraw

ICONS = "assets/icons/"
TIERS = "assets/icons/tiers/"
OUTLINE = (40, 35, 49, 255)          # the tier icons' own outline, #282331

# The frost dragon's colours, as its sheet has them (frost_dragon/idle.png).
ICE = [(52, 76, 116), (92, 125, 171), (148, 190, 226), (209, 228, 244), (244, 251, 255)]
FROST_WOOD = [(30, 36, 60), (51, 59, 86), (79, 105, 145), (124, 150, 190)]


def lum(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000.0


def ramp(stops, t):
    """A colour `t` of the way along `stops`, 0 to 1."""
    t = max(0.0, min(1.0, t))
    seg = t * (len(stops) - 1)
    i = min(int(seg), len(stops) - 2)
    f = seg - i
    a, b = stops[i], stops[i + 1]
    return tuple(int(round(a[k] + (b[k] - a[k]) * f)) for k in range(3)) + (255,)


def mix(a, b, f):
    return tuple(int(round(a[k] + (b[k] - a[k]) * f)) for k in range(3)) + (255,)


# --- the five weapons ---------------------------------------------------------------
def frozen(name):
    """A damascus piece in Hoarfang's ice. The outline (anything darker than
    the darkest metal) stays as it is; warm pixels -- wood, leather -- go
    to the dark blue of the dragon's horns; the metal goes along ICE by how
    light it was, lifted a little so the whole reads as pale ice and not
    blue steel; and the metal touching the outline above or to the left,
    where the light falls, is whitened a little -- the hint of white at the
    edge."""
    src = Image.open(TIERS + name + "_damascus.png").convert("RGBA")
    out = src.copy()
    s, px = src.load(), out.load()
    w, h = src.size
    metal = set()
    for y in range(h):
        for x in range(w):
            c = s[x, y]
            if c[3] == 0:
                continue
            L = lum(c)
            if L < 62:
                continue                                    # outline and deepest shade
            if c[0] > c[2] + 10 and c[0] >= c[1]:           # wood, leather
                px[x, y] = ramp(FROST_WOOD, (L - 40) / 90.0)
                continue
            t = (L - 62) / (205 - 62)
            px[x, y] = ramp(ICE, t ** 0.8)
            metal.add((x, y))

    def dark(x, y):
        return not (0 <= x < w and 0 <= y < h) or s[x, y][3] == 0 or lum(s[x, y]) < 62
    for (x, y) in metal:
        if dark(x - 1, y) or dark(x, y - 1):
            px[x, y] = mix(px[x, y], (255, 255, 255), 0.45)
    # Glints: the lightest metal pixels in the piece, white.
    if metal:
        top = sorted(metal, key=lambda p: -lum(s[p[0], p[1]]))[:max(2, len(metal) // 40)]
        for (x, y) in top:
            px[x, y] = (250, 253, 255, 255)
    return out


# --- drawing at four times size -----------------------------------------------------
S = 4


class Big:
    """A 32 pixel icon drawn at 4x in flat colours, in the icon's own
    coordinates (so 16.0 is the middle), then cut down by majority and
    outlined."""

    def __init__(self):
        self.img = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def poly(self, pts, c):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=c)

    def ellipse(self, cx, cy, rx, ry, c):
        self.d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S - 1, (cy + ry) * S - 1], fill=c)

    def line(self, pts, c, width):
        self.d.line([(x * S, y * S) for x, y in pts], fill=c, width=int(round(width * S)), joint="curve")

    def small(self, outline=OUTLINE):
        src = self.img.load()
        out = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        px = out.load()
        for y in range(32):
            for x in range(32):
                cnt = Counter(src[x * S + i, y * S + j] for i in range(S) for j in range(S))
                clear = sum(n for c, n in cnt.items() if c[3] == 0)
                if clear * 2 >= S * S:
                    continue
                px[x, y] = max(((c, n) for c, n in cnt.items() if c[3] > 0), key=lambda t: t[1])[0]
        if outline:
            ring = [(x, y) for y in range(32) for x in range(32) if px[x, y][3] == 0 and any(
                0 <= x + dx < 32 and 0 <= y + dy < 32 and px[x + dx, y + dy][3] > 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
            for (x, y) in ring:
                px[x, y] = outline
        return out


# --- the death talisman -------------------------------------------------------------
BONE = [(150, 138, 116), (190, 178, 152), (224, 214, 190), (246, 240, 224)]
VIOLET = (172, 118, 236, 255)
VIOLET_DK = (96, 58, 150, 255)
HALO = (70, 44, 108, 255)


def death_talisman():
    """A disc of bone on a black cord, a closed eye carved in it and the
    carving glowing a faint violet, with a ring of violet light round the
    disc."""
    b = Big()
    cord, cord_lt = (26, 22, 32, 255), (62, 54, 76, 255)
    # The cord: a loop up to the top of the picture from a bone bail.
    b.line([(16, 9.5), (10.5, 2.5), (16, 1.2), (21.5, 2.5), (16, 9.5)], cord, 1.6)
    b.line([(11.2, 2.6), (16, 1.6)], cord_lt, 0.7)
    b.ellipse(16, 10.0, 2.4, 2.0, ramp(BONE, 0.30))
    b.ellipse(16, 10.0, 1.0, 0.9, (0, 0, 0, 0))
    # The disc: a rim, then the face, lit from the upper left.
    cx, cy, r = 16, 19.5, 8.6
    b.ellipse(cx, cy, r, r, ramp(BONE, 0.33))
    b.ellipse(cx - 0.4, cy - 0.4, r - 1.2, r - 1.2, ramp(BONE, 0.62))
    b.ellipse(cx - 1.4, cy - 1.6, r - 3.2, r - 3.2, ramp(BONE, 0.80))
    b.ellipse(cx - 3.0, cy - 3.6, 1.8, 1.5, ramp(BONE, 1.0))
    # The closed eye: the lid's curve and four lashes, cut in and glowing.
    lid = [(cx - 5.2 + i * 0.65, cy - 0.6 + 2.6 * math.sin(math.pi * i / 16)) for i in range(17)]
    b.line(lid, VIOLET_DK, 1.9)
    b.line(lid[1:-1], VIOLET, 0.9)
    for k, (x0, x1) in enumerate(((-3.6, -4.8), (-1.2, -1.6), (1.2, 1.6), (3.6, 4.8))):
        y0 = cy - 0.6 + 2.6 * math.sin(math.pi * (x0 + 5.2) / 10.4) + 0.4
        b.line([(cx + x0, y0), (cx + x1, y0 + 2.4)], VIOLET, 0.9)
    img = b.small()
    # The glow: a ring of dim violet outside the disc's outline.
    px = img.load()
    for y in range(32):
        for x in range(32):
            if px[x, y][3]:
                continue
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if r + 0.6 <= d <= r + 1.6 and y > 11:
                px[x, y] = HALO
    for (x, y) in ((5, 12), (27, 15), (25, 29), (6, 27)):
        if not px[x, y][3]:
            px[x, y] = VIOLET
    return img


# --- the rubbing of the pit's rune --------------------------------------------------
# blender_act1_props.PIT_RUNE, the same strokes (x across, z up, -1 to 1).
PIT_RUNE = ((0.0, 0.05, 0.0, 1.0),
            (0.0, 0.40, -0.70, 0.98), (0.0, 0.40, 0.70, 0.98),
            (0.0, 0.05, 0.62, -0.45), (0.62, -0.45, 0.0, -0.95),
            (0.0, -0.95, -0.62, -0.45), (-0.62, -0.45, 0.0, 0.05))


def rune_rubbing():
    """A sheet of paper rubbed over with charcoal on the Pit's slab, the rune
    standing out pale where the grooves kept the charcoal off it, its edges
    a little torn and one corner gone."""
    b = Big()
    rng = random.Random(7)
    x0, y0, x1, y1 = 5.0, 3.0, 27.0, 29.5
    pts = []
    # Round the sheet clockwise, jagged where it was torn: a few notches on
    # each side and the top right corner torn off.
    for i in range(7):
        pts.append((x0 + (x1 - 4.5 - x0) * i / 6, y0 + rng.uniform(-0.4, 0.6)))
    pts += [(x1 - 3.0, y0 + 1.6), (x1 - 1.6, y0 + 2.2), (x1, y0 + 4.2)]
    for i in range(1, 8):
        pts.append((x1 + rng.uniform(-0.7, 0.3), y0 + 4.2 + (y1 - y0 - 4.2) * i / 7))
    for i in range(1, 8):
        pts.append((x1 - (x1 - x0) * i / 7, y1 + rng.uniform(-0.8, 0.2)))
    for i in range(1, 7):
        pts.append((x0 + rng.uniform(-0.3, 0.8), y1 - (y1 - y0) * i / 7))
    paper, paper_edge = (66, 64, 70, 255), (104, 100, 104, 255)
    b.poly(pts, paper_edge)
    inner = [((p[0] - 16) * 0.86 + 16, (p[1] - 16.2) * 0.88 + 16.2) for p in pts]
    b.poly(inner, paper)
    # The charcoal's strokes, slanting.
    for k in range(9):
        y = 6 + k * 2.6 + rng.uniform(-0.5, 0.5)
        b.line([(7.5, y + 2.0), (24.5, y - 2.0)], ((54, 52, 58, 255), (80, 78, 84, 255))[k % 2], 0.8)
    # The rune, pale.
    cx, cy, w, h = 16.0, 16.4, 13.0, 17.0
    for (xa, za, xb, zb) in PIT_RUNE:
        b.line([(cx + xa * w / 2, cy - za * h / 2), (cx + xb * w / 2, cy - zb * h / 2)], (208, 220, 232, 255), 2.0)
    return b.small((36, 34, 42, 255))


# --- Hoarfang's head ----------------------------------------------------------------
def hoarfang_head():
    """The frost dragon's head in profile, looking left: pale ice-blue scales
    shaded down to glacier blue, white horns swept back, the jaw a little
    open on icy teeth and a cold light in the eye."""
    b = Big()
    base, shade, deep, light = (161, 189, 219, 255), (106, 140, 186, 255), (70, 98, 146, 255), (209, 225, 241, 255)
    horn, horn_sh = (242, 247, 252, 255), (178, 196, 220, 255)
    mouth, tooth = (40, 48, 78, 255), (236, 248, 255, 255)
    # The far horn, behind everything.
    b.poly([(18.0, 9.5), (22.5, 5.0), (28.0, 2.0), (30.5, 1.0), (27.5, 4.5), (23.0, 9.5)], horn_sh)
    # The neck, down and back to the corner.
    b.poly([(14.5, 19.0), (21.0, 17.5), (29.5, 28.0), (27.5, 31.0), (15.5, 31.0)], shade)
    b.poly([(16.5, 21.0), (20.0, 20.0), (25.0, 27.0), (24.0, 31.0), (18.0, 31.0)], base)
    # The lower jaw, dropped a little.
    b.poly([(15.5, 19.5), (5.0, 20.5), (3.6, 21.8), (6.0, 23.0), (16.5, 22.5)], shade)
    # The mouth, and the upper jaw and snout over it.
    b.poly([(15.0, 18.0), (4.0, 17.5), (4.5, 20.6), (15.5, 20.0)], mouth)
    b.poly([(14.5, 10.0), (6.5, 11.8), (3.2, 13.6), (2.6, 16.4), (4.0, 18.0), (15.5, 18.5)], base)
    b.poly([(14.0, 10.6), (6.5, 12.2), (3.6, 13.9), (4.6, 14.4), (13.5, 13.0)], light)
    # The skull.
    b.ellipse(19.5, 14.5, 6.6, 6.0, base)
    b.ellipse(18.6, 12.8, 4.6, 3.6, light)
    b.poly([(14.0, 18.6), (25.0, 17.0), (25.5, 20.0), (16.0, 20.5)], shade)
    # Teeth along both jaws.
    for k in range(5):
        x = 5.4 + k * 2.0
        b.poly([(x, 17.6), (x + 1.2, 17.6), (x + 0.6, 19.4)], tooth)
        b.poly([(x + 0.8, 20.8), (x + 2.0, 20.8), (x + 1.4, 19.2)], tooth)
    # Brow, eye and nostril.
    b.line([(13.0, 11.6), (18.0, 10.6)], deep, 1.1)
    b.poly([(14.6, 12.6), (17.4, 12.2), (17.0, 13.8), (14.8, 13.8)], (140, 240, 255, 255))
    b.ellipse(4.6, 14.6, 0.7, 0.6, deep)
    # Frill spines down the back of the head and neck.
    for (sx, sy, tx, ty) in ((24.0, 15.0, 29.0, 13.5), (24.5, 18.5, 29.5, 18.0), (24.0, 22.0, 28.5, 23.0)):
        b.poly([(sx, sy - 1.0), (tx, ty), (sx, sy + 1.0)], deep)
    # The near horn, white, swept back and up from the top of the skull.
    b.poly([(16.5, 9.8), (19.5, 6.5), (24.5, 3.2), (29.5, 1.0), (26.5, 5.2), (22.5, 9.0), (20.0, 11.5)], horn)
    b.poly([(20.0, 11.5), (22.5, 9.0), (26.5, 5.2), (29.5, 1.0), (27.5, 5.8), (23.0, 10.8)], horn_sh)
    # A short horn under it, from the jaw's hinge.
    b.poly([(22.0, 15.5), (27.0, 13.0), (30.5, 12.2), (27.5, 14.8), (23.0, 17.0)], horn)
    return b.small()


def main():
    made = {
        "hoarfang_fangs": frozen("dagger"),
        "hoarfang_bow": frozen("bow"),
        "hoarfang_staff": frozen("staff"),
        "hoarfang_talon": frozen("greatsword"),
        "hoarfang_maul": frozen("mace"),
        "death_talisman": death_talisman(),
        "rune_rubbing": rune_rubbing(),
        "hoarfang_head": hoarfang_head(),
    }
    for name, img in made.items():
        img.save(ICONS + name + ".png")
        print("wrote", ICONS + name + ".png", img.size)
    if len(sys.argv) > 1:
        scale = 8
        imgs = list(made.values())
        out = Image.new("RGBA", (len(imgs) * (32 * scale + 24), 32 * scale), (40, 34, 30, 255))
        for i, img in enumerate(imgs):
            out.alpha_composite(img.resize((32 * scale, 32 * scale), Image.NEAREST), (i * (32 * scale + 24), 0))
        out.save(sys.argv[1])


if __name__ == "__main__":
    main()
