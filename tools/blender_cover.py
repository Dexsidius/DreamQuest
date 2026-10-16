"""
Solace/Reverie -- the cover painting's foreground, modelled.

The title painting is a sword driven into a mossy cairn under a crescent moon,
between two ruined pillars, with the dream country behind. The parts that need
light on them to read -- the sword, the stones, the pillars, the moss -- are
modelled here and rendered with a transparent sky; tools/make_cover.py paints
everything behind them (sky, moon, clouds, the far spires and floating isles)
in pixel art, brings this render down to the painting's own pixels, and adds
the glow and the sparkles.

The camera is orthographic and nearly level, and the frame is laid out in
painting pixels: one world unit is PX_PER_UNIT pixels of the 480x270 painting,
the ground line (z = 0) sits GROUND_Y pixels down, and x = 0 is the middle. So
make_cover.py knows where the blade is without looking for it.

Usage (Blender 5.2, from the repository root):
    blender --background --python tools/blender_cover.py
writes assets/_render/cover_foreground.png at SUPERSAMPLE times the painting.
"""

import math
import os
import random
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blender_props import (bevel, blk, clear_scene, cyl, material,  # noqa: E402
                           RENDER_DIR, sphere)

# --- the frame ----------------------------------------------------------------
PAINT_W, PAINT_H = 480, 270      # the painting, in its own pixels
SUPERSAMPLE = 4                  # rendered pixels per painting pixel
PX_PER_UNIT = 50.0               # painting pixels per world unit
GROUND_Y = 258.0                 # where z = 0 lands, in painting pixels from the top
ELEVATION = 7.0                  # degrees the camera looks down
BLADE_FOOT, BLADE_TOP = 1.46, 3.02   # where the blade leaves the stone, and meets the guard

# --- colours (sRGB, as a colour picker gives them) ----------------------------
STONE = (0.30, 0.37, 0.47)
STONE_DARK = (0.22, 0.27, 0.37)
STONE_FAR = (0.20, 0.22, 0.36)
MOSS = (0.30, 0.62, 0.18)
MOSS_DEEP = (0.17, 0.42, 0.14)
GRASS = (0.23, 0.52, 0.16)
GRASS_DARK = (0.12, 0.30, 0.13)
GOLD = (0.96, 0.74, 0.26)
GOLD_DEEP = (0.70, 0.45, 0.14)
GRIP = (0.16, 0.20, 0.55)
BLADE = (0.42, 0.74, 1.00)
BLADE_CORE = (0.86, 0.96, 1.00)
GEM = (0.30, 0.66, 1.00)
FLOWER = (0.96, 0.96, 0.92)
FLOWER_EYE = (1.00, 0.84, 0.30)
CARVE = (0.18, 0.22, 0.31)


def stone(name, size, loc, rot_z=0.0, tilt=0.0, colour=STONE):
    """A dressed block gone soft at the edges -- a big bevel, so the cairn
    reads as old stone rather than as boxes."""
    ob = blk(name, size, loc, colour, rot=(math.radians(tilt), 0.0, math.radians(rot_z)),
             rough=0.9, bev=0.0)
    bevel(ob, width=min(size) * 0.18, segments=3)
    return ob


def moss_cap(name, size, loc, rng, drips=3):
    """A pad of moss on top of a stone, with a few runs of it down the front."""
    w, d, _ = size
    blk(name, (w * 1.04, d * 1.04, 0.07), (loc[0], loc[1], loc[2] + 0.02), MOSS, rough=0.95, bev=0.02)
    for i in range(drips):
        x = loc[0] + rng.uniform(-w * 0.4, w * 0.4)
        length = rng.uniform(0.08, 0.26)
        blk("%s_drip%d" % (name, i), (rng.uniform(0.06, 0.14), 0.05, length),
            (x, loc[1] - d * 0.52, loc[2] - length * 0.5), MOSS_DEEP, rough=0.95, bev=0.01)


def tuft(name, x, y, z, rng, height=0.32, blades=6, colour=GRASS, spread=40.0):
    """A clump of grass: thin blades leaning out from one root."""
    for i in range(blades):
        lean = math.radians(rng.uniform(-spread, spread))
        h = height * rng.uniform(0.6, 1.1)
        ob = blk("%s_%d" % (name, i), (0.045, 0.03, h),
                 (x + math.sin(lean) * h * 0.5, y, z + math.cos(lean) * h * 0.5),
                 colour if rng.random() > 0.3 else GRASS_DARK, rot=(0.0, lean, 0.0), rough=0.95, bev=0.0)
        ob.rotation_euler = (0.0, lean, rng.uniform(-0.4, 0.4))


def fern(name, x, y, z, rng, size=0.55, colour=MOSS):
    """Fronds fanned out flat and arching -- the bright green that crowns the
    cairn in the old painting."""
    for i in range(5):
        a = math.radians(-70 + i * 35 + rng.uniform(-8, 8))
        length = size * rng.uniform(0.8, 1.1)
        cx = x + math.sin(a) * length * 0.5
        cz = z + math.cos(a) * length * 0.42
        blk("%s_%d" % (name, i), (0.10, 0.04, length), (cx, y, cz), colour,
            rot=(0.0, a, 0.0), rough=0.95, bev=0.0, emit=0.35)


def flower(name, x, y, z):
    for i in range(5):
        a = i * math.tau / 5
        sphere("%s_p%d" % (name, i), 0.045, (x + math.cos(a) * 0.05, y, z + math.sin(a) * 0.05), FLOWER, rough=0.9)
    sphere(name + "_eye", 0.035, (x, y - 0.02, z), FLOWER_EYE, rough=0.9, emit=0.4)


def carved_squares(name, x, y, z, side, depth_y):
    """The square spiral cut into the pillars: three nested frames."""
    for i, s in enumerate((side, side * 0.66, side * 0.33)):
        t = 0.045
        for (dx, dz, w, h) in ((0, s / 2, s, t), (0, -s / 2, s, t), (-s / 2, 0, t, s), (s / 2, 0, t, s)):
            blk("%s_%d_%d_%d" % (name, i, int(dx * 100), int(dz * 100)), (w, 0.03, h),
                (x + dx, depth_y, z + dz), CARVE, rough=0.95, bev=0.0)


def pillar(name, x, y, width, blocks, rng, carve_on=(), lean=0.0, colour=STONE, moss=True):
    """A column of stacked blocks, each a little off true, the top one broken."""
    z = 0.0
    for i, h in enumerate(blocks):
        w = width * rng.uniform(0.94, 1.02)
        top = i == len(blocks) - 1
        ob = stone("%s_%d" % (name, i), (w, width * 0.9, h), (x + lean * z, y, z + h / 2),
                   rot_z=rng.uniform(-3, 3), colour=colour)
        if top:
            # Broken: a wedge knocked off one corner.
            ob.rotation_euler = (0.0, math.radians(rng.choice((-1, 1)) * 9), ob.rotation_euler[2])
        if i in carve_on:
            carved_squares("%s_carve%d" % (name, i), x + lean * z, y, z + h / 2, min(w, h) * 0.62,
                           y - width * 0.46)
        z += h
    if moss:
        moss_cap(name + "_moss", (width, width * 0.9, 0.1), (x + lean * z, y, z), rng, drips=4)
        # Ivy down one side.
        side = rng.choice((-1, 1))
        for i in range(5):
            length = rng.uniform(0.3, 0.9)
            blk("%s_ivy%d" % (name, i), (0.07, 0.06, length),
                (x + side * width * rng.uniform(0.3, 0.52), y - width * 0.47, z - length / 2 - rng.uniform(0, 0.6)),
                MOSS_DEEP if i % 2 else MOSS, rough=0.95, bev=0.0)
    return z


def sword():
    """Point down in the cairn's top stone. Blade glowing, gold guard with a
    gem at its heart, a wrapped grip and a gemmed pommel."""
    y = -0.12
    h = BLADE_TOP - BLADE_FOOT
    blk("blade", (0.34, 0.07, h), (0.0, y, BLADE_FOOT + h / 2), BLADE, rough=0.3, emit=0.75, bev=0.01)
    blk("blade_core", (0.095, 0.02, h - 0.12), (0.0, y - 0.045, BLADE_FOOT + h / 2 + 0.03), BLADE_CORE,
        rough=0.2, emit=1.6, bev=0.0)
    # The guard: a bar, its tips swept up, and a block with the gem.
    g = BLADE_TOP + 0.07
    blk("guard", (1.46, 0.14, 0.15), (0.0, y, g), GOLD, rough=0.35, metal=0.6, bev=0.03)
    for side in (-1, 1):
        blk("guard_tip%d" % side, (0.15, 0.15, 0.26), (side * 0.74, y, g + 0.07), GOLD, rough=0.35, metal=0.6,
            bev=0.04).rotation_euler = (0.0, math.radians(-side * 18), 0.0)
        sphere("guard_ball%d" % side, 0.09, (side * 0.79, y, g + 0.24), GOLD_DEEP, rough=0.35)
    blk("guard_heart", (0.42, 0.18, 0.42), (0.0, y, g), GOLD, rough=0.35, metal=0.6, bev=0.06).rotation_euler = (
        0.0, math.radians(45), 0.0)
    sphere("guard_gem", 0.13, (0.0, y - 0.12, g), GEM, rough=0.2, emit=2.0)
    # Grip, wrapped, with gold bands.
    cyl("grip", 0.08, 0.56, (0.0, y, g + 0.40), GRIP, rough=0.8)
    for i, dz in enumerate((0.20, 0.40, 0.60)):
        cyl("band%d" % i, 0.097, 0.035, (0.0, y, g + dz), GOLD, rough=0.35, metal=0.6)
    sphere("pommel", 0.16, (0.0, y, g + 0.80), GOLD, rough=0.35)
    sphere("pommel_gem", 0.08, (0.0, y - 0.14, g + 0.80), GEM, rough=0.2, emit=2.0)
    bpy.ops.mesh.primitive_cone_add(radius1=0.06, depth=0.16, location=(0.0, y, g + 0.99))
    finial = bpy.context.active_object
    finial.data.materials.append(material("finial", GOLD, 0.35, 0.6))


def cairn(rng):
    """The stones the sword stands in, four courses narrowing to the top one
    it is driven into: a mound, standing well clear of the grass."""
    courses = [
        (0.0, 0.42, [(-1.75, 0.85), (-0.88, 0.9), (0.0, 0.9), (0.9, 0.92), (1.76, 0.8)]),
        (0.42, 0.40, [(-1.3, 0.85), (-0.42, 0.92), (0.46, 0.9), (1.3, 0.8)]),
        (0.82, 0.36, [(-0.82, 0.85), (0.04, 0.9), (0.86, 0.8)]),
        (1.18, 0.30, [(-0.05, 1.1)]),
    ]
    for c, (z0, h, row) in enumerate(courses):
        for i, (x, w) in enumerate(row):
            hh = h * rng.uniform(0.92, 1.05)
            size = (w * rng.uniform(0.94, 1.0), 0.9 - c * 0.12, hh)
            stone("cairn_%d_%d" % (c, i), size, (x, 0.2 + c * 0.05, z0 + hh / 2), rot_z=rng.uniform(-5, 5),
                  colour=STONE if (c + i) % 3 else STONE_DARK)
            moss_cap("cairn_moss_%d_%d" % (c, i), size, (x, 0.2 + c * 0.05, z0 + hh), rng,
                     drips=rng.randint(1, 3))
    # The crown of ferns about the blade's foot.
    for i, (x, s) in enumerate(((-0.42, 0.5), (0.38, 0.55), (-0.15, 0.38), (0.18, 0.4))):
        fern("crown%d" % i, x, -0.08, BLADE_FOOT - 0.02, rng, size=s)
    # Tufts in the joints of the lower courses.
    for i in range(12):
        tuft("cairn_tuft%d" % i, rng.uniform(-2.0, 2.0), -0.3, rng.choice((0.0, 0.42, 0.82)), rng,
             height=rng.uniform(0.18, 0.32), blades=rng.randint(3, 6))


def ground(rng):
    blk("ground", (12.0, 4.0, 0.2), (0.0, 1.5, -0.1), GRASS_DARK, rough=1.0, bev=0.0)
    # Grass along the front edge, thicker toward the corners where the old
    # painting's bushes were, and kept low in front of the cairn.
    for i in range(70):
        x = rng.uniform(-5.0, 5.0)
        edge = abs(x) / 5.0
        tuft("tuft%d" % i, x, rng.uniform(-0.6, -0.2), 0.0, rng, height=0.18 + 0.5 * edge * rng.random(),
             blades=rng.randint(4, 8))
    # And along the back, so the ground ends in grass rather than an edge.
    for i in range(40):
        tuft("back_tuft%d" % i, rng.uniform(-5.0, 5.0), 3.3, 0.0, rng, height=rng.uniform(0.15, 0.35),
             blades=rng.randint(3, 6), colour=GRASS_DARK)
    for i in range(18):
        x = rng.choice((-1, 1)) * rng.uniform(1.8, 4.8)
        fern("ground_fern%d" % i, x, rng.uniform(-0.5, 0.3), 0.0, rng, size=rng.uniform(0.4, 0.7))
    for i, (x, z) in enumerate(((-2.1, 0.25), (1.8, 0.3), (-0.9, 0.12), (3.0, 0.2), (-3.4, 0.35))):
        flower("flower%d" % i, x, -0.62, z)


def ruins(rng):
    # The near pillars frame the picture; a lower one behind each.
    pillar("pillar_l", -4.05, 0.4, 1.3, [0.72, 0.68, 0.70, 0.62], rng, carve_on=(1, 2))
    pillar("pillar_l2", -2.75, 1.6, 0.85, [0.6, 0.55, 0.4], rng, carve_on=(), colour=STONE_DARK)
    pillar("pillar_r", 4.0, 0.4, 1.2, [0.7, 0.66, 0.62, 0.5], rng, carve_on=(1,))
    pillar("pillar_r2", 2.9, 1.8, 0.7, [0.5, 0.42], rng, colour=STONE_DARK, moss=False)
    # Fallen blocks in the grass.
    stone("fallen_l", (0.9, 0.7, 0.5), (-2.4, -0.1, 0.25), rot_z=14, colour=STONE_DARK)
    moss_cap("fallen_l_moss", (0.9, 0.7, 0.5), (-2.4, -0.1, 0.5), rng, drips=2)
    stone("fallen_r", (0.8, 0.6, 0.42), (2.55, -0.2, 0.21), rot_z=-10)
    moss_cap("fallen_r_moss", (0.8, 0.6, 0.42), (2.55, -0.2, 0.42), rng, drips=2)


def lights():
    world = bpy.data.worlds.new("cover") if not bpy.data.worlds else bpy.data.worlds[0]
    bpy.context.scene.world = world
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.05, 0.06, 0.16, 1.0)
    bg.inputs["Strength"].default_value = 1.6

    def sun(name, direction, energy, colour):
        data = bpy.data.lights.new(name, "SUN")
        data.energy = energy
        data.color = colour
        data.angle = math.radians(8)
        ob = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(ob)
        ob.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()

    # The moon, behind and above: warm rims on every top edge.
    sun("moon", (0.25, -0.55, -0.80), 2.6, (1.0, 0.90, 0.70))
    # A cool fill from in front and to the left, so faces are not black.
    sun("fill", (0.55, 0.65, -0.52), 1.4, (0.58, 0.68, 1.0))

    # The blade's own light on the stones round it.
    data = bpy.data.lights.new("blade_glow", "POINT")
    data.energy = 140.0
    data.color = (0.40, 0.70, 1.0)
    data.shadow_soft_size = 0.4
    ob = bpy.data.objects.new("blade_glow", data)
    bpy.context.collection.objects.link(ob)
    ob.location = (0.0, -0.9, 2.2)


def camera():
    data = bpy.data.cameras.new("cam")
    data.type = "ORTHO"
    data.ortho_scale = PAINT_W / PX_PER_UNIT
    cam = bpy.data.objects.new("cam", data)
    bpy.context.collection.objects.link(cam)
    bpy.context.scene.camera = cam
    # The middle of the frame is (PAINT_H / 2) pixels down; z = 0 is GROUND_Y.
    target_z = (GROUND_Y - PAINT_H / 2) / PX_PER_UNIT / math.cos(math.radians(ELEVATION))
    elev = math.radians(ELEVATION)
    dist = 30.0
    target = Vector((0.0, 0.0, target_z))
    cam.location = target + Vector((0.0, -math.cos(elev) * dist, math.sin(elev) * dist))
    cam.rotation_euler = (math.radians(90.0 - ELEVATION), 0.0, 0.0)


def render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    try:
        scene.cycles.device = "CPU"
        scene.cycles.use_auto_tile = True
        scene.cycles.tile_size = 256
    except Exception:
        pass
    scene.render.resolution_x = PAINT_W * SUPERSAMPLE
    scene.render.resolution_y = PAINT_H * SUPERSAMPLE
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filter_size = 0.6
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    path = os.path.join(RENDER_DIR, "cover_foreground.png")
    os.makedirs(RENDER_DIR, exist_ok=True)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("rendered cover foreground -> %s" % path)


def main():
    rng = random.Random(1015)
    clear_scene()
    ground(rng)
    ruins(rng)
    cairn(rng)
    sword()
    lights()
    camera()
    render()


if __name__ == "__main__":
    main()
