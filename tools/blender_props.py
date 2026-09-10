# =============================================================================
#  blender_props.py - original furniture and interior props for the guild hall.
#
#  Run headless:
#      blender --background --python tools/blender_props.py
#
#  Everything here is built from primitives in code rather than modelled by
#  hand, so the set can be re-rendered at a different size or angle by changing
#  one number instead of reopening a .blend. Output lands in assets/_render/ as
#  oversized PNGs; tools/make_props.ps1 downsamples and flattens them into the
#  pixel art the game actually loads.
#
#  The view matches the rest of the game: orthographic, looking down the -Y
#  axis at 55 degrees above horizontal, which is the angle the CraftPix
#  buildings and props are drawn at. Z is up, Y runs into the screen, and every
#  prop is modelled standing on Z=0 so shadows land where the object meets the
#  floor.
# =============================================================================

import math
import os
import sys

import bpy
from mathutils import Vector

# --- palette -----------------------------------------------------------------
# One shared palette so a bookshelf and a table read as furniture from the same
# room. Written the way a colour picker shows them -- sRGB, 0..1 -- and
# converted below, because Blender takes base colours in linear light. Feeding
# sRGB values straight in renders everything about twice as pale as intended,
# which is exactly what a barrel that should be oak looked like.
PALETTE = {
    "oak":        (0.478, 0.294, 0.157),
    "oak_light":  (0.612, 0.404, 0.220),
    "oak_pale":   (0.741, 0.553, 0.353),
    "iron":       (0.286, 0.302, 0.345),
    "iron_light": (0.502, 0.529, 0.580),
    "brass":      (0.796, 0.596, 0.208),
    "cloth_red":  (0.639, 0.180, 0.176),
    "cloth_blue": (0.239, 0.353, 0.596),
    "cloth_cream":(0.847, 0.788, 0.643),
    "stone":      (0.478, 0.475, 0.494),
    "stone_pale": (0.639, 0.624, 0.588),
    "paper":      (0.886, 0.839, 0.706),
    "ember":      (1.000, 0.561, 0.176),
    "leaf":       (0.298, 0.494, 0.267),
}

def to_linear(rgb):
    """sRGB to linear, the standard piecewise transfer curve."""
    def one(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return tuple(one(c) for c in rgb)


RENDER_DIR = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "assets", "_render")

# Measured off the art this furniture stands next to rather than picked: the
# CraftPix interior tables and benches show their front edge, their legs, and
# only a sliver of the top, which is a viewpoint a little over thirty degrees
# above the floor. Rendering at fifty-five hid every table leg behind its own
# top and the props read as floating slabs.
CAMERA_ELEVATION = 34.0     # degrees above the horizon
SUPERSAMPLE = 8             # rendered pixels per final pixel


# --- scene plumbing ----------------------------------------------------------

def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.materials,
                  bpy.data.cameras, bpy.data.lights):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def material(name, colour, rough=0.75, metal=0.0, emit=0.0):
    """A flat-ish material. Pixel art wants readable blocks of colour, so
    roughness is high and speculars are kept down; the shape has to read at
    thirty pixels across, and a glossy highlight there is just noise."""
    key = "%s_%.2f_%.2f_%.2f" % (name, rough, metal, emit)
    if key in bpy.data.materials:
        return bpy.data.materials[key]

    mat = bpy.data.materials.new(key)
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    rgb = to_linear(PALETTE.get(colour,
                    colour if isinstance(colour, tuple) else (1.0, 0.0, 1.0)))
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.25
    elif "Specular" in bsdf.inputs:
        bsdf.inputs["Specular"].default_value = 0.25
    if emit > 0.0:
        bsdf.inputs["Emission Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = emit
    return mat


def box(name, size, loc, colour, rot=(0, 0, 0), rough=0.75, metal=0.0, emit=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (size[0] / 2.0, size[1] / 2.0, size[2] / 2.0)
    ob.rotation_euler = rot
    ob.data.materials.append(material(name, colour, rough, metal, emit))
    return ob


def cyl(name, radius, depth, loc, colour, rot=(0, 0, 0), verts=24,
        rough=0.75, metal=0.0, emit=0.0):
    bpy.ops.mesh.primitive_cylinder_add(radius=radius, depth=depth,
                                        location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(material(name, colour, rough, metal, emit))
    return ob


def sphere(name, radius, loc, colour, rough=0.75, emit=0.0):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=radius, location=loc,
                                         segments=24, ring_count=12)
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(material(name, colour, rough, 0.0, emit))
    return ob


def bevel(ob, width=0.012, segments=2):
    """A hair of bevel on every edge. Without it a cube renders as three flat
    fields of colour with no edge between them, and at this size the silhouette
    is all the reader has to go on."""
    m = ob.modifiers.new("bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(35)
    return ob


def setup_world():
    """A warm key from the upper left, a cool fill from the right, and a dim
    ambient so nothing goes to pure black. Directional rather than soft: the
    downsample below averages away anything subtle anyway, so the lighting has
    to state the form plainly."""
    world = bpy.data.worlds.new("props") if not bpy.data.worlds else bpy.data.worlds[0]
    bpy.context.scene.world = world
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.34, 0.36, 0.42, 1.0)
    bg.inputs["Strength"].default_value = 1.25

    key = bpy.data.lights.new("key", "SUN")
    key.energy = 3.2
    key.angle = math.radians(12)
    key.color = (1.0, 0.94, 0.84)
    ob = bpy.data.objects.new("key", key)
    bpy.context.collection.objects.link(ob)
    ob.rotation_euler = (math.radians(66), 0, math.radians(-34))

    fill = bpy.data.lights.new("fill", "SUN")
    fill.energy = 0.9
    fill.color = (0.72, 0.80, 1.0)
    ob2 = bpy.data.objects.new("fill", fill)
    bpy.context.collection.objects.link(ob2)
    ob2.rotation_euler = (math.radians(62), 0, math.radians(125))


# A shadow catcher was tried here and removed. With a transparent film it
# writes shadow density into the alpha channel, so a dense shadow and the prop
# itself both arrive as "nearly opaque" and nothing downstream can tell them
# apart. The contact shadow is drawn in make_props.ps1 instead, from the prop's
# own silhouette, where the two are never confused.


def setup_camera(span):
    """Orthographic, aimed at the middle of a `span`-wide cube sitting on the
    floor. Orthographic matters: a perspective camera makes two copies of the
    same prop placed at different spots on a map look like different objects."""
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = span
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    elev = math.radians(CAMERA_ELEVATION)
    dist = span * 3.0
    target = Vector((0.0, 0.0, span * 0.22))
    cam.location = target + Vector((0.0,
                                    -math.cos(elev) * dist,
                                    math.sin(elev) * dist))
    cam.rotation_euler = (math.radians(90.0 - CAMERA_ELEVATION), 0.0, 0.0)


def setup_render(px):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 96
    scene.cycles.use_denoising = True
    try:
        scene.cycles.device = "CPU"
    except Exception:
        pass
    scene.render.resolution_x = px
    scene.render.resolution_y = px
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filter_size = 0.6      # keep edges tight for the downsample
    # Standard rather than a film curve: the palette above is chosen in the
    # colours we want out, and a tone curve would shift every one of them.
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"


def render_to(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


# --- the props ---------------------------------------------------------------
# Each builds itself around the origin, standing on Z=0, and declares how wide
# a square it wants the camera to frame.

def prop_long_table():
    top = box("table_top", (2.30, 0.95, 0.09), (0, 0, 0.74), "oak_light")
    bevel(top, 0.02)
    for sx in (-0.95, 0.95):
        for sy in (-0.33, 0.33):
            bevel(box("leg", (0.17, 0.17, 0.70), (sx, sy, 0.35), "oak"))
    bevel(box("rail", (1.95, 0.13, 0.13), (0, 0, 0.52), "oak"))
    return 2.9


def prop_bench():
    seat = box("seat", (1.70, 0.42, 0.08), (0, 0, 0.44), "oak_light")
    bevel(seat, 0.018)
    for sx in (-0.66, 0.66):
        bevel(box("plank_leg", (0.16, 0.36, 0.40), (sx, 0, 0.20), "oak"))
    bevel(box("stretcher", (1.30, 0.08, 0.08), (0, 0, 0.26), "oak"))
    return 2.2


def prop_chair():
    seat = box("seat", (0.56, 0.54, 0.07), (0, 0, 0.46), "oak_light")
    bevel(seat, 0.016)
    bevel(box("back", (0.54, 0.08, 0.62), (0, 0.23, 0.77), "oak"))
    bevel(box("slat", (0.44, 0.05, 0.10), (0, 0.19, 0.62), "oak_pale"))
    for sx in (-0.22, 0.22):
        for sy in (-0.21, 0.21):
            bevel(box("leg", (0.10, 0.10, 0.44), (sx, sy, 0.22), "oak"))
    return 1.15


def prop_bookshelf():
    bevel(box("back", (1.30, 0.10, 1.90), (0, 0.20, 0.95), "oak"))
    for sx in (-0.62, 0.62):
        bevel(box("side", (0.10, 0.50, 1.90), (sx, 0, 0.95), "oak"))
    # Shelves, and a scatter of books that is deliberately uneven -- a shelf
    # filled edge to edge reads as a striped box rather than as books.
    heights = (0.36, 0.82, 1.28, 1.72)
    fills = ((-0.52, 0.34), (-0.54, 0.50), (-0.30, 0.46), (-0.52, 0.20))
    tones = ("cloth_red", "cloth_blue", "leaf", "paper", "brass")
    for i, h in enumerate(heights):
        bevel(box("shelf", (1.20, 0.46, 0.05), (0, 0, h), "oak_pale"), 0.012)
        x, end = fills[i]
        n = 0
        while x < end:
            w = 0.055 + 0.03 * ((i + n) % 3)
            tall = 0.26 + 0.05 * ((i * 2 + n) % 3)
            bevel(box("book", (w, 0.30, tall), (x + w / 2, -0.02, h + tall / 2 + 0.03),
                      tones[(i * 3 + n) % len(tones)]), 0.006)
            x += w + 0.012
            n += 1
    return 2.2


def prop_hearth():
    bevel(box("back", (1.70, 0.36, 1.55), (0, 0.42, 0.78), "stone"))
    for sx in (-0.66, 0.66):
        bevel(box("jamb", (0.38, 0.52, 1.20), (sx, 0.12, 0.60), "stone"))
    bevel(box("lintel", (1.70, 0.56, 0.26), (0, 0.12, 1.32), "stone_pale"))
    bevel(box("hearthstone", (1.66, 0.60, 0.10), (0, -0.18, 0.05), "stone_pale"))
    # Logs and the fire itself. The emission is what makes it read as lit
    # rather than as a hole in a wall.
    for i, (x, z, ang) in enumerate(((-0.16, 0.16, 0.5), (0.18, 0.15, -0.4),
                                     (0.0, 0.30, 0.15))):
        cyl("log", 0.075, 0.62, (x, 0.14, z), "oak",
            rot=(0, math.radians(90), ang), verts=10)
    sphere("flame", 0.24, (0.0, 0.14, 0.36), "ember", emit=2.2)
    sphere("flame_tip", 0.13, (0.02, 0.12, 0.55), "ember", emit=3.0)
    return 2.3


def prop_weapon_rack():
    bevel(box("base", (1.35, 0.40, 0.12), (0, 0, 0.06), "oak"))
    for sx in (-0.60, 0.60):
        bevel(box("post", (0.10, 0.10, 1.45), (sx, 0, 0.72), "oak"))
    bevel(box("crossbar", (1.30, 0.09, 0.09), (0, 0, 1.32), "oak"))
    bevel(box("lower_bar", (1.30, 0.09, 0.09), (0, 0, 0.52), "oak"))
    # Two spears and a sword, leaning at slightly different angles so the rack
    # looks used rather than displayed.
    for x, tilt in ((-0.34, 0.05), (0.06, -0.03)):
        cyl("haft", 0.035, 1.58, (x, -0.02, 0.80), "oak_pale",
            rot=(tilt, 0, 0), verts=8)
        bevel(box("spearhead", (0.10, 0.03, 0.30), (x, -0.02 - 1.58 * tilt / 2, 1.66),
                  "iron_light", rot=(tilt, 0, 0), metal=0.85, rough=0.35), 0.008)
    bevel(box("blade", (0.10, 0.028, 0.86), (0.40, -0.04, 0.92), "iron_light",
              rot=(0.06, 0, 0), metal=0.9, rough=0.3), 0.008)
    bevel(box("guard", (0.28, 0.06, 0.06), (0.40, -0.04, 0.50), "brass",
              metal=0.8, rough=0.4), 0.01)
    bevel(box("grip", (0.06, 0.06, 0.26), (0.40, -0.04, 0.36), "oak"), 0.012)
    return 2.1


def prop_barrel():
    # A barrel bulges in the middle. Built as a stack of thin discs following a
    # cosine so the silhouette curves instead of stepping -- a handful of fat
    # slices left visible ledges once the render was reduced to forty pixels.
    slices = 14
    for i in range(slices):
        t = (i + 0.5) / slices
        z = 0.03 + t * 0.72
        r = 0.27 + 0.085 * math.sin(t * math.pi)
        cyl("stave", r, 0.062, (0, 0, z), "oak")
    for z in (0.17, 0.61):
        cyl("hoop", 0.352, 0.055, (0, 0, z), "iron", metal=0.7, rough=0.5)
    cyl("lid", 0.28, 0.04, (0, 0, 0.77), "oak_pale")
    return 1.15


def prop_strongbox():
    bevel(box("body", (0.86, 0.60, 0.40), (0, 0, 0.20), "oak"), 0.018)
    # The lid sits proud of the body on every side, so there is a visible line
    # between the two from above. A flush lid renders as one brown block.
    bevel(box("lid", (0.92, 0.66, 0.16), (0, 0, 0.48), "oak_light"), 0.02)
    for sx in (-0.28, 0.28):
        bevel(box("band", (0.10, 0.68, 0.58), (sx, 0, 0.28), "iron",
                  metal=0.7, rough=0.45), 0.008)
    bevel(box("lock", (0.18, 0.08, 0.16), (0, -0.33, 0.44), "brass",
              metal=0.8, rough=0.35), 0.01)
    return 1.25


def prop_rug():
    # Flat, so it is really only read by its pattern. Three nested rectangles
    # is enough at this size and survives the downsample.
    box("rug", (2.20, 1.50, 0.012), (0, 0, 0.006), "cloth_red", rough=0.95)
    box("rug_border", (1.94, 1.24, 0.014), (0, 0, 0.008), "cloth_cream", rough=0.95)
    box("rug_field", (1.72, 1.04, 0.016), (0, 0, 0.010), "cloth_blue", rough=0.95)
    box("rug_motif", (0.62, 0.40, 0.018), (0, 0, 0.012), "cloth_cream", rough=0.95)
    return 2.6


def prop_banner():
    cyl("rod", 0.045, 1.10, (0, 0, 1.72), "oak",
        rot=(0, math.radians(90), 0), verts=10)
    bevel(box("cloth", (0.92, 0.05, 1.42), (0, 0.03, 0.99), "cloth_blue", rough=0.95),
          0.01)
    bevel(box("chevron", (0.62, 0.02, 0.30), (0, 0.005, 1.30), "cloth_cream",
              rough=0.95), 0.006)
    bevel(box("chevron2", (0.40, 0.02, 0.22), (0, 0.005, 0.92), "brass",
              metal=0.6, rough=0.5), 0.006)
    # The pointed tail, made by rotating a square corner into a diamond.
    bevel(box("tail", (0.65, 0.05, 0.65), (0, 0.03, 0.28), "cloth_blue",
              rot=(0, math.radians(45), 0), rough=0.95), 0.01)
    return 2.1


def prop_candlestand():
    cyl("foot", 0.26, 0.07, (0, 0, 0.035), "iron", verts=14, metal=0.7, rough=0.45)
    cyl("stem", 0.075, 1.30, (0, 0, 0.68), "iron", verts=12, metal=0.7, rough=0.45)
    cyl("pan", 0.20, 0.05, (0, 0, 1.34), "iron", verts=14, metal=0.7, rough=0.45)
    for x, y in ((-0.10, 0.0), (0.06, 0.07), (0.06, -0.07)):
        cyl("candle", 0.045, 0.28, (x, y, 1.50), "cloth_cream", verts=8)
        sphere("wick_flame", 0.055, (x, y, 1.68), "ember", emit=3.4)
    return 2.0


def prop_table_round():
    top = cyl("top", 0.66, 0.08, (0, 0, 0.72), "oak_light", verts=20)
    bevel(top, 0.016)
    cyl("column", 0.18, 0.68, (0, 0, 0.34), "oak", verts=16)
    cyl("foot", 0.42, 0.07, (0, 0, 0.035), "oak", verts=16)
    return 1.7


def prop_lectern():
    cyl("foot", 0.38, 0.09, (0, 0, 0.045), "oak", verts=16)
    cyl("column", 0.15, 1.00, (0, 0, 0.50), "oak", verts=16)
    bevel(box("desk", (0.72, 0.56, 0.07), (0, 0, 1.05), "oak_light",
              rot=(math.radians(-24), 0, 0)), 0.014)
    bevel(box("lip", (0.72, 0.06, 0.07), (0, -0.24, 0.99), "oak"), 0.01)
    bevel(box("book", (0.50, 0.38, 0.06), (0, 0.02, 1.12), "paper",
              rot=(math.radians(-24), 0, 0)), 0.008)
    return 1.8


def prop_signpost():
    """A waymarker for the crossroads. This one is here because none of the
    packs contain a signpost: the overworld waymarker had been borrowing the
    guild hall's own plaque, so the first thing a new player saw was a sign
    reading GUILD HALL standing in an empty field."""
    # Everything here is deliberately thick. At fifty-six pixels across a
    # two-and-a-half metre frame, a realistic 8cm post is two pixels wide and
    # disappears under the outline pass; the first attempt at this had boards
    # floating free of a post that had all but vanished.
    cyl("post", 0.15, 1.90, (0, 0, 0.95), "oak", verts=14)
    cyl("cap", 0.20, 0.13, (0, 0, 1.94), "oak_light", verts=14)
    # Two boards pointing opposite ways, each overlapping the post so the three
    # read as one object. The upper is weathered paler than the lower.
    for z, sx, tone in ((1.46, 1.0, "oak_pale"), (1.02, -1.0, "oak_light")):
        # One plain board. A separate rotated arrow-tip was tried and dropped:
        # at this size it landed a pixel clear of the board and read as a speck
        # of dirt floating beside the sign.
        bevel(box("board", (1.02, 0.13, 0.34), (sx * 0.40, 0, z), tone), 0.016)
    bevel(box("plinth", (0.52, 0.44, 0.16), (0, 0, 0.08), "stone_pale"), 0.02)
    return 1.95


PROPS = {
    "signpost":    (prop_signpost,    56),
    "table_long":  (prop_long_table,  96),
    "table_round": (prop_table_round, 64),
    "bench":       (prop_bench,       72),
    "chair":       (prop_chair,       40),
    "bookshelf":   (prop_bookshelf,   72),
    "hearth":      (prop_hearth,      80),
    "weapon_rack": (prop_weapon_rack, 72),
    "barrel":      (prop_barrel,      40),
    "strongbox":   (prop_strongbox,   44),
    "rug":         (prop_rug,         88),
    "banner":      (prop_banner,      72),
    "candlestand": (prop_candlestand, 64),
    "lectern":     (prop_lectern,     56),
}


def main():
    only = None
    if "--" in sys.argv:
        rest = sys.argv[sys.argv.index("--") + 1:]
        if rest:
            only = set(rest)

    for name, (builder, out_px) in sorted(PROPS.items()):
        if only and name not in only:
            continue
        clear_scene()
        setup_world()
        span = builder()
        setup_camera(span)
        setup_render(out_px * SUPERSAMPLE)
        path = os.path.join(RENDER_DIR, "%s.png" % name)
        render_to(path)
        print("rendered %-14s %dpx -> %s" % (name, out_px * SUPERSAMPLE, path))


if __name__ == "__main__":
    main()
