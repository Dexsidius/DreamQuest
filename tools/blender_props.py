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
    # The forge.
    "coal":       (0.160, 0.150, 0.150),
    "coal_hot":   (0.420, 0.160, 0.080),
    "leather":    (0.459, 0.302, 0.192),
    "water":      (0.290, 0.478, 0.604),
    "brick":      (0.588, 0.365, 0.278),
    "brick_dark": (0.420, 0.259, 0.208),
    "soot":       (0.227, 0.212, 0.216),
    # The inn and the houses.
    "plaster":    (0.867, 0.820, 0.702),
    "shingle":    (0.380, 0.259, 0.216),
    "shingle_dk": (0.290, 0.192, 0.165),
    "glass":      (0.420, 0.557, 0.690),
    "glass_lit":  (1.000, 0.816, 0.478),
    "pewter":     (0.620, 0.631, 0.659),
    "ale":        (0.788, 0.537, 0.169),
    "foam":       (0.949, 0.918, 0.827),
    "bottle":     (0.263, 0.451, 0.286),
    "bottle_br":  (0.494, 0.318, 0.157),
    "straw":      (0.816, 0.702, 0.408),
    # The Westwold's steadings and the Brackenwood's den.
    "hide_tan":   (0.640, 0.440, 0.280), "hide_pale":  (0.820, 0.690, 0.520),
    "hay":        (0.800, 0.660, 0.300), "hay_lt":     (0.900, 0.780, 0.420), "hay_dk": (0.600, 0.470, 0.200),
    "earth_dk":   (0.300, 0.230, 0.170), "void":       (0.040, 0.035, 0.045),
    "rock":       (0.500, 0.490, 0.480), "rock_dk":    (0.350, 0.340, 0.350),
    "bone":       (0.890, 0.855, 0.769),
    "clay":       (0.694, 0.408, 0.286),
    "wool_green": (0.365, 0.498, 0.318),
    "chalk":      (0.188, 0.200, 0.192),
    # Woodland: Mossvale and the Whisperwood. Weathered logs rather than sawn
    # oak, and moss that is a different green from the leaves around it.
    "log":        (0.431, 0.306, 0.196),
    "log_dk":     (0.310, 0.216, 0.141),
    "log_end":    (0.776, 0.639, 0.443),
    "moss":       (0.353, 0.463, 0.243),
    "moss_dk":    (0.247, 0.349, 0.180),
    "moss_lt":    (0.494, 0.600, 0.310),
    "antler":     (0.878, 0.812, 0.690),
    # Thatch: weathered straw, darker than fresh "straw" so a roof of it does
    # not read as a sheet of yellow.
    "thatch":     (0.690, 0.545, 0.314),
    "thatch_dk":  (0.522, 0.396, 0.224),
    "thatch_lt":  (0.800, 0.682, 0.420),
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


def setup_camera(span, elevation=None):
    """Orthographic, aimed at the middle of a `span`-wide cube sitting on the
    floor. Orthographic matters: a perspective camera makes two copies of the
    same prop placed at different spots on a map look like different objects."""
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = span
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    elevation = CAMERA_ELEVATION if elevation is None else elevation
    elev = math.radians(elevation)
    dist = span * 3.0
    # Aimed a third of the way up the frame rather than a fifth. At a fifth,
    # anything taller than it is wide -- a forge with its chimney, an armour
    # stand -- was cut off at the top while a quarter of the frame stood empty
    # under it. make_props.ps1 sits every prop on the bottom of its image
    # afterwards anyway, so aiming high costs nothing.
    target = Vector((0.0, 0.0, span * 0.34))
    cam.location = target + Vector((0.0,
                                    -math.cos(elev) * dist,
                                    math.sin(elev) * dist))
    cam.rotation_euler = (math.radians(90.0 - elevation), 0.0, 0.0)


def setup_render(px):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 96
    scene.cycles.use_denoising = True
    # Render in small tiles. A building at 1536 pixels square with denoising
    # needs a few gigabytes rendered in one piece, and ran out of memory on a
    # machine with other work open; tiling changes nothing in the image.
    try:
        scene.cycles.use_auto_tile = True
        scene.cycles.tile_size = 256
    except Exception:
        pass
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


# -----------------------------------------------------------------------------
#  The forge
#
#  Built with blk() rather than box(). box() scales a unit cube by half the
#  size it is given, so everything above is modelled at half its written
#  dimensions -- which is why those props sit small in their frames. Changing
#  box() would resize the signpost already placed on the overworld, so the
#  forge set gets a helper that means what it says instead.
#
#  Everything here is chunky on purpose. At forty pixels a pair of tongs is two
#  pixels wide, and a two-pixel feature is all outline once make_props.ps1 has
#  been over it; the tools on the rack are thicker than any real tool would be
#  for exactly that reason.
# -----------------------------------------------------------------------------

def blk(name, size, loc, colour, rot=(0, 0, 0), rough=0.8, metal=0.0, emit=0.0,
        bev=0.015):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = size
    ob.rotation_euler = rot
    ob.data.materials.append(material(name, colour, rough, metal, emit))
    if bev > 0:
        bevel(ob, bev)
    return ob


def turn_all(degrees):
    """Rotates everything built so far about the vertical axis through the
    origin. Parenting to a rotated empty keeps each part's own transform
    untouched, so props are still modelled square-on and turned at the end."""
    pivot = bpy.data.objects.new("pivot", None)
    bpy.context.collection.objects.link(pivot)
    for ob in list(bpy.context.scene.objects):
        if ob is pivot or ob.type not in {"MESH"} or ob.parent is not None:
            continue
        ob.parent = pivot
    pivot.rotation_euler = (0.0, 0.0, math.radians(degrees))


def prop_forge():
    """The centrepiece: a brick hearth with a bed of coals, a hood over it to
    carry the smoke, and a chimney. The coals glow, and are the only emissive
    thing in the set, so the eye finds the forge first."""
    # The hearth body.
    blk("hearth_body", (1.90, 1.10, 0.78), (0, 0.10, 0.39), "brick")
    blk("hearth_band", (1.96, 1.16, 0.10), (0, 0.10, 0.06), "brick_dark")
    blk("hearth_lip", (1.96, 1.16, 0.10), (0, 0.10, 0.80), "stone_pale")
    # The fire bed, sunk into the top.
    blk("bed_rim", (1.50, 0.80, 0.10), (0, 0.02, 0.88), "stone")
    blk("coals", (1.30, 0.62, 0.06), (0, 0.02, 0.92), "coal_hot", emit=1.4, bev=0)
    for i, (x, y) in enumerate(((-0.42, -0.10), (-0.14, 0.12), (0.18, -0.06),
                                (0.44, 0.10), (-0.28, 0.18), (0.30, 0.20))):
        blk("coal_%d" % i, (0.16, 0.14, 0.10), (x, y, 0.96), "coal", bev=0.02)
    sphere("glow_a", 0.15, (-0.10, 0.00, 0.97), "ember", emit=1.5)
    sphere("glow_b", 0.10, (0.26, 0.04, 0.97), "ember", emit=1.3)
    # An iron bar across the front, and a mouth where the heat is let out.
    blk("fire_bar", (1.52, 0.07, 0.07), (0, -0.40, 0.98), "iron", metal=0.7, rough=0.5)
    blk("mouth", (0.62, 0.05, 0.30), (0, -0.46, 0.44), "coal", bev=0.01)
    blk("mouth_glow", (0.44, 0.04, 0.16), (0, -0.48, 0.40), "ember", emit=1.0, bev=0)
    # The hood and chimney, set back so the fire is not hidden behind them.
    blk("hood", (1.70, 0.62, 0.52), (0, 0.46, 1.52), "brick_dark")
    blk("hood_lip", (1.76, 0.68, 0.08), (0, 0.44, 1.26), "stone")
    blk("chimney", (0.70, 0.52, 0.78), (0, 0.52, 2.16), "brick")
    blk("chimney_cap", (0.80, 0.60, 0.08), (0, 0.52, 2.58), "stone_pale")
    blk("soot_mark", (0.56, 0.03, 0.40), (0, 0.19, 1.58), "soot", bev=0)
    return 3.7


def prop_anvil():
    """On a stump, with its horn pointing off to one side so the silhouette is
    unmistakably an anvil and not a block of iron."""
    cyl("stump", 0.30, 0.46, (0, 0, 0.23), "oak")
    cyl("stump_top", 0.28, 0.02, (0, 0, 0.465), "oak_pale")
    blk("waist", (0.26, 0.22, 0.14), (0, 0, 0.53), "iron", metal=0.75, rough=0.5)
    blk("foot", (0.42, 0.30, 0.06), (0, 0, 0.49), "iron", metal=0.75, rough=0.5)
    blk("face", (0.58, 0.28, 0.14), (0.02, 0, 0.66), "iron_light", metal=0.85, rough=0.35)
    # The horn: a block tapering to a point, done as two steps.
    blk("horn_a", (0.20, 0.20, 0.11), (0.38, 0, 0.665), "iron_light", metal=0.85, rough=0.35)
    blk("horn_b", (0.14, 0.12, 0.07), (0.53, 0, 0.67), "iron_light", metal=0.85, rough=0.35)
    blk("heel", (0.12, 0.24, 0.12), (-0.33, 0, 0.66), "iron_light", metal=0.85, rough=0.35)
    # A hammer left on it mid-job.
    blk("hammer_handle", (0.44, 0.06, 0.06), (-0.02, -0.02, 0.76), "oak_pale",
        rot=(0, 0, math.radians(18)))
    blk("hammer_head", (0.10, 0.18, 0.12), (0.18, 0.06, 0.78), "iron", metal=0.7,
        rot=(0, 0, math.radians(18)))
    return 1.35


def prop_bellows():
    """Leather bellows on a frame, pointed at where the forge will be. Two
    boards with a fat leather body between them reads better than any attempt
    at the pleats."""
    blk("frame_l", (0.08, 0.08, 0.40), (-0.38, 0, 0.20), "oak")
    blk("frame_r", (0.08, 0.08, 0.40), (0.38, 0, 0.20), "oak")
    blk("frame_bar", (0.84, 0.08, 0.08), (0, 0, 0.38), "oak")
    blk("board_low", (1.00, 0.52, 0.05), (0, 0, 0.46), "oak_light")
    blk("leather", (0.94, 0.56, 0.30), (0.02, 0, 0.62), "cloth_red", rough=0.95, bev=0.09)
    blk("leather_fold", (0.96, 0.58, 0.05), (0.02, 0, 0.62), "leather", rough=0.95, bev=0.02)
    blk("board_top", (1.00, 0.52, 0.05), (0.02, 0, 0.80), "oak_light",
        rot=(0, math.radians(-8), 0))
    blk("handle", (0.30, 0.08, 0.08), (-0.60, 0, 0.78), "oak",
        rot=(0, math.radians(-8), 0))
    cyl("nozzle", 0.06, 0.34, (0.66, 0, 0.56), "iron", rot=(0, math.radians(90), 0),
        metal=0.7, rough=0.5)
    return 1.6


def prop_quench_trough():
    """A trough of water the work is plunged into. The water is the only blue
    in the room, which is what lets it read at all at this size."""
    # Built as a floor and four walls rather than one solid block, with the
    # front wall lowest. From this angle the water is otherwise hidden behind
    # the trough's own front edge, and a trough with no visible water is a box.
    blk("trough_floor", (1.30, 0.66, 0.10), (0, 0, 0.05), "oak")
    blk("wall_back", (1.30, 0.08, 0.46), (0, 0.29, 0.23), "oak")
    blk("wall_front", (1.30, 0.08, 0.26), (0, -0.29, 0.13), "oak_light")
    blk("wall_l", (0.08, 0.66, 0.40), (-0.61, 0, 0.20), "oak")
    blk("wall_r", (0.08, 0.66, 0.40), (0.61, 0, 0.20), "oak")
    blk("water", (1.16, 0.52, 0.04), (0, 0, 0.24), "water", rough=0.15, bev=0)
    blk("water_shine", (0.40, 0.10, 0.045), (-0.20, 0.06, 0.245), "paper", rough=0.1, bev=0)
    for x in (-0.44, 0.44):
        blk("band", (0.06, 0.70, 0.30), (x, 0, 0.15), "iron", metal=0.7, rough=0.5)
    # Tongs propped in the water, handles out.
    blk("tong_a", (0.05, 0.05, 0.62), (0.34, -0.06, 0.40), "iron", metal=0.7,
        rot=(math.radians(30), math.radians(-20), 0))
    blk("tong_b", (0.05, 0.05, 0.62), (0.42, -0.02, 0.40), "iron", metal=0.7,
        rot=(math.radians(30), math.radians(-6), 0))
    return 1.6


def prop_grindstone():
    """A stone wheel on a wooden frame, with a crank. The wheel is turned edge
    on to the camera, which is the one angle a wheel is recognisable from."""
    blk("base", (0.90, 0.40, 0.10), (0, 0, 0.05), "oak")
    blk("post_l", (0.10, 0.12, 0.62), (-0.30, 0, 0.36), "oak")
    blk("post_r", (0.10, 0.12, 0.62), (0.30, 0, 0.36), "oak")
    cyl("wheel", 0.36, 0.16, (0, 0, 0.62), "stone_pale", rot=(0, math.radians(90), 0),
        rough=0.95)
    cyl("wheel_face", 0.28, 0.17, (0, 0, 0.62), "stone", rot=(0, math.radians(90), 0),
        rough=0.95)
    cyl("axle", 0.04, 0.78, (0, 0, 0.62), "iron", rot=(0, math.radians(90), 0),
        metal=0.7)
    blk("crank", (0.06, 0.06, 0.26), (0.42, 0, 0.54), "iron", metal=0.7)
    blk("crank_grip", (0.14, 0.07, 0.07), (0.46, 0, 0.42), "oak_pale")
    # A water pot hung under the wheel, as grindstones had.
    cyl("pot", 0.12, 0.14, (0, -0.10, 0.20), "iron", metal=0.5)
    # Square-on, the wheel is its rim: a grey bar between two posts. Turned
    # most of the way round, it is a disc, which is what a grindstone is.
    turn_all(58)
    return 1.35


def prop_tool_rack():
    """A board of pegs with hammers and tongs hung on it. Stands against a
    wall, so it is built facing the camera like a shelf."""
    blk("board", (1.30, 0.08, 0.96), (0, 0.10, 0.62), "oak")
    blk("board_top", (1.36, 0.14, 0.08), (0, 0.10, 1.12), "oak_light")
    blk("leg_l", (0.08, 0.10, 0.60), (-0.60, 0.10, 0.30), "oak")
    blk("leg_r", (0.08, 0.10, 0.60), (0.60, 0.10, 0.30), "oak")
    # Hammers, hanging head up.
    for i, x in enumerate((-0.42, -0.14)):
        blk("h_handle_%d" % i, (0.06, 0.06, 0.46), (x, 0.02, 0.70), "oak_pale")
        blk("h_head_%d" % i, (0.24, 0.12, 0.12), (x, 0.02, 0.96), "iron", metal=0.7)
    # Tongs: two long bars splayed at the jaws.
    for i, (x, tilt) in enumerate(((0.18, 6), (0.30, -6))):
        blk("tong_%d" % i, (0.05, 0.05, 0.64), (x, 0.02, 0.68), "iron", metal=0.7,
            rot=(0, math.radians(tilt), 0))
    # A horseshoe, as a squared-off U, because a torus at this size is a dot.
    blk("shoe_l", (0.05, 0.05, 0.22), (0.44, 0.02, 0.62), "iron_light", metal=0.8)
    blk("shoe_r", (0.05, 0.05, 0.22), (0.56, 0.02, 0.62), "iron_light", metal=0.8)
    blk("shoe_b", (0.17, 0.05, 0.05), (0.50, 0.02, 0.52), "iron_light", metal=0.8)
    return 1.7


def prop_coal_bin():
    """An open crate heaped with coal and a shovel stuck in it."""
    blk("bin_front", (0.80, 0.06, 0.36), (0, -0.30, 0.18), "oak")
    blk("bin_back", (0.80, 0.06, 0.46), (0, 0.30, 0.23), "oak")
    blk("bin_l", (0.06, 0.60, 0.40), (-0.37, 0, 0.20), "oak")
    blk("bin_r", (0.06, 0.60, 0.40), (0.37, 0, 0.20), "oak")
    for i in range(9):
        x = -0.24 + (i % 3) * 0.24
        y = -0.14 + (i // 3) * 0.14
        sphere("lump_%d" % i, 0.11, (x, y, 0.34 + 0.03 * ((i * 5) % 3)), "coal",
               rough=0.9)
    blk("shovel_handle", (0.05, 0.05, 0.62), (0.18, 0.06, 0.62), "oak_pale",
        rot=(math.radians(-14), math.radians(18), 0))
    blk("shovel_blade", (0.20, 0.04, 0.22), (0.10, 0.12, 0.36), "iron", metal=0.7,
        rot=(math.radians(-14), math.radians(18), 0))
    return 1.15


def prop_ingot_crate():
    """A shallow crate of iron bars, stacked the way bars are stacked, with a
    couple of lumps of ore beside them for what the bars were."""
    blk("crate", (0.80, 0.56, 0.24), (0, 0, 0.12), "oak_light")
    blk("crate_band", (0.84, 0.60, 0.05), (0, 0, 0.22), "oak")
    for layer in range(2):
        for i in range(3 - layer):
            x = -0.20 + i * 0.20 + layer * 0.10
            blk("bar_%d_%d" % (layer, i), (0.16, 0.40, 0.08),
                (x, 0, 0.28 + layer * 0.08), "iron_light", metal=0.85, rough=0.35)
    sphere("ore_a", 0.11, (0.52, -0.16, 0.10), "stone", rough=0.9)
    sphere("ore_b", 0.08, (0.54, 0.06, 0.08), "brick_dark", rough=0.9)
    return 1.25


def prop_armour_stand():
    """A cross of timber wearing a breastplate and a helm -- the smith's work on
    show, which is the thing that makes a room a shop rather than a workshop."""
    blk("base", (0.52, 0.40, 0.06), (0, 0, 0.03), "oak")
    blk("post", (0.08, 0.08, 1.20), (0, 0, 0.62), "oak")
    blk("arms", (0.72, 0.08, 0.08), (0, 0, 1.04), "oak")
    blk("breastplate", (0.46, 0.26, 0.52), (0, -0.04, 0.84), "iron_light",
        metal=0.85, rough=0.3, bev=0.08)
    blk("plackart", (0.38, 0.22, 0.14), (0, -0.06, 0.54), "iron", metal=0.8, rough=0.35,
        bev=0.04)
    for x in (-0.30, 0.30):
        sphere("pauldron", 0.13, (x, -0.02, 1.06), "iron_light")
    sphere("helm", 0.18, (0, -0.02, 1.36), "iron_light")
    blk("visor", (0.26, 0.05, 0.05), (0, -0.19, 1.34), "coal", bev=0)
    blk("crest", (0.05, 0.28, 0.08), (0, 0, 1.54), "cloth_red")
    return 2.05


def prop_weapon_barrel():
    """The oldest shop display there is: a barrel of blades, hilts up."""
    slices = 12
    for i in range(slices):
        t = (i + 0.5) / slices
        r = 0.25 + 0.06 * math.sin(t * math.pi)
        cyl("stave", r, 0.056, (0, 0, 0.03 + t * 0.62), "oak")
    for z in (0.14, 0.52):
        cyl("hoop", 0.31, 0.05, (0, 0, z), "iron", metal=0.7, rough=0.5)
    cyl("mouth", 0.23, 0.02, (0, 0, 0.66), "coal")
    # Three swords and an axe, at slightly different leans.
    for i, (x, y, lean_x, lean_y) in enumerate(((-0.11, 0.02, -9, 4),
                                                (0.05, -0.06, 5, -7),
                                                (0.14, 0.10, 12, 6))):
        rot = (math.radians(lean_y), math.radians(lean_x), 0)
        blk("blade_%d" % i, (0.13, 0.05, 0.64), (x, y, 0.94), "iron_light", rot=rot,
            metal=0.6, rough=0.35, bev=0.01)
        blk("guard_%d" % i, (0.22, 0.07, 0.06), (x, y, 1.24), "brass", rot=rot, metal=0.8)
        blk("grip_%d" % i, (0.06, 0.06, 0.18), (x, y, 1.35), "leather", rot=rot)
    blk("axe_haft", (0.06, 0.06, 0.80), (-0.02, 0.14, 0.98), "oak_pale",
        rot=(math.radians(-8), math.radians(-16), 0))
    blk("axe_head", (0.24, 0.05, 0.20), (-0.14, 0.18, 1.30), "iron", metal=0.8,
        rot=(math.radians(-8), math.radians(-16), 0))
    return 1.75


def prop_shop_counter():
    """Where business is done. A heavy plank counter with the day's stock on
    it: a finished blade, a couple of bars and the ledger."""
    blk("counter_body", (2.10, 0.60, 0.84), (0, 0.04, 0.42), "oak")
    blk("counter_panel_l", (0.90, 0.03, 0.56), (-0.52, -0.27, 0.42), "oak_light", bev=0.01)
    blk("counter_panel_r", (0.90, 0.03, 0.56), (0.52, -0.27, 0.42), "oak_light", bev=0.01)
    blk("counter_top", (2.24, 0.72, 0.08), (0, 0.04, 0.88), "oak_pale")
    # On the counter.
    blk("ledger", (0.36, 0.28, 0.06), (-0.66, 0.10, 0.95), "cloth_red")
    blk("ledger_pages", (0.32, 0.26, 0.03), (-0.66, 0.10, 0.99), "paper", bev=0)
    blk("sword_blade", (0.80, 0.07, 0.03), (0.10, -0.02, 0.94), "iron_light",
        metal=0.9, rough=0.3, bev=0.005)
    blk("sword_guard", (0.05, 0.24, 0.05), (-0.32, -0.02, 0.95), "brass", metal=0.8)
    blk("sword_grip", (0.18, 0.06, 0.06), (-0.44, -0.02, 0.95), "leather")
    for i in range(2):
        blk("stock_bar_%d" % i, (0.34, 0.12, 0.07), (0.72, 0.06 + i * 0.14, 0.96),
            "iron_light", metal=0.85, rough=0.35)
    return 2.7


# -----------------------------------------------------------------------------
#  The Barley and Bell -- the tavern downstairs
# -----------------------------------------------------------------------------

def tankard(name, x, y, z, full=True):
    """A pewter tankard with a handle, and a head of foam if it is full."""
    cyl(name, 0.075, 0.18, (x, y, z + 0.09), "pewter", metal=0.6, rough=0.4)
    blk(name + "_handle", (0.04, 0.05, 0.12), (x + 0.10, y, z + 0.09), "pewter",
        metal=0.6, rough=0.4, bev=0.01)
    if full:
        cyl(name + "_ale", 0.068, 0.02, (x, y, z + 0.18), "foam", rough=0.9)


def prop_bar_counter():
    """The bar. Long, heavy and panelled, with a brass foot rail and the
    evening's tankards along the top."""
    blk("bar_body", (2.80, 0.62, 0.92), (0, 0.04, 0.46), "oak")
    for i, x in enumerate((-0.92, 0.0, 0.92)):
        blk("bar_panel_%d" % i, (0.80, 0.03, 0.62), (x, -0.28, 0.48), "oak_light", bev=0.01)
    blk("bar_top", (2.96, 0.80, 0.09), (0, 0.04, 0.965), "oak_pale")
    blk("bar_plinth", (2.86, 0.68, 0.10), (0, 0.04, 0.05), "oak_dark" if "oak_dark" in PALETTE else "oak")
    cyl("foot_rail", 0.035, 2.70, (0, -0.44, 0.18), "brass", rot=(0, math.radians(90), 0),
        metal=0.85, rough=0.35)
    for i, x in enumerate((-1.10, -0.05, 1.12)):
        blk("rail_bracket_%d" % i, (0.04, 0.14, 0.14), (x, -0.38, 0.14), "brass", metal=0.8)
    tankard("tk_a", -0.90, 0.02, 1.01)
    tankard("tk_b", -0.62, 0.10, 1.01)
    tankard("tk_c", 0.72, -0.02, 1.01, full=False)
    blk("bar_cloth", (0.34, 0.24, 0.02), (0.20, 0.06, 1.02), "cloth_cream", bev=0.005)
    return 3.2


def prop_bar_stool():
    cyl("seat", 0.20, 0.07, (0, 0, 0.66), "oak_light")
    for i in range(3):
        a = math.radians(90 + i * 120)
        blk("leg_%d" % i, (0.07, 0.07, 0.64),
            (math.cos(a) * 0.12, math.sin(a) * 0.12, 0.32), "oak",
            rot=(math.sin(a) * 0.14, -math.cos(a) * 0.14, 0))
    cyl("ring", 0.15, 0.03, (0, 0, 0.26), "oak")
    return 0.95


def lying_barrel(name, x, y, z, radius=0.24, length=0.54, tap=True):
    """A barrel on its side with its end toward the camera, which is the one
    view where a keg rack reads as kegs and not as a row of logs."""
    cyl(name, radius, length, (x, y, z), "oak", rot=(math.radians(90), 0, 0))
    for k, dy in enumerate((-length * 0.30, length * 0.30)):
        cyl(name + "_hoop%d" % k, radius + 0.012, 0.045, (x, y + dy, z), "iron",
            rot=(math.radians(90), 0, 0), metal=0.7, rough=0.5)
    cyl(name + "_end", radius * 0.86, 0.02, (x, y - length / 2 - 0.005, z), "oak_pale",
        rot=(math.radians(90), 0, 0))
    if tap:
        blk(name + "_tap", (0.06, 0.10, 0.06), (x, y - length / 2 - 0.06, z - radius * 0.45),
            "brass", metal=0.85, rough=0.35, bev=0.01)


def prop_keg_rack():
    blk("rack_l", (0.08, 0.62, 1.12), (-0.86, 0, 0.56), "oak")
    blk("rack_r", (0.08, 0.62, 1.12), (0.86, 0, 0.56), "oak")
    blk("rack_low", (1.72, 0.62, 0.06), (0, 0, 0.03), "oak")
    blk("rack_mid", (1.72, 0.62, 0.06), (0, 0, 0.56), "oak_light")
    blk("rack_top", (1.80, 0.66, 0.06), (0, 0, 1.12), "oak_light")
    for i, x in enumerate((-0.54, 0.0, 0.54)):
        lying_barrel("keg_low_%d" % i, x, 0, 0.30)
    for i, x in enumerate((-0.27, 0.27)):
        lying_barrel("keg_top_%d" % i, x, 0, 0.83)
    return 2.1


def prop_bottle_shelf():
    blk("back", (1.50, 0.08, 1.40), (0, 0.22, 0.70), "oak")
    blk("side_l", (0.08, 0.40, 1.40), (-0.71, 0.06, 0.70), "oak")
    blk("side_r", (0.08, 0.40, 1.40), (0.71, 0.06, 0.70), "oak")
    tones = ("bottle", "bottle_br", "bottle", "glass", "bottle_br")
    for row, z in enumerate((0.34, 0.80, 1.26)):
        blk("shelf_%d" % row, (1.40, 0.36, 0.05), (0, 0.06, z - 0.03), "oak_light")
        for i in range(5):
            x = -0.52 + i * 0.26
            if (row + i) % 4 == 3:
                tankard("sh_tk_%d_%d" % (row, i), x, 0.02, z, full=False)
                continue
            tone = tones[(row * 2 + i) % len(tones)]
            cyl("bottle_%d_%d" % (row, i), 0.065, 0.24, (x, 0.02, z + 0.12), tone, rough=0.3)
            cyl("neck_%d_%d" % (row, i), 0.028, 0.10, (x, 0.02, z + 0.29), tone, rough=0.3)
    blk("crown", (1.60, 0.44, 0.08), (0, 0.06, 1.42), "oak_light")
    return 1.95


def prop_tavern_table():
    cyl("top", 0.56, 0.08, (0, 0, 0.72), "oak_light")
    cyl("rim", 0.58, 0.03, (0, 0, 0.675), "oak")
    cyl("column", 0.12, 0.66, (0, 0, 0.35), "oak")
    blk("foot_a", (0.86, 0.12, 0.08), (0, 0, 0.04), "oak")
    blk("foot_b", (0.12, 0.86, 0.08), (0, 0, 0.04), "oak")
    tankard("t_a", -0.22, -0.10, 0.76)
    tankard("t_b", 0.20, 0.14, 0.76)
    cyl("candle", 0.05, 0.16, (0.04, -0.20, 0.84), "cloth_cream")
    sphere("flame", 0.05, (0.04, -0.20, 0.96), "ember", emit=2.0)
    return 1.55


def prop_tavern_bench():
    blk("seat", (1.30, 0.38, 0.09), (0, 0, 0.46), "oak_light")
    blk("slab_l", (0.10, 0.34, 0.42), (-0.50, 0, 0.21), "oak")
    blk("slab_r", (0.10, 0.34, 0.42), (0.50, 0, 0.21), "oak")
    blk("stretcher", (1.00, 0.08, 0.08), (0, 0, 0.22), "oak")
    return 1.6


def prop_tavern_chair():
    blk("seat", (0.50, 0.48, 0.07), (0, 0, 0.46), "oak_light")
    for i, (x, y) in enumerate(((-0.20, -0.19), (0.20, -0.19), (-0.20, 0.19), (0.20, 0.19))):
        blk("leg_%d" % i, (0.08, 0.08, 0.44), (x, y, 0.22), "oak")
    blk("post_l", (0.08, 0.08, 0.62), (-0.20, 0.20, 0.78), "oak")
    blk("post_r", (0.08, 0.08, 0.62), (0.20, 0.20, 0.78), "oak")
    blk("slat_a", (0.44, 0.05, 0.10), (0, 0.20, 0.96), "oak_light")
    blk("slat_b", (0.44, 0.05, 0.10), (0, 0.20, 0.72), "oak_light")
    return 1.3


def prop_inn_fireplace():
    """A big stone fireplace with a mantel and a pot over the fire. It is the
    inn's kitchen as well as its hearth, so it is the cooking range too.

    Built from individual stones rather than one block. A single grey slab
    reads as concrete at this size; offset courses of slightly different
    greys are what make it masonry."""
    import random
    rng = random.Random(7)
    tones = ("stone", "stone_pale", "stone")
    # The body, as courses of stones either side of the opening and over it.
    course_h, stone_w = 0.24, 0.42
    for row in range(6):
        z = 0.12 + row * course_h
        offset = (stone_w / 2) if row % 2 else 0.0
        x = -1.05 + offset
        while x < 1.05:
            w = stone_w * (0.8 + rng.random() * 0.4)
            cx = x + w / 2
            inside_opening = (abs(cx) < 0.55 and z < 1.0)
            if not inside_opening and abs(cx) < 1.08:
                blk("stone_%d_%.2f" % (row, x), (w - 0.03, 0.60, course_h - 0.03),
                    (cx, 0.06, z), tones[rng.randrange(3)], bev=0.03)
            x += w
    blk("back", (1.00, 0.10, 0.96), (0, 0.30, 0.48), "coal", bev=0)
    blk("hearthstone", (2.24, 0.80, 0.08), (0, -0.08, 0.04), "stone_pale")
    blk("lintel", (1.24, 0.62, 0.16), (0, 0.06, 1.04), "stone_pale")
    for i, ang in enumerate((0.45, -0.4)):
        cyl("log_%d" % i, 0.07, 0.62, (0, -0.02, 0.14 + i * 0.07), "oak",
            rot=(0, math.radians(90), ang))
    sphere("fire_a", 0.16, (-0.04, -0.04, 0.26), "ember", emit=0.9)
    sphere("fire_b", 0.10, (0.12, -0.06, 0.40), "ember", emit=1.1)
    blk("crane", (0.44, 0.04, 0.04), (0.20, -0.02, 0.86), "iron", metal=0.7)
    cyl("chain", 0.015, 0.20, (0, -0.02, 0.76), "iron", metal=0.7)
    sphere("pot", 0.19, (0, -0.02, 0.56), "iron", rough=0.55)
    cyl("pot_rim", 0.15, 0.03, (0, -0.02, 0.72), "coal")
    blk("mantel", (2.36, 0.42, 0.12), (0, -0.20, 1.40), "oak")
    blk("chimney_breast", (1.10, 0.46, 0.60), (0, 0.14, 1.76), "stone_pale")
    cyl("mantel_candle_a", 0.05, 0.18, (-0.86, -0.20, 1.55), "cloth_cream")
    sphere("mantel_flame_a", 0.05, (-0.86, -0.20, 1.68), "ember", emit=1.6)
    cyl("mantel_jug", 0.10, 0.22, (0.78, -0.20, 1.57), "clay")
    tankard("mantel_tk", 0.46, -0.22, 1.46, full=False)
    return (2.7, None)


def prop_stairs_up():
    """A flight rising away from the camera along a wall, with a banister on
    its open side. Seen from below at a low angle, which is exactly the angle
    that shows every tread."""
    steps = 7
    rise, run, width = 0.21, 0.30, 0.92
    for i in range(steps):
        h = (i + 1) * rise
        y = -0.95 + i * run
        blk("riser_%d" % i, (width, run, h), (0, y, h / 2), "oak", bev=0.008)
        blk("tread_%d" % i, (width + 0.04, run + 0.02, 0.04), (0, y, h + 0.01), "oak_pale",
            bev=0.008)
    total_run = steps * run
    slope = math.atan2(rise, run)
    # A stringer up the open side, and a handrail on posts above it.
    mid_y = -0.95 + total_run / 2 - run / 2
    blk("stringer", (0.08, total_run * 1.18, 0.18), (width / 2 + 0.04, mid_y, steps * rise / 2),
        "oak", rot=(slope, 0, 0), bev=0.01)
    blk("rail", (0.07, total_run * 1.18, 0.07), (width / 2 + 0.04, mid_y, steps * rise / 2 + 0.72),
        "oak_light", rot=(slope, 0, 0), bev=0.01)
    for i in (0, steps // 2, steps - 1):
        h = (i + 1) * rise
        y = -0.95 + i * run
        blk("post_%d" % i, (0.08, 0.08, 0.76), (width / 2 + 0.04, y, h + 0.36), "oak")
    blk("newel", (0.12, 0.12, 0.96), (width / 2 + 0.04, -0.95, 0.48), "oak")
    sphere("newel_cap", 0.08, (width / 2 + 0.04, -0.95, 0.98), "oak_light")
    return 2.85


def prop_crates_sacks():
    blk("crate_a", (0.52, 0.50, 0.46), (-0.18, 0.06, 0.23), "oak_light", bev=0.02)
    blk("crate_a_band", (0.54, 0.52, 0.06), (-0.18, 0.06, 0.40), "oak", bev=0.01)
    blk("crate_b", (0.46, 0.46, 0.40), (-0.14, 0.10, 0.66), "oak_light",
        rot=(0, 0, math.radians(12)), bev=0.02)
    blk("sack_a", (0.40, 0.34, 0.50), (0.34, -0.06, 0.25), "straw", bev=0.13)
    blk("sack_a_tie", (0.14, 0.12, 0.08), (0.34, -0.06, 0.52), "leather", bev=0.03)
    blk("sack_b", (0.36, 0.30, 0.34), (0.26, 0.28, 0.17), "cloth_cream", bev=0.11)
    return 1.35


def prop_chalk_board():
    for i, (x, tilt) in enumerate(((-0.24, 8), (0.24, -8))):
        blk("easel_leg_%d" % i, (0.06, 0.06, 1.10), (x, 0.04, 0.52), "oak",
            rot=(math.radians(-6), math.radians(tilt), 0))
    blk("board", (0.64, 0.05, 0.56), (0, -0.02, 0.72), "chalk")
    blk("frame", (0.70, 0.04, 0.62), (0, 0.01, 0.72), "oak_light")
    for i, (w, z) in enumerate(((0.44, 0.88), (0.30, 0.78), (0.40, 0.66), (0.24, 0.56))):
        blk("chalk_line_%d" % i, (w, 0.01, 0.03), (-0.04 + (0.44 - w) * -0.3, -0.05, z),
            "paper", bev=0)
    return 1.4


def prop_workbench():
    """A carpenter's bench: a thick top on splayed legs, a wooden vise at one
    end, and the day's work on it -- a plank, a saw, a mallet and shavings --
    with more timber stacked on the shelf underneath."""
    blk("top", (1.80, 0.72, 0.14), (0, 0, 0.84), "oak_light")
    blk("apron", (1.72, 0.06, 0.16), (0, -0.33, 0.70), "oak")
    for i, (x, y) in enumerate(((-0.78, -0.28), (0.78, -0.28), (-0.78, 0.28), (0.78, 0.28))):
        blk("leg_%d" % i, (0.14, 0.14, 0.78), (x, y, 0.39), "oak")
    blk("shelf", (1.64, 0.60, 0.06), (0, 0, 0.20), "oak")
    for i in range(3):
        blk("stock_%d" % i, (1.30, 0.16, 0.07), (0.02, -0.14 + i * 0.16, 0.265), "oak_pale",
            bev=0.01)
    # The vise: a jaw block and a screw with a tommy bar through it.
    blk("vise_jaw", (0.12, 0.40, 0.30), (0.96, -0.10, 0.76), "oak")
    cyl("vise_screw", 0.035, 0.34, (1.10, -0.10, 0.80), "iron", rot=(0, math.radians(90), 0),
        metal=0.7)
    blk("vise_bar", (0.04, 0.34, 0.04), (1.26, -0.10, 0.80), "oak_pale")
    # On the top.
    blk("plank", (1.00, 0.22, 0.05), (-0.22, 0.06, 0.935), "oak_pale", bev=0.01)
    blk("saw_blade", (0.56, 0.02, 0.18), (0.36, 0.22, 0.96), "iron_light", metal=0.85,
        rot=(math.radians(70), 0, math.radians(-10)), bev=0.004)
    blk("saw_handle", (0.16, 0.05, 0.12), (0.08, 0.18, 0.96), "oak",
        rot=(math.radians(70), 0, math.radians(-10)))
    blk("mallet_head", (0.20, 0.12, 0.12), (0.52, -0.16, 0.97), "oak", bev=0.02)
    blk("mallet_handle", (0.05, 0.30, 0.05), (0.52, -0.02, 0.945), "oak_pale")
    for i, (x, y) in enumerate(((-0.62, -0.20), (-0.50, -0.24), (-0.70, -0.12))):
        sphere("shaving_%d" % i, 0.05, (x, y, 0.925), "oak_pale")
    # A tool board along the back. Without it this is a table with things on
    # it; with a row of hanging tools it cannot be anything but a workbench.
    blk("board", (1.70, 0.06, 0.62), (0, 0.36, 1.22), "oak")
    blk("board_cap", (1.76, 0.12, 0.06), (0, 0.36, 1.55), "oak_light")
    for i, x in enumerate((-0.62, -0.34)):
        blk("hang_handle_%d" % i, (0.05, 0.05, 0.36), (x, 0.31, 1.20), "oak_pale")
        blk("hang_head_%d" % i, (0.20, 0.08, 0.10), (x, 0.31, 1.40), "iron", metal=0.7)
    blk("hang_saw", (0.40, 0.03, 0.16), (0.02, 0.31, 1.24), "iron_light", metal=0.8)
    blk("hang_saw_grip", (0.12, 0.04, 0.14), (-0.22, 0.31, 1.26), "oak")
    blk("hang_square_a", (0.05, 0.04, 0.30), (0.40, 0.31, 1.24), "iron_light", metal=0.8)
    blk("hang_square_b", (0.22, 0.04, 0.05), (0.49, 0.31, 1.11), "iron_light", metal=0.8)
    cyl("hang_coil", 0.09, 0.05, (0.72, 0.31, 1.26), "leather", rot=(math.radians(90), 0, 0))
    # The vise, made bigger: it was two dark pixels at the end of the bench.
    blk("vise_block", (0.20, 0.46, 0.34), (0.98, -0.12, 0.80), "oak")
    return 2.6


def prop_inn_rug():
    blk("rug", (2.20, 1.40, 0.02), (0, 0, 0.01), "cloth_red", rough=0.95, bev=0)
    blk("rug_border", (1.98, 1.18, 0.022), (0, 0, 0.012), "straw", rough=0.95, bev=0)
    blk("rug_field", (1.80, 1.00, 0.024), (0, 0, 0.014), "cloth_red", rough=0.95, bev=0)
    blk("rug_motif", (0.70, 0.40, 0.026), (0, 0, 0.016), "straw", rough=0.95, bev=0)
    return 2.4


# -----------------------------------------------------------------------------
#  The Barley and Bell -- the rooms upstairs, and Maren's cottage
# -----------------------------------------------------------------------------

def bed(width, blanket):
    """A bed with its head against the wall behind it, so from the front you
    see the footboard, the blanket, and the pillow and headboard beyond."""
    blk("frame", (width, 1.90, 0.28), (0, 0, 0.24), "oak")
    blk("mattress", (width - 0.08, 1.80, 0.18), (0, 0.02, 0.46), "cloth_cream", bev=0.06)
    blk("blanket", (width - 0.02, 1.16, 0.08), (0, -0.30, 0.56), blanket, bev=0.05)
    blk("blanket_fold", (width - 0.02, 0.16, 0.09), (0, 0.24, 0.57), "cloth_cream", bev=0.04)
    pillows = 2 if width > 1.2 else 1
    for i in range(pillows):
        x = 0.0 if pillows == 1 else (-0.30 + i * 0.60)
        blk("pillow_%d" % i, (0.52, 0.30, 0.14), (x, 0.66, 0.60), "paper", bev=0.07)
    blk("headboard", (width + 0.08, 0.10, 0.86), (0, 0.96, 0.60), "oak")
    blk("headboard_cap", (width + 0.16, 0.16, 0.08), (0, 0.96, 1.05), "oak_light")
    blk("footboard", (width + 0.08, 0.10, 0.44), (0, -0.96, 0.32), "oak")
    for x in (-(width / 2), width / 2):
        blk("foot_post_%.2f" % x, (0.10, 0.10, 0.52), (x, -0.96, 0.30), "oak_light")


def prop_bed_single():
    bed(0.92, "cloth_red")
    return 2.35


def prop_bed_double():
    bed(1.46, "cloth_blue")
    return 2.45


def prop_wardrobe():
    blk("body", (1.10, 0.60, 1.70), (0, 0, 0.87), "oak")
    blk("plinth", (1.16, 0.64, 0.08), (0, 0, 0.04), "oak")
    blk("crown", (1.22, 0.70, 0.10), (0, 0, 1.77), "oak_light")
    for i, x in enumerate((-0.27, 0.27)):
        blk("door_%d" % i, (0.50, 0.03, 1.46), (x, -0.31, 0.90), "oak_light", bev=0.01)
        blk("door_inset_%d" % i, (0.34, 0.02, 0.56), (x, -0.33, 1.20), "oak", bev=0.005)
        blk("door_inset_low_%d" % i, (0.34, 0.02, 0.44), (x, -0.33, 0.52), "oak", bev=0.005)
        blk("handle_%d" % i, (0.04, 0.05, 0.12), (x * 0.18, -0.35, 0.92), "brass", metal=0.8)
    return 2.3


def prop_nightstand():
    blk("body", (0.50, 0.44, 0.52), (0, 0, 0.26), "oak")
    blk("top", (0.56, 0.50, 0.05), (0, 0, 0.545), "oak_light")
    blk("drawer", (0.42, 0.03, 0.16), (0, -0.23, 0.38), "oak_light", bev=0.01)
    blk("knob", (0.05, 0.04, 0.05), (0, -0.26, 0.38), "brass", metal=0.8, bev=0.01)
    cyl("holder", 0.09, 0.03, (0.08, 0.0, 0.585), "brass", metal=0.7)
    cyl("candle", 0.045, 0.18, (0.08, 0.0, 0.69), "cloth_cream")
    sphere("flame", 0.045, (0.08, 0.0, 0.81), "ember", emit=1.6)
    return 1.05


def prop_washstand():
    blk("top", (0.70, 0.50, 0.05), (0, 0, 0.74), "oak_light")
    for i, (x, y) in enumerate(((-0.29, -0.19), (0.29, -0.19), (-0.29, 0.19), (0.29, 0.19))):
        blk("leg_%d" % i, (0.07, 0.07, 0.72), (x, y, 0.36), "oak")
    blk("shelf", (0.62, 0.42, 0.04), (0, 0, 0.22), "oak")
    cyl("basin", 0.22, 0.10, (-0.05, 0.02, 0.81), "paper", rough=0.4)
    cyl("basin_water", 0.18, 0.02, (-0.05, 0.02, 0.86), "water", rough=0.2)
    cyl("jug", 0.08, 0.26, (0.24, 0.08, 0.90), "cloth_blue", rough=0.5)
    blk("towel", (0.30, 0.04, 0.34), (-0.20, -0.26, 0.62), "cloth_cream", bev=0.01)
    return 1.3


def prop_travel_chest():
    blk("body", (0.86, 0.54, 0.40), (0, 0, 0.20), "oak", bev=0.02)
    cyl("lid", 0.27, 0.86, (0, 0, 0.40), "oak_light", rot=(0, math.radians(90), 0))
    for x in (-0.30, 0.30):
        blk("band_%.1f" % x, (0.08, 0.58, 0.46), (x, 0, 0.23), "iron", metal=0.7, rough=0.45)
        cyl("band_lid_%.1f" % x, 0.285, 0.08, (x, 0, 0.40), "iron",
            rot=(0, math.radians(90), 0), metal=0.7, rough=0.45)
    blk("lock", (0.16, 0.06, 0.18), (0, -0.29, 0.36), "brass", metal=0.8, rough=0.35)
    return 1.25


def prop_stairwell_down():
    """The top of the stairs, seen from upstairs: an opening in the floor with
    a railing on the two sides you could fall from, and steps going down into
    it. Open at the far end, which is where you step onto them.

    The steps are pale and drop in shallow increments. A true stair drops a
    real rise per step, and from this angle a real hole is simply black -- the
    first version was a dark rectangle with a fence round it. Cheating the
    depth is what lets the flight be seen at all."""
    width, depth = 0.96, 2.00
    blk("well", (width, depth, 0.02), (0, 0, -0.30), "coal", bev=0)
    steps = 7
    for i in range(steps):
        y = depth / 2 - 0.15 - i * (depth / steps)
        z = -0.03 - i * 0.035
        blk("step_down_%d" % i, (width - 0.02, depth / steps - 0.02, 0.05), (0, y, z),
            "oak_pale" if i % 2 == 0 else "oak_light", bev=0.006)
    blk("trim_l", (0.12, depth + 0.12, 0.08), (-width / 2 - 0.06, 0, 0.04), "oak")
    blk("trim_r", (0.12, depth + 0.12, 0.08), (width / 2 + 0.06, 0, 0.04), "oak")
    blk("trim_s", (width + 0.24, 0.12, 0.08), (0, -depth / 2 - 0.06, 0.04), "oak")
    rail_h = 0.80
    for y in (-depth / 2 - 0.06, -0.33, 0.33, depth / 2):
        blk("post_r_%.2f" % y, (0.08, 0.08, rail_h), (width / 2 + 0.06, y, rail_h / 2), "oak")
    blk("post_s_l", (0.08, 0.08, rail_h), (-width / 2 - 0.06, -depth / 2 - 0.06, rail_h / 2), "oak")
    blk("rail_r", (0.08, depth + 0.14, 0.08), (width / 2 + 0.06, 0, rail_h), "oak_light")
    blk("rail_s", (width + 0.22, 0.08, 0.08), (0, -depth / 2 - 0.06, rail_h), "oak_light")
    for k in range(6):
        y = -depth / 2 + 0.18 + k * 0.33
        blk("baluster_%d" % k, (0.04, 0.04, rail_h - 0.06), (width / 2 + 0.06, y, rail_h / 2), "oak")
    for k in range(2):
        x = -width / 4 + k * width / 2
        blk("baluster_s_%d" % k, (0.04, 0.04, rail_h - 0.06), (x, -depth / 2 - 0.06, rail_h / 2), "oak")
    return 2.6


def prop_room_door():
    """An open door in its frame, set into a partition wall. The leaf swings
    in at an angle so its face shows -- flat against the wall it was a line,
    and the doorway read as a gate. Only the jambs block; the gap is walkable."""
    blk("jamb_l", (0.14, 0.32, 1.60), (-0.50, 0, 0.80), "oak")
    blk("jamb_r", (0.14, 0.32, 1.60), (0.50, 0, 0.80), "oak")
    blk("lintel", (1.14, 0.34, 0.16), (0, 0, 1.66), "oak_light")
    blk("threshold", (1.00, 0.34, 0.03), (0, 0, 0.015), "oak_pale", bev=0)
    hinge_x, hinge_y = -0.43, -0.14
    swing = math.radians(-58)
    leaf_w = 0.84
    cx = hinge_x + math.cos(swing) * leaf_w / 2
    cy = hinge_y + math.sin(swing) * leaf_w / 2
    blk("leaf", (leaf_w, 0.06, 1.48), (cx, cy, 0.76), "oak_light", rot=(0, 0, swing), bev=0.01)
    for z in (0.34, 1.16):
        blk("leaf_brace_%.2f" % z, (leaf_w - 0.10, 0.07, 0.09), (cx, cy - 0.01, z), "oak",
            rot=(0, 0, swing), bev=0.01)
    blk("ring", (0.08, 0.04, 0.08), (hinge_x + math.cos(swing) * 0.72,
                                       hinge_y + math.sin(swing) * 0.72 - 0.04, 0.78),
        "iron", metal=0.7, bev=0.01)
    blk("plaque", (0.26, 0.04, 0.16), (0, -0.18, 1.46), "brass", metal=0.8)
    return 2.1


def prop_cottage_hearth():
    """A smaller, homelier hearth than the inn's: rough stone, a kettle on the
    hob and a bunch of herbs hanging from the beam to dry."""
    import random
    rng = random.Random(11)
    for row in range(4):
        z = 0.12 + row * 0.24
        x = -0.78 + (0.20 if row % 2 else 0.0)
        while x < 0.78:
            w = 0.36 * (0.8 + rng.random() * 0.4)
            cx = x + w / 2
            if not (abs(cx) < 0.40 and z < 0.75) and abs(cx) < 0.80:
                blk("st_%d_%.2f" % (row, x), (w - 0.03, 0.56, 0.21), (cx, 0.04, z),
                    ("stone", "stone_pale")[rng.randrange(2)], bev=0.03)
            x += w
    blk("back", (0.72, 0.08, 0.70), (0, 0.28, 0.35), "coal", bev=0)
    blk("beam", (1.70, 0.46, 0.14), (0, -0.02, 1.04), "oak")
    sphere("fire", 0.13, (0, 0.0, 0.20), "ember", emit=0.9)
    cyl("log", 0.06, 0.50, (0, 0.02, 0.10), "oak", rot=(0, math.radians(90), 0.3))
    sphere("kettle", 0.15, (0.02, -0.04, 0.46), "iron", rough=0.5)
    blk("kettle_spout", (0.14, 0.05, 0.05), (0.18, -0.04, 0.50), "iron", metal=0.6,
        rot=(0, math.radians(-25), 0))
    for i, x in enumerate((-0.52, -0.36, 0.44)):
        blk("herb_%d" % i, (0.12, 0.10, 0.26), (x, -0.26, 0.86), "leaf", bev=0.04)
        blk("herb_tie_%d" % i, (0.06, 0.06, 0.06), (x, -0.26, 0.99), "straw", bev=0.01)
    cyl("mug", 0.07, 0.14, (0.62, -0.10, 1.18), "clay")
    return 2.1


def prop_spinning_wheel():
    blk("bench", (0.90, 0.24, 0.08), (0, 0, 0.42), "oak_light",
        rot=(0, math.radians(-10), 0))
    for i, (x, y) in enumerate(((-0.36, -0.08), (-0.36, 0.08), (0.36, 0.0))):
        blk("leg_%d" % i, (0.06, 0.06, 0.48), (x, y, 0.22), "oak",
            rot=(0, math.radians(8 if x < 0 else -12), 0))
    blk("upright_a", (0.05, 0.05, 0.62), (0.18, -0.10, 0.74), "oak")
    blk("upright_b", (0.05, 0.05, 0.62), (0.18, 0.10, 0.74), "oak")
    cyl("wheel_rim", 0.34, 0.05, (0.18, 0.0, 0.90), "oak_light", rot=(math.radians(90), 0, 0))
    cyl("wheel_hub", 0.26, 0.055, (0.18, 0.0, 0.90), "cloth_cream",
        rot=(math.radians(90), 0, 0))
    cyl("wheel_centre", 0.06, 0.07, (0.18, 0.0, 0.90), "oak", rot=(math.radians(90), 0, 0))
    for k in range(4):
        a = math.radians(k * 45)
        blk("spoke_%d" % k, (0.60, 0.02, 0.03), (0.18, -0.03, 0.90), "oak",
            rot=(0, a, 0), bev=0)
    blk("distaff", (0.04, 0.04, 0.46), (-0.34, 0.0, 0.70), "oak")
    sphere("wool", 0.12, (-0.34, 0.0, 0.92), "cloth_cream", rough=0.95)
    blk("treadle", (0.36, 0.22, 0.03), (0.10, -0.18, 0.06), "oak")
    turn_all(-30)
    return 1.6


def prop_loom():
    """A four-post floor loom: the warp beam at the back, the cloth beam at the
    front with a finished length wound on it, the warp stretched between them
    under the beater, a shuttle sat in the shed and a bench to sit at.

    The thing that has to read at eighty pixels is the *sheet* -- a pale plane
    of threads lying flat between two dark beams. A loom drawn as a frame with
    nothing in it is a gate. So the warp is laid as one pale slab with the
    individual threads scored into it, which survives the downscale where
    forty separate cylinders would turn to mush."""
    # The frame: four posts and the rails that tie them.
    for i, (x, y) in enumerate(((-0.70, -0.62), (0.70, -0.62), (-0.70, 0.62), (0.70, 0.62))):
        blk("post_%d" % i, (0.11, 0.11, 1.44), (x, y, 0.72), "oak")
        blk("post_cap_%d" % i, (0.15, 0.15, 0.07), (x, y, 1.47), "oak_light")
    for y in (-0.62, 0.62):
        blk("rail_%s" % y, (1.52, 0.08, 0.09), (0, y, 0.30), "oak")
    # The two beams. The cloth beam is at the front, with the woven length
    # already rolled onto it -- that roll is what says a loom and not a frame.
    cyl("warp_beam", 0.13, 1.44, (0, 0.58, 1.02), "oak", rot=(0, math.radians(90), 0), verts=12)
    cyl("cloth_beam", 0.11, 1.44, (0, -0.58, 0.84), "oak", rot=(0, math.radians(90), 0), verts=12)
    # The roll is the loom's whole front face from this camera, so it is linen
    # and not dyed: a blue one that size reads as a blanket over a bed.
    cyl("cloth_roll", 0.12, 1.28, (0, -0.60, 0.84), "cloth_cream", rot=(0, math.radians(90), 0),
        verts=14, rough=0.95)
    # The bed of the loom, and the whole of the read at eighty pixels: a pale
    # plane of threads with a hand's width of finished cloth at the near edge.
    # Weight it the other way and it is a bed with a blanket on it -- what says
    # loom is the threads, so the threads get the room.
    blk("warp", (1.28, 0.94, 0.03), (0, 0.14, 0.955), "cloth_cream", rough=0.95, bev=0,
        rot=(math.radians(-6), 0, 0))
    for i in range(9):
        blk("thread_%d" % i, (0.045, 0.92, 0.05), (-0.56 + i * 0.14, 0.14, 0.972), "oak_pale",
            rough=0.95, bev=0, rot=(math.radians(-6), 0, 0))
    blk("web", (1.26, 0.28, 0.035), (0, -0.46, 0.915), "cloth_blue", rough=0.95, bev=0,
        rot=(math.radians(6), 0, 0))
    # The fell: the line where the last pick was beaten in.
    blk("fell", (1.28, 0.05, 0.055), (0, -0.32, 0.945), "oak", rough=0.9, bev=0)
    # The beater, hung from the top: the comb the weaver pulls towards them.
    blk("beater_bar", (1.40, 0.10, 0.10), (0, 0.06, 1.28), "oak_light")
    blk("beater_frame", (1.34, 0.07, 0.34), (0, 0.06, 1.10), "oak")
    for i in range(7):
        blk("reed_%d" % i, (0.025, 0.05, 0.26), (-0.54 + i * 0.18, 0.06, 1.10), "oak_pale", bev=0)
    for sx in (-1, 1):
        blk("beater_arm", (0.06, 0.06, 0.30), (sx * 0.66, 0.06, 1.34), "oak")
    # The heddles: two shafts on cords, one lifted, which is what opens the shed.
    for i, (y, z) in enumerate(((0.26, 1.18), (0.40, 1.24))):
        blk("shaft_%d" % i, (1.30, 0.05, 0.06), (0, y, z), "oak_light")
        for sx in (-1, 1):
            blk("cord_%d_%d" % (i, sx), (0.02, 0.02, 0.24), (sx * 0.58, y, z + 0.16), "straw", bev=0)
    # The shuttle sat in the shed, a spool of weft on it, and the spare yarn.
    blk("shuttle", (0.34, 0.09, 0.06), (0.24, -0.16, 0.99), "oak_light", bev=0.02,
        rot=(0, 0, math.radians(-4)))
    cyl("shuttle_spool", 0.05, 0.22, (0.24, -0.16, 1.03), "cloth_blue",
        rot=(0, math.radians(90), 0), verts=10, rough=0.95)
    sphere("yarn", 0.13, (-0.86, -0.34, 0.14), "cloth_red", rough=0.95)
    sphere("yarn_b", 0.11, (-0.80, -0.06, 0.12), "cloth_blue", rough=0.95)
    # The treadles underfoot and the bench to sit at.
    for i, x in enumerate((-0.26, 0.26)):
        blk("treadle_%d" % i, (0.22, 0.60, 0.05), (x, -0.06, 0.16), "oak",
            rot=(math.radians(6), 0, 0))
    blk("bench", (1.10, 0.28, 0.07), (0, -1.04, 0.52), "oak_light")
    for i, x in enumerate((-0.44, 0.44)):
        blk("bench_leg_%d" % i, (0.08, 0.08, 0.50), (x, -1.04, 0.26), "oak")
    return 2.6


def prop_writing_desk():
    """An old woman's desk: an open book, a stack of scrolls, a candle and an
    inkwell with a quill in it."""
    blk("top", (1.40, 0.70, 0.08), (0, 0, 0.78), "oak_light")
    blk("apron", (1.30, 0.04, 0.14), (0, -0.32, 0.67), "oak")
    for i, (x, y) in enumerate(((-0.62, -0.28), (0.62, -0.28), (-0.62, 0.28), (0.62, 0.28))):
        blk("leg_%d" % i, (0.08, 0.08, 0.76), (x, y, 0.38), "oak")
    blk("book_l", (0.30, 0.40, 0.04), (-0.16, 0.0, 0.84), "paper", rot=(0, math.radians(-6), 0))
    blk("book_r", (0.30, 0.40, 0.04), (0.14, 0.0, 0.84), "paper", rot=(0, math.radians(6), 0))
    blk("book_spine", (0.06, 0.42, 0.03), (-0.01, 0.0, 0.82), "cloth_red")
    for i in range(3):
        cyl("scroll_%d" % i, 0.05, 0.40, (0.48, 0.10 - i * 0.02, 0.87 + i * 0.07), "paper",
            rot=(0, math.radians(90), math.radians(10 * i)))
    cyl("candle", 0.05, 0.20, (-0.52, 0.18, 0.92), "cloth_cream")
    sphere("flame", 0.05, (-0.52, 0.18, 1.05), "ember", emit=1.6)
    cyl("inkwell", 0.06, 0.08, (0.40, -0.18, 0.86), "coal")
    blk("quill", (0.02, 0.02, 0.30), (0.42, -0.16, 1.00), "paper", rot=(0, math.radians(-20), 0), bev=0)
    blk("chair_seat", (0.46, 0.44, 0.06), (0, -0.62, 0.46), "oak_light")
    for i, (x, y) in enumerate(((-0.18, -0.44), (0.18, -0.44), (-0.18, -0.80), (0.18, -0.80))):
        blk("chair_leg_%d" % i, (0.07, 0.07, 0.44), (x, y, 0.22), "oak")
    return 1.9


def prop_cottage_bookshelf():
    blk("back", (1.30, 0.08, 1.70), (0, 0.22, 0.85), "oak")
    blk("side_l", (0.08, 0.46, 1.70), (-0.61, 0.04, 0.85), "oak")
    blk("side_r", (0.08, 0.46, 1.70), (0.61, 0.04, 0.85), "oak")
    blk("crown", (1.40, 0.52, 0.08), (0, 0.04, 1.72), "oak_light")
    tones = ("cloth_red", "cloth_blue", "leaf", "paper", "leather", "straw")
    import random
    rng = random.Random(5)
    for row, z in enumerate((0.10, 0.52, 0.94, 1.36)):
        blk("shelf_%d" % row, (1.18, 0.42, 0.05), (0, 0.04, z), "oak_light")
        x = -0.54
        n = 0
        while x < 0.46:
            w = 0.08 + rng.random() * 0.06
            h = 0.24 + rng.random() * 0.10
            if row == 3 and n == 4:
                sphere("skull", 0.12, (x + 0.12, 0.0, z + 0.14), "paper")
                x += 0.26
                n += 1
                continue
            lean = math.radians(12) if (row == 1 and n == 5) else 0.0
            blk("book_%d_%d" % (row, n), (w, 0.30, h), (x + w / 2, 0.0, z + h / 2 + 0.03),
                tones[rng.randrange(len(tones))], rot=(0, lean, 0), bev=0.012)
            x += w + 0.015
            n += 1
    return 2.25


def prop_dining_table():
    blk("top", (1.40, 0.80, 0.08), (0, 0, 0.74), "oak_light")
    for i, (x, y) in enumerate(((-0.60, -0.32), (0.60, -0.32), (-0.60, 0.32), (0.60, 0.32))):
        blk("leg_%d" % i, (0.10, 0.10, 0.72), (x, y, 0.36), "oak")
    blk("stretcher", (1.20, 0.06, 0.06), (0, 0, 0.18), "oak")
    cyl("bowl_a", 0.14, 0.07, (-0.34, -0.10, 0.815), "clay")
    cyl("bowl_b", 0.14, 0.07, (0.30, 0.12, 0.815), "clay")
    blk("loaf", (0.30, 0.18, 0.12), (0.00, 0.08, 0.84), "straw", bev=0.06)
    cyl("jug", 0.09, 0.24, (0.48, -0.18, 0.90), "paper", rough=0.5)
    return 1.7


def prop_herb_pots():
    for i, (x, y, r, h, leaves) in enumerate(((-0.28, 0.06, 0.16, 0.22, 3),
                                               (0.04, -0.06, 0.20, 0.28, 4),
                                               (0.34, 0.08, 0.14, 0.20, 3))):
        cyl("pot_%d" % i, r, h, (x, y, h / 2), "clay")
        cyl("pot_rim_%d" % i, r + 0.02, 0.04, (x, y, h), "clay")
        for k in range(leaves):
            a = k * 2.1
            sphere("leaf_%d_%d" % (i, k), r * 0.62,
                   (x + math.cos(a) * r * 0.45, y + math.sin(a) * r * 0.3, h + 0.12 + 0.05 * (k % 2)),
                   "leaf")
    return 1.15


# -----------------------------------------------------------------------------
#  The Barley and Bell -- outside
#
#  Seen from much higher than the furniture, because that is how the CraftPix
#  buildings it stands among are drawn: roof first, then the front wall. And
#  built to look nothing like them. Those are single-storey plaster cottages
#  with red tile roofs; this is a coaching inn -- a stone ground floor for the
#  taproom, a jettied timber-framed storey of guest rooms overhanging it, a
#  steep dark shingle roof with a dormer, a porch over the door, and a sign.
# -----------------------------------------------------------------------------

BUILDING_ELEVATION = 46.0


def window(name, x, y, z, w=0.36, h=0.46, lit=True, shutters=True):
    blk(name + "_frame", (w + 0.10, 0.06, h + 0.10), (x, y, z), "oak")
    blk(name + "_glass", (w, 0.04, h), (x, y - 0.03, z), "glass_lit" if lit else "glass",
        emit=0.55 if lit else 0.0, rough=0.3, bev=0)
    blk(name + "_mullion", (0.04, 0.05, h), (x, y - 0.05, z), "oak", bev=0)
    blk(name + "_transom", (w, 0.05, 0.04), (x, y - 0.05, z + h * 0.1), "oak", bev=0)
    blk(name + "_sill", (w + 0.16, 0.14, 0.05), (x, y - 0.06, z - h / 2 - 0.05), "stone_pale")
    if shutters:
        for side in (-1, 1):
            blk(name + "_shutter_%d" % side, (0.12, 0.05, h + 0.04),
                (x + side * (w / 2 + 0.12), y - 0.02, z), "cloth_green" if False else "leaf",
                bev=0.01)


def gable_roof(name, width, depth, eave_z, pitch_deg, colour, thick=0.12, overhang=0.22):
    """Two slanted slabs meeting at a ridge along X, with a ridge cap."""
    pitch = math.radians(pitch_deg)
    half = depth / 2 + overhang
    slab = half / math.cos(pitch)
    rise = half * math.tan(pitch)
    for side in (-1, 1):
        cy = side * half / 2
        cz = eave_z + rise / 2
        blk("%s_slab_%d" % (name, side), (width + overhang * 2, slab, thick), (0, cy, cz),
            colour, rot=(-side * pitch, 0, 0), bev=0.02)
    blk(name + "_ridge", (width + overhang * 2 + 0.04, 0.16, 0.12), (0, 0, eave_z + rise + 0.02),
        "shingle_dk", bev=0.02)
    return rise


def prop_inn_building():
    W, D = 3.60, 2.40          # ground floor footprint
    G = 1.30                   # ground floor height
    U = 1.15                   # upper storey height
    JET = 0.18                 # how far the upper storey overhangs the front

    # --- ground floor: coursed stone ------------------------------------------
    import random
    rng = random.Random(21)
    course = 0.26
    rows = int(G / course)
    for row in range(rows):
        z = course / 2 + row * course
        x = -W / 2 + (0.22 if row % 2 else 0.0)
        while x < W / 2 - 0.05:
            w = 0.46 * (0.75 + rng.random() * 0.5)
            w = min(w, W / 2 - x)
            cx = x + w / 2
            in_door = abs(cx) < 0.44 and z < 1.02
            if not in_door:
                blk("gstone_%d_%.2f" % (row, x), (w - 0.03, 0.10, course - 0.03),
                    (cx, -D / 2, z), ("stone", "stone_pale", "stone")[rng.randrange(3)],
                    bev=0.025)
            x += w
    blk("ground_core", (W - 0.02, D - 0.10, G), (0, 0.04, G / 2), "stone", bev=0)
    blk("plinth", (W + 0.10, D + 0.10, 0.10), (0, 0, 0.05), "stone_pale")

    # The door, with a porch roof over it on two posts.
    blk("door", (0.70, 0.08, 1.00), (0, -D / 2 - 0.01, 0.50), "oak_light")
    blk("door_arch", (0.86, 0.12, 0.14), (0, -D / 2 - 0.02, 1.06), "stone_pale")
    for zz in (0.26, 0.74):
        blk("door_hinge_%.2f" % zz, (0.56, 0.02, 0.05), (-0.04, -D / 2 - 0.06, zz), "iron", metal=0.7)
    blk("door_ring", (0.07, 0.04, 0.07), (0.22, -D / 2 - 0.07, 0.52), "brass", metal=0.8)
    # A shallow canopy on brackets rather than a porch on posts: from this
    # height a porch roof covers the door completely, and the door is the one
    # thing on the building the player needs to find.
    for side in (-1, 1):
        blk("canopy_bracket_%d" % side, (0.06, 0.34, 0.06), (side * 0.50, -D / 2 - 0.16, 1.14),
            "iron", metal=0.7, rot=(math.radians(-30), 0, 0))
    blk("canopy", (1.20, 0.40, 0.06), (0, -D / 2 - 0.22, 1.24), "shingle",
        rot=(math.radians(18), 0, 0), bev=0.02)
    blk("step", (1.00, 0.36, 0.08), (0, -D / 2 - 0.20, 0.04), "stone_pale")

    # Ground floor windows, warm with the taproom behind them.
    window("gwin_l", -1.14, -D / 2 - 0.02, 0.72, w=0.52, h=0.50)
    window("gwin_r", 1.14, -D / 2 - 0.02, 0.72, w=0.52, h=0.50)

    # --- upper storey: jettied timber frame over plaster ----------------------
    uy = -D / 2 - JET            # the front face of the upper storey
    blk("jetty_beam", (W + 0.30, D + JET + 0.10, 0.14), (0, -JET / 2, G + 0.07), "oak")
    blk("upper_core", (W + 0.20, D + JET, U), (0, -JET / 2, G + 0.14 + U / 2), "plaster", bev=0.01)
    zb, zt = G + 0.14, G + 0.14 + U
    # Posts, rails and braces on the front face -- the dark lattice is most of
    # what makes a building read as timber-framed.
    for x in (-W / 2 - 0.08, -1.28, -0.42, 0.42, 1.28, W / 2 + 0.08):
        blk("post_%.2f" % x, (0.10, 0.06, U), (x, uy - 0.02, zb + U / 2), "oak", bev=0.005)
    for z in (zb + 0.05, zb + U * 0.5, zt - 0.05):
        blk("rail_%.2f" % z, (W + 0.30, 0.06, 0.09), (0, uy - 0.02, z), "oak", bev=0.005)
    for i, (x0, x1) in enumerate(((-W / 2 - 0.08, -1.28), (1.28, W / 2 + 0.08))):
        cx = (x0 + x1) / 2
        length = math.hypot(x1 - x0, U * 0.5)
        ang = math.atan2(U * 0.5, x1 - x0) * (1 if i == 0 else -1)
        blk("brace_%d" % i, (length, 0.06, 0.08), (cx, uy - 0.02, zb + U * 0.25), "oak",
            rot=(0, -ang, 0), bev=0.005)
    window("uwin_l", -0.85, uy - 0.04, zb + U * 0.64, w=0.40, h=0.40, shutters=False)
    window("uwin_m", 0.00, uy - 0.04, zb + U * 0.64, w=0.40, h=0.40, shutters=False, lit=False)
    window("uwin_r", 0.85, uy - 0.04, zb + U * 0.64, w=0.40, h=0.40, shutters=False)

    # --- roof -----------------------------------------------------------------
    rise = gable_roof("roof", W + 0.20, D + JET, zt, 48, "shingle", overhang=0.26)
    # Shingle courses, so the roof has a texture and not just a colour.
    pitch = math.radians(48)
    half = (D + JET) / 2 + 0.26
    for k in range(1, 5):
        t = k / 5.0
        yy = -half * (1 - t) - JET / 2
        zz = zt + rise * t
        blk("course_%d" % k, (W + 0.72, 0.05, 0.05), (0, yy - 0.05, zz + 0.07), "shingle_dk",
            rot=(pitch, 0, 0), bev=0)
    # A dormer on the front slope.
    dz = zt + rise * 0.42
    dy = -half * 0.58 - JET / 2
    blk("dormer_face", (0.70, 0.10, 0.62), (0, dy - 0.10, dz + 0.20), "plaster")
    window("dormer_win", 0, dy - 0.16, dz + 0.20, w=0.32, h=0.30, shutters=False)
    for side in (-1, 1):
        blk("dormer_roof_%d" % side, (0.50, 0.66, 0.06), (side * 0.22, dy + 0.10, dz + 0.62),
            "shingle_dk", rot=(0, side * math.radians(40), 0), bev=0.01)

    # --- chimney --------------------------------------------------------------
    # On the front slope and rising from inside the roof. On the back slope,
    # which this camera never sees, its base is hidden and it floats.
    chim_z0 = zt + rise * 0.25
    chim_h = rise * 0.95 + 0.30
    blk("chimney", (0.50, 0.46, chim_h), (W / 2 - 0.50, -0.30, chim_z0 + chim_h / 2), "stone")
    blk("chimney_cap", (0.62, 0.58, 0.10), (W / 2 - 0.50, -0.30, chim_z0 + chim_h + 0.05),
        "stone_pale")
    blk("chimney_pot", (0.20, 0.20, 0.18), (W / 2 - 0.50, -0.30, chim_z0 + chim_h + 0.19), "clay")

    # --- the sign: a sheaf of barley and a bell ----------------------------------
    sx = -W / 2 - 0.12
    blk("sign_bracket", (0.80, 0.08, 0.08), (sx - 0.30, uy - 0.10, zt - 0.14), "iron", metal=0.7)
    blk("sign_stay", (0.60, 0.05, 0.05), (sx - 0.18, uy - 0.10, zt - 0.34), "iron", metal=0.7,
        rot=(0, math.radians(-35), 0))
    blk("sign_board", (0.62, 0.06, 0.46), (sx - 0.46, uy - 0.10, zt - 0.48), "cloth_red")
    blk("sign_edge", (0.68, 0.05, 0.52), (sx - 0.46, uy - 0.07, zt - 0.48), "oak_light")
    for k in range(3):
        blk("sheaf_%d" % k, (0.05, 0.03, 0.30), (sx - 0.52 + k * 0.06, uy - 0.14, zt - 0.46),
            "straw", rot=(0, math.radians((k - 1) * 14), 0), bev=0)
    sphere("bell", 0.08, (sx - 0.34, uy - 0.14, zt - 0.62), "brass", rough=0.35)

    # --- life at the door ---------------------------------------------------------
    cyl("barrel_a", 0.22, 0.52, (-1.10, -D / 2 - 0.44, 0.26), "oak")
    cyl("barrel_a_hoop", 0.23, 0.05, (-1.10, -D / 2 - 0.44, 0.40), "iron", metal=0.7)
    cyl("barrel_b", 0.20, 0.46, (-1.52, -D / 2 - 0.30, 0.23), "oak")
    cyl("barrel_b_hoop", 0.21, 0.05, (-1.52, -D / 2 - 0.30, 0.34), "iron", metal=0.7)
    blk("bench", (0.90, 0.28, 0.08), (1.20, -D / 2 - 0.36, 0.40), "oak_light")
    for side in (-1, 1):
        blk("bench_leg_%d" % side, (0.08, 0.24, 0.40), (1.20 + side * 0.36, -D / 2 - 0.36, 0.20), "oak")
    blk("lantern", (0.14, 0.14, 0.20), (0.62, -D / 2 - 0.08, 1.10), "glass_lit", emit=1.0, bev=0.02)
    blk("lantern_cap", (0.18, 0.18, 0.05), (0.62, -D / 2 - 0.08, 1.23), "iron", metal=0.7)

    return (5.6, BUILDING_ELEVATION)


def prop_mossvale_lodge():
    """Mossvale's longhouse: a log cabin under a mossed bark-shingle roof.

    Deliberately unlike anything in Havenbrook. The cottages there are
    red-tiled and plastered and the inn is stone under timber framing; this is
    round logs stacked with their ends crossing at the corners, a roof the
    forest has half reclaimed, a fieldstone chimney, and antlers over the door.

    The first pass read as a lawn on a box: a green roof covering most of the
    image with dots on it, and the walls a thin strip underneath. So the walls
    are taller, the logs alternate in tone so the courses read, and the roof is
    dark shingle with moss lying on it in patches rather than being moss.
    """
    import random
    rng = random.Random(7)
    W, D = 3.40, 2.00          # footprint
    H = 1.72                   # wall height to the eaves
    R = 0.10                   # log radius -- chunky, or the courses vanish at 192px
    front = -D / 2
    base = 0.12

    blk("plinth", (W + 0.20, D + 0.20, 0.12), (0, 0, 0.06), "stone_pale")
    blk("core", (W - 0.06, D - 0.06, H), (0, 0, base + H / 2), "log_dk", bev=0)

    # Courses of logs on every face, with the ends crossing past the corners.
    courses = int(H / (R * 2))
    tones = ("log", "oak_light", "log", "log_dk")
    for k in range(courses):
        z = base + R + k * R * 2
        shade = tones[k % len(tones)]
        for yface in (front, D / 2):
            if yface == front and z < 1.20:
                for side in (-1, 1):
                    length = W / 2 - 0.48 + 0.16
                    cx = side * (0.48 + length / 2)
                    cyl("log_f_%d_%d" % (k, side), R, length, (cx, yface, z), shade,
                        rot=(0, math.radians(90), 0), verts=12)
            else:
                cyl("log_%d_%.1f" % (k, yface), R, W + 0.32, (0, yface, z), shade,
                    rot=(0, math.radians(90), 0), verts=12)
        for xface in (-W / 2, W / 2):
            cyl("side_%d_%.1f" % (k, xface), R, D + 0.32, (xface, 0, z + R), shade,
                rot=(math.radians(90), 0, 0), verts=12)
        for side in (-1, 1):
            cyl("end_%d_%d" % (k, side), R * 0.84, 0.03, (side * (W / 2 + 0.17), front, z),
                "log_end", rot=(0, math.radians(90), 0), verts=12)

    # --- the door ---------------------------------------------------------------
    blk("door", (0.80, 0.08, 1.06), (0, front - 0.07, base + 0.53), "oak_light")
    for i in range(4):
        blk("door_plank_%d" % i, (0.03, 0.02, 1.02), (-0.30 + i * 0.20, front - 0.12, base + 0.53),
            "oak", bev=0)
    for side in (-1, 1):
        blk("door_post_%d" % side, (0.15, 0.15, 1.20), (side * 0.48, front - 0.05, base + 0.60), "log_dk")
    blk("door_lintel", (1.16, 0.17, 0.17), (0, front - 0.05, base + 1.24), "log_dk")
    for zz in (0.34, 0.92):
        blk("hinge_%.2f" % zz, (0.50, 0.02, 0.05), (-0.08, front - 0.13, base + zz), "iron", metal=0.7)
    blk("latch", (0.06, 0.04, 0.10), (0.27, front - 0.14, base + 0.56), "iron", metal=0.7)
    blk("step", (1.14, 0.44, 0.10), (0, front - 0.28, 0.05), "stone_pale")

    # Antlers in the wall space between lintel and eaves: the one detail that
    # says hunters' lodge. Big, pale, and clear of the roof overhang.
    az = base + 1.46
    blk("antler_skull", (0.18, 0.07, 0.14), (0, front - 0.15, az), "antler")
    for side in (-1, 1):
        blk("antler_beam_%d" % side, (0.06, 0.06, 0.34), (side * 0.17, front - 0.15, az + 0.10),
            "antler", rot=(0, side * math.radians(-62), 0), bev=0.01)
        for t, (dx, dz, ang) in enumerate(((0.16, 0.14, 12), (0.28, 0.12, 40))):
            blk("antler_tine_%d_%d" % (side, t), (0.05, 0.05, 0.16),
                (side * (0.17 + dx), front - 0.15, az + dz), "antler",
                rot=(0, side * math.radians(-ang), 0), bev=0.01)

    # --- windows, shuttered, one lit and one dark -------------------------------
    window("win_l", -1.08, front - 0.09, base + 0.86, w=0.48, h=0.46)
    window("win_r", 1.08, front - 0.09, base + 0.86, w=0.48, h=0.46, lit=False)

    # --- the roof: bark shingle, mossed over --------------------------------------
    eave = base + H
    pitch_deg = 46
    rise = gable_roof("roof", W + 0.20, D, eave, pitch_deg, "shingle_dk", thick=0.16,
                      overhang=0.26)
    pitch = math.radians(pitch_deg)
    half = D / 2 + 0.26
    # Course lines across the front slope, so it reads as shingles and as a slope.
    for k in range(1, 6):
        t = k / 6.0
        blk("course_%d" % k, (W + 0.70, 0.05, 0.05),
            (0, -half * (1 - t) - 0.05, eave + rise * t + 0.08), "log_dk",
            rot=(pitch, 0, 0), bev=0)
    # Moss lying on the slope in irregular patches, thickest low down where the
    # water runs off, with a few fern tufts standing out of it.
    # Small and many: large patches read as green cards laid on the roof.
    for i in range(80):
        t = rng.random() ** 2.2 * 0.80 + 0.03
        x = (rng.random() - 0.5) * (W + 0.30)
        y = -half * (1 - t)
        z = eave + rise * t + 0.10
        w = 0.08 + rng.random() * 0.18
        d = 0.07 + rng.random() * 0.12
        colour = ("moss", "moss_dk", "moss_dk", "moss", "moss_lt")[rng.randrange(5)]
        blk("moss_%d" % i, (w, d, 0.05), (x, y - 0.02, z), colour,
            rot=(pitch, 0, math.radians(rng.uniform(-20, 20))), bev=0.02)
    for i in range(7):
        t = 0.15 + rng.random() * 0.55
        x = (rng.random() - 0.5) * (W - 0.20)
        sphere("fern_%d" % i, 0.07, (x, -half * (1 - t) - 0.04, eave + rise * t + 0.16), "leaf")
    # Bargeboards: dark log edges down the gable ends, so the roof has an outline.
    for side in (-1, 1):
        for face in (-1, 1):
            blk("barge_%d_%d" % (side, face), (0.11, half / math.cos(pitch) + 0.06, 0.13),
                (side * (W / 2 + 0.31), face * half / 2, eave + rise / 2 + 0.06),
                "log_dk", rot=(-face * pitch, 0, 0), bev=0.01)

    # --- a fieldstone chimney, on the front slope so it does not float ----------
    cx, cy0 = W / 2 - 0.66, -0.30
    z = eave + rise * 0.22
    top = eave + rise + 0.44
    k = 0
    while z < top:
        for j in range(2):
            blk("chim_%d_%d" % (k, j), (0.27 + rng.random() * 0.05, 0.48, 0.15),
                (cx - 0.14 + j * 0.28, cy0, z), ("stone", "stone_pale")[(k + j) % 2], bev=0.03)
        z += 0.16
        k += 1
    blk("chim_cap", (0.66, 0.58, 0.08), (cx, cy0, top + 0.04), "stone_pale")

    # --- life around it -----------------------------------------------------------
    for row in range(3):
        for col in range(4 - row):
            x = -1.64 + col * 0.21 + row * 0.105
            cyl("wood_%d_%d" % (row, col), 0.095, 0.44, (x, front - 0.32, 0.10 + row * 0.18),
                "log", rot=(math.radians(90), 0, 0), verts=10)
            cyl("wood_end_%d_%d" % (row, col), 0.08, 0.02, (x, front - 0.55, 0.10 + row * 0.18),
                "log_end", rot=(math.radians(90), 0, 0), verts=10)
    blk("lantern", (0.15, 0.15, 0.21), (0.66, front - 0.13, base + 1.02), "glass_lit", emit=1.1, bev=0.02)
    blk("lantern_cap", (0.19, 0.19, 0.05), (0.66, front - 0.13, base + 1.15), "iron", metal=0.7)
    cyl("stump", 0.22, 0.34, (1.46, front - 0.46, 0.17), "log", verts=16)
    cyl("stump_top", 0.20, 0.02, (1.46, front - 0.46, 0.35), "log_end", verts=16)
    blk("axe_haft", (0.05, 0.05, 0.42), (1.42, front - 0.46, 0.54), "oak_light",
        rot=(0, math.radians(24), 0), bev=0.01)
    blk("axe_head", (0.18, 0.05, 0.12), (1.49, front - 0.46, 0.40), "iron_light", metal=0.6)

    return (5.6, BUILDING_ELEVATION)


def prop_herbalist_cottage():
    """Oona's cottage in Mossvale: whitewashed cob under a deep thatch.

    Built for the herbalist rather than borrowed. building_house_b, which was
    the only other cottage in the art, turned out to be a mis-cut piece of a
    sprite sheet -- an awning, some crates and a roof with no walls. Thatch
    and whitewash keep it apart from both the log lodge next door and
    Havenbrook's red tiles, and the herbs are what say who lives there.

    The first thatch was clean bands of bright straw and read as planks; this
    one is broken into uneven lengths in three tones with a rolled eave.
    """
    import random
    rng = random.Random(19)
    W, D, H = 2.80, 1.80, 1.45
    front = -D / 2
    base = 0.12

    blk("plinth", (W + 0.16, D + 0.16, 0.12), (0, 0, 0.06), "stone")
    blk("walls", (W, D, H), (0, 0, base + H / 2), "plaster", bev=0.06)
    for side in (-1, 1):
        blk("corner_%d" % side, (0.12, 0.12, H), (side * (W / 2 - 0.02), front - 0.02, base + H / 2), "log_dk")
    blk("sill_beam", (W + 0.04, 0.10, 0.10), (0, front - 0.03, base + 0.05), "log_dk")
    blk("head_beam", (W + 0.04, 0.10, 0.10), (0, front - 0.03, base + H - 0.06), "log_dk")

    # A green plank door under a lintel.
    blk("door", (0.66, 0.08, 0.96), (0, front - 0.05, base + 0.48), "leaf")
    for i in range(3):
        blk("door_plank_%d" % i, (0.02, 0.02, 0.92), (-0.17 + i * 0.17, front - 0.10, base + 0.48),
            "moss_dk", bev=0)
    blk("door_lintel", (0.86, 0.12, 0.12), (0, front - 0.06, base + 1.02), "log_dk")
    for side in (-1, 1):
        blk("door_post_%d" % side, (0.09, 0.10, 1.00), (side * 0.38, front - 0.05, base + 0.50), "log_dk")
    sphere("door_knob", 0.045, (0.20, front - 0.12, base + 0.48), "brass")
    blk("step", (0.92, 0.36, 0.08), (0, front - 0.24, 0.04), "stone_pale")

    for side in (-1, 1):
        x = side * 0.92
        window("win_%d" % side, x, front - 0.05, base + 0.78, w=0.42, h=0.38, lit=(side < 0), shutters=False)
        blk("box_%d" % side, (0.54, 0.18, 0.12), (x, front - 0.14, base + 0.50), "log")
        for k in range(4):
            sphere("flower_%d_%d" % (side, k), 0.065, (x - 0.19 + k * 0.125, front - 0.16, base + 0.60),
                   ("cloth_red", "cloth_cream", "brass", "cloth_red")[k])

    # --- the thatch ------------------------------------------------------------
    eave = base + H
    pitch = math.radians(48)
    half = D / 2 + 0.34
    slab = half / math.cos(pitch)
    rise = half * math.tan(pitch)
    for side in (-1, 1):
        blk("thatch_%d" % side, (W + 0.66, slab, 0.24), (0, side * half / 2, eave + rise / 2),
            "thatch", rot=(-side * pitch, 0, 0), bev=0.08)
    # Broken courses: each row is several uneven lengths, in three tones.
    rows = 7
    for k in range(rows):
        t = (k + 0.5) / rows
        y = -half * (1 - t) - 0.11
        z = eave + rise * t + 0.13
        x = -W / 2 - 0.34
        j = 0
        while x < W / 2 + 0.34:
            length = min(0.30 + rng.random() * 0.55, W / 2 + 0.34 - x)
            tone = ("thatch_dk", "thatch_lt", "thatch", "thatch_dk")[rng.randrange(4)]
            blk("course_%d_%d" % (k, j), (length - 0.03, 0.12, 0.09),
                (x + length / 2, y + rng.uniform(-0.02, 0.02), z + rng.uniform(-0.02, 0.02)),
                tone, rot=(pitch, 0, 0), bev=0.03)
            x += length
            j += 1
    # A rolled eave and a rolled ridge, which is what makes thatch look thick.
    cyl("eave_roll", 0.13, W + 0.70, (0, -half + 0.02, eave - 0.02), "thatch_dk",
        rot=(0, math.radians(90), 0), verts=16)
    cyl("ridge", 0.17, W + 0.72, (0, 0, eave + rise + 0.10), "thatch_dk",
        rot=(0, math.radians(90), 0), verts=16)
    for i in range(24):
        x = -W / 2 - 0.30 + i * (W + 0.60) / 23
        blk("fringe_%d" % i, (0.09, 0.10, 0.09 + rng.random() * 0.07),
            (x, -half - 0.06, eave - 0.14), ("thatch_dk", "thatch")[i % 2], bev=0.02)

    # Herbs drying in bunches under the eave.
    for i, x in enumerate((-1.30, -0.62, 0.58, 1.26)):
        blk("herb_string_%d" % i, (0.02, 0.02, 0.12), (x, front - 0.24, eave - 0.18), "straw", bev=0)
        sphere("herb_%d" % i, 0.085, (x, front - 0.24, eave - 0.32), ("leaf", "moss_lt", "wool_green", "leaf")[i])

    cx = W / 2 - 0.55
    cbase = eave + rise * 0.30
    blk("chimney", (0.34, 0.34, rise * 0.90), (cx, -0.20, cbase + rise * 0.45), "stone")
    blk("chimney_cap", (0.44, 0.44, 0.08), (cx, -0.20, cbase + rise * 0.90 + 0.04), "stone_pale")

    # Pots by the door. There was a wattled herb bed out in front as well, a
    # yard from the wall -- which put most of it below the bottom of the
    # picture, so what reached the game was the back corner of a fence and two
    # rows of cabbages, cut off square, lying on the grass by the cottage. A
    # building's picture ends at its doorstep; a garden is the map's to plant.
    cyl("pot_a", 0.12, 0.18, (1.15, front - 0.40, 0.09), "clay", verts=12)
    sphere("pot_a_herb", 0.12, (1.15, front - 0.40, 0.24), "leaf")
    cyl("pot_b", 0.10, 0.14, (1.42, front - 0.30, 0.07), "clay", verts=12)
    sphere("pot_b_herb", 0.10, (1.42, front - 0.30, 0.19), "moss_lt")

    return (4.8, BUILDING_ELEVATION)


BUILDING_PROPS = {
    "inn_building":   (prop_inn_building, 192),
    "mossvale_lodge": (prop_mossvale_lodge, 192),
    "herbalist_cottage": (prop_herbalist_cottage, 168),
}


ROOM_PROPS = {
    "bed_single":        (prop_bed_single,        64),
    "bed_double":        (prop_bed_double,        72),
    "wardrobe":          (prop_wardrobe,          64),
    "nightstand":        (prop_nightstand,        32),
    "washstand":         (prop_washstand,         40),
    "travel_chest":      (prop_travel_chest,      40),
    "stairwell_down":    (prop_stairwell_down,    88),
    "room_door":         (prop_room_door,         56),
    "cottage_hearth":    (prop_cottage_hearth,    80),
    "spinning_wheel":    (prop_spinning_wheel,    56),
    "loom":              (prop_loom,              80),
    "writing_desk":      (prop_writing_desk,      64),
    "cottage_bookshelf": (prop_cottage_bookshelf, 64),
    "dining_table":      (prop_dining_table,      56),
    "herb_pots":         (prop_herb_pots,         36),
}


INN_PROPS = {
    "bar_counter":   (prop_bar_counter,   112),
    "bar_stool":     (prop_bar_stool,      32),
    "keg_rack":      (prop_keg_rack,       72),
    "bottle_shelf":  (prop_bottle_shelf,   72),
    "tavern_table":  (prop_tavern_table,   56),
    "tavern_bench":  (prop_tavern_bench,   56),
    "tavern_chair":  (prop_tavern_chair,   40),
    "inn_fireplace": (prop_inn_fireplace,  104),
    "stairs_up":     (prop_stairs_up,      88),
    "crates_sacks":  (prop_crates_sacks,   48),
    "chalk_board":   (prop_chalk_board,    44),
    "workbench":     (prop_workbench,      80),
    "inn_rug":       (prop_inn_rug,        88),
}


FORGE_PROPS = {
    "forge":          (prop_forge,          96),
    "anvil":          (prop_anvil,          48),
    "bellows":        (prop_bellows,        56),
    "quench_trough":  (prop_quench_trough,  56),
    "grindstone":     (prop_grindstone,     48),
    "tool_rack":      (prop_tool_rack,      64),
    "coal_bin":       (prop_coal_bin,       44),
    "ingot_crate":    (prop_ingot_crate,    44),
    "armour_stand":   (prop_armour_stand,   64),
    "weapon_barrel":  (prop_weapon_barrel,  48),
    "shop_counter":   (prop_shop_counter,   96),
}


# --- the woodland set: Mossvale, Fernhollow and the Whisperwood ---------------

def cone(name, radius, depth, loc, colour, rot=(0, 0, 0), verts=12, rough=0.8, emit=0.0):
    """A cone along Z, point up. Sharpened stakes, tent poles, and the crystals
    that grow out of an azuryte seam -- which are lit from inside, hence `emit`."""
    bpy.ops.mesh.primitive_cone_add(radius1=radius, radius2=0.0, depth=depth,
                                    location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(material(name, colour, rough, 0.0, emit))
    return ob


def prop_well():
    """The village well: a ring of fieldstone, a little shingle roof on two
    log posts, and a bucket on a rope. The dark water inside is what makes it
    a well rather than a planter, so the rim is kept low enough to see into."""
    import random
    rng = random.Random(3)
    R = 0.50
    n = 14
    for course in range(2):
        for i in range(n):
            a = (i + course * 0.5) / n * math.tau
            x, y = math.cos(a) * R, math.sin(a) * R
            blk("ring_%d_%d" % (course, i), (0.24, 0.20, 0.22), (x, y, 0.11 + course * 0.22),
                ("stone", "stone_pale")[(i + course) % 2 if rng.random() > 0.3 else 0],
                rot=(0, 0, a + math.pi / 2), bev=0.03)
    cyl("water", R - 0.06, 0.04, (0, 0, 0.36), "water", verts=24, rough=0.2)
    cyl("water_dark", R - 0.04, 0.30, (0, 0, 0.20), "coal", verts=24)
    for side in (-1, 1):
        blk("post_%d" % side, (0.12, 0.12, 1.30), (side * (R + 0.06), 0, 0.65), "log")
    cyl("crank", 0.04, 1.26, (0, 0, 1.00), "oak", rot=(0, math.radians(90), 0), verts=10)
    blk("handle", (0.05, 0.05, 0.22), (R + 0.18, -0.08, 0.92), "oak_light", bev=0.01)
    blk("rope", (0.02, 0.02, 0.40), (0, 0, 0.78), "straw", bev=0)
    cyl("bucket", 0.10, 0.14, (0, 0, 0.56), "oak_light", verts=12)
    cyl("bucket_hoop", 0.105, 0.03, (0, 0, 0.60), "iron", verts=12, metal=0.7)
    gable_roof("roof", 1.22, 0.90, 1.30, 38, "shingle", thick=0.08, overhang=0.12)
    return 2.3


def prop_well_dry():
    """The same well eleven years after it stopped: no water in it, the bucket
    stood on the rim where somebody left it, and a board nailed across the
    mouth. The dark shaft is the whole point -- from above it should read as a
    hole rather than as a pool."""
    import random
    rng = random.Random(3)
    R = 0.50
    n = 14
    for course in range(2):
        for i in range(n):
            a = (i + course * 0.5) / n * math.tau
            x, y = math.cos(a) * R, math.sin(a) * R
            blk("ring_%d_%d" % (course, i), (0.24, 0.20, 0.22), (x, y, 0.11 + course * 0.22),
                ("stone", "stone_pale")[(i + course) % 2 if rng.random() > 0.3 else 0],
                rot=(0, 0, a + math.pi / 2), bev=0.03)
    # No water disc: just the shaft, sunk deeper so no floor catches the light.
    cyl("shaft", R - 0.04, 0.60, (0, 0, 0.05), "coal", verts=24, rough=1.0)
    for side in (-1, 1):
        blk("post_%d" % side, (0.12, 0.12, 1.30), (side * (R + 0.06), 0, 0.65), "log")
    cyl("crank", 0.04, 1.26, (0, 0, 1.00), "oak", rot=(0, math.radians(90), 0), verts=10)
    blk("handle", (0.05, 0.05, 0.22), (R + 0.18, -0.08, 0.92), "oak_light", bev=0.01)
    # The board nailed over the mouth, and the rope hanging slack off the crank.
    blk("board", (1.16, 0.16, 0.04), (0, 0.10, 0.46), "oak", rot=(0, 0, math.radians(-7)))
    blk("rope", (0.02, 0.02, 0.30), (0.10, -0.06, 0.84), "straw", bev=0)
    # The bucket is up on the rim, dry and tipped, not down the hole.
    cyl("bucket", 0.10, 0.14, (-0.30, -0.30, 0.52), "oak_light", verts=12,
        rot=(math.radians(22), 0, 0))
    cyl("bucket_hoop", 0.105, 0.03, (-0.30, -0.30, 0.56), "iron", verts=12, metal=0.7,
        rot=(math.radians(22), 0, 0))
    gable_roof("roof", 1.22, 0.90, 1.30, 38, "shingle", thick=0.08, overhang=0.12)
    return 2.3


def prop_coach():
    """A carrier's waggon drawn up at its post: a plank bed on four spoked
    wheels, a canvas tilt arched over hoops, a driver's bench and a shaft at
    the front, a lantern, and the board that says where it goes.

    What has to read at ninety-six pixels is *waggon*: a pale arch over a dark
    box, on wheels. The wheels are the part a smaller render loses first, so
    they are big, dark-rimmed and stand proud of the bed rather than under it."""
    # The bed, and the rails round it.
    blk("bed", (2.00, 0.96, 0.16), (0, 0, 0.62), "oak")
    blk("bed_side_l", (2.00, 0.06, 0.30), (0, -0.48, 0.80), "oak_light")
    blk("bed_side_r", (2.00, 0.06, 0.30), (0, 0.48, 0.80), "oak_light")
    blk("tailboard", (0.06, 0.96, 0.30), (-1.00, 0, 0.80), "oak_light")
    # The tilt: canvas over hoops. A cylinder along the waggon, sunk to its
    # middle in the bed, is an arch; the hoops are what stop it being a barrel.
    cyl("tilt", 0.50, 1.62, (-0.10, 0, 0.98), "cloth_cream", rot=(0, math.radians(90), 0), verts=20,
        rough=0.95)
    for i, x in enumerate((-0.88, -0.36, 0.16, 0.68)):
        cyl("hoop_%d" % i, 0.515, 0.05, (x, 0, 0.98), "oak", rot=(0, math.radians(90), 0), verts=20)
    blk("tilt_mouth", (0.04, 0.70, 0.46), (0.72, 0, 1.06), "soot", bev=0)
    # The bench and the footboard.
    blk("bench", (0.30, 0.90, 0.07), (0.92, 0, 1.02), "oak_light")
    blk("bench_back", (0.05, 0.90, 0.26), (0.79, 0, 1.16), "oak")
    blk("footboard", (0.26, 0.90, 0.06), (1.12, 0, 0.76), "oak")
    # Four wheels, the back pair bigger, each a dark rim, a pale felloe inside
    # it, spokes and a hub.
    for x, rad in ((-0.62, 0.40), (0.66, 0.33)):
        for y in (-0.56, 0.56):
            tag = "%s_%s" % ("b" if x < 0 else "f", "l" if y < 0 else "r")
            cyl("rim_" + tag, rad, 0.09, (x, y, rad), "soot", rot=(math.radians(90), 0, 0), verts=20)
            cyl("felloe_" + tag, rad - 0.06, 0.10, (x, y, rad), "oak_pale", rot=(math.radians(90), 0, 0), verts=20)
            for k in range(4):
                blk("spoke_%s_%d" % (tag, k), (rad * 1.7, 0.11, 0.045), (x, y, rad), "oak",
                    rot=(0, math.radians(k * 45), 0), bev=0)
            cyl("hub_" + tag, 0.08, 0.14, (x, y, rad), "iron", rot=(math.radians(90), 0, 0), verts=12, metal=0.6)
    blk("axle_b", (0.08, 1.12, 0.08), (-0.62, 0, 0.40), "iron", metal=0.5)
    blk("axle_f", (0.08, 1.12, 0.08), (0.66, 0, 0.33), "iron", metal=0.5)
    # The shaft, down on its prop while the horse is at the trough.
    blk("shaft", (1.20, 0.08, 0.08), (1.62, 0, 0.44), "oak_light", rot=(0, math.radians(14), 0))
    blk("shaft_bar", (0.08, 0.80, 0.08), (2.16, 0, 0.30), "oak")
    # A lantern on an iron crook at the front corner.
    blk("crook", (0.04, 0.04, 0.50), (0.98, -0.44, 1.30), "iron", metal=0.6)
    blk("lantern", (0.13, 0.13, 0.18), (0.98, -0.44, 1.52), "glass_lit", emit=1.4)
    blk("lantern_cap", (0.17, 0.17, 0.04), (0.98, -0.44, 1.63), "iron", metal=0.6)
    # The board: where it goes, on a post of its own beside the tailboard.
    blk("post", (0.09, 0.09, 1.50), (-1.34, -0.40, 0.75), "oak")
    blk("board", (0.06, 0.62, 0.34), (-1.34, -0.40, 1.34), "oak_pale")
    blk("board_band", (0.07, 0.62, 0.06), (-1.34, -0.40, 1.34), "cloth_red", bev=0)
    # A trunk and a sack in the back, which is what a carrier carries.
    blk("trunk", (0.34, 0.30, 0.22), (-0.80, -0.20, 0.82), "leather")
    sphere("sack", 0.16, (-0.78, 0.22, 0.84), "hay_lt", rough=0.95)
    return 3.3


PALETTE.update({
    # The waystones: a cold light, so that one burning in a town square is never
    # mistaken for a lamp or a fire, which are all the warm end of the palette.
    "way_glow":    (0.62, 0.90, 1.00),
    "way_glow_dp": (0.30, 0.60, 0.95),
    "way_carve":   (0.26, 0.27, 0.30),
})


def _waystone(lit):
    """A waystone: a broad tapering slab of dressed stone on a round plinth of
    flags, a ringed eye set into its face, runes cut below the eye, a stone at
    either shoulder and an offering bowl at its foot. Asleep the eye is a dark
    socket and the runes are only cuts; awake the eye holds a cold light, the
    runes burn with it and a mote hangs over the peak.

    The first attempt had the eye as a round head on a neck, which is a lamp
    post: what makes a standing stone is that it is *wide*, wider than a man,
    and that whatever is carved on it is carved into it. So the eye is in the
    face of the slab and the slab is most of the silhouette.

    Built in courses, each a different grey and a hair out of true: one block
    this size is a concrete bollard, and the whole of what says *old* and
    *made* is the joints."""
    greys = ("stone", "stone_pale", "rock", "stone", "stone_pale", "rock")
    # The plinth: a ring of separate flags round a low step.
    for k in range(11):
        a = math.radians(k * 360.0 / 11.0)
        blk("flag_%d" % k, (0.42, 0.32, 0.12), (math.cos(a) * 0.74, math.sin(a) * 0.62, 0.06),
            greys[k % 3], rot=(0, 0, a + math.radians(90)), bev=0.02)
    cyl("step", 0.62, 0.14, (0, 0, 0.19), "stone_pale", verts=14)
    # The slab: six courses, 0.92 wide at the foot and 0.62 at the shoulder.
    z = 0.26
    widths = []
    for i in range(6):
        w = 0.92 - i * 0.06
        d = 0.40 - i * 0.02
        h = 0.25
        blk("course_%d" % i, (w, d, h), (0.014 * (-1) ** i, 0, z + h / 2.0), greys[i],
            rot=(0, 0, math.radians(2.0 * (-1) ** i)), bev=0.03)
        widths.append((w, d, z + h / 2.0))
        z += h
    # A stepped peak, so the top is a point and not a shelf.
    blk("peak_a", (0.46, 0.26, 0.15), (0, 0, z + 0.075), "stone_pale", bev=0.03)
    blk("peak_b", (0.26, 0.20, 0.13), (0, 0, z + 0.215), "rock", bev=0.03)
    peak = z + 0.28
    # The eye, set into the face across the fourth and fifth courses: a raised
    # ring of paler stone, and the socket inside it.
    ew, ed, ez = widths[4]
    eye_z = (widths[3][2] + widths[4][2]) / 2.0 + 0.02
    face_y = -widths[3][1] / 2.0
    cyl("eye_ring", 0.235, 0.07, (0, face_y - 0.01, eye_z), "stone_pale", rot=(math.radians(90), 0, 0), verts=18)
    cyl("eye_ring_in", 0.175, 0.08, (0, face_y - 0.012, eye_z), "rock_dk", rot=(math.radians(90), 0, 0), verts=18)
    if lit:
        # Two discs, the outer deep and the inner pale, both kept low: a single
        # bright one burnt out to a flat white coin.
        cyl("eye", 0.150, 0.09, (0, face_y - 0.016, eye_z), "way_glow_dp", rot=(math.radians(90), 0, 0),
            verts=18, emit=0.95)
        cyl("eye_core", 0.080, 0.10, (0, face_y - 0.020, eye_z), "way_glow", rot=(math.radians(90), 0, 0),
            verts=14, emit=1.15)
        sphere("mote_halo", 0.105, (0, -0.02, peak + 0.26), "way_glow_dp", emit=0.8)
        sphere("mote", 0.060, (0, -0.04, peak + 0.26), "way_glow", emit=1.2)
    else:
        cyl("eye", 0.150, 0.09, (0, face_y - 0.016, eye_z), "void", rot=(math.radians(90), 0, 0), verts=18)
    # Runes on the three courses under the eye: a bar and a tick each.
    for i in range(3):
        w, d, cz = widths[i]
        colour, glow = ("way_glow_dp", 1.1) if lit else ("way_carve", 0.0)
        blk("rune_%d" % i, (0.26, 0.03, 0.05), (0, -d / 2.0 - 0.006, cz + 0.02), colour, emit=glow, bev=0)
        blk("tick_%d" % i, (0.05, 0.03, 0.13), ((-0.11 if i % 2 else 0.11), -d / 2.0 - 0.006, cz), colour,
            emit=glow, bev=0)
    # A stone at either shoulder, half its height and leaning in.
    for sx in (-1, 1):
        blk("warden_lo_%d" % sx, (0.26, 0.24, 0.30), (sx * 0.72, 0.04, 0.27), "rock", bev=0.03,
            rot=(0, math.radians(-5 * sx), math.radians(8 * sx)))
        blk("warden_hi_%d" % sx, (0.20, 0.19, 0.24), (sx * 0.71, 0.04, 0.52), "stone", bev=0.03,
            rot=(0, math.radians(-7 * sx), math.radians(-6 * sx)))
    # The offering bowl, and what has been left in it.
    cyl("bowl", 0.15, 0.09, (0.22, -0.58, 0.17), "rock_dk", verts=12)
    cyl("bowl_in", 0.11, 0.095, (0.22, -0.58, 0.18), "way_glow_dp" if lit else "soot", verts=12,
        emit=0.8 if lit else 0.0)
    # Moss, more of it on a stone nobody has woken.
    for k, (x, y, zz, r) in enumerate(((-0.62, -0.40, 0.13, 0.12), (0.66, 0.30, 0.13, 0.10),
                                        (-0.34, -0.20, 0.34, 0.10), (-0.74, 0.02, 0.62, 0.08),
                                        (0.30, -0.16, 0.62, 0.07))):
        if lit and k >= 2:
            continue
        sphere("moss_%d" % k, r, (x, y, zz), "moss" if k % 2 else "moss_dk", rough=0.95)
    return 3.0


def prop_waystone():
    return _waystone(False)


def prop_waystone_lit():
    return _waystone(True)


def prop_market_stall():
    """A market stall: a counter under a striped awning with baskets of
    produce on it. The stripes are the identifying mark -- without them it is
    a table with a roof."""
    W, D = 1.70, 0.80
    blk("counter", (W, D, 0.10), (0, 0, 0.84), "oak_light")
    blk("counter_front", (W, 0.06, 0.78), (0, -D / 2 + 0.03, 0.42), "oak")
    for i in range(4):
        blk("plank_%d" % i, (0.03, 0.02, 0.74), (-W / 2 + 0.25 + i * 0.40, -D / 2 - 0.01, 0.42),
            "log_dk", bev=0)
    for x in (-W / 2 + 0.05, W / 2 - 0.05):
        for y in (-D / 2 + 0.05, D / 2 - 0.05):
            blk("post_%.1f_%.1f" % (x, y), (0.08, 0.08, 1.80), (x, y, 0.90), "log")
    # The awning: stripes tilted down toward the customer.
    stripes = 7
    for i in range(stripes):
        x = -W / 2 - 0.10 + (i + 0.5) * (W + 0.20) / stripes
        blk("awning_%d" % i, ((W + 0.20) / stripes + 0.005, D + 0.50, 0.05), (x, -0.12, 1.86),
            ("cloth_red", "cloth_cream")[i % 2], rot=(math.radians(-16), 0, 0), bev=0.005)
    for i in range(stripes):
        x = -W / 2 - 0.10 + (i + 0.5) * (W + 0.20) / stripes
        blk("valance_%d" % i, ((W + 0.20) / stripes + 0.005, 0.03, 0.14), (x, -D / 2 - 0.38, 1.70),
            ("cloth_red", "cloth_cream")[i % 2], bev=0)
    # Produce: apples, cabbages and bread, each in its own basket.
    for k, (x, fill) in enumerate(((-0.52, "cloth_red"), (0.0, "leaf"), (0.52, "straw"))):
        cyl("basket_%d" % k, 0.20, 0.14, (x, -0.05, 0.96), "straw", verts=14)
        for j in range(5):
            a = j / 5 * math.tau
            sphere("fruit_%d_%d" % (k, j), 0.07, (x + math.cos(a) * 0.09, -0.05 + math.sin(a) * 0.09,
                                               1.06), fill)
    blk("crate", (0.40, 0.36, 0.34), (W / 2 + 0.30, -0.10, 0.17), "oak")
    return 2.9


def prop_palisade():
    """A length of palisade: sharpened stakes lashed to a rail. Laid end to end
    along the edge of a woodland village, where Havenbrook would have a hedge."""
    n = 6
    step = 0.28
    for i in range(n):
        x = -step * (n - 1) / 2 + i * step
        h = 1.20 + (0.10 if i % 2 else 0.0)
        cyl("stake_%d" % i, 0.13, h, (x, 0, h / 2), ("log", "log_dk")[i % 2], verts=10)
        cone("tip_%d" % i, 0.13, 0.26, (x, 0, h + 0.13), "log_end", verts=10)
    for z in (0.40, 0.92):
        blk("rail_%.2f" % z, (step * n + 0.10, 0.10, 0.10), (0, -0.14, z), "log_dk")
        for i in range(n):
            x = -step * (n - 1) / 2 + i * step
            blk("lash_%d_%.2f" % (i, z), (0.05, 0.16, 0.14), (x, -0.13, z), "straw", bev=0)
    return 2.0


def prop_gate_tower():
    """One tower of a gate in a wall that runs north and south, for a road that
    goes through it east and west: the same stacked logs, cap and shingled roof
    as Havenbrook's gate, with a lamp on the face toward the road.

    A gate seen from the front is one picture -- prop_town_gate -- because
    everything that walks through it is in front of all of it. A gate seen from
    the side cannot be: whoever is on the road is in front of the tower north
    of it and behind the tower south of it, and one picture can only be sorted
    once. So this is one tower, placed twice, and the road between them is
    simply the road."""
    # The tower itself is the front gate's, log for log, so the two kinds of
    # gate are plainly the same carpenter's.
    for k in range(6):
        z = 0.16 + k * 0.30
        for row, y in enumerate((-0.32, 0.0, 0.32)):
            cyl("log_%d_%d" % (k, row), 0.15, 0.84, (0, y, z), ("log", "log_dk")[(k + row) % 2],
                rot=(0, math.radians(90), 0), verts=10)
    blk("cap", (1.00, 1.06, 0.12), (0, 0, 1.98), "log_dk", bev=0.03)
    for rx in (-1, 1):
        blk("shingle_%d" % rx, (0.66, 1.16, 0.09), (rx * 0.27, 0, 2.12), "shingle",
            rot=(0, rx * math.radians(32), 0), bev=0.02)
    blk("ridge", (0.14, 1.20, 0.09), (0, 0, 2.30), "shingle_dk", bev=0.02)
    # A lamp on the face toward the camera, which for the tower north of a road
    # is the face toward the road.
    blk("lamp", (0.17, 0.17, 0.22), (0.30, -0.50, 1.46), "candle_glow", emit=1.5, bev=0.03)
    blk("lamp_cap", (0.22, 0.22, 0.05), (0.30, -0.50, 1.60), "iron", bev=0.02)
    blk("lamp_arm", (0.06, 0.20, 0.06), (0.30, -0.42, 1.60), "iron", bev=0.015)
    # The leaf of the gate, swung back flat against the tower's face.
    for k in range(4):
        cyl("bar_%d" % k, 0.034, 1.10, (-0.34 + k * 0.15, -0.50, 0.58), "log", verts=8)
    for z in (0.28, 0.96):
        blk("leaf_rail_%.2f" % z, (0.62, 0.08, 0.08), (-0.12, -0.52, z), "log_dk", bev=0.02)
    return 3.2


def prop_palisade_side():
    """A cell's length of palisade running north and south, seen along its
    length: three stakes one behind another, and the rail down their side."""
    for i, y in enumerate((0.34, 0.0, -0.34)):
        h = 1.20 + (0.10 if i % 2 else 0.0)
        cyl("stake_%d" % i, 0.13, h, (0, y, h / 2), ("log", "log_dk")[i % 2], verts=10)
        cone("tip_%d" % i, 0.13, 0.26, (0, y, h + 0.13), "log_end", verts=10)
    for z in (0.40, 0.92):
        blk("rail_%.2f" % z, (0.10, 0.98, 0.10), (0.14, 0, z), "log_dk")
    return 2.2


# -----------------------------------------------------------------------------
#  The Reverie goes down. Between one depth of the dream and the next there is
#  a ladder: a hole worn through the cloud with the top of a ladder standing up
#  out of it, and, underneath, the same ladder climbing away into the dark
#  overhead until there is no more of it to see.
# -----------------------------------------------------------------------------

PALETTE.update({
    "cloud_rim":   (0.905, 0.895, 0.960), "cloud_rim_dk": (0.700, 0.670, 0.820),
    "dream_void":  (0.050, 0.030, 0.110), "dream_deep":   (0.160, 0.100, 0.300),
    "ladder_wood": (0.560, 0.430, 0.300), "ladder_dk":    (0.380, 0.280, 0.200),
    "dream_glow":  (0.700, 0.560, 1.000),
})


def _ladder(x, y, z0, z1, lean=0.0, prefix="l", fade_from=None):
    """Two rails and their rungs from z0 up to z1, leaning back by `lean` a
    metre. Above `fade_from` the rungs thin out and darken, so a ladder going
    up ends in nothing rather than in a sawn-off top."""
    h = z1 - z0
    mid = (z0 + z1) / 2.0
    tilt = math.atan2(lean * h, h)
    for sx in (-1, 1):
        blk("dl_%s_rail_%d" % (prefix, sx), (0.07, 0.07, h), (x + sx * 0.23, y + lean * h / 2.0, mid), "ladder_dk",
            rot=(-tilt, 0, 0), bev=0.015)
    n = max(2, int(h / 0.26))
    for k in range(n):
        z = z0 + 0.16 + k * (h - 0.24) / (n - 1)
        if fade_from is not None and z > fade_from and k % 2:
            continue
        tone = "ladder_wood" if fade_from is None or z <= fade_from else "ladder_dk"
        blk("dl_%s_rung_%d_%s" % (prefix, k, tone), (0.46, 0.055, 0.055), (x, y + lean * (z - z0), z), tone, bev=0.012)


def prop_dream_ladder_down():
    """The way deeper into the dream: a hole through the cloud, its rim puffed
    up round it, violet dark underneath, and the top of a ladder standing out
    of it on the far side where you would step onto it."""
    cyl("dl_hole", 0.62, 0.02, (0, 0, 0.012), "dream_void", verts=20)
    cyl("dl_hole_glow", 0.40, 0.02, (0, 0.04, 0.016), "dream_deep", verts=18, emit=0.6)
    # The rim: a ring of cloud, lumpier than a circle.
    for k in range(12):
        a = k / 12.0 * 2.0 * math.pi
        r = 0.72 + (0.05 if k % 3 == 0 else 0.0)
        sphere("dl_rim_%d" % k, 0.19 + (0.04 if k % 2 else 0.0), (math.cos(a) * r, math.sin(a) * r * 0.92, 0.07),
               "cloud_rim" if k % 4 else "cloud_rim_dk")
    # The ladder's top, standing up out of the far side and leaning back.
    _ladder(0.0, 0.34, -0.30, 0.92, lean=0.10, prefix="top")
    # A rung or two seen going down inside the hole.
    for k in range(3):
        blk("dl_in_rung_%d" % k, (0.44, 0.05, 0.05), (0, 0.30 - k * 0.04, -0.10 - k * 0.12), "ladder_dk", bev=0.01)
    return (2.3, 58.0)


def prop_dream_ladder_up():
    """The same ladder from underneath: standing on the cloud and climbing away
    into the dark overhead, where it thins out and is gone, with a wisp of the
    cloud it came through still round it."""
    _ladder(0.0, 0.0, 0.0, 2.70, lean=0.05, prefix="up", fade_from=1.9)
    # A little cloud at its foot, so it stands in something.
    for k, (x, y, r) in enumerate(((-0.30, -0.06, 0.17), (0.30, -0.04, 0.16), (0.0, -0.14, 0.19), (0.0, 0.16, 0.15))):
        sphere("dl_foot_%d" % k, r, (x, y, 0.05), "cloud_rim" if k % 2 else "cloud_rim_dk")
    # And the wisp it disappears into.
    for k, (x, z, r) in enumerate(((-0.26, 2.52, 0.20), (0.24, 2.60, 0.22), (0.0, 2.74, 0.26), (-0.10, 2.40, 0.14))):
        sphere("dl_wisp_%d" % k, r, (x, 0.12, z), "dream_deep", emit=0.35)
    return 3.3


def prop_fence_post():
    """One post of a split-rail fence with the rail ends coming off it north and
    south: what a rail fence looks like end-on, for the sides of a pen."""
    blk("post", (0.14, 0.14, 1.05), (0, 0, 0.52), "log_dk", bev=0.02)
    blk("cap", (0.18, 0.18, 0.07), (0, 0, 1.06), "log", bev=0.02)
    for z in (0.44, 0.78):
        for sy in (-1, 1):
            blk("rail_%d_%.2f" % (sy, z), (0.10, 0.44, 0.09), (0, sy * 0.30, z), "log", bev=0.02)
    return 1.9


def prop_log_pile():
    """Split firewood stacked with its end grain toward the path, and an axe
    leaning on it. Woodcutting country."""
    rows = (4, 3, 2)
    for r, count in enumerate(rows):
        for c in range(count):
            x = (c - (count - 1) / 2) * 0.26
            z = 0.12 + r * 0.22
            cyl("log_%d_%d" % (r, c), 0.12, 0.90, (x, 0, z), ("log", "log_dk")[(r + c) % 2],
                rot=(math.radians(90), 0, 0), verts=12)
            cyl("end_%d_%d" % (r, c), 0.10, 0.02, (x, -0.46, z), "log_end",
                rot=(math.radians(90), 0, 0), verts=12)
    blk("axe_haft", (0.06, 0.06, 0.80), (0.70, -0.10, 0.38), "oak_light",
        rot=(0, math.radians(-18), 0), bev=0.01)
    blk("axe_head", (0.26, 0.06, 0.16), (0.60, -0.10, 0.74), "iron_light", metal=0.6)
    return 2.0


def prop_tanning_rack():
    """A hide laced into a pole frame to dry, leaning back on a prop, with a
    scraping beam beside it. What a tannery is, from a distance."""
    for x in (-0.62, 0.62):
        cyl("post_%s" % x, 0.045, 1.50, (x, 0.10, 0.74), "log_dk", rot=(math.radians(-9), 0, 0), verts=10)
    for z, y in ((0.12, 0.0), (1.42, 0.21)):
        cyl("rail_%s" % z, 0.040, 1.40, (0, y, z), "log", rot=(0, math.radians(90), 0), verts=10)
    blk("hide", (0.98, 0.03, 1.02), (0, 0.10, 0.76), "hide_tan", rot=(math.radians(-9), 0, 0), bev=0.02)
    blk("hide_belly", (0.46, 0.035, 0.62), (0, 0.085, 0.74), "hide_pale", rot=(math.radians(-9), 0, 0), bev=0.02)
    # The lacing: short thongs from the hide's edge out to the frame.
    for i in range(5):
        z = 0.30 + i * 0.23
        for sx in (-1, 1):
            blk("lace_%d_%d" % (i, sx), (0.14, 0.015, 0.015), (sx * 0.55, 0.06 + i * 0.035, z), "straw", bev=0)
    cyl("prop", 0.035, 1.30, (0, 0.62, 0.60), "log_dk", rot=(math.radians(38), 0, 0), verts=8)
    cyl("beam", 0.10, 1.10, (1.10, -0.10, 0.34), "log", rot=(0, math.radians(68), 0), verts=12)
    for sx in (-1, 1):
        cyl("beam_leg_%d" % sx, 0.03, 0.46, (1.40, -0.10 + sx * 0.14, 0.20), "log_dk", verts=8)
    return 2.3


def prop_hay_rick():
    """A rick of hay thatched to a point, a pitchfork stood in its side."""
    cyl("base", 0.74, 0.70, (0, 0, 0.35), "hay", verts=16)
    cone("top", 0.82, 0.86, (0, 0, 1.12), "hay_lt", verts=16)
    cyl("band", 0.76, 0.06, (0, 0, 0.62), "hay_dk", verts=16)
    cyl("cap", 0.07, 0.20, (0, 0, 1.60), "log_dk", verts=8)
    blk("fork_haft", (0.04, 0.04, 1.10), (0.78, -0.30, 0.56), "oak_light", rot=(0, math.radians(-14), 0), bev=0.01)
    for k in (-1, 0, 1):
        blk("tine_%d" % k, (0.02, 0.02, 0.26), (0.66 + k * 0.06, -0.30, 1.20), "iron_light",
            rot=(0, math.radians(-14), 0), bev=0)
    return 1.9


def prop_rail_fence():
    """A length of split-rail fence: three posts and two rails. Laid end to end
    it is a field's edge."""
    for x in (-0.92, 0.0, 0.92):
        blk("post_%s" % x, (0.11, 0.11, 0.78), (x, 0, 0.39), "log_dk", bev=0.02)
    for z in (0.30, 0.60):
        blk("rail_%s" % z, (1.96, 0.06, 0.09), (0, -0.02, z), "log", bev=0.02)
    return 2.0


def prop_bear_den():
    """The Den Mother's: a mouth of dark under a lintel of fallen slabs, banked
    with earth and bracken, claw-raked trunks either side and bones at the
    door. Scenery, not a way in -- she comes out to you."""
    blk("bank", (3.4, 1.8, 1.10), (0, 0.55, 0.55), "earth_dk", bev=0.30)
    blk("bank_top", (2.9, 1.5, 0.40), (0, 0.60, 1.20), "moss", bev=0.18)
    blk("mouth", (1.30, 0.30, 0.92), (0, -0.30, 0.46), "void", bev=0.10, rough=1.0)
    blk("lintel", (2.0, 0.60, 0.34), (0, -0.20, 1.06), "rock", rot=(0, math.radians(4), 0), bev=0.08)
    blk("jamb_l", (0.44, 0.56, 1.00), (-0.86, -0.22, 0.50), "rock_dk", rot=(0, math.radians(-6), 0), bev=0.08)
    blk("jamb_r", (0.48, 0.56, 0.96), (0.88, -0.22, 0.48), "rock", rot=(0, math.radians(7), 0), bev=0.08)
    for i, (x, y, r) in enumerate(((-1.35, 0.1, 0.30), (1.30, 0.2, 0.26), (-0.4, 0.9, 0.34), (0.7, 1.0, 0.28))):
        sphere("fern_%d" % i, r, (x, y, 1.20 + r * 0.4), ("leaf", "moss_lt")[i % 2])
    for sx in (-1, 1):
        cyl("trunk_%d" % sx, 0.17, 1.9, (sx * 1.85, 0.0, 0.95), "log_dk", verts=12)
        for k in range(3):
            blk("rake_%d_%d" % (sx, k), (0.03, 0.02, 0.50), (sx * 1.85 + (k - 1) * 0.07, -0.17, 1.00),
                "log_end", rot=(0, math.radians(sx * 8), 0), bev=0)
    for i, (x, y) in enumerate(((-0.5, -0.80), (0.35, -0.95), (0.0, -0.70))):
        cyl("bone_%d" % i, 0.035, 0.36, (x, y, 0.05), "bone", rot=(math.radians(90), 0, math.radians(30 + i * 50)), verts=8)
    sphere("skull", 0.11, (0.62, -0.78, 0.10), "bone")
    return 4.0


def prop_tent():
    """A canvas tent with its flap tied back, pegged out on guy ropes. The
    dark opening is what makes it a tent and not a roof on the ground.

    A rotation about Y by a positive angle swings a slab's top toward +X, so
    the left side leans right by +lean and the right side by -lean; the first
    render had that backwards and the two sides flared out like an open book.
    """
    W, D, H = 1.70, 1.90, 1.30
    lean = math.atan2(W / 2, H)            # from vertical
    slab = math.hypot(W / 2, H)
    for side in (-1, 1):
        blk("canvas_%d" % side, (0.05, D, slab), (side * W / 4, 0, H / 2), "cloth_cream",
            rot=(0, -side * lean, 0), bev=0.01)
        # A darker seam down the outside, so each side reads as a sheet.
        blk("seam_%d" % side, (0.06, 0.05, slab), (side * W / 4, -D / 2 + 0.04, H / 2), "straw",
            rot=(0, -side * lean, 0), bev=0)
    # The opening: a dark triangle set just inside the front edge, built from
    # thin strips narrowing to the ridge.
    strips = 12
    for k in range(strips):
        t = (k + 0.5) / strips
        w = (W - 0.16) * (1 - t)
        blk("dark_%d" % k, (w, 0.03, H / strips + 0.01), (0, -D / 2 + 0.10, t * H), "coal", bev=0)
    # The flap, tied back against the right-hand side.
    blk("flap", (0.42, 0.04, 0.86), (0.44, -D / 2 - 0.03, 0.46), "cloth_cream",
        rot=(0, -lean * 0.7, 0), bev=0.01)
    blk("flap_tie", (0.08, 0.05, 0.05), (0.52, -D / 2 - 0.06, 0.62), "cloth_red", bev=0)
    blk("ridge_pole", (0.07, D + 0.30, 0.07), (0, 0, H + 0.02), "log_dk")
    for y in (-D / 2 - 0.08, D / 2 + 0.08):
        blk("pole_%.1f" % y, (0.07, 0.07, H + 0.12), (0, y, (H + 0.12) / 2), "log_dk")
    # Guy ropes running out and down to pegs; a positive Y rotation drops the
    # +X end, so the right side takes +angle.
    for side in (-1, 1):
        for y in (-D / 3, D / 3):
            blk("guy_%d_%.1f" % (side, y), (0.66, 0.025, 0.025), (side * (W / 2 + 0.26), y, H * 0.28),
                "straw", rot=(0, side * math.radians(30), 0), bev=0)
            blk("peg_%d_%.1f" % (side, y), (0.06, 0.06, 0.14), (side * (W / 2 + 0.55), y, 0.07), "log")
    blk("bedroll", (0.62, 0.30, 0.16), (-0.66, -D / 2 - 0.42, 0.08), "wool_green")
    blk("bedroll_strap", (0.05, 0.32, 0.17), (-0.66, -D / 2 - 0.42, 0.08), "leather", bev=0)
    return 3.0


def prop_campfire_ring():
    """A cooking fire in a ring of stones, logs crossed over the flames.

    The only fire the game had was a CraftPix sprite five pixels wide, which
    read as a candle standing in the grass. Stones and logs give it a footprint,
    and the flames stay low and orange (emission under 2) so they read as fire
    rather than a yellow disc."""
    import random
    rng = random.Random(11)
    n = 11
    R = 0.40
    for i in range(n):
        a = i / n * math.tau
        blk("stone_%d" % i, (0.20, 0.16, 0.14), (math.cos(a) * R, math.sin(a) * R, 0.07),
            ("stone", "stone_pale")[i % 2], rot=(0, 0, a + rng.uniform(-0.3, 0.3)), bev=0.04)
    cyl("ash", R - 0.10, 0.03, (0, 0, 0.02), "soot", verts=20)
    for k, ang in enumerate((20, 80, 140)):
        cyl("log_%d" % k, 0.06, 0.62, (0, 0, 0.12 + k * 0.03), ("log", "log_dk", "log")[k],
            rot=(math.radians(90), 0, math.radians(ang)), verts=10)
    for k in range(5):
        a = k / 5 * math.tau
        cone("flame_%d" % k, 0.09, 0.30 + rng.random() * 0.14,
             (math.cos(a) * 0.08, math.sin(a) * 0.08, 0.30), "ember", verts=8)
        bpy.context.active_object.data.materials[0] = material("flame_%d" % k, "ember", 0.6, 0.0, 1.6)
    cone("flame_core", 0.10, 0.44, (0, 0, 0.36), "glass_lit", verts=8)
    bpy.context.active_object.data.materials[0] = material("flame_core", "glass_lit", 0.6, 0.0, 1.8)
    for k in range(4):
        a = k / 4 * math.tau + 0.4
        sphere("coal_%d" % k, 0.05, (math.cos(a) * 0.16, math.sin(a) * 0.16, 0.08), "coal_hot", emit=1.2)
    return 1.6


def prop_town_gate():
    """Havenbrook's gate: two stacked-log towers either side of the Sunken
    Road, a lintel across the top with the town's board hung under it, and the
    leaves of the gate standing open flat against the towers.

    The road used to simply stop in a field, and the way into the town was a
    rectangle of grass at the end of it. A gate is what tells you that you have
    arrived somewhere before you walk through it.

    The opening is left clear all the way up to the lintel: anything hung
    across it -- the board, a beam, a bar of the gate -- reads as a closed
    door from above, and the one thing this has to say is "walk through here".
    """
    OPEN = 2.20          # the gap the road runs through
    px_of = lambda sx: sx * (OPEN / 2 + 0.44)

    # The roadway under the gate, so the opening reads as a way through rather
    # than as a gap between two sheds.
    blk("threshold", (OPEN + 0.20, 0.90, 0.06), (0, 0, 0.03), "stone", bev=0.02)
    for k in range(4):
        blk("rut_%d" % k, (OPEN + 0.16, 0.09, 0.03), (0, -0.33 + k * 0.22, 0.07),
            "stone_pale", bev=0.01)

    for sx in (-1, 1):
        px = px_of(sx)
        # A tower of stacked logs laid along the wall, ends facing the road.
        for k in range(6):
            z = 0.16 + k * 0.30
            for row, y in enumerate((-0.32, 0.0, 0.32)):
                cyl("log_%d_%d_%d" % (sx, k, row), 0.15, 0.84,
                    (px, y, z), ("log", "log_dk")[(k + row) % 2],
                    rot=(0, math.radians(90), 0), verts=10)
        blk("cap_%d" % sx, (1.00, 1.06, 0.12), (px, 0, 1.98), "log_dk", bev=0.03)
        # A little shingled roof on each tower, so they read as gatehouses
        # rather than as two stacks of firewood.
        for rx in (-1, 1):
            blk("shingle_%d_%d" % (sx, rx), (0.66, 1.16, 0.09), (px + rx * 0.27, 0, 2.12),
                "shingle", rot=(0, rx * math.radians(32), 0), bev=0.02)
        blk("ridge_%d" % sx, (0.14, 1.20, 0.09), (px, 0, 2.30), "shingle_dk", bev=0.02)
        # The leaf of the gate, swung right back against the outside of its
        # tower and out of the road.
        lx = px + sx * 0.52
        for k in range(5):
            cyl("bar_%d_%d" % (sx, k), 0.036, 1.20, (lx, -0.28 + k * 0.15, 0.62), "log", verts=8)
        for z in (0.30, 1.02):
            blk("leaf_rail_%d_%.2f" % (sx, z), (0.10, 0.76, 0.08), (lx, 0, z), "log_dk", bev=0.02)
        blk("hinge_%d" % sx, (0.20, 0.10, 0.10), (px + sx * 0.28, -0.26, 0.96), "iron", bev=0.02)
        # A lamp on the inner face of each tower, for coming home after dark.
        blk("lamp_%d" % sx, (0.17, 0.17, 0.22), (px - sx * 0.46, -0.34, 1.50),
            "candle_glow", emit=1.5, bev=0.03)
        blk("lamp_cap_%d" % sx, (0.22, 0.22, 0.05), (px - sx * 0.46, -0.34, 1.64), "iron", bev=0.02)
        blk("lamp_arm_%d" % sx, (0.26, 0.06, 0.06), (px - sx * 0.34, -0.34, 1.64), "iron", bev=0.015)

    # The lintel across the top, high enough that the way under it stays open,
    # with the town's board fixed to its face rather than hung in the gap.
    blk("lintel", (OPEN + 1.90, 0.40, 0.30), (0, 0, 1.82), "oak", bev=0.03)
    blk("lintel_trim", (OPEN + 1.90, 0.44, 0.08), (0, 0, 1.64), "log_dk", bev=0.02)
    for sx in (-1, 1):
        blk("brace_%d" % sx, (0.60, 0.18, 0.18), (sx * (OPEN / 2 + 0.06), 0, 1.44),
            "log_dk", rot=(0, sx * math.radians(44), 0), bev=0.02)
    blk("board", (1.24, 0.10, 0.40), (0, -0.24, 1.80), "oak_light", bev=0.02)
    blk("board_edge", (1.30, 0.07, 0.06), (0, -0.26, 1.58), "log_dk", bev=0.015)
    return (4.8, BUILDING_ELEVATION)


WOODLAND_PROPS = {
    "town_gate":    (prop_town_gate,    160),
    "well":         (prop_well,         56),
    "well_dry":     (prop_well_dry,     56),
    "market_stall": (prop_market_stall, 80),
    "waystone":     (prop_waystone,     72),
    "waystone_lit": (prop_waystone_lit, 72),
    "coach":        (prop_coach,        104),
    "palisade":     (prop_palisade,     64),
    "gate_tower":   (prop_gate_tower,   104),
    "fence_post":   (prop_fence_post,   56),
    "dream_ladder_down": (prop_dream_ladder_down, 80),
    "dream_ladder_up":   (prop_dream_ladder_up,   104),
    "palisade_side": (prop_palisade_side, 64),
    "log_pile":     (prop_log_pile,     48),
    "tanning_rack": (prop_tanning_rack, 64),
    "hay_rick":     (prop_hay_rick,     56),
    "rail_fence":   (prop_rail_fence,   64),
    "bear_den":     (prop_bear_den,     144),
    "tent":         (prop_tent,         72),
    "campfire_ring": (prop_campfire_ring, 48),
}


# -----------------------------------------------------------------------------
#  Herbs and plants -- Foraging
#
#  Each plant is drawn twice: growing, with its flowers or caps, and picked, as
#  the cut stubs and a leaf or two left behind while it grows back. They are
#  seen from higher than the furniture, the way the ground scatter they stand
#  among is drawn, so a flower head reads from above. And each has one strong
#  thing it is known by -- orange pompoms, paired mint spears, a trefoil in
#  black water, glowing caps -- because at forty pixels that is all anyone
#  sees.
# -----------------------------------------------------------------------------

HERB_ELEVATION = 52.0
HERB_SPAN = 0.80

PALETTE.update({
    "herb_leaf":     (0.294, 0.478, 0.216),
    "herb_leaf_dk":  (0.192, 0.337, 0.157),
    "herb_leaf_lt":  (0.482, 0.671, 0.290),
    "stub":          (0.557, 0.600, 0.341),
    "flax_stalk":    (0.640, 0.650, 0.340),
    "flax_blue":     (0.420, 0.560, 0.940),
    "flax_eye":      (0.930, 0.900, 0.560),
    "marigold":      (0.973, 0.557, 0.106),
    "marigold_dk":   (0.839, 0.345, 0.078),
    "mint":          (0.380, 0.749, 0.478),
    "mint_dk":       (0.227, 0.537, 0.345),
    "mint_flower":   (0.733, 0.639, 0.906),
    "nettle":        (0.220, 0.431, 0.200),
    "nettle_lt":     (0.286, 0.502, 0.231),
    "bog_water":     (0.227, 0.290, 0.259),
    "bogbean":       (0.451, 0.580, 0.314),
    "bog_flower":    (0.965, 0.914, 0.925),
    "bog_pink":      (0.886, 0.600, 0.667),
    "sage":          (0.557, 0.635, 0.529),
    "sage_dk":       (0.408, 0.482, 0.388),
    "sage_flower":   (0.643, 0.518, 0.812),
    "pebble":        (0.588, 0.576, 0.553),
    "glowcap":       (0.435, 0.788, 0.812),
    "glowcap_dot":   (0.847, 0.976, 0.965),
    "glowcap_stem":  (0.851, 0.824, 0.749),
    "ash":           (0.353, 0.318, 0.298),
    "ember_petal":   (0.925, 0.231, 0.090),
    "ember_core":    (1.000, 0.741, 0.227),
    "ember_stem":    (0.259, 0.157, 0.141),
    "moon_petal":    (0.420, 0.557, 0.953),
    "moon_core":     (0.937, 0.953, 1.000),
    "moon_leaf":     (0.365, 0.431, 0.600),
    "star_petal":    (1.000, 0.973, 0.878),
    "star_core":     (1.000, 0.835, 0.341),
    "star_leaf":     (0.518, 0.749, 0.749),
    "cauldron":      (0.180, 0.180, 0.196),
    "cauldron_lt":   (0.310, 0.314, 0.337),
    "brew":          (0.412, 0.749, 0.337),
})


def leaf(name, length, width, loc, yaw, pitch, colour, thick=0.018):
    """A flattened, pointed leaf: a squashed sphere, stretched along its own X,
    turned out from the stem and tipped up or down."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=loc, segments=12, ring_count=6)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (length / 2.0, width / 2.0, thick)
    ob.rotation_euler = (0.0, math.radians(-pitch), math.radians(yaw))
    ob.data.materials.append(material(name, colour, 0.85))
    return ob


def stem(name, height, loc, colour="herb_leaf_dk", radius=0.014, lean=(0.0, 0.0)):
    x, y, z = loc
    ob = cyl(name, radius, height, (x + lean[0] * height / 2, y + lean[1] * height / 2, z + height / 2),
             colour, rot=(math.atan2(-lean[1], 1.0), math.atan2(lean[0], 1.0), 0.0), verts=8)
    return (x + lean[0] * height, y + lean[1] * height, z + height)


def ground_tuft(rng, count, radius, colour, length=0.18, width=0.07, z=0.03):
    for i in range(count):
        a = i / count * math.tau + rng.random() * 0.4
        r = radius * (0.35 + rng.random() * 0.65)
        leaf("tuft_%d" % i, length, width, (math.cos(a) * r, math.sin(a) * r, z),
             math.degrees(a), 18 + rng.random() * 12, colour)


def stubs(rng, count, radius, height=0.07, colour="stub"):
    """What is left once a plant has been picked: cut stalks, pale at the top."""
    for i in range(count):
        a = i / count * math.tau + rng.random()
        r = radius * rng.random()
        cyl("stub_%d" % i, 0.016, height, (math.cos(a) * r, math.sin(a) * r, height / 2), colour, verts=8)


def herb_marigold(picked):
    import random
    rng = random.Random(11)
    ground_tuft(rng, 9, 0.22, "herb_leaf", 0.22, 0.09)
    if picked:
        stubs(rng, 4, 0.12)
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.12, 0.02, 0.26), (0.10, -0.06, 0.32), (0.02, 0.12, 0.22), (0.16, 0.10, 0.18))):
        top = stem("stem_%d" % i, h, (x, y, 0.02))
        sphere("bloom_%d" % i, 0.075, top, "marigold")
        sphere("bloom_c_%d" % i, 0.045, (top[0], top[1] - 0.03, top[2] + 0.04), "marigold_dk")
        leaf("sl_%d" % i, 0.12, 0.05, (x, y, h * 0.5), rng.random() * 360, 20, "herb_leaf_lt")
    return (HERB_SPAN, HERB_ELEVATION)


def herb_flax(picked):
    """Thin straw-green stalks in a loose sheaf, a blue flower at the top of
    each. It grows at the edge of a ploughed field, which is where it is put."""
    import random
    rng = random.Random(23)
    ground_tuft(rng, 7, 0.20, "flax_stalk", 0.20, 0.05)
    if picked:
        stubs(rng, 6, 0.14, colour="flax_stalk")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.14, 0.0, 0.40), (-0.05, 0.08, 0.46), (0.05, -0.04, 0.44),
                                   (0.13, 0.06, 0.38), (0.0, -0.10, 0.34))):
        top = stem("stem_%d" % i, h, (x, y, 0.02), colour="flax_stalk", radius=0.011,
                   lean=((i - 2) * 0.05, 0.0))
        sphere("bloom_%d" % i, 0.052, top, "flax_blue")
        sphere("bloom_c_%d" % i, 0.024, (top[0], top[1] - 0.03, top[2] + 0.02), "flax_eye")
    return (HERB_SPAN, HERB_ELEVATION)


def herb_brookmint(picked):
    import random
    rng = random.Random(12)
    ground_tuft(rng, 6, 0.2, "mint_dk", 0.16, 0.07)
    if picked:
        stubs(rng, 5, 0.14, 0.09, "mint_dk")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.10, 0.0, 0.40), (0.08, -0.05, 0.34), (0.0, 0.10, 0.30), (0.17, 0.08, 0.26))):
        top = stem("stem_%d" % i, h, (x, y, 0.0), "mint_dk", 0.016)
        # Paired leaves up the stem, each pair turned a quarter from the last.
        for k in range(3):
            z = 0.08 + k * (h - 0.1) / 3
            for side in (0, 180):
                leaf("ml_%d_%d_%d" % (i, k, side), 0.15 - k * 0.02, 0.08, (x, y, z),
                     side + k * 90 + i * 30, 12, "mint")
        cone("spike_%d" % i, 0.035, 0.10, (top[0], top[1], top[2] + 0.02), "mint_flower", verts=8)
    return (HERB_SPAN, HERB_ELEVATION)


def herb_nettle(picked):
    import random
    rng = random.Random(13)
    ground_tuft(rng, 5, 0.18, "nettle", 0.16, 0.08)
    if picked:
        stubs(rng, 3, 0.1, 0.1, "nettle_lt")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.08, 0.02, 0.50), (0.10, -0.04, 0.44), (0.02, 0.12, 0.38))):
        stem("stem_%d" % i, h, (x, y, 0.0), "nettle", 0.02)
        # Big drooping heart-shaped leaves, darkest low down.
        for k in range(4):
            z = 0.10 + k * (h - 0.08) / 4
            for side in (0, 180):
                leaf("nl_%d_%d_%d" % (i, k, side), 0.20 - k * 0.03, 0.12 - k * 0.015, (x, y, z),
                     side + k * 70 + i * 40, -18, "nettle" if k < 2 else "nettle_lt", 0.02)
    return (HERB_SPAN, HERB_ELEVATION)


def herb_bogbean(picked):
    import random
    rng = random.Random(14)
    cyl("water", 0.19, 0.02, (0, 0, 0.01), "bog_water", verts=20, rough=0.25)
    if picked:
        stubs(rng, 4, 0.14, 0.08, "bogbean")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.12, 0.0, 0.26), (0.10, 0.06, 0.22), (0.0, -0.10, 0.18))):
        top = stem("stalk_%d" % i, h, (x, y, 0.0), "bogbean", 0.02)
        for k in range(3):
            leaf("tl_%d_%d" % (i, k), 0.14, 0.09, (top[0] + math.cos(k * 2.094 + i) * 0.06,
                                                   top[1] + math.sin(k * 2.094 + i) * 0.06, top[2]),
                 math.degrees(k * 2.094 + i), 8, "bogbean")
    # The fringed white flower spike that gives it away.
    top = stem("spike", 0.38, (0.04, 0.04, 0.0), "bogbean", 0.018)
    for k in range(5):
        sphere("flower_%d" % k, 0.035, (0.04 + math.cos(k * 1.3) * 0.03, 0.04 + math.sin(k * 1.3) * 0.03,
                                        top[2] - k * 0.045), "bog_flower" if k % 2 == 0 else "bog_pink")
    return (HERB_SPAN, HERB_ELEVATION)


def herb_mountain_sage(picked):
    import random
    rng = random.Random(15)
    for i, (x, y, s) in enumerate(((-0.24, 0.10, 0.09), (0.22, -0.06, 0.07), (0.16, 0.18, 0.06))):
        bpy.ops.mesh.primitive_ico_sphere_add(radius=s, location=(x, y, s * 0.5), subdivisions=1)
        ob = bpy.context.active_object
        ob.data.materials.append(material("pebble_%d" % i, "pebble"))
    if picked:
        ground_tuft(rng, 4, 0.12, "sage_dk", 0.12, 0.07)
        stubs(rng, 4, 0.10, 0.06, "sage")
        return (HERB_SPAN, HERB_ELEVATION)
    # A low mound of soft, rounded grey-green leaves, with lilac spikes above.
    for i in range(14):
        a = i * 2.39
        r = 0.04 + (i / 14) * 0.18
        leaf("sg_%d" % i, 0.15, 0.09, (math.cos(a) * r, math.sin(a) * r, 0.05 + (1 - i / 14) * 0.12),
             math.degrees(a), 25, "sage" if i % 3 else "sage_dk", 0.03)
    for i, (x, y) in enumerate(((-0.06, 0.02), (0.06, -0.03), (0.0, 0.08))):
        top = stem("fs_%d" % i, 0.18, (x, y, 0.14), "sage_dk")
        cone("sf_%d" % i, 0.035, 0.12, (top[0], top[1], top[2] + 0.03), "sage_flower", verts=8)
    return (HERB_SPAN, HERB_ELEVATION)


def herb_glowcap(picked):
    import random
    rng = random.Random(16)
    ground_tuft(rng, 6, 0.24, "moss_dk", 0.14, 0.08)
    caps = ((-0.10, 0.02, 0.26, 0.12), (0.10, -0.04, 0.19, 0.10), (0.02, 0.13, 0.14, 0.08), (0.18, 0.10, 0.10, 0.06))
    if picked:
        for i, (x, y, h, r) in enumerate(caps[:3]):
            cyl("cut_%d" % i, r * 0.35, 0.04, (x, y, 0.02), "glowcap_stem", verts=10)
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h, r) in enumerate(caps):
        cyl("stalk_%d" % i, r * 0.3, h, (x, y, h / 2), "glowcap_stem", verts=10)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=(x, y, h), segments=16, ring_count=8)
        cap = bpy.context.active_object
        cap.scale = (1.0, 1.0, 0.55)
        cap.data.materials.append(material("cap_%d" % i, "glowcap", 0.5, 0.0, 0.35))
        for k in range(3):
            a = k * 2.1 + i
            sphere("dot_%d_%d" % (i, k), r * 0.16, (x + math.cos(a) * r * 0.5, y + math.sin(a) * r * 0.5, h + r * 0.4),
                   "glowcap_dot", emit=0.5)
    return (HERB_SPAN, HERB_ELEVATION)


def herb_emberbloom(picked):
    import random
    rng = random.Random(17)
    cyl("scorch", 0.17, 0.015, (0, 0, 0.008), "ash", verts=16)
    ground_tuft(rng, 5, 0.16, "ember_stem", 0.14, 0.05)
    if picked:
        stubs(rng, 3, 0.1, 0.08, "ember_stem")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.08, 0.02, 0.34), (0.10, -0.02, 0.28), (0.02, 0.12, 0.22))):
        top = stem("stem_%d" % i, h, (x, y, 0.0), "ember_stem", 0.018)
        # Petals like tongues of flame, pointing up and out.
        for k in range(5):
            a = k / 5 * math.tau
            cone("petal_%d_%d" % (i, k), 0.05, 0.19,
                 (top[0] + math.cos(a) * 0.035, top[1] + math.sin(a) * 0.035, top[2] + 0.05), "ember_petal",
                 rot=(math.sin(a) * -0.45, math.cos(a) * 0.45, 0.0), verts=6)
            bpy.context.active_object.data.materials[0] = material("petal_%d_%d" % (i, k), "ember_petal", 0.6, 0.0, 0.9)
        sphere("core_%d" % i, 0.045, (top[0], top[1], top[2] + 0.04), "ember_core", emit=1.0)
    return (HERB_SPAN, HERB_ELEVATION)


def flower(name, loc, petals, length, width, petal_col, core_col, emit=0.0, tilt=30):
    for k in range(petals):
        a = k / petals * 360.0
        l = leaf("%s_p%d" % (name, k), length, width,
                 (loc[0] + math.cos(math.radians(a)) * length * 0.45,
                  loc[1] + math.sin(math.radians(a)) * length * 0.45, loc[2]), a, tilt, petal_col, 0.02)
        if emit:
            l.data.materials[0] = material("%s_p%d" % (name, k), petal_col, 0.6, 0.0, emit)
    sphere(name + "_core", width * 0.35, (loc[0], loc[1], loc[2] + 0.02), core_col, emit=emit * 1.4)


def herb_moonpetal(picked):
    import random
    rng = random.Random(18)
    ground_tuft(rng, 7, 0.2, "moon_leaf", 0.18, 0.06)
    if picked:
        stubs(rng, 3, 0.1, 0.08, "moon_leaf")
        return (HERB_SPAN, HERB_ELEVATION)
    for i, (x, y, h) in enumerate(((-0.09, 0.03, 0.30), (0.11, -0.03, 0.24))):
        top = stem("stem_%d" % i, h, (x, y, 0.0), "moon_leaf", 0.016)
        flower("bloom_%d" % i, top, 6, 0.14, 0.08, "moon_petal", "moon_core", emit=0.25)
    return (HERB_SPAN, HERB_ELEVATION)


def herb_starlily(picked):
    import random
    rng = random.Random(19)
    # Leaves like blades of pale glass.
    for i in range(6):
        a = i / 6 * math.tau + 0.3
        cone("blade_%d" % i, 0.04, 0.26, (math.cos(a) * 0.08, math.sin(a) * 0.08, 0.11), "star_leaf",
             rot=(math.sin(a) * -0.5, math.cos(a) * 0.5, 0.0), verts=4)
    if picked:
        stubs(rng, 2, 0.05, 0.10, "star_leaf")
        return (HERB_SPAN, HERB_ELEVATION)
    top = stem("stem", 0.40, (0.0, 0.0, 0.0), "star_leaf", 0.02)
    flower("lily", top, 6, 0.18, 0.08, "star_petal", "star_core", emit=1.0, tilt=45)
    top2 = stem("stem2", 0.26, (0.12, -0.04, 0.0), "star_leaf", 0.016)
    flower("bud", top2, 5, 0.10, 0.05, "star_petal", "star_core", emit=0.9, tilt=55)
    return (HERB_SPAN, HERB_ELEVATION)


def prop_cauldron():
    """An iron pot on three legs over a small fire, full to near the brim with
    something green. The green is the whole identity: without it, a pot."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.36, location=(0, 0, 0.66), segments=28, ring_count=14)
    pot = bpy.context.active_object
    pot.scale = (1.0, 1.0, 0.78)
    pot.data.materials.append(material("pot", "cauldron", 0.8, 0.0))
    cyl("rim", 0.31, 0.06, (0, 0, 0.93), "cauldron_lt", verts=28, rough=0.7)
    cyl("brew", 0.27, 0.02, (0, 0, 0.95), "brew", verts=28, rough=0.3)
    bpy.context.active_object.data.materials[0] = material("brew", "brew", 0.3, 0.0, 0.5)
    for k, (x, y) in enumerate(((0.08, 0.05), (-0.10, -0.06), (0.02, -0.14))):
        sphere("bubble_%d" % k, 0.03 + 0.01 * k, (x * 0.8, y * 0.8, 0.97), "brew", emit=0.8)
    for k in range(3):
        a = k / 3 * math.tau + 0.5
        blk("leg_%d" % k, (0.08, 0.08, 0.42), (math.cos(a) * 0.27, math.sin(a) * 0.27, 0.21), "cauldron_lt")
    for k in range(2):
        a = math.radians(-90 + (k * 2 - 1) * 60)
        cyl("handle_%d" % k, 0.03, 0.12, (math.cos(a) * 0.38, math.sin(a) * 0.38, 0.86), "cauldron_lt",
            rot=(0, math.radians(90), a), verts=8)
    for k in range(4):
        a = k / 4 * math.tau
        blk("log_%d" % k, (0.40, 0.09, 0.09), (math.cos(a) * 0.20, math.sin(a) * 0.20, 0.05), "log",
            rot=(0, 0, a))
    for k, (x, h) in enumerate(((-0.08, 0.20), (0.08, 0.24), (0.0, 0.30))):
        cone("flame_%d" % k, 0.08, h, (x, -0.12, 0.10 + h / 2), "ember")
        bpy.context.active_object.data.materials[0] = material("flame_%d" % k, "ember", 0.6, 0.0, 1.6)
    cone("flame", 0.10, 0.22, (0, 0, 0.16), "ember")
    bpy.context.active_object.data.materials[0] = material("flame", "ember", 0.6, 0.0, 1.6)
    return 1.45


HERBS = ("marigold", "brookmint", "nettle", "bogbean", "mountain_sage", "glowcap",
         "emberbloom", "moonpetal", "starlily", "flax")
HERB_PROPS = {"cauldron": (prop_cauldron, 48)}
for _name in HERBS:
    _builder = globals()["herb_" + _name]
    HERB_PROPS["herb_" + _name] = ((lambda b=_builder: b(False)), 40)
    HERB_PROPS["herb_" + _name + "_picked"] = ((lambda b=_builder: b(True)), 40)


# -----------------------------------------------------------------------------
#  The Emberfell adit -- the mine's way in, on the overworld
#
#  It used to be a door sprite standing on open dirt, which read as a cupboard
#  somebody had left in a field. A mine is a hole in a hill. So this is the
#  hillside: a shoulder of broken rock in bands of grey and rust, scrub on its
#  top, and a timber-framed tunnel mouth cut into its face with rails running
#  out of the dark, an ore cart left by the entrance, a lantern on the lintel
#  and a spoil heap of tailings. The mouth sits at the image's horizontal
#  centre, where genmaps puts the portal.
# -----------------------------------------------------------------------------

PALETTE.update({
    "crag":        (0.545, 0.463, 0.357),
    "crag_dk":     (0.400, 0.337, 0.263),
    "crag_lt":     (0.690, 0.616, 0.490),
    "crag_rust":   (0.639, 0.443, 0.263),
    "scrub":       (0.388, 0.463, 0.259),
    "tunnel":      (0.055, 0.047, 0.047),
    "timber":      (0.451, 0.314, 0.200),
    "timber_dk":   (0.310, 0.212, 0.141),
    "rail":        (0.412, 0.408, 0.420),
    "tailings":    (0.494, 0.431, 0.380),
    "ore_red":     (0.639, 0.337, 0.247),
})


def rock(name, size, loc, colour, rng, flat=True):
    """A chunky faceted boulder: a low-poly sphere squashed to size and turned
    at random, so no two in the face repeat."""
    bpy.ops.mesh.primitive_ico_sphere_add(radius=1.0, location=loc, subdivisions=1)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = size
    ob.rotation_euler = (rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), rng.uniform(0, math.tau))
    ob.data.materials.append(material(name, colour, 0.9))
    if flat:
        for p in ob.data.polygons:
            p.use_smooth = False
    return ob


def prop_mine_adit():
    import random
    rng = random.Random(21)
    W = 4.6            # width of the rock face
    OPEN_W, OPEN_H = 1.10, 1.45
    face_y = 0.0       # the front of the cliff, where the mouth is cut

    # --- the hillside ----------------------------------------------------------------
    # Terraces stepping back and up, each a block of rock with boulders along
    # its front edge and scattered on its top, so the camera sees a hillside of
    # ledges rather than a wall. The lowest terrace has the mouth cut in it.
    tones = ("crag", "crag_dk", "crag_rust", "crag", "crag_lt", "crag_dk")
    steps = ((0.00, W,        1.55, 0.00), (0.55, W - 0.5, 2.05, 0.2), (1.10, W - 1.1, 2.50, -0.15), (1.60, W - 1.9, 2.85, 0.25))
    for t, (back, width, top, shift) in enumerate(steps):
        depth = 0.75
        blk("terrace_%d" % t, (width, depth, top), (shift, face_y + back + depth / 2 + 0.15, top / 2),
            "crag_dk" if t % 2 == 0 else "crag", bev=0.04)
        # Boulders along the front edge of the terrace's top, and down its face.
        x = shift - width / 2
        k = 0
        while x < shift + width / 2:
            rr = rng.uniform(0.20, 0.34)
            cx = x + rr * 0.6
            x += rr * 1.3
            rock("ledge_%d_%d" % (t, k), (rr * 1.15, rr * 0.9, rr * 0.8),
                 (cx, face_y + back + 0.12, top - rr * 0.2), tones[(t * 2 + k) % len(tones)], rng)
            if t == 0 and abs(cx) > OPEN_W / 2 + 0.35:
                rock("face_%d" % k, (rr * 1.2, rr * 0.7, rr * 1.1),
                     (cx + rng.uniform(-0.1, 0.1), face_y + 0.05, rng.uniform(0.3, 1.1)), tones[(k + 3) % len(tones)], rng)
            k += 1
    # Big boulders at the foot of the slope, either side, to break the base line.
    for k, (x, rr) in enumerate(((-2.25, 0.46), (-1.75, 0.34), (2.2, 0.50), (1.65, 0.32))):
        rock("foot_%d" % k, (rr, rr * 0.85, rr * 0.8), (x, face_y - 0.05, rr * 0.55), tones[k % len(tones)], rng)
    # Over the mouth, a lip of rock the timbers hold up.
    rock("brow", (OPEN_W * 0.62, 0.40, 0.26), (0, face_y + 0.18, OPEN_H + 0.32), "crag_dk", rng)
    # Scrub clinging to the ledges.
    for k in range(10):
        t = k % len(steps)
        back, width, top, shift = steps[t]
        sphere("scrub_%d" % k, rng.uniform(0.12, 0.19),
               (shift + rng.uniform(-width / 2 + 0.3, width / 2 - 0.3), face_y + back + 0.3, top + 0.06), "scrub")

    # --- the tunnel mouth ----------------------------------------------------------------
    blk("dark", (OPEN_W, 0.9, OPEN_H), (0, face_y + 0.55, OPEN_H / 2), "tunnel", bev=0)
    for sx in (-1, 1):
        blk("post_%d" % sx, (0.16, 0.18, OPEN_H + 0.05), (sx * (OPEN_W / 2 + 0.02), face_y + 0.08, OPEN_H / 2), "timber")
        # A brace from the post up into the rock, the way a real adit is set.
        blk("brace_%d" % sx, (0.10, 0.12, 0.55), (sx * (OPEN_W / 2 + 0.2), face_y + 0.10, OPEN_H - 0.22), "timber_dk",
            rot=(0, math.radians(-38 * sx), 0))
    blk("lintel", (OPEN_W + 0.46, 0.22, 0.18), (0, face_y + 0.05, OPEN_H + 0.08), "timber")
    # A second set a little way in, so the dark reads as depth, not paint.
    for sx in (-1, 1):
        blk("inner_post_%d" % sx, (0.10, 0.10, OPEN_H - 0.1), (sx * (OPEN_W / 2 - 0.1), face_y + 0.55, OPEN_H / 2 - 0.05), "timber_dk")
    blk("inner_lintel", (OPEN_W - 0.1, 0.10, 0.10), (0, face_y + 0.55, OPEN_H - 0.08), "timber_dk")
    # The lantern on its hook.
    blk("hook", (0.04, 0.22, 0.04), (0.38, face_y - 0.08, OPEN_H + 0.02), "iron")
    blk("lantern", (0.12, 0.12, 0.17), (0.38, face_y - 0.18, OPEN_H - 0.12), "glass_lit", emit=1.4)
    blk("lantern_cap", (0.15, 0.15, 0.04), (0.38, face_y - 0.18, OPEN_H - 0.02), "iron")

    # --- rails, cart and tailings ----------------------------------------------------------
    for i in range(7):
        y = face_y + 0.35 - i * 0.30
        blk("sleeper_%d" % i, (0.78, 0.12, 0.05), (0, y, 0.025), "timber_dk")
    for sx in (-1, 1):
        blk("rail_%d" % sx, (0.05, 2.2, 0.05), (sx * 0.26, face_y - 0.55, 0.07), "rail", metal=0.4, rough=0.5)
    # The cart, off the rails to one side, loaded with rust-red ore.
    cx, cy = -1.05, face_y - 0.55
    blk("cart", (0.62, 0.46, 0.34), (cx, cy, 0.30), "timber")
    blk("cart_band", (0.66, 0.50, 0.06), (cx, cy, 0.42), "iron")
    for k in range(5):
        rock("ore_%d" % k, (0.10, 0.09, 0.08), (cx + rng.uniform(-0.2, 0.2), cy + rng.uniform(-0.12, 0.12), 0.50),
             "ore_red" if k % 2 else "crag_lt", rng)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cyl("wheel", 0.08, 0.05, (cx + sx * 0.22, cy + sy * 0.24, 0.10), "iron", rot=(0, math.radians(90), 0), verts=12)
    # Tailings tipped out to the right.
    for k in range(10):
        rock("spoil_%d" % k, (rng.uniform(0.10, 0.2),) * 3,
             (1.2 + rng.uniform(-0.35, 0.35), face_y - 0.35 + rng.uniform(-0.3, 0.25), rng.uniform(0.05, 0.16)),
             "tailings" if k % 3 else "crag_lt", rng)
    # A pick left leaning on the post.
    blk("pick_haft", (0.05, 0.05, 0.62), (-OPEN_W / 2 - 0.22, face_y - 0.15, 0.30), "timber",
        rot=(0, math.radians(14), 0))
    blk("pick_head", (0.36, 0.06, 0.06), (-OPEN_W / 2 - 0.15, face_y - 0.15, 0.60), "iron",
        rot=(0, math.radians(14), 0))
    return (5.6, BUILDING_ELEVATION)


HERB_PROPS["mine_adit"] = (prop_mine_adit, 256)


# -----------------------------------------------------------------------------
#  The enchanting table -- where charms are worked into worn pieces
# -----------------------------------------------------------------------------

PALETTE.update({
    "altar_stone":    (0.470, 0.478, 0.530),
    "altar_stone_dk": (0.300, 0.310, 0.360),
    "rune_glow":      (0.420, 0.700, 1.000),
    "crystal":        (0.640, 0.840, 1.000),
    "book_leather":   (0.360, 0.160, 0.140),
    "page":           (0.920, 0.880, 0.760),
    "candle":         (0.920, 0.900, 0.800),
    "flame":          (1.000, 0.820, 0.400),
})


def prop_enchanting_table():
    """A slab of grey stone on a squat plinth, a ring of runes cut into its top
    and lit from below, an open book at the front, two candles, and a crystal
    standing at the back throwing pale blue light. The glow is the whole
    identity: without it, a table."""
    blk("plinth", (1.10, 0.70, 0.30), (0, 0, 0.15), "altar_stone_dk", bev=0.02)
    blk("slab", (1.56, 0.98, 0.16), (0, 0, 0.38), "altar_stone", bev=0.02)
    # The ring of runes, sunk into the slab and lit.
    bpy.ops.mesh.primitive_torus_add(major_radius=0.32, minor_radius=0.03, location=(0.02, 0.06, 0.462),
                                     major_segments=24, minor_segments=8)
    ring = bpy.context.active_object
    ring.name = "rune_ring"
    ring.data.materials.append(material("rune_ring", "rune_glow", 0.5, 0.0, 2.2))
    for k in range(4):
        a = k / 4 * math.tau + math.tau / 8
        blk("rune_%d" % k, (0.09, 0.05, 0.02), (0.02 + math.cos(a) * 0.20, 0.06 + math.sin(a) * 0.20, 0.468),
            "rune_glow", rot=(0, 0, a), emit=2.0, bev=0)
    sphere("eye", 0.045, (0.02, 0.06, 0.47), "rune_glow", emit=2.4)
    # The book, open, lying at the front.
    blk("book_cover", (0.52, 0.36, 0.05), (-0.42, -0.24, 0.485), "book_leather", rot=(0, 0, 0.22), bev=0.008)
    blk("pages", (0.46, 0.30, 0.05), (-0.42, -0.24, 0.525), "page", rot=(0, 0, 0.22), bev=0.006)
    blk("spine", (0.02, 0.30, 0.07), (-0.42, -0.24, 0.53), "book_leather", rot=(0, 0, 0.22), bev=0)
    # The crystal at the back, and a smaller one leaning on it.
    # Lit, but not so brightly it burns out to white: it has to stay blue.
    cone("crystal", 0.12, 0.66, (0.40, 0.26, 0.46 + 0.33), "crystal", verts=6)
    bpy.context.active_object.data.materials[0] = material("crystal_lit", "crystal", 0.35, 0.0, 0.7)
    cone("crystal_b", 0.075, 0.38, (0.55, 0.12, 0.46 + 0.17), "crystal", rot=(0, math.radians(20), 0), verts=6)
    bpy.context.active_object.data.materials[0] = material("crystal_lit", "crystal", 0.35, 0.0, 0.7)
    # Two candles on the left, one taller than the other.
    for k, (x, y, h) in enumerate(((-0.58, 0.30, 0.24), (-0.66, 0.10, 0.16))):
        cyl("candle_%d" % k, 0.038, h, (x, y, 0.46 + h / 2), "candle", verts=8)
        sphere("flame_%d" % k, 0.05, (x, y, 0.46 + h + 0.05), "flame", emit=3.0)
    return 2.0


HERB_PROPS["enchanting_table"] = (prop_enchanting_table, 64)


# -----------------------------------------------------------------------------
#  The mage college at Fernhollow, and the circle cut into its floor
# -----------------------------------------------------------------------------

PALETTE.update({
    "slate":        (0.300, 0.320, 0.400),
    "slate_dk":     (0.210, 0.225, 0.290),
    "circle_stone": (0.360, 0.350, 0.400),
})


def prop_mage_college():
    """A round stone tower under a cone of slate, with a lit arched window
    over an oak door, a lantern on a bracket beside it, a low stone annex
    with its own roof, and a crystal on the finial throwing a pale light.
    Older than the hamlet, so the stone is darker and the courses uneven."""
    import random
    rng = random.Random(41)
    R, H = 1.25, 2.70
    base = 0.14
    front = -R
    cyl("plinth", R + 0.16, base, (0, 0, base / 2), "stone_pale", verts=28)
    cyl("tower", R, H, (0, 0, base + H / 2), "stone", verts=28, rough=0.9)
    # Courses, uneven: every other one a shade paler, and a few stones proud.
    for k in range(6):
        z = base + 0.32 + k * 0.42
        cyl("course_%d" % k, R + 0.015, 0.05, (0, 0, z), ("stone_pale", "stone")[k % 2], verts=28)
    for k in range(14):
        a = rng.uniform(0.3, 2.85) + (0 if k % 2 else math.pi)
        z = base + 0.25 + rng.uniform(0, H - 0.5)
        blk("stone_%d" % k, (0.22, 0.08, 0.12), (math.cos(a) * R, math.sin(a) * R, z), "stone_pale",
            rot=(0, 0, a), bev=0.01)
    # The door, arched with a lintel, and the step.
    blk("door", (0.62, 0.10, 1.00), (0, front + 0.06, base + 0.50), "log_dk")
    for i in range(3):
        blk("door_plank_%d" % i, (0.02, 0.02, 0.94), (-0.17 + i * 0.17, front + 0.02, base + 0.50), "log", bev=0)
    cyl("arch", 0.36, 0.14, (0, front + 0.07, base + 1.00), "stone_pale", rot=(math.radians(90), 0, 0), verts=20)
    blk("step", (0.90, 0.34, 0.08), (0, front - 0.18, 0.04), "stone_pale")
    sphere("knob", 0.045, (0.20, front - 0.02, base + 0.48), "brass")
    # A lantern on a bracket beside the door.
    blk("bracket", (0.06, 0.20, 0.06), (0.62, front - 0.06, base + 1.34), "iron")
    blk("lantern", (0.16, 0.16, 0.22), (0.62, front - 0.16, base + 1.20), "candle_glow", emit=1.3, bev=0.03)
    # Windows: a tall lit one over the door and two smaller round the sides.
    window("win_front", 0, front + 0.02, base + 1.90, w=0.34, h=0.50, lit=True, shutters=False)
    for side in (-1, 1):
        a = math.radians(90 + side * 52)
        x, y = math.cos(a) * R * -side * 0 + side * R * 0.80, -R * 0.60
        window("win_%d" % side, x, y, base + 1.35, w=0.26, h=0.34, lit=(side > 0), shutters=False)
    # The cone of slate, in bands, and a finial with a crystal in it.
    roof_z = base + H
    # The cone is kept short enough that the crystal on its finial stays
    # inside the frame; the first one lost its point off the top.
    cone("roof", R + 0.34, 1.55, (0, 0, roof_z + 0.77), "slate", verts=28)
    for k in range(4):
        t = 0.15 + k * 0.22
        cone("band_%d" % k, (R + 0.34) * (1 - t) + 0.02, 0.07, (0, 0, roof_z + t * 1.55), "slate_dk", verts=28)
    cyl("finial", 0.06, 0.30, (0, 0, roof_z + 1.55 + 0.13), "iron", verts=8)
    cone("crystal", 0.11, 0.36, (0, 0, roof_z + 1.55 + 0.44), "crystal", verts=6)
    bpy.context.active_object.data.materials[0] = material("crystal_top", "crystal", 0.35, 0.0, 0.9)
    # The annex: a low square wing on the right with a gable roof.
    ax = R + 0.55
    blk("annex", (1.10, 1.30, 1.10), (ax, 0.10, base + 0.55), "stone", bev=0.03)
    pitch = math.radians(38)
    half = 0.65 + 0.18
    slab = half / math.cos(pitch)
    rise = half * math.tan(pitch)
    for side in (-1, 1):
        blk("annex_roof_%d" % side, (1.10 + 0.36, slab, 0.12),
            (ax, 0.10 + side * half / 2, base + 1.10 + rise / 2), "slate",
            rot=(-side * pitch, 0, 0), bev=0.03)
    cyl("annex_ridge", 0.06, 1.10 + 0.40, (ax, 0.10, base + 1.10 + rise + 0.02), "slate_dk",
        rot=(0, math.radians(90), 0), verts=10)
    window("win_annex", ax, 0.10 - 0.65 + 0.02, base + 0.62, w=0.30, h=0.30, lit=True, shutters=False)
    return (5.0, BUILDING_ELEVATION)


def prop_spell_circle():
    """The circle cut into the college's floor: two rings of pale stone, the
    runes between them lit from below, and a star of lines at the middle.
    Drawn flat, to lie on the floor as an overlay."""
    cyl("outer", 0.90, 0.02, (0, 0, 0.01), "circle_stone", verts=36)
    cyl("inner_dark", 0.80, 0.022, (0, 0, 0.011), "stone", verts=36)
    cyl("inner", 0.56, 0.024, (0, 0, 0.012), "circle_stone", verts=36)
    cyl("core", 0.48, 0.026, (0, 0, 0.013), "stone", verts=36)
    for k in range(12):
        a = k / 12 * math.tau
        blk("rune_%d" % k, (0.12, 0.06, 0.02), (math.cos(a) * 0.68, math.sin(a) * 0.68, 0.03),
            "rune_glow", rot=(0, 0, a), emit=1.8, bev=0)
    for k in range(3):
        a = k / 3 * math.pi
        blk("line_%d" % k, (0.92, 0.035, 0.018), (0, 0, 0.03), "rune_glow", rot=(0, 0, a), emit=1.4, bev=0)
    sphere("eye", 0.07, (0, 0, 0.03), "rune_glow", emit=2.2)
    return (2.0, 62.0)


HERB_PROPS["mage_college"] = (prop_mage_college, 176)
HERB_PROPS["spell_circle"] = (prop_spell_circle, 96)


# -----------------------------------------------------------------------------
#  Totems
#
#  What a boss leaves the fifteenth time it is killed, and the ring in the
#  floor of the house at Mossvale that one is stood in. One builder: a squat
#  carved post -- three blocks with a band between, a face with lit eyes in the
#  top one -- and what is on its head says whose it is. At thirty-two pixels a
#  carving is a colour and a silhouette, so that is all each one is given: the
#  Broodmother's is a spider, the Warchief's has horns, the Wight's is a skull.
#  The same picture is the thing in the bag and the thing in the ring.
# -----------------------------------------------------------------------------

PALETTE.update({
    "totem_base":   (0.455, 0.447, 0.431),
    "totem_ring":   (0.780, 0.760, 0.700),
    "totem_ring_dk": (0.420, 0.400, 0.370),
    "totem_rune":   (1.000, 0.780, 0.360),
    "tt_brood":     (0.290, 0.220, 0.300),  "tt_brood_b":   (0.560, 0.780, 0.300),
    "tt_lizard":    (0.300, 0.500, 0.330),  "tt_lizard_b":  (0.880, 0.780, 0.300),
    "tt_wight":     (0.520, 0.540, 0.560),  "tt_wight_b":   (0.890, 0.855, 0.769),
    "tt_orc":       (0.560, 0.200, 0.160),  "tt_orc_b":     (0.900, 0.860, 0.760),
    "tt_well":      (0.300, 0.400, 0.480),  "tt_well_b":    (0.400, 0.900, 0.860),
    "tt_bear":      (0.440, 0.300, 0.190),  "tt_bear_b":    (0.800, 0.620, 0.380),
    "tt_troll":     (0.420, 0.300, 0.560),  "tt_troll_b":   (0.860, 0.820, 0.700),
    "tt_wyvern":    (0.560, 0.700, 0.800),  "tt_wyvern_b":  (0.920, 0.950, 0.980),
    "tt_pit":       (0.160, 0.130, 0.140),  "tt_pit_b":     (1.000, 0.450, 0.150),
    "tt_frost":     (0.620, 0.855, 0.945),  "tt_frost_b":   (0.960, 0.990, 1.000),
    "tt_dread":     (0.250, 0.160, 0.360),  "tt_dread_b":   (0.800, 0.400, 1.000),
    # Lord Ashcroft: black wood and the red gold of his own signet, flattened
    # and nailed to it. The Mother of the Fen: river clay and reed.
    "tt_vamp":      (0.115, 0.090, 0.105),  "tt_vamp_b":    (0.880, 0.170, 0.230),
    "tt_fen":       (0.235, 0.290, 0.205),  "tt_fen_b":     (0.560, 0.780, 0.480),
})


def _totem(post, band, cap, eye_emit=1.3):
    cyl("base", 0.30, 0.09, (0, 0, 0.045), "totem_base", verts=10)
    blk("p0", (0.38, 0.32, 0.30), (0, 0, 0.24), post)
    blk("band0", (0.44, 0.38, 0.08), (0, 0, 0.42), band)
    blk("p1", (0.34, 0.30, 0.22), (0, 0, 0.57), post)
    blk("band1", (0.42, 0.36, 0.07), (0, 0, 0.71), band)
    blk("head", (0.42, 0.34, 0.28), (0, 0, 0.88), post)
    # The face: a brow, and two eyes that are lit. The eyes are most of what
    # says "this is a made thing, and it is looking at you".
    blk("brow", (0.44, 0.06, 0.07), (0, -0.175, 0.96), band, bev=0)
    for sx in (-1, 1):
        blk("eye_%d" % sx, (0.10, 0.04, 0.08), (sx * 0.10, -0.178, 0.87), band, emit=eye_emit, bev=0)
    blk("mouth", (0.20, 0.04, 0.05), (0, -0.175, 0.78), "totem_ring_dk", bev=0)

    top = 1.02
    if cap == "spider":
        sphere("body", 0.15, (0, 0, top + 0.10), post)
        sphere("abdomen", 0.12, (0, 0.10, top + 0.16), post)
        for sx in (-1, 1):
            for k in range(3):
                blk("leg_%d_%d" % (sx, k), (0.26, 0.05, 0.05),
                    (sx * 0.24, -0.08 + k * 0.10, top + 0.06), band,
                    rot=(0, sx * math.radians(32), sx * math.radians(-18 + k * 18)), bev=0)
    elif cap == "crest":
        for k, x in enumerate((-0.13, 0.0, 0.13)):
            blk("fin_%d" % k, (0.07, 0.30, 0.22 + (0.10 if k == 1 else 0.0)), (x, 0.02, top + 0.11 + (0.05 if k == 1 else 0.0)), band)
    elif cap == "skull":
        sphere("skull", 0.17, (0, 0, top + 0.13), band)
        blk("jaw", (0.20, 0.16, 0.08), (0, -0.04, top + 0.00), band)
        for sx in (-1, 1):
            blk("socket_%d" % sx, (0.07, 0.04, 0.07), (sx * 0.07, -0.16, top + 0.14), "totem_ring_dk", bev=0)
    elif cap == "horns":
        blk("helm", (0.46, 0.38, 0.08), (0, 0, top + 0.03), "totem_ring_dk")
        for sx in (-1, 1):
            cone("horn_%d" % sx, 0.085, 0.36, (sx * 0.25, 0, top + 0.17), band, rot=(0, sx * math.radians(34), 0))
    elif cap == "lantern":
        blk("cage_floor", (0.30, 0.26, 0.05), (0, 0, top + 0.02), "totem_ring_dk")
        blk("light", (0.20, 0.18, 0.18), (0, 0, top + 0.13), band, emit=1.5, bev=0)
        blk("cage_roof", (0.32, 0.28, 0.06), (0, 0, top + 0.25), "totem_ring_dk")
        cone("cage_peak", 0.12, 0.12, (0, 0, top + 0.34), "totem_ring_dk")
    elif cap == "ears":
        for sx in (-1, 1):
            sphere("ear_%d" % sx, 0.10, (sx * 0.17, 0.02, top + 0.06), post)
            sphere("ear_in_%d" % sx, 0.055, (sx * 0.17, -0.05, top + 0.06), band)
        blk("snout", (0.18, 0.12, 0.12), (0, -0.20, 0.82), band)
    elif cap == "tusks":
        blk("heavy_brow", (0.50, 0.14, 0.12), (0, -0.14, top + 0.02), post)
        for sx in (-1, 1):
            cone("tusk_%d" % sx, 0.06, 0.26, (sx * 0.14, -0.20, 0.86), band)
    elif cap == "wings":
        for sx in (-1, 1):
            blk("wing_%d" % sx, (0.34, 0.06, 0.30), (sx * 0.30, 0.06, top + 0.06), band,
                rot=(0, sx * math.radians(-28), 0))
        cone("crown", 0.10, 0.18, (0, 0, top + 0.09), post)
    elif cap == "great_horns":
        for sx in (-1, 1):
            cone("horn_%d" % sx, 0.10, 0.46, (sx * 0.24, 0, top + 0.20), post, rot=(0, sx * math.radians(26), 0))
        blk("ember", (0.16, 0.14, 0.14), (0, 0, top + 0.07), band, emit=1.6, bev=0)
    elif cap == "spikes":
        for k, y in enumerate((-0.10, 0.04, 0.16)):
            cone("spike_%d" % k, 0.085, 0.30 - k * 0.04, (0, y, top + 0.15 - k * 0.02), band)
        for sx in (-1, 1):
            cone("side_%d" % sx, 0.06, 0.20, (sx * 0.20, 0.02, top + 0.08), post, rot=(0, sx * math.radians(30), 0))
    # Tight: it is also the picture in the bag, and a totem lost in the middle
    # of its slot is a smudge. Not tighter than this: at 1.5 the spider, the
    # crest and the lantern ran into the top of the frame.
    return 1.64


def prop_totem_broodmother():      return _totem("tt_brood", "tt_brood_b", "spider")
def prop_totem_lizardman_chief():  return _totem("tt_lizard", "tt_lizard_b", "crest")
def prop_totem_barrow_wight():     return _totem("tt_wight", "tt_wight_b", "skull")
def prop_totem_orc3():             return _totem("tt_orc", "tt_orc_b", "horns")
def prop_totem_well_warden():      return _totem("tt_well", "tt_well_b", "lantern")
def prop_totem_den_mother():       return _totem("tt_bear", "tt_bear_b", "ears")
def prop_totem_nightmare_troll():  return _totem("tt_troll", "tt_troll_b", "tusks")
def prop_totem_wyvern_matriarch(): return _totem("tt_wyvern", "tt_wyvern_b", "wings")
def prop_totem_pit_lord():         return _totem("tt_pit", "tt_pit_b", "great_horns")
def prop_totem_frost_dragon():     return _totem("tt_frost", "tt_frost_b", "spikes")
def prop_totem_nightmare_dragon(): return _totem("tt_dread", "tt_dread_b", "spikes")
def prop_totem_vampire_lord():     return _totem("tt_vamp", "tt_vamp_b", "skull", eye_emit=2.1)
def prop_totem_bayou_matriarch():  return _totem("tt_fen", "tt_fen_b", "tusks")


def prop_totem_circle():
    """The ring in the floor of the house at Mossvale: a band of pale stone
    set flush in the boards, eight runes cut in it and lit, and a dark socket
    in the middle for a totem's foot. Drawn flat, as an overlay."""
    cyl("ring", 0.46, 0.02, (0, 0, 0.010), "totem_ring", verts=20)
    # Dark inside the band, and darker again at the middle: a plate is a thing
    # put down, a socket is somewhere to put a thing.
    cyl("dark", 0.33, 0.022, (0, 0, 0.011), "totem_ring_dk", verts=20)
    cyl("socket", 0.17, 0.024, (0, 0, 0.012), (0.13, 0.11, 0.10), verts=16)
    for k in range(8):
        a = k / 8 * math.tau
        blk("rune_%d" % k, (0.10, 0.06, 0.02), (math.cos(a) * 0.40, math.sin(a) * 0.40, 0.03),
            "totem_rune", rot=(0, 0, a), emit=1.4, bev=0)
    return (1.05, 62.0)


TOTEM_PROPS = {
    "totem_circle":           (prop_totem_circle, 24),
    "totem_broodmother":      (prop_totem_broodmother, 32),
    "totem_lizardman_chief":  (prop_totem_lizardman_chief, 32),
    "totem_barrow_wight":     (prop_totem_barrow_wight, 32),
    "totem_orc3":             (prop_totem_orc3, 32),
    "totem_well_warden":      (prop_totem_well_warden, 32),
    "totem_den_mother":       (prop_totem_den_mother, 32),
    "totem_nightmare_troll":  (prop_totem_nightmare_troll, 32),
    "totem_wyvern_matriarch": (prop_totem_wyvern_matriarch, 32),
    "totem_pit_lord":         (prop_totem_pit_lord, 32),
    "totem_frost_dragon":     (prop_totem_frost_dragon, 32),
    "totem_nightmare_dragon": (prop_totem_nightmare_dragon, 32),
    "totem_vampire_lord":     (prop_totem_vampire_lord, 32),
    "totem_bayou_matriarch":  (prop_totem_bayou_matriarch, 32),
}


# -----------------------------------------------------------------------------
#  The College at Fernhollow
#
#  It was a tower in a corner of the hamlet. It is a place of its own now, north
#  of the water: a gatehouse, a great court, a hall across the north side of it
#  and chambers off the west and the east. Everything here is pale stone under
#  blue slate with gold on it, because everything else in the Hollowmarch is
#  brown timber, and the point of the place is that it is not the Hollowmarch's.
# -----------------------------------------------------------------------------

PALETTE.update({
    "col_stone":    (0.800, 0.780, 0.730),
    "col_stone_lt": (0.890, 0.870, 0.820),
    "col_stone_dk": (0.600, 0.585, 0.600),
    "col_blue":     (0.215, 0.310, 0.580),
    "col_blue_dk":  (0.145, 0.205, 0.420),
    "col_blue_lt":  (0.380, 0.500, 0.800),
    "col_gold":     (0.850, 0.700, 0.350),
    "col_glass":    (0.560, 0.820, 0.980),
    "col_water":    (0.330, 0.600, 0.860),
    "col_water_lt": (0.700, 0.880, 0.980),
    "col_door":     (0.300, 0.200, 0.140),
    "hedge":        (0.220, 0.420, 0.220),
    "hedge_lt":     (0.330, 0.540, 0.290),
    "hedge_dk":     (0.150, 0.300, 0.170),
    "dummy_sack":   (0.760, 0.660, 0.460),
    "dummy_sack_dk": (0.560, 0.470, 0.320),
    "scorch":       (0.160, 0.130, 0.120),
    "board_green":  (0.160, 0.260, 0.220),
    "brass_lt":     (0.900, 0.780, 0.420),
})


def _col_window(name, x, y, z, w=0.36, h=0.80, lit=True):
    """A tall arched window of lit blue glass in a pale stone surround."""
    # Square-headed under a lintel. Arched heads were tried: at this size the
    # curve is two dark pixels at the top corners, and every window had eyes.
    blk(name + "_surround", (w + 0.14, 0.08, h + 0.14), (x, y, z), "col_stone_lt")
    blk(name + "_glass", (w, 0.05, h), (x, y - 0.04, z), "col_glass", emit=0.9 if lit else 0.0, rough=0.3, bev=0)
    blk(name + "_lintel", (w + 0.24, 0.12, 0.10), (x, y - 0.02, z + h / 2 + 0.09), "col_stone_lt")
    blk(name + "_sill", (w + 0.24, 0.14, 0.07), (x, y - 0.03, z - h / 2 - 0.07), "col_stone_lt")
    blk(name + "_mullion", (0.05, 0.06, h), (x, y - 0.06, z), "col_stone_lt", bev=0)


def _col_column(name, x, y, height, radius=0.13):
    cyl(name + "_base", radius + 0.07, 0.10, (x, y, 0.05), "col_stone_lt", verts=14)
    cyl(name + "_shaft", radius, height, (x, y, 0.10 + height / 2), "col_stone", verts=14)
    cyl(name + "_band", radius + 0.02, 0.07, (x, y, 0.10 + height - 0.10), "col_gold", verts=14, metal=0.4, rough=0.45)
    blk(name + "_cap", (radius * 2 + 0.16, radius * 2 + 0.16, 0.10), (x, y, 0.10 + height + 0.05), "col_stone_lt")


def _col_spire(name, x, y, z, radius, height, crystal=True):
    cone(name, radius, height, (x, y, z + height / 2), "col_blue", verts=20)
    for k in range(3):
        t = 0.18 + k * 0.24
        cone("%s_band_%d" % (name, k), radius * (1 - t) + 0.02, 0.06, (x, y, z + t * height), "col_blue_dk", verts=20)
    cyl(name + "_finial", 0.04, 0.22, (x, y, z + height + 0.08), "col_gold", verts=8, metal=0.4, rough=0.45)
    if crystal:
        cone(name + "_crystal", 0.09, 0.30, (x, y, z + height + 0.32), "crystal", verts=6)
        bpy.context.active_object.data.materials[0] = material(name + "_cr", "crystal", 0.35, 0.0, 1.0)


def prop_college_hall():
    """The great hall across the north of the court: a long pale front on a
    flight of steps, six columns under a pediment with the college's star in
    it, tall lit windows either side of a pair of doors, a blue slate roof, and
    a round tower at each end under a spire with a crystal on it. The council
    sits behind those doors, so it is the widest thing in the Hollowmarch."""
    W, D, H = 7.6, 1.6, 2.5
    front = -D / 2
    # Steps across the whole portico.
    for k in range(3):
        blk("step_%d" % k, (4.2 - k * 0.3, 0.30, 0.10), (0, front - 0.72 + k * 0.24, 0.05 + k * 0.10), "col_stone_lt")
    blk("body", (W, D, H), (0, 0, 0.30 + H / 2), "col_stone", bev=0.03)
    blk("plinth", (W + 0.2, D + 0.2, 0.30), (0, 0, 0.15), "col_stone_dk", bev=0.03)
    blk("cornice", (W + 0.24, D + 0.24, 0.16), (0, 0, 0.30 + H + 0.08), "col_stone_lt", bev=0.03)
    blk("frieze", (W + 0.02, 0.06, 0.22), (0, front - 0.02, 0.30 + H - 0.22), "col_blue", bev=0)
    for k in range(9):
        blk("frieze_pip_%d" % k, (0.10, 0.04, 0.10), (-3.4 + k * 0.85, front - 0.05, 0.30 + H - 0.22), "col_gold", bev=0)
    # The portico: six columns and a pediment.
    for k in range(6):
        _col_column("col_%d" % k, -1.75 + k * 0.70, front - 0.45, 2.05, radius=0.12)
    blk("entablature", (4.10, 0.60, 0.22), (0, front - 0.30, 0.30 + 2.05 + 0.26), "col_stone_lt")
    # A pediment: a shallow triangle made of a wide thin block and two slanted ones.
    ped_z = 0.30 + 2.05 + 0.37
    # The face is a triangle, which at this size is three courses each shorter
    # than the one under it; the two slabs over it rise to the middle. (The
    # first ones fell to the middle, and the hall had a butterfly on its head.)
    for k, wdt in enumerate((3.7, 2.5, 1.3)):
        blk("ped_face_%d" % k, (wdt, 0.50, 0.22), (0, front - 0.30, ped_z + 0.11 + k * 0.22), "col_stone", bev=0)
    for side in (-1, 1):
        blk("ped_%d" % side, (2.25, 0.66, 0.12), (side * 1.02, front - 0.30, ped_z + 0.42), "col_blue",
            rot=(0, side * math.radians(19), 0), bev=0.02)
    sphere("star", 0.17, (0, front - 0.58, ped_z + 0.26), "col_gold", emit=0.6)
    # The doors, a pair, under a fanlight.
    blk("door_case", (1.50, 0.10, 1.95), (0, front - 0.01, 0.30 + 0.97), "col_stone_lt")
    for side in (-1, 1):
        blk("door_%d" % side, (0.60, 0.08, 1.50), (side * 0.31, front - 0.05, 0.30 + 0.75), "col_door")
        blk("door_panel_%d" % side, (0.38, 0.03, 0.52), (side * 0.31, front - 0.10, 0.30 + 1.08), "oak", bev=0.01)
        blk("door_panel_b_%d" % side, (0.38, 0.03, 0.52), (side * 0.31, front - 0.10, 0.30 + 0.42), "oak", bev=0.01)
        sphere("knob_%d" % side, 0.05, (side * 0.08, front - 0.11, 0.30 + 0.76), "brass_lt")
    blk("fanlight", (1.24, 0.08, 0.30), (0, front - 0.05, 0.30 + 1.68), "col_glass", emit=1.0, bev=0)
    # Windows, tall and lit, down the front.
    for k, x in enumerate((-3.10, -2.45, 2.45, 3.10)):
        _col_window("win_%d" % k, x, front - 0.02, 0.30 + 1.25, w=0.34, h=1.10)
    # The roof: blue slate, hipped -- a long ridge with sloped ends.
    rz = 0.30 + H + 0.16
    pitch = math.radians(34)
    half = D / 2 + 0.30
    for side in (-1, 1):
        blk("roof_%d" % side, (W + 0.5, half / math.cos(pitch), 0.12), (0, side * half / 2, rz + half * math.tan(pitch) / 2),
            "col_blue", rot=(-side * pitch, 0, 0), bev=0.02)
    blk("ridge", (W + 0.54, 0.16, 0.12), (0, 0, rz + half * math.tan(pitch) + 0.02), "col_blue_dk", bev=0.02)
    for k in range(3):
        t = 0.25 + k * 0.25
        blk("roof_course_%d" % k, (W + 0.52, 0.05, 0.03), (0, -half * (1 - t), rz + half * math.tan(pitch) * t + 0.075),
            "col_blue_dk", bev=0)
    # A tower at each end.
    for side in (-1, 1):
        tx = side * (W / 2 + 0.35)
        cyl("tower_%d" % side, 0.78, 3.7, (tx, 0.05, 1.85), "col_stone", verts=22)
        cyl("tower_plinth_%d" % side, 0.88, 0.30, (tx, 0.05, 0.15), "col_stone_dk", verts=22)
        for k in range(3):
            cyl("tower_course_%d_%d" % (side, k), 0.80, 0.07, (tx, 0.05, 1.0 + k * 1.0), "col_stone_lt", verts=22)
        cyl("tower_top_%d" % side, 0.90, 0.16, (tx, 0.05, 3.78), "col_stone_lt", verts=22)
        _col_window("tower_win_%d" % side, tx, -0.74, 2.70, w=0.26, h=0.60)
        _col_window("tower_win_lo_%d" % side, tx, -0.74, 1.30, w=0.26, h=0.60)
        _col_spire("spire_%d" % side, tx, 0.05, 3.86, 0.98, 1.55)
    # Banners either side of the portico.
    for side in (-1, 1):
        blk("banner_%d" % side, (0.34, 0.04, 1.30), (side * 2.55 * 0 + side * 1.98, front - 0.06, 0.30 + 1.55), "col_blue", bev=0)
        blk("banner_gold_%d" % side, (0.34, 0.05, 0.08), (side * 1.98, front - 0.07, 0.30 + 0.96), "col_gold", bev=0)
        sphere("banner_star_%d" % side, 0.08, (side * 1.98, front - 0.09, 0.30 + 1.70), "col_gold")
    return (10.6, BUILDING_ELEVATION)


def prop_college_wing():
    """A wing of the hall: the same pale range under the same blue roof, seven
    tall lit windows between pilasters, and nothing else -- it stands either
    side of the hall and makes the north of the court one front."""
    W, D, H = 6.9, 1.6, 2.5
    front = -D / 2
    blk("body", (W, D, H), (0, 0, 0.30 + H / 2), "col_stone", bev=0.03)
    blk("plinth", (W + 0.2, D + 0.2, 0.30), (0, 0, 0.15), "col_stone_dk", bev=0.03)
    blk("cornice", (W + 0.24, D + 0.24, 0.16), (0, 0, 0.30 + H + 0.08), "col_stone_lt", bev=0.03)
    blk("frieze", (W + 0.02, 0.06, 0.22), (0, front - 0.02, 0.30 + H - 0.22), "col_blue", bev=0)
    for k in range(8):
        blk("frieze_pip_%d" % k, (0.10, 0.04, 0.10), (-2.98 + k * 0.85, front - 0.05, 0.30 + H - 0.22), "col_gold", bev=0)
    for k in range(7):
        x = -2.76 + k * 0.92
        _col_window("win_%d" % k, x, front - 0.02, 0.30 + 1.22, w=0.36, h=1.20)
    for k in range(8):
        x = -3.22 + k * 0.92
        blk("pilaster_%d" % k, (0.16, 0.10, H - 0.30), (x, front - 0.03, 0.30 + (H - 0.30) / 2), "col_stone_lt", bev=0.01)
    rz = 0.30 + H + 0.16
    pitch = math.radians(34)
    half = D / 2 + 0.30
    for side in (-1, 1):
        blk("roof_%d" % side, (W + 0.5, half / math.cos(pitch), 0.12), (0, side * half / 2, rz + half * math.tan(pitch) / 2),
            "col_blue", rot=(-side * pitch, 0, 0), bev=0.02)
    blk("ridge", (W + 0.54, 0.16, 0.12), (0, 0, rz + half * math.tan(pitch) + 0.02), "col_blue_dk", bev=0.02)
    for k in range(3):
        t = 0.25 + k * 0.25
        blk("roof_course_%d" % k, (W + 0.52, 0.05, 0.03), (0, -half * (1 - t), rz + half * math.tan(pitch) * t + 0.075),
            "col_blue_dk", bev=0)
    return (7.7, BUILDING_ELEVATION)


def prop_college_gate():
    """The gatehouse on the hamlet's north side: two round towers under blue
    spires, a wall between them with a wide arch in it, the gates standing
    open, a lit window over the arch and the college's banner down each tower."""
    for side in (-1, 1):
        tx = side * 1.85
        cyl("tower_%d" % side, 0.80, 3.0, (tx, 0, 1.5), "col_stone", verts=22)
        cyl("tower_plinth_%d" % side, 0.90, 0.30, (tx, 0, 0.15), "col_stone_dk", verts=22)
        for k in range(3):
            cyl("course_%d_%d" % (side, k), 0.82, 0.07, (tx, 0, 0.9 + k * 0.8), "col_stone_lt", verts=22)
        cyl("tower_top_%d" % side, 0.92, 0.16, (tx, 0, 3.08), "col_stone_lt", verts=22)
        _col_window("tower_win_%d" % side, tx, -0.78, 2.15, w=0.24, h=0.50)
        _col_spire("spire_%d" % side, tx, 0, 3.16, 1.0, 1.45)
        blk("banner_%d" % side, (0.36, 0.04, 1.10), (tx, -0.83, 1.30), "col_blue", bev=0)
        sphere("banner_star_%d" % side, 0.09, (tx, -0.86, 1.45), "col_gold")
        blk("banner_hem_%d" % side, (0.36, 0.05, 0.08), (tx, -0.84, 0.78), "col_gold", bev=0)
    # The wall between, with the arch through it.
    # One block, with the way through it laid on its face in the dark. It was
    # three blocks round a hole, and where they met there were two black bars.
    blk("wall", (2.50, 0.70, 2.80), (0, 0, 1.40), "col_stone")
    cyl("arch_ring", 0.76, 0.10, (0, -0.36, 1.64), "col_stone_lt", rot=(math.radians(90), 0, 0), verts=24)
    cyl("arch_inner", 0.66, 0.12, (0, -0.37, 1.64), "scorch", rot=(math.radians(90), 0, 0), verts=24)
    blk("way_inner", (1.32, 0.12, 1.64), (0, -0.37, 0.82), "scorch", bev=0)
    sphere("keystone", 0.13, (0, -0.42, 2.40), "col_gold", emit=0.5)
    # The gates, open: a leaf folded back against each side of the way.
    for side in (-1, 1):
        blk("gate_%d" % side, (0.10, 0.62, 1.50), (side * 0.60, -0.62, 0.75), "col_door", rot=(0, 0, side * math.radians(12)))
        for k in range(2):
            blk("gate_band_%d_%d" % (side, k), (0.12, 0.64, 0.07), (side * 0.60, -0.62, 0.45 + k * 0.70), "iron",
                rot=(0, 0, side * math.radians(12)), bev=0)
    # Battlements along the wall's top, and a lit window over the arch.
    for k in range(5):
        blk("merlon_%d" % k, (0.30, 0.74, 0.26), (-1.0 + k * 0.5, 0, 2.83), "col_stone_lt")
    _col_window("gate_win", 0, -0.37, 2.62 - 0.30, w=0.30, h=0.30)
    return (6.2, BUILDING_ELEVATION)


def prop_college_fountain():
    """Three tiers of pale stone with water standing in each and falling from
    one to the next, a lit crystal at the top. The middle of the court."""
    cyl("basin_wall", 1.30, 0.34, (0, 0, 0.17), "col_stone", verts=24)
    cyl("basin_rim", 1.36, 0.08, (0, 0, 0.36), "col_stone_lt", verts=24)
    cyl("water_0", 1.18, 0.04, (0, 0, 0.33), "col_water", verts=24, emit=0.35, rough=0.25)
    for k in range(8):
        a = k / 8 * math.tau
        blk("rim_stud_%d" % k, (0.14, 0.14, 0.10), (math.cos(a) * 1.33, math.sin(a) * 1.33, 0.42), "col_gold", rot=(0, 0, a))
    cyl("stem_0", 0.22, 0.60, (0, 0, 0.62), "col_stone", verts=14)
    cyl("bowl_1", 0.74, 0.14, (0, 0, 0.96), "col_stone_lt", verts=20)
    cyl("water_1", 0.64, 0.04, (0, 0, 1.03), "col_water", verts=20, emit=0.35, rough=0.25)
    cyl("stem_1", 0.14, 0.46, (0, 0, 1.26), "col_stone", verts=12)
    cyl("bowl_2", 0.40, 0.12, (0, 0, 1.52), "col_stone_lt", verts=16)
    cyl("water_2", 0.32, 0.04, (0, 0, 1.58), "col_water", verts=16, emit=0.35, rough=0.25)
    # Falling water: pale strips from each bowl's lip to the one below.
    for k in range(6):
        a = k / 6 * math.tau + 0.3
        blk("fall_a_%d" % k, (0.07, 0.07, 0.62), (math.cos(a) * 0.70, math.sin(a) * 0.70, 0.66), "col_water_lt", emit=0.5, bev=0)
    for k in range(4):
        a = k / 4 * math.tau + 0.8
        blk("fall_b_%d" % k, (0.06, 0.06, 0.46), (math.cos(a) * 0.37, math.sin(a) * 0.37, 1.27), "col_water_lt", emit=0.5, bev=0)
    cone("crystal", 0.13, 0.42, (0, 0, 1.86), "crystal", verts=6)
    bpy.context.active_object.data.materials[0] = material("fountain_cr", "crystal", 0.35, 0.0, 1.1)
    return 3.3


def prop_college_statue():
    """A founder in stone on a plinth: robed, hooded, a staff in one hand with
    a lit crystal at its head, a book in the other. Twice a man's height."""
    blk("plinth", (0.78, 0.78, 0.50), (0, 0, 0.25), "col_stone_dk")
    blk("plinth_cap", (0.90, 0.90, 0.10), (0, 0, 0.55), "col_stone_lt")
    blk("plaque", (0.44, 0.03, 0.20), (0, -0.40, 0.28), "col_gold", bev=0)
    cone("robe", 0.42, 1.30, (0, 0, 0.60 + 0.65), "col_stone", verts=14)
    cyl("chest", 0.26, 0.50, (0, 0, 1.55), "col_stone", verts=12)
    sphere("head", 0.19, (0, -0.02, 1.98), "col_stone_lt")
    cone("hood", 0.26, 0.40, (0, 0.04, 2.12), "col_stone", verts=12)
    for side in (-1, 1):
        blk("arm_%d" % side, (0.13, 0.13, 0.50), (side * 0.32, -0.08, 1.50), "col_stone",
            rot=(math.radians(-28), 0, side * math.radians(8)))
    cyl("staff", 0.04, 2.00, (0.44, -0.22, 1.30), "col_stone_dk", verts=8)
    cone("staff_crystal", 0.11, 0.30, (0.44, -0.22, 2.44), "crystal", verts=6)
    bpy.context.active_object.data.materials[0] = material("statue_cr", "crystal", 0.35, 0.0, 1.0)
    blk("book", (0.24, 0.08, 0.30), (-0.34, -0.26, 1.38), "col_blue", rot=(math.radians(-20), 0, 0))
    return 2.9


def prop_college_column():
    """A free-standing column: the court's colonnade is a row of these."""
    _col_column("col", 0, 0, 1.70, radius=0.17)
    sphere("orb", 0.15, (0, 0, 2.08), "col_gold", emit=0.35)
    return 2.9


def prop_college_banner():
    """The college's colours on a standing pole: blue, a gold star, a gold hem."""
    cyl("foot", 0.20, 0.10, (0, 0, 0.05), "col_stone_dk", verts=12)
    cyl("pole", 0.04, 2.10, (0, 0, 1.05), "col_gold", verts=8, metal=0.4, rough=0.45)
    blk("arm", (0.70, 0.05, 0.05), (0, 0, 2.02), "col_gold", bev=0)
    blk("cloth", (0.62, 0.04, 1.30), (0, -0.02, 1.34), "col_blue", bev=0.01)
    blk("cloth_side_l", (0.06, 0.05, 1.30), (-0.30, -0.03, 1.34), "col_blue_dk", bev=0)
    blk("cloth_side_r", (0.06, 0.05, 1.30), (0.30, -0.03, 1.34), "col_blue_dk", bev=0)
    blk("hem", (0.62, 0.06, 0.10), (0, -0.03, 0.72), "col_gold", bev=0)
    # The star: a diamond and a bar, which is what a star is at this size.
    blk("star_a", (0.26, 0.05, 0.26), (0, -0.05, 1.50), "col_gold", rot=(0, math.radians(45), 0), bev=0)
    blk("star_b", (0.40, 0.05, 0.08), (0, -0.06, 1.50), "col_gold", bev=0)
    blk("star_c", (0.08, 0.05, 0.40), (0, -0.06, 1.50), "col_gold", bev=0)
    sphere("finial", 0.08, (0, 0, 2.16), "col_gold")
    return 2.5


def prop_college_lamp():
    """A lamp standard with a lit crystal in a cage: the court is lit at night."""
    cyl("foot", 0.22, 0.12, (0, 0, 0.06), "col_stone_dk", verts=12)
    cyl("post", 0.06, 1.70, (0, 0, 0.95), "iron", verts=8)
    blk("cage_floor", (0.34, 0.34, 0.06), (0, 0, 1.80), "iron")
    blk("glow", (0.22, 0.22, 0.30), (0, 0, 1.98), "col_glass", emit=1.6, bev=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("bar_%d_%d" % (sx, sy), (0.04, 0.04, 0.34), (sx * 0.14, sy * 0.14, 1.98), "iron", bev=0)
    cone("cap", 0.26, 0.24, (0, 0, 2.26), "col_blue", verts=8)
    return 3.0


def prop_hedge():
    """A clipped box hedge, a length of it: the court's beds are edged in these."""
    blk("hedge", (1.60, 0.50, 0.62), (0, 0, 0.33), "hedge", bev=0.06)
    blk("hedge_top", (1.50, 0.40, 0.10), (0, 0, 0.66), "hedge_lt", bev=0.04)
    import random
    rng = random.Random(7)
    for k in range(14):
        blk("leaf_%d" % k, (0.12, 0.04, 0.10), (rng.uniform(-0.72, 0.72), -0.26, rng.uniform(0.12, 0.58)),
            ("hedge_lt", "hedge_dk")[k % 2], bev=0)
    return 1.9


def prop_topiary():
    """A ball of box on a stem in a stone pot."""
    cyl("pot", 0.26, 0.30, (0, 0, 0.15), "col_stone", verts=12)
    cyl("pot_rim", 0.30, 0.07, (0, 0, 0.32), "col_stone_lt", verts=12)
    cyl("stem", 0.05, 0.50, (0, 0, 0.58), "log_dk", verts=8)
    sphere("ball", 0.42, (0, 0, 1.12), "hedge")
    sphere("ball_lt", 0.30, (-0.10, -0.16, 1.26), "hedge_lt")
    return 1.9


def prop_stone_bench():
    blk("seat", (1.30, 0.42, 0.10), (0, 0, 0.42), "col_stone_lt")
    for sx in (-1, 1):
        blk("leg_%d" % sx, (0.18, 0.36, 0.38), (sx * 0.46, 0, 0.19), "col_stone")
    return 1.7


def prop_training_dummy():
    """A straw man on a post with a painted target on his chest, scorched and
    split where the spells land. What the apprentices throw fire at."""
    cyl("foot", 0.34, 0.10, (0, 0, 0.05), "log_dk", verts=12)
    for a in (0, math.pi / 2):
        blk("foot_beam_%d" % int(a * 10), (0.90, 0.12, 0.10), (0, 0, 0.10), "log", rot=(0, 0, a))
    cyl("post", 0.07, 1.30, (0, 0, 0.75), "log", verts=8)
    cyl("body", 0.30, 0.74, (0, 0, 1.12), "dummy_sack", verts=14)
    cyl("belt", 0.31, 0.08, (0, 0, 0.92), "leather", verts=14)
    blk("arms", (1.20, 0.13, 0.13), (0, 0, 1.36), "log")
    for sx in (-1, 1):
        sphere("hand_%d" % sx, 0.12, (sx * 0.62, 0, 1.36), "dummy_sack_dk")
    sphere("head", 0.22, (0, 0, 1.72), "dummy_sack")
    cyl("hat", 0.26, 0.05, (0, 0, 1.90), "dummy_sack_dk", verts=12)
    # The target, and what has been done to it.
    cyl("target_a", 0.20, 0.04, (0, -0.29, 1.14), "paint_red", rot=(math.radians(90), 0, 0), verts=14)
    cyl("target_b", 0.12, 0.05, (0, -0.30, 1.14), "chalk", rot=(math.radians(90), 0, 0), verts=12)
    cyl("target_c", 0.05, 0.06, (0, -0.31, 1.14), "paint_red", rot=(math.radians(90), 0, 0), verts=10)
    blk("scorch_a", (0.16, 0.03, 0.22), (0.16, -0.27, 1.32), "scorch", bev=0)
    blk("scorch_b", (0.12, 0.03, 0.14), (-0.18, -0.27, 0.98), "scorch", bev=0)
    for k in range(5):
        blk("straw_%d" % k, (0.03, 0.03, 0.18), (-0.20 + k * 0.10, -0.06, 0.70), "straw", rot=(math.radians(20), 0, 0), bev=0)
    return 2.6


def prop_college_desk():
    """A student's desk: a sloped top with an open book and an inkwell, and the
    bench that goes with it. The classroom is rows of these."""
    blk("top", (1.10, 0.50, 0.07), (0, -0.05, 0.74), "oak_light", rot=(math.radians(10), 0, 0))
    blk("front", (1.10, 0.06, 0.56), (0, -0.30, 0.42), "oak")
    for sx in (-1, 1):
        blk("side_%d" % sx, (0.07, 0.50, 0.70), (sx * 0.52, -0.05, 0.35), "oak")
    blk("book", (0.40, 0.28, 0.04), (-0.12, -0.06, 0.80), "page", rot=(math.radians(10), 0, 0), bev=0.01)
    blk("book_spine", (0.03, 0.28, 0.05), (-0.12, -0.06, 0.805), "col_blue", rot=(math.radians(10), 0, 0), bev=0)
    cyl("inkwell", 0.06, 0.09, (0.34, 0.04, 0.84), "col_blue_dk", verts=8)
    blk("bench", (1.00, 0.30, 0.07), (0, 0.52, 0.42), "oak_light")
    for sx in (-1, 1):
        blk("bench_leg_%d" % sx, (0.08, 0.26, 0.40), (sx * 0.40, 0.52, 0.20), "oak")
    return 1.8


def prop_college_blackboard():
    """The classroom's board: wide, green, in an oak frame on the wall, chalked
    with a circle of runes and the lines of a working, a ledge of chalk under it."""
    blk("frame", (3.00, 0.10, 1.50), (0, 0, 1.35), "oak")
    blk("board", (2.80, 0.06, 1.30), (0, -0.04, 1.35), "board_green", bev=0)
    # The working: a circle, a star in it, and two lines of writing.
    for k in range(16):
        a = k / 16 * math.tau
        blk("ring_%d" % k, (0.10, 0.03, 0.04), (-0.75 + math.cos(a) * 0.42, -0.08, 1.38 + math.sin(a) * 0.42), "chalk",
            rot=(0, -a + math.pi / 2, 0), bev=0)
    for k in range(3):
        a = k / 3 * math.pi
        blk("star_%d" % k, (0.78, 0.03, 0.03), (-0.75, -0.08, 1.38), "chalk", rot=(0, a, 0), bev=0)
    for row in range(4):
        for k in range(5 - row % 2):
            blk("word_%d_%d" % (row, k), (0.20, 0.03, 0.05), (0.05 + k * 0.27, -0.08, 1.78 - row * 0.24), "chalk", bev=0)
    blk("ledge", (2.80, 0.16, 0.06), (0, -0.10, 0.66), "oak_light")
    for k in range(3):
        blk("chalk_%d" % k, (0.12, 0.04, 0.04), (-0.9 + k * 0.5, -0.12, 0.71), "chalk", bev=0)
    return 3.5


def prop_council_table():
    """The council's table: a long oval of dark oak under a blue cloth with a
    gold edge, a branch of candles, papers, and the college's orb at its head."""
    cyl("top", 1.0, 0.10, (0, 0, 0.78), "oak", verts=28)
    bpy.context.active_object.scale = (1.75, 0.80, 1.0)
    cyl("cloth", 1.0, 0.03, (0, 0, 0.845), "col_blue", verts=28)
    bpy.context.active_object.scale = (1.55, 0.62, 1.0)
    cyl("cloth_edge", 1.0, 0.02, (0, 0, 0.84), "col_gold", verts=28)
    bpy.context.active_object.scale = (1.62, 0.68, 1.0)
    for sx in (-1, 1):
        cyl("leg_%d" % sx, 0.16, 0.74, (sx * 1.05, 0, 0.37), "oak", verts=10)
        cyl("foot_%d" % sx, 0.30, 0.08, (sx * 1.05, 0, 0.04), "oak", verts=12)
    for k in range(3):
        cyl("candle_%d" % k, 0.035, 0.26, (-0.30 + k * 0.30, 0.05, 1.00), "candle", verts=8)
        sphere("flame_%d" % k, 0.05, (-0.30 + k * 0.30, 0.05, 1.18), "flame", emit=1.6)
    blk("candle_base", (0.80, 0.10, 0.04), (0, 0.05, 0.88), "brass_lt")
    for k, (x, y) in enumerate(((-1.0, -0.20), (0.95, -0.15), (-0.55, 0.25), (0.60, 0.28))):
        blk("paper_%d" % k, (0.26, 0.20, 0.015), (x, y, 0.87), "page", rot=(0, 0, 0.3 * k), bev=0)
    sphere("orb", 0.13, (1.20, 0.05, 1.00), "col_glass", emit=1.2)
    cyl("orb_stand", 0.08, 0.10, (1.20, 0.05, 0.90), "brass_lt", verts=10)
    return 3.9


def _high_chair(back_to_camera):
    blk("seat", (0.50, 0.46, 0.08), (0, 0, 0.46), "oak")
    blk("cushion", (0.44, 0.40, 0.06), (0, 0, 0.53), "col_blue")
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.07, 0.07, 0.44), (sx * 0.20, sy * 0.18, 0.22), "oak")
    y = -0.22 if back_to_camera else 0.22
    blk("back", (0.50, 0.07, 0.95), (0, y, 0.98), "oak")
    blk("back_pad", (0.36, 0.03, 0.60), (0, y + (0.04 if back_to_camera else -0.04), 1.02), "col_blue", bev=0.01)
    blk("back_top", (0.56, 0.09, 0.10), (0, y, 1.48), "col_gold", bev=0.02)
    return 1.9


def prop_high_chair():
    return _high_chair(False)


def prop_high_chair_back():
    return _high_chair(True)


def prop_college_orrery():
    """Brass rings turning about a lit sun on a stand: the heavens, as the
    college has them. It is what a lecture hall has where an inn has a keg."""
    cyl("foot", 0.34, 0.08, (0, 0, 0.04), "oak", verts=14)
    cyl("stand", 0.06, 0.80, (0, 0, 0.46), "brass_lt", verts=8, metal=0.5, rough=0.4)
    sphere("sun", 0.20, (0, 0, 1.20), "flame", emit=1.5)
    for k, (r, tilt) in enumerate(((0.46, 18), (0.64, -24), (0.82, 10))):
        bpy.ops.mesh.primitive_torus_add(major_radius=r, minor_radius=0.025, location=(0, 0, 1.20),
                                         rotation=(math.radians(68 + tilt), 0, math.radians(k * 40)))
        ob = bpy.context.active_object
        ob.name = "ring_%d" % k
        ob.data.materials.append(material("ring_%d" % k, "brass_lt", 0.4, 0.5, 0.0))
        a = k * 2.1 + 0.6
        sphere("world_%d" % k, 0.08, (math.cos(a) * r, math.sin(a) * r * 0.4, 1.20 + math.sin(a) * r * 0.5),
               ("col_blue_lt", "paint_red", "hedge_lt")[k])
    return 2.9


def prop_crystal_pylon():
    """A tall crystal in a stone socket with a ring of lit runes at its foot:
    the training hall draws on these, and they are why nobody runs dry in it."""
    cyl("socket", 0.40, 0.24, (0, 0, 0.12), "col_stone", verts=14)
    cyl("rune_ring", 0.42, 0.05, (0, 0, 0.20), "col_glass", verts=14, emit=1.0)
    cone("shard", 0.24, 1.50, (0, 0, 1.00), "crystal", verts=6)
    bpy.context.active_object.data.materials[0] = material("pylon_cr", "crystal", 0.30, 0.0, 1.1)
    for side in (-1, 1):
        cone("shard_%d" % side, 0.13, 0.70, (side * 0.22, 0.05, 0.55), "crystal", verts=6, rot=(0, side * math.radians(16), 0))
        bpy.context.active_object.data.materials[0] = material("pylon_cr_s", "crystal", 0.30, 0.0, 0.8)
    return 2.2


COLLEGE_PROPS = {
    "college_hall":       (prop_college_hall, 352),
    "college_gate":       (prop_college_gate, 224),
    "college_wing":       (prop_college_wing, 256),
    "college_fountain":   (prop_college_fountain, 112),
    "college_statue":     (prop_college_statue, 96),
    "college_column":     (prop_college_column, 64),
    "college_banner":     (prop_college_banner, 72),
    "college_lamp":       (prop_college_lamp, 72),
    "hedge":              (prop_hedge, 56),
    "topiary":            (prop_topiary, 56),
    "stone_bench":        (prop_stone_bench, 56),
    "training_dummy":     (prop_training_dummy, 64),
    "college_desk":       (prop_college_desk, 56),
    "college_blackboard": (prop_college_blackboard, 112),
    "council_table":      (prop_council_table, 128),
    "high_chair":         (prop_high_chair, 48),
    "high_chair_back":    (prop_high_chair_back, 48),
    "college_orrery":     (prop_college_orrery, 64),
    "crystal_pylon":      (prop_crystal_pylon, 64),
}


# -----------------------------------------------------------------------------
#  Wynn's, at Mossvale
#
#  She kept a stall on the square, a loom standing out in the weather beside
#  it, within earshot of the anvil. She has a shop now: a timbered house with a
#  window full of what she makes, and inside it the loom, the wheel, forms
#  dressed in her work, bolts on the shelves and hangings on the walls.
# -----------------------------------------------------------------------------

PALETTE.update({
    "form_wood":    (0.780, 0.640, 0.460),
    "fab_blue":     (0.240, 0.360, 0.660),
    "fab_red":      (0.700, 0.200, 0.220),
    "fab_green":    (0.260, 0.520, 0.320),
    "fab_gold":     (0.860, 0.700, 0.300),
    "fab_purple":   (0.480, 0.300, 0.620),
    "fab_cream":    (0.920, 0.880, 0.780),
    "fab_teal":     (0.240, 0.580, 0.600),
    "fab_rose":     (0.860, 0.520, 0.580),
    "shop_plaster": (0.900, 0.860, 0.760),
    "shop_timber":  (0.340, 0.230, 0.160),
    "awning_blue":  (0.300, 0.440, 0.720),
})


def _mannequin(garment, trim, kind):
    """A dressmaker's form on a turned stand: a padded torso, no head but a
    knob, dressed in something of Wynn's. `kind` is what: a robe to the floor,
    a dress with a full skirt, or a cloak and a pointed hat."""
    cyl("foot", 0.26, 0.06, (0, 0, 0.03), "oak", verts=12)
    cyl("stand", 0.04, 0.70, (0, 0, 0.38), "oak", verts=8)
    if kind == "robe":
        cone("skirt", 0.40, 1.10, (0, 0, 0.62), garment, verts=14)
        cyl("torso", 0.23, 0.52, (0, 0, 1.30), garment, verts=12)
        blk("sash", (0.50, 0.40, 0.08), (0, 0, 1.08), trim, bev=0.02)
        blk("front", (0.08, 0.04, 0.95), (0, -0.24, 0.95), trim, bev=0)
        for sx in (-1, 1):
            blk("sleeve_%d" % sx, (0.14, 0.16, 0.56), (sx * 0.30, 0, 1.22), garment, rot=(0, sx * math.radians(-12), 0))
    elif kind == "dress":
        cone("skirt", 0.52, 0.95, (0, 0, 0.56), garment, verts=16)
        cyl("hem", 0.50, 0.07, (0, 0, 0.12), trim, verts=16)
        cyl("torso", 0.20, 0.50, (0, 0, 1.28), garment, verts=12)
        cyl("waist", 0.17, 0.10, (0, 0, 1.02), trim, verts=12)
        for sx in (-1, 1):
            sphere("puff_%d" % sx, 0.13, (sx * 0.26, 0, 1.44), garment)
    else:
        cone("cloak", 0.44, 1.20, (0, 0.04, 0.72), garment, verts=14)
        cyl("torso", 0.21, 0.46, (0, -0.02, 1.30), "fab_cream", verts=12)
        blk("clasp", (0.12, 0.05, 0.10), (0, -0.23, 1.46), trim, bev=0.02)
        blk("cloak_edge_l", (0.07, 0.05, 1.00), (-0.20, -0.24, 0.84), trim, rot=(0, math.radians(8), 0), bev=0)
        blk("cloak_edge_r", (0.07, 0.05, 1.00), (0.20, -0.24, 0.84), trim, rot=(0, math.radians(-8), 0), bev=0)
    cyl("neck", 0.07, 0.12, (0, 0, 1.60), "form_wood", verts=8)
    sphere("knob", 0.10, (0, 0, 1.72), "form_wood")
    if kind == "cloak":
        cyl("hat_brim", 0.30, 0.04, (0, 0, 1.80), garment, verts=14)
        cone("hat", 0.20, 0.50, (0, 0, 2.06), garment, verts=12)
        cyl("hat_band", 0.205, 0.07, (0, 0, 1.86), trim, verts=12)
    return 2.5


def prop_mannequin_robe():  return _mannequin("fab_blue", "fab_gold", "robe")
def prop_mannequin_dress(): return _mannequin("fab_red", "fab_cream", "dress")
def prop_mannequin_cloak(): return _mannequin("fab_green", "fab_gold", "cloak")


def _tapestry(field, border, motif):
    """A hanging on a rod: a field of one colour in a border of another with a
    device worked in the middle, and a fringe. For a wall."""
    cyl("rod", 0.035, 1.30, (0, 0, 1.92), "brass", rot=(0, math.radians(90), 0), verts=8)
    for sx in (-1, 1):
        sphere("rod_end_%d" % sx, 0.07, (sx * 0.66, 0, 1.92), "brass")
    blk("border", (1.10, 0.04, 1.50), (0, -0.01, 1.14), border, bev=0)
    blk("field", (0.86, 0.05, 1.22), (0, -0.02, 1.16), field, bev=0)
    if motif == "star":
        blk("m_a", (0.34, 0.06, 0.34), (0, -0.03, 1.20), border, rot=(0, math.radians(45), 0), bev=0)
        blk("m_b", (0.56, 0.06, 0.10), (0, -0.04, 1.20), border, bev=0)
        blk("m_c", (0.10, 0.06, 0.56), (0, -0.04, 1.20), border, bev=0)
    elif motif == "tree":
        blk("trunk", (0.10, 0.06, 0.50), (0, -0.03, 0.92), border, bev=0)
        for k, (w, z) in enumerate(((0.60, 1.20), (0.44, 1.38), (0.26, 1.54))):
            blk("bough_%d" % k, (w, 0.06, 0.16), (0, -0.04, z), border, bev=0)
    else:
        for k in range(4):
            blk("chev_%d" % k, (0.62, 0.06, 0.09), (0, -0.03, 0.78 + k * 0.26), border,
                rot=(0, math.radians(18 if k % 2 else -18), 0), bev=0)
    for k in range(8):
        blk("fringe_%d" % k, (0.06, 0.03, 0.14), (-0.49 + k * 0.14, -0.01, 0.34), "fab_gold", bev=0)
    return 2.3


def prop_tapestry_blue():  return _tapestry("fab_blue", "fab_gold", "star")
def prop_tapestry_red():   return _tapestry("fab_red", "fab_cream", "chevron")
def prop_tapestry_green(): return _tapestry("fab_green", "fab_gold", "tree")


def prop_fabric_shelf():
    """Bolts of cloth end-on in a rack against the wall, every colour she dyes,
    which is what makes a room a draper's at a glance."""
    blk("back", (1.90, 0.10, 1.70), (0, 0.22, 0.85), "oak")
    for sx in (-1, 1):
        blk("side_%d" % sx, (0.08, 0.50, 1.70), (sx * 0.95, 0, 0.85), "oak")
    colours = ("fab_blue", "fab_red", "fab_green", "fab_gold", "fab_purple", "fab_cream", "fab_teal", "fab_rose")
    for row in range(4):
        z = 0.10 + row * 0.42
        blk("shelf_%d" % row, (1.90, 0.50, 0.06), (0, 0, z), "oak_light")
        for k in range(6):
            c = colours[(row * 3 + k * 5) % len(colours)]
            cyl("bolt_%d_%d" % (row, k), 0.135, 0.46, (-0.75 + k * 0.30, -0.02, z + 0.18), c,
                rot=(math.radians(90), 0, 0), verts=12)
            cyl("bolt_core_%d_%d" % (row, k), 0.04, 0.47, (-0.75 + k * 0.30, -0.025, z + 0.18), "oak_pale",
                rot=(math.radians(90), 0, 0), verts=8)
    blk("top", (2.00, 0.56, 0.08), (0, 0, 1.74), "oak")
    return 2.6


def prop_fabric_rolls():
    """A tub of rolls stood on end, a length pulled out of one and over the side."""
    cyl("tub", 0.42, 0.50, (0, 0, 0.25), "oak", verts=14)
    cyl("tub_band", 0.43, 0.06, (0, 0, 0.40), "iron", verts=14)
    rolls = (("fab_blue", -0.18, -0.08, 1.20), ("fab_red", 0.16, -0.10, 1.05), ("fab_gold", 0.0, 0.14, 1.32),
             ("fab_green", -0.20, 0.16, 0.95), ("fab_purple", 0.22, 0.12, 1.12))
    for k, (c, x, y, h) in enumerate(rolls):
        cyl("roll_%d" % k, 0.12, h, (x, y, 0.20 + h / 2), c, verts=10, rot=(math.radians(4 * (k - 2)), 0, 0))
        cyl("roll_core_%d" % k, 0.035, 0.04, (x, y, 0.22 + h), "oak_pale", verts=8)
    blk("drape", (0.22, 0.03, 0.62), (0.16, -0.43, 0.42), "fab_red", bev=0.01)
    return 1.9


def prop_cutting_table():
    """Her cutting table: a length of blue laid out across it, the shears, a
    tape, a pincushion and chalk."""
    blk("top", (2.00, 0.90, 0.08), (0, 0, 0.80), "oak_light")
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.10, 0.10, 0.76), (sx * 0.88, sy * 0.36, 0.38), "oak")
    blk("rail", (1.80, 0.06, 0.10), (0, -0.40, 0.70), "oak")
    blk("cloth", (1.30, 0.70, 0.025), (-0.20, 0, 0.855), "fab_blue", bev=0.01)
    blk("cloth_hang", (1.30, 0.03, 0.34), (-0.20, -0.46, 0.68), "fab_blue", bev=0.01)
    blk("cloth_edge", (1.30, 0.06, 0.03), (-0.20, -0.33, 0.87), "fab_gold", bev=0)
    # Shears: two blades and two bows.
    blk("blade_a", (0.34, 0.05, 0.02), (0.66, 0.06, 0.87), "iron_light", rot=(0, 0, math.radians(18)), bev=0)
    blk("blade_b", (0.34, 0.05, 0.02), (0.66, 0.06, 0.875), "iron_light", rot=(0, 0, math.radians(-18)), bev=0)
    for sy in (-1, 1):
        cyl("bow_%d" % sy, 0.06, 0.02, (0.86, 0.06 + sy * 0.08, 0.87), "iron", verts=10)
    sphere("pincushion", 0.09, (0.60, -0.24, 0.90), "fab_red")
    blk("chalk", (0.12, 0.05, 0.03), (0.84, -0.22, 0.86), "chalk", bev=0)
    blk("tape", (0.60, 0.04, 0.015), (0.10, 0.30, 0.875), "fab_cream", rot=(0, 0, math.radians(-8)), bev=0)
    return 2.6


def prop_clothier_shop():
    """Wynn's shop: a timbered house under a steep tiled roof, a wide window of
    small panes with three gowns standing in it, a striped awning over that, a
    painted door, and a hanging sign with a spool on it."""
    W, D, H = 3.6, 2.2, 1.85
    front = -D / 2
    blk("plinth", (W + 0.12, D + 0.12, 0.22), (0, 0, 0.11), "stone")
    blk("body", (W, D, H), (0, 0, 0.22 + H / 2), "shop_plaster", bev=0.02)
    # Timber framing on the front.
    for x in (-W / 2 + 0.06, -0.52, 0.52, W / 2 - 0.06):
        blk("post_%s" % x, (0.12, 0.06, H), (x, front - 0.02, 0.22 + H / 2), "shop_timber", bev=0)
    for z in (0.22 + 0.06, 0.22 + H - 0.06):
        blk("beam_%s" % z, (W, 0.06, 0.12), (0, front - 0.02, z), "shop_timber", bev=0)
    # The door is in the middle, because the way in is: the first front had it
    # to one side of a single wide window, and the way in was through the glass.
    # A window either side of it, in small panes, lit, with gowns stood in each
    # and a striped awning over.
    wz, ww, wh = 0.22 + 0.92, 1.00, 0.95
    gowns = (("fab_blue", "fab_gold"), ("fab_red", "fab_green"))
    for side, pair in zip((-1, 1), gowns):
        wx = side * 1.15
        tag = "l" if side < 0 else "r"
        blk("win_frame_" + tag, (ww + 0.14, 0.08, wh + 0.14), (wx, front - 0.02, wz), "shop_timber")
        blk("win_glass_" + tag, (ww, 0.05, wh), (wx, front - 0.05, wz), "glass_lit", emit=0.7, rough=0.3, bev=0)
        for k, c in enumerate(pair):
            cone("gown_%s_%d" % (tag, k), 0.18, 0.62, (wx - 0.24 + k * 0.48, front - 0.09, wz - 0.12), c, verts=10)
            sphere("gown_head_%s_%d" % (tag, k), 0.07, (wx - 0.24 + k * 0.48, front - 0.09, wz + 0.26), "form_wood")
        blk("mullion_" + tag, (0.03, 0.04, wh), (wx, front - 0.10, wz), "shop_timber", bev=0)
        blk("transom_" + tag, (ww, 0.04, 0.03), (wx, front - 0.10, wz + 0.10), "shop_timber", bev=0)
        blk("sill_" + tag, (ww + 0.24, 0.16, 0.06), (wx, front - 0.08, wz - wh / 2 - 0.08), "stone_pale")
        for k in range(4):
            blk("awning_%s_%d" % (tag, k), (0.30, 0.62, 0.05), (wx - 0.45 + k * 0.30, front - 0.34, wz + wh / 2 + 0.22),
                ("awning_blue", "fab_cream")[k % 2], rot=(math.radians(-26), 0, 0), bev=0)
    dx = 0.0
    blk("door_frame", (0.80, 0.08, 1.36), (dx, front - 0.02, 0.22 + 0.68), "shop_timber")
    blk("door", (0.64, 0.06, 1.24), (dx, front - 0.05, 0.22 + 0.62), "awning_blue")
    for z in (0.22 + 0.30, 0.22 + 0.86):
        blk("door_panel_%s" % z, (0.44, 0.03, 0.40), (dx, front - 0.09, z), "col_blue_dk", bev=0.01)
    sphere("knob", 0.045, (dx + 0.22, front - 0.10, 0.22 + 0.62), "brass")
    blk("step", (1.0, 0.36, 0.10), (dx, front - 0.20, 0.05), "stone_pale")
    # The sign over the door: a bracket, a board, and a spool of gold thread on it.
    blk("bracket", (0.06, 0.56, 0.06), (dx, front - 0.28, 0.22 + 1.78), "iron")
    blk("board", (0.50, 0.05, 0.30), (dx, front - 0.50, 0.22 + 1.58), "shop_timber")
    cyl("spool", 0.09, 0.06, (dx, front - 0.54, 0.22 + 1.58), "fab_gold", rot=(math.radians(90), 0, 0), verts=12)
    # A small lit window upstairs in the gable, and the roof.
    rz = 0.22 + H
    rise = gable_roof("roof", W, D, rz, 44, "roof_tile", thick=0.12, overhang=0.26)
    # A dormer in the front slope, with a lit window: the ridge runs across,
    # so there is no gable to the street to put one in. (Three stepped blocks
    # of plaster were tried for a gable, and were a ziggurat on a roof.)
    dz = rz + 0.42
    blk("dormer", (0.80, 0.70, 0.62), (0, front + 0.30, dz), "shop_plaster", bev=0.02)
    for side in (-1, 1):
        blk("dormer_roof_%d" % side, (0.58, 0.86, 0.08), (side * 0.22, front + 0.28, dz + 0.42), "roof_tile",
            rot=(0, side * math.radians(34), 0), bev=0.01)
    window("win_dormer", 0, front - 0.06, dz - 0.02, w=0.34, h=0.34, lit=True, shutters=False)
    blk("chimney", (0.36, 0.36, 1.00), (W / 2 - 0.60, 0.40, rz + rise * 0.55 + 0.30), "brick")
    blk("chimney_cap", (0.44, 0.44, 0.08), (W / 2 - 0.60, 0.40, rz + rise * 0.55 + 0.84), "stone_pale")
    return (5.4, BUILDING_ELEVATION)


CLOTHIER_PROPS = {
    "clothier_shop":   (prop_clothier_shop, 192),
    "mannequin_robe":  (prop_mannequin_robe, 64),
    "mannequin_dress": (prop_mannequin_dress, 64),
    "mannequin_cloak": (prop_mannequin_cloak, 64),
    "tapestry_blue":   (prop_tapestry_blue, 64),
    "tapestry_red":    (prop_tapestry_red, 64),
    "tapestry_green":  (prop_tapestry_green, 64),
    "fabric_shelf":    (prop_fabric_shelf, 80),
    "fabric_rolls":    (prop_fabric_rolls, 56),
    "cutting_table":   (prop_cutting_table, 80),
}


# -----------------------------------------------------------------------------
#  The swamp, the Ice Spire, the Ashen Path and the inn's cellar
# -----------------------------------------------------------------------------

PALETTE.update({
    "reed":        (0.455, 0.522, 0.263),
    "reed_dk":     (0.329, 0.388, 0.200),
    "cattail":     (0.431, 0.286, 0.176),
    "lily":        (0.318, 0.529, 0.286),
    "lily_flower": (0.965, 0.827, 0.878),
    "bog_bark":    (0.349, 0.310, 0.255),
    "swamp_moss":  (0.494, 0.553, 0.357),
    "hut_thatch":  (0.557, 0.490, 0.314),
    "hut_wood":    (0.400, 0.318, 0.231),
    "bone_white":  (0.890, 0.855, 0.769),
    "paint_red":   (0.690, 0.200, 0.157),
    "ice_blue":    (0.620, 0.855, 0.945),
    "ice_deep":    (0.345, 0.620, 0.820),
    "ice_pale":    (0.870, 0.960, 1.000),
    "snow_white":  (0.930, 0.955, 0.980),
    "pine":        (0.216, 0.369, 0.318),
    "pine_dk":     (0.141, 0.259, 0.227),
    "twig":        (0.412, 0.337, 0.255),
    "egg":         (0.690, 0.800, 0.840),
    "char":        (0.176, 0.153, 0.149),
    "char_lt":     (0.302, 0.259, 0.243),
    "obsidian":    (0.137, 0.118, 0.157),
    "obsidian_lt": (0.290, 0.259, 0.337),
    "hell_red":    (0.620, 0.157, 0.110),
    "cobweb":      (0.900, 0.900, 0.900),
})


def prop_reeds():
    """A clump of reeds and bulrushes: the brown cigar heads are what make it a
    swamp plant rather than tall grass."""
    import random
    rng = random.Random(31)
    for k in range(22):
        a = rng.uniform(0, math.tau)
        r = rng.uniform(0.0, 0.30)
        h = rng.uniform(0.50, 0.95)
        x, y = math.cos(a) * r, math.sin(a) * r
        lean = rng.uniform(-0.18, 0.18)
        cyl("blade_%d" % k, 0.035, h, (x + lean * 0.5, y, h / 2), "reed" if k % 3 else "reed_dk",
            rot=(0, lean, 0), verts=6)
        if k % 3 == 0:
            cyl("head_%d" % k, 0.07, 0.22, (x + lean, y, h + 0.02), "cattail", verts=8)
    return 1.25


def prop_lily_pads():
    """Pads floating on open water, one flowering. Flat, so it lies on the bog
    without looking stood on it."""
    import random
    rng = random.Random(32)
    for k in range(6):
        a = k * 1.1
        r = 0.1 + k * 0.08
        x, y = math.cos(a) * r, math.sin(a) * r * 0.7
        s = rng.uniform(0.14, 0.22)
        cyl("pad_%d" % k, s, 0.015, (x, y, 0.01), "lily", verts=16)
        blk("notch_%d" % k, (s * 0.5, 0.03, 0.03), (x + s * 0.5, y, 0.02), "reed_dk", bev=0)
    sphere("flower", 0.07, (0.02, -0.04, 0.06), "lily_flower")
    return (1.1, 60.0)


def prop_swamp_tree():
    """A drowned tree: a leaning trunk, bare crooked limbs, and moss hanging
    from them in ragged curtains."""
    import random
    rng = random.Random(33)
    cyl("trunk", 0.16, 1.5, (0.0, 0.0, 0.75), "bog_bark", rot=(0.0, 0.10, 0.0), verts=10)
    for k in range(4):
        cyl("root_%d" % k, 0.07, 0.5, (math.cos(k * 1.6) * 0.22, math.sin(k * 1.6) * 0.22, 0.10), "bog_bark",
            rot=(math.sin(k * 1.6) * 1.0, -math.cos(k * 1.6) * 1.0, 0), verts=8)
    tips = []
    for k in range(5):
        a = k / 5 * math.tau + 0.4
        ln = rng.uniform(0.5, 0.8)
        z = 1.2 + k * 0.08
        x, y = math.cos(a) * ln * 0.5, math.sin(a) * ln * 0.5
        cyl("limb_%d" % k, 0.05, ln, (x + 0.07, y, z + 0.15), "bog_bark",
            rot=(math.sin(a) * -0.9, math.cos(a) * 0.9, 0), verts=6)
        tips.append((x * 2 + 0.07, y * 2, z + 0.3))
    for k, (x, y, z) in enumerate(tips):
        for m in range(3):
            h = rng.uniform(0.25, 0.5)
            blk("moss_%d_%d" % (k, m), (0.05, 0.03, h), (x + (m - 1) * 0.06, y, z - h / 2), "swamp_moss", bev=0.01)
    return 2.7


def prop_lizard_hut():
    """The lizardmen's hut, up on stilts out of the water: a round platform of
    poles, a woven wall, a shaggy cone of reed thatch, and a ladder down."""
    for k in range(6):
        a = k / 6 * math.tau
        cyl("stilt_%d" % k, 0.06, 0.7, (math.cos(a) * 0.62, math.sin(a) * 0.62, 0.35), "hut_wood", verts=8)
    cyl("platform", 0.85, 0.10, (0, 0, 0.72), "hut_wood", verts=20)
    cyl("wall", 0.66, 0.62, (0, 0, 1.08), "twig", verts=20)
    blk("door", (0.30, 0.06, 0.46), (0, -0.66, 1.00), "char", bev=0)
    bpy.ops.mesh.primitive_cone_add(radius1=0.98, radius2=0.04, depth=0.95, location=(0, 0, 1.82), vertices=20)
    roof = bpy.context.active_object
    roof.data.materials.append(material("roof", "hut_thatch", 0.95))
    for k in range(12):
        a = k / 12 * math.tau
        cone("fringe_%d" % k, 0.12, 0.22, (math.cos(a) * 0.9, math.sin(a) * 0.9, 1.30), "hut_thatch",
             rot=(math.pi, 0, 0), verts=5)
    for k in range(5):
        blk("rung_%d" % k, (0.34, 0.04, 0.04), (0, -0.98, 0.12 + k * 0.14), "hut_wood", bev=0)
    for sx in (-1, 1):
        blk("rail_%d" % sx, (0.04, 0.04, 0.78), (sx * 0.17, -0.98, 0.38), "hut_wood", bev=0)
    sphere("skull", 0.09, (0, -0.70, 1.40), "bone_white")
    return (2.6, BUILDING_ELEVATION)


def prop_bayou_hut(great=False):
    """A lizardman hut built on a deck rather than up its own legs: the same
    woven wall and shaggy reed cone as the Mire's huts, sat flat on the boards
    of a stilt village in the Bayou. The stilts are the deck's, not the hut's --
    a hut on legs stood on a deck on legs is a hut on stilts on stilts.

    `great` is the Mother of the Fen's: wider, a second tier of thatch, bone
    charms strung round the eaves and a pair of skulls either side of the door."""
    r = 0.86 if great else 0.66
    cyl("sill", r + 0.10, 0.08, (0, 0, 0.04), "hut_wood", verts=20)
    cyl("wall", r, 0.66, (0, 0, 0.41), "twig", verts=20)
    # Uprights in the weave, so the wall reads as built of poles and not as a
    # brown drum.
    for k in range(10 if great else 8):
        a = k / (10 if great else 8) * math.tau
        cyl("pole_%d" % k, 0.035, 0.72, (math.cos(a) * r, math.sin(a) * r, 0.40), "hut_wood", verts=6)
    blk("door", (0.34 if great else 0.28, 0.07, 0.50), (0, -r - 0.005, 0.32), "char", bev=0)
    blk("lintel", (0.46 if great else 0.38, 0.08, 0.07), (0, -r - 0.01, 0.60), "hut_wood", bev=0)
    bpy.ops.mesh.primitive_cone_add(radius1=r + 0.36, radius2=0.04, depth=0.95 if great else 0.86,
                                    location=(0, 0, 1.18 if great else 1.12), vertices=20)
    roof = bpy.context.active_object
    roof.data.materials.append(material("roof", "hut_thatch", 0.95))
    fringe = 14 if great else 12
    for k in range(fringe):
        a = k / fringe * math.tau
        cone("fringe_%d" % k, 0.12, 0.22, (math.cos(a) * (r + 0.26), math.sin(a) * (r + 0.26), 0.66),
             "hut_thatch", rot=(math.pi, 0, 0), verts=5)
    if great:
        # A second, smaller cone over the first: the house of somebody who
        # matters, seen from across the water.
        bpy.ops.mesh.primitive_cone_add(radius1=0.52, radius2=0.03, depth=0.52, location=(0, 0, 1.78), vertices=16)
        top = bpy.context.active_object
        top.data.materials.append(material("roof_top", "reed_dk", 0.95))
        for k in range(8):
            a = k / 8 * math.tau + 0.2
            sphere("charm_%d" % k, 0.055, (math.cos(a) * (r + 0.30), math.sin(a) * (r + 0.30), 0.60), "bone_white")
        for sx in (-1, 1):
            cyl("stake_%d" % sx, 0.035, 0.70, (sx * 0.34, -r - 0.10, 0.35), "hut_wood", verts=6)
            sphere("skull_%d" % sx, 0.09, (sx * 0.34, -r - 0.10, 0.74), "bone_white")
        blk("paint", (0.50, 0.03, 0.06), (0, -r - 0.02, 0.70), "paint_red", bev=0)
    else:
        sphere("skull", 0.08, (0, -r - 0.03, 0.72), "bone_white")
    return (3.1 if great else 2.3, BUILDING_ELEVATION)


def prop_bayou_piling():
    """A stilt under a Bayou deck: a thick post driven into the bog, bound with
    rope near the top and green with slime where the water has been, and a dark
    ring of wet round its foot. It stands under the front edge of a deck, where
    a deck on stilts is seen from; the ones behind are under the boards."""
    cyl("post", 0.13, 1.10, (0, 0, 0.55), "hut_wood", verts=10)
    cyl("cap", 0.145, 0.05, (0, 0, 1.10), "bog_bark", verts=10)
    for k, z in enumerate((0.86, 0.96)):
        cyl("rope_%d" % k, 0.14, 0.035, (0, 0, z), "cattail", verts=10)
    # Slime to the tide line, and the water standing round the foot.
    cyl("slime", 0.136, 0.34, (0, 0, 0.17), "swamp_moss", verts=10)
    cyl("wet", 0.30, 0.012, (0, 0, 0.006), (0.10, 0.14, 0.12), verts=16)
    cyl("ring", 0.22, 0.014, (0, 0, 0.008), (0.30, 0.38, 0.34), verts=16)
    return 1.6


def prop_lizard_totem():
    """A carved post with a lizard skull on top and red painted bands."""
    cyl("post", 0.12, 1.3, (0, 0, 0.65), "hut_wood", verts=10)
    for k, z in enumerate((0.35, 0.75)):
        cyl("band_%d" % k, 0.13, 0.08, (0, 0, z), "paint_red", verts=10)
    sphere("skull", 0.16, (0, -0.02, 1.38), "bone_white")
    blk("snout", (0.12, 0.22, 0.09), (0, -0.16, 1.34), "bone_white", bev=0.03)
    for sx in (-1, 1):
        cone("horn_%d" % sx, 0.04, 0.24, (sx * 0.12, 0.02, 1.52), "bone_white", rot=(0, sx * 0.5, 0), verts=6)
        blk("feather_%d" % sx, (0.03, 0.02, 0.28), (sx * 0.16, 0.02, 1.05), "paint_red", rot=(0, sx * 0.35, 0), bev=0)
    return 1.9


def ice_shard(name, h, r, loc, lean, colour):
    bpy.ops.mesh.primitive_cone_add(radius1=r, radius2=0.0, depth=h, location=(loc[0], loc[1], loc[2] + h / 2),
                                    vertices=6)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = lean
    ob.data.materials.append(material(name, colour, 0.25, 0.0, 0.15))
    for p in ob.data.polygons:
        p.use_smooth = False
    return ob


def prop_ice_spire():
    """A spire of blue ice pushing out of the snow, with lesser shards round
    its foot. Faceted and flat shaded, so every face is its own band."""
    import random
    rng = random.Random(34)
    ice_shard("spire", 2.6, 0.42, (0, 0, 0), (0.05, 0.04, 0), "ice_blue")
    ice_shard("spire_core", 2.0, 0.26, (0.08, -0.12, 0), (0.03, -0.02, 0.4), "ice_pale")
    for k in range(7):
        a = k / 7 * math.tau + rng.uniform(-0.2, 0.2)
        d = rng.uniform(0.38, 0.62)
        ice_shard("shard_%d" % k, rng.uniform(0.5, 1.2), rng.uniform(0.12, 0.2), (math.cos(a) * d, math.sin(a) * d, 0),
                  (math.sin(a) * 0.35, -math.cos(a) * 0.35, 0), "ice_deep" if k % 2 else "ice_blue")
    cyl("drift", 0.8, 0.12, (0, 0, 0.04), "snow_white", verts=18)
    return 3.2


def prop_ice_crystal():
    import random
    rng = random.Random(35)
    for k in range(4):
        a = k / 4 * math.tau + 0.3
        ice_shard("c_%d" % k, rng.uniform(0.35, 0.7), 0.10, (math.cos(a) * 0.12, math.sin(a) * 0.12, 0),
                  (math.sin(a) * 0.3, -math.cos(a) * 0.3, 0), "ice_blue" if k % 2 else "ice_pale")
    cyl("drift", 0.28, 0.06, (0, 0, 0.02), "snow_white", verts=12)
    return 1.1


def prop_snow_pine():
    """A fir heavy with snow: tiers of dark needles with white laid on top."""
    cyl("trunk", 0.09, 0.5, (0, 0, 0.25), "bog_bark", verts=8)
    for k in range(4):
        z = 0.45 + k * 0.42
        r = 0.78 - k * 0.17
        bpy.ops.mesh.primitive_cone_add(radius1=r, radius2=0.0, depth=0.62, location=(0, 0, z + 0.31), vertices=12)
        ob = bpy.context.active_object
        ob.data.materials.append(material("tier_%d" % k, "pine" if k % 2 else "pine_dk", 0.9))
        bpy.ops.mesh.primitive_cone_add(radius1=r * 0.82, radius2=0.0, depth=0.34, location=(0, 0.02, z + 0.46),
                                        vertices=12)
        ob = bpy.context.active_object
        ob.data.materials.append(material("snow_%d" % k, "snow_white", 0.9))
    return 2.9


def prop_wyvern_nest():
    """A ring of broken branches and bones on bare rock, with pale blue eggs."""
    import random
    rng = random.Random(36)
    for k in range(26):
        a = k / 26 * math.tau + rng.uniform(-0.1, 0.1)
        r = rng.uniform(0.52, 0.66)
        cyl("stick_%d" % k, 0.035, rng.uniform(0.45, 0.7), (math.cos(a) * r, math.sin(a) * r, 0.10 + (k % 3) * 0.05),
            "twig", rot=(0.0, math.pi / 2, a + math.pi / 2 + rng.uniform(-0.4, 0.4)), verts=6)
    cyl("bed", 0.5, 0.06, (0, 0, 0.04), "hut_thatch", verts=16)
    for k, (x, y) in enumerate(((-0.12, 0.05), (0.12, 0.08), (0.0, -0.12))):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.12, location=(x, y, 0.16), segments=16, ring_count=10)
        egg = bpy.context.active_object
        egg.scale = (1.0, 1.0, 1.3)
        egg.data.materials.append(material("egg_%d" % k, "egg", 0.5))
    for k in range(3):
        a = k * 2.2
        blk("bone_%d" % k, (0.36, 0.05, 0.05), (math.cos(a) * 0.72, math.sin(a) * 0.72, 0.05), "bone_white",
            rot=(0, 0, a + 1.2), bev=0.02)
    return 1.9


def prop_charred_tree():
    """A tree burnt to the heartwood: black, split limbs, an ember still in it."""
    cyl("trunk", 0.15, 1.6, (0, 0, 0.8), "char", verts=8)
    for k, (a, z, ln) in enumerate(((0.3, 1.3, 0.7), (2.4, 1.1, 0.6), (4.2, 1.45, 0.55), (5.3, 0.9, 0.45))):
        cyl("limb_%d" % k, 0.05, ln, (math.cos(a) * ln * 0.4, math.sin(a) * ln * 0.4, z + 0.2), "char_lt",
            rot=(math.sin(a) * -0.9, math.cos(a) * 0.9, 0), verts=6)
    blk("ember", (0.10, 0.04, 0.16), (0.0, -0.15, 0.55), "ember", emit=1.2, bev=0)
    for k in range(4):
        cyl("root_%d" % k, 0.06, 0.4, (math.cos(k * 1.57) * 0.2, math.sin(k * 1.57) * 0.2, 0.08), "char",
            rot=(math.sin(k * 1.57) * 1.1, -math.cos(k * 1.57) * 1.1, 0), verts=6)
    return 2.1


def prop_obsidian_rock():
    import random
    rng = random.Random(37)
    for k in range(5):
        a = k / 5 * math.tau
        ice_shard("ob_%d" % k, rng.uniform(0.35, 0.8), rng.uniform(0.12, 0.2),
                  (math.cos(a) * 0.14, math.sin(a) * 0.14, 0), (math.sin(a) * 0.3, -math.cos(a) * 0.3, 0),
                  "obsidian" if k % 2 else "obsidian_lt")
    return 1.2


def prop_hellgate():
    """The way into the pit: an arch of black stone ribbed with horns, a
    doorway full of red light, braziers either side."""
    for sx in (-1, 1):
        for k in range(6):
            blk("pier_%d_%d" % (sx, k), (0.42, 0.44, 0.32), (sx * 0.78, 0, 0.16 + k * 0.33), "obsidian_lt" if k % 2 else "obsidian")
        cone("horn_%d" % sx, 0.14, 0.9, (sx * 0.92, 0, 2.30), "bone_white", rot=(0, sx * -0.6, 0), verts=8)
        cyl("brazier_%d" % sx, 0.16, 0.5, (sx * 1.40, -0.30, 0.25), "iron", verts=10)
        cone("fire_%d" % sx, 0.14, 0.4, (sx * 1.40, -0.30, 0.70), "ember")
        bpy.context.active_object.data.materials[0] = material("fire_%d" % sx, "ember", 0.6, 0.0, 1.6)
    blk("lintel", (2.10, 0.50, 0.40), (0, 0, 2.15), "obsidian")
    sphere("skull", 0.20, (0, -0.28, 2.15), "bone_white")
    blk("glow", (1.14, 0.20, 1.95), (0, 0.10, 0.98), "hell_red", emit=1.3, bev=0)
    blk("step", (1.4, 0.5, 0.12), (0, -0.4, 0.06), "obsidian_lt")
    return (3.6, BUILDING_ELEVATION)


def prop_cellar_hatch():
    """An open trapdoor in the floorboards, its lid thrown back, and steps
    going down into the dark."""
    blk("hole", (0.80, 0.90, 0.02), (0, 0, 0.01), "char", bev=0)
    for k in range(4):
        # Darker the further down they go, into the dark.
        blk("step_%d" % k, (0.62, 0.14, 0.02), (0, -0.30 + k * 0.19, 0.02), ("hut_wood", "bog_bark", "char_lt", "char")[k],
            bev=0.005)
    blk("frame", (0.92, 1.02, 0.04), (0, 0, 0.0), "oak", bev=0)
    # The lid thrown over onto the boards beside it, so it lies nearly flat
    # and the hole is the thing seen.
    blk("lid", (0.80, 0.90, 0.06), (0.86, 0, 0.10), "oak_light", rot=(0, -0.2, 0))
    for k, y in enumerate((-0.25, 0.25)):
        blk("strap_%d" % k, (0.84, 0.08, 0.08), (0.86, y, 0.13), "iron", rot=(0, -0.2, 0))
    return (2.0, 72.0)


def prop_cobweb():
    """A web slung across a corner: radial threads and a spiral of short
    segments, pale enough to read on dark stone."""
    for k in range(7):
        a = math.radians(-8 + k * 16)
        blk("spoke_%d" % k, (0.012, 0.012, 0.95), (math.sin(a) * 0.48, 0, 0.95 - math.cos(a) * 0.48), "cobweb",
            rot=(0, a, 0), bev=0)
    for ring in range(1, 5):
        r = ring * 0.19
        for k in range(6):
            a0 = math.radians(-8 + k * 16)
            a1 = math.radians(-8 + (k + 1) * 16)
            x0, z0 = math.sin(a0) * r, 0.95 - math.cos(a0) * r
            x1, z1 = math.sin(a1) * r, 0.95 - math.cos(a1) * r
            ln = math.hypot(x1 - x0, z1 - z0)
            blk("thread_%d_%d" % (ring, k), (ln, 0.01, 0.01), ((x0 + x1) / 2, 0, (z0 + z1) / 2), "cobweb",
                rot=(0, -math.atan2(z1 - z0, x1 - x0), 0), bev=0)
    return 1.2


# -----------------------------------------------------------------------------
#  Ways in and out of the dungeons
#
#  The barrow in the Mire was a door sprite standing on the grass next to a
#  mushroom, and inside every dungeon the way out was another door and the way
#  down a third, standing in the middle of a room. A way in should say what it
#  is from across the screen: a grave mound with a stone doorway cut into it,
#  a stone flight climbing up to daylight, a stairwell dropping into the dark.
# -----------------------------------------------------------------------------

PALETTE.update({
    "barrow_turf":    (0.447, 0.502, 0.286),
    "barrow_turf_dk": (0.341, 0.396, 0.224),
    "barrow_stone":   (0.482, 0.490, 0.463),
    "barrow_stone_dk":(0.345, 0.357, 0.337),
    "barrow_stone_lt":(0.620, 0.620, 0.576),
    "lichen":         (0.600, 0.620, 0.380),
    "grave_glow":     (0.420, 0.780, 0.580),
    "daylight":       (1.000, 0.945, 0.780),
})


def prop_barrow_mound():
    """A long grave mound grown over with turf, its doorway a dark passage
    framed by two standing stones and a capstone, with a facade of slabs
    curving out either side, flagstones leading up to it, a faint green light
    far down the passage and a skull on a stake to say keep out. The doorway
    sits at the image's horizontal centre, where genmaps puts the portal."""
    import random
    rng = random.Random(44)
    # The mound: a long low dome with lumps on it, so its outline is ground
    # rather than a ball.
    ob = sphere("mound", 1.0, (0, 1.05, 0.0), "barrow_turf")
    ob.scale = (2.05, 1.45, 1.55)
    for p in ob.data.polygons:
        p.use_smooth = True
    for k, (x, y, r, z) in enumerate(((-1.35, 0.9, 0.62, 0.55), (1.30, 1.0, 0.66, 0.6), (-0.4, 1.7, 0.7, 1.05),
                                      (0.6, 1.5, 0.64, 1.1), (-1.8, 1.4, 0.45, 0.2), (1.85, 1.3, 0.5, 0.2))):
        lump = sphere("lump_%d" % k, r, (x, y, z), "barrow_turf_dk" if k % 2 else "barrow_turf")
        lump.scale = (1.2, 1.0, 0.7)
    # The passage mouth, set into the front of the mound.
    OPEN_W, OPEN_H = 0.86, 1.18
    face_y = -0.42
    blk("dark", (OPEN_W + 0.1, 0.7, OPEN_H), (0, face_y + 0.30, OPEN_H / 2), "tunnel", bev=0)
    blk("glow", (0.40, 0.04, 0.34), (0, face_y + 0.62, 0.22), "grave_glow", emit=0.9, bev=0)
    # The door stones and the capstone laid across them.
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.30, 0.34, OPEN_H + 0.12), (sx * (OPEN_W / 2 + 0.14), face_y, (OPEN_H + 0.12) / 2),
            "barrow_stone", rot=(0, math.radians(-3 * sx), 0), bev=0.05)
    blk("capstone", (1.62, 0.62, 0.30), (0.03, face_y + 0.08, OPEN_H + 0.27), "barrow_stone_lt",
        rot=(0, math.radians(2), 0), bev=0.07)
    blk("capstone_under", (1.50, 0.50, 0.08), (0.03, face_y + 0.02, OPEN_H + 0.10), "barrow_stone_dk", bev=0.02)
    # Turf grown over the back of the capstone, flattened into the mound.
    turf = sphere("cap_turf", 0.5, (0, face_y + 0.52, OPEN_H + 0.40), "barrow_turf_dk")
    turf.scale = (1.35, 0.7, 0.32)
    # The facade: slabs set on end, shorter the further out, curving forward.
    for sx in (-1, 1):
        for k in range(4):
            x = sx * (0.95 + k * 0.36)
            h = 1.10 - k * 0.2
            y = face_y - 0.04 - k * k * 0.05
            rock("slab_%d_%d" % (sx, k), (0.19, 0.14, h / 2), (x, y, h / 2 - 0.05),
                 ("barrow_stone", "barrow_stone_dk", "barrow_stone_lt", "barrow_stone")[k], rng)
        sphere("lichen_%d" % sx, 0.08, (sx * 0.62, face_y - 0.16, 0.95), "lichen")
    # Flagstones up to the door, and the kerb of the forecourt.
    for k, (x, y) in enumerate(((0.0, -0.78), (0.12, -1.12), (-0.1, -1.44), (0.06, -1.76))):
        blk("flag_%d" % k, (0.62 - k * 0.04, 0.28, 0.06), (x, y, 0.03), "barrow_stone_lt" if k % 2 else "barrow_stone",
            rot=(0, 0, math.radians(rng.uniform(-8, 8))), bev=0.02)
    # The warning: a skull on a leaning stake to one side of the path.
    blk("stake", (0.06, 0.06, 1.05), (0.82, -1.15, 0.50), "timber_dk", rot=(math.radians(-6), math.radians(8), 0))
    sphere("skull", 0.13, (0.86, -1.20, 1.07), "bone_white")
    blk("jaw", (0.14, 0.08, 0.06), (0.86, -1.30, 0.98), "bone_white", bev=0.02)
    for sx in (-1, 1):
        sphere("eye_%d" % sx, 0.03, (0.86 + sx * 0.05, -1.32, 1.08), "tunnel")
    # Sedge round the foot of the mound.
    for k in range(9):
        x = rng.uniform(-2.0, 2.0)
        if abs(x) < 0.7:
            continue
        cone("sedge_%d" % k, 0.09, 0.42, (x, rng.uniform(-0.3, 0.3), 0.18), "reed" if k % 2 else "reed_dk", verts=6)
    return (4.8, BUILDING_ELEVATION)


def prop_dungeon_stairs_up():
    """The way out of a dungeon: a stone flight climbing away from the camera
    between rough walls to a landing lit by daylight from above."""
    import random
    rng = random.Random(7)
    steps, rise, run, width = 7, 0.19, 0.26, 0.90
    for i in range(steps):
        h = (i + 1) * rise
        y = -0.80 + i * run
        blk("riser_%d" % i, (width, run, h), (0, y, h / 2), "barrow_stone_dk", bev=0.01)
        blk("tread_%d" % i, (width + 0.02, run + 0.02, 0.05), (0, y, h + 0.01),
            "barrow_stone_lt" if i % 2 else "barrow_stone", bev=0.01)
    top = steps * rise
    far = -0.80 + steps * run
    # The light coming down the stairs from outside.
    blk("daylight", (width, 0.30, 0.9), (0, far + 0.05, top + 0.45), "daylight", emit=1.6, bev=0)
    blk("light_pool", (width - 0.1, run * 2.5, 0.02), (0, far - run * 1.6, top - rise + 0.04), "daylight", emit=0.5, bev=0)
    # Walls of rough stone either side, rising with the flight.
    for sx in (-1, 1):
        for i in range(steps + 1):
            h = min(top, (i + 1) * rise) + 0.55
            y = -0.80 + i * run
            rock("wall_%d_%d" % (sx, i), (0.20, run * 0.62, h / 2),
                 (sx * (width / 2 + 0.16), y, h / 2), ("barrow_stone", "barrow_stone_dk")[(i + (sx > 0)) % 2], rng)
    # The arch the flight climbs out through.
    blk("lintel", (width + 0.7, 0.34, 0.28), (0, far + 0.15, top + 1.05), "barrow_stone_lt", bev=0.04)
    for sx in (-1, 1):
        blk("pier_%d" % sx, (0.32, 0.34, top + 1.0), (sx * (width / 2 + 0.2), far + 0.15, (top + 1.0) / 2),
            "barrow_stone", bev=0.04)
    return (3.7, 40.0)


def prop_dungeon_stairs_down():
    """The way deeper: a stairwell in the floor with a stone kerb round it and
    a low parapet on three sides, steps dropping into the dark, and a torch on
    the corner. Open on the near side, where you step onto the flight."""
    width, depth = 0.96, 1.60
    blk("well", (width, depth, 0.02), (0, 0, -0.30), "tunnel", bev=0)
    steps = 6
    for i in range(steps):
        # Nearest step highest; they drop away from the camera.
        y = -depth / 2 + 0.14 + i * (depth / steps)
        z = -0.03 - i * 0.045
        tone = ("barrow_stone_lt", "barrow_stone", "barrow_stone", "barrow_stone_dk", "char_lt", "char")[i]
        blk("step_%d" % i, (width - 0.02, depth / steps - 0.02, 0.05), (0, y, z), tone, bev=0.006)
    for sx in (-1, 1):
        blk("parapet_%d" % sx, (0.18, depth + 0.18, 0.30), (sx * (width / 2 + 0.09), 0.09, 0.15), "barrow_stone", bev=0.03)
    blk("parapet_far", (width + 0.36, 0.18, 0.22), (0, depth / 2 + 0.09, 0.11), "barrow_stone_dk", bev=0.03)
    # A torch in a bracket on the far corner.
    blk("torch_post", (0.08, 0.08, 0.60), (width / 2 + 0.09, depth / 2 + 0.09, 0.52), "timber_dk")
    cone("torch_flame", 0.09, 0.24, (width / 2 + 0.09, depth / 2 + 0.09, 0.94), "ember")
    bpy.context.active_object.data.materials[0] = material("torch_flame", "ember", 0.6, 0.0, 1.8)
    return (2.3, 62.0)


# -----------------------------------------------------------------------------
#  The three working camps in Havenbrook, where a new character is taught to
#  cut, mine and fish: a sawpit, an ore cart, and a boat drawn up on the bank.
# -----------------------------------------------------------------------------

PALETTE.update({
    "sawdust":    (0.812, 0.702, 0.490),
    "boat_hull":  (0.478, 0.333, 0.216),
    "boat_trim":  (0.639, 0.475, 0.294),
    "boat_in":    (0.361, 0.259, 0.180),
    "paint_blue": (0.263, 0.412, 0.549),
})


def prop_sawmill():
    """A sawpit: a log up on two trestles, lying across the view, with a
    two-man saw standing in the cut, fresh boards stacked in front of it and
    sawdust underfoot. Laid across rather than away from the camera -- end-on,
    a log two metres long is a circle."""
    import random
    rng = random.Random(9)
    LOG_Z = 0.74
    for sx in (-1, 1):
        x = sx * 0.78
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.09, 0.09, 0.80), (x, sy * 0.24, 0.36),
                "oak", rot=(math.radians(-sy * 16), 0, 0))
        blk("cross_%d" % sx, (0.10, 0.60, 0.08), (x, 0, 0.34), "oak_light")
        blk("cap_%d" % sx, (0.16, 0.74, 0.09), (x, 0, 0.72), "oak")
    cyl("log", 0.26, 2.10, (0, 0, LOG_Z + 0.14), "log", rot=(0, math.radians(90), 0), verts=14)
    for sx in (-1, 1):
        cyl("log_end_%d" % sx, 0.23, 0.03, (sx * 1.05, 0, LOG_Z + 0.14), "log_end",
            rot=(0, math.radians(90), 0), verts=14)
    # The cut, and the saw standing in it.
    blk("cut", (0.05, 0.50, 0.46), (0.16, 0.0, LOG_Z + 0.24), "log_end", bev=0)
    blk("blade", (1.20, 0.05, 0.30), (0.16, 0.02, LOG_Z + 0.52), "iron_light", metal=0.6, rough=0.4, bev=0)
    for k in range(12):
        blk("tooth_%d" % k, (0.06, 0.05, 0.07), (-0.34 + k * 0.09, 0.02, LOG_Z + 0.34), "iron_light", bev=0)
    for sx in (-1, 1):
        blk("handle_%d" % sx, (0.20, 0.07, 0.09), (sx * 0.74 + 0.16, 0.02, LOG_Z + 0.56), "oak_light")
        blk("grip_%d" % sx, (0.07, 0.07, 0.24), (sx * 0.84 + 0.16, 0.02, LOG_Z + 0.44), "oak_pale")
    # Boards off the log, stacked flat in front.
    for k in range(4):
        blk("board_%d" % k, (1.70, 0.44, 0.06), (-0.10, -0.86, 0.05 + k * 0.07),
            "oak_pale" if k % 2 else "oak_light")
    # A chopping block with an axe left in it.
    cyl("block", 0.28, 0.48, (1.18, -0.46, 0.24), "log", verts=14)
    cyl("block_top", 0.27, 0.03, (1.18, -0.46, 0.48), "log_end", verts=14)
    blk("axe_haft", (0.05, 0.05, 0.56), (1.24, -0.46, 0.70), "oak_light", rot=(0, math.radians(16), 0))
    blk("axe_head", (0.20, 0.05, 0.13), (1.12, -0.46, 0.92), "iron_light", metal=0.6)
    # Sawdust under the cut, and offcuts thrown clear.
    for k in range(16):
        sphere("dust_%d" % k, rng.uniform(0.05, 0.10),
               (rng.uniform(-0.5, 0.8), rng.uniform(-0.5, 0.35), 0.02), "sawdust")
    for k in range(3):
        cyl("offcut_%d" % k, 0.07, rng.uniform(0.3, 0.5),
            (-1.15 + rng.uniform(-0.15, 0.15), rng.uniform(-0.6, 0.2), 0.07), "log_dk",
            rot=(0, math.radians(90), math.radians(rng.uniform(0, 180))), verts=10)
    return (3.4, 40.0)


def prop_ore_cart():
    """A tipper cart of broken rock on a short length of rail, with a pick
    leaning on it: the working end of a quarry."""
    import random
    rng = random.Random(11)
    # Rails and sleepers under it.
    for k in range(4):
        blk("sleeper_%d" % k, (0.86, 0.12, 0.05), (0, 0.42 - k * 0.30, 0.025), "timber_dk")
    for sx in (-1, 1):
        blk("rail_%d" % sx, (0.05, 1.30, 0.05), (sx * 0.28, 0, 0.07), "rail", metal=0.4, rough=0.5)
    cx, cy = 0.0, 0.05
    blk("body", (0.74, 0.62, 0.42), (cx, cy, 0.36), "timber")
    blk("band_low", (0.78, 0.66, 0.06), (cx, cy, 0.24), "iron", metal=0.5)
    blk("band_top", (0.80, 0.68, 0.06), (cx, cy, 0.48), "iron", metal=0.5)
    # Heaped ore, rust-red in the grey.
    for k in range(12):
        rock("ore_%d" % k, (0.13, 0.12, 0.10),
             (cx + rng.uniform(-0.26, 0.26), cy + rng.uniform(-0.20, 0.20), 0.62 + rng.uniform(0, 0.08)),
             ("ore_red", "crag_lt", "tailings")[k % 3], rng)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cyl("wheel_%d_%d" % (sx, sy), 0.09, 0.05, (cx + sx * 0.28, cy + sy * 0.22, 0.11), "iron",
                rot=(0, math.radians(90), 0), verts=12)
    blk("pick_haft", (0.05, 0.05, 0.70), (-0.52, -0.30, 0.34), "timber", rot=(0, math.radians(-16), 0))
    blk("pick_head", (0.40, 0.06, 0.06), (-0.62, -0.30, 0.66), "iron", metal=0.5)
    # Spoil tipped out beside the rails.
    for k in range(7):
        rock("spoil_%d" % k, (rng.uniform(0.08, 0.15),) * 3,
             (0.60 + rng.uniform(-0.2, 0.2), rng.uniform(-0.5, 0.5), rng.uniform(0.04, 0.12)),
             "tailings" if k % 3 else "crag_lt", rng)
    return (2.6, 40.0)


def prop_rowboat():
    """A little boat drawn up on the bank, bow toward the water, with its oars
    crossed inside and a creel of fish in the stern. Hull and inside are two
    squashed domes rather than four planks: from above a boat is an outline,
    and a boxy one reads as a crate."""
    hull = sphere("hull", 1.0, (0, 0, 0.14), "boat_hull")
    hull.scale = (0.46, 1.02, 0.32)
    # The bow, drawn out to a point, and a stem post on it.
    bow = cone("bow", 0.19, 0.60, (0, 1.06, 0.26), "boat_hull", rot=(math.radians(-96), 0, 0), verts=14)
    bow.scale = (1.0, 1.0, 0.55)
    blk("stem", (0.09, 0.14, 0.26), (0, 1.16, 0.34), "boat_trim", rot=(math.radians(20), 0, 0))
    # The gunwale: a rim of trim round the top of the hull.
    rim = sphere("rim", 1.0, (0, 0, 0.30), "boat_trim")
    rim.scale = (0.44, 0.98, 0.10)
    # The inside, lower than the rim so she reads as hollow.
    inner = sphere("inner", 1.0, (0, 0, 0.22), "boat_in")
    inner.scale = (0.36, 0.88, 0.14)
    # Thwarts to sit on.
    for y in (0.42, -0.34):
        blk("thwart_%.2f" % y, (0.70, 0.14, 0.06), (0, y, 0.40), "boat_trim", bev=0.02)
    # Oars crossed inside her, blades over the stern.
    for sx in (-1, 1):
        blk("oar_%d" % sx, (0.06, 1.50, 0.05), (sx * 0.10, 0.10, 0.44), "oak_light",
            rot=(0, 0, math.radians(sx * 9)))
        blk("blade_%d" % sx, (0.14, 0.34, 0.04), (sx * 0.24, -0.62, 0.44), "oak_pale",
            rot=(0, 0, math.radians(sx * 9)))
    # A creel of the morning's catch, and a coil of rope at the bow.
    blk("creel", (0.26, 0.26, 0.22), (0.14, -0.74, 0.40), "straw", bev=0.06)
    blk("creel_lid", (0.28, 0.28, 0.05), (0.14, -0.74, 0.53), "leather", bev=0.02)
    for k in range(3):
        cyl("rope_%d" % k, 0.12 - k * 0.03, 0.04, (-0.04, 0.62, 0.42 + k * 0.04), "straw", verts=14)
    return (3.0, 46.0)


# -----------------------------------------------------------------------------
#  Hollowrest: the graveyard in the south of the Hollowmarch
#
#  Everything here is meant to be read at a glance from above and in the dark:
#  pale stone against dead grass, and silhouettes that are nothing else on the
#  map -- a round-topped headstone, a leaning cross, a fresh mound with the
#  spade still in it, a railing of spear-tipped iron, a lych gate to walk in
#  through and a crypt at the far end.
# -----------------------------------------------------------------------------

PALETTE.update({
    # Cool grey, not warm: under this light a stone mixed toward yellow comes
    # out pink, and a graveyard full of pink stones reads as sandstone.
    "spring_water": (0.290, 0.560, 0.620),
    "spring_glow":  (0.520, 0.840, 0.860),
    "granite":     (0.470, 0.500, 0.520),
    "granite_lt":  (0.620, 0.650, 0.665),
    "granite_dk":  (0.300, 0.325, 0.345),
    "grave_moss":  (0.360, 0.450, 0.290),
    "grave_earth": (0.300, 0.250, 0.200),
    "grave_earth_lt": (0.400, 0.340, 0.270),
    "grave_iron":  (0.220, 0.225, 0.240),
    "grave_iron_lt": (0.360, 0.370, 0.390),
    "crypt_dark":  (0.050, 0.048, 0.055),
    "candle_glow": (1.000, 0.870, 0.520),
})


def _weathering(rng, parent, n, x_span, z_span, colour="granite_dk"):
    """Chips and lichen on a face of stone, so no two markers are the same."""
    for k in range(n):
        sphere("wear_%d" % k, rng.uniform(0.02, 0.05),
               (rng.uniform(-x_span, x_span), -0.06, rng.uniform(0.05, z_span)), colour)


def prop_gravestone():
    """A round-topped headstone, leaning, with moss up the back of it."""
    import random
    rng = random.Random(3)
    lean = math.radians(-6)
    blk("stone", (0.46, 0.14, 0.62), (0, 0, 0.34), "granite", rot=(lean, 0, 0), bev=0.03)
    cyl("top", 0.23, 0.14, (0, 0.035, 0.64), "granite", rot=(math.radians(90), 0, 0), verts=16)
    blk("face", (0.34, 0.04, 0.42), (0, -0.08, 0.36), "granite_lt", rot=(lean, 0, 0), bev=0.02)
    for k in range(3):
        blk("line", (0.20 - k * 0.03, 0.02, 0.02), (0, -0.11, 0.48 - k * 0.09), "granite_dk", bev=0)
    blk("base", (0.56, 0.24, 0.10), (0, 0, 0.05), "granite_dk", bev=0.02)
    for k in range(4):
        sphere("moss_%d" % k, rng.uniform(0.05, 0.09),
               (rng.uniform(-0.2, 0.2), 0.08, rng.uniform(0.05, 0.3)), "grave_moss")
    _weathering(rng, None, 3, 0.18, 0.5)
    return 1.5


def prop_gravestone_cross():
    """A stone cross, leaning harder than the headstone does."""
    import random
    rng = random.Random(5)
    lean = math.radians(-11)
    blk("shaft", (0.16, 0.14, 0.86), (0, 0, 0.46), "granite", rot=(lean, 0, 0), bev=0.03)
    blk("arms", (0.56, 0.13, 0.15), (0, -0.09, 0.66), "granite", rot=(lean, 0, 0), bev=0.03)
    blk("boss", (0.17, 0.10, 0.17), (0, -0.12, 0.66), "granite_lt", rot=(lean, 0, 0), bev=0.04)
    blk("base", (0.40, 0.26, 0.12), (0, 0, 0.06), "granite_dk", bev=0.02)
    for k in range(3):
        sphere("moss_%d" % k, rng.uniform(0.05, 0.08),
               (rng.uniform(-0.16, 0.16), 0.07, rng.uniform(0.06, 0.34)), "grave_moss")
    return 1.6


def prop_grave_mound():
    """A grave dug and filled in again this week: turned earth, a plank marker
    and the spade left standing in it."""
    import random
    rng = random.Random(7)
    mound = sphere("mound", 0.5, (0, 0, 0.0), "grave_earth")
    mound.scale = (0.72, 1.05, 0.34)
    for k in range(9):
        clod = sphere("clod_%d" % k, rng.uniform(0.05, 0.10),
                      (rng.uniform(-0.3, 0.3), rng.uniform(-0.45, 0.45), rng.uniform(0.10, 0.17)),
                      "grave_earth_lt" if k % 2 else "grave_earth")
        clod.scale = (1.2, 1.2, 0.7)
    blk("marker", (0.26, 0.05, 0.34), (0, 0.42, 0.21), "oak", rot=(math.radians(-8), 0, 0), bev=0.02)
    blk("marker_bar", (0.34, 0.04, 0.06), (0, 0.40, 0.30), "oak_light", bev=0.02)
    blk("spade_haft", (0.05, 0.05, 0.60), (0.30, -0.22, 0.32), "oak_light", rot=(0, math.radians(14), 0))
    blk("spade_grip", (0.14, 0.05, 0.06), (0.36, -0.22, 0.60), "oak_light", bev=0.02)
    blk("spade_blade", (0.18, 0.06, 0.20), (0.22, -0.22, 0.10), "iron", rot=(0, math.radians(14), 0), bev=0.02)
    return 1.9


def prop_grave_fence():
    """A length of spear-tipped iron railing, laid end to end round the yard."""
    n = 7
    step = 0.26
    width = step * (n - 1)
    for i in range(n):
        x = -width / 2 + i * step
        cyl("bar_%d" % i, 0.026, 0.92, (x, 0, 0.46), "grave_iron", verts=8)
        cone("tip_%d" % i, 0.05, 0.16, (x, 0, 0.98), "grave_iron_lt", verts=8)
    for z in (0.24, 0.74):
        blk("rail_%.2f" % z, (width + 0.16, 0.05, 0.05), (0, 0, z), "grave_iron", bev=0.01)
    for sx in (-1, 1):
        cyl("post_%d" % sx, 0.05, 1.10, (sx * (width / 2 + 0.10), 0, 0.55), "grave_iron", verts=10)
        blk("cap_%d" % sx, (0.13, 0.13, 0.07), (sx * (width / 2 + 0.10), 0, 1.12), "grave_iron_lt", bev=0.02)
    return 2.3


def prop_lych_gate():
    """The way in: two stone piers, an iron gate standing open, and a little
    shingled roof over the whole thing for the bearers to stand under."""
    for sx in (-1, 1):
        blk("pier_%d" % sx, (0.34, 0.34, 1.30), (sx * 0.92, 0, 0.65), "granite", bev=0.04)
        blk("cap_%d" % sx, (0.44, 0.44, 0.12), (sx * 0.92, 0, 1.36), "granite_lt", bev=0.03)
        sphere("finial_%d" % sx, 0.13, (sx * 0.92, 0, 1.50), "granite_lt")
        # A leaf of the gate, swung back against its pier.
        for k in range(4):
            cyl("bar_%d_%d" % (sx, k), 0.022, 0.86, (sx * (0.70 - k * 0.14), 0.24 + k * 0.03, 0.46),
                "grave_iron", verts=8)
        blk("leaf_rail_%d" % sx, (0.60, 0.05, 0.05), (sx * 0.52, 0.28, 0.80), "grave_iron", bev=0.01)
    # The roof over the gateway: two slopes on a pair of tie beams.
    blk("beam", (2.30, 0.18, 0.14), (0, 0, 1.46), "oak", bev=0.02)
    for sx in (-1, 1):
        blk("rafter_%d" % sx, (1.30, 0.06, 0.10), (sx * 0.58, 0, 1.76), "oak",
            rot=(0, sx * math.radians(34), 0), bev=0.02)
        blk("shingle_%d" % sx, (1.38, 1.10, 0.09), (sx * 0.56, 0, 1.78), "shingle",
            rot=(0, sx * math.radians(34), 0), bev=0.02)
    blk("ridge", (0.16, 1.16, 0.10), (0, 0, 2.10), "shingle_dk", bev=0.02)
    # A lantern hung under the roof, because somebody still keeps it lit.
    blk("hook", (0.04, 0.04, 0.22), (0, -0.30, 1.36), "iron")
    blk("lantern", (0.16, 0.16, 0.20), (0, -0.30, 1.16), "candle_glow", emit=1.3, bev=0.03)
    blk("lantern_cap", (0.20, 0.20, 0.05), (0, -0.30, 1.28), "iron", bev=0.02)
    return (3.6, BUILDING_ELEVATION)


def prop_crypt(barred=True):
    """The mausoleum at the head of the yard: a stone house for one family,
    with a pediment and urns either side of the step.

    `barred` is how it stood for as long as nobody had been down there: an iron
    grille across the doorway. Opened, the bars are gone, the dark is cut deeper
    and wider so it reads as somewhere you can walk, and the grille is left
    leaning against the wall beside it -- somebody took it off, and the yard
    should say so."""
    import random
    rng = random.Random(11)
    W, D, H = 2.10, 1.70, 1.15
    blk("plinth", (W + 0.30, D + 0.26, 0.18), (0, 0, 0.09), "granite_dk", bev=0.03)
    blk("body", (W, D, H), (0, 0, 0.18 + H / 2), "granite", bev=0.04)
    # Pilasters at the corners, and a course under the roof.
    for sx in (-1, 1):
        blk("pilaster_%d" % sx, (0.20, 0.20, H), (sx * (W / 2 - 0.08), -D / 2 + 0.06, 0.18 + H / 2),
            "granite_lt", bev=0.03)
    blk("cornice", (W + 0.22, D + 0.20, 0.16), (0, 0, 0.18 + H + 0.06), "granite_lt", bev=0.03)
    # A shallow pitched roof with a pediment facing the yard.
    for sx in (-1, 1):
        blk("roof_%d" % sx, (W * 0.66, D + 0.20, 0.13), (sx * W * 0.27, 0, 0.18 + H + 0.30),
            "granite_dk", rot=(0, sx * math.radians(26), 0), bev=0.03)
        # Courses of slate, so the roof is not one flat grey lid.
        for k in range(3):
            blk("slate_%d_%d" % (sx, k), (W * 0.60, 0.05, 0.03),
                (sx * W * 0.27, -D / 2 + 0.35 + k * 0.42, 0.18 + H + 0.30 + 0.07),
                "granite", rot=(0, sx * math.radians(26), 0), bev=0)
    blk("ridge", (0.18, D + 0.18, 0.10), (0, 0, 0.18 + H + 0.52), "granite_lt", bev=0.03)
    blk("pediment", (W * 0.78, 0.12, 0.34), (0, -D / 2 - 0.02, 0.18 + H + 0.18), "granite_lt", bev=0.03)
    # The doorway: dark, with a step up to it -- barred, or open and deeper.
    if barred:
        blk("dark", (0.86, 0.30, 0.98), (0, -D / 2 + 0.16, 0.66), "crypt_dark", bev=0)
        for k in range(4):
            cyl("bar_%d" % k, 0.025, 1.00, (-0.27 + k * 0.18, -D / 2 + 0.02, 0.70), "grave_iron", verts=8)
    else:
        # Wider and taller than the barred one, and kept FLUSH with the front
        # face. Set back into the building it is simply hidden by the wall in
        # front of it -- blk stacks solids, it does not cut holes -- and the
        # doorway renders as pale stone.
        blk("dark", (1.02, 0.34, 1.08), (0, -D / 2 - 0.06, 0.70), "crypt_dark", bev=0)
        # A sill of the same dark at the foot of it, so the threshold reads as
        # floor going in rather than as a picture hung on the wall.
        blk("sill", (1.02, 0.26, 0.10), (0, -D / 2 - 0.06, 0.21), "crypt_dark", bev=0)
        # A jamb either side, so the opening has an edge and is not a smear.
        for sx in (-1, 1):
            blk("jamb_%d" % sx, (0.14, 0.26, 1.14), (sx * 0.580, -D / 2 - 0.08, 0.70),
                "granite_lt", bev=0.02)
        # The grille that used to be in it, leaning against the wall.
        for k in range(4):
            cyl("leaned_%d" % k, 0.025, 0.96, (0.80 + k * 0.05, -D / 2 - 0.04, 0.50),
                "grave_iron", rot=(math.radians(16), 0, math.radians(13)), verts=8)
    blk("lintel", (0.96, 0.18, 0.16), (0, -D / 2 + 0.02, 1.28), "granite_lt", bev=0.03)
    blk("step", (1.10, 0.36, 0.10), (0, -D / 2 - 0.20, 0.10), "granite_dk", bev=0.02)
    # Urns on the step, and moss creeping up the north side.
    for sx in (-1, 1):
        cyl("urn_%d" % sx, 0.13, 0.26, (sx * 0.72, -D / 2 - 0.16, 0.28), "granite_lt", verts=12)
        cyl("urn_lip_%d" % sx, 0.15, 0.05, (sx * 0.72, -D / 2 - 0.16, 0.43), "granite", verts=12)
    for k in range(7):
        sphere("moss_%d" % k, rng.uniform(0.07, 0.13),
               (rng.uniform(-W / 2, W / 2), D / 2 - 0.05, rng.uniform(0.2, 1.2)), "grave_moss")
    return (4.4, BUILDING_ELEVATION)


def prop_spring_basin():
    """The head of the spring at the bottom of the well: a stone basin with a
    mouth cut into the rock behind it, choked with rubble and weed, and a
    little water still finding its way through."""
    import random
    rng = random.Random(23)
    # The mouth in the rock, and the basin under it.
    blk("back", (1.50, 0.42, 1.10), (0, 0.42, 0.55), "granite_dk", bev=0.05)
    blk("mouth", (0.60, 0.26, 0.52), (0, 0.24, 0.52), "crypt_dark", bev=0)
    blk("lintel", (0.86, 0.30, 0.16), (0, 0.22, 0.84), "granite_lt", bev=0.03)
    cyl("basin", 0.62, 0.30, (0, -0.16, 0.15), "granite", verts=18)
    cyl("rim", 0.66, 0.10, (0, -0.16, 0.30), "granite_lt", verts=18)
    cyl("water", 0.54, 0.06, (0, -0.16, 0.31), "spring_water", verts=18)
    for k in range(3):
        cyl("ripple", 0.34 - k * 0.10, 0.02, (0.04, -0.20, 0.335 + k * 0.004), "spring_glow", verts=14)
    # The rubble in the mouth, and weed growing out of the wet.
    for k in range(7):
        rock("rubble_%d" % k, (rng.uniform(0.10, 0.20),) * 3,
             (rng.uniform(-0.28, 0.28), 0.10 + rng.uniform(-0.06, 0.10), 0.12 + rng.uniform(0, 0.42)),
             "granite" if k % 2 else "granite_dk", rng)
    for k in range(5):
        parts_x = -0.40 + k * 0.20
        cone("weed_%d" % k, 0.05, 0.26, (parts_x, -0.02, 0.28), "grave_moss", verts=6)
    return (2.6, 44.0)


AREA_PROPS = {
    "reeds": (prop_reeds, 48), "lily_pads": (prop_lily_pads, 40), "swamp_tree": (prop_swamp_tree, 72),
    "lizard_hut": (prop_lizard_hut, 128), "lizard_totem": (prop_lizard_totem, 56),
    # The Bayou: huts that sit on a stilt village's deck, the Mother of the
    # Fen's, and the stilts under the front edge of each deck.
    "bayou_hut": (prop_bayou_hut, 112), "bayou_hut_great": (lambda: prop_bayou_hut(great=True), 152),
    "bayou_piling": (prop_bayou_piling, 64),
    "ice_spire": (prop_ice_spire, 112), "ice_crystal": (prop_ice_crystal, 40), "snow_pine": (prop_snow_pine, 80),
    "wyvern_nest": (prop_wyvern_nest, 72), "charred_tree": (prop_charred_tree, 72),
    "obsidian_rock": (prop_obsidian_rock, 40), "hellgate": (prop_hellgate, 144),
    "cellar_hatch": (prop_cellar_hatch, 48), "cobweb": (prop_cobweb, 40),
    "barrow_mound": (prop_barrow_mound, 208), "dungeon_stairs_up": (prop_dungeon_stairs_up, 96),
    "dungeon_stairs_down": (prop_dungeon_stairs_down, 80),
    "sawmill": (prop_sawmill, 144), "ore_cart": (prop_ore_cart, 96), "rowboat": (prop_rowboat, 128),
    "spring_basin": (prop_spring_basin, 112),
    "gravestone": (prop_gravestone, 48), "gravestone_cross": (prop_gravestone_cross, 52),
    "grave_mound": (prop_grave_mound, 64), "grave_fence": (prop_grave_fence, 72),
    "lych_gate": (prop_lych_gate, 144), "crypt": (prop_crypt, 176),
    # The same building with the grille off it: what Hollowrest has once there
    # is a way down. See prop_crypt.
    "crypt_open": (lambda: prop_crypt(barred=False), 176),
}
HERB_PROPS.update(AREA_PROPS)


# =============================================================================
#  Scenery: trees, rocks, bushes and fungus
#
#  These replace a set of CraftPix packs -- trees, rocks, bushes, forest
#  objects -- so that assets/objects/ is the game's own and can travel with it.
#
#  They are generated rather than modelled one at a time: a builder takes a
#  seed and shakes out a different tree from the same rules, because what a
#  wood needs is ten trees that are plainly the same kind of tree and plainly
#  not the same tree. Everything is built around the origin with its base at
#  z=0, and the renderer frames a square of the size the table gives.
# =============================================================================
PALETTE.update({
    "bark":        (0.42, 0.29, 0.18), "bark_dk": (0.30, 0.20, 0.13),
    "bark_pale":   (0.55, 0.43, 0.30),
    "canopy":      (0.22, 0.40, 0.21), "canopy_dk": (0.14, 0.27, 0.15),
    "canopy_lt":   (0.33, 0.54, 0.26),
    "canopy_gold": (0.62, 0.45, 0.14), "canopy_rust": (0.52, 0.26, 0.12),
    "shrub":       (0.24, 0.40, 0.20), "shrub_dk": (0.16, 0.28, 0.15),
    "berry_red":   (0.66, 0.18, 0.18), "berry_blue": (0.33, 0.36, 0.62),
    # Warm grey rather than neutral: the shading step tints the shadow band
    # cool, and a neutral stone came out of it lavender.
    "boulder":     (0.46, 0.44, 0.40), "boulder_dk": (0.31, 0.30, 0.27),
    "boulder_lt":  (0.58, 0.56, 0.51),
    "cap_red":     (0.62, 0.17, 0.16), "cap_pale": (0.80, 0.76, 0.66),
    "cap_brown":   (0.48, 0.33, 0.19), "stalk": (0.84, 0.80, 0.68),
    "cap_blue":    (0.30, 0.40, 0.60), "cap_rim": (0.20, 0.16, 0.14),
})


def _rng(seed):
    import random
    return random.Random(seed)


def scenery_tree(seed, big=True):
    """A trunk that leans a little and a canopy of three or four overlapping
    blobs. The blobs are what makes it a tree at this size; the trunk only has
    to be visible under them."""
    rng = _rng(seed)
    autumn = rng.random() < 0.3
    leaf = "canopy" if not autumn else rng.choice(("canopy_gold", "canopy_rust"))
    leaf_dk = "canopy_dk" if not autumn else "bark"
    h = (1.35 if big else 0.85) * rng.uniform(0.9, 1.1)
    lean = rng.uniform(-0.06, 0.06)
    cyl("trunk", 0.10 if big else 0.075, h, (lean * 0.5, 0, h / 2), "bark",
        rot=(0, lean, 0), verts=8)
    cyl("root", 0.16 if big else 0.12, 0.10, (0, 0, 0.05), "bark_dk", verts=8)
    # One or two boughs, so the trunk is not a pole.
    for k in range(rng.randint(1, 2)):
        a = rng.uniform(0, 6.28)
        cyl("bough_%d" % k, 0.045, 0.34, (math.cos(a) * 0.14, math.sin(a) * 0.14, h * 0.72),
            "bark", rot=(math.cos(a) * 0.9, math.sin(a) * 0.9, 0), verts=6)
    blobs = rng.randint(3, 4)
    for k in range(blobs):
        a = k / blobs * math.tau + rng.uniform(-0.3, 0.3)
        r = (0.46 if big else 0.32) * rng.uniform(0.82, 1.12)
        d = (0.30 if big else 0.20) * rng.uniform(0.7, 1.15)
        z = h - (0.06 if big else 0.04) + rng.uniform(-0.06, 0.10) * (1.2 if big else 0.8)
        ob = sphere("leaf_%d" % k, r, (math.cos(a) * d + lean, math.sin(a) * d, z),
                    leaf_dk if k % 2 else leaf, rough=0.95)
        ob.scale = (1.0, 1.0, 0.72)
    top = sphere("crown", (0.40 if big else 0.28), (lean, 0, h + (0.20 if big else 0.13)),
                 "canopy_lt" if not autumn else leaf, rough=0.95)
    top.scale = (1.0, 1.0, 0.68)
    return (2.6 if big else 1.8)


PALETTE.update({
    "cut_wood":    (0.780, 0.620, 0.380),
    "cut_wood_dk": (0.580, 0.420, 0.240),
})


def scenery_stump(seed, big=True):
    """What is left when a tree comes down: a short length of trunk cut flat
    with its rings showing, the root flare, a splinter the axe left standing,
    and a chip or two on the ground. Small enough that the ground around it
    reads as cleared."""
    rng = _rng(seed)
    r = 0.14 if big else 0.10
    h = 0.34 if big else 0.24
    lean = rng.uniform(-0.05, 0.05)
    cyl("trunk", r, h, (lean * 0.5, 0, h / 2), "bark", rot=(0, lean, 0), verts=10)
    cyl("root", r * 1.6, 0.09, (0, 0, 0.045), "bark_dk", verts=10)
    cyl("cut", r * 0.92, 0.024, (lean * h, 0, h + 0.006), "cut_wood", verts=10)
    cyl("rings", r * 0.55, 0.014, (lean * h, 0, h + 0.022), "cut_wood_dk", verts=10)
    cyl("heart", r * 0.2, 0.014, (lean * h, 0, h + 0.03), "cut_wood", verts=8)
    cone("splinter", r * 0.32, 0.18, (lean * h + r * 0.55, 0.0, h + 0.07), "bark", verts=5)
    for k in range(2):
        a = rng.uniform(0, math.tau)
        blk("chip_%d" % k, (0.09, 0.045, 0.02), (math.cos(a) * r * 2.2, math.sin(a) * r * 2.2, 0.01),
            "cut_wood", rot=(0, 0, a), bev=0)
    return (0.95 if big else 0.68)


def scenery_bush(seed, big=True):
    """Three or four low blobs with a few berries, which is all a bush is at
    thirty pixels."""
    rng = _rng(seed)
    berries = rng.random() < 0.45
    berry = rng.choice(("berry_red", "berry_blue"))
    n = rng.randint(3, 5)
    base = 0.34 if big else 0.24
    for k in range(n):
        a = k / n * math.tau + rng.uniform(-0.4, 0.4)
        d = base * rng.uniform(0.25, 0.7)
        r = base * rng.uniform(0.62, 1.0)
        sphere("clump_%d" % k, r, (math.cos(a) * d, math.sin(a) * d, r * 0.78),
               "shrub" if k % 2 == 0 else "shrub_dk", rough=0.9)
    if berries:
        for k in range(rng.randint(3, 6)):
            a = rng.uniform(0, 6.28)
            d = base * rng.uniform(0.3, 0.85)
            sphere("berry_%d" % k, base * 0.12,
                   (math.cos(a) * d, math.sin(a) * d, base * rng.uniform(0.75, 1.25)),
                   berry, rough=0.4)
    return (1.25 if big else 0.9)


def scenery_rock(seed, big=True):
    """A boulder is two or three lumps with the smaller ones tucked against the
    biggest, and a fleck of moss on the shaded side."""
    rng = _rng(seed)
    n = rng.randint(2, 3)
    base = 0.40 if big else 0.26
    for k in range(n):
        a = k / n * math.tau + rng.uniform(-0.5, 0.5)
        d = base * rng.uniform(0.0, 0.55) if k else 0.0
        r = base * (1.0 if k == 0 else rng.uniform(0.45, 0.75))
        ob = sphere("lump_%d" % k, r, (math.cos(a) * d, math.sin(a) * d, r * 0.55),
                    ("boulder", "boulder_dk", "boulder_lt")[k % 3], rough=0.98)
        ob.scale = (1.0, rng.uniform(0.80, 1.05), rng.uniform(0.52, 0.70))
        ob.rotation_euler = (0, rng.uniform(-0.25, 0.25), rng.uniform(0, 3.14))
    if rng.random() < 0.6:
        sphere("moss", base * 0.34, (base * 0.2, base * 0.28, base * 0.78), "moss", rough=1.0)
    return (1.35 if big else 0.95)


def scenery_mushroom(seed, tall=False):
    """A cluster: one big cap and two smaller, with gills under the big one."""
    rng = _rng(seed)
    cap = rng.choice(("cap_red", "cap_brown", "cap_pale", "cap_blue"))
    n = rng.randint(2, 3)
    scale = 1.0 if tall else 0.7
    for k in range(n):
        a = k / n * math.tau + rng.uniform(-0.4, 0.4)
        d = 0.0 if k == 0 else rng.uniform(0.16, 0.30) * scale
        r = (0.28 if k == 0 else rng.uniform(0.14, 0.20)) * scale
        h = (0.44 if k == 0 else rng.uniform(0.20, 0.30)) * scale
        x, y = math.cos(a) * d, math.sin(a) * d
        cyl("stalk_%d" % k, r * 0.30, h, (x, y, h / 2), "stalk", verts=10)
        rim = sphere("rim_%d" % k, r * 1.04, (x, y, h - r * 0.06), "cap_rim", rough=0.9)
        rim.scale = (1.0, 1.0, 0.40)
        ob = sphere("cap_%d" % k, r, (x, y, h), cap, rough=0.85)
        ob.scale = (1.0, 1.0, 0.46)
        cyl("gills_%d" % k, r * 0.72, 0.03, (x, y, h - r * 0.10), "cap_pale", verts=12)
        if rng.random() < 0.5:
            for j in range(3):
                b = rng.uniform(0, 6.28)
                sphere("spot_%d_%d" % (k, j), r * 0.16,
                       (x + math.cos(b) * r * 0.5, y + math.sin(b) * r * 0.5, h + r * 0.30),
                       "cap_pale" if cap != "cap_pale" else "cap_brown", rough=0.6)
    return (1.5 if tall else 1.0)


def scenery_fungus(seed):
    """Shelf fungus and toadstools: ground cover rather than a cluster."""
    rng = _rng(seed)
    cap = rng.choice(("cap_brown", "cap_pale", "cap_blue"))
    for k in range(rng.randint(3, 5)):
        a = rng.uniform(0, 6.28)
        d = rng.uniform(0.05, 0.26)
        r = rng.uniform(0.09, 0.15)
        x, y = math.cos(a) * d, math.sin(a) * d
        cyl("stalk_%d" % k, r * 0.28, r * 1.1, (x, y, r * 0.55), "stalk", verts=8)
        ob = sphere("cap_%d" % k, r, (x, y, r * 1.05), cap, rough=0.9)
        ob.scale = (1.0, 1.0, 0.5)
    return 0.85


# =============================================================================
#  Ore seams
#
#  Every ore used to be the same grey boulder, told apart only by the word the
#  game printed over it -- so a new miner walked up to iron they could not touch
#  with nothing on screen to say which of the rocks around them was copper. Each
#  ore has its own rock now.
#
#  Two things do the telling, because one is not enough at forty pixels across:
#  the colour of the host stone, and *what is growing out of it*. Veins, half
#  buried nodules, crystals standing proud, banding, glassy chunks -- a shape
#  survives being reduced to pixel art where a tint alone washes out. Copper is
#  green veins and coal is black glass even when the two rocks are the same grey.
# =============================================================================
PALETTE.update({
    # What the ore is in: five host stones, cool to warm.
    "host_pale":   (0.640, 0.624, 0.592), "host_grey": (0.452, 0.444, 0.436),
    "host_dark":   (0.224, 0.216, 0.232), "host_warm": (0.560, 0.470, 0.360),
    "host_cool":   (0.396, 0.436, 0.500),
    # And the ore in it.
    "ore_copper":     (0.180, 0.720, 0.520),   # malachite: green, not the metal
    "ore_copper_hot": (0.870, 0.470, 0.220),
    "ore_iron":       (0.640, 0.310, 0.200),
    "ore_iron_dk":    (0.420, 0.200, 0.140),
    "ore_coal":       (0.090, 0.088, 0.104),
    "ore_coal_lt":    (0.240, 0.238, 0.264),
    "ore_azuryte":    (0.240, 0.540, 0.960),
    "ore_azuryte_lt": (0.580, 0.820, 1.000),
    "ore_damascus":   (0.740, 0.756, 0.792),
    "ore_damascus_dk": (0.330, 0.340, 0.390),
    "ore_orichalcum":  (0.930, 0.730, 0.250),
    "ore_orichalcum_dk": (0.660, 0.455, 0.140),
    "ore_diamond":    (0.880, 0.960, 1.000),
    "ore_diamond_dk": (0.620, 0.740, 0.840),
    "ore_platinum":   (0.820, 0.856, 0.892),
    "ore_platinum_dk": (0.480, 0.530, 0.600),
    "ore_demonite":   (0.430, 0.190, 0.560),
    "ore_demonite_hot": (0.960, 0.200, 0.150),
})

#            host stones (biggest lump first)       the ore      its accent            how it shows   glow
ORES = {
    "copper":     (("host_grey", "crag_dk", "crag"),     "ore_copper",     "ore_copper_hot",     "veins",    0.0),
    "iron":       (("crag_rust", "crag_dk", "crag"),     "ore_iron",       "ore_iron_dk",        "nodules",  0.0),
    # Grey rock, not black: jet chunks in a black boulder are a black boulder.
    "coal":       (("host_grey", "boulder", "host_pale"), "ore_coal",      "ore_coal_lt",        "chunks",   0.0),
    "azuryte":    (("host_pale", "boulder", "boulder_lt"), "ore_azuryte",  "ore_azuryte_lt",     "crystals", 1.15),
    "damascus":   (("host_grey", "host_dark", "boulder"), "ore_damascus",  "ore_damascus_dk",    "bands",    0.0),
    "orichalcum": (("host_warm", "crag_dk", "crag_lt"),  "ore_orichalcum", "ore_orichalcum_dk",  "nodules",  0.0),
    "diamond":    (("host_pale", "boulder_lt", "boulder"), "ore_diamond",  "ore_diamond_dk",     "crystals", 0.30),
    "platinum":   (("host_cool", "boulder_dk", "boulder"), "ore_platinum", "ore_platinum_dk",    "nodules",  0.0),
    "demonite":   (("host_dark", "host_dark", "boulder_dk"), "ore_demonite", "ore_demonite_hot", "crystals", 1.30),
}


def scenery_ore(seed, ore, big=True):
    """A boulder of the ore's own host stone with the ore showing in it.

    The ore is placed on the surface the camera can see -- up and toward -Y --
    rather than at some fraction of the way out from the middle of a lump, which
    is *inside* the rock: the first pass buried every vein and the nine ores came
    out as nine grey boulders."""
    host, vein, accent, style, glow = ORES[ore]
    rng = _rng(seed)
    base = 0.40 if big else 0.26
    lumps = []
    for k in range(rng.randint(2, 3)):
        a = k / 3.0 * math.tau + rng.uniform(-0.5, 0.5)
        d = base * rng.uniform(0.0, 0.55) if k else 0.0
        r = base * (1.0 if k == 0 else rng.uniform(0.45, 0.75))
        ys, zs = rng.uniform(0.80, 1.05), rng.uniform(0.52, 0.70)
        x, y = math.cos(a) * d, math.sin(a) * d
        ob = sphere("lump_%d" % k, r, (x, y, r * 0.55), host[k % 3], rough=0.98)
        ob.scale = (1.0, ys, zs)
        ob.rotation_euler = (0, rng.uniform(-0.25, 0.25), 0)
        lumps.append((x, y, r, ys, zs))

    def spot(i, out=0.90):
        """A point on the face of one of the lumps, up and toward the camera."""
        x, y, r, ys, zs = lumps[i % len(lumps)]
        a = rng.uniform(-2.5, -0.6)                    # round the upright; -Y is the camera
        t = rng.uniform(0.30, 0.95)                    # 0 at the top, pi/2 at the equator
        return (x + math.cos(a) * math.sin(t) * r * out,
                y + math.sin(a) * math.sin(t) * r * ys * out,
                r * 0.55 + math.cos(t) * r * zs * out,
                r)

    n = (6 if big else 4)
    if style == "veins":
        # Streaks lying along the rock, and a fleck or two of the raw metal.
        for i in range(n):
            x, y, z, r = spot(i)
            ob = sphere("vein_%d" % i, r * rng.uniform(0.22, 0.32), (x, y, z), vein, rough=0.55)
            ob.scale = (rng.uniform(1.6, 2.6), 0.75, 0.55)
            ob.rotation_euler = (0, 0, rng.uniform(0, math.tau))
        for i in range(3 if big else 2):
            x, y, z, r = spot(i + 1)
            sphere("fleck_%d" % i, r * 0.16, (x, y, z), accent, rough=0.35)
    elif style == "nodules":
        # Half-buried lumps of the metal itself.
        for i in range(n):
            x, y, z, r = spot(i)
            rr = r * (0.30 if i == 0 else rng.uniform(0.17, 0.26))
            ob = sphere("nodule_%d" % i, rr, (x, y, z), vein if i % 2 == 0 else accent, rough=0.35)
            ob.scale = (1.0, 0.85, 0.9)
    elif style == "crystals":
        # Spikes standing out of the rock, leaning away from where they grew.
        for i in range(n):
            x, y, z, r = spot(i, 0.80)
            h = r * rng.uniform(0.80, 1.20) * (1.45 if i == 0 else 1.0)
            lean = rng.uniform(0.15, 0.50)
            a = rng.uniform(0, math.tau)
            rot = (math.sin(a) * lean, -math.cos(a) * lean, 0)
            cone("shard_%d" % i, r * rng.uniform(0.19, 0.28), h, (x, y, z + h * 0.30),
                 vein, rot=rot, verts=6, rough=0.30, emit=glow)
            if i % 2 == 0:
                cone("shard_lt_%d" % i, r * 0.12, h * 0.6, (x, y - r * 0.05, z + h * 0.40),
                     accent, rot=rot, verts=6, rough=0.25, emit=glow * 1.3)
    elif style == "bands":
        # Watered steel: pale bands wrapping the rock, one over the other.
        for i in range(n):
            x, y, z, r = spot(i)
            ob = sphere("band_%d" % i, r * rng.uniform(0.30, 0.42), (x, y, z),
                        vein if i % 2 == 0 else accent, rough=0.45)
            ob.scale = (rng.uniform(2.0, 2.8), 0.80, 0.34)
            ob.rotation_euler = (0, rng.uniform(-0.3, 0.3), rng.uniform(-0.6, 0.6))
    else:   # chunks: coal, which is glassy and breaks square
        for i in range(n):
            x, y, z, r = spot(i)
            blk("chunk_%d" % i, (r * rng.uniform(0.30, 0.46), r * rng.uniform(0.26, 0.38),
                                 r * rng.uniform(0.26, 0.40)),
                (x, y, z), vein if i % 3 else accent, rough=0.18,
                rot=(rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5), rng.uniform(0, math.tau)), bev=0.012)
    return (1.35 if big else 0.95)


# Each ore twice over, so a hillside of copper is not the same rock stamped out.
ORE_NAMES = []
for _ore in ORES:
    for _v in range(2):
        ORE_NAMES.append("ore_%s_%d" % (_ore, _v))
        ORE_NAMES.append("oresmall_%s_%d" % (_ore, _v))


def _scenery_table():
    """Names to (builder, framed pixels), matching what maps and genmaps already
    ask for by name."""
    out = {}
    for i in range(10):
        out["tree_%02d" % i] = ((lambda s=i: scenery_tree(400 + s, True)), 128)
        out["treesmall_%02d" % i] = ((lambda s=i: scenery_tree(500 + s, False)), 64)
    for i in range(8):
        out["rock_%02d" % i] = ((lambda s=i: scenery_rock(600 + s, True)), 64)
        out["rocksmall_%02d" % i] = ((lambda s=i: scenery_rock(700 + s, False)), 32)
        out["bush_%02d" % i] = ((lambda s=i: scenery_bush(800 + s, True)), 64)
        out["bushsmall_%02d" % i] = ((lambda s=i: scenery_bush(900 + s, False)), 32)
    for i, px in enumerate((128, 64, 32, 128, 64, 64)):
        out["mushroom_%02d" % i] = ((lambda s=i: scenery_mushroom(1000 + s, s in (0, 3))), px)
    for i in range(3):
        out["fungus_%02d" % i] = ((lambda s=i: scenery_fungus(1100 + s)), 32)
    # One rock per ore, so what is in it can be seen before it is mined.
    for i, ore in enumerate(ORES):
        for v in range(2):
            out["ore_%s_%d" % (ore, v)] = ((lambda o=ore, k=i, j=v: scenery_ore(1300 + k * 10 + j, o, True)), 64)
            out["oresmall_%s_%d" % (ore, v)] = ((lambda o=ore, k=i, j=v: scenery_ore(1400 + k * 10 + j, o, False)), 32)
    # What a felled tree leaves, at each size of tree.
    out["stump"] = ((lambda: scenery_stump(1200, True)), 48)
    out["stumpsmall"] = ((lambda: scenery_stump(1201, False)), 32)
    return out


SCENERY = _scenery_table()


# =============================================================================
#  Buildings, the guild's furniture, and the small things on the ground
#
#  The last of the pack art: four buildings, the guild hall's insides, the
#  chests and doors a dungeon needs, a campfire and an arrow. Most of the
#  guild's furniture is furniture this file already builds -- a bench is a
#  bench -- so those entries are aliases rather than new models, which also
#  keeps the hall matching the inn it stands across the square from.
# =============================================================================
PALETTE.update({
    "wall_daub":   (0.78, 0.71, 0.58), "wall_daub_dk": (0.64, 0.57, 0.46),
    "beam":        (0.36, 0.25, 0.17), "beam_dk": (0.26, 0.18, 0.12),
    "roof_tile":   (0.44, 0.29, 0.25), "roof_tile_dk": (0.33, 0.21, 0.19),
    "guild_stone": (0.52, 0.51, 0.52), "guild_stone_dk": (0.38, 0.37, 0.39),
    "awning_red":  (0.58, 0.24, 0.22), "awning_cream": (0.83, 0.79, 0.68),
    "flame_hot":   (1.00, 0.82, 0.32), "flame_mid": (0.95, 0.52, 0.16),
    "cushion":     (0.45, 0.24, 0.26), "cushion_dk": (0.33, 0.17, 0.19),
    "paper":       (0.88, 0.85, 0.76),
})


def _timber_frame(W, D, H, beams=4):
    """Daub panels between upright beams, with a sill and a top plate: the
    front of every ordinary building in the Hollowmarch."""
    blk("walls", (W, D, H), (0, 0, H / 2), "wall_daub", bev=0.02)
    blk("sill", (W + 0.08, D + 0.08, 0.10), (0, 0, 0.05), "beam_dk")
    blk("plate", (W + 0.06, D + 0.06, 0.10), (0, 0, H - 0.05), "beam")
    for i in range(beams):
        x = -W / 2 + (i + 0.5) * W / beams
        blk("post_%d" % i, (0.10, D + 0.02, H), (x, 0, H / 2), "beam")
    for sx in (-1, 1):
        blk("corner_%d" % sx, (0.12, D + 0.04, H), (sx * (W / 2 - 0.05), 0, H / 2), "beam")


def _door(W, D, z=0.0, h=0.95):
    blk("door", (0.62, 0.10, h), (0, -D / 2 - 0.02, z + h / 2), "oak")
    for k in (0.25, 0.72):
        blk("door_plank_%.2f" % k, (0.52, 0.02, 0.05), (0, -D / 2 - 0.08, z + h * k), "oak_light")
    blk("knob", (0.06, 0.05, 0.06), (0.20, -D / 2 - 0.09, z + h * 0.48), "brass", metal=0.8)
    blk("step", (0.86, 0.30, 0.08), (0, -D / 2 - 0.20, 0.04), "stone_pale")


def prop_building_house_a():
    """A cottage: one storey of timber frame under a steep tiled roof, with a
    chimney. The door is centred at the front because the map hangs its portal
    there."""
    W, D, H = 2.30, 1.70, 1.15
    _timber_frame(W, D, H, beams=3)
    _door(W, D)
    window("win_l", -0.76, -D / 2 - 0.02, 0.72, w=0.42, h=0.42)
    window("win_r", 0.76, -D / 2 - 0.02, 0.72, w=0.42, h=0.42)
    gable_roof("roof", W + 0.36, D + 0.36, H, 42, "roof_tile", thick=0.10, overhang=0.20)
    blk("chimney", (0.30, 0.30, 0.95), (W / 2 - 0.42, 0.34, H + 0.48), "stone")
    blk("chimney_cap", (0.38, 0.38, 0.10), (W / 2 - 0.42, 0.34, H + 0.98), "stone_pale")
    return 3.1


def prop_building_house_b():
    """A townhouse: two storeys, the upper one jettied out over the street, a
    steeper roof and two chimneys. Taller than the cottage so a street of them
    is not a row of the same shape."""
    W, D = 2.10, 1.60
    G, U = 1.15, 1.00
    _timber_frame(W, D, G, beams=3)
    _door(W, D)
    window("gwin", 0.70, -D / 2 - 0.02, 0.70, w=0.40, h=0.40)
    # The jetty: the upper storey hangs forward on a moulded bressumer.
    JET = 0.16
    blk("bressumer", (W + 0.10, D + JET + 0.10, 0.12), (0, -JET / 2, G + 0.06), "beam")
    blk("upper", (W + 0.02, D + JET, U), (0, -JET / 2, G + U / 2 + 0.10), "wall_daub", bev=0.02)
    for i in range(3):
        x = -W / 2 + (i + 0.5) * W / 3
        blk("upost_%d" % i, (0.09, D + JET + 0.02, U), (x, -JET / 2, G + U / 2 + 0.10), "beam")
    window("uwin_l", -0.58, -D / 2 - JET - 0.02, G + 0.62, w=0.40, h=0.42)
    window("uwin_r", 0.58, -D / 2 - JET - 0.02, G + 0.62, w=0.40, h=0.42)
    gable_roof("roof", W + 0.40, D + JET + 0.40, G + U + 0.12, 46, "roof_tile",
               thick=0.10, overhang=0.20)
    for sx in (-1, 1):
        blk("chimney_%d" % sx, (0.26, 0.26, 0.85), (sx * (W / 2 - 0.30), 0.30, G + U + 0.52), "stone")
    return 3.8


def prop_building_shop():
    """A shopfront: low and wide, with a striped awning over an open counter
    and the shutters folded down. Reads as somewhere to buy something rather
    than somewhere to live."""
    W, D, H = 2.20, 1.40, 1.05
    _timber_frame(W, D, H, beams=3)
    _door(W, D, h=0.90)
    # The open front: a counter under a striped awning, to the right of the door.
    blk("counter", (0.95, 0.18, 0.12), (0.62, -D / 2 - 0.14, 0.62), "oak_light")
    blk("counter_front", (0.95, 0.06, 0.55), (0.62, -D / 2 - 0.20, 0.34), "oak")
    blk("shutter", (1.00, 0.06, 0.40), (0.62, -D / 2 - 0.30, 0.86),
        "oak_light", rot=(math.radians(-24), 0, 0))
    stripes = 5
    for i in range(stripes):
        x = 0.62 - 0.55 + (i + 0.5) * 1.10 / stripes
        blk("awning_%d" % i, (1.10 / stripes + 0.005, 0.62, 0.05), (x, -D / 2 - 0.30, 1.02),
            ("awning_red", "awning_cream")[i % 2], rot=(math.radians(-18), 0, 0), bev=0.004)
    window("win_l", -0.72, -D / 2 - 0.02, 0.66, w=0.38, h=0.38)
    gable_roof("roof", W + 0.34, D + 0.34, H, 34, "shingle", thick=0.09, overhang=0.22)
    return 2.9


def prop_building_guild():
    """The guild hall: stone rather than timber, wider than it is tall, with a
    porch on two columns and a banner over the door."""
    W, D, H = 2.90, 1.90, 1.35
    import random
    rng = random.Random(58)
    course = 0.24
    for row in range(int(H / course)):
        z = course / 2 + row * course
        x = -W / 2 + (0.20 if row % 2 else 0.0)
        while x < W / 2 - 0.05:
            w = 0.44 * (0.75 + rng.random() * 0.5)
            w = min(w, W / 2 - x)
            cx = x + w / 2
            if not (abs(cx) < 0.42 and z < 1.00):
                blk("stone_%d_%.2f" % (row, x), (w - 0.03, 0.10, course - 0.03),
                    (cx, -D / 2, z), ("guild_stone", "guild_stone_dk")[rng.randrange(2)], bev=0.02)
            x += w
    blk("core", (W - 0.02, D - 0.10, H), (0, 0.04, H / 2), "guild_stone", bev=0)
    blk("plinth", (W + 0.12, D + 0.12, 0.12), (0, 0, 0.06), "stone_pale")
    _door(W, D, h=1.00)
    for sx in (-1, 1):
        cyl("column_%d" % sx, 0.11, 1.10, (sx * 0.60, -D / 2 - 0.30, 0.55), "stone_pale", verts=12)
        cyl("capital_%d" % sx, 0.14, 0.10, (sx * 0.60, -D / 2 - 0.30, 1.14), "stone_pale", verts=12)
    blk("porch", (1.60, 0.70, 0.12), (0, -D / 2 - 0.30, 1.24), "shingle", bev=0.02)
    blk("banner", (0.46, 0.05, 0.62), (0, -D / 2 - 0.09, 1.02), "cloth_red")
    blk("banner_trim", (0.46, 0.06, 0.08), (0, -D / 2 - 0.10, 0.74), "brass", metal=0.7)
    window("win_l", -1.02, -D / 2 - 0.02, 0.80, w=0.46, h=0.52)
    window("win_r", 1.02, -D / 2 - 0.02, 0.80, w=0.46, h=0.52)
    gable_roof("roof", W + 0.40, D + 0.40, H, 32, "shingle", thick=0.12, overhang=0.22)
    return 3.6


def prop_sign_guild():
    """A hanging sign on a bracket: the board is what carries, so it is wide
    and plain with a painted device on it."""
    cyl("post", 0.05, 1.10, (-0.62, 0, 0.55), "oak", verts=8)
    blk("arm", (1.05, 0.06, 0.07), (-0.10, 0, 1.02), "oak")
    blk("brace", (0.34, 0.05, 0.05), (-0.42, 0, 0.84), "oak", rot=(0, math.radians(-40), 0))
    for x in (-0.48, 0.26):
        cyl("ring_%.2f" % x, 0.035, 0.03, (x, 0, 0.96), "iron", rot=(math.pi / 2, 0, 0),
            verts=10, metal=0.7)
    blk("board", (0.86, 0.07, 0.44), (-0.11, 0, 0.70), "oak_light", bev=0.02)
    blk("board_edge", (0.90, 0.05, 0.05), (-0.11, 0, 0.90), "oak")
    blk("device", (0.26, 0.04, 0.26), (-0.11, -0.05, 0.70), "cloth_red")
    blk("device_bar", (0.30, 0.03, 0.06), (-0.11, -0.06, 0.70), "brass", metal=0.7)
    return 1.5


def _chest(open_lid):
    """A banded chest. The open one has its lid back and a little gold showing,
    because an open chest the player has already looted should read as looted
    from across the room."""
    blk("body", (0.66, 0.44, 0.34), (0, 0, 0.17), "oak", bev=0.02)
    blk("band_l", (0.06, 0.46, 0.36), (-0.22, 0, 0.18), "iron", metal=0.6)
    blk("band_r", (0.06, 0.46, 0.36), (0.22, 0, 0.18), "iron", metal=0.6)
    blk("lock", (0.12, 0.05, 0.12), (0, -0.23, 0.30), "brass", metal=0.8)
    if open_lid:
        blk("lid", (0.68, 0.40, 0.10), (0, 0.20, 0.52), "oak_light",
            rot=(math.radians(-72), 0, 0), bev=0.02)
        for k, (x, y) in enumerate(((-0.14, -0.02), (0.10, 0.06), (0.0, -0.10))):
            sphere("coin_%d" % k, 0.055, (x, y, 0.36), "brass")
    else:
        blk("lid", (0.68, 0.46, 0.14), (0, 0, 0.41), "oak_light", bev=0.03)
        blk("lid_band", (0.06, 0.48, 0.16), (-0.22, 0, 0.41), "iron", metal=0.6)
        blk("lid_band_r", (0.06, 0.48, 0.16), (0.22, 0, 0.41), "iron", metal=0.6)
    return 1.15


def prop_chest():
    return _chest(False)


def prop_chest_open():
    return _chest(True)


def _door_panel(is_open):
    """A door in a frame, seen from the front. The open one swings inward and
    shows the dark of whatever is behind it."""
    blk("frame_l", (0.10, 0.16, 1.20), (-0.44, 0, 0.60), "beam")
    blk("frame_r", (0.10, 0.16, 1.20), (0.44, 0, 0.60), "beam")
    blk("lintel", (1.00, 0.16, 0.12), (0, 0, 1.22), "beam")
    blk("dark", (0.78, 0.06, 1.16), (0, 0.06, 0.58), "coal", bev=0)
    if is_open:
        blk("leaf", (0.74, 0.08, 1.14), (-0.30, 0.30, 0.57), "oak",
            rot=(0, 0, math.radians(-64)), bev=0.02)
    else:
        blk("leaf", (0.76, 0.08, 1.16), (0, -0.04, 0.58), "oak", bev=0.02)
        for k in (0.30, 0.86):
            blk("brace_%.2f" % k, (0.66, 0.03, 0.06), (0, -0.09, 1.16 * k), "oak_light")
        blk("ring", (0.10, 0.04, 0.10), (0.24, -0.10, 0.56), "iron", metal=0.7)
    return 1.5


def prop_door():
    return _door_panel(False)


def prop_door_open():
    # The leaf swings out past the frame, so this one needs more room.
    return _door_panel(True) * 1.18


def prop_campfire():
    """A lit fire: stones round a stack of burning wood. The flame is emissive
    so it survives the night's light map."""
    import random
    rng = random.Random(12)
    for k in range(9):
        a = k / 9 * math.tau
        r = 0.44
        sphere("stone_%d" % k, rng.uniform(0.09, 0.13),
               (math.cos(a) * r, math.sin(a) * r, 0.06), ("stone", "stone_pale")[k % 2])
    for k in range(5):
        a = k / 5 * math.tau + 0.3
        cyl("log_%d" % k, 0.055, 0.62, (math.cos(a) * 0.10, math.sin(a) * 0.10, 0.20),
            "log_dk", rot=(math.radians(58), 0, a), verts=8)
    sphere("ember", 0.17, (0, 0, 0.16), "flame_mid", emit=1.6)
    sphere("flame", 0.15, (0, 0, 0.34), "flame_hot", emit=2.4)
    sphere("flame_tip", 0.085, (0.02, -0.02, 0.50), "flame_hot", emit=2.6)
    return 1.3


def prop_arrow():
    """An arrow in flight, pointing away from the archer -- up the screen, so
    the game turns it to whatever direction it was loosed in."""
    cyl("shaft", 0.028, 1.05, (0, 0, 0.10), "twig", rot=(math.radians(90), 0, 0), verts=8)
    cone("head", 0.075, 0.26, (0, -0.62, 0.10), "iron", rot=(math.radians(-90), 0, 0),
         verts=8)
    for sx in (-1, 1):
        blk("fletch_%d" % sx, (0.02, 0.22, 0.14), (sx * 0.03, 0.42, 0.12), "cloth_cream",
            rot=(0, math.radians(sx * 14), 0), bev=0.004)
    return 1.45


def prop_guild_noticeboard():
    """A board on two posts with notices pinned to it, some of them crooked."""
    import random
    rng = random.Random(77)
    for sx in (-1, 1):
        cyl("post_%d" % sx, 0.055, 1.00, (sx * 0.52, 0.06, 0.50), "oak", verts=8)
    blk("board", (1.24, 0.08, 0.78), (0, 0, 0.78), "oak_light", bev=0.02)
    blk("board_frame", (1.32, 0.06, 0.08), (0, -0.02, 1.16), "oak")
    blk("board_sill", (1.32, 0.10, 0.08), (0, -0.02, 0.40), "oak")
    for k in range(5):
        x = -0.44 + (k % 3) * 0.44
        z = 0.62 + (k // 3) * 0.30
        blk("note_%d" % k, (0.26, 0.02, 0.20), (x, -0.05, z), "paper",
            rot=(0, 0, rng.uniform(-0.12, 0.12)), bev=0.004)
    gable_roof("hood", 1.44, 0.44, 1.20, 26, "shingle", thick=0.06, overhang=0.10)
    return 1.9


def prop_guild_couch():
    """A padded couch: a frame, two cushions and a rolled arm at each end."""
    blk("frame", (1.30, 0.66, 0.16), (0, 0, 0.26), "oak")
    for sx in (-1, 1):
        blk("leg_f_%d" % sx, (0.09, 0.09, 0.20), (sx * 0.56, -0.24, 0.10), "oak_dk" if "oak_dk" in PALETTE else "oak")
        blk("leg_b_%d" % sx, (0.09, 0.09, 0.20), (sx * 0.56, 0.24, 0.10), "oak_dk" if "oak_dk" in PALETTE else "oak")
        cyl("arm_%d" % sx, 0.15, 0.62, (sx * 0.60, 0, 0.44), "cushion",
            rot=(math.radians(90), 0, 0), verts=12)
    blk("seat", (1.16, 0.60, 0.16), (0, 0, 0.42), "cushion", bev=0.04)
    blk("back", (1.16, 0.18, 0.44), (0, 0.26, 0.64), "cushion", bev=0.04)
    for sx in (-1, 1):
        blk("cushion_%d" % sx, (0.52, 0.48, 0.12), (sx * 0.28, -0.04, 0.53), "cushion_dk", bev=0.05)
    return 1.9


def _wider(builder, factor):
    """The same prop framed with more room round it. The builders return the
    width of square the camera should frame, so a factor above one zooms out;
    four of the aliases below were touching the top of their frame."""
    def build():
        return builder() * factor
    return build


STRUCTURES = {
    # Buildings.
    "building_house_a": (prop_building_house_a, 144),
    "building_house_b": (prop_building_house_b, 160),
    "building_shop":    (prop_building_shop,    128),
    "building_guild":   (prop_building_guild,   160),
    "sign_guild":       (prop_sign_guild,        72),
    # The small things.
    "chest":      (prop_chest,      32),
    "chest_open": (prop_chest_open, 32),
    "door":       (prop_door,       32),
    "door_open":  (prop_door_open,  32),
    "campfire":   (prop_campfire,   48),
    "arrow":      (prop_arrow,      24),
    # The guild hall's insides. Most of it is furniture this file already
    # builds, so the hall matches the inn across the square.
    "guild_noticeboard": (prop_guild_noticeboard, 48),
    "guild_couch":       (prop_guild_couch,       56),
    "guild_bench":       (prop_bench,             48),
    "guild_settle":      (prop_tavern_bench,      48),
    "guild_chair":       (_wider(prop_chair, 1.18),    24),
    "guild_table":       (prop_table_round,       56),
    "guild_desk":        (prop_writing_desk,      56),
    "guild_cabinet":     (prop_wardrobe,          56),
    "guild_bookshelf":   (_wider(prop_bookshelf, 1.12), 56),
    "guild_bookshelf_b": (prop_cottage_bookshelf, 56),
    "guild_chest":       (prop_travel_chest,      32),
    "guild_rug":         (prop_rug,               72),
    "guild_banner":      (prop_banner,            48),
    "guild_weapon_rack": (prop_weapon_rack,       48),
    "guild_armour_rack": (prop_armour_stand,      56),
    "guild_rack":        (prop_tool_rack,         48),
    "guild_plant":       (prop_herb_pots,         36),
    "guild_door":        (prop_room_door,         40),
}
SCENERY.update(STRUCTURES)


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
PROPS.update(FORGE_PROPS)
PROPS.update(INN_PROPS)
PROPS.update(ROOM_PROPS)
PROPS.update(BUILDING_PROPS)
PROPS.update(WOODLAND_PROPS)
PROPS.update(HERB_PROPS)
PROPS.update(TOTEM_PROPS)
PROPS.update(COLLEGE_PROPS)
PROPS.update(CLOTHIER_PROPS)

# The Brimstone Palace's props are in tools/blender_palace.py, built with the
# tools above. This module is handed to it rather than imported by it: run by
# Blender this file is __main__, and importing it by name would run it twice.
sys.modules.setdefault("blender_props", sys.modules[__name__])
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_palace  # noqa: E402

PROPS.update(blender_palace.PROPS)


def main():
    only = None
    table = PROPS
    if "--" in sys.argv:
        rest = sys.argv[sys.argv.index("--") + 1:]
        # The scenery is a second family, written to assets/objects/ rather
        # than assets/props/, because that is where the maps look for it.
        if "--objects" in rest:
            table = SCENERY
            rest = [x for x in rest if x != "--objects"]
        if rest:
            only = set(rest)

    for name, (builder, out_px) in sorted(table.items()):
        if only and name not in only:
            continue
        clear_scene()
        setup_world()
        # A builder returns the width of square it wants framed, or that and
        # a camera elevation. Furniture is seen from low down, the way the
        # CraftPix interiors draw it; a building is seen from much higher, the
        # way the CraftPix exteriors draw theirs, so the roof is most of it.
        framed = builder()
        span, elevation = framed if isinstance(framed, tuple) else (framed, None)
        setup_camera(span, elevation)
        setup_render(out_px * SUPERSAMPLE)
        path = os.path.join(RENDER_DIR, "%s.png" % name)
        render_to(path)
        print("rendered %-14s %dpx -> %s" % (name, out_px * SUPERSAMPLE, path))


if __name__ == "__main__":
    main()
