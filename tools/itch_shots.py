"""
Solace/Reverie -- the store page's screenshots, taken by the game itself.

Runs bin/DreamQuest.exe once per scene with the game's own launch options
(--scratch, --map, --hour, --screen, --shot ...) and writes the pictures to
docs/itch/screenshots/, numbered in the order the page shows them: the first
is the one itch puts at the top.

Every run is a scratch game in a stage folder of its own (dist/shots_stage:
junctions to data/, assets/, art/ and maps/, a settings.json fixed here, and a
portable.txt), so no run reads this machine's settings or touches anybody's
saves in the user folder.

Usage, from the repository root, after .\\build.ps1:
    python tools/itch_shots.py              every scene
    python tools/itch_shots.py town fight   just these
"""

import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "dist", "shots_stage")
BIN = os.path.join(STAGE, "bin")
OUT = os.path.join(ROOT, "docs", "itch", "screenshots")

# The settings every picture is taken with: the Cozy look, every effect on,
# the default zoom, a 1280x720 window, nothing on screen a player would not see.
SETTINGS = {"art_style": 1, "visual_effects": True, "zoom": 2.0, "ui_scale": 1.0, "fullscreen": False,
            "show_fps": False, "damage_numbers": True, "xp_drops": True, "quest_waypoints": True,
            "screen_shake": True, "flashes": True, "colour_fringing": True, "screen_distortion": True}

# name: (seconds before the shot, launch options). In page order, which tells
# the loop: a fight, the town it starts from, the dream it goes down into,
# playing it together, then the screens a character is built on. The spell's
# moment is timed: the Dreamweaver's fire lands on the highwayman at 4.1 s.
SCENES = [
    ("spell", 4.1, ["--scratch", "weaver", "--level", "40", "--map", "westwold", "--at", "2400", "1660",
                    "--hour", "11", "--hold", "J", "4.0", "4.15"]),
    ("town", 5, ["--scratch", "knight", "--level", "20", "--map", "town_havenbrook", "--hour", "16.5"]),
    ("dream_town", 5, ["--scratch", "lantern", "--level", "70", "--map", "dream_havenbrook", "--hour", "23"]),
    ("reverie", 5, ["--scratch", "ranger", "--level", "40", "--map", "dreamworld_2", "--hour", "23"]),
    ("fight", 6, ["--scratch", "knight", "--level", "30", "--map", "westwold", "--at", "2400", "1660",
                  "--hour", "11", "--hold", "J", "4.6", "6"]),
    ("coop", 5, ["--scratch", "knight", "--level", "20", "--map", "town_havenbrook", "--hour", "12", "--p2"]),
    ("character", 4, ["--scratch", "lantern", "--level", "45", "--screen", "sheet"]),
    ("tree", 4, ["--scratch", "weaver", "--level", "60", "--screen", "tree"]),
    ("looks", 4, ["--screen", "looks:player_lantern", "--looks", "e8cc78,-,c4302e"]),
    ("title", 4, []),
]


def stage():
    os.makedirs(BIN, exist_ok=True)
    for d in ("data", "assets", "art", "maps"):
        link = os.path.join(STAGE, d)
        if not os.path.exists(link):
            subprocess.run(["cmd", "/c", "mklink", "/J", link, os.path.join(ROOT, d)], check=True,
                           capture_output=True)
    src = os.path.join(ROOT, "bin")
    for f in os.listdir(src):
        if f == "DreamQuest.exe" or f.endswith(".dll"):
            shutil.copy2(os.path.join(src, f), BIN)
    open(os.path.join(STAGE, "portable.txt"), "w").close()
    json.dump(SETTINGS, open(os.path.join(STAGE, "settings.json"), "w", encoding="utf-8"))


def main():
    only = set(sys.argv[1:])
    stage()
    os.makedirs(OUT, exist_ok=True)
    if not only:
        # A whole tour replaces the last one, numbers and all.
        for f in os.listdir(OUT):
            if f.endswith(".png"):
                os.remove(os.path.join(OUT, f))
    n = 0
    for i, (name, wait, args) in enumerate(SCENES, 1):
        if only and name not in only:
            continue
        out = os.path.join(OUT, "%02d_%s.png" % (i, name))
        if os.path.exists(out):
            os.remove(out)
        # Settings first every time: a scene that opens Options must not leave
        # its changes to the next.
        json.dump(SETTINGS, open(os.path.join(STAGE, "settings.json"), "w", encoding="utf-8"))
        cmd = [os.path.join(BIN, "DreamQuest.exe")] + args + ["--shot", out, str(wait)]
        try:
            subprocess.run(cmd, cwd=BIN, timeout=120, capture_output=True)
        except subprocess.TimeoutExpired:
            print("%-12s timed out" % name)
            continue
        ok = os.path.exists(out)
        n += ok
        print("%-12s %s" % (name, out if ok else "MISSING"))
    print("%d screenshots in %s" % (n, OUT))


if __name__ == "__main__":
    main()
