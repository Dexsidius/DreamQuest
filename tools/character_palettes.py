"""
character_palettes.py - writes assets/characters/<look>/palette.json for the
four playable characters, so the game can recolour their hair, skin and
clothes (src/entity/looks.*).

    python tools/character_palettes.py
    blender --background --python tools/character_palettes.py   (as make_character.ps1 runs it)

The character art is cel-shaded: every material is rendered in exactly three
flat colours -- shade, mid and light -- worked out from its base colour by the
ramp in tools/blender_character.py, and each layer gets a one-pixel outline in
a darkened mix of what it borders. So a recolour needs no masks. A pixel in one
of hair's three colours is hair, and it becomes the same band of the new hair
colour; the outline beside it is moved by the same 0.42 of the change that the
outline was drawn with. This file hands the game what it needs to do that: the
base colour of every material the look is rendered with, the ramp, the outline,
and which materials each part of the look recolours.

The numbers are read out of blender_character.py itself (with ast, so Blender
is not needed), and make_character.ps1 runs this after every render. A palette
change there and a stale file here would have the game recolour nothing, and
the self-test (TestLooks) counts how much of each sheet it recognises.
"""
import ast
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "tools", "blender_character.py")
PLAYERS = ["player_hero", "player_warden", "player_wayfarer", "player_lantern"]
# What the body and head layers are made of. Only these are recognised: the
# palette's other materials -- a dwarf's beard, a sweeper's apron, a carter's
# cart -- are not on a player, and some of them are the very colour of a
# player's hair (the apron is the Dreamweaver's to the digit).
MATERIALS = ["skin", "hair", "tunic", "trim", "belt", "gold", "trouser", "boot", "scarf", "eye"]

# Which materials each part of the look recolours. The first is the one the
# colour chosen is; the rest follow it channel by channel, as far from it as
# they were drawn, so a sleeve's trim stays a trim on a red tunic.
DYE = {
    "hair": ["hair"],
    "skin": ["skin"],
    "clothes": ["tunic", "trim"],
}
# Where a look's clothes are more or less than that. The Dreamweaver's scarf is
# the tunic's blue a shade down, part of one robe; the Lantern Warden's trim is
# brass and the mantle the colour of the light carried, which stay what they
# are whatever the cloth under them is.
DYE_BY_LOOK = {
    "player_wayfarer": {"clothes": ["tunic", "trim", "scarf"]},
    "player_lantern": {"clothes": ["tunic"]},
}


def constants():
    tree = ast.parse(open(SCRIPT, encoding="utf-8").read())
    found = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id in ("PALETTE", "LOOKS", "SHADE_TINT"):
            found[target.id] = ast.literal_eval(node.value)
        elif isinstance(target, ast.Tuple):
            names = [e.id for e in target.elts if isinstance(e, ast.Name)]
            values = ast.literal_eval(node.value)
            for n, v in zip(names, values):
                found[n] = v
    # The outline's strength and the colour it is pushed toward, from the
    # defaults of outline() and the constant in its body.
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == "outline":
            found["OUTLINE"] = ast.literal_eval(node.args.defaults[0])
            for sub in ast.walk(node):
                if (isinstance(sub, ast.Call) and getattr(sub.func, "attr", "") == "array"
                        and sub.args and isinstance(sub.args[0], ast.List)):
                    found["OUTLINE_TOWARD"] = ast.literal_eval(sub.args[0])
        # The materials drawn flat rather than through the ramp, and how much
        # of the cool tint the shade and the mid band are mixed with: the
        # third argument of each band(k, tint, amount) call, in the order made.
        if isinstance(node, ast.FunctionDef) and node.name == "material":
            amounts = []
            for sub in ast.walk(node):
                if isinstance(sub, ast.Compare) and isinstance(sub.comparators[0], ast.Tuple):
                    found["FLAT"] = list(ast.literal_eval(sub.comparators[0]))
                if (isinstance(sub, ast.Call) and getattr(sub.func, "id", "") == "band"
                        and len(sub.args) == 3):
                    amounts.append((sub.lineno, ast.literal_eval(sub.args[2])))
            if len(amounts) == 2:
                found["TINT_AMOUNT"] = [a for _, a in sorted(amounts)] + [0.0]
    need = ("PALETTE", "LOOKS", "SHADE_TINT", "BAND_SHADE", "BAND_MID", "BAND_LIGHT",
            "OUTLINE", "OUTLINE_TOWARD", "FLAT", "TINT_AMOUNT")
    missing = [n for n in need if n not in found]
    if missing:
        raise SystemExit("blender_character.py no longer has %s where this expects it" % ", ".join(missing))
    return found


def main():
    c = constants()
    for look in PLAYERS:
        palette = dict(c["PALETTE"])
        palette.update(c["LOOKS"][look].get("palette", {}))
        dye = dict(DYE)
        dye.update(DYE_BY_LOOK.get(look, {}))
        out = {
            "_comment": "Written by tools/character_palettes.py from tools/blender_character.py. Do not edit.",
            "ramp": {
                "bands": [c["BAND_SHADE"], c["BAND_MID"], c["BAND_LIGHT"]],
                # How much of the cool tint each band is mixed with (see band()
                # in material()): the shade most, the light not at all.
                "tint": list(c["SHADE_TINT"]),
                "tint_amount": c["TINT_AMOUNT"],
            },
            "outline": {"strength": c["OUTLINE"], "toward": c["OUTLINE_TOWARD"]},
            "flat": [m for m in c["FLAT"] if m in MATERIALS],
            "materials": {name: [round(v, 4) for v in palette[name]] for name in MATERIALS},
            "dye": dye,
        }
        path = os.path.join(ROOT, "assets", "characters", look, "palette.json")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(out, f, indent=1)
            f.write("\n")
        print("wrote", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
