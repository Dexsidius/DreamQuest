"""The poppet quest's icons: Oona's poppet idol, and the three things a full one
pays out -- the Hexpin, Thornwife's Bow and the Poppet Staff.

Each is made from art the game already has, so it sits with the rest of the
bag: the idol from the reed doll it was (re-threaded in red, darker for being
fed), the bow and the staff from the damascus pieces they are the same shape as
(recoloured, with thorns and red thread on the bow and a little poppet of its
own on top of the staff). The Hexpin is drawn: nothing in the armoury is a pin.

    python tools/make_poppet_icons.py            # writes assets/icons/*.png
    python tools/make_poppet_icons.py preview.png  # and a picture of all four, large
"""
import sys
from PIL import Image

ICONS = "assets/icons/"
TIERS = "assets/icons/tiers/"

RED = (178, 34, 40, 255)
RED_LIT = (226, 64, 58, 255)
RED_DARK = (110, 20, 28, 255)


def lum(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000.0


def ramp(stops, t):
    """A colour `t` of the way along `stops`, 0 to 1."""
    t = max(0.0, min(1.0, t))
    seg = t * (len(stops) - 1)
    i = min(int(seg), len(stops) - 2)
    f = seg - i
    a, b = stops[i], stops[i + 1]
    return tuple(int(a[k] + (b[k] - a[k]) * f) for k in range(3)) + (255,)


def recolour(img, stops, keep=lambda c: False):
    """Every opaque pixel along a new ramp by how light it was, save what `keep` spares."""
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            c = px[x, y]
            if c[3] == 0 or keep(c):
                continue
            n = ramp(stops, lum(c) / 255.0)
            px[x, y] = n[:3] + (c[3],)
    return out


def poppet_idol():
    """The reed doll, fed: reeds gone dark as peat, thread fresh and red, and
    the five knots of it standing out down its middle."""
    doll = Image.open(ICONS + "reed_doll.png").convert("RGBA")
    reed = [(34, 22, 16), (86, 56, 34), (150, 104, 60), (200, 160, 104)]
    red = lambda c: c[0] > 120 and c[1] < 90 and c[2] < 90
    out = recolour(doll, reed, keep=red)
    px = out.load()
    # The thread: the old band made new, and a line of knots down the body.
    for y in range(out.height):
        for x in range(out.width):
            if red(px[x, y]):
                px[x, y] = RED
    xs = [x for x in range(out.width) if any(px[x, y][3] for y in range(out.height))]
    mid = (min(xs) + max(xs)) // 2
    for y in (7, 8, 9, 10, 11):
        if px[mid, y][3]:
            px[mid, y] = RED_LIT if y % 2 else RED
    return out


def hexpin():
    """A long black blade no wider than a pin, a red bead for a pommel and red
    thread up the grip, lying corner to corner like the swords do."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    px = img.load()
    steel = [(30, 26, 36), (70, 62, 80), (128, 120, 140), (214, 208, 224)]
    # The blade, from the guard up to the point: two pixels wide, lit along one edge.
    for i in range(20):
        x, y = 9 + i, 22 - i
        if 0 <= x < 32 and 0 <= y < 32:
            px[x, y] = ramp(steel, 0.35)
            if x + 1 < 32:
                px[x + 1, y] = ramp(steel, 0.78 if i < 17 else 1.0)
            if y + 1 < 32 and i < 18:
                px[x, y + 1] = ramp(steel, 0.12)
    px[29, 2] = ramp(steel, 1.0)
    # The guard: a short bar across.
    for k in range(-2, 3):
        gx, gy = 8 + k, 22 + k
        if 0 <= gx < 32 and 0 <= gy < 32:
            px[gx, gy] = ramp(steel, 0.55 if k else 0.8)
    # The grip, wound in red thread, and the bead at its end.
    for i in range(1, 6):
        x, y = 8 - i, 23 + i
        px[x, y] = RED if i % 2 else RED_DARK
        px[x + 1, y] = RED_DARK if i % 2 else RED
    for (x, y), c in {(1, 29): RED_DARK, (2, 29): RED, (1, 30): RED, (2, 30): RED_LIT,
                      (0, 30): RED_DARK, (1, 31): RED_DARK}.items():
        px[x, y] = c
    return img


def thornwife_bow():
    """The damascus bow in black thorn: dark wood, thorns along its back and
    the Mother's red thread for a string."""
    bow = Image.open(TIERS + "bow_damascus.png").convert("RGBA")
    thorn = [(18, 12, 14), (52, 30, 34), (98, 60, 62), (150, 104, 100)]
    out = recolour(bow, thorn)
    px = out.load()
    w, h = out.size
    # The string: whatever faint straight line the stave holds between its tips,
    # made red -- found as the pixels most to one side in each row.
    rows = {}
    for y in range(h):
        xs = [x for x in range(w) if px[x, y][3] > 0]
        if xs:
            rows[y] = (min(xs), max(xs))
    ys = sorted(rows)
    for y in ys[2:-2]:
        lo, hi = rows[y]
        if hi - lo >= 3:
            px[lo, y] = RED if y % 3 else RED_LIT
    # Thorns: a pixel standing off the back of the stave every few rows.
    for y in ys[3:-3:4]:
        lo, hi = rows[y]
        if hi + 1 < w:
            px[hi + 1, y] = (200, 176, 150, 255)
    return out


def poppet_staff():
    """The damascus staff, its shaft darkened, its head taken off and a little
    poppet of its own tied on in its place."""
    staff = Image.open(TIERS + "staff_damascus.png").convert("RGBA")
    shaft = [(22, 14, 12), (62, 38, 28), (112, 76, 52), (170, 132, 96)]
    out = recolour(staff, shaft)
    px = out.load()
    w, h = out.size
    # The head is the top quarter: cleared.
    top = min(y for y in range(h) if any(px[x, y][3] for x in range(w)))
    for y in range(top, top + 8):
        for x in range(w):
            px[x, y] = (0, 0, 0, 0)
    # Where the shaft now ends, the poppet sits.
    end_y = min(y for y in range(h) if any(px[x, y][3] for x in range(w)))
    end_x = sum(x for x in range(w) if px[x, end_y][3]) // max(1, sum(1 for x in range(w) if px[x, end_y][3]))
    doll = poppet_idol().resize((11, 11), Image.NEAREST)
    out.alpha_composite(doll, (max(0, end_x - 5), max(0, end_y - 10)))
    return out


def main():
    made = {
        "poppet_idol": poppet_idol(),
        "hexpin": hexpin(),
        "thornwife_bow": thornwife_bow(),
        "poppet_staff": poppet_staff(),
    }
    for name, img in made.items():
        img.save(ICONS + name + ".png")
        print("wrote", ICONS + name + ".png", img.size)
    if len(sys.argv) > 1:
        scale = 8
        imgs = list(made.values())
        W = sum(32 * scale + 24 for _ in imgs)
        out = Image.new("RGBA", (W, 32 * scale), (40, 34, 30, 255))
        x = 0
        for img in imgs:
            big = img.resize((img.width * scale * 32 // img.width, img.height * scale * 32 // img.width), Image.NEAREST)
            out.alpha_composite(big, (x, 0))
            x += 32 * scale + 24
        out.save(sys.argv[1])


if __name__ == "__main__":
    main()
