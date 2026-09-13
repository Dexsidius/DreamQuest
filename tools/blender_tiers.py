# =============================================================================
#  blender_tiers.py - the art for the nine material tiers.
#
#  Run headless (tools/make_tiers.ps1 does this):
#      blender --background --python tools/blender_tiers.py -- [icons] [layers]
#          [--only CLIP,CLIP] [--models sword_iron,bow_wood]
#
#  Wood, Bronze, Iron, Steel, Azuryte, Adamantium, Diamond, Platinum, Demonrite.
#  Every tier gets its own ore, bar, sword, bow, staff, shield, helm, cuirass and
#  greaves, modelled here from the same rounded parts and cel shading as the
#  player hero (tools/blender_character.py, which this imports), so the icons
#  and the weapon in the hero's hand are the same object seen two ways:
#
#    icons    One 32px inventory icon per item, rendered at four times the size
#             and reduced by majority colour with the hero's selective outline,
#             into assets/icons/tiers/.
#    layers   For the sword, bow and staff of every tier, a weapon layer for
#             each of the hero's clips -- layers/<clip>_4_weapon_<model>.png --
#             posed in the hero's hand frame by frame and cut by the body and
#             head exactly as the hero's own sword layer is. The game draws the
#             one for whatever is equipped.
#
#  Telling the tiers apart is done three ways at once, because at game size a
#  colour on its own is not enough: each tier has its own palette, its own
#  silhouette (a wooden sword is short and blunt, a platinum one long with a
#  winged guard, a demonrite one jagged), and the top tiers carry something
#  that glows -- azuryte's cyan edge, diamond's white facets, demonrite's red.
# =============================================================================

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_character as bc  # noqa: E402

import bmesh  # noqa: E402
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Euler, Matrix, Vector  # noqa: E402

ICON_DIR = os.path.join(bc.ROOT, "assets", "icons", "tiers")
RENDER_DIR = os.path.join(bc.ROOT, "assets", "_render", "tiers")
ICON_PX = 32

TIERS = ["wood", "bronze", "iron", "steel", "azuryte", "adamantium", "diamond", "platinum", "demonrite"]

# Per tier: the main material, a lighter one for edges and highlights, a dark
# one for fittings, the grip, an accent, and a glow (None for no glow).
PALETTES = {
    "wood":       dict(main=(0.66, 0.47, 0.28), light=(0.80, 0.62, 0.40), dark=(0.42, 0.28, 0.17),
                       grip=(0.34, 0.22, 0.14), accent=(0.55, 0.39, 0.23), glow=None),
    "bronze":     dict(main=(0.82, 0.54, 0.27), light=(0.98, 0.76, 0.46), dark=(0.56, 0.34, 0.16),
                       grip=(0.40, 0.25, 0.15), accent=(0.70, 0.46, 0.22), glow=None),
    "iron":       dict(main=(0.56, 0.58, 0.63), light=(0.74, 0.76, 0.80), dark=(0.33, 0.33, 0.37),
                       grip=(0.30, 0.21, 0.15), accent=(0.44, 0.44, 0.49), glow=None),
    "steel":      dict(main=(0.84, 0.88, 0.94), light=(0.98, 0.99, 1.00), dark=(0.50, 0.55, 0.64),
                       grip=(0.20, 0.23, 0.36), accent=(0.93, 0.76, 0.34), glow=None),
    "azuryte":    dict(main=(0.24, 0.48, 0.94), light=(0.50, 0.74, 1.00), dark=(0.13, 0.22, 0.54),
                       grip=(0.12, 0.16, 0.32), accent=(0.84, 0.88, 0.96), glow=(0.52, 0.94, 1.00)),
    "adamantium": dict(main=(0.20, 0.54, 0.36), light=(0.44, 0.80, 0.58), dark=(0.10, 0.25, 0.17),
                       grip=(0.18, 0.15, 0.13), accent=(0.66, 0.70, 0.62), glow=None),
    "diamond":    dict(main=(0.74, 0.93, 1.00), light=(1.00, 1.00, 1.00), dark=(0.46, 0.74, 0.88),
                       grip=(0.30, 0.36, 0.48), accent=(0.60, 0.86, 0.98), glow=(0.86, 1.00, 1.00)),
    "platinum":   dict(main=(0.93, 0.91, 0.85), light=(1.00, 1.00, 0.96), dark=(0.66, 0.63, 0.56),
                       grip=(0.48, 0.16, 0.19), accent=(0.96, 0.78, 0.32), glow=(1.00, 0.94, 0.68)),
    "demonrite":  dict(main=(0.30, 0.15, 0.18), light=(0.56, 0.26, 0.28), dark=(0.14, 0.07, 0.09),
                       grip=(0.12, 0.07, 0.08), accent=(0.42, 0.14, 0.16), glow=(1.00, 0.26, 0.12)),
}

for _tier, _pal in PALETTES.items():
    for _key, _rgb in _pal.items():
        if _rgb is not None:
            bc.PALETTE["%s_%s" % (_tier, _key)] = _rgb
bc.PALETTE["string"] = (0.92, 0.88, 0.78)
bc.PALETTE["rock"] = (0.46, 0.43, 0.42)
bc.PALETTE["rock_dark"] = (0.30, 0.28, 0.29)
bc.PALETTE["leather"] = (0.46, 0.31, 0.20)

_toon = bc.material


def material(colour):
    """The hero's cel material, except that anything named *_glow is flat
    light: it ignores the sun, so it reads as lit from inside."""
    if not colour.endswith("_glow"):
        return _toon(colour)
    if colour in bc._materials:
        return bc._materials[colour]
    mat = bpy.data.materials.new("glow_" + colour)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    emit = nodes.new("ShaderNodeEmission")
    rgb = bc.to_linear(bc.PALETTE[colour])
    emit.inputs["Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    emit.inputs["Strength"].default_value = 1.0
    links.new(emit.outputs["Emission"], out.inputs["Surface"])
    bc._materials[colour] = mat
    return mat


bc.material = material


def mesh_box(sx, sy, sz, taper=1.0):
    """A box centred on its origin; the top face is scaled by taper, so a
    taper under one makes an ingot."""
    key = ("box", sx, sy, sz, taper)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        k = taper if v.co.z > 0 else 1.0
        v.co = Vector((v.co.x * sx * k, v.co.y * sy * k, v.co.z * sz))
    me = bpy.data.meshes.new("box")
    bm.to_mesh(me)
    bm.free()
    bc._meshes[key] = me
    return me


def mesh_gem(rx, ry, rz, sides=6):
    """A faceted crystal: flat shaded, so every face is its own band."""
    key = ("gem", rx, ry, rz, sides)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=sides, v_segments=3, radius=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * rx, v.co.y * ry, v.co.z * rz))
    me = bpy.data.meshes.new("gem")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    bc._meshes[key] = me
    return me


def P(tier, key):
    return "%s_%s" % (tier, key)


def glow_or(tier, fallback):
    return P(tier, "glow") if PALETTES[tier]["glow"] else P(tier, fallback)


# --- weapons ------------------------------------------------------------------------
# All three are built in the frame of the hero's grip: the hand holds the origin,
# a sword's blade runs down -Z from the guard, and a staff's head is up +Z.

SWORDS = {
    #             blade  width  guard  extras
    "wood":       (0.34, 0.058, 0.00, ""),
    "bronze":     (0.40, 0.070, 0.07, "leaf"),
    "iron":       (0.46, 0.050, 0.10, ""),
    "steel":      (0.54, 0.052, 0.13, "pommel"),
    "azuryte":    (0.56, 0.056, 0.12, "edge gem"),
    "adamantium": (0.52, 0.085, 0.15, "heavy"),
    "diamond":    (0.60, 0.058, 0.10, "crystal edge"),
    "platinum":   (0.66, 0.060, 0.12, "wings gem pommel"),
    "demonrite":  (0.64, 0.066, 0.13, "spikes horns edge gem"),
}


def build_sword(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    hilt = P(tier, "grip")
    fitting = P(tier, "accent") if "pommel" in extras or tier == "platinum" else P(tier, "dark")

    add("hilt", bc.mesh_capsule(0.021, 0.021, 0.10), hilt, parent, loc=(0, 0, 0.06))
    add("pommel", bc.mesh_ellipsoid(0.030, 0.030, 0.030), fitting, parent, loc=(0, 0, 0.09))
    if guard > 0:
        add("guard", bc.mesh_ellipsoid(guard, 0.032, 0.028), fitting, parent, loc=(0, 0, -0.05))

    top = -0.06
    if tier == "wood":
        add("blade", bc.mesh_capsule(width, width * 0.8, blade, squash_y=0.45), P(tier, "main"), parent,
            loc=(0, 0, top))
    elif "leaf" in extras:
        add("blade", bc.mesh_capsule(width * 0.6, 0.012, blade, squash_y=0.35), P(tier, "main"), parent,
            loc=(0, 0, top))
        add("leaf", bc.mesh_ellipsoid(width, width * 0.3, blade * 0.34), P(tier, "light"), parent,
            loc=(0, 0, top - blade * 0.55))
    elif "heavy" in extras:
        add("blade", bc.mesh_capsule(width * 0.8, width, blade, squash_y=0.30), P(tier, "main"), parent,
            loc=(0, 0, top))
        add("spine", bc.mesh_capsule(width * 0.35, width * 0.35, blade * 0.85, squash_y=0.9), P(tier, "light"),
            parent, loc=(width * 0.45, 0, top - 0.02))
    elif "crystal" in extras:
        add("blade", mesh_gem(width, width * 0.45, blade * 0.55, sides=4), P(tier, "main"), parent,
            loc=(0, 0, top - blade * 0.5))
    else:
        add("blade", bc.mesh_capsule(width, 0.012, blade, squash_y=0.35), P(tier, "main"), parent,
            loc=(0, 0, top))
        add("fuller", bc.mesh_capsule(width * 0.35, 0.008, blade * 0.8, squash_y=0.55), P(tier, "light"),
            parent, loc=(0, 0, top - 0.02))

    if "edge" in extras:
        add("edge", bc.mesh_capsule(width * 0.32, 0.008, blade * 0.9, squash_y=0.75), glow_or(tier, "light"),
            parent, loc=(0, 0, top - 0.03))
    if "gem" in extras:
        add("gem", mesh_gem(0.026, 0.02, 0.03), glow_or(tier, "light"), parent, loc=(0, -0.03, -0.05))
    if "wings" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("wing", (side * 0.05, 0, -0.05), (side * 0.19, 0, 0.05), 0.03,
                                  P(tier, "accent"), parent))
    if "horns" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.08, 0, -0.05), (side * 0.16, 0, -0.14), 0.032,
                                  P(tier, "light"), parent))
    if "spikes" in extras:
        for i in range(3):
            z = top - blade * (0.25 + 0.22 * i)
            for side in (-1, 1):
                parts.append(bc.spike("barb", (side * width * 0.6, 0, z), (side * width * 1.9, 0, z + 0.05),
                                      0.022, P(tier, "main"), parent))
    return parts


BOWS = {
    #             half   bulge  thick  extras
    "wood":       (0.34, 0.12, 0.026, ""),
    "bronze":     (0.36, 0.12, 0.028, "tips"),
    "iron":       (0.38, 0.11, 0.030, "bands"),
    "steel":      (0.42, 0.10, 0.028, "recurve tips"),
    "azuryte":    (0.44, 0.12, 0.030, "recurve glowstring gem"),
    "adamantium": (0.42, 0.09, 0.040, "bands tips"),
    "diamond":    (0.46, 0.12, 0.030, "crystal"),
    "platinum":   (0.48, 0.12, 0.030, "recurve wings gem"),
    "demonrite":  (0.48, 0.13, 0.034, "recurve spikes glowstring"),
}


def build_bow(tier, parent):
    half, bulge, thick, extras = BOWS[tier]
    parts = []
    # Built bulging along -Y, then turned so the arc swings out to the
    # character's right as well as forward: edge-on from any one side a bow is
    # a line, and this way it is an arc from the front and from the side.
    #
    # The hand hangs with the top of anything it holds leaning in over the
    # shoulder, where the arm hides it. So the bow is also stood up straighter
    # and carried a little out and forward, clear of the sleeve.
    frame = bc.empty("bow_frame", (-0.10, -0.07, 0.02), parent)
    frame.rotation_euler = Euler((0, math.radians(-32), math.radians(-40)), "XYZ")
    parts.append(frame)
    parent = frame
    limb = P(tier, "main") if tier != "wood" else P(tier, "main")
    tips = []
    for sign in (1, -1):
        # Three segments per limb, so the arc is a curve rather than a V.
        pts = [(0, 0, 0), (0, -bulge * 0.9, sign * half * 0.45), (0, -bulge * 0.55, sign * half * 0.85),
               (0, -bulge * (0.05 if "recurve" in extras else 0.35), sign * half)]
        for i in range(3):
            parts.append(bc.spike("limb", pts[i], pts[i + 1], thick * (1.0 - 0.18 * i), limb, parent,
                                  r_tip=thick * (0.82 - 0.18 * i)))
        tips.append(Vector(pts[3]))
        if "tips" in extras:
            parts.append(bc.part("tip", bc.mesh_ellipsoid(0.03, 0.03, 0.04), P(tier, "light"), parent,
                                 loc=pts[3]))
        if "crystal" in extras:
            parts.append(bc.part("tip", mesh_gem(0.035, 0.03, 0.07), P(tier, "light"), parent, loc=pts[3]))
        if "wings" in extras:
            parts.append(bc.spike("wing", pts[2], (0, -bulge * 1.4, sign * half * 1.0), 0.026,
                                  P(tier, "accent"), parent))
        if "spikes" in extras:
            parts.append(bc.spike("barb", pts[1], (0, -bulge * 1.9, sign * half * 0.62), 0.026,
                                  P(tier, "light"), parent))
        if "bands" in extras:
            parts.append(bc.part("band", bc.mesh_ellipsoid(0.04, 0.04, 0.018), P(tier, "dark"), parent,
                                 loc=pts[2]))
    string = glow_or(tier, "light") if "glowstring" in extras else "string"
    length = (tips[0] - tips[1]).length
    parts.append(bc.part("string", bc.mesh_capsule(0.008, 0.008, length), string, parent,
                         loc=tips[0]))
    parts.append(bc.part("grip", bc.mesh_ellipsoid(0.038, 0.040, 0.07), P(tier, "grip"), parent,
                         loc=(0, -bulge * 0.2, 0)))
    if "gem" in extras:
        parts.append(bc.part("gem", mesh_gem(0.03, 0.025, 0.035), glow_or(tier, "accent"), parent,
                             loc=(0, -bulge * 0.55, 0)))
    return parts


def build_staff(tier, parent):
    parts = []
    # Stood up straighter than the hand hangs, and held out and forward, so the
    # head clears the shoulder instead of disappearing behind the arm.
    frame = bc.empty("staff_frame", (-0.10, -0.08, 0.0), parent)
    frame.rotation_euler = Euler((0, math.radians(-34), 0), "XYZ")
    parts.append(frame)
    parent = frame
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    shaft = {"wood": P(tier, "main"), "platinum": P(tier, "main"), "demonrite": P(tier, "main"),
             "diamond": P(tier, "dark")}.get(tier, "wood_dark")
    top, bottom = 0.60, -0.34
    add("shaft", bc.mesh_capsule(0.024, 0.022, top - bottom), shaft, parent, loc=(0, 0, top))
    head = (0, 0, top + 0.04)

    if tier == "wood":
        add("knob", bc.mesh_ellipsoid(0.055, 0.05, 0.06), P(tier, "dark"), parent, loc=head)
        parts.append(bc.spike("twig", (0, 0, top - 0.1), (0.1, 0, top + 0.02), 0.02, P(tier, "main"), parent))
    elif tier == "bronze":
        add("cap", bc.mesh_ellipsoid(0.05, 0.05, 0.07), P(tier, "main"), parent, loc=head)
        add("ring", bc.mesh_torus(0.036, 0.014), P(tier, "light"), parent, loc=(0, 0, top - 0.08))
    elif tier == "iron":
        add("ring", bc.mesh_torus(0.075, 0.016), P(tier, "main"), parent, loc=(0, 0, top + 0.08),
            rot=(math.radians(90), 0, 0))
        parts.append(bc.spike("point", (0, 0, top), (0, 0, top + 0.2), 0.03, P(tier, "light"), parent))
    elif tier == "steel":
        add("orb", bc.mesh_ellipsoid(0.045, 0.045, 0.045), P(tier, "accent"), parent, loc=(0, 0, top + 0.08))
        for a in (0, 90):
            add("cage", bc.mesh_torus(0.07, 0.012), P(tier, "main"), parent, loc=(0, 0, top + 0.08),
                rot=(math.radians(90), 0, math.radians(a)))
    elif tier == "azuryte":
        for side in (-1, 1):
            parts.append(bc.spike("prong", (0, 0, top), (side * 0.07, 0, top + 0.12), 0.022,
                                  P(tier, "main"), parent))
        add("crystal", mesh_gem(0.05, 0.045, 0.11), P(tier, "glow"), parent, loc=(0, 0, top + 0.14))
    elif tier == "adamantium":
        for dx in (-0.08, 0.0, 0.08):
            parts.append(bc.spike("prong", (0, 0, top - 0.02), (dx, 0, top + 0.2), 0.026,
                                  P(tier, "main"), parent))
        add("orb", bc.mesh_ellipsoid(0.04, 0.04, 0.04), P(tier, "light"), parent, loc=(0, 0, top + 0.06))
    elif tier == "diamond":
        for side in (-1, 1):
            parts.append(bc.spike("prong", (0, 0, top - 0.02), (side * 0.09, 0, top + 0.08), 0.02,
                                  P(tier, "dark"), parent))
        add("diamond", mesh_gem(0.08, 0.07, 0.13, sides=4), P(tier, "main"), parent, loc=(0, 0, top + 0.16))
        add("spark", mesh_gem(0.025, 0.02, 0.04, sides=4), P(tier, "glow"), parent, loc=(0, -0.05, top + 0.18))
    elif tier == "platinum":
        add("halo", bc.mesh_torus(0.10, 0.016), P(tier, "accent"), parent, loc=(0, 0, top + 0.14),
            rot=(math.radians(90), 0, 0))
        add("orb", bc.mesh_ellipsoid(0.05, 0.05, 0.05), P(tier, "glow"), parent, loc=(0, 0, top + 0.14))
        add("collar", bc.mesh_ellipsoid(0.04, 0.04, 0.03), P(tier, "accent"), parent, loc=(0, 0, top))
    elif tier == "demonrite":
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.02, 0, top - 0.02), (side * 0.12, 0, top + 0.12), 0.034,
                                  P(tier, "light"), parent))
            parts.append(bc.spike("hook", (side * 0.12, 0, top + 0.12), (side * 0.06, 0, top + 0.24), 0.02,
                                  P(tier, "light"), parent))
        add("orb", bc.mesh_ellipsoid(0.05, 0.05, 0.05), P(tier, "glow"), parent, loc=(0, 0, top + 0.1))

    # Metal-shod foot on every metal staff.
    if tier != "wood":
        add("ferrule", bc.mesh_ellipsoid(0.03, 0.03, 0.04), P(tier, "dark"), parent, loc=(0, 0, bottom))
    return parts


# The wood tier's staff shaft for the metal tiers is dark wood.
bc.PALETTE["wood_dark"] = PALETTES["wood"]["dark"]

WEAPONS = {"sword": build_sword, "bow": build_bow, "staff": build_staff}


# --- armour, ore and bars (icons only) ----------------------------------------------------

def build_shield(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    i = TIERS.index(tier)
    face = P(tier, "main")
    if i <= 1:
        add("face", bc.mesh_ellipsoid(0.30, 0.05, 0.30), face, parent, rot=(math.radians(90), 0, 0))
        add("rim", bc.mesh_torus(0.30, 0.03), P(tier, "dark"), parent, rot=(math.radians(90), 0, 0))
        if tier == "wood":
            for dx in (-0.1, 0.1):
                add("plank", mesh_box(0.012, 0.03, 0.56), P(tier, "dark"), parent, loc=(dx, -0.04, 0))
        add("boss", bc.mesh_ellipsoid(0.08, 0.06, 0.08), P(tier, "light"), parent, loc=(0, -0.06, 0))
    else:
        # A heater shield: wider at the top, coming to a point.
        add("face", bc.mesh_capsule(0.30, 0.05, 0.36, squash_y=0.2), face, parent, loc=(0, 0, 0.18))
        add("band", bc.mesh_capsule(0.06, 0.02, 0.36, squash_y=0.35), P(tier, "light"), parent,
            loc=(0, -0.05, 0.18))
        add("bar", bc.mesh_ellipsoid(0.26, 0.05, 0.05), P(tier, "light"), parent, loc=(0, -0.05, 0.12))
        if PALETTES[tier]["glow"]:
            add("gem", mesh_gem(0.06, 0.05, 0.07), P(tier, "glow"), parent, loc=(0, -0.09, 0.12))
        if tier == "platinum":
            add("rim", bc.mesh_ellipsoid(0.32, 0.03, 0.05), P(tier, "accent"), parent, loc=(0, -0.02, 0.36))
        if tier == "demonrite":
            for side in (-1, 1):
                parts.append(bc.spike("spike", (side * 0.26, 0, 0.3), (side * 0.40, 0, 0.44), 0.05,
                                      P(tier, "light"), parent))
        if tier == "adamantium":
            add("rivets", bc.mesh_ellipsoid(0.30, 0.04, 0.03), P(tier, "dark"), parent, loc=(0, -0.03, 0.30))
    return parts


def build_helm(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    i = TIERS.index(tier)
    add("dome", bc.mesh_ellipsoid(0.26, 0.26, 0.24), P(tier, "main"), parent, loc=(0, 0, 0.04))
    add("brim", bc.mesh_torus(0.25, 0.035), P(tier, "dark"), parent, loc=(0, 0, -0.06))
    if i >= 2:
        add("visor", mesh_box(0.30, 0.06, 0.05), P(tier, "dark"), parent, loc=(0, -0.24, -0.02))
    if tier in ("steel", "platinum"):
        add("crest", bc.mesh_ellipsoid(0.04, 0.22, 0.09), P(tier, "accent"), parent, loc=(0, 0.02, 0.28))
    if tier == "azuryte":
        add("gem", mesh_gem(0.05, 0.03, 0.06), P(tier, "glow"), parent, loc=(0, -0.26, 0.12))
    if tier == "adamantium":
        add("ridge", bc.mesh_ellipsoid(0.05, 0.26, 0.05), P(tier, "light"), parent, loc=(0, 0, 0.26))
    if tier == "diamond":
        for dx in (-0.12, 0, 0.12):
            add("crystal", mesh_gem(0.04, 0.04, 0.10, sides=4), P(tier, "light"), parent, loc=(dx, 0, 0.30))
    if tier == "platinum":
        for side in (-1, 1):
            parts.append(bc.spike("wing", (side * 0.22, 0, 0.1), (side * 0.40, 0, 0.30), 0.04,
                                  P(tier, "light"), parent))
    if tier == "demonrite":
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.2, 0, 0.14), (side * 0.36, 0, 0.40), 0.06,
                                  P(tier, "light"), parent))
        add("eyes", mesh_box(0.18, 0.02, 0.02), P(tier, "glow"), parent, loc=(0, -0.28, -0.02))
    if tier == "wood":
        add("band", bc.mesh_torus(0.24, 0.02), "leather", parent, loc=(0, 0, 0.08))
    return parts


def build_body(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("torso", bc.mesh_capsule(0.26, 0.20, 0.26, squash_y=0.7), P(tier, "main"), parent, loc=(0, 0, 0.16))
    add("plate", bc.mesh_ellipsoid(0.16, 0.08, 0.17), P(tier, "light"), parent, loc=(0, -0.14, 0.08))
    add("belt", bc.mesh_ellipsoid(0.24, 0.17, 0.04), P(tier, "dark"), parent, loc=(0, 0, -0.18))
    for side in (-1, 1):
        add("pauldron", bc.mesh_ellipsoid(0.13, 0.13, 0.09), P(tier, "dark" if tier != "platinum" else "accent"),
            parent, loc=(side * 0.27, 0, 0.18))
        if tier == "demonrite":
            parts.append(bc.spike("spike", (side * 0.3, 0, 0.22), (side * 0.44, 0, 0.40), 0.05,
                                  P(tier, "light"), parent))
        if tier == "diamond":
            add("crystal", mesh_gem(0.05, 0.05, 0.10, sides=4), P(tier, "light"), parent,
                loc=(side * 0.27, 0, 0.30))
    if PALETTES[tier]["glow"]:
        add("core", mesh_gem(0.05, 0.03, 0.06), P(tier, "glow"), parent, loc=(0, -0.22, 0.10))
    if tier == "steel":
        add("trim", bc.mesh_ellipsoid(0.03, 0.03, 0.14), P(tier, "accent"), parent, loc=(0, -0.21, 0.08))
    return parts


def build_legs(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("waist", bc.mesh_ellipsoid(0.24, 0.14, 0.06), P(tier, "dark"), parent, loc=(0, 0, 0.30))
    for side in (-1, 1):
        add("thigh", bc.mesh_capsule(0.10, 0.085, 0.20), P(tier, "main"), parent, loc=(side * 0.11, 0, 0.26))
        add("knee", bc.mesh_ellipsoid(0.07, 0.07, 0.06), P(tier, "light"), parent, loc=(side * 0.11, -0.05, -0.02))
        add("shin", bc.mesh_capsule(0.085, 0.075, 0.22), P(tier, "main"), parent, loc=(side * 0.11, 0, -0.06))
        if tier == "demonrite":
            parts.append(bc.spike("spike", (side * 0.11, -0.05, -0.02), (side * 0.2, -0.12, 0.06), 0.04,
                                  P(tier, "light"), parent))
        if PALETTES[tier]["glow"] and tier != "demonrite":
            add("gem", mesh_gem(0.03, 0.02, 0.04), P(tier, "glow"), parent, loc=(side * 0.11, -0.08, -0.02))
    return parts


def build_ore(tier, parent):
    """A lump of rock with the tier showing through it: nuggets, veins or
    crystals. Wood has no ore; bronze is worked from copper ore."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    rock = "rock_dark" if tier in ("demonrite", "adamantium") else "rock"
    add("rock", bc.mesh_ellipsoid(0.30, 0.24, 0.20), rock, parent)
    add("rock2", bc.mesh_ellipsoid(0.16, 0.16, 0.14), rock, parent, loc=(0.2, 0.02, 0.1))
    spots = [(-0.12, -0.2, 0.06), (0.06, -0.22, 0.0), (0.18, -0.12, 0.16), (-0.05, -0.14, 0.16)]
    if tier == "steel":            # coal: black lumps rather than metal in rock
        for i, s in enumerate(spots):
            add("coal", mesh_gem(0.10, 0.09, 0.08, sides=5), P("demonrite", "main"), parent, loc=s)
        return parts
    crystal = tier in ("azuryte", "diamond", "demonrite")
    for i, s in enumerate(spots):
        colour = P(tier, "light") if i % 2 else P(tier, "main")
        if crystal:
            colour = P(tier, "glow") if (i == 1 and PALETTES[tier]["glow"]) else colour
            add("crystal", mesh_gem(0.05, 0.05, 0.11, sides=5), colour, parent, loc=s,
                rot=(math.radians(-20 + i * 14), math.radians(i * 20 - 30), 0))
        else:
            add("nugget", bc.mesh_ellipsoid(0.065, 0.055, 0.05), colour, parent, loc=s)
    return parts


def build_bar(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    if tier == "diamond":
        # Lying along X: the crystal's poles are on its Z axis, so turn it over.
        add("bar", mesh_gem(0.12, 0.10, 0.30, sides=4), P(tier, "main"), parent, rot=(0, math.radians(90), 0))
        add("spark", mesh_gem(0.05, 0.03, 0.05, sides=4), P(tier, "glow"), parent, loc=(0.06, -0.12, 0.04))
        return parts
    add("bar", mesh_box(0.54, 0.24, 0.14, taper=0.78), P(tier, "main"), parent)
    add("top", mesh_box(0.40, 0.15, 0.02), P(tier, "light"), parent, loc=(0, 0, 0.075))
    if PALETTES[tier]["glow"]:
        add("vein", mesh_box(0.44, 0.02, 0.015), P(tier, "glow"), parent, loc=(0, -0.125, 0.0))
    return parts


# --- icons ------------------------------------------------------------------------------

def icon_camera(ortho):
    cam_data = bpy.data.cameras.new("icon_cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = ortho
    cam = bpy.data.objects.new("icon_cam", cam_data)
    bc.link(cam)
    elev = math.radians(24.0)
    cam.location = Vector((0.0, -6.0 * math.cos(elev), 6.0 * math.sin(elev)))
    cam.rotation_euler = (math.radians(90.0 - 24.0), 0.0, 0.0)
    cam_data.clip_end = 40.0
    bpy.context.scene.camera = cam


def render_icon(name, builder, tier, tilt, spin, fill=0.92):
    bc.clear_scene()
    bc._materials.clear()
    bc._meshes.clear()
    bc.setup_world()
    scene = bpy.context.scene
    bc.setup_render(1, 1)
    scene.render.resolution_x = ICON_PX * bc.SUPERSAMPLE
    scene.render.resolution_y = ICON_PX * bc.SUPERSAMPLE

    # Spin about the model's own long axis first, then lean it in the picture.
    holder = bc.empty("holder", (0, 0, 0))
    holder.rotation_mode = "QUATERNION"
    holder.rotation_quaternion = (Matrix.Rotation(math.radians(tilt), 4, "Y") @
                                  Matrix.Rotation(math.radians(spin), 4, "Z")).to_quaternion()
    parts = builder(tier, holder)
    bpy.context.view_layer.update()

    # Frame the model: centre its silhouette as the camera sees it, and size
    # the view so the longer side fills the icon. Every icon then uses its
    # square the same way, whether it is a sword or a lump of ore.
    elev = math.radians(24.0)
    up = Vector((0.0, math.sin(elev), math.cos(elev)))
    xs, us = [], []
    for ob in parts:
        if ob.type != "MESH":
            continue
        for corner in ob.bound_box:
            w = ob.matrix_world @ Vector(corner)
            xs.append(w.x)
            us.append(w.dot(up))
    cx, cu = (min(xs) + max(xs)) / 2.0, (min(us) + max(us)) / 2.0
    extent = max(max(xs) - min(xs), max(us) - min(us))
    holder.location = Vector((-cx, 0.0, 0.0)) - up * cu
    icon_camera(extent / fill)

    raw = os.path.join(RENDER_DIR, name + ".png")
    bc.render_to(raw)
    small = bc.outline(bc.reduce_majority(bc.read_png(raw), coverage_needed=7))
    bc.write_png(os.path.join(ICON_DIR, name + ".png"), small)


ORES = {"bronze": "copper_ore", "iron": "iron_ore", "steel": "coal", "azuryte": "azuryte_ore",
        "adamantium": "adamantium_ore", "diamond": "diamond_ore", "platinum": "platinum_ore",
        "demonrite": "demonrite_ore"}
BARS = {"bronze": "bronze_bar", "iron": "iron_bar", "steel": "steel_bar", "azuryte": "azuryte_bar",
        "adamantium": "adamantium_bar", "diamond": "diamond_ingot", "platinum": "platinum_bar",
        "demonrite": "demonrite_bar"}


def all_icons(only_tiers):
    count = 0
    for tier in only_tiers:
        #                  file                builder       tilt  spin  fill
        jobs = [("sword_" + tier,  build_sword,  -135, 0,   1.0),
                ("staff_" + tier,  build_staff,  45,   0,   1.0),
                ("bow_" + tier,    build_bow,    30,   0,   0.96),
                ("shield_" + tier, build_shield, 0,    12,  0.9),
                ("helm_" + tier,   build_helm,   0,    20,  0.88),
                ("body_" + tier,   build_body,   0,    14,  0.9),
                ("legs_" + tier,   build_legs,   0,    14,  0.88)]
        if tier in ORES:
            jobs.append((ORES[tier], build_ore, 0, 18, 0.9))
            jobs.append((BARS[tier], build_bar, 0, 28, 0.9))
        for name, builder, tilt, spin, fill in jobs:
            render_icon(name, builder, tier, tilt, spin, fill)
            count += 1
    print("icons %d" % count)


# --- weapon layers on the hero --------------------------------------------------------------

def weapon_layers(clip_name, models, out_dir):
    pose_fn, frames, loops = bc.CLIPS[clip_name]
    cols, rows = frames, len(bc.FACINGS)

    bc.clear_scene()
    bc._materials.clear()
    bc._meshes.clear()
    bc.setup_world()
    right, up = bc.camera_basis()

    occluders, own_sword, grips = [], [], []
    for row, (facing, turn) in enumerate(bc.FACINGS):
        for col in range(frames):
            t = col / float(frames) if loops else col / float(frames - 1)
            joints, groups, extras = bc.build_character()
            values = pose_fn(t)
            if facing in ("down", "up"):
                for k in ("lean", "hips_lean", "lunge"):
                    if k in values and clip_name in ("run", "sprint"):
                        values[k] *= 0.4
            bc.apply_pose(joints, extras, values)
            offset = right * (bc.FRAME_SPAN * col) - up * (bc.FRAME_SPAN * row)
            joints["root"].rotation_euler.z = math.radians(turn)
            joints["root"].location = offset
            occluders += groups[bc.BODY] + groups[bc.HEAD]
            own_sword += groups[bc.WEAPON]
            grips.append(joints["grip"])

    for ob in occluders:
        ob.hide_render = False
        ob.is_holdout = True
    for ob in own_sword:
        ob.hide_render = True

    bc.setup_camera(cols, rows)
    bc.setup_render(cols, rows)

    for model in models:
        kind, tier = model.split("_", 1)
        made = []
        for grip in grips:
            made += WEAPONS[kind](tier, grip)
        raw = os.path.join(RENDER_DIR, "layer_%s_%s.png" % (clip_name, model))
        bc.render_to(raw)
        small = bc.outline(bc.reduce_majority(bc.read_png(raw)))
        bc.write_png(os.path.join(out_dir, "layers", "%s_4_weapon_%s.png" % (clip_name, model)), small)
        for ob in made:
            bpy.data.objects.remove(ob, do_unlink=True)
    print("layers %-7s %d models" % (clip_name, len(models)))


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    def option(flag):
        if flag in args:
            i = args.index(flag)
            value = args[i + 1].split(",")
            del args[i:i + 2]
            return value
        return None
    clips = option("--only") or list(bc.CLIPS)
    tiers = option("--tiers") or TIERS
    models = option("--models") or ["%s_%s" % (k, t) for t in tiers for k in ("sword", "bow", "staff")]
    wanted = set(args) or {"icons", "layers"}

    os.makedirs(ICON_DIR, exist_ok=True)
    if "icons" in wanted:
        all_icons(tiers)
    if "layers" in wanted:
        for clip in clips:
            weapon_layers(clip, models, bc.OUT_DIR)


if __name__ == "__main__":
    main()
