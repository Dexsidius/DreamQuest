"""The small life about the world: assets/effects/critters.png.

Animals nobody fights -- Ambience's critters -- a row each, facing right (the
game flips them), drawn as pixel art from a few shapes a frame and given a
one-pixel outline a shade of whatever it borders, the same way as the birds
(tools/make_birds.py). Original art; nothing traced.

    row  cell   frames
     0   16     rabbit    sit, nibble, alert, hop (stretched), hop (tucked)
     1   16     squirrel  sit up, nibble, run, run
     2   16     hen       stand, peck, walk, flap
     3   16     cat       asleep, sit, walk, walk, stretch
     4   16     frog      sit, croak, leap
     5   16     fish      leaping up, the top of the leap, diving
     6   16     bat       wings up, level, down (seen from above)
     7   16     rat       sit, run, run
     8   16     lizard    bask, push-up, run, run
     9   32     deer      graze, head up, alert, bound (stretched), bound (tucked)
    10   16x32  the figure in the Reverie: standing

The rows of 16 are 16 px tall and start at the top; the deer's row is 32 tall
at y 144, and the figure's 32 tall at y 176. Ambience reads this layout
(Ambience::CritterCell). The hen and the cat are drawn pale, to be tinted as
each one is drawn: a brown hen, a speckled one, a ginger cat, a black one.

    python tools/make_critters.py
"""
import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "effects", "critters.png")
SCALE = 4   # drawn at four times the size, then reduced


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def ell(self, x0, y0, x1, y1, c):
        self.d.ellipse([x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE], fill=c)

    def poly(self, pts, c):
        self.d.polygon([(x * SCALE, y * SCALE) for x, y in pts], fill=c)

    def line(self, pts, c, width=1.0):
        self.d.line([(x * SCALE, y * SCALE) for x, y in pts], fill=c, width=max(1, int(width * SCALE)))

    def dot(self, x, y, c):
        # One whole pixel, after the reduction.
        self.d.rectangle([int(x) * SCALE, int(y) * SCALE, int(x) * SCALE + SCALE - 1, int(y) * SCALE + SCALE - 1], fill=c)

    def finish(self, threshold=110):
        small = self.im.resize((self.w, self.h), Image.BOX)
        px = small.load()
        for y in range(self.h):
            for x in range(self.w):
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 255) if a > threshold else (0, 0, 0, 0)
        out = small.copy()
        po = out.load()
        for y in range(self.h):
            for x in range(self.w):
                if px[x, y][3]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.w and 0 <= ny < self.h and px[nx, ny][3]:
                        r, g, b, _ = px[nx, ny]
                        po[x, y] = (int(r * 0.35) + 10, int(g * 0.32) + 8, int(b * 0.35) + 14, 255)
                        break
        return out


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c[:3])


# --- the rabbit ---------------------------------------------------------------------------
RABBIT = dict(body=(150, 124, 98), belly=(200, 182, 156), dark=(104, 84, 66), tail=(238, 234, 228),
              ear=(206, 158, 146), eye=(22, 16, 14))


def rabbit(frame):
    c = Canvas(16, 16)
    k = RABBIT
    if frame in (0, 1, 2):
        up = frame == 2
        # Haunch, body, the white scut, a fore-paw.
        c.ell(2.8, 8.4 - (1 if up else 0), 9.0, 13.4, k["body"])
        c.ell(4.0, 6.6 - (1.5 if up else 0), 11.0, 13.0, k["body"])
        c.ell(5.0, 9.6, 10.6, 13.2, k["belly"])
        c.ell(2.0, 8.2, 4.4, 10.8, k["tail"])
        c.ell(9.6, 11.8, 11.6, 13.4, k["belly"])
        if frame == 1:
            # Nose down in the grass.
            c.ell(9.4, 8.0, 13.6, 12.0, k["body"])
            c.poly([(10.0, 8.6), (11.0, 8.4), (8.6, 4.6), (7.8, 5.2)], k["dark"])
            c.poly([(10.4, 8.8), (10.9, 8.6), (8.9, 5.4), (8.5, 5.7)], k["ear"])
            c.dot(12, 9, k["eye"])
        else:
            hy = 2.4 if up else 4.4
            c.ell(9.0, hy, 13.2, hy + 4.0, k["body"])
            c.ell(12.0, hy + 2.2, 13.8, hy + 3.8, k["belly"])
            # Ears: straight up when it has heard something, laid back otherwise.
            if up:
                c.poly([(9.8, hy + 0.8), (11.2, hy + 0.6), (11.0, 0.2), (10.0, 0.0)], k["dark"])
                c.poly([(10.2, hy + 0.6), (10.8, hy + 0.5), (10.6, 0.8), (10.3, 0.7)], k["ear"])
            else:
                c.poly([(9.8, hy + 1.0), (11.0, hy + 0.6), (9.2, 0.6), (8.2, 1.2)], k["dark"])
                c.poly([(10.1, hy + 0.9), (10.6, hy + 0.7), (9.1, 1.4), (8.7, 1.6)], k["ear"])
            c.dot(11, int(hy + 1.6), k["eye"])
    elif frame == 3:
        # Stretched out in the middle of a bound.
        c.ell(1.0, 9.0, 4.6, 11.0, k["dark"])           # the hind legs thrown back
        c.ell(2.2, 5.6, 12.4, 10.2, k["body"])
        c.ell(4.0, 8.0, 11.0, 10.4, k["belly"])
        c.ell(1.2, 5.4, 3.6, 7.8, k["tail"])
        c.ell(11.2, 9.2, 14.2, 11.0, k["dark"])          # the fore-legs reaching
        c.ell(10.6, 3.6, 14.6, 7.4, k["body"])
        c.poly([(11.0, 4.6), (12.0, 4.2), (8.4, 1.4), (7.6, 2.0)], k["dark"])
        c.dot(13, 5, k["eye"])
    else:
        # Tucked up at the top of it.
        c.ell(3.2, 4.6, 11.2, 10.8, k["body"])
        c.ell(5.0, 7.6, 10.4, 10.8, k["belly"])
        c.ell(2.4, 4.8, 4.8, 7.2, k["tail"])
        c.ell(5.0, 9.8, 9.6, 11.8, k["dark"])
        c.ell(9.0, 2.8, 13.0, 6.6, k["body"])
        c.poly([(9.6, 3.8), (10.6, 3.4), (7.0, 0.8), (6.2, 1.4)], k["dark"])
        c.dot(11, 4, k["eye"])
    return c.finish()


# --- the squirrel ------------------------------------------------------------------------------
SQUIRREL = dict(body=(176, 92, 46), belly=(234, 202, 156), tail=(196, 110, 56), tail_lit=(228, 152, 92),
                dark=(122, 62, 32), eye=(20, 14, 12), nut=(128, 90, 48))


def squirrel(frame):
    c = Canvas(16, 16)
    k = SQUIRREL
    if frame in (0, 1):
        # The tail up its back in a curl, the body sitting up, paws at the mouth.
        c.ell(1.0, 1.4, 7.4, 11.4, k["tail"])
        c.ell(2.2, 2.4, 6.0, 7.6, k["tail_lit"])
        c.ell(1.6, 4.6, 5.4, 9.8, k["tail"])
        c.ell(5.2, 5.4, 10.6, 13.4, k["body"])
        c.ell(7.4, 7.4, 10.6, 13.0, k["belly"])
        c.ell(4.6, 11.0, 8.4, 13.6, k["dark"])           # the haunch
        hy = 2.4 if frame == 0 else 3.6
        c.ell(7.6, hy, 12.0, hy + 4.0, k["body"])
        c.ell(10.4, hy + 2.0, 12.6, hy + 3.8, k["belly"])
        c.poly([(8.2, hy + 0.8), (9.2, hy + 0.4), (8.6, hy - 1.4)], k["dark"])
        c.dot(10, int(hy + 1.4), k["eye"])
        if frame == 1:
            c.ell(10.2, hy + 3.6, 12.2, hy + 5.6, k["nut"])
        c.ell(9.8, 8.4 if frame == 0 else 8.8, 11.6, 10.2 if frame == 0 else 10.6, k["dark"])
    else:
        a = frame == 2
        # Tail streaming out behind, body low and long.
        c.poly([(5.0, 8.0), (3.0, 4.0 if a else 2.6), (0.4, 3.0 if a else 1.4), (0.6, 6.2 if a else 4.8), (3.4, 9.4)],
               k["tail"])
        c.poly([(4.2, 7.6), (2.6, 4.6 if a else 3.2), (1.4, 4.4 if a else 2.8), (2.6, 7.6)], k["tail_lit"])
        c.ell(3.6, 6.6 if a else 6.0, 11.4, 11.0 if a else 10.6, k["body"])
        c.ell(5.4, 8.8, 10.6, 11.0, k["belly"])
        c.ell(9.8, 5.2 if a else 4.8, 13.8, 8.8 if a else 8.4, k["body"])
        c.poly([(10.4, 5.6), (11.4, 5.2), (10.6, 3.6)], k["dark"])
        c.dot(12, 6, k["eye"])
        if a:
            c.ell(1.8, 10.0, 4.6, 11.8, k["dark"])
            c.ell(10.6, 10.2, 13.4, 12.0, k["dark"])
        else:
            c.ell(5.2, 10.2, 9.6, 12.2, k["dark"])
    return c.finish()


# --- the hen (pale: tinted as drawn) ----------------------------------------------------------
HEN = dict(body=(244, 240, 232), wing=(214, 208, 196), tail=(200, 194, 182), comb=(214, 56, 46),
           beak=(236, 178, 64), legs=(226, 156, 64), eye=(20, 16, 14))


def hen(frame):
    c = Canvas(16, 16)
    k = HEN
    if frame != 3:
        legs = [(6.6, 7.4), (8.8, 9.6)] if frame != 2 else [(5.8, 6.6), (9.4, 10.2)]
        for x0, x1 in legs:
            c.poly([(x0, 11.4), (x1, 11.4), (x1, 14.2), (x0, 14.2)], k["legs"])
    else:
        c.poly([(5.4, 11.4), (6.2, 11.4), (5.2, 14.2), (4.4, 14.2)], k["legs"])
        c.poly([(9.0, 11.4), (9.8, 11.4), (11.0, 14.0), (10.2, 14.2)], k["legs"])
    c.poly([(1.6, 3.8), (4.8, 6.4), (4.4, 9.4), (2.0, 7.8)], k["tail"])
    c.ell(3.2, 5.8, 11.6, 12.2, k["body"])
    if frame == 3:
        # Running off, wings up.
        c.poly([(4.6, 7.4), (9.4, 7.4), (8.4, 2.4), (5.2, 3.2)], k["wing"])
    else:
        c.ell(4.8, 7.2, 10.2, 10.8, k["wing"])
    if frame == 1:
        c.ell(10.2, 8.6, 13.4, 11.8, k["body"])
        c.ell(10.8, 7.8, 12.4, 9.0, k["comb"])
        c.poly([(13.0, 10.2), (14.8, 11.2), (13.0, 11.6)], k["beak"])
        c.dot(12, 9, k["eye"])
    else:
        hy = 2.4 if frame != 3 else 3.0
        c.ell(9.4, hy, 12.8, hy + 3.6, k["body"])
        c.ell(9.8, hy - 1.0, 12.0, hy + 0.8, k["comb"])
        c.poly([(12.4, hy + 1.4), (14.4, hy + 2.0), (12.4, hy + 2.6)], k["beak"])
        c.ell(11.6, hy + 2.8, 12.6, hy + 4.2, k["comb"])
        c.dot(11, int(hy + 1.2), k["eye"])
    return c.finish()


# --- the cat (pale: tinted as drawn) -------------------------------------------------------------
CAT = dict(body=(208, 204, 198), stripe=(150, 146, 142), belly=(236, 232, 226), nose=(214, 150, 150),
           eye=(150, 206, 96), lid=(110, 106, 102), dark=(96, 92, 90))


def cat(frame):
    c = Canvas(16, 16)
    k = CAT
    if frame == 0:
        # Curled up asleep: a round loaf, the tail round the front of it, eyes shut.
        c.ell(2.6, 7.6, 12.6, 13.8, k["body"])
        for x in (5.0, 7.0, 9.0):
            c.poly([(x, 7.9), (x + 0.8, 7.9), (x + 0.4, 10.0)], k["stripe"])
        c.ell(8.6, 8.2, 13.0, 12.4, k["body"])
        c.poly([(9.2, 8.8), (10.2, 6.6), (10.8, 8.6)], k["body"])
        c.poly([(11.0, 8.6), (12.0, 6.8), (12.6, 8.8)], k["body"])
        c.line([(10.2, 10.4), (11.2, 10.6)], k["lid"], 0.6)
        c.line([(2.6, 12.6), (6.0, 13.6), (10.0, 13.4)], k["stripe"], 1.2)
    elif frame == 1:
        # Sitting up, the tail curled round its feet.
        c.line([(6.0, 13.2), (3.4, 13.0), (2.2, 11.4)], k["stripe"], 1.3)
        c.ell(4.8, 6.2, 10.2, 13.6, k["body"])
        c.ell(6.6, 8.0, 9.8, 13.4, k["belly"])
        c.ell(6.8, 2.4, 11.6, 6.8, k["body"])
        c.poly([(7.2, 3.4), (7.8, 0.8), (8.8, 2.8)], k["body"])
        c.poly([(9.8, 2.8), (10.8, 0.9), (11.4, 3.4)], k["body"])
        c.dot(9, 4, k["eye"])
        c.dot(11, 4, k["eye"])
        c.dot(10, 5, k["nose"])
        for y in (7.4, 9.4):
            c.poly([(4.8, y), (6.4, y), (6.0, y + 0.8), (4.9, y + 0.8)], k["stripe"])
    elif frame in (2, 3):
        a = frame == 2
        c.line([(3.8, 7.0), (2.2, 4.4), (2.4, 1.8)], k["stripe"], 1.2)
        for x0, x1 in ([(4.4, 5.4), (6.0, 7.0), (9.6, 10.6), (11.2, 12.2)] if a else
                       [(5.0, 6.0), (5.8, 6.8), (10.2, 11.2), (10.8, 11.8)]):
            c.poly([(x0, 9.6), (x1, 9.6), (x1, 13.6), (x0, 13.6)], k["dark"])
        c.ell(3.4, 5.8, 12.6, 10.6, k["body"])
        for x in (5.4, 7.4, 9.4):
            c.poly([(x, 6.0), (x + 0.9, 6.0), (x + 0.4, 8.2)], k["stripe"])
        c.ell(10.4, 3.2, 14.6, 7.6, k["body"])
        c.poly([(10.8, 4.2), (11.4, 1.8), (12.2, 3.6)], k["body"])
        c.poly([(12.6, 3.6), (13.6, 1.8), (14.0, 4.4)], k["body"])
        c.dot(13, 5, k["eye"])
    else:
        # The stretch: forepaws out and low, the hind end up.
        c.poly([(3.0, 4.4), (11.0, 8.4), (11.4, 11.0), (3.6, 8.8)], k["body"])
        c.line([(3.2, 5.2), (1.4, 2.2)], k["stripe"], 1.2)
        c.poly([(3.6, 7.4), (4.8, 7.4), (4.8, 13.6), (3.6, 13.6)], k["dark"])
        c.poly([(10.0, 10.6), (14.6, 12.6), (14.4, 13.6), (9.8, 12.2)], k["dark"])
        c.ell(10.0, 7.4, 14.0, 11.6, k["body"])
        c.poly([(10.6, 8.2), (11.2, 6.0), (12.0, 8.0)], k["body"])
        c.poly([(12.4, 8.0), (13.4, 6.2), (13.8, 8.6)], k["body"])
        c.line([(12.2, 9.4), (13.2, 9.6)], k["lid"], 0.6)
    return c.finish()


# --- the frog --------------------------------------------------------------------------------
FROG = dict(body=(96, 148, 66), belly=(184, 204, 116), spot=(58, 96, 42), eye=(226, 196, 70), pupil=(16, 14, 12),
            sac=(236, 226, 160))


def frog(frame):
    c = Canvas(16, 16)
    k = FROG
    if frame < 2:
        c.ell(2.6, 9.0, 8.6, 13.6, k["spot"])             # the folded hind leg
        c.ell(3.6, 7.0, 12.4, 12.8, k["body"])
        c.ell(5.0, 10.0, 11.6, 13.0, k["belly"])
        c.ell(8.8, 5.6, 13.6, 10.0, k["body"])
        if frame == 1:
            c.ell(10.6, 8.4, 14.6, 12.2, k["sac"])
        c.ell(9.8, 4.4, 12.4, 7.0, k["eye"])
        c.dot(11, 5, k["pupil"])
        c.ell(4.6, 7.6, 6.2, 9.0, k["spot"])
        c.ell(7.2, 7.4, 8.6, 8.6, k["spot"])
        c.ell(10.4, 11.4, 12.4, 13.6, k["body"])          # a fore-foot
    else:
        # Mid-leap: stretched out, legs trailing.
        c.poly([(1.0, 11.0), (4.6, 8.4), (5.6, 9.6), (2.2, 12.6)], k["spot"])
        c.poly([(0.6, 9.4), (4.0, 7.2), (4.6, 8.4), (1.0, 10.6)], k["spot"])
        c.ell(3.8, 4.2, 12.6, 9.6, k["body"])
        c.ell(5.4, 6.8, 11.6, 9.8, k["belly"])
        c.ell(10.2, 2.8, 14.6, 7.0, k["body"])
        c.ell(10.8, 2.0, 13.2, 4.4, k["eye"])
        c.dot(12, 3, k["pupil"])
        c.ell(11.8, 7.6, 14.2, 9.4, k["body"])
    return c.finish()


# --- the fish ---------------------------------------------------------------------------------
FISH = dict(back=(84, 106, 128), side=(186, 198, 206), belly=(232, 238, 238), fin=(120, 138, 154), eye=(18, 18, 22))


def fish(frame):
    c = Canvas(16, 16)
    k = FISH
    if frame == 1:
        # The top of the leap: arched, level.
        c.poly([(1.0, 6.0), (3.6, 7.6), (1.0, 9.6)], k["fin"])
        c.ell(2.8, 5.6, 13.6, 10.0, k["side"])
        c.ell(3.6, 5.4, 13.0, 7.6, k["back"])
        c.ell(4.6, 8.0, 12.6, 10.0, k["belly"])
        c.poly([(6.6, 5.8), (8.6, 3.8), (9.4, 5.8)], k["fin"])
        c.dot(11, 7, k["eye"])
    else:
        # Up out of the water, or nose-down back into it.
        up = frame == 0
        pts = [(4.0, 13.0), (11.6, 4.0)] if up else [(4.0, 4.0), (11.6, 13.0)]
        (x0, y0), (x1, y1) = pts
        dx, dy = x1 - x0, y1 - y0
        L = (dx * dx + dy * dy) ** 0.5
        nx, ny = -dy / L, dx / L
        w = 2.2
        body = [(x0 + nx * 0.6, y0 + ny * 0.6), (x0 + dx * 0.35 + nx * w, y0 + dy * 0.35 + ny * w),
                (x1 + nx * 0.4, y1 + ny * 0.4), (x1 - nx * 0.4, y1 - ny * 0.4),
                (x0 + dx * 0.35 - nx * w, y0 + dy * 0.35 - ny * w), (x0 - nx * 0.6, y0 - ny * 0.6)]
        c.poly(body, k["side"])
        c.poly([(x0, y0), (x0 - dx / L * 2.4 + nx * 2.2, y0 - dy / L * 2.4 + ny * 2.2),
                (x0 - dx / L * 2.4 - nx * 2.2, y0 - dy / L * 2.4 - ny * 2.2)], k["fin"])
        c.poly([(x0 + dx * 0.3 + nx * 1.2, y0 + dy * 0.3 + ny * 1.2), (x1 + nx * 0.2, y1 + ny * 0.2),
                (x0 + dx * 0.6 + nx * 1.8, y0 + dy * 0.6 + ny * 1.8)], k["back"])
        c.dot(x1 - dx / L * 1.6, y1 - dy / L * 1.6, k["eye"])
    return c.finish()


# --- the bat -----------------------------------------------------------------------------------
BAT = dict(body=(86, 70, 84), wing=(122, 100, 120), far=(102, 84, 100), eye=(234, 140, 100))


def bat(frame):
    c = Canvas(16, 16)
    k = BAT
    y = 8.0
    tip = {0: (2.4, 2.6), 1: (0.6, 6.4), 2: (2.2, 11.2)}[frame]
    # Each wing a membrane with its trailing edge in scallops between the fingers.
    for side in (-1, 1):
        tx = 8.0 + side * (8.0 - tip[0])
        col = k["wing"] if side < 0 else k["far"]
        c.poly([(8.0, y - 0.8), (tx, tip[1]), (8.0 + side * 4.6, y + 1.2), (8.0 + side * 2.6, y + 2.4)
                if frame != 2 else (8.0 + side * 2.6, y + 1.6), (8.0, y + 1.2)], col)
        c.ell(tx - 1.0 if side < 0 else tx, tip[1] - 0.6, tx + 1.0 if side < 0 else tx + 1.2, tip[1] + 0.8, col)
    c.ell(6.6, 6.0, 9.4, 10.6, k["body"])
    c.ell(6.8, 4.2, 9.2, 6.8, k["body"])
    c.poly([(6.8, 4.8), (7.0, 3.0), (7.8, 4.4)], k["body"])
    c.poly([(8.2, 4.4), (9.0, 3.0), (9.2, 4.8)], k["body"])
    c.dot(7, 5, k["eye"])
    c.dot(8, 5, k["eye"])
    return c.finish()


# --- the rat ------------------------------------------------------------------------------------
RAT = dict(body=(98, 88, 82), belly=(152, 142, 132), tail=(198, 150, 146), ear=(196, 146, 140), eye=(16, 12, 12))


def rat(frame):
    c = Canvas(16, 16)
    k = RAT
    a = frame == 1
    c.line([(3.4, 11.8), (1.6, 12.6), (0.2, 11.6) if frame != 2 else (0.2, 13.4)], k["tail"], 0.9)
    if frame == 0:
        c.ell(2.8, 8.2, 10.6, 13.2, k["body"])
        c.ell(4.4, 10.6, 10.0, 13.2, k["belly"])
        c.ell(8.4, 7.2, 12.6, 11.0, k["body"])
    else:
        c.ell(2.4, 8.8 if a else 8.2, 11.6, 12.6 if a else 12.2, k["body"])
        c.ell(4.0, 10.6, 10.8, 12.6, k["belly"])
        c.ell(9.4, 8.4 if a else 8.0, 13.2, 11.6 if a else 11.2, k["body"])
        legs = [(3.4, 4.6), (9.0, 10.2)] if a else [(5.4, 6.6), (7.2, 8.4)]
        for x0, x1 in legs:
            c.poly([(x0, 12.0), (x1, 12.0), (x1, 13.6), (x0, 13.6)], k["body"])
    hx = 12.6 if frame == 0 else 13.2
    c.poly([(hx - 1.6, 8.6 if frame == 0 else 9.0), (hx + 1.8, 10.0), (hx - 1.6, 11.0)], k["body"])
    c.dot(hx + 1.2, 9 if frame == 0 else 10, k["ear"])
    c.ell(9.0, 6.4 if frame == 0 else 7.2, 10.8, 8.4 if frame == 0 else 9.2, k["ear"])
    c.dot(int(hx - 1), 8 if frame == 0 else 9, k["eye"])
    return c.finish()


# --- the lizard (the burnt land's: soot-dark, with embers down its back) ------------------------
LIZARD = dict(body=(72, 64, 60), back=(224, 116, 44), belly=(168, 104, 66), eye=(250, 200, 80), dark=(44, 38, 36))


def lizard(frame):
    c = Canvas(16, 16)
    k = LIZARD
    lift = 1.6 if frame == 1 else 0.0
    c.poly([(4.6, 11.0), (0.2, 12.4 if frame != 3 else 10.8), (0.4, 12.9 if frame != 3 else 11.5), (4.6, 12.4)],
           k["body"])
    c.ell(3.6, 10.0 - lift * 0.3, 11.8, 12.8, k["body"])
    c.ell(10.4, 9.4 - lift, 14.6, 12.0 - lift * 0.6, k["body"])
    c.ell(5.0, 11.6, 11.0, 12.9, k["belly"])
    for x in (5.0, 7.0, 9.0):
        c.dot(x, 10 - (1 if frame == 1 and x > 8 else 0), k["back"])
    c.dot(13, int(10 - lift), k["eye"])
    if frame == 2:
        legs = [((4.6, 12.0), (3.0, 13.8)), ((10.0, 12.0), (11.8, 13.8))]
    elif frame == 3:
        legs = [((4.6, 12.0), (6.2, 13.8)), ((10.0, 12.0), (8.6, 13.8))]
    else:
        legs = [((4.6, 12.0), (3.6, 13.8)), ((10.0, 12.0 - lift), (11.0, 13.8))]
    for a, b in legs:
        c.line([a, b], k["dark"], 1.0)
    return c.finish()


# --- the deer (a roe: small, with the pale rump) ----------------------------------------------------
DEER = dict(body=(156, 108, 66), belly=(204, 168, 126), rump=(238, 232, 218), dark=(96, 66, 42), nose=(30, 24, 22),
            leg=(122, 84, 52), ear=(186, 140, 100))


def deer(frame):
    c = Canvas(32, 32)
    k = DEER
    if frame in (0, 1, 2):
        for x in (9.6, 12.0, 20.0, 22.4):
            c.poly([(x, 17.0), (x + 1.4, 17.0), (x + 1.2, 28.4), (x + 0.2, 28.4)], k["leg"])
            c.poly([(x - 0.1, 27.4), (x + 1.4, 27.4), (x + 1.4, 28.6), (x - 0.1, 28.6)], k["nose"])
        c.ell(7.2, 10.6, 24.6, 19.4, k["body"])
        c.ell(10.0, 15.4, 23.0, 19.4, k["belly"])
        flare = 1.0 if frame == 2 else 0.0
        c.ell(6.2 - flare, 10.8 - flare, 10.8, 16.6 + flare, k["rump"])
        if frame == 0:
            # Grazing: the neck down and the muzzle in the grass.
            c.poly([(21.0, 12.2), (24.4, 12.6), (28.2, 20.4), (25.2, 21.4)], k["body"])
            c.ell(24.6, 19.0, 29.8, 23.2, k["body"])
            c.ell(28.2, 21.0, 30.4, 23.4, k["nose"])
            c.poly([(25.2, 19.4), (26.2, 18.4), (24.0, 16.4), (23.4, 17.2)], k["ear"])
            c.dot(27, 20, k["nose"])
        else:
            c.poly([(20.6, 12.6), (24.4, 11.6), (26.2, 4.6), (23.0, 4.8)], k["body"])
            c.ell(22.6, 2.4, 28.8, 7.2, k["body"])
            c.ell(27.4, 4.6, 30.4, 7.0, k["dark"])
            c.dot(29, 5, k["nose"])
            c.poly([(23.4, 3.4), (24.6, 2.6), (22.6, 0.0), (21.8, 0.6)], k["ear"])
            c.poly([(24.6, 3.0), (25.6, 2.6), (25.2, 0.0), (24.4, 0.2)], k["ear"])
            c.dot(26, 4, k["nose"])
            if frame == 2:
                c.poly([(6.6, 11.6), (5.2, 8.2), (7.2, 8.6)], k["rump"])     # the tail up
    else:
        stretched = frame == 3
        lift = 2.0 if stretched else 4.0
        if stretched:
            legs = [((9.0, 15.0), (2.4, 21.6)), ((11.0, 15.0), (4.2, 22.4)),
                    ((21.4, 15.0), (28.6, 20.6)), ((23.0, 14.6), (30.0, 18.4))]
        else:
            legs = [((10.0, 15.0), (12.6, 21.4)), ((12.0, 15.0), (14.4, 21.0)),
                    ((19.6, 15.0), (17.4, 21.4)), ((21.6, 15.0), (19.6, 21.0))]
        for a, b in legs:
            c.line([(a[0], a[1] - lift + 2.0), (b[0], b[1] - lift + 2.0)], k["leg"], 1.3)
        c.ell(6.6, 7.6 - lift + 2.0, 25.4, 15.6 - lift + 2.0, k["body"])
        c.ell(9.4, 11.6 - lift + 2.0, 23.4, 15.6 - lift + 2.0, k["belly"])
        c.ell(5.6, 7.6 - lift + 2.0, 10.4, 13.0 - lift + 2.0, k["rump"])
        hy = 1.6 - lift + 2.0
        c.poly([(21.4, 9.6 - lift + 2.0), (25.0, 8.6 - lift + 2.0), (27.6, hy + 3.0), (24.4, hy + 3.6)], k["body"])
        c.ell(24.2, hy + 1.0, 30.6, hy + 5.4, k["body"])
        c.ell(28.8, hy + 3.0, 31.4, hy + 5.4, k["dark"])
        c.poly([(24.6, hy + 1.6), (25.6, hy + 1.0), (22.6, hy - 1.0), (22.2, hy - 0.2)], k["ear"])
        c.dot(27, int(hy + 2.6), k["nose"])
    return c.finish()


# --- the figure in the Reverie ---------------------------------------------------------------------
def watcher():
    c = Canvas(16, 32)
    dark, edge, eye = (20, 14, 30), (34, 26, 48), (232, 222, 255)
    c.poly([(5.2, 8.4), (10.8, 8.4), (13.4, 31.0), (2.6, 31.0)], dark)
    c.poly([(4.4, 9.0), (5.6, 8.6), (4.2, 22.0), (3.2, 21.6)], edge)
    c.poly([(10.4, 8.6), (11.6, 9.0), (12.8, 21.6), (11.8, 22.0)], edge)
    c.ell(5.2, 1.6, 10.8, 9.0, dark)
    c.dot(6, 5, eye)
    c.dot(9, 5, eye)
    return c.finish(threshold=100)


ROWS = [  # (y, cell w, cell h, frames)
    (0, 16, 16, [rabbit(f) for f in range(5)]),
    (16, 16, 16, [squirrel(f) for f in range(4)]),
    (32, 16, 16, [hen(f) for f in range(4)]),
    (48, 16, 16, [cat(f) for f in range(5)]),
    (64, 16, 16, [frog(f) for f in range(3)]),
    (80, 16, 16, [fish(f) for f in range(3)]),
    (96, 16, 16, [bat(f) for f in range(3)]),
    (112, 16, 16, [rat(f) for f in range(3)]),
    (128, 16, 16, [lizard(f) for f in range(4)]),
    (144, 32, 32, [deer(f) for f in range(5)]),
    (176, 16, 32, [watcher()]),
]


def main():
    sheet = Image.new("RGBA", (160, 208), (0, 0, 0, 0))
    for y, w, h, frames in ROWS:
        for i, im in enumerate(frames):
            sheet.alpha_composite(im, (i * w, y))
    sheet.save(OUT)
    print("wrote", os.path.normpath(OUT), sheet.size)


if __name__ == "__main__":
    main()
