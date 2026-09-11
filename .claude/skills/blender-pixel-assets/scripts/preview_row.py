"""Lays images out in a row at a fixed zoom on a grass-coloured ground, each
spaced by its own width so nothing overlaps, with name and size underneath.

    python preview_row.py OUT.png ZOOM image [image ...]

Put a reference asset (e.g. assets/objects/chest.png) in the row to judge a
new prop against what it will stand beside.
"""
import sys
from PIL import Image, ImageDraw


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    out, zoom, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]

    imgs = [(f, Image.open(f).convert("RGBA")) for f in files]
    gap, label = 16, 22
    w = sum(i.width * zoom + gap for _, i in imgs) + gap
    h = max(i.height * zoom for _, i in imgs) + label + gap * 2

    canvas = Image.new("RGBA", (w, h), (58, 78, 52, 255))
    d = ImageDraw.Draw(canvas)
    x = gap
    for path, im in imgs:
        big = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
        y = gap + (h - label - gap * 2 - big.height)   # common baseline
        canvas.paste(big, (x, y), big)
        d.rectangle([x - 1, y - 1, x + big.width, y + big.height], outline=(255, 210, 90, 120))
        name = path.replace("\\", "/").rsplit("/", 1)[-1]
        d.text((x, h - label + 2), "%s %dx%d" % (name, im.width, im.height),
               fill=(240, 240, 230, 255))
        x += big.width + gap
    canvas.save(out)
    print(out)


if __name__ == "__main__":
    main()
