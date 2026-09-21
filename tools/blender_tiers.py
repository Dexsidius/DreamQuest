# =============================================================================
#  blender_tiers.py - the art for the nine material tiers.
#
#  Run headless (tools/make_tiers.ps1 does this):
#      blender --background --python tools/blender_tiers.py -- [icons] [layers]
#          [--only CLIP,CLIP] [--models sword_iron,bow_wood]
#
#  Wood, Bronze, Iron, Steel, Azuryte, Damascus, Diamond, Platinum, Demonite.
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
#  winged guard, a demonite one jagged), and the top tiers carry something
#  that glows -- azuryte's cyan edge, diamond's white facets, demonite's red.
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

TIERS = ["wood", "bronze", "iron", "steel", "azuryte", "damascus", "orichalcum",
         "diamond", "platinum", "demonite", "dracon", "enchanted"]

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
    "damascus":   dict(main=(0.42, 0.44, 0.50), light=(0.74, 0.78, 0.84), dark=(0.19, 0.20, 0.25),
                       grip=(0.24, 0.18, 0.14), accent=(0.58, 0.62, 0.70), glow=None),
    "diamond":    dict(main=(0.74, 0.93, 1.00), light=(1.00, 1.00, 1.00), dark=(0.46, 0.74, 0.88),
                       grip=(0.30, 0.36, 0.48), accent=(0.60, 0.86, 0.98), glow=(0.86, 1.00, 1.00)),
    "platinum":   dict(main=(0.93, 0.91, 0.85), light=(1.00, 1.00, 0.96), dark=(0.66, 0.63, 0.56),
                       grip=(0.48, 0.16, 0.19), accent=(0.96, 0.78, 0.32), glow=(1.00, 0.94, 0.68)),
    "demonite":  dict(main=(0.30, 0.15, 0.18), light=(0.56, 0.26, 0.28), dark=(0.14, 0.07, 0.09),
                       grip=(0.12, 0.07, 0.08), accent=(0.42, 0.14, 0.16), glow=(1.00, 0.26, 0.12)),
    "orichalcum": dict(main=(0.84, 0.62, 0.26), light=(0.98, 0.84, 0.48), dark=(0.50, 0.33, 0.12),
                       grip=(0.26, 0.17, 0.10), accent=(0.72, 0.36, 0.20), glow=None),
    "dracon":     dict(main=(0.78, 0.38, 0.12), light=(0.98, 0.64, 0.26), dark=(0.40, 0.17, 0.06),
                       grip=(0.22, 0.14, 0.10), accent=(0.96, 0.84, 0.44), glow=(1.00, 0.62, 0.18)),
    "enchanted":  dict(main=(0.56, 0.40, 0.86), light=(0.80, 0.68, 1.00), dark=(0.28, 0.18, 0.48),
                       grip=(0.22, 0.16, 0.34), accent=(0.92, 0.88, 1.00), glow=(0.78, 0.56, 1.00)),
}

for _tier, _pal in PALETTES.items():
    for _key, _rgb in _pal.items():
        if _rgb is not None:
            bc.PALETTE["%s_%s" % (_tier, _key)] = _rgb
bc.PALETTE["string"] = (0.92, 0.88, 0.78)
bc.PALETTE["rock"] = (0.46, 0.43, 0.42)
bc.PALETTE["rock_dark"] = (0.30, 0.28, 0.29)
bc.PALETTE["leather"] = (0.46, 0.31, 0.20)
bc.PALETTE["orb_glass"] = (0.56, 0.44, 0.96)
bc.PALETTE["orb_core"] = (0.90, 0.84, 1.00)

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
    "damascus": (0.52, 0.085, 0.15, "heavy"),
    "diamond":    (0.60, 0.058, 0.10, "crystal edge"),
    "platinum":   (0.66, 0.060, 0.12, "wings gem pommel"),
    "demonite":  (0.64, 0.066, 0.13, "spikes horns edge gem"),
    "orichalcum": (0.60, 0.072, 0.15, "heavy leaf pommel"),
    "dracon":     (0.70, 0.070, 0.14, "spikes edge wings gem"),
    "enchanted":  (0.72, 0.050, 0.12, "edge crystal gem pommel wings"),
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
    "damascus": (0.42, 0.09, 0.040, "bands tips"),
    "diamond":    (0.46, 0.12, 0.030, "crystal"),
    "platinum":   (0.48, 0.12, 0.030, "recurve wings gem"),
    "demonite":  (0.48, 0.13, 0.034, "recurve spikes glowstring"),
    "orichalcum": (0.46, 0.11, 0.036, "recurve bands tips gem"),
    "dracon":     (0.50, 0.14, 0.034, "recurve spikes wings glowstring"),
    "enchanted":  (0.52, 0.12, 0.028, "recurve crystal glowstring gem"),
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
    shaft = {"wood": P(tier, "main"), "platinum": P(tier, "main"), "demonite": P(tier, "main"),
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
    elif tier == "damascus":
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
    elif tier == "demonite":
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


SPEARS = {
    #             head   width  extras
    "wood":       (0.16, 0.030, "hardened"),
    "bronze":     (0.20, 0.052, "leaf"),
    "iron":       (0.21, 0.036, ""),
    "steel":      (0.25, 0.040, "lugs"),
    "azuryte":    (0.25, 0.044, "edge gem"),
    "damascus": (0.23, 0.064, "heavy lugs"),
    "diamond":    (0.26, 0.050, "crystal"),
    "platinum":   (0.28, 0.050, "wings gem collar"),
    "demonite":  (0.28, 0.054, "barbs horns edge"),
    "orichalcum": (0.27, 0.058, "heavy lugs collar"),
    "dracon":     (0.30, 0.058, "barbs wings edge"),
    "enchanted":  (0.31, 0.046, "crystal edge gem collar"),
}
# Which way a spear is carried: upright beside the shoulder like a staff, or,
# during a thrust, levelled along the arm with the head out in front.
SPEAR_MODE = {"thrust": False}


def build_spear(tier, parent):
    head_len, width, extras = SPEARS[tier]
    parts = []
    icon = ICON_MODE["on"]
    frame = bc.empty("spear_frame", (0, 0, 0), parent)
    if SPEAR_MODE["thrust"] and not icon:
        # The grip's -Z is the way a blade points; turn the spear's +Z onto it,
        # and take off the grip's outward cant, or the shaft points across the
        # body rather than down the facing.
        frame.rotation_euler = Euler((math.radians(180), math.radians(-18), 0), "XYZ")
    elif not icon:
        # Carried like the staff: stood up, out and forward of the sleeve.
        frame.location = Vector((-0.10, -0.08, 0.0))
        frame.rotation_euler = Euler((0, math.radians(-30), 0), "XYZ")
    parts.append(frame)
    parent = frame
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    # An icon has to show the head in 32 pixels, so its shaft is short.
    top, bottom = (0.34, -0.26) if icon else (0.74, -0.42)
    if icon:
        head_len, width = head_len * 1.5, width * 1.5
    shaft = P(tier, "main") if tier == "wood" else "wood_dark"
    add("shaft", bc.mesh_capsule(0.019, 0.019, top - bottom), shaft, parent, loc=(0, 0, top))
    z0 = top
    if tier != "wood":
        add("socket", bc.mesh_ellipsoid(0.028, 0.028, 0.04), P(tier, "dark"), parent, loc=(0, 0, top))
        z0 = top + 0.02

    if "hardened" in extras:
        # A point cut on the shaft itself and blackened in the fire.
        parts.append(bc.spike("point", (0, 0, z0 - 0.02), (0, 0, z0 + head_len), 0.026, P(tier, "dark"), parent))
        add("binding", bc.mesh_ellipsoid(0.026, 0.026, 0.018), "string", parent, loc=(0, 0, z0 - 0.06))
    elif "crystal" in extras:
        add("head", mesh_gem(width, width * 0.5, head_len * 0.5, sides=4), P(tier, "main"), parent,
            loc=(0, 0, z0 + head_len * 0.5))
    else:
        broad = 1.25 if ("leaf" in extras or "heavy" in extras) else 0.85
        add("head", bc.mesh_ellipsoid(width * broad, width * 0.28, head_len * 0.42), P(tier, "main"), parent,
            loc=(0, 0, z0 + head_len * 0.40))
        parts.append(bc.spike("point", (0, 0, z0 + head_len * 0.5), (0, 0, z0 + head_len), width * 0.55,
                              P(tier, "light"), parent))
    if "edge" in extras:
        add("edge", bc.mesh_ellipsoid(width * 0.35, width * 0.32, head_len * 0.36), glow_or(tier, "light"),
            parent, loc=(0, 0, z0 + head_len * 0.42))
    if "gem" in extras:
        add("gem", mesh_gem(0.022, 0.018, 0.026), glow_or(tier, "accent"), parent, loc=(0, -0.025, z0 + 0.01))
    if "lugs" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("lug", (0, 0, z0 - 0.01), (side * 0.07, 0, z0 - 0.03), 0.016,
                                  P(tier, "dark"), parent))
    if "wings" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("wing", (side * 0.02, 0, z0), (side * 0.11, 0, z0 - 0.07), 0.022,
                                  P(tier, "accent"), parent))
    if "collar" in extras:
        add("collar", bc.mesh_ellipsoid(0.03, 0.03, 0.02), P(tier, "accent"), parent, loc=(0, 0, z0 - 0.08))
    if "barbs" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("barb", (side * width * 0.6, 0, z0 + head_len * 0.2),
                                  (side * width * 1.8, 0, z0 - 0.03), 0.02, P(tier, "light"), parent))
    if "horns" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.02, 0, z0 - 0.04), (side * 0.10, 0, z0 + 0.06), 0.022,
                                  P(tier, "light"), parent))
    # A metal-shod butt.
    if tier != "wood" and not icon:
        add("butt", bc.mesh_ellipsoid(0.026, 0.026, 0.035), P(tier, "dark"), parent, loc=(0, 0, bottom))
    return parts


# The wood tier's staff shaft for the metal tiers is dark wood.
bc.PALETTE["wood_dark"] = PALETTES["wood"]["dark"]

# --- tools ---------------------------------------------------------------------------------
# Built in the grip's frame like the sword: the fist at the origin, the haft
# running down -Z past it to the head. The heads grow a little with each tier
# and pick up the tier's extras, so a diamond pick carries a crystal and a
# demonite axe is barbed.

# In an icon the whole tool has to fit a 32-pixel square, and at its in-hand
# proportions that is a long stick with a speck on the end: an axe and a pick
# looked the same. So icons draw a shorter haft and a much bigger head.
ICON_MODE = {"on": False}


def _tool_proportions(tier):
    i = TIERS.index(tier)
    size = 1.0 + i * 0.045
    haft = 0.50
    if ICON_MODE["on"]:
        size *= 1.35
        haft = 0.40
    return size, haft


def _tool_extras(tier, parent, at, size):
    parts = []
    if tier in ("platinum", "steel"):
        parts.append(bc.part("band", bc.mesh_torus(0.028 * size, 0.009), P(tier, "accent"), parent,
                             loc=(0, 0, at + 0.07 * size)))
    if tier == "demonite":
        for side in (-1, 1):
            parts.append(bc.spike("barb", (0, 0, at), (side * 0.06 * size, 0, at + 0.10 * size), 0.018,
                                  P(tier, "light"), parent))
    return parts


def build_axe(tier, parent):
    size, haft_len = _tool_proportions(tier)
    parts = []
    haft = P("wood", "main") if tier in ("wood", "bronze", "iron") else P(tier, "grip")
    parts.append(bc.part("haft", bc.mesh_capsule(0.02, 0.019, haft_len), haft, parent, loc=(0, 0, 0.07)))
    head_z = 0.07 - haft_len + 0.07
    if tier == "diamond":
        parts.append(bc.part("head", mesh_gem(0.03, 0.13 * size, 0.09 * size, sides=4), P(tier, "main"), parent,
                             loc=(0, -0.07 * size, head_z)))
    else:
        # A wedge: thick at the haft, fanning out to the edge in front.
        parts.append(bc.part("cheek", bc.mesh_ellipsoid(0.03 * size, 0.06 * size, 0.05 * size), P(tier, "dark"),
                             parent, loc=(0, 0, head_z)))
        parts.append(bc.part("blade", bc.mesh_ellipsoid(0.022 * size, 0.09 * size, 0.10 * size), P(tier, "main"),
                             parent, loc=(0, -0.08 * size, head_z)))
    parts.append(bc.part("edge", bc.mesh_ellipsoid(0.026 * size, 0.022 * size, 0.10 * size), glow_or(tier, "light"),
                         parent, loc=(0, -0.16 * size, head_z)))
    return parts + _tool_extras(tier, parent, head_z, size)


def build_pickaxe(tier, parent):
    size, haft_len = _tool_proportions(tier)
    haft_len += 0.02
    parts = []
    haft = P("wood", "main") if tier in ("wood", "bronze", "iron") else P(tier, "grip")
    parts.append(bc.part("haft", bc.mesh_capsule(0.02, 0.019, haft_len), haft, parent, loc=(0, 0, 0.07)))
    head_z = 0.07 - haft_len + 0.05
    parts.append(bc.part("eye", bc.mesh_ellipsoid(0.035 * size, 0.04 * size, 0.035 * size), P(tier, "dark"), parent,
                         loc=(0, 0, head_z)))
    # Two points, curving back toward the hand.
    for side in (-1, 1):
        mid = (0, side * 0.10 * size, head_z + 0.02)
        tip = (0, side * 0.19 * size, head_z + 0.09 * size)
        colour = glow_or(tier, "light") if side < 0 else P(tier, "main")
        parts.append(bc.spike("arm", (0, 0, head_z), mid, 0.034 * size, P(tier, "main"), parent, r_tip=0.028 * size))
        parts.append(bc.spike("point", mid, tip, 0.028 * size, colour, parent))
    if tier == "diamond":
        parts.append(bc.part("crystal", mesh_gem(0.04, 0.03, 0.06, sides=4), P(tier, "glow"), parent,
                             loc=(0, 0, head_z - 0.03)))
    return parts + _tool_extras(tier, parent, head_z, size)


def build_rod(tier, parent):
    # One rod, not a tier: willow, a leather grip, a reel, and a line with a
    # red float hanging from the tip.
    parts = []
    parts.append(bc.part("grip", bc.mesh_capsule(0.028, 0.024, 0.12), "leather", parent, loc=(0, 0, 0.08)))
    parts.append(bc.part("reel", bc.mesh_torus(0.03, 0.012), P("steel", "dark"), parent, loc=(0, 0.03, -0.02),
                         rot=(0, math.radians(90), 0)))
    parts.append(bc.part("rod", bc.mesh_capsule(0.018, 0.006, 0.95), P("wood", "light"), parent, loc=(0, 0, -0.04)))
    tip = (0, 0, -1.0)
    parts.append(bc.spike("line", tip, (0, -0.20, -1.02), 0.005, "string", parent, r_tip=0.005))
    parts.append(bc.part("float", bc.mesh_ellipsoid(0.03, 0.03, 0.035), "float_red", parent, loc=(0, -0.21, -1.02)))
    return parts


bc.PALETTE["float_red"] = (0.90, 0.22, 0.20)

# --- fish (icons) --------------------------------------------------------------------------------
FISH = {
    #          body                belly               marks               length  depth
    "minnow": ((0.70, 0.74, 0.78), (0.92, 0.94, 0.96), None,               0.24, 0.07),
    "trout":  ((0.52, 0.56, 0.36), (0.90, 0.80, 0.62), (0.82, 0.30, 0.26), 0.32, 0.10),
    "pike":   ((0.30, 0.48, 0.30), (0.84, 0.86, 0.62), (0.72, 0.80, 0.50), 0.40, 0.09),
    "salmon": ((0.66, 0.70, 0.78), (0.96, 0.62, 0.56), (0.40, 0.44, 0.56), 0.38, 0.12),
    "eel":    ((0.34, 0.28, 0.18), (0.60, 0.52, 0.34), None,               0.46, 0.085),
}
for _name, (_body, _belly, _marks, _l, _d) in FISH.items():
    bc.PALETTE["fish_%s_body" % _name] = _body
    bc.PALETTE["fish_%s_belly" % _name] = _belly
    bc.PALETTE["fish_%s_marks" % _name] = _marks or _body
bc.PALETTE["cooked_body"] = (0.74, 0.46, 0.22)
bc.PALETTE["cooked_belly"] = (0.90, 0.68, 0.38)
# Browned, but each still leaning toward its own colour, so a roast pike and a
# roast salmon can be told apart in the bag.
for _name, (_body, _belly, _marks, _l, _d) in FISH.items():
    bc.PALETTE["cooked_%s_body" % _name] = tuple(0.5 * a + 0.5 * b for a, b in zip(_body, (0.74, 0.46, 0.22)))
    bc.PALETTE["cooked_%s_belly" % _name] = tuple(0.45 * a + 0.55 * b for a, b in zip(_belly, (0.92, 0.70, 0.40)))
bc.PALETTE["cooked_char"] = (0.34, 0.18, 0.10)


def build_fish(name, parent, cooked=False):
    body_c, belly_c, marks_c, length, depth = FISH[name]
    body = ("cooked_%s_body" if cooked else "fish_%s_body") % name
    belly = ("cooked_%s_belly" if cooked else "fish_%s_belly") % name
    parts = []
    if name == "eel":
        # Long and thin, lying along X with a little wave in it.
        pts = [(-length, 0, 0.0), (-length * 0.4, 0, depth * 1.2), (length * 0.2, 0, -depth), (length, 0, depth * 0.6)]
        for i in range(3):
            parts.append(bc.spike("body", pts[i], pts[i + 1], depth * (1.0 - 0.2 * i), body, parent,
                                  r_tip=depth * (0.8 - 0.2 * i)))
        eye_at = (-length * 0.95, -depth * 0.8, depth * 0.3)
    else:
        parts.append(bc.part("body", bc.mesh_ellipsoid(length, depth * 0.55, depth), body, parent))
        parts.append(bc.part("belly", bc.mesh_ellipsoid(length * 0.8, depth * 0.5, depth * 0.55), belly, parent,
                             loc=(0, -0.01, -depth * 0.35)))
        for side in (-1, 1):
            parts.append(bc.spike("tail", (length * 0.85, 0, 0), (length * 1.35, 0, side * depth * 0.9),
                                  depth * 0.45, body, parent))
        parts.append(bc.spike("fin", (-length * 0.1, 0, depth * 0.8), (length * 0.25, 0, depth * 1.5),
                              depth * 0.35, body, parent))
        eye_at = (-length * 0.72, -depth * 0.5, depth * 0.25)
    if not cooked:
        parts.append(bc.part("eye", bc.mesh_ellipsoid(0.018, 0.02, 0.018), "eye", parent, loc=eye_at))
        if marks_c:
            for k in range(3):
                parts.append(bc.part("mark", bc.mesh_ellipsoid(0.02, 0.012, 0.02), "fish_%s_marks" % name, parent,
                                     loc=(-length * 0.3 + k * length * 0.3, -depth * 0.52, depth * 0.3)))
    else:
        for k in range(3):
            parts.append(bc.part("grill", mesh_box(0.018, 0.02, depth * 1.6), "cooked_char", parent,
                                 loc=(-length * 0.4 + k * length * 0.35, -depth * 0.6, 0)))
    return parts


# --- the armoury ----------------------------------------------------------------------
# Nine more things to hold, each cut from the same tier tables as the sword so a
# bronze mace is a bronze sword's cousin: the sword's length, width and extras
# say how big a tier's weapons are and what they are dressed in, and each of
# these is that, in its own proportions. All are built in the grip's frame,
# with the business end down -Z like a sword's blade -- so an arm raised
# forward points them forward, which is what a crossbow and a wand want too.

# How a great weapon is carried when it is not being swung: up over the
# shoulder, where a blade as long as the man is tall does not drag its point
# through the floor. Set by weapon_layers for the clips that swing it.
GREAT_MODE = {"swing": False}


def _dress(tier, parts, parent, at_z, reach):
    """What the high tiers hang on everything: a gem at the guard, wings, horns."""
    extras = SWORDS[tier][3]
    if "gem" in extras:
        parts.append(bc.part("gem", mesh_gem(0.028, 0.022, 0.032), glow_or(tier, "light"), parent, loc=(0, -0.035, at_z)))
    if "wings" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("wing", (side * 0.04, 0, at_z), (side * reach, 0, at_z + 0.09), 0.028,
                                  P(tier, "accent"), parent))
    if "horns" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.06, 0, at_z), (side * reach * 0.8, 0, at_z - 0.09), 0.03,
                                  P(tier, "light"), parent))


def build_dagger(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    # Two thirds of a sword's length and half as wide again. It was a cone a
    # third the length of a sword, and a cone that size is three pixels of blade
    # and then nothing: the thin half never survives being reduced, so what was
    # in the fist read as a nail. A broad leaf with a short point keeps its
    # width nearly to the tip, and that is what reads as a blade at this size.
    blade, width = 0.15 + blade * 0.36, width * 1.35
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("hilt", bc.mesh_capsule(0.022, 0.022, 0.095), P(tier, "grip"), parent, loc=(0, 0, 0.055))
    add("pommel", bc.mesh_ellipsoid(0.030, 0.030, 0.030), P(tier, "dark"), parent, loc=(0, 0, 0.08))
    add("guard", bc.mesh_ellipsoid(0.078, 0.034, 0.026), P(tier, "accent" if tier != "wood" else "dark"), parent,
        loc=(0, 0, -0.04))
    if "crystal" in extras:
        add("blade", mesh_gem(width, width * 0.5, blade * 0.62, sides=4), P(tier, "main"), parent,
            loc=(0, 0, -0.05 - blade * 0.5))
    else:
        body = blade * 0.66
        add("blade", bc.mesh_capsule(width, width * 0.8, body, squash_y=0.55), P(tier, "main"), parent, loc=(0, 0, -0.05))
        parts.append(bc.spike("point", (0, 0, -0.05 - body + 0.02), (0, 0, -0.05 - blade), width * 0.78, P(tier, "main"), parent,
                              r_tip=0.008))
        add("ridge", bc.mesh_capsule(width * 0.34, width * 0.22, blade * 0.8, squash_y=0.5),
            glow_or(tier, "light") if "edge" in extras else P(tier, "light"), parent, loc=(0, -0.006, -0.06))
    _dress(tier, parts, parent, -0.04, 0.11)
    return parts


def build_knives(tier, parent):
    """Throwing knives: three in the hand, fanned, all blade and no guard."""
    blade, width, guard, extras = SWORDS[tier]
    blade, width = blade * 0.36, width * 0.75
    parts = []
    for i, lean in enumerate((-0.10, 0.0, 0.10)):
        tip = (lean * 1.6, -0.01 * i, -0.04 - blade)
        parts.append(bc.spike("knife", (lean * 0.3, -0.01 * i, -0.03), tip, width, P(tier, "main"), parent, r_tip=0.006))
        parts.append(bc.spike("tang", (lean * 0.3, -0.01 * i, -0.03), (lean * -0.2, -0.01 * i, 0.07), 0.016,
                              P(tier, "grip"), parent, r_tip=0.014))
    if "edge" in extras or "gem" in extras:
        parts.append(bc.spike("edge", (0, -0.014, -0.05), (0, -0.014, -0.04 - blade * 0.85), width * 0.3,
                              glow_or(tier, "light"), parent, r_tip=0.004))
    return parts


def build_mace(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    haft = 0.32 + blade * 0.34
    head = 0.075 + width * 0.60
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("grip", bc.mesh_capsule(0.022, 0.022, 0.11), P(tier, "grip"), parent, loc=(0, 0, 0.06))
    add("haft", bc.mesh_capsule(0.020, 0.018, haft), P(tier, "dark") if tier != "wood" else P(tier, "main"), parent,
        loc=(0, 0, -0.03))
    at = -0.03 - haft
    add("head", bc.mesh_ellipsoid(head, head, head * 1.05), P(tier, "main"), parent, loc=(0, 0, at))
    add("collar", bc.mesh_ellipsoid(head * 0.55, head * 0.55, 0.03), P(tier, "light"), parent, loc=(0, 0, at + head * 0.9))
    # Flanges all round it: the thing that makes it a mace and not a ball on a stick.
    flange = glow_or(tier, "light") if "edge" in extras else P(tier, "light")
    for i in range(6):
        a = math.radians(60 * i)
        dx, dy = math.cos(a), math.sin(a)
        parts.append(bc.spike("flange", (dx * head * 0.7, dy * head * 0.7, at),
                              (dx * head * (1.75 if "spikes" in extras else 1.4), dy * head * (1.75 if "spikes" in extras else 1.4), at),
                              head * 0.42, flange, parent, r_tip=head * (0.05 if "spikes" in extras else 0.2)))
    parts.append(bc.spike("spike", (0, 0, at - head * 0.7), (0, 0, at - head * 1.7), head * 0.4, flange, parent, r_tip=0.006))
    _dress(tier, parts, parent, 0.0, 0.10)
    return parts


def _great_frame(parent):
    """Swung, a great weapon is in the hands like a sword. Carried, it is turned
    end for end and laid back over the shoulder."""
    if GREAT_MODE["swing"] or ICON_MODE["on"]:
        return parent, []
    frame = bc.empty("great_frame", (-0.02, 0.06, 0.04), parent)
    frame.rotation_euler = Euler((math.radians(164), math.radians(10), 0), "XYZ")
    return frame, [frame]


def build_greatsword(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    blade, width, guard = 0.52 + blade * 0.72, width * 1.55, max(0.13, guard * 1.5)
    parent, parts = _great_frame(parent)
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    fitting = P(tier, "accent") if tier != "wood" else P(tier, "dark")
    # A hilt long enough for both hands.
    add("hilt", bc.mesh_capsule(0.023, 0.023, 0.22), P(tier, "grip"), parent, loc=(0, 0, 0.17))
    add("pommel", bc.mesh_ellipsoid(0.038, 0.038, 0.038), fitting, parent, loc=(0, 0, 0.20))
    add("guard", bc.mesh_ellipsoid(guard, 0.036, 0.032), fitting, parent, loc=(0, 0, -0.06))
    top = -0.07
    if "crystal" in extras:
        add("blade", mesh_gem(width, width * 0.4, blade * 0.55, sides=4), P(tier, "main"), parent, loc=(0, 0, top - blade * 0.5))
    else:
        # Thicker through than a sword's: swung, it is seen edge-on as often as
        # flat, and a flat one is a line for half the clip.
        add("blade", bc.mesh_capsule(width, width * 0.5, blade, squash_y=0.62), P(tier, "main"), parent, loc=(0, 0, top))
        add("fuller", bc.mesh_capsule(width * 0.34, width * 0.2, blade * 0.78, squash_y=0.5),
            glow_or(tier, "light") if "edge" in extras else P(tier, "light"), parent, loc=(0, -0.004, top - 0.03))
        parts.append(bc.spike("point", (0, 0, top - blade + 0.02), (0, 0, top - blade - width * 1.3), width * 0.72,
                              P(tier, "main"), parent, r_tip=0.006))
    if "spikes" in extras:
        for side in (-1, 1):
            parts.append(bc.spike("barb", (side * width * 0.7, 0, top - blade * 0.3), (side * width * 1.9, 0, top - blade * 0.22),
                                  0.026, P(tier, "main"), parent))
    _dress(tier, parts, parent, -0.06, guard * 1.25)
    return parts


def build_greataxe(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    haft = 0.66 + blade * 0.35
    bit = 0.15 + width * 1.2                     # how far the blade stands off the haft
    parent, parts = _great_frame(parent)
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("haft", bc.mesh_capsule(0.024, 0.022, haft + 0.24), "wood_dark" if tier not in ("wood", "demonite") else P(tier, "dark"),
        parent, loc=(0, 0, 0.22))
    add("grip", bc.mesh_capsule(0.027, 0.027, 0.20), P(tier, "grip"), parent, loc=(0, 0, 0.16))
    at = -haft * 0.80
    double = "heavy" in extras or "wings" in extras or "spikes" in extras
    edge = glow_or(tier, "light") if "edge" in extras else P(tier, "light")
    for side in ((1, -1) if double else (1,)):
        # A fan: narrow where it leaves the haft and twice as tall at the edge,
        # which is what makes it an axe and not a hammer -- a box laid on its
        # side, with its far face flared -- and a bright curved edge on the end.
        add("cheek", mesh_box(bit * 0.95, 0.03, bit, taper=2.3), P(tier, "main"), parent,
            loc=(side * bit * 0.55, 0, at), rot=(0, math.radians(side * 90), 0))
        add("edge", bc.mesh_ellipsoid(bit * 0.16, 0.02, bit * 1.22), edge, parent, loc=(side * bit * 1.04, 0, at))
    if not double:
        parts.append(bc.spike("poll", (-0.02, 0, at), (-bit * 0.5, 0, at), 0.04, P(tier, "dark"), parent, r_tip=0.03))
    add("eye", bc.mesh_ellipsoid(0.045, 0.045, bit * 0.5), P(tier, "dark"), parent, loc=(0, 0, at))
    parts.append(bc.spike("spike", (0, 0, at - bit * 0.4), (0, 0, at - bit * 1.0), 0.03, P(tier, "light"), parent, r_tip=0.005))
    _dress(tier, parts, parent, at + bit * 0.75, 0.10)
    return parts


def build_crossbow(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    span = 0.17 + blade * 0.16                   # half the prod
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    stock = "wood_dark" if tier != "wood" else P(tier, "main")
    # Along -Z like a blade: nose down at the side, and level when it is brought up.
    parts.append(bc.spike("stock", (0, 0, 0.12), (0, 0, -0.40), 0.036, stock, parent, r_tip=0.028))
    add("butt", bc.mesh_ellipsoid(0.04, 0.05, 0.05), stock, parent, loc=(0, 0, 0.13))
    nose = -0.34
    limb = P(tier, "main")
    tips = []
    for side in (-1, 1):
        mid = (side * span * 0.6, 0, nose - 0.02)
        tip = (side * span, 0, nose + 0.07)
        parts.append(bc.spike("prod", (0, 0, nose), mid, 0.030, limb, parent, r_tip=0.024))
        parts.append(bc.spike("prod", mid, tip, 0.024, limb, parent, r_tip=0.016))
        tips.append(tip)
        if "wings" in extras or "spikes" in extras:
            parts.append(bc.spike("barb", mid, (side * span * 0.8, 0, nose - 0.10), 0.02, P(tier, "light"), parent))
    string = glow_or(tier, "light") if PALETTES[tier]["glow"] else "string"
    nut = (0, -0.02, nose + 0.22)                # spanned: the string drawn back to the nut
    for tip in tips:
        parts.append(bc.spike("string", tip, nut, 0.008, string, parent, r_tip=0.008))
    parts.append(bc.spike("bolt", (0, -0.03, nose + 0.22), (0, -0.03, nose - 0.10), 0.012, P(tier, "light"), parent, r_tip=0.004))
    add("stirrup", bc.mesh_torus(0.045, 0.012), P(tier, "dark"), parent, loc=(0, 0, nose - 0.05), rot=(0, math.radians(90), 0))
    add("lock", bc.mesh_ellipsoid(0.03, 0.03, 0.04), P(tier, "accent"), parent, loc=(0, -0.03, nose + 0.24))
    if "gem" in extras:
        add("gem", mesh_gem(0.026, 0.02, 0.03), glow_or(tier, "light"), parent, loc=(0, -0.04, nose + 0.02))
    return parts


def build_wand(tier, parent):
    blade, width, guard, extras = SWORDS[tier]
    length = 0.22 + blade * 0.22
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("grip", bc.mesh_capsule(0.022, 0.020, 0.10), P(tier, "grip"), parent, loc=(0, 0, 0.05))
    parts.append(bc.spike("rod", (0, 0, -0.04), (0, 0, -0.04 - length), 0.020, P(tier, "main") if tier != "wood" else "wood_dark",
                          parent, r_tip=0.012))
    add("band", bc.mesh_ellipsoid(0.028, 0.028, 0.016), P(tier, "accent"), parent, loc=(0, 0, -0.045))
    tip = -0.04 - length
    if tier == "wood":
        add("knot", bc.mesh_ellipsoid(0.028, 0.028, 0.03), P(tier, "light"), parent, loc=(0, 0, tip))
    else:
        add("tip", mesh_gem(0.034, 0.030, 0.06), glow_or(tier, "light"), parent, loc=(0, 0, tip - 0.03))
        if "wings" in extras or "horns" in extras:
            for side in (-1, 1):
                parts.append(bc.spike("claw", (0, 0, tip + 0.03), (side * 0.06, 0, tip - 0.04), 0.014, P(tier, "accent"), parent))
    return parts


def build_grimoire(tier, parent):
    """A book held open on the palm. Its pages face -Y, which is up once the
    forearm is level (and forward, at the hip, when the arm hangs)."""
    blade, width, guard, extras = SWORDS[tier]
    parts = []
    frame = bc.empty("book_frame", (0.0, -0.04, -0.10), parent)
    parts.append(frame)
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    # Big for a book, and the cover a good deal bigger than the pages: in the
    # hand it is six pixels across, and what makes six pixels a book is a dark
    # board showing all round two pale pages with a dark gutter between them.
    # The cover was the tier's pale metal and the pages a hair smaller, and what
    # that made was a white tile.
    w, h = 0.16, 0.22
    for side in (-1, 1):
        rot = (0, 0, math.radians(side * 16))
        add("cover", mesh_box(w, 0.026, h), P(tier, "dark"), frame, loc=(side * w * 0.52, 0.018, 0), rot=rot)
        add("pages", mesh_box(w * 0.76, 0.022, h * 0.78), "string", frame, loc=(side * w * 0.50, -0.004, 0), rot=rot)
        add("corner", mesh_box(0.04, 0.030, 0.04), P(tier, "accent"), frame, loc=(side * w * 0.93, 0.016, h * 0.42), rot=rot)
        add("corner", mesh_box(0.04, 0.030, 0.04), P(tier, "accent"), frame, loc=(side * w * 0.93, 0.016, -h * 0.42), rot=rot)
        # Lines of writing: what says it is a book and not a tile.
        for k in (-0.055, 0.0, 0.055):
            add("line", mesh_box(w * 0.5, 0.004, 0.018), P(tier, "dark"), frame, loc=(side * w * 0.50, -0.017, k), rot=rot)
    add("spine", bc.mesh_capsule(0.022, 0.022, h), P(tier, "main"), frame, loc=(0, 0.024, h * 0.5))
    add("gutter", mesh_box(0.02, 0.012, h * 0.8), P(tier, "dark"), frame, loc=(0, -0.006, 0))
    if PALETTES[tier]["glow"] or "gem" in extras:
        add("sigil", mesh_gem(0.03, 0.012, 0.03), glow_or(tier, "light"), frame, loc=(0, -0.02, 0.0))
    return parts


def build_orb(tier, parent):
    """An orb over an open palm, in a claw of the tier's metal."""
    blade, width, guard, extras = SWORDS[tier]
    # As big as the fist holding it twice over, and well clear of the palm: at
    # three pixels across and touching the hand it was a button on the glove.
    r = 0.086 + blade * 0.04
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    at = (0.0, -0.155, -0.07)
    # The glass is the colour of what is in it, not of the tier: a tier's pale
    # metal made a steel orb a white ball, which is a snowball. The tiers that
    # glow keep their own light; the rest are the violet of a spell with no
    # element, with the tier in the claw and the ring round it.
    add("orb", bc.mesh_ellipsoid(r, r, r), P(tier, "glow") if PALETTES[tier]["glow"] else "orb_glass", parent, loc=at)
    add("core", bc.mesh_ellipsoid(r * 0.5, r * 0.5, r * 0.5), "orb_core", parent, loc=(at[0] - r * 0.25, at[1] - r * 0.6, at[2] - r * 0.2))
    for a in (20, 140, 260):
        dx, dz = math.cos(math.radians(a)), math.sin(math.radians(a))
        parts.append(bc.spike("claw", (0, -0.02, -0.06), (at[0] + dx * r * 1.05, at[1] + r * 0.2, at[2] + dz * r * 1.05), 0.016,
                              P(tier, "dark"), parent, r_tip=0.008))
    add("ring", bc.mesh_torus(r * 1.35, 0.010), P(tier, "accent"), parent, loc=at, rot=(math.radians(62), 0, math.radians(20)))
    if "wings" in extras or "crystal" in extras:
        add("ring2", bc.mesh_torus(r * 1.6, 0.008), P(tier, "light"), parent, loc=at, rot=(math.radians(100), math.radians(30), 0))
    return parts


# The four elements' staves: a tier's own staff with the element in its head.
# Only their icons are modelled; in the hand they are that tier's staff, tinted.
ELEMENTS = {"fire": (1.00, 0.50, 0.18), "water": (0.32, 0.64, 1.00), "earth": (0.74, 0.56, 0.30), "air": (0.84, 0.96, 1.00)}
for _el, _rgb in ELEMENTS.items():
    bc.PALETTE["element_" + _el] = _rgb


def _element_staff(element):
    def build(tier, parent):
        parts = build_staff(tier, parent)
        frame = parts[0]
        top = 0.60
        parts.append(bc.part("element", mesh_gem(0.10, 0.085, 0.15), "element_" + element, frame, loc=(0, -0.02, top + 0.10)))
        for side in (-1, 1):
            parts.append(bc.spike("claw", (0, 0, top - 0.06), (side * 0.10, 0, top + 0.10), 0.02, "element_" + element, frame))
        return parts
    return build


WEAPONS = {"sword": build_sword, "spear": build_spear, "bow": build_bow, "staff": build_staff,
           "axe": build_axe, "pickaxe": build_pickaxe, "rod": build_rod,
           "dagger": build_dagger, "mace": build_mace, "greatsword": build_greatsword, "greataxe": build_greataxe,
           "crossbow": build_crossbow, "knives": build_knives, "wand": build_wand, "grimoire": build_grimoire,
           "orb": build_orb}


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
        if tier == "demonite":
            for side in (-1, 1):
                parts.append(bc.spike("spike", (side * 0.26, 0, 0.3), (side * 0.40, 0, 0.44), 0.05,
                                      P(tier, "light"), parent))
        if tier == "damascus":
            add("rivets", bc.mesh_ellipsoid(0.30, 0.04, 0.03), P(tier, "dark"), parent, loc=(0, -0.03, 0.30))
        if tier == "orichalcum":
            # Heavy and banded: red gold worked the way the old smiths worked it.
            for z in (0.10, 0.26):
                add("band", bc.mesh_ellipsoid(0.29, 0.04, 0.025), P(tier, "accent"), parent,
                    loc=(0, -0.03, z))
        if tier == "dracon":
            for side in (-1, 1):
                parts.append(bc.spike("wing", (side * 0.24, 0, 0.32), (side * 0.44, 0, 0.14), 0.06,
                                      P(tier, "accent"), parent))
        if tier == "enchanted":
            for dz in (0.06, 0.20, 0.34):
                add("shard", mesh_gem(0.045, 0.035, 0.08, sides=4), P(tier, "accent"), parent,
                    loc=(0, -0.09, dz))
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
    if tier == "damascus":
        add("ridge", bc.mesh_ellipsoid(0.05, 0.26, 0.05), P(tier, "light"), parent, loc=(0, 0, 0.26))
    if tier == "diamond":
        for dx in (-0.12, 0, 0.12):
            add("crystal", mesh_gem(0.04, 0.04, 0.10, sides=4), P(tier, "light"), parent, loc=(dx, 0, 0.30))
    if tier == "platinum":
        for side in (-1, 1):
            parts.append(bc.spike("wing", (side * 0.22, 0, 0.1), (side * 0.40, 0, 0.30), 0.04,
                                  P(tier, "light"), parent))
    if tier == "demonite":
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.2, 0, 0.14), (side * 0.36, 0, 0.40), 0.06,
                                  P(tier, "light"), parent))
        add("eyes", mesh_box(0.18, 0.02, 0.02), P(tier, "glow"), parent, loc=(0, -0.28, -0.02))
    if tier == "orichalcum":
        # A broad brow and a cheek plate either side: a heavier helm, not a
        # spikier one -- the spikes belong to the tiers above it.
        add("brow", bc.mesh_ellipsoid(0.27, 0.27, 0.04), P(tier, "accent"), parent, loc=(0, 0, 0.10))
        for side in (-1, 1):
            add("cheek", bc.mesh_ellipsoid(0.07, 0.10, 0.12), P(tier, "dark"), parent,
                loc=(side * 0.22, -0.08, -0.06))
    if tier == "dracon":
        # Horns curling forward, and a fin down the crown.
        for side in (-1, 1):
            parts.append(bc.spike("horn", (side * 0.20, 0.04, 0.16), (side * 0.34, -0.22, 0.34), 0.06,
                                  P(tier, "accent"), parent))
        add("fin", bc.mesh_ellipsoid(0.04, 0.24, 0.10), P(tier, "light"), parent, loc=(0, 0.02, 0.28))
        add("eyes", mesh_box(0.18, 0.02, 0.02), P(tier, "glow"), parent, loc=(0, -0.28, -0.02))
    if tier == "enchanted":
        for dx, dz in ((-0.13, 0.28), (0, 0.36), (0.13, 0.28)):
            add("shard", mesh_gem(0.04, 0.04, 0.12, sides=4), P(tier, "accent"), parent,
                loc=(dx, 0, dz))
        add("eyes", mesh_box(0.16, 0.02, 0.02), P(tier, "glow"), parent, loc=(0, -0.27, -0.02))
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
        if tier == "demonite":
            parts.append(bc.spike("spike", (side * 0.3, 0, 0.22), (side * 0.44, 0, 0.40), 0.05,
                                  P(tier, "light"), parent))
        if tier == "diamond":
            add("crystal", mesh_gem(0.05, 0.05, 0.10, sides=4), P(tier, "light"), parent,
                loc=(side * 0.27, 0, 0.30))
        if tier == "orichalcum":
            add("stud", bc.mesh_ellipsoid(0.05, 0.05, 0.05), P(tier, "accent"), parent,
                loc=(side * 0.27, -0.04, 0.24))
        if tier == "dracon":
            parts.append(bc.spike("wing", (side * 0.26, 0.04, 0.22), (side * 0.48, 0.16, 0.42), 0.06,
                                  P(tier, "accent"), parent))
        if tier == "enchanted":
            add("shard", mesh_gem(0.04, 0.04, 0.12, sides=4), P(tier, "accent"), parent,
                loc=(side * 0.27, 0, 0.32))
    if PALETTES[tier]["glow"]:
        add("core", mesh_gem(0.05, 0.03, 0.06), P(tier, "glow"), parent, loc=(0, -0.22, 0.10))
    if tier == "steel":
        add("trim", bc.mesh_ellipsoid(0.03, 0.03, 0.14), P(tier, "accent"), parent, loc=(0, -0.21, 0.08))
    if tier == "orichalcum":
        add("collar", bc.mesh_torus(0.15, 0.03), P(tier, "accent"), parent, loc=(0, 0, 0.30))
    if tier == "dracon":
        for z in (0.02, 0.12, 0.22):
            add("scale", bc.mesh_ellipsoid(0.11, 0.05, 0.035), P(tier, "dark"), parent,
                loc=(0, -0.17, z))
    return parts


def build_legs(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("waist", bc.mesh_ellipsoid(0.24, 0.14, 0.06), P(tier, "dark"), parent, loc=(0, 0, 0.30))
    for side in (-1, 1):
        add("thigh", bc.mesh_capsule(0.10, 0.085, 0.20), P(tier, "main"), parent, loc=(side * 0.11, 0, 0.26))
        add("knee", bc.mesh_ellipsoid(0.07, 0.07, 0.06), P(tier, "light"), parent, loc=(side * 0.11, -0.05, -0.02))
        add("shin", bc.mesh_capsule(0.085, 0.075, 0.22), P(tier, "main"), parent, loc=(side * 0.11, 0, -0.06))
        if tier in ("demonite", "dracon"):
            parts.append(bc.spike("spike", (side * 0.11, -0.05, -0.02), (side * 0.2, -0.12, 0.06), 0.04,
                                  P(tier, "light" if tier == "demonite" else "accent"), parent))
        if tier == "orichalcum":
            add("band", bc.mesh_torus(0.09, 0.022), P(tier, "accent"), parent,
                loc=(side * 0.11, 0, -0.16))
        if tier == "enchanted":
            add("shard", mesh_gem(0.03, 0.03, 0.08, sides=4), P(tier, "accent"), parent,
                loc=(side * 0.11, -0.09, 0.04))
        if PALETTES[tier]["glow"] and tier not in ("demonite", "dracon", "enchanted"):
            add("gem", mesh_gem(0.03, 0.02, 0.04), P(tier, "glow"), parent, loc=(side * 0.11, -0.08, -0.02))
    return parts


def build_ore(tier, parent):
    """A lump of rock with the tier showing through it: nuggets, veins or
    crystals. Wood has no ore; bronze is worked from copper ore."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    rock = "rock_dark" if tier in ("demonite", "damascus") else "rock"
    add("rock", bc.mesh_ellipsoid(0.30, 0.24, 0.20), rock, parent)
    add("rock2", bc.mesh_ellipsoid(0.16, 0.16, 0.14), rock, parent, loc=(0.2, 0.02, 0.1))
    spots = [(-0.12, -0.2, 0.06), (0.06, -0.22, 0.0), (0.18, -0.12, 0.16), (-0.05, -0.14, 0.16)]
    if tier == "steel":            # coal: black lumps rather than metal in rock
        for i, s in enumerate(spots):
            add("coal", mesh_gem(0.10, 0.09, 0.08, sides=5), P("demonite", "main"), parent, loc=s)
        return parts
    crystal = tier in ("azuryte", "diamond", "demonite")
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
        "damascus": "damascus_ore", "orichalcum": "orichalcum_ore", "diamond": "diamond_ore",
        "platinum": "platinum_ore", "demonite": "demonite_ore"}
BARS = {"bronze": "bronze_bar", "iron": "iron_bar", "steel": "steel_bar", "azuryte": "azuryte_bar",
        "damascus": "damascus_bar", "orichalcum": "orichalcum_bar", "diamond": "diamond_ingot",
        "platinum": "platinum_bar", "demonite": "demonite_bar",
        # Neither of the last two is mined, so neither has an ore icon -- but
        # both still need a bar to sit in the bag.
        "dracon": "dracon_bar", "enchanted": "enchanted_bar"}


ARMOURY_ONLY = {"on": False}


def all_icons(only_tiers):
    ICON_MODE["on"] = True
    count = 0
    for tier in only_tiers:
        #                  file                builder       tilt  spin  fill
        jobs = [("sword_" + tier,  build_sword,  -135, 0,   1.0),
                ("staff_" + tier,  build_staff,  45,   0,   1.0),
                ("spear_" + tier,  build_spear,  45,   0,   1.0),
                ("bow_" + tier,    build_bow,    30,   0,   0.96),
                ("shield_" + tier, build_shield, 0,    12,  0.9),
                ("helm_" + tier,   build_helm,   0,    20,  0.88),
                ("body_" + tier,   build_body,   0,    14,  0.9),
                ("legs_" + tier,   build_legs,   0,    14,  0.88)]
        armoury = [("dagger_" + tier,     build_dagger,     -135, 0,  0.80),
                   ("mace_" + tier,       build_mace,       -135, 0,  0.95),
                   ("greatsword_" + tier, build_greatsword, -135, 0,  1.0),
                   ("greataxe_" + tier,   build_greataxe,   -135, 0,  1.0),
                   ("crossbow_" + tier,   build_crossbow,   -135, 0,  0.96),
                   ("knives_" + tier,     build_knives,     -135, 0,  0.74),
                   ("wand_" + tier,       build_wand,       -135, 0,  0.84),
                   ("grimoire_" + tier,   build_grimoire,   0,    0,  0.92),
                   ("orb_" + tier,        build_orb,        0,    24, 0.86)]
        armoury += [("%s_staff_%s" % (el, tier), _element_staff(el), 45, 0, 1.0) for el in ELEMENTS]
        if ARMOURY_ONLY["on"]:
            jobs = []
        jobs += armoury
        if ARMOURY_ONLY["on"]:
            for name, builder, tilt, spin, fill in jobs:
                render_icon(name, builder, tier, tilt, spin, fill)
                count += 1
            continue
        # Turned side-on, so the blade and the points are seen in profile.
        jobs.append(("axe_" + tier,     build_axe,     -135, 90, 1.0))
        jobs.append(("pickaxe_" + tier, build_pickaxe, -135, 90, 1.0))
        # A tier with no ore is smelted from something else -- dracon from a
        # dragon's fang, enchanted in the Reverie -- so it has a bar to draw
        # but no rock.
        if tier in ORES:
            jobs.append((ORES[tier], build_ore, 0, 18, 0.9))
        if tier in BARS:
            jobs.append((BARS[tier], build_bar, 0, 28, 0.9))
        for name, builder, tilt, spin, fill in jobs:
            render_icon(name, builder, tier, tilt, spin, fill)
            count += 1
    # The rod and the fish belong to no tier; they are drawn once, with the
    # full set.
    if len(only_tiers) == len(TIERS) and not ARMOURY_ONLY["on"]:
        render_icon("fishing_rod", build_rod, "wood", -135, 0, 1.0)
        count += 1
        for name in FISH:
            render_icon("raw_" + name, lambda t, p, n=name: build_fish(n, p), "wood", 0, 0, 0.94)
            render_icon("cooked_" + name, lambda t, p, n=name: build_fish(n, p, cooked=True), "wood", 0, 0, 0.94)
            count += 2
    print("icons %d" % count)


# --- herbs, vials, potions and recipes (icons) -------------------------------------------------
# Foraging and Brewing. Everything is laid out in the picture plane (X across,
# Z up), facing the icon camera, the way a fish is.

bc.PALETTE.update({
    "stalk": (0.28, 0.44, 0.20), "stalk_dk": (0.18, 0.30, 0.14), "leafy": (0.36, 0.60, 0.26),
    "marigold_head": (0.98, 0.56, 0.10), "marigold_eye": (0.80, 0.32, 0.06),
    "mint_leaf": (0.40, 0.78, 0.50), "mint_tip": (0.72, 0.62, 0.92),
    "nettle_leaf": (0.22, 0.46, 0.20), "nettle_vein": (0.36, 0.60, 0.30),
    "bog_leaf": (0.48, 0.62, 0.32), "bog_white": (0.97, 0.94, 0.95), "bog_pink": (0.90, 0.60, 0.68),
    "sage_leaf": (0.58, 0.66, 0.55), "sage_leaf_dk": (0.42, 0.50, 0.40),
    "cap_glow": (0.52, 0.90, 0.92), "cap_stalk": (0.88, 0.85, 0.76),
    "ember_petal": (0.93, 0.24, 0.10), "ember_glow": (1.00, 0.78, 0.26), "ember_stalk": (0.28, 0.16, 0.14),
    "moon_petal": (0.44, 0.58, 0.96), "moon_glow": (0.90, 0.94, 1.00), "moon_stalk": (0.36, 0.42, 0.60),
    "star_petal": (1.00, 0.97, 0.88), "star_glow": (1.00, 0.84, 0.36), "star_stalk": (0.52, 0.74, 0.74),
    "glass": (0.76, 0.86, 0.90), "glass_shine": (0.97, 0.99, 1.00), "cork": (0.66, 0.48, 0.30),
    "parchment": (0.90, 0.83, 0.64), "parchment_dk": (0.72, 0.62, 0.44), "rod_wood": (0.40, 0.26, 0.16),
    "seal": (0.72, 0.14, 0.14), "ink": (0.26, 0.20, 0.18),
    "fang_ice": (0.92, 0.96, 0.99), "fang_root": (0.74, 0.82, 0.90), "fang_glow": (0.78, 0.96, 1.00),
    # What the dead of Hollowrest leave behind.
    "rot_meat": (0.48, 0.34, 0.33), "rot_meat_lt": (0.62, 0.47, 0.44), "rot_mould": (0.46, 0.55, 0.36),
    "bone_pale": (0.88, 0.86, 0.78), "tarnish": (0.42, 0.44, 0.40), "tarnish_lt": (0.60, 0.60, 0.52),
    "grave_stone_gem": (0.34, 0.30, 0.42), "locket_hair": (0.36, 0.26, 0.20),
    "wax": (0.84, 0.82, 0.72), "wax_lt": (0.93, 0.92, 0.85), "candle_flame": (1.00, 0.84, 0.42),
    # The lantern and what lights it.
    "lamp_iron": (0.30, 0.31, 0.34), "lamp_iron_lt": (0.50, 0.52, 0.56),
    "horn_pane": (0.98, 0.84, 0.46), "horn_pane_dk": (0.42, 0.40, 0.34),
    "lamp_flame": (1.00, 0.92, 0.60), "flint_grey": (0.44, 0.44, 0.48), "char_cloth": (0.26, 0.24, 0.22),
    "bog_leather": (0.20, 0.22, 0.18), "bog_leather_lt": (0.28, 0.30, 0.24), "bog_sole": (0.14, 0.15, 0.13),
    "drowned_gold": (0.78, 0.66, 0.30), "bog_weed": (0.36, 0.48, 0.26), "bog_glow": (0.56, 0.86, 0.72),
})


def _leaf(parent, name, at, length, width, angle, colour, depth=0.012):
    """A leaf lying in the picture plane, pointing along `angle` (degrees from +X)."""
    a = math.radians(angle)
    centre = (at[0] + math.cos(a) * length * 0.5, at[1], at[2] + math.sin(a) * length * 0.5)
    return bc.part(name, bc.mesh_ellipsoid(length * 0.5, depth, width * 0.5), colour, parent,
                   loc=centre, rot=(0, -a, 0))


def _sprig(parent, parts, base, top, colour="stalk", r=0.014):
    parts.append(bc.spike("stalk", base, top, r, colour, parent, r_tip=r * 0.7))


def build_herb(name, parent):
    parts = []
    if name == "marigold":
        for i, (top, h) in enumerate((((-0.07, 0, 0.12), 0.07), ((0.08, 0, 0.16), 0.08))):
            _sprig(parent, parts, (0, 0, -0.20), top)
            parts.append(bc.part("head", bc.mesh_ellipsoid(h, 0.04, h), "marigold_head", parent, loc=top))
            parts.append(bc.part("eye", bc.mesh_ellipsoid(h * 0.45, 0.045, h * 0.45), "marigold_eye", parent,
                                 loc=(top[0], -0.01, top[2])))
        parts.append(_leaf(parent, "leaf", (0, 0, -0.10), 0.14, 0.06, 150, "leafy"))
        parts.append(_leaf(parent, "leaf", (0, 0, -0.06), 0.14, 0.06, 30, "leafy"))
    elif name == "brookmint":
        _sprig(parent, parts, (0, 0, -0.22), (0, 0, 0.18), "stalk")
        for k in range(4):
            z = -0.14 + k * 0.08
            size = 0.13 - k * 0.018
            parts.append(_leaf(parent, "l", (0, 0, z), size, 0.07, 160 - k * 6, "mint_leaf"))
            parts.append(_leaf(parent, "r", (0, 0, z), size, 0.07, 20 + k * 6, "mint_leaf"))
        parts.append(bc.part("tip", bc.mesh_ellipsoid(0.03, 0.03, 0.05), "mint_tip", parent, loc=(0, 0, 0.21)))
    elif name == "nettle":
        _sprig(parent, parts, (0, 0, -0.24), (0.02, 0, 0.20), "stalk_dk", 0.018)
        for k in range(3):
            z = -0.14 + k * 0.12
            size = 0.19 - k * 0.04
            for side, ang in ((0, 200), (1, -20)):
                l = _leaf(parent, "nl", (0.01, 0, z), size, 0.10 - k * 0.015, ang if k else ang + (10 if side else -10),
                          "nettle_leaf")
                parts.append(l)
                a = math.radians(ang)
                parts.append(bc.spike("vein", (0.01, -0.012, z), (0.01 + math.cos(a) * size * 0.8, -0.012,
                                                                  z + math.sin(a) * size * 0.8),
                                      0.006, "nettle_vein", parent, r_tip=0.004))
    elif name == "bogbean":
        _sprig(parent, parts, (0, 0, -0.24), (-0.04, 0, 0.04), "bog_leaf", 0.016)
        for ang in (40, 150, 260):
            parts.append(_leaf(parent, "tri", (-0.04, 0, 0.04), 0.13, 0.09, ang, "bog_leaf"))
        _sprig(parent, parts, (0, 0, -0.24), (0.10, 0, 0.20), "bog_leaf", 0.012)
        for k in range(4):
            parts.append(bc.part("flower", bc.mesh_ellipsoid(0.028, 0.03, 0.028), "bog_white" if k % 2 else "bog_pink",
                                 parent, loc=(0.10 - k * 0.012, -0.01, 0.20 - k * 0.05)))
    elif name == "mountain_sage":
        # A tied bundle of soft grey-green leaves.
        for k, ang in enumerate((60, 80, 100, 120, 90)):
            parts.append(_leaf(parent, "sg", (0, 0, -0.06), 0.24, 0.09, ang, "sage_leaf" if k % 2 else "sage_leaf_dk",
                               depth=0.02 + 0.004 * k))
        for k in range(3):
            parts.append(bc.spike("stem", (0, 0, -0.06), (-0.03 + k * 0.03, 0, -0.22), 0.012, "stalk_dk", parent))
        parts.append(bc.part("twine", bc.mesh_torus(0.035, 0.012), "string", parent, loc=(0, 0, -0.08),
                             rot=(math.radians(90), 0, 0)))
    elif name == "glowcap":
        for (x, h, r) in ((-0.06, 0.20, 0.10), (0.08, 0.13, 0.075)):
            parts.append(bc.part("stalk", bc.mesh_capsule(r * 0.3, r * 0.36, h), "cap_stalk", parent,
                                 loc=(x, 0, -0.20 + h)))
            parts.append(bc.part("cap", bc.mesh_ellipsoid(r, r * 0.9, r * 0.55), "cap_glow", parent,
                                 loc=(x, 0, -0.20 + h)))
    else:
        petal, glow, stalk, count, length = {
            "emberbloom": ("ember_petal", "ember_glow", "ember_stalk", 6, 0.12),
            "moonpetal":  ("moon_petal", "moon_glow", "moon_stalk", 6, 0.12),
            "starlily":   ("star_petal", "star_glow", "star_stalk", 6, 0.15),
        }[name]
        centre = (0.0, 0.0, 0.06)
        _sprig(parent, parts, (0, 0, -0.24), centre, stalk)
        parts.append(_leaf(parent, "leaf", (0, 0, -0.14), 0.12, 0.05, 150, stalk))
        for k in range(count):
            ang = 90 + k * 360.0 / count
            if name == "emberbloom":
                a = math.radians(ang)
                parts.append(bc.spike("petal", centre, (math.cos(a) * length, 0, centre[2] + math.sin(a) * length),
                                      0.04, petal, parent, r_tip=0.006))
            else:
                parts.append(_leaf(parent, "petal", centre, length, 0.07 if name == "moonpetal" else 0.055, ang, petal))
        parts.append(bc.part("core", bc.mesh_ellipsoid(0.035, 0.03, 0.035), glow, parent,
                             loc=(0, -0.02, centre[2])))
    return parts


POTIONS = {
    #                     liquid               glow?  shape
    "healing_draught":   ((0.86, 0.20, 0.18), False, "round"),
    "mana_tonic":        ((0.22, 0.42, 0.92), False, "round"),
    "nettle_brew":       ((0.24, 0.52, 0.20), False, "round"),
    "fen_bitters":       ((0.50, 0.44, 0.20), False, "tall"),
    "stoneskin_draught": ((0.56, 0.56, 0.60), False, "tall"),
    "hunters_focus":     ((0.24, 0.78, 0.70), True,  "flask"),
    "emberfire_elixir":  ((1.00, 0.46, 0.10), True,  "flask"),
    "moonlit_draught":   ((0.62, 0.72, 1.00), True,  "flask"),
    "starlily_panacea":  ((1.00, 0.90, 0.56), True,  "star"),
}
for _name, (_rgb, _glow, _shape) in POTIONS.items():
    bc.PALETTE["potion_%s" % _name] = _rgb
    bc.PALETTE["potion_%s_glow" % _name] = _rgb


def build_potion(name, parent):
    """A vial, empty or filled. The liquid is the body, so the colour carries the
    whole icon; a brew that glows is lit from inside."""
    parts = []
    if name == "vial":
        liquid, shape = "glass", "round"
    else:
        rgb, glow, shape = POTIONS[name]
        liquid = ("potion_%s_glow" if glow else "potion_%s") % name
    if shape == "tall":
        parts.append(bc.part("body", bc.mesh_capsule(0.10, 0.11, 0.20), liquid, parent, loc=(0, 0, 0.02)))
        neck_z = 0.08
    elif shape == "flask":
        parts.append(bc.part("body", bc.mesh_ellipsoid(0.15, 0.10, 0.12), liquid, parent, loc=(0, 0, -0.10)))
        parts.append(bc.part("shoulder", bc.mesh_capsule(0.05, 0.10, 0.08), liquid, parent, loc=(0, 0, 0.02)))
        neck_z = 0.07
    else:
        parts.append(bc.part("body", bc.mesh_ellipsoid(0.13, 0.11, 0.13), liquid, parent, loc=(0, 0, -0.08)))
        neck_z = 0.05
    if name != "vial":
        parts.append(bc.part("glass_top", bc.mesh_ellipsoid(0.08, 0.07, 0.03), "glass", parent,
                             loc=(0, 0, neck_z - 0.04)))
    parts.append(bc.part("neck", bc.mesh_capsule(0.042, 0.048, 0.09), "glass", parent, loc=(0, 0, neck_z + 0.08)))
    parts.append(bc.part("lip", bc.mesh_torus(0.045, 0.014), "glass", parent, loc=(0, 0, neck_z + 0.08),
                         rot=(0, 0, 0)))
    parts.append(bc.part("cork", bc.mesh_capsule(0.036, 0.034, 0.06), "cork", parent, loc=(0, 0, neck_z + 0.13)))
    parts.append(bc.part("shine", bc.mesh_ellipsoid(0.022, 0.02, 0.05), "glass_shine", parent,
                         loc=(-0.07, -0.10, -0.06)))
    if shape == "star":
        # A glint of starlight beside the bottle, not over it.
        for a in (0, 90):
            r = math.radians(a)
            parts.append(bc.spike("glint", (0.15 - math.cos(r) * 0.05, -0.12, 0.06 - math.sin(r) * 0.05),
                                  (0.15 + math.cos(r) * 0.05, -0.12, 0.06 + math.sin(r) * 0.05),
                                  0.014, "star_glow", parent, r_tip=0.014))
    return parts


def build_recipe_scroll(parent):
    parts = []
    parts.append(bc.part("sheet", mesh_box(0.30, 0.02, 0.24), "parchment", parent, loc=(0, 0, 0)))
    for z in (0.13, -0.13):
        parts.append(bc.part("roll", bc.mesh_capsule(0.035, 0.035, 0.34), "parchment_dk", parent,
                             loc=(0.17, 0, z), rot=(0, math.radians(90), 0)))
        for x in (-0.19, 0.19):
            parts.append(bc.part("knob", bc.mesh_ellipsoid(0.02, 0.02, 0.03), "rod_wood", parent, loc=(x, 0, z)))
    for k in range(4):
        parts.append(bc.part("line", mesh_box(0.18 - 0.05 * (k % 2), 0.03, 0.022), "ink", parent,
                             loc=(-0.02 - 0.025 * (k % 2), -0.012, 0.065 - k * 0.045)))
    parts.append(bc.part("seal", bc.mesh_ellipsoid(0.045, 0.03, 0.045), "seal", parent, loc=(0.10, -0.02, -0.08)))
    return parts


# --- what the monsters leave (icons) ----------------------------------------------------------
bc.PALETTE.update({
    "silk": (0.94, 0.94, 0.90), "silk_dk": (0.74, 0.74, 0.72),
    "scale_green": (0.36, 0.62, 0.34), "scale_green_lt": (0.62, 0.82, 0.48),
    "troll_hide": (0.72, 0.80, 0.86), "troll_fur": (0.93, 0.95, 0.97),
    "wyv_scale": (0.46, 0.66, 0.86), "wyv_scale_glow": (0.66, 0.94, 1.00),
    "horn_black": (0.22, 0.17, 0.17), "horn_tip": (0.62, 0.20, 0.14),
})


def build_trophy(name, parent):
    parts = []
    if name == "spider_silk":
        # A skein of silk wound on itself, with a loose strand.
        for k in range(4):
            parts.append(bc.part("wind", bc.mesh_torus(0.13 - k * 0.018, 0.035), "silk" if k % 2 else "silk_dk", parent,
                                 loc=(0, 0, -0.04 + k * 0.03), rot=(math.radians(70), 0, k * 0.6)))
        parts.append(bc.spike("strand", (0.10, -0.02, 0.0), (0.24, -0.02, 0.20), 0.012, "silk", parent, r_tip=0.006))
    elif name in ("lizard_scale", "wyvern_scale"):
        main, light = ("scale_green", "scale_green_lt") if name == "lizard_scale" else ("wyv_scale", "wyv_scale_glow")
        parts.append(bc.part("scale", bc.mesh_ellipsoid(0.16, 0.03, 0.22), main, parent))
        parts.append(bc.part("ridge", bc.mesh_ellipsoid(0.03, 0.035, 0.18), light, parent, loc=(0, -0.02, 0.01)))
        parts.append(bc.part("scale2", bc.mesh_ellipsoid(0.11, 0.025, 0.15), light, parent, loc=(0.12, 0.03, -0.10),
                             rot=(0, 0.5, 0)))
    elif name == "troll_hide":
        parts.append(bc.part("hide", mesh_box(0.40, 0.04, 0.30), "troll_hide", parent, rot=(0, 0.15, 0)))
        for k in range(5):
            parts.append(bc.spike("fur", (-0.16 + k * 0.08, -0.03, 0.12), (-0.18 + k * 0.08, -0.03, 0.26), 0.035,
                                  "troll_fur", parent, r_tip=0.01))
        parts.append(bc.part("fold", mesh_box(0.40, 0.05, 0.06), "troll_fur", parent, loc=(0, -0.02, -0.12)))
    elif name == "dragon_fang":
        # A curved tooth, pale and cold, with frost still on the root.
        pts = [(-0.06, 0, -0.26), (-0.02, 0, -0.04), (0.06, 0, 0.16), (0.16, 0, 0.28)]
        for i in range(3):
            parts.append(bc.spike("fang", pts[i], pts[i + 1], 0.085 - i * 0.026, "fang_ice" if i else "fang_root",
                                  parent, r_tip=0.06 - i * 0.026))
        for k in range(3):
            parts.append(bc.part("rime", bc.mesh_ellipsoid(0.035, 0.03, 0.03), "fang_glow", parent,
                                 loc=(-0.07 + k * 0.03, -0.02, -0.22 + k * 0.05)))
    elif name == "rotten_flesh":
        # A lump of something that was meat, with a rib still in it.
        parts.append(bc.part("meat", bc.mesh_ellipsoid(0.18, 0.10, 0.13), "rot_meat", parent))
        parts.append(bc.part("fat", bc.mesh_ellipsoid(0.11, 0.06, 0.07), "rot_meat_lt", parent,
                             loc=(-0.04, -0.05, 0.04)))
        parts.append(bc.part("mould", bc.mesh_ellipsoid(0.06, 0.04, 0.05), "rot_mould", parent,
                             loc=(0.07, -0.05, -0.03)))
        parts.append(bc.spike("rib", (0.10, -0.02, -0.02), (0.22, -0.02, 0.10), 0.022, "bone_pale", parent,
                              r_tip=0.008))
    elif name == "tarnished_ring":
        parts.append(bc.part("band", bc.mesh_torus(0.15, 0.035), "tarnish", parent,
                             rot=(math.radians(64), 0, math.radians(14))))
        parts.append(bc.part("shoulder", bc.mesh_ellipsoid(0.06, 0.04, 0.05), "tarnish_lt", parent,
                             loc=(0, -0.03, 0.14)))
        parts.append(bc.part("stone", mesh_gem(0.055, 0.04, 0.05), "grave_stone_gem", parent, loc=(0, -0.05, 0.18)))
    elif name == "mourning_locket":
        parts.append(bc.part("case", bc.mesh_ellipsoid(0.13, 0.05, 0.16), "tarnish_lt", parent, loc=(0, 0, -0.04)))
        parts.append(bc.part("lid", bc.mesh_ellipsoid(0.11, 0.04, 0.13), "tarnish", parent,
                             loc=(0.05, -0.05, 0.02), rot=(0, math.radians(26), 0)))
        parts.append(bc.part("hair", bc.mesh_ellipsoid(0.06, 0.02, 0.07), "locket_hair", parent, loc=(0, -0.04, -0.04)))
        parts.append(bc.part("loop", bc.mesh_torus(0.035, 0.014), "tarnish", parent,
                             loc=(0, 0, 0.14), rot=(math.radians(90), 0, 0)))
        for k in range(5):
            parts.append(bc.part("link", bc.mesh_torus(0.028, 0.011), "tarnish", parent,
                                 loc=(-0.05 - k * 0.045, 0, 0.19 + k * 0.03),
                                 rot=(math.radians(90), 0, math.radians(40))))
    elif name == "grave_candle":
        parts.append(bc.part("stub", bc.mesh_capsule(0.075, 0.08, 0.26), "wax", parent, loc=(0, 0, 0.12)))
        for k in range(3):
            parts.append(bc.part("drip", bc.mesh_ellipsoid(0.028, 0.028, 0.06), "wax_lt", parent,
                                 loc=(-0.06 + k * 0.06, -0.06, -0.02 - k * 0.02)))
        parts.append(bc.part("pool", bc.mesh_ellipsoid(0.13, 0.11, 0.025), "wax_lt", parent, loc=(0, 0, -0.13)))
        parts.append(bc.spike("wick", (0, 0, 0.12), (0, 0, 0.19), 0.012, "ink", parent, r_tip=0.005))
        parts.append(bc.part("flame", mesh_gem(0.045, 0.04, 0.075), "candle_flame", parent, loc=(0, 0, 0.25)))
    elif name in ("lantern", "lantern_unlit"):
        lit = name == "lantern"
        # A frame of iron with horn panes, a ring on top and a foot under it.
        parts.append(bc.part("pane", mesh_box(0.16, 0.10, 0.22), "horn_pane" if lit else "horn_pane_dk",
                             parent, loc=(0, 0, 0.02)))
        if lit:
            parts.append(bc.part("flame", mesh_gem(0.045, 0.035, 0.085), "lamp_flame", parent, loc=(0, -0.03, 0.0)))
        for sx in (-1, 1):
            parts.append(bc.part("post", mesh_box(0.022, 0.09, 0.26), "lamp_iron", parent, loc=(sx * 0.085, 0, 0.02)))
        parts.append(bc.part("cap", mesh_box(0.20, 0.12, 0.05), "lamp_iron", parent, loc=(0, 0, 0.17)))
        parts.append(bc.part("vent", mesh_box(0.12, 0.09, 0.04), "lamp_iron_lt", parent, loc=(0, 0, 0.22)))
        parts.append(bc.part("base", mesh_box(0.21, 0.13, 0.05), "lamp_iron", parent, loc=(0, 0, -0.13)))
        parts.append(bc.part("ring", bc.mesh_torus(0.05, 0.016), "lamp_iron_lt", parent,
                             loc=(0, 0, 0.29), rot=(math.radians(90), 0, 0)))
        parts.append(bc.part("handle", bc.mesh_torus(0.09, 0.014), "lamp_iron", parent,
                             loc=(0, 0, 0.26), rot=(0, math.radians(90), 0)))
    elif name == "tinderbox":
        # A tin with a flint and a steel striker on it, and one spark.
        parts.append(bc.part("tin", mesh_box(0.30, 0.20, 0.12), "lamp_iron", parent, loc=(0, 0, -0.06)))
        parts.append(bc.part("lid", mesh_box(0.31, 0.21, 0.04), "lamp_iron_lt", parent, loc=(0, -0.01, 0.01)))
        parts.append(bc.part("flint", mesh_gem(0.07, 0.05, 0.05), "flint_grey", parent, loc=(-0.07, -0.04, 0.07)))
        parts.append(bc.spike("striker", (0.02, -0.03, 0.05), (0.16, -0.03, 0.12), 0.022, "lamp_iron_lt",
                              parent, r_tip=0.012))
        parts.append(bc.part("spark", mesh_gem(0.03, 0.025, 0.04), "lamp_flame", parent, loc=(-0.01, -0.06, 0.13)))
        parts.append(bc.part("cloth", mesh_box(0.12, 0.08, 0.03), "char_cloth", parent, loc=(0.07, 0.05, 0.04)))
    elif name == "demon_horn":
        pts = [(-0.18, 0, -0.20), (-0.14, 0, 0.04), (0.0, 0, 0.20), (0.18, 0, 0.18)]
        for i in range(3):
            parts.append(bc.spike("horn", pts[i], pts[i + 1], 0.09 - i * 0.025, "horn_black" if i < 2 else "horn_tip",
                                  parent, r_tip=0.065 - i * 0.025))
    return parts


TROPHY_ICONS = ["spider_silk", "lizard_scale", "troll_hide", "wyvern_scale", "demon_horn", "dragon_fang",
                "rotten_flesh", "tarnished_ring", "mourning_locket", "grave_candle",
                "lantern", "lantern_unlit", "tinderbox"]


def build_drowned_boots(parent):
    """The one pair of boots in the barrow's oldest chest: bog-black leather
    under drowned gold, weed still caught in the buckles. Laid out in the
    picture plane the way the herbs and the fish are -- side on, one boot
    behind the other -- because from the icon camera a boot built standing up
    is a cylinder seen down the leg."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    #        x      y     dark              gold
    for k, (x, y, dark) in enumerate(((-0.10, 0.06, "bog_leather_lt"), (0.06, -0.04, "bog_leather"))):
        top = 0.40
        add("shaft", bc.mesh_ellipsoid(0.075, 0.05, 0.15), dark, parent, loc=(x, y, top - 0.14))
        add("cuff", bc.mesh_ellipsoid(0.088, 0.055, 0.03), "drowned_gold", parent, loc=(x, y, top))
        add("ankle", bc.mesh_ellipsoid(0.07, 0.05, 0.05), dark, parent, loc=(x, y, top - 0.27))
        add("foot", bc.mesh_ellipsoid(0.125, 0.05, 0.048), dark, parent, loc=(x + 0.06, y, top - 0.33))
        add("toe", bc.mesh_ellipsoid(0.045, 0.045, 0.042), "drowned_gold", parent, loc=(x + 0.16, y, top - 0.33))
        add("sole", bc.mesh_ellipsoid(0.135, 0.05, 0.016), "bog_sole", parent, loc=(x + 0.06, y, top - 0.37))
        add("heel", bc.mesh_ellipsoid(0.035, 0.045, 0.03), "bog_sole", parent, loc=(x - 0.05, y, top - 0.36))
        for b in range(2):
            add("strap", mesh_box(0.16, 0.055, 0.022), "drowned_gold", parent, loc=(x, y - 0.005, top - 0.10 - b * 0.09))
        parts.append(bc.spike("weed", (x - 0.06, y - 0.02, top - 0.18), (x - 0.13, y - 0.02, top - 0.02),
                              0.014, "bog_weed", parent, r_tip=0.004))
        add("drip", bc.mesh_ellipsoid(0.018, 0.018, 0.022), "bog_glow", parent, loc=(x + 0.17, y - 0.03, top - 0.38))
    return parts


bc.PALETTE.update({
    "hide_tan": (0.64, 0.46, 0.28), "hide_dk": (0.44, 0.30, 0.18), "hide_sole": (0.30, 0.22, 0.15),
    "hide_lace": (0.82, 0.74, 0.56),
    "rune_seal": (0.26, 0.44, 0.90), "rune_glow": (0.56, 0.78, 1.00),
})


def build_hide_boots(parent):
    """Soft boar-hide boots, laid out the way the drowned king's are -- side
    on, one behind the other -- in tan leather with a darker turned-down cuff,
    a lace criss-crossed up the front and a plain sole."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    for x, y, main, dark in ((-0.10, 0.06, "hide_tan", "hide_dk"), (0.06, -0.04, "hide_dk", "hide_tan")):
        top = 0.36
        add("shaft", bc.mesh_ellipsoid(0.075, 0.05, 0.13), main, parent, loc=(x, y, top - 0.13))
        add("cuff", bc.mesh_ellipsoid(0.09, 0.056, 0.035), dark, parent, loc=(x, y, top))
        add("ankle", bc.mesh_ellipsoid(0.07, 0.05, 0.05), main, parent, loc=(x, y, top - 0.25))
        add("foot", bc.mesh_ellipsoid(0.125, 0.05, 0.048), main, parent, loc=(x + 0.06, y, top - 0.31))
        add("toe", bc.mesh_ellipsoid(0.05, 0.045, 0.04), dark, parent, loc=(x + 0.16, y, top - 0.31))
        add("sole", bc.mesh_ellipsoid(0.135, 0.05, 0.016), "hide_sole", parent, loc=(x + 0.06, y, top - 0.35))
        add("heel", bc.mesh_ellipsoid(0.035, 0.045, 0.03), "hide_sole", parent, loc=(x - 0.05, y, top - 0.34))
        for b in range(3):
            add("lace", mesh_box(0.10, 0.055, 0.014), "hide_lace", parent,
                loc=(x + 0.01, y - 0.005, top - 0.08 - b * 0.06),
                rot=(0, math.radians(18 if b % 2 else -18), 0))
    return parts


bc.PALETTE.update({
    "tome_cover": (0.28, 0.18, 0.44), "tome_cover_dk": (0.18, 0.11, 0.30),
    "tome_page": (0.90, 0.84, 0.68), "tome_clasp": (0.80, 0.62, 0.24),
})


def build_spell_tome(parent):
    """A closed book in violet leather with a brass clasp and a lit rune on
    the cover: an ancient spell, bound. Seen a little from above and to the
    side, so the pages show along one edge."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("cover", mesh_box(0.28, 0.09, 0.22), "tome_cover", parent, loc=(0, 0, 0))
    add("pages", mesh_box(0.25, 0.07, 0.19), "tome_page", parent, loc=(0.02, 0, 0))
    add("spine", mesh_box(0.03, 0.10, 0.23), "tome_cover_dk", parent, loc=(-0.14, 0, 0))
    add("clasp", mesh_box(0.05, 0.03, 0.06), "tome_clasp", parent, loc=(0.135, -0.04, 0))
    for a in (45, 135, 225, 315):
        r = math.radians(a)
        parts.append(bc.spike("stroke", (0.0 + math.cos(r) * 0.015, -0.05, math.sin(r) * 0.015),
                              (0.0 + math.cos(r) * 0.07, -0.05, math.sin(r) * 0.07),
                              0.012, "rune_glow", parent, r_tip=0.012))
    add("eye", bc.mesh_ellipsoid(0.02, 0.018, 0.02), "rune_glow", parent, loc=(0, -0.052, 0))
    return parts


def build_enchant_scroll(parent):
    """A recipe scroll with a rune on it rather than lines of writing, and a
    blue seal: a charm's page, told from a brew's at a glance."""
    parts = []
    parts.append(bc.part("sheet", mesh_box(0.30, 0.02, 0.24), "parchment", parent, loc=(0, 0, 0)))
    for z in (0.13, -0.13):
        parts.append(bc.part("roll", bc.mesh_capsule(0.035, 0.035, 0.34), "parchment_dk", parent,
                             loc=(0.17, 0, z), rot=(0, math.radians(90), 0)))
        for x in (-0.19, 0.19):
            parts.append(bc.part("knob", bc.mesh_ellipsoid(0.02, 0.02, 0.03), "rod_wood", parent, loc=(x, 0, z)))
    # The rune: four strokes from a centre, and the centre itself.
    for a in (45, 135, 225, 315):
        r = math.radians(a)
        parts.append(bc.spike("stroke", (-0.03 + math.cos(r) * 0.02, -0.012, math.sin(r) * 0.02),
                              (-0.03 + math.cos(r) * 0.085, -0.012, math.sin(r) * 0.085),
                              0.013, "rune_glow", parent, r_tip=0.013))
    parts.append(bc.part("eye", bc.mesh_ellipsoid(0.022, 0.02, 0.022), "rune_seal", parent, loc=(-0.03, -0.014, 0)))
    parts.append(bc.part("seal", bc.mesh_ellipsoid(0.045, 0.03, 0.045), "rune_seal", parent, loc=(0.10, -0.02, -0.08)))
    return parts


HERB_ICONS = ["marigold", "brookmint", "nettle", "bogbean", "mountain_sage", "glowcap",
              "emberbloom", "moonpetal", "starlily"]


# --- the larder ------------------------------------------------------------------------------------
# What a farm gives and what a fire makes of it. Each is one readable shape at
# thirty pixels: a bowl is a bowl, a pie is a pie, a skewer is three lumps on a
# stick. Cooked things are browner and glossier than raw ones, which is the
# whole of the difference a player needs to see in a bag.

bc.PALETTE.update({
    "crock":      (0.60, 0.42, 0.32), "crock_dk":  (0.42, 0.28, 0.22),
    "broth":      (0.72, 0.44, 0.20), "broth_lt":  (0.86, 0.60, 0.28),
    "crust":      (0.84, 0.66, 0.36), "crust_dk":  (0.66, 0.48, 0.24),
    "meat_raw":   (0.78, 0.32, 0.32), "meat_done": (0.62, 0.38, 0.20),
    "greens":     (0.36, 0.58, 0.28), "greens_lt": (0.52, 0.74, 0.38),
    "milk_white": (0.96, 0.95, 0.90), "pail":      (0.52, 0.40, 0.28),
    "shell":      (0.94, 0.90, 0.80), "shell_dk":  (0.80, 0.74, 0.62),
    "wool_white": (0.93, 0.91, 0.86), "wool_dk":   (0.78, 0.75, 0.70),
    "tea":        (0.72, 0.82, 0.62), "mug":       (0.56, 0.52, 0.50),
    "skewer":     (0.66, 0.50, 0.30), "frog_leg":  (0.74, 0.62, 0.42),
    "honey":      (0.88, 0.62, 0.18),
})


def _bowl(parts, parent, fill, rim="crock"):
    """A crock with something in it, seen from a little above."""
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("bowl", bc.mesh_frustum(0.20, 0.30, 0.20, squash_y=0.72), "crock", parent, loc=(0, 0, -0.04))
    add("foot", bc.mesh_torus(0.12, 0.03), "crock_dk", parent, loc=(0, 0, -0.23))
    add("rim", bc.mesh_torus(0.30, 0.035), rim, parent, loc=(0, 0, 0.16))
    add("fill", bc.mesh_ellipsoid(0.27, 0.19, 0.05), fill, parent, loc=(0, 0, 0.14))


def build_dish(name, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    if name in ("hearty_stew", "fishermans_broth"):
        # A broth is paler and thinner than a stew: two bowls have to be told
        # apart at thirty pixels by their colour, since the bowl is the same.
        _bowl(parts, parent, "broth" if name == "hearty_stew" else "tea")
        # Lumps in it, so it reads as a stew and not as soup.
        for k, (x, y) in enumerate(((-0.10, -0.04), (0.08, 0.02), (0.0, 0.07))):
            add("lump_%d" % k, bc.mesh_ellipsoid(0.055, 0.04, 0.035),
                "meat_done" if name == "hearty_stew" else "broth_lt", parent, loc=(x, y, 0.16))
        add("herb", bc.mesh_ellipsoid(0.05, 0.035, 0.02), "greens_lt", parent, loc=(0.12, -0.06, 0.17))
    elif name == "hunters_skewers":
        add("stick", bc.mesh_capsule(0.018, 0.018, 0.62), "skewer", parent, loc=(0, 0, 0.30),
            rot=(0, math.radians(28), 0))
        for k, z in enumerate((0.16, -0.02, -0.20)):
            add("cut_%d" % k, bc.mesh_ellipsoid(0.095, 0.085, 0.075), "meat_done", parent,
                loc=(-z * 0.52, 0, z))
            add("char_%d" % k, bc.mesh_ellipsoid(0.055, 0.05, 0.02), "crust_dk", parent,
                loc=(-z * 0.52, -0.07, z + 0.05))
    elif name == "travellers_pie":
        add("dish", bc.mesh_frustum(0.26, 0.30, 0.12, squash_y=0.72), "crock_dk", parent, loc=(0, 0, -0.08))
        add("crust", bc.mesh_ellipsoid(0.29, 0.21, 0.10), "crust", parent, loc=(0, 0, 0.06))
        add("crimp", bc.mesh_torus(0.29, 0.03), "crust_dk", parent, loc=(0, 0, 0.02))
        for k, a in enumerate((20, 90, 160)):
            add("vent_%d" % k, mesh_box(0.10, 0.02, 0.012), "crock_dk", parent,
                loc=(math.cos(math.radians(a)) * 0.06, math.sin(math.radians(a)) * 0.04, 0.14),
                rot=(0, 0, math.radians(a)))
    elif name == "moonpetal_tea":
        add("mug", bc.mesh_capsule(0.15, 0.16, 0.26), "mug", parent, loc=(0, 0, 0.12))
        add("tea", bc.mesh_ellipsoid(0.135, 0.135, 0.04), "tea", parent, loc=(0, 0, 0.13))
        add("handle", bc.mesh_torus(0.09, 0.025), "mug", parent, loc=(0.20, 0, -0.02),
            rot=(math.radians(90), 0, 0))
        add("steam", bc.mesh_ellipsoid(0.04, 0.03, 0.09), "tea", parent, loc=(0.02, -0.02, 0.24))
    elif name == "honeyed_oats":
        _bowl(parts, parent, "crust")
        add("honey", bc.mesh_ellipsoid(0.10, 0.07, 0.03), "honey", parent, loc=(-0.02, -0.02, 0.17))
        add("drip", bc.mesh_capsule(0.02, 0.015, 0.09), "honey", parent, loc=(0.16, -0.04, 0.16))
    elif name == "frog_legs":
        for side in (-1, 1):
            add("thigh_%d" % side, bc.mesh_ellipsoid(0.085, 0.07, 0.13), "frog_leg", parent,
                loc=(side * 0.11, 0, 0.02), rot=(0, math.radians(side * 14), 0))
            parts.append(bc.spike("shin_%d" % side, (side * 0.13, 0, -0.08), (side * 0.22, 0, -0.26),
                                  0.035, "frog_leg", parent, r_tip=0.02))
            add("char_%d" % side, bc.mesh_ellipsoid(0.05, 0.04, 0.02), "crust_dk", parent,
                loc=(side * 0.11, -0.06, 0.10))
    elif name == "egg":
        add("egg", bc.mesh_ellipsoid(0.15, 0.15, 0.20), "shell", parent, loc=(0, 0, 0.0))
        add("shade", bc.mesh_ellipsoid(0.10, 0.10, 0.13), "shell_dk", parent, loc=(0.05, 0.05, -0.04))
    elif name == "milk":
        add("pail", bc.mesh_frustum(0.17, 0.22, 0.34, squash_y=0.82), "pail", parent, loc=(0, 0, 0.10))
        add("milk", bc.mesh_ellipsoid(0.20, 0.16, 0.03), "milk_white", parent, loc=(0, 0, 0.09))
        add("band", bc.mesh_torus(0.20, 0.022), "crock_dk", parent, loc=(0, 0, -0.04))
        add("handle", bc.mesh_torus(0.19, 0.018), "crock_dk", parent, loc=(0, 0, 0.16), rot=(math.radians(90), 0, 0))
    elif name == "wool":
        for k, (x, y, z, r) in enumerate(((0, 0, 0, 0.24), (-0.12, -0.05, 0.10, 0.15), (0.13, 0.03, 0.08, 0.16),
                                          (0.02, -0.08, -0.14, 0.14), (-0.10, 0.06, -0.12, 0.13))):
            add("puff_%d" % k, bc.mesh_ellipsoid(r, r * 0.92, r * 0.9),
                "wool_white" if k % 2 == 0 else "wool_dk", parent, loc=(x, y, z))
    return parts


FOOD_ICONS = ["hearty_stew", "fishermans_broth", "hunters_skewers", "travellers_pie", "moonpetal_tea",
              "honeyed_oats", "frog_legs", "egg", "milk", "wool"]


def food_icons(only=None):
    count = 0
    for name in FOOD_ICONS:
        if only and name not in only:
            continue
        render_icon(name, lambda t, p, n=name: build_dish(n, p), "wood", 18, 0, 0.9)
        count += 1
    print("icons %d food" % count)


def brewing_icons(only=None):
    count = 0
    for name in HERB_ICONS:
        if only and name not in only:
            continue
        render_icon(name, lambda t, p, n=name: build_herb(n, p), "wood", 0, 0, 0.9)
        count += 1
    for name in ["vial"] + list(POTIONS):
        if only and name not in only:
            continue
        render_icon(name, lambda t, p, n=name: build_potion(n, p), "wood", 0, 0, 0.86)
        count += 1
    for name in TROPHY_ICONS:
        if only and name not in only:
            continue
        render_icon(name, lambda t, p, n=name: build_trophy(n, p), "wood", 0, 0, 0.88)
        count += 1
    if not only or "drowned_king_boots" in only:
        render_icon("drowned_king_boots", lambda t, p: build_drowned_boots(p), "wood", 0, 0, 0.9)
        count += 1
    if not only or "recipe_scroll" in only:
        render_icon("recipe_scroll", lambda t, p: build_recipe_scroll(p), "wood", 0, 0, 0.92)
        count += 1
    if not only or "enchant_scroll" in only:
        render_icon("enchant_scroll", lambda t, p: build_enchant_scroll(p), "wood", 0, 0, 0.92)
        count += 1
    if not only or "hide_boots" in only:
        render_icon("hide_boots", lambda t, p: build_hide_boots(p), "wood", 0, 0, 0.9)
        count += 1
    if not only or "spell_tome" in only:
        render_icon("spell_tome", lambda t, p: build_spell_tome(p), "wood", 20, -30, 0.9)
        count += 1
    print("icons %d brewing" % count)


# --- the sets that are not metal (icons) ----------------------------------------------------------
# The ranger's hides and the mage's robes, a hood, a jerkin and chaps or a hat, a
# robe and a skirt for every tier, in the colour data/tiers.json gives the set
# at that tier -- the same colour the game paints the worn layer with, so what
# is in the bag is what goes on the character. And what they are made of: the
# hides something has to be killed for, flax, a bolt of cloth, and the dyes.

def _set_colours():
    import json
    with open(os.path.join(bc.ROOT, "data", "tiers.json"), encoding="utf-8") as f:
        sets = json.load(f).get("sets", {})
    out = {}
    for kind, block in sets.items():
        for tier, entry in block.get("tiers", {}).items():
            c = entry.get("colour", [160, 160, 160])
            out[(kind, tier)] = (c[0] / 255.0, c[1] / 255.0, c[2] / 255.0)
    return out


def _use_colour(rgb):
    """The three shades a set piece is modelled in, from its one colour."""
    bc.PALETTE["set_main"] = rgb
    bc.PALETTE["set_dark"] = tuple(v * 0.62 for v in rgb)
    bc.PALETTE["set_light"] = tuple(min(1.0, v * 1.22 + 0.06) for v in rgb)
    bc.PALETTE["set_fur"] = (0.88, 0.84, 0.76)
    bc.PALETTE["set_fur_dk"] = (0.62, 0.57, 0.49)


def build_coif(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("hood", bc.mesh_ellipsoid(0.27, 0.27, 0.26), "set_main", parent, loc=(0, 0.02, 0.04))
    add("drape", bc.mesh_ellipsoid(0.24, 0.16, 0.20), "set_dark", parent, loc=(0, 0.12, -0.16))
    add("brow_fur", bc.mesh_torus(0.235, 0.055), "set_fur", parent, loc=(0, -0.04, -0.06),
        rot=(math.radians(18), 0, 0))
    add("face", bc.mesh_ellipsoid(0.16, 0.05, 0.13), "set_dark", parent, loc=(0, -0.23, -0.10))
    parts.append(bc.spike("peak", (0, 0.10, 0.24), (0, 0.30, 0.34), 0.09, "set_main", parent, r_tip=0.02))
    return parts


def build_jerkin(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("torso", bc.mesh_capsule(0.25, 0.20, 0.27, squash_y=0.7), "set_main", parent, loc=(0, 0, 0.16))
    add("collar", bc.mesh_torus(0.15, 0.055), "set_fur", parent, loc=(0, 0, 0.27))
    add("belt", bc.mesh_ellipsoid(0.235, 0.165, 0.04), "set_dark", parent, loc=(0, 0, -0.19))
    add("buckle", mesh_box(0.06, 0.03, 0.05), "set_light", parent, loc=(0, -0.16, -0.19))
    for z in (0.12, 0.03, -0.06):
        add("lace", mesh_box(0.10, 0.02, 0.018), "set_light", parent, loc=(0, -0.155, z))
    for side in (-1, 1):
        add("shoulder_fur", bc.mesh_ellipsoid(0.12, 0.12, 0.07), "set_fur_dk", parent, loc=(side * 0.26, 0, 0.19))
        add("strap", bc.mesh_capsule(0.075, 0.065, 0.12), "set_dark", parent, loc=(side * 0.30, 0, 0.10))
    return parts


def build_chaps(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("waist", bc.mesh_ellipsoid(0.24, 0.14, 0.06), "set_dark", parent, loc=(0, 0, 0.30))
    for side in (-1, 1):
        add("thigh", bc.mesh_capsule(0.10, 0.09, 0.20), "set_main", parent, loc=(side * 0.11, 0, 0.26))
        add("wrap", bc.mesh_capsule(0.088, 0.078, 0.20), "set_dark", parent, loc=(side * 0.11, 0, -0.04))
        add("cuff", bc.mesh_torus(0.088, 0.034), "set_fur", parent, loc=(side * 0.11, 0, -0.02))
        add("boot", bc.mesh_ellipsoid(0.09, 0.13, 0.06), "set_main", parent, loc=(side * 0.11, -0.04, -0.30))
    return parts


def build_hat(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("brim", bc.mesh_ellipsoid(0.36, 0.36, 0.035), "set_dark", parent, loc=(0, 0, -0.16))
    parts.append(bc.spike("cone", (0, 0, -0.16), (0.10, 0.06, 0.40), 0.23, "set_main", parent, r_tip=0.025))
    add("band", bc.mesh_torus(0.205, 0.035), "set_light", parent, loc=(0.005, 0.003, -0.10))
    add("buckle", mesh_box(0.07, 0.03, 0.06), "platinum_accent", parent, loc=(0, -0.21, -0.10))
    return parts


def build_robe_top(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("torso", bc.mesh_capsule(0.24, 0.26, 0.40, squash_y=0.7), "set_main", parent, loc=(0, 0, 0.22))
    add("mantle", bc.mesh_torus(0.17, 0.065), "set_light", parent, loc=(0, 0, 0.26))
    add("sash", bc.mesh_ellipsoid(0.255, 0.18, 0.04), "set_dark", parent, loc=(0, 0, -0.04))
    add("trim", mesh_box(0.05, 0.02, 0.34), "set_light", parent, loc=(0, -0.185, 0.02))
    for side in (-1, 1):
        parts.append(bc.spike("sleeve", (side * 0.22, 0, 0.20), (side * 0.40, 0, -0.10), 0.075, "set_main",
                              parent, r_tip=0.12))
        add("cuff", bc.mesh_torus(0.115, 0.022), "set_light", parent, loc=(side * 0.405, 0, -0.105),
            rot=(0, math.radians(side * 32), 0))
    return parts


def build_skirt(tier, parent):
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("waist", bc.mesh_ellipsoid(0.20, 0.14, 0.045), "set_dark", parent, loc=(0, 0, 0.30))
    add("skirt", bc.mesh_frustum(0.19, 0.34, 0.58, squash_y=0.7), "set_main", parent, loc=(0, 0, 0.30))
    add("band", bc.mesh_torus(0.27, 0.02), "set_dark", parent, loc=(0, 0, 0.0))
    add("hem", bc.mesh_torus(0.335, 0.03), "set_light", parent, loc=(0, 0, -0.27))
    for g in parts[-2:]:
        g.scale = (1.0, 0.7, 1.0)
    return parts


def build_dye(tier, parent):
    """A stoppered pot of it, and a drip down the side so the colour shows."""
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("pot", bc.mesh_ellipsoid(0.17, 0.15, 0.15), "pot_clay", parent, loc=(0, 0, -0.06))
    add("neck", bc.mesh_capsule(0.10, 0.11, 0.07), "pot_clay_dk", parent, loc=(0, 0, 0.12))
    add("surface", bc.mesh_ellipsoid(0.095, 0.085, 0.03), "set_main", parent, loc=(0, 0, 0.135))
    add("label", bc.mesh_ellipsoid(0.10, 0.03, 0.07), "set_main", parent, loc=(0, -0.135, -0.06))
    parts.append(bc.spike("drip", (0.07, -0.10, 0.10), (0.10, -0.13, -0.12), 0.026, "set_light", parent, r_tip=0.012))
    return parts


PELTS = {
    #                   hide                  fur                   how shaggy
    "wolf_pelt":      ((0.50, 0.49, 0.48), (0.72, 0.71, 0.70), 1.0),
    "bear_hide":      ((0.38, 0.25, 0.16), (0.27, 0.18, 0.12), 1.2),
    "demon_hide":     ((0.52, 0.14, 0.12), (0.20, 0.08, 0.08), 0.0),
    "greatwolf_pelt": ((0.80, 0.83, 0.90), (0.97, 0.98, 1.00), 1.2),
    "dire_bear_hide": ((0.26, 0.24, 0.27), (0.40, 0.38, 0.42), 1.4),
    "dread_hide":     ((0.42, 0.26, 0.62), (0.70, 0.52, 0.92), 0.6),
    "dragonhide":     ((0.70, 0.80, 0.88), (0.93, 0.97, 1.00), 0.0),
}


def build_pelt(name, parent):
    hide, fur, shag = PELTS[name]
    bc.PALETTE["pelt_hide"] = hide
    bc.PALETTE["pelt_fur"] = fur
    parts = [bc.part("hide", mesh_box(0.44, 0.04, 0.32), "pelt_hide", parent, rot=(0, 0.12, 0)),
             bc.part("fold", mesh_box(0.44, 0.055, 0.07), "pelt_fur", parent, loc=(0, -0.02, -0.13))]
    for side in (-1, 1):
        # The legs of it, splayed at the corners the way a pelt is pegged out.
        parts.append(bc.part("leg", mesh_box(0.10, 0.04, 0.10), "pelt_hide", parent,
                             loc=(side * 0.24, 0, 0.17), rot=(0, side * 0.5, 0)))
    if shag > 0.0:
        for k in range(6):
            parts.append(bc.spike("fur", (-0.19 + k * 0.076, -0.03, 0.10), (-0.21 + k * 0.076, -0.03, 0.10 + 0.13 * shag),
                                  0.036, "pelt_fur", parent, r_tip=0.01))
    else:
        # Scale or bare skin: rows of plates instead of a coat.
        for row in range(2):
            for k in range(4):
                parts.append(bc.part("scale", bc.mesh_ellipsoid(0.05, 0.02, 0.045), "pelt_fur", parent,
                                     loc=(-0.15 + k * 0.10 + row * 0.05, -0.03, 0.06 - row * 0.10)))
    return parts


BAGS = {
    #                 body                  flap                  trim / fur            how big, fur on the flap
    "bag_satchel":   ((0.56, 0.38, 0.24), (0.44, 0.29, 0.18), (0.80, 0.68, 0.44), 0.84, False),
    "bag_pack":      ((0.50, 0.49, 0.48), (0.36, 0.35, 0.35), (0.74, 0.73, 0.72), 0.92, True),
    "bag_rucksack":  ((0.38, 0.25, 0.16), (0.25, 0.17, 0.11), (0.52, 0.40, 0.30), 1.0, True),
    "bag_haversack": ((0.84, 0.87, 0.93), (0.60, 0.66, 0.78), (0.62, 0.50, 0.92), 1.06, True),
}


def build_bag(name, parent):
    """A pack stood on its base, flap to the front: the same cut four times, in
    what each is made of, and a little bigger every time."""
    body, flap, trim, size, furred = BAGS[name]
    bc.PALETTE["bag_body"] = body
    bc.PALETTE["bag_flap"] = flap
    bc.PALETTE["bag_trim"] = trim
    s = size
    parts = []
    add = lambda *a, **k: parts.append(bc.part(*a, **k))
    add("sack", bc.mesh_capsule(0.22 * s, 0.25 * s, 0.30 * s, squash_y=0.72), "bag_body", parent, loc=(0, 0, 0.10 * s))
    add("base", bc.mesh_ellipsoid(0.255 * s, 0.185 * s, 0.07 * s), "bag_flap", parent, loc=(0, 0, -0.27 * s))
    add("flap", bc.mesh_ellipsoid(0.235 * s, 0.17 * s, 0.13 * s), "bag_flap", parent, loc=(0, -0.035 * s, 0.17 * s))
    add("strap", mesh_box(0.05 * s, 0.02, 0.26 * s), "bag_trim", parent, loc=(0, -0.185 * s, 0.02 * s))
    add("buckle", mesh_box(0.085 * s, 0.03, 0.06 * s), "platinum_accent", parent, loc=(0, -0.195 * s, -0.06 * s))
    add("roll", bc.mesh_capsule(0.07 * s, 0.07 * s, 0.40 * s), "bag_trim", parent, loc=(0.20 * s, 0, 0.34 * s),
        rot=(0, math.radians(90), 0))
    for side in (-1, 1):
        add("pocket", bc.mesh_ellipsoid(0.075 * s, 0.10 * s, 0.11 * s), "bag_body", parent,
            loc=(side * 0.255 * s, -0.02, -0.10 * s))
        add("pocket_flap", bc.mesh_ellipsoid(0.08 * s, 0.105 * s, 0.045 * s), "bag_flap", parent,
            loc=(side * 0.255 * s, -0.025, -0.02 * s))
        parts.append(bc.spike("shoulder", (side * 0.13 * s, 0.10 * s, 0.30 * s), (side * 0.20 * s, 0.13 * s, -0.22 * s),
                              0.03 * s, "bag_trim", parent, r_tip=0.03 * s))
    if furred:
        for k in range(5):
            x = (-0.16 + k * 0.08) * s
            parts.append(bc.spike("fur", (x, -0.15 * s, 0.13 * s), (x * 1.08, -0.19 * s, 0.03 * s), 0.034 * s,
                                  "bag_trim", parent, r_tip=0.008))
    return parts


def build_flax(tier, parent):
    parts = []
    for k, x in enumerate((-0.12, -0.04, 0.05, 0.13)):
        top = (x + (k - 1.5) * 0.04, 0, 0.30 - abs(k - 1.5) * 0.04)
        _sprig(parent, parts, (x * 0.4, 0, -0.30), top, colour="flax_stalk", r=0.012)
        parts.append(bc.part("flower", bc.mesh_ellipsoid(0.042, 0.03, 0.042), "flax_flower", parent, loc=top))
    parts.append(bc.part("tie", bc.mesh_torus(0.05, 0.016), "flax_tie", parent, loc=(0, 0, -0.16)))
    return parts


def build_bolt(tier, parent):
    parts = [bc.part("roll", bc.mesh_capsule(0.13, 0.13, 0.40), "cloth_plain", parent, loc=(-0.20, 0, 0.0),
                     rot=(0, math.radians(90), 0)),
             bc.part("core", bc.mesh_ellipsoid(0.03, 0.06, 0.06), "cloth_plain_dk", parent, loc=(0.215, 0, 0.0)),
             bc.part("flap", mesh_box(0.36, 0.20, 0.02), "cloth_plain", parent, loc=(0.0, -0.14, -0.125))]
    for x in (-0.10, 0.06):
        parts.append(bc.part("band", bc.mesh_torus(0.133, 0.012), "cloth_plain_dk", parent, loc=(x, 0, 0),
                             rot=(0, math.radians(90), 0)))
    return parts


bc.PALETTE.update({
    "pot_clay": (0.62, 0.42, 0.30), "pot_clay_dk": (0.44, 0.29, 0.21),
    "flax_stalk": (0.62, 0.64, 0.34), "flax_flower": (0.44, 0.58, 0.94), "flax_tie": (0.50, 0.36, 0.22),
    "cloth_plain": (0.86, 0.82, 0.72), "cloth_plain_dk": (0.64, 0.59, 0.49),
})


def set_icons(only_tiers, only=None):
    import json
    colours = _set_colours()
    with open(os.path.join(bc.ROOT, "data", "tiers.json"), encoding="utf-8") as f:
        sets = json.load(f).get("sets", {})
    count = 0
    jobs = {"hide": (("head", build_coif, 0.88), ("body", build_jerkin, 0.9), ("legs", build_chaps, 0.88)),
            "robe": (("head", build_hat, 0.92), ("body", build_robe_top, 0.92), ("legs", build_skirt, 0.9))}
    for tier in only_tiers:
        for kind, pieces in jobs.items():
            if (kind, tier) not in colours:
                continue
            for piece, builder, fill in pieces:
                name = "%s_%s_%s" % (kind, piece, tier)
                if only and name not in only:
                    continue
                _use_colour(colours[(kind, tier)])
                render_icon(name, builder, tier, 0, 16, fill)
                count += 1
            dye = sets.get(kind, {}).get("tiers", {}).get(tier, {}).get("dye")
            if dye and (not only or dye["id"] in only):
                _use_colour(colours[(kind, tier)])
                render_icon(dye["id"], build_dye, tier, 0, 0, 0.86)
                count += 1
    if len(only_tiers) == len(TIERS) or only:
        for name in PELTS:
            if only and name not in only:
                continue
            render_icon(name, lambda t, p, n=name: build_pelt(n, p), "wood", 0, 0, 0.9)
            count += 1
        for name in BAGS:
            if only and name not in only:
                continue
            render_icon(name, lambda t, p, n=name: build_bag(n, p), "wood", 0, 18, 0.9)
            count += 1
        if not only or "flax" in only:
            render_icon("flax", build_flax, "wood", 0, 0, 0.92)
            count += 1
        if not only or "bolt_cloth" in only:
            render_icon("bolt_cloth", build_bolt, "wood", 0, 20, 0.9)
            count += 1
    print("icons %d sets" % count)


# --- weapon layers on the hero --------------------------------------------------------------

# What the left hand will hold as well as the right: see "dual" in data/tiers.json.
OFFHAND_KINDS = ("dagger",)

def weapon_layers(clip_name, models, out_dir):
    pose_fn, frames, loops = bc.CLIPS[clip_name]
    cols, rows = frames, len(bc.FACINGS)

    bc.clear_scene()
    bc._materials.clear()
    bc._meshes.clear()
    bc.setup_world()
    right, up = bc.camera_basis()

    occluders, own_sword, grips, grips_left = [], [], [], []
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
            # And the armour it was built in. It was left standing in the
            # render -- neither held out nor hidden -- so every weapon sheet
            # had a suit of plain plate drawn into it under the weapon, and a
            # character with nothing on their legs grew steel greaves the
            # moment they picked up a sword. Held out, like the body: what is
            # worn is drawn from its own sheets, over this one.
            # The shield is only hidden: not everybody carries one, and held out
            # it would cut a shield-shaped hole in a blade that crossed it.
            for key in bc.ARMOUR_GROUPS:
                if key == bc.ARM_SHIELD: own_sword += groups.get(key, [])
                else:                    occluders += groups.get(key, [])
            grips.append(joints["grip"])
            grips_left.append(joints["grip_l"])

    for ob in occluders:
        ob.hide_render = False
        ob.is_holdout = True
    for ob in own_sword:
        ob.hide_render = True

    bc.setup_camera(cols, rows)
    bc.setup_render(cols, rows)
    SPEAR_MODE["thrust"] = clip_name == "thrust"
    # A great weapon is in the hands for anything that swings it, and over the
    # shoulder for everything else.
    GREAT_MODE["swing"] = clip_name in ("sweep", "hew", "rush", "crush", "cleave", "backhand", "spin") or clip_name.endswith("_2h")

    for model in models:
        kind, tier = model.split("_", 1) if "_" in model else (model, "wood")
        made = []
        for grip in grips:
            made += WEAPONS[kind](tier, grip)
        raw = os.path.join(RENDER_DIR, "layer_%s_%s.png" % (clip_name, model))
        bc.render_to(raw)
        small = bc.outline(bc.reduce_majority(bc.read_png(raw)))
        bc.write_png(os.path.join(out_dir, "layers", "%s_4_weapon_%s.png" % (clip_name, model)), small)
        for ob in made:
            bpy.data.objects.remove(ob, do_unlink=True)
        # What can be held in the other hand gets a second sheet, built in that
        # hand: layers/<clip>_4_weapon_off_<model>.png, drawn beside the first
        # when there is a pair. A sheet of its own, not a second blade in the
        # first: one dagger is still the usual thing to be holding.
        if kind in OFFHAND_KINDS:
            made = []
            for grip in grips_left:
                made += WEAPONS[kind](tier, grip)
            raw = os.path.join(RENDER_DIR, "layer_%s_off_%s.png" % (clip_name, model))
            bc.render_to(raw)
            small = bc.outline(bc.reduce_majority(bc.read_png(raw)))
            bc.write_png(os.path.join(out_dir, "layers", "%s_4_weapon_off_%s.png" % (clip_name, model)), small)
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
    chosen = option("--models")
    names = option("--names")

    # Each clip draws what is in hand during it: the work clips their tool,
    # every other clip the weapons.
    def models_for(clip):
        # Picking herbs is done bare-handed: nothing in hand, so no layers.
        if clip == "gather":
            return []
        # A spear strikes with its own clip and never plays the swing, and
        # nothing but a spear plays the thrust.
        melee = ("sword", "spear", "dagger", "mace", "greatsword", "greataxe")
        great = ("greatsword", "greataxe")
        kinds = {"chop": ("axe",), "mine": ("pickaxe",), "fish": ("rod",),
                 "attack": ("sword", "bow", "staff"),
                 # A spear strikes with a thrust, and so does a dagger.
                 "thrust": ("spear", "dagger"),
                 # And with a pair of them, every other stab is the left hand's.
                 "offstab": ("dagger",),
                 # The combos and the leap with both hands on the hilt: a great weapon's.
                 "rush_2h": great, "crush_2h": great, "cleave_2h": great, "backhand_2h": great, "spin_2h": great,
                 # The armoury's own strikes: see blender_character.CLIPS.
                 "bash": ("mace",), "sweep": ("greatsword", "greataxe"), "hew": ("greataxe",),
                 "shoot": ("crossbow",), "reload": ("crossbow",), "throw": ("knives",),
                 "flick": ("wand",), "invoke": ("grimoire", "orb"),
                 # The leap is a melee move: a bow or a staff never makes it.
                 "rush": melee,
                 # And so are the combos.
                 "crush": melee, "cleave": melee, "backhand": melee, "spin": melee}.get(
                     clip, ("sword", "spear", "bow", "staff", "dagger", "mace", "greatsword", "greataxe",
                            "crossbow", "knives", "wand", "grimoire", "orb"))
        every = ["rod"] if kinds == ("rod",) else ["%s_%s" % (k, t) for t in tiers for k in kinds]
        if chosen:
            return [m for m in chosen if m.split("_", 1)[0] in kinds]
        return every
    # `armoury` with `icons`: only the nine new weapons' and the elements' staves'.
    if "armoury" in args:
        args.remove("armoury")
        ARMOURY_ONLY["on"] = True
    wanted = set(args) or {"icons", "layers", "sets", "food"}

    os.makedirs(ICON_DIR, exist_ok=True)
    if "icons" in wanted:
        all_icons(tiers)
    if "icons" in wanted or "brewing" in wanted:
        if "icons" in wanted and len(tiers) != len(TIERS) and not names:
            pass
        else:
            brewing_icons(set(names) if names else None)
    if "food" in wanted:
        food_icons(set(names) if names else None)
    if "sets" in wanted:
        set_icons(tiers, set(names) if names else None)
    if "layers" in wanted:
        for clip in clips:
            wanted_models = models_for(clip)
            if wanted_models:
                weapon_layers(clip, wanted_models, bc.OUT_DIR)


if __name__ == "__main__":
    main()
