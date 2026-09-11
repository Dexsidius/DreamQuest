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
    "clay":       (0.694, 0.408, 0.286),
    "wool_green": (0.365, 0.498, 0.318),
    "chalk":      (0.188, 0.200, 0.192),
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


BUILDING_PROPS = {
    "inn_building": (prop_inn_building, 192),
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
