"""
make_quest_icons.py - the questlines' item icons, made from the game's own.

    python tools/make_quest_icons.py

Each is an icon already in assets/icons recoloured: the Guild's ten seals are
the Guild Amulet in each tier's metal (the colour tiers.json gives the tier),
and the questlines' finds are a horn, a core, a hide, a sword and a dagger
the game already draws, in their own colours. Recolouring keeps the pixel
art's shading: each pixel's lightness is laid over the new colour, so the
highlights and the outline stay where they were drawn.
"""
import json
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "assets", "icons")


def lightness(r, g, b):
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0


def recolour(src, colour, lift=0.0, keep_dark=0.16):
    """Every pixel the same colour at its own lightness: the darkest (the
    outline) stays dark, the lightest goes toward white."""
    im = Image.open(os.path.join(ROOT, src)).convert("RGBA")
    px = im.load()
    # Spread the icon's own lightness over the whole range, so a dark icon
    # recoloured light is not muddy.
    values = [lightness(*px[x, y][:3]) for y in range(im.height) for x in range(im.width) if px[x, y][3] > 0]
    lo, hi = (min(values), max(values)) if values else (0.0, 1.0)
    span = max(0.05, hi - lo)
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            v = (lightness(r, g, b) - lo) / span
            if v < keep_dark:
                k = 0.25 + v
                px[x, y] = (int(colour[0] * k * 0.6), int(colour[1] * k * 0.6), int(colour[2] * k * 0.6), a)
                continue
            v = min(1.0, v + lift)
            # Below the middle, the colour darkened; above it, the colour
            # toward white.
            if v < 0.6:
                k = 0.35 + v * 1.08
                out = [c * k for c in colour]
            else:
                t = (v - 0.6) / 0.4 * 0.55
                out = [c + (255 - c) * t for c in colour]
            px[x, y] = tuple(max(0, min(255, int(round(c)))) for c in out) + (a,)
    return im


def marks(im, colour, points):
    """A few painted strokes laid on, pixel by pixel."""
    px = im.load()
    for x, y in points:
        if 0 <= x < im.width and 0 <= y < im.height and px[x, y][3] > 0:
            px[x, y] = tuple(colour) + (255,)
    return im


def save(im, name):
    path = os.path.join(ICONS, name + ".png")
    im.save(path)
    print("wrote", os.path.relpath(path, ROOT))


def main():
    tiers = {t["id"]: t for t in json.load(open(os.path.join(ROOT, "data", "tiers.json"), encoding="utf-8"))["tiers"]}
    for tier in ("iron", "steel", "azuryte", "damascus", "orichalcum", "diamond", "platinum", "demonite",
                 "dracon", "enchanted"):
        save(recolour("assets/icons/guild_amulet.png", tiers[tier]["colour"]), "guild_seal_" + tier)

    # The lizardfolk's painted hide: a hide with their marks daubed on it.
    hide = recolour("assets/icons/hide.png", (170, 132, 92))
    w, h = hide.size
    zig = [(int(w * 0.30) + i, int(h * 0.42) + (i % 4 if i % 8 < 4 else 4 - i % 4)) for i in range(int(w * 0.40))]
    dots = [(int(w * 0.38) + i * 3, int(h * 0.62)) for i in range(5)]
    marks(hide, (196, 56, 40), zig)
    marks(hide, (236, 196, 72), dots + [(x + 1, y) for x, y in dots])
    save(hide, "painted_hide")

    save(recolour("assets/icons/ember_core.png", (214, 66, 34), lift=0.05), "pyre_gland")
    save(recolour("assets/icons/tiers/demon_horn.png", (88, 52, 120)), "abyssal_horn")
    save(recolour("assets/icons/tiers/demon_horn.png", (206, 160, 72), lift=0.1), "herald_horn")
    save(recolour("assets/icons/tiers/sword_steel.png", (176, 210, 236), lift=0.05), "spirewatch_sword")
    save(recolour("assets/icons/tiers/dagger_iron.png", (150, 140, 128)), "harls_knife")
    save(recolour("assets/icons/tiers/mace_iron.png", (120, 112, 104)), "hulda_hammer")

    # And one prop: the College's old circle, lit gold for good once the
    # Magister has the five and the heart (What the World Was Made Of).
    # Only its cut lines, which glow a cold blue in the floor: they come up
    # gold, and everything else is left clear, so the stone under them shows.
    lit = Image.open(os.path.join(ROOT, "assets", "props", "spell_circle.png")).convert("RGBA")
    px = lit.load()
    for y in range(lit.height):
        for x in range(lit.width):
            r, g, b, a = px[x, y]
            glowing = a > 0 and b > 150 and g > 140 and r < 200 and (b - r) > 40
            if glowing:
                v = (g + b) / 510.0
                px[x, y] = (min(255, int(250 * (0.8 + 0.2 * v))), min(255, int(206 * (0.75 + 0.25 * v))),
                            int(96 + 60 * v), 255)
            else:
                px[x, y] = (0, 0, 0, 0)
    path = os.path.join(ROOT, "assets", "props", "spell_circle_lit.png")
    lit.save(path)
    print("wrote", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
