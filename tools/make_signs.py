"""Shop signs that hang out over the street and swing in the wind.

    assets/props/hanging_sign_<what>.png   a board on two short chains, with
                                           what the shop is painted on it
    assets/props/sign_bracket.png          the iron arm it hangs from

Each board is drawn hanging from the middle of its top edge, which is where
the game swings it from (World::Render, a "hanging_sign" object: see
docs/MAP_FORMAT.md); the arm is a separate thing that does not move. Pixel
art from a few shapes each, original, outlined a shade of what it borders
like the birds and the critters.

    python tools/make_signs.py
"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "props")
W, H = 20, 18
WOOD, WOOD_DK, WOOD_LT, IRON = (150, 104, 62), (98, 64, 38), (186, 138, 88), (58, 56, 62)


def outline(im):
    px = im.load()
    out = im.copy()
    po = out.load()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < im.width and 0 <= ny < im.height and px[nx, ny][3]:
                    r, g, b, _ = px[nx, ny]
                    po[x, y] = (int(r * 0.35) + 10, int(g * 0.32) + 8, int(b * 0.35) + 14, 255)
                    break
    return out


def board(icon):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # Two chains from the top edge (the pivot is the middle of it).
    for x in (5, 14):
        for y in range(0, 5):
            d.point((x, y), fill=IRON if y % 2 == 0 else (90, 88, 96, 255))
    # The board: a frame, planks, a lighter top edge.
    d.rectangle([2, 5, 17, 16], fill=WOOD_DK)
    d.rectangle([3, 6, 16, 15], fill=WOOD)
    d.line([(3, 6), (16, 6)], fill=WOOD_LT)
    d.line([(3, 11), (16, 11)], fill=(138, 94, 56))
    icon(d)
    return outline(im)


def mug(d):
    # A foaming tankard: the inn.
    d.rectangle([7, 9, 11, 14], fill=(206, 170, 96))
    d.rectangle([7, 8, 11, 9], fill=(244, 240, 228))
    d.line([(12, 10), (13, 10), (13, 12), (12, 12)], fill=(206, 170, 96))
    d.line([(7, 14), (11, 14)], fill=(150, 116, 60))


def anvil(d):
    # An anvil and a hammer over it: the forge.
    d.rectangle([6, 11, 13, 12], fill=(70, 70, 78))
    d.polygon([(5, 11), (7, 10), (13, 10), (13, 11)], fill=(96, 96, 106))
    d.rectangle([8, 13, 11, 14], fill=(70, 70, 78))
    d.line([(9, 7), (12, 9)], fill=(120, 84, 48))
    d.rectangle([12, 7, 13, 9], fill=(150, 150, 160))


def shield(d):
    # A shield with a sword behind it: the guild.
    d.line([(6, 7), (13, 14)], fill=(200, 204, 214))
    d.polygon([(8, 8), (12, 8), (12, 12), (10, 14), (8, 12)], fill=(150, 40, 40))
    d.line([(10, 8), (10, 13)], fill=(232, 196, 80))


def leaf(d):
    # A leaf over a mortar: the herbalist.
    d.polygon([(8, 11), (10, 7), (13, 7), (12, 10)], fill=(96, 160, 70))
    d.line([(8, 11), (12, 8)], fill=(60, 110, 46))
    d.rectangle([6, 12, 10, 14], fill=(178, 172, 164))
    d.line([(6, 12), (10, 12)], fill=(214, 208, 200))


def spool(d):
    # A spool of thread and a needle: the clothier.
    d.rectangle([7, 8, 11, 8], fill=(214, 180, 120))
    d.rectangle([7, 14, 11, 14], fill=(214, 180, 120))
    d.rectangle([8, 9, 10, 13], fill=(170, 60, 120))
    d.line([(12, 8), (14, 14)], fill=(214, 216, 224))


def fish(d):
    # A fish: the ferry house, and what comes off its boats.
    d.ellipse([6, 9, 12, 12], fill=(170, 186, 196))
    d.polygon([(12, 10), (14, 8), (14, 13)], fill=(130, 150, 166))
    d.point((7, 10), fill=(20, 20, 24))


def bracket():
    im = Image.new("RGBA", (26, 7), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # A plate on the wall at the left, the arm out to the right, and a scroll
    # of iron under it for strength.
    d.rectangle([0, 0, 2, 6], fill=(46, 44, 50))
    d.line([(2, 1), (25, 1)], fill=IRON, width=2)
    d.arc([3, 0, 11, 8], 180, 360, fill=IRON)
    d.arc([3, 1, 11, 9], 90, 180, fill=IRON)
    return outline(im)


def main():
    for name, icon in (("mug", mug), ("anvil", anvil), ("shield", shield), ("leaf", leaf), ("spool", spool),
                       ("fish", fish)):
        board(icon).save(os.path.join(OUT, "hanging_sign_%s.png" % name))
    bracket().save(os.path.join(OUT, "sign_bracket.png"))
    print("wrote 6 signs and the bracket into", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
