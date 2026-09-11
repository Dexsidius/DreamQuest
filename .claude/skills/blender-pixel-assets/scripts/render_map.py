"""Composites a DreamQuest .mx map roughly the way the game draws it -- floor,
then floor overlays, then standing props and objects sorted by base line -- and
marks collision (red), NPCs (blue) and portals (green). Lets a layout be
checked without launching the game.

    python render_map.py maps/house_inn.mx OUT.png [ZOOM]

Run from the repo root so asset paths resolve. Outer-wall and whole-cell
collision boxes are left unmarked to keep the picture readable.
"""
import json
import os
import sys
from PIL import Image, ImageDraw


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    mx, out = sys.argv[1], sys.argv[2]
    zoom = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    d = json.load(open(mx))
    dq = d["dreamquest"]
    W, H = dq["bounds"]
    canvas = Image.new("RGBA", (W, H), (20, 16, 12, 255))

    cache = {}

    def img(path):
        if path not in cache:
            cache[path] = Image.open(path).convert("RGBA") if os.path.exists(path) else None
        return cache[path]

    layers = dq.get("layers", {})
    floor, overlays, standing = [], [], []
    for name, t in d["tiles"].items():
        layer = layers.get(name, 0)
        for (cx, cy, w, h) in t["locations"]:
            entry = (cy + h / 2, t["filepath"], cx - w / 2, cy - h / 2, w, h)
            if layer != 0:
                standing.append(entry)
            elif name.startswith("~"):
                overlays.append(entry)
            else:
                floor.append(entry)

    for o in dq.get("objects", []):
        if o.get("sprite"):
            im = img(o["sprite"])
            if im:
                standing.append((o["y"], o["sprite"], o["x"] - im.width / 2,
                                 o["y"] - im.height, im.width, im.height))

    def draw(items):
        for _, path, x, y, w, h in items:
            im = img(path)
            if im:
                canvas.alpha_composite(im.resize((int(w), int(h)), Image.NEAREST),
                                       (int(x), int(y)))

    draw(floor)
    draw(overlays)
    draw(sorted(standing, key=lambda s: s[0]))

    dr = ImageDraw.Draw(canvas)
    for n in dq.get("npcs", []):
        dr.ellipse([n["x"] - 7, n["y"] - 20, n["x"] + 7, n["y"]], outline=(120, 220, 255, 255), width=2)
    for x, y, w, h in dq.get("collision", []):
        if x <= 0 or y <= 0 or x + w >= W or y + h >= H or (w == 32 and h == 32):
            continue
        dr.rectangle([x, y, x + w, y + h], outline=(255, 70, 70, 200))
    for p in dq.get("portals", []):
        x, y, w, h = p["rect"]
        dr.rectangle([x, y, x + w, y + h], outline=(90, 255, 120, 255), width=2)

    canvas.resize((W * zoom, H * zoom), Image.NEAREST).save(out)
    print(out)


if __name__ == "__main__":
    main()
