# =============================================================================
#  blender_character.py - the original player character, modelled, animated
#  and rendered straight into the sheet layout the game already reads.
#
#  Run headless (tools/make_character.ps1 does this):
#      blender --background --python tools/blender_character.py -- [clip ...]
#          [--out DIR]
#
#  The output convention is the one every character in this project uses: a
#  64px square frame, four rows in the order down / left / right / up, one
#  column per frame, split into layers -- shadow, the weapon behind the body,
#  the body, the weapon in front, the head -- so worn equipment and armour
#  tinting keep working.
#
#  The first version of this character was bevelled boxes, and at game size it
#  read as boxes: a crate for a head, planks for limbs. This one is built the
#  way a pixel artist would draw it rather than the way a modeller would --
#
#    * Rounded forms. Every part is an ellipsoid or a tapered capsule, so the
#      silhouette has curves, and the limbs join without seams.
#    * Chibi proportions matched to the CraftPix characters it stands beside:
#      a head about half the height, so a face survives at twenty-five pixels.
#    * Cel shading. A diffuse term run through a three-step ramp gives flat
#      bands of light, mid and shadow, the shadow hue-shifted cool, instead of
#      smooth gradients that turn to mush when reduced.
#    * Reduction by majority, not by averaging. Each game pixel takes the most
#      common colour among the sixteen rendered pixels under it, so the bands
#      stay crisp and a two-pixel eye stays two pixels.
#    * A selective outline: one pixel around each layer in a darkened version
#      of the colour it borders, which is how hand-drawn sprites separate a
#      head from a body without a hard black line.
#    * Occlusion between layers. A layer is rendered with the layers drawn
#      before it standing in as holdouts, so a scarf tail behind the body is
#      cut away in the head layer rather than being drawn over the chest.
#
#  There is no armature. The character is a tree of empties with meshes
#  parented to them, and a pose is a dict of angles applied to those empties --
#  far easier to read than a rig for a figure twenty-five pixels tall. And a
#  whole sheet is one render: the camera is orthographic, so copies of the
#  character offset along its right and up vectors land on an exact grid.
# =============================================================================

import math
import os
import struct
import sys
import zlib

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Vector

# --- output ------------------------------------------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER_DIR = os.path.join(ROOT, "assets", "_render", "character")
OUT_DIR = os.path.join(ROOT, "assets", "characters", "player_hero")

FRAME_PX = 64          # one animation frame, in game pixels
SUPERSAMPLE = 4        # rendered pixels per game pixel
# Measured against the rigs this character stands beside. Their bodies read as
# seen from a little under fifty degrees; their faces are drawn front-on, a
# flat-art cheat that HEAD_PITCH reproduces by tipping the head back.
CAMERA_ELEVATION = 46.0
HEAD_PITCH = 24.0
# The head is scaled as a whole. Chibi enough to carry a face, but not so much
# that it hides the legs and arms that a run and a sprint are told apart by.
HEAD_SCALE = 0.9
FRAME_SPAN = 3.5       # world units across one frame (18.3 game px per unit)


def to_linear(rgb):
    def one(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return tuple(one(c) for c in rgb)


# Light and low in contrast on the body, because the game multiplies the body
# layer by the colour of whatever armour is worn -- a dark base leaves the tint
# nothing to do.
PALETTE = {
    "skin":     (0.97, 0.80, 0.66),
    "hair":     (0.58, 0.31, 0.17),
    "tunic":    (0.88, 0.85, 0.76),
    "trim":     (0.66, 0.74, 0.60),
    "belt":     (0.44, 0.30, 0.20),
    "gold":     (0.93, 0.74, 0.32),
    "trouser":  (0.55, 0.46, 0.37),
    "boot":     (0.40, 0.28, 0.20),
    "scarf":    (0.84, 0.24, 0.21),
    # Plate is rendered pale and almost colourless, because the game multiplies
    # it by the metal of whatever is worn: bronze, iron, steel, azuryte and the
    # rest are this same armour in their own colour. Anything saturated here
    # would tint every tier toward it.
    "plate":    (0.88, 0.89, 0.92),
    "plate_dk": (0.62, 0.64, 0.70),
    "plate_lt": (0.99, 0.99, 1.00),
    "steel":    (0.80, 0.84, 0.88),
    "grip":     (0.38, 0.25, 0.17),
    "eye":      (0.13, 0.11, 0.18),
    "shadow":   (0.00, 0.00, 0.00),
}

# The three characters offered at the start, as differences from the palette
# above. They used to be this one plus two from a CraftPix pack, which meant
# the art could not be redistributed with the game; these are the same rig in
# its own clothes, so everything the repository ships is its own.
#
# "hair" scales the locks rather than replacing them, and "scarf" takes the
# throat wrap and its tail off entirely, which changes the silhouette as much
# as any colour does.
LOOKS = {
    "player_hero": {},
    "player_warden": {
        "palette": {"hair":    (0.16, 0.13, 0.12),
                    "tunic":   (0.36, 0.45, 0.33),
                    "trim":    (0.24, 0.31, 0.24),
                    "belt":    (0.30, 0.22, 0.16),
                    "trouser": (0.36, 0.33, 0.27),
                    "boot":    (0.28, 0.21, 0.16),
                    "skin":    (0.85, 0.66, 0.50)},
        "hair": 0.55,
        "scarf": False,
    },
    # The town: the same rig again, in working clothes. These are NPC sprites
    # rather than characters to choose, so they carry nothing but the watchman,
    # who would look odd on a gate without a sword.
    "citizen1": {
        "palette": {"hair":    (0.36, 0.24, 0.16),
                    "tunic":   (0.74, 0.46, 0.31),
                    "trim":    (0.53, 0.32, 0.22),
                    "belt":    (0.36, 0.26, 0.18),
                    "trouser": (0.40, 0.36, 0.32),
                    "boot":    (0.31, 0.25, 0.19),
                    "skin":    (0.95, 0.78, 0.63)},
        "hair": 1.15,
        "scarf": False,
        "weapon": False,
    },
    "citizen2": {
        "palette": {"hair":    (0.23, 0.19, 0.15),
                    "tunic":   (0.52, 0.57, 0.64),
                    "trim":    (0.35, 0.39, 0.45),
                    "belt":    (0.33, 0.27, 0.22),
                    "trouser": (0.38, 0.36, 0.34),
                    "boot":    (0.28, 0.24, 0.20),
                    "skin":    (0.88, 0.70, 0.55)},
        "hair": 0.7,
        "scarf": False,
        "weapon": False,
    },
    "fighter2": {
        "palette": {"hair":    (0.30, 0.26, 0.22),
                    "tunic":   (0.60, 0.62, 0.68),
                    "trim":    (0.74, 0.62, 0.31),
                    "belt":    (0.32, 0.26, 0.20),
                    "trouser": (0.33, 0.35, 0.40),
                    "boot":    (0.27, 0.25, 0.24),
                    "skin":    (0.93, 0.76, 0.60)},
        "hair": 0.45,
        "scarf": False,
        "weapon": True,
    },
    # The highwaymen on the forest paths: dark leathers, a red neckerchief
    # for a mask, and a sword. Monsters rather than a character to choose, so
    # no plate, and only the clips a monster plays.
    "highwayman": {
        "palette": {"hair":    (0.13, 0.11, 0.10),
                    "tunic":   (0.30, 0.22, 0.16),
                    "trim":    (0.20, 0.15, 0.11),
                    "belt":    (0.15, 0.12, 0.10),
                    "trouser": (0.24, 0.22, 0.20),
                    "boot":    (0.17, 0.14, 0.12),
                    "scarf":   (0.58, 0.16, 0.14),
                    "skin":    (0.80, 0.62, 0.48)},
        "hair": 1.0,
        "scarf": True,
        "weapon": True,
    },
    # The magister of the college at Fernhollow: a blue robe with a gold hem,
    # grey hair worn long, and nothing in his hands but his own.
    "magister": {
        "palette": {"hair":    (0.80, 0.80, 0.82),
                    "tunic":   (0.24, 0.26, 0.50),
                    "trim":    (0.72, 0.62, 0.30),
                    "belt":    (0.30, 0.26, 0.20),
                    "trouser": (0.20, 0.20, 0.32),
                    "boot":    (0.16, 0.14, 0.18),
                    "skin":    (0.90, 0.74, 0.60)},
        "hair": 1.3,
        "scarf": False,
        "weapon": False,
    },
    # The college's students. An apprentice is young and in the pale blue of a
    # first-year, an adept in the violet of someone who has been let near the
    # ancient magic. Neither holds anything: what they throw, they throw with
    # their hands, and the "attack" clip with nothing in it is a cast.
    "apprentice": {
        "palette": {"hair":    (0.46, 0.30, 0.18),
                    "tunic":   (0.42, 0.56, 0.82),
                    "trim":    (0.90, 0.86, 0.74),
                    "belt":    (0.34, 0.28, 0.22),
                    "trouser": (0.30, 0.34, 0.48),
                    "boot":    (0.22, 0.18, 0.16),
                    "skin":    (0.94, 0.78, 0.64)},
        "hair": 1.0,
        "scarf": False,
        "weapon": False,
    },
    "adept": {
        "palette": {"hair":    (0.14, 0.12, 0.16),
                    "tunic":   (0.44, 0.28, 0.60),
                    "trim":    (0.80, 0.68, 0.34),
                    "belt":    (0.26, 0.20, 0.26),
                    "trouser": (0.26, 0.18, 0.36),
                    "boot":    (0.16, 0.13, 0.17),
                    "skin":    (0.78, 0.60, 0.46)},
        "hair": 1.2,
        "scarf": False,
        "weapon": False,
    },
    "player_wayfarer": {
        "palette": {"hair":    (0.86, 0.82, 0.70),
                    "tunic":   (0.62, 0.68, 0.80),
                    "trim":    (0.42, 0.48, 0.64),
                    "belt":    (0.34, 0.30, 0.34),
                    "trouser": (0.40, 0.42, 0.50),
                    "boot":    (0.30, 0.28, 0.32),
                    "scarf":   (0.30, 0.40, 0.62),
                    "skin":    (0.72, 0.55, 0.42)},
        "hair": 1.45,
        "scarf": True,
    },
}

# Set from --look; the defaults are the hero's.
HAIR_SCALE = 1.0
SCARF_ON = True
WEAPON_ON = True
# Whether to render the five plate layers. Only the playable characters need
# them; a grocer is never going to put a cuirass on.
ARMOUR_ON = True


def apply_look(name):
    """Palette and shape for one of LOOKS, before anything is built."""
    global HAIR_SCALE, SCARF_ON, WEAPON_ON, ARMOUR_ON
    look = LOOKS.get(name)
    if look is None:
        raise SystemExit("unknown look '%s'; have %s" % (name, ", ".join(LOOKS)))
    PALETTE.update(look.get("palette", {}))
    HAIR_SCALE = look.get("hair", 1.0)
    SCARF_ON = look.get("scarf", True)
    WEAPON_ON = look.get("weapon", True)
    ARMOUR_ON = look.get("armour", name.startswith("player_"))


# The ramp: how bright each band is relative to the base colour, where the
# bands change over (in N.L, which the sun's strength keeps in 0..1), and the
# cool tint mixed into the shadow band.
BAND_SHADE, BAND_MID, BAND_LIGHT = 0.64, 0.86, 1.0
STEP_MID, STEP_LIGHT = 0.12, 0.52
SHADE_TINT = (0.36, 0.33, 0.52)


def rad(d):
    return math.radians(d)


# --- scene plumbing ----------------------------------------------------------

def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras,
                  bpy.data.lights, bpy.data.images):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def link(ob, parent=None):
    bpy.context.scene.collection.objects.link(ob)
    if parent is not None:
        ob.parent = parent
    return ob


_materials = {}


def material(colour):
    """Cel shading: diffuse lighting quantised by a constant ramp into three
    flat bands of the base colour, emitted so nothing else in the scene can
    add a gradient on top."""
    if colour in _materials:
        return _materials[colour]
    mat = bpy.data.materials.new("toon_" + colour)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = 1.0
    links.new(emit.outputs["Emission"], out.inputs["Surface"])

    base = PALETTE[colour]
    if colour in ("eye", "shadow"):
        rgb = to_linear(base)
        emit.inputs["Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    else:
        diffuse = nodes.new("ShaderNodeBsdfDiffuse")
        diffuse.inputs["Color"].default_value = (1, 1, 1, 1)
        to_rgb = nodes.new("ShaderNodeShaderToRGB")
        ramp = nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.interpolation = "CONSTANT"
        links.new(diffuse.outputs["BSDF"], to_rgb.inputs["Shader"])
        links.new(to_rgb.outputs["Color"], ramp.inputs["Fac"])
        links.new(ramp.outputs["Color"], emit.inputs["Color"])

        def band(k, tint=None, amount=0.0):
            c = [min(1.0, ch * k) for ch in base]
            if tint:
                c = [c[i] * (1 - amount) + tint[i] * k * amount for i in range(3)]
            lin = to_linear(c)
            return (lin[0], lin[1], lin[2], 1.0)

        els = ramp.color_ramp.elements
        els[0].position = 0.0
        els[0].color = band(BAND_SHADE, SHADE_TINT, 0.22)
        els[1].position = STEP_MID
        els[1].color = band(BAND_MID, SHADE_TINT, 0.06)
        light = els.new(STEP_LIGHT)
        light.color = band(BAND_LIGHT)
    _materials[colour] = mat
    return mat


_meshes = {}


def mesh_capsule(r_top, r_bot, length, squash_y=1.0, segments=18, rings=12):
    """A capsule hanging down from its top cap's centre: the top hemisphere
    has radius r_top at z=0, the bottom one radius r_bot at z=-length, and
    the side tapers between them. squash_y flattens it front to back."""
    key = ("cap", r_top, r_bot, length, squash_y)
    if key in _meshes:
        return _meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=1.0)
    for v in bm.verts:
        x, y, z = v.co
        if z >= -1e-6:
            v.co = Vector((x * r_top, y * r_top * squash_y, z * r_top))
        else:
            v.co = Vector((x * r_bot, y * r_bot * squash_y, z * r_bot - length))
    me = bpy.data.meshes.new("capsule")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    _meshes[key] = me
    return me


def mesh_ellipsoid(rx, ry, rz):
    key = ("ell", rx, ry, rz)
    if key in _meshes:
        return _meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=18, v_segments=12, radius=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * rx, v.co.y * ry, v.co.z * rz))
    me = bpy.data.meshes.new("ellipsoid")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    _meshes[key] = me
    return me


def mesh_frustum(r_top, r_bot, depth, squash_y=1.0):
    """An open-bottomed flared skirt: a cone with its top at z=0."""
    key = ("fru", r_top, r_bot, depth, squash_y)
    if key in _meshes:
        return _meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=r_bot,
                          radius2=r_top, depth=depth)
    for v in bm.verts:
        v.co = Vector((v.co.x, v.co.y * squash_y, v.co.z - depth / 2.0))
    me = bpy.data.meshes.new("frustum")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    _meshes[key] = me
    return me


def mesh_torus(major, minor):
    key = ("tor", major, minor)
    if key in _meshes:
        return _meshes[key]
    bm = bmesh.new()
    seg, ring = 20, 8
    verts = []
    for i in range(seg):
        a = i / seg * math.tau
        row = []
        for j in range(ring):
            b = j / ring * math.tau
            r = major + minor * math.cos(b)
            row.append(bm.verts.new((r * math.cos(a), r * math.sin(a), minor * math.sin(b))))
        verts.append(row)
    for i in range(seg):
        for j in range(ring):
            bm.faces.new((verts[i][j], verts[(i + 1) % seg][j],
                          verts[(i + 1) % seg][(j + 1) % ring], verts[i][(j + 1) % ring]))
    me = bpy.data.meshes.new("torus")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    _meshes[key] = me
    return me


def part(name, mesh, colour, parent, loc=(0, 0, 0), rot=(0, 0, 0)):
    ob = bpy.data.objects.new(name, mesh)
    ob.data.materials.clear() if False else None
    ob.material_slots  # touch
    link(ob, parent)
    if not ob.data.materials:
        ob.data.materials.append(material(colour))
    # Meshes are shared between parts of different colours, so the material
    # goes on the object rather than the mesh.
    ob.material_slots[0].link = "OBJECT"
    ob.material_slots[0].material = material(colour)
    ob.location = loc
    ob.rotation_euler = rot
    return ob


def spike(name, root, tip, r_root, colour, parent, r_tip=0.012):
    """A tapered lock of hair from root to tip. HAIR_SCALE stretches the lock
    from its root, so a look can be cropped or long-haired without a second
    set of coordinates."""
    root, tip = Vector(root), Vector(tip)
    d = (tip - root) * (HAIR_SCALE if colour == "hair" else 1.0)
    length = max(0.001, d.length - r_root * 0.2)
    ob = part(name, mesh_capsule(r_root, r_tip, length), colour, parent, loc=root)
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, -1)).rotation_difference(d.normalized())
    return ob


def empty(name, loc, parent=None):
    e = bpy.data.objects.new(name, None)
    e.location = loc
    return link(e, parent)


# --- the character -----------------------------------------------------------
# Modelled facing -Y, toward the camera, so "down" is no rotation at all.
# Units: 1.0 is about eighteen game pixels. Feet on z=0, crown of the hair at
# about z=1.35, which lands the figure in the same box as the CraftPix rigs.

BODY, HEAD, WEAPON = "body", "head", "weapon"
# Worn plate, one group per equipment slot, each rendered as its own sheet so
# the game can draw a bronze cuirass over iron greaves.
ARM_LEGS, ARM_BODY, ARM_HANDS, ARM_HEAD, ARM_SHIELD = (
    "armour_legs", "armour_body", "armour_hands", "armour_head", "armour_shield")
ARMOUR_GROUPS = (ARM_LEGS, ARM_BODY, ARM_HANDS, ARM_HEAD, ARM_SHIELD)

# Three cuts of armour, so a tier reads as a different kind of armour and not
# only as a different colour. "plate" is the middle of the game and the one the
# sheets are named after; the other two get a suffix.
#
#   light   wood, bronze, iron -- hide and mail: a cap, a jerkin, no pauldrons
#   plate   steel to platinum  -- the full harness
#   ornate  demonite and above -- horned helm, winged pauldrons, a heavier skirt
#
# And two that are not metal at all, worn by whoever does not fight with a
# blade. They are a set of three -- head, body, legs -- so only those groups are
# rendered for them:
#
#   hide    the ranger's leathers -- a fur-lined hood, a jerkin with a fur
#           collar and a quiver on the back, bracers, wrapped legs, soft boots
#   robe    the mage's -- a pointed hat with a brim, a mantled robe with bell
#           sleeves, and a skirt to the ankle
ARMOUR_STYLES = ("light", "plate", "ornate", "hide", "robe")
SOFT_STYLES = ("hide", "robe")
ARMOUR_STYLE = "plate"


def build_character():
    """Returns (joints, groups, extras)."""
    g = {BODY: [], HEAD: [], WEAPON: []}
    for key in ARMOUR_GROUPS:
        g[key] = []

    root = empty("root", (0, 0, 0))
    move = empty("move", (0, 0, 0), root)          # bob, lean and lunge
    hips = empty("hips", (0, 0, 0.41), move)
    chest = empty("chest", (0, 0, 0.03), hips)
    neck = empty("neck", (0, 0, 0.37), chest)
    head_tilt = empty("head_tilt", (0, 0, 0), neck)
    head_tilt.rotation_euler = Euler((rad(-HEAD_PITCH), 0, 0), "XYZ")
    head_tilt.scale = (HEAD_SCALE, HEAD_SCALE, HEAD_SCALE)
    hair = empty("hair", (0, 0.02, 0.30), head_tilt)
    skirt = empty("skirt", (0, 0, 0.02), hips)

    # --- torso: a soft tapered barrel, a flared tunic skirt, a belt ---------
    g[BODY] += [
        part("torso", mesh_capsule(0.155, 0.14, 0.13, squash_y=0.78), "tunic", chest,
             loc=(0, 0, 0.20)),
        part("tunic_skirt", mesh_frustum(0.15, 0.20, 0.15, squash_y=0.82), "tunic", skirt),
        part("hem", mesh_torus(0.19, 0.018), "trim", skirt, loc=(0, 0, -0.14)),
        part("belt", mesh_ellipsoid(0.165, 0.13, 0.032), "belt", chest, loc=(0, 0, 0.0)),
        part("buckle", mesh_ellipsoid(0.034, 0.02, 0.028), "gold", chest,
             loc=(0, -0.128, 0.0)),
        part("collar", mesh_torus(0.10, 0.028), "trim", chest, loc=(0, 0, 0.33)),
    ]

    # --- arms: hanging from the shoulders, sleeve, bare forearm, mitten -----
    joints = {}
    for side, x in (("r", -0.19), ("l", 0.19)):
        sh = empty("shoulder_" + side, (x, 0, 0.27), chest)
        el = empty("elbow_" + side, (0, 0, -0.125), sh)
        ha = empty("hand_" + side, (0, 0, -0.11), el)
        g[BODY] += [
            part("sleeve_" + side, mesh_capsule(0.062, 0.056, 0.085), "trim", sh),
            part("fore_" + side, mesh_capsule(0.05, 0.046, 0.07), "skin", el),
            part("mitt_" + side, mesh_ellipsoid(0.058, 0.055, 0.06), "skin", ha,
                 loc=(0, 0, -0.01)),
        ]
        joints["shoulder_" + side] = sh
        joints["elbow_" + side] = el
        joints["hand_" + side] = ha

    # --- legs: trouser, boot shaft, a round-toed boot ------------------------
    for side, x in (("r", -0.075), ("l", 0.075)):
        hp = empty("hip_" + side, (x, 0, -0.05), hips)
        kn = empty("knee_" + side, (0, 0, -0.145), hp)
        g[BODY] += [
            part("thigh_" + side, mesh_capsule(0.068, 0.06, 0.09), "trouser", hp),
            part("shaft_" + side, mesh_capsule(0.064, 0.062, 0.07), "boot", kn,
                 loc=(0, 0, -0.035)),
            part("boot_" + side, mesh_ellipsoid(0.07, 0.105, 0.058), "boot", kn,
                 loc=(0, -0.035, -0.15)),
        ]
        joints["hip_" + side] = hp
        joints["knee_" + side] = kn

    # --- the head ------------------------------------------------------------
    # Built on the tilted pivot, so -Y is the face here.
    head_c = 0.24
    eyes = [
        part("eye_r", mesh_ellipsoid(0.052, 0.03, 0.098), "eye", head_tilt,
             loc=(-0.115, -0.232, head_c - 0.035)),
        part("eye_l", mesh_ellipsoid(0.052, 0.03, 0.098), "eye", head_tilt,
             loc=(0.115, -0.232, head_c - 0.035)),
    ]
    g[HEAD] += [
        part("skull", mesh_capsule(0.29, 0.25, 0.03), "skin", head_tilt,
             loc=(0, 0, head_c + 0.015)),
        part("ear_r", mesh_ellipsoid(0.04, 0.03, 0.055), "skin", head_tilt,
             loc=(-0.285, 0.0, head_c - 0.02)),
        part("ear_l", mesh_ellipsoid(0.04, 0.03, 0.055), "skin", head_tilt,
             loc=(0.285, 0.0, head_c - 0.02)),
    ] + eyes

    # Hair: a cap set back so the face shows, then locks. The bangs fall over
    # the forehead, two long side locks frame the face, the back is a fan of
    # points, and a single cowlick sticks up so the silhouette is not a dome.
    g[HEAD] += [
        part("hair_cap", mesh_ellipsoid(0.318, 0.30, 0.27), "hair", hair,
             loc=(0, 0.035, 0.05)),
        spike("bang_1", (-0.16, -0.17, 0.16), (-0.21, -0.30, 0.03), 0.08, "hair", hair),
        spike("bang_2", (-0.04, -0.21, 0.19), (-0.07, -0.33, 0.05), 0.085, "hair", hair),
        spike("bang_3", (0.08, -0.20, 0.18), (0.12, -0.32, 0.05), 0.08, "hair", hair),
        spike("bang_4", (0.19, -0.15, 0.14), (0.24, -0.27, 0.02), 0.07, "hair", hair),
        spike("lock_r", (-0.25, -0.07, 0.08), (-0.30, -0.14, -0.20), 0.065, "hair", hair),
        spike("lock_l", (0.25, -0.07, 0.08), (0.30, -0.14, -0.20), 0.065, "hair", hair),
        spike("back_1", (-0.13, 0.18, 0.08), (-0.18, 0.29, -0.12), 0.085, "hair", hair),
        spike("back_2", (0.0, 0.21, 0.10), (0.0, 0.32, -0.10), 0.09, "hair", hair),
        spike("back_3", (0.13, 0.18, 0.08), (0.18, 0.29, -0.12), 0.085, "hair", hair),
        spike("cowlick", (0.02, 0.02, 0.26), (0.10, 0.12, 0.44), 0.065, "hair", hair),
    ]

    # The scarf: a wrap at the throat and a two-piece tail down the back,
    # which trails further the faster the character goes.
    scarf1 = empty("scarf1", (0.05, 0.12, 0.25), chest)
    scarf2 = empty("scarf2", (0, 0, -0.125), scarf1)
    scarf3 = empty("scarf3", (0, 0, -0.12), scarf2)
    # The wrap belongs to the body layer: drawn in the head layer it sat over
    # the lower half of the face and read as a red mouth.
    #
    # A look without a scarf keeps the empties -- every pose sets angles on
    # them -- and simply hangs nothing off them.
    if SCARF_ON:
        g[BODY].append(part("scarf_wrap", mesh_torus(0.125, 0.03), "scarf", chest, loc=(0, 0, 0.25)))
        g[HEAD] += [
            part("scarf_tail1", mesh_capsule(0.046, 0.042, 0.10, squash_y=0.6), "scarf", scarf1),
            part("scarf_tail2", mesh_capsule(0.042, 0.038, 0.10, squash_y=0.6), "scarf", scarf2),
            part("scarf_tail3", mesh_capsule(0.038, 0.024, 0.10, squash_y=0.6), "scarf", scarf3),
        ]
    else:
        # Something at the throat, so the neckline is not bare: a rolled collar
        # in the tunic's trim.
        g[BODY].append(part("collar", mesh_torus(0.128, 0.026), "trim", chest, loc=(0, 0, 0.25)))

    # --- the sword, in the right hand: screen left when facing down, where the
    # CraftPix rigs carry theirs --------------------------------------------
    grip = empty("grip", (0, -0.01, -0.02), joints["hand_r"])
    grip.rotation_euler = Euler((rad(10), rad(18), 0), "XYZ")
    # And one in the left, its mirror: nothing hangs off it here, and the tier
    # renderer builds the off-hand dagger on it.
    grip_l = empty("grip_l", (0, -0.01, -0.02), joints["hand_l"])
    grip_l.rotation_euler = Euler((rad(10), rad(-18), 0), "XYZ")
    # A townsfolk look carries nothing: the grip empty stays, because every pose
    # turns it, and nothing hangs off it.
    if WEAPON_ON:
        g[WEAPON] += [
            part("pommel", mesh_ellipsoid(0.028, 0.028, 0.028), "gold", grip, loc=(0, 0, 0.09)),
            part("hilt", mesh_capsule(0.02, 0.02, 0.1), "grip", grip, loc=(0, 0, 0.06)),
            part("guard", mesh_ellipsoid(0.085, 0.03, 0.026), "gold", grip, loc=(0, 0, -0.05)),
            part("blade", mesh_capsule(0.036, 0.012, 0.40, squash_y=0.35), "steel", grip,
                 loc=(0, 0, -0.07)),
        ]

    # --- worn plate ----------------------------------------------------------
    # Built on the same joints as the body under it, so it moves with every
    # pose without a second rig. Each piece is a little larger than the part it
    # covers: at this size armour has to sit *outside* the silhouette or it
    # simply disappears into it.
    if ARMOUR_ON and ARMOUR_STYLE in SOFT_STYLES:
        build_soft_armour(g, joints, chest, skirt, head_tilt, head_c)
    elif ARMOUR_ON:
        light = ARMOUR_STYLE == "light"
        ornate = ARMOUR_STYLE == "ornate"
        # Cuirass: a breastplate over the torso, a gorget at the throat, a
        # fauld hanging off the belt, pauldrons on the shoulders.
        g[ARM_BODY] += [
            # A capsule hangs from its top cap's centre, so the top sits
            # r_top above the location: too generous a shoulder here and the
            # collar climbs over the character's chin.
            part("cuirass", mesh_capsule(0.140, 0.172, 0.160, squash_y=0.80),
                 "plate_dk" if light else "plate", chest, loc=(0, 0, 0.175)),
            part("cuirass_ridge", mesh_ellipsoid(0.052, 0.072, 0.105), "plate_lt", chest,
                 loc=(0, -0.112, 0.195)),
            part("gorget", mesh_torus(0.100, 0.024), "plate_dk", chest, loc=(0, 0, 0.278)),
            part("plackart", mesh_ellipsoid(0.168, 0.135, 0.040), "plate_dk", chest,
                 loc=(0, 0, 0.055)),
            part("fauld", mesh_frustum(0.165, 0.205, 0.105 * (1.35 if ornate else 1.0),
                                       squash_y=0.82), "plate", skirt, loc=(0, 0, 0.012)),
            part("fauld_hem", mesh_torus(0.196, 0.020), "plate_dk", skirt, loc=(0, 0, -0.055)),
        ]
        for side in ("r", "l"):
            sh = joints["shoulder_" + side]
            el = joints["elbow_" + side]
            if light:
                # Hide and mail: a shoulder strap and a bracer, no plate.
                g[ARM_BODY] += [
                    part("strap_" + side, mesh_torus(0.072, 0.018), "plate_dk", sh,
                         loc=(0, 0, 0.010)),
                    part("bracer_" + side, mesh_capsule(0.058, 0.054, 0.050), "plate", el,
                         loc=(0, 0, -0.016)),
                ]
            else:
                g[ARM_BODY] += [
                    part("pauldron_" + side, mesh_ellipsoid(0.104, 0.098, 0.070), "plate", sh,
                         loc=(0, 0, 0.026)),
                    part("pauldron_rim_" + side, mesh_torus(0.085, 0.016), "plate_dk", sh,
                         loc=(0, 0, -0.010)),
                    part("rerebrace_" + side, mesh_capsule(0.068, 0.062, 0.060), "plate_dk", sh,
                         loc=(0, 0, -0.055)),
                    part("vambrace_" + side, mesh_capsule(0.056, 0.052, 0.058), "plate", el,
                         loc=(0, 0, -0.012)),
                ]
            if ornate:
                # A wing swept back off each pauldron, and a spike on top.
                sx = -1 if side == "r" else 1
                g[ARM_BODY] += [
                    part("wing_" + side, mesh_ellipsoid(0.040, 0.105, 0.075), "plate_lt", sh,
                         loc=(sx * 0.050, 0.070, 0.055), rot=(rad(-18), 0, rad(sx * 22))),
                    part("shoulder_spike_" + side, mesh_capsule(0.026, 0.008, 0.075), "plate_lt", sh,
                         loc=(sx * 0.030, -0.010, 0.135)),
                ]

        # Greaves: thigh plate, knee cop, shin, and a sabaton over the boot.
        for side in ("r", "l"):
            hp = joints["hip_" + side]
            kn = joints["knee_" + side]
            # Light legs are a boot and a wrap: no knee cop, so the leg keeps a
            # soft outline. Ornate gets a spike off the knee instead.
            if not light:
                g[ARM_LEGS].append(
                    part("poleyn_" + side, mesh_ellipsoid(0.066, 0.060, 0.050), "plate_lt", kn,
                         loc=(0, -0.014, 0.012)))
            g[ARM_LEGS] += [
                part("greave_" + side, mesh_capsule(0.084, 0.080, 0.082),
                     "plate_dk" if light else "plate", kn, loc=(0, 0, -0.044)),
                part("sabaton_" + side, mesh_ellipsoid(0.088, 0.124, 0.064), "plate_dk", kn,
                     loc=(0, -0.040, -0.152)),
            ]
            if ornate:
                g[ARM_LEGS].append(
                    part("knee_spike_" + side, mesh_capsule(0.030, 0.008, 0.078), "plate_lt", kn,
                         loc=(0, -0.048, 0.016), rot=(rad(118), 0, 0)))
            # The hip joint is unused by the leg plate, but naming it keeps the
            # loop honest about what it is standing on.
            _ = hp

        # Gauntlets: a cuff and a shell over the mitten.
        for side in ("r", "l"):
            ha = joints["hand_" + side]
            g[ARM_HANDS] += [
                part("cuff_" + side, mesh_torus(0.072 if light else 0.082, 0.024), "plate_dk",
                     ha, loc=(0, 0, 0.042)),
                # A leather glove is barely larger than the hand; a gauntlet is
                # a shell over it, and the ornate one is a shell with a cuff.
                part("gauntlet_" + side,
                     mesh_ellipsoid(0.068 if light else 0.076, 0.066 if light else 0.072,
                                    0.068 if light else 0.076),
                     "plate_dk" if light else "plate", ha, loc=(0, 0, -0.014)),
            ]

        # Helm: a dome over the skull, a brow band, a nose guard and a low
        # crest, all on the tilted head pivot so it turns with the face.
        # A nasal helm rather than a bucket: a cap over the crown with its rim
        # just above the eyes, a brow band, a bar down the nose and a flap
        # either side. Built as a dome sitting high on the skull -- a full
        # capsule the size of the head enclosed the face and the character
        # became an egg with hair.
        #
        # Nothing below the brow and nothing behind the ears. The helm is the
        # one layer the head does not cut -- it has to sit over the hair, which
        # is as wide as it is -- so anything modelled at or below eye level is
        # drawn straight through the face.
        g[ARM_HEAD] += [
            # Slim front to back and set well up the skull: the dome's own
            # depth is what pushes its lower edge down the face on a camera
            # looking down at forty-six degrees.
            # The light cut is a leather cap: shallower, and it keeps neither
            # the crest nor the nasal, so the early tiers read as a hood rather
            # than as a knight in a cheaper colour.
            part("helm", mesh_ellipsoid(0.296, 0.205, 0.100 if light else 0.118), "plate",
                 head_tilt, loc=(0, 0.055, head_c + (0.210 if light else 0.225))),
            # The brow band has to sit above the eyes, which are at head_c
            # minus 0.035: at eye level the ring crosses the face and the
            # character wears a blindfold.
            part("helm_brow", mesh_torus(0.286, 0.026), "plate_dk", head_tilt,
                 loc=(0, 0.014, head_c + 0.060)),
        ]
        if not light:
            g[ARM_HEAD] += [
                part("helm_nasal", mesh_capsule(0.026, 0.022, 0.070), "plate_dk", head_tilt,
                     loc=(0, -0.252, head_c - 0.020)),
                part("helm_crest", mesh_ellipsoid(0.030 * (1.7 if ornate else 1.0),
                                                  0.165 * (1.2 if ornate else 1.0),
                                                  0.070 * (1.9 if ornate else 1.0)),
                     "plate_lt", head_tilt, loc=(0, 0.014, head_c + 0.280)),
            ]
        if ornate:
            # Horns, swept up and out off the brow band. A capsule hangs down
            # from its location, so the rotation that matters is about Y: a Z
            # turn only spins it about its own axis and the horns stayed buried
            # in the dome. 143 degrees puts the tip up and out at about 37
            # degrees off vertical, which clears the helm at this width.
            for sx in (-1, 1):
                g[ARM_HEAD].append(
                    part("horn_%d" % sx, mesh_capsule(0.042, 0.010, 0.230), "plate_lt", head_tilt,
                         loc=(sx * 0.205, 0.105, head_c + 0.120),
                         rot=(0, rad(-sx * 143), 0)))

        # Shield, on the off hand: a round face with a rim and a boss.
        # A buckler for the light cut, a kite face for the rest, and spikes off
        # the rim for the ornate one.
        shield_hand = joints["hand_l"]
        sr = 0.128 if light else 0.175
        g[ARM_SHIELD] += [
            part("shield", mesh_ellipsoid(sr, 0.052, sr), "plate", shield_hand,
                 loc=(0.02, -0.075, -0.030)),
            part("shield_rim", mesh_torus(sr - 0.003, 0.024), "plate_dk", shield_hand,
                 loc=(0.02, -0.075, -0.030), rot=(rad(90), 0, 0)),
            part("shield_boss", mesh_ellipsoid(0.055, 0.040, 0.055), "plate_lt", shield_hand,
                 loc=(0.02, -0.115, -0.030)),
        ]
        if ornate:
            for i, (dx, dz) in enumerate(((0, 1), (-0.87, -0.5), (0.87, -0.5))):
                g[ARM_SHIELD].append(
                    part("shield_spike_%d" % i, mesh_capsule(0.024, 0.006, 0.070), "plate_lt",
                         shield_hand,
                         loc=(0.02 + dx * sr, -0.075, -0.030 + dz * sr),
                         # A capsule points down, so a Y turn of atan2(-dx, -dz)
                         # aims it along the rim's outward direction.
                         rot=(0, math.atan2(-dx, -dz), 0)))

    joints.update({
        "root": root, "move": move, "hips": hips, "chest": chest, "neck": neck,
        "hair": hair, "skirt": skirt, "scarf1": scarf1, "scarf2": scarf2, "scarf3": scarf3,
        "grip": grip, "grip_l": grip_l,
    })
    return joints, g, {"eyes": eyes}


def build_soft_armour(g, joints, chest, skirt, head_tilt, head_c):
    """The two sets that are not metal: the ranger's hides and the mage's robes.
    Rendered pale like the plate and painted by the piece at draw time, so the
    three shades -- plate, plate_dk, plate_lt -- are all the colour there is:
    the light one is fur on the hides and trim on the robes."""
    hide = ARMOUR_STYLE == "hide"

    if hide:
        # --- jerkin: leather over the tunic, fur at the throat and shoulders,
        # tassets off the belt, bracers, and a quiver slung across the back.
        g[ARM_BODY] += [
            part("jerkin", mesh_capsule(0.146, 0.168, 0.158, squash_y=0.80), "plate", chest,
                 loc=(0, 0, 0.178)),
            part("fur_collar", mesh_torus(0.116, 0.040), "plate_lt", chest, loc=(0, 0.005, 0.272)),
            part("jerkin_belt", mesh_ellipsoid(0.172, 0.138, 0.034), "plate_dk", chest,
                 loc=(0, 0, 0.040)),
            part("tassets", mesh_frustum(0.165, 0.202, 0.090, squash_y=0.82), "plate_dk", skirt,
                 loc=(0, 0, 0.012)),
            part("quiver", mesh_capsule(0.050, 0.044, 0.200), "plate_dk", chest,
                 loc=(0.075, 0.150, 0.330), rot=(rad(-14), rad(22), 0)),
            part("fletching_a", mesh_ellipsoid(0.020, 0.020, 0.040), "plate_lt", chest,
                 loc=(0.060, 0.165, 0.400)),
            part("fletching_b", mesh_ellipsoid(0.020, 0.020, 0.040), "plate_lt", chest,
                 loc=(0.100, 0.150, 0.390)),
        ]
        for side in ("r", "l"):
            sh = joints["shoulder_" + side]
            el = joints["elbow_" + side]
            g[ARM_BODY] += [
                part("fur_shoulder_" + side, mesh_ellipsoid(0.088, 0.084, 0.052), "plate_lt", sh,
                     loc=(0, 0, 0.030)),
                part("bracer_" + side, mesh_capsule(0.058, 0.054, 0.060), "plate_dk", el,
                     loc=(0, 0, -0.014)),
            ]
        # --- legs: a thigh guard, wraps to the knee, a fur cuff and a soft boot.
        for side in ("r", "l"):
            hp = joints["hip_" + side]
            kn = joints["knee_" + side]
            g[ARM_LEGS] += [
                part("thigh_guard_" + side, mesh_capsule(0.078, 0.070, 0.075), "plate_dk", hp,
                     loc=(0, 0, -0.015)),
                part("wrap_" + side, mesh_capsule(0.078, 0.074, 0.080), "plate", kn,
                     loc=(0, 0, -0.046)),
                part("fur_cuff_" + side, mesh_torus(0.076, 0.024), "plate_lt", kn,
                     loc=(0, 0, -0.040)),
                part("soft_boot_" + side, mesh_ellipsoid(0.082, 0.116, 0.060), "plate_dk", kn,
                     loc=(0, -0.038, -0.152)),
            ]
        # --- hood: a cap set back on the skull with a fur-lined brow, a drape
        # behind and a peak. Like the helm it is not cut by the head, so nothing
        # of it may come below the brow at the front.
        g[ARM_HEAD] += [
            part("hood", mesh_ellipsoid(0.302, 0.232, 0.112), "plate", head_tilt,
                 loc=(0, 0.062, head_c + 0.206)),
            part("hood_fur", mesh_torus(0.288, 0.030), "plate_lt", head_tilt,
                 loc=(0, 0.016, head_c + 0.066)),
            part("hood_drape", mesh_ellipsoid(0.235, 0.100, 0.150), "plate", head_tilt,
                 loc=(0, 0.200, head_c + 0.070)),
            part("hood_peak", mesh_ellipsoid(0.070, 0.105, 0.062), "plate_dk", head_tilt,
                 loc=(0, 0.215, head_c + 0.250)),
        ]
        return

    # --- the robe: a top with a mantle over the shoulders and sleeves that
    # open out to the wrist, a sash, and a flare below it for whoever wears the
    # top without the skirt.
    g[ARM_BODY] += [
        part("robe_top", mesh_capsule(0.150, 0.170, 0.165, squash_y=0.82), "plate", chest,
             loc=(0, 0, 0.182)),
        part("mantle", mesh_torus(0.140, 0.044), "plate_lt", chest, loc=(0, 0.004, 0.262)),
        part("sash", mesh_ellipsoid(0.174, 0.140, 0.036), "plate_dk", chest, loc=(0, 0, 0.034)),
        part("sash_knot", mesh_ellipsoid(0.040, 0.030, 0.050), "plate_lt", chest,
             loc=(0.060, -0.128, 0.010)),
        part("robe_flare", mesh_frustum(0.166, 0.218, 0.130, squash_y=0.84), "plate", skirt,
             loc=(0, 0, 0.012)),
    ]
    for side in ("r", "l"):
        sh = joints["shoulder_" + side]
        el = joints["elbow_" + side]
        g[ARM_BODY] += [
            part("sleeve_upper_" + side, mesh_capsule(0.070, 0.076, 0.105), "plate", sh),
            # Wider at the wrist than at the elbow: the bell of the sleeve.
            part("sleeve_bell_" + side, mesh_capsule(0.064, 0.094, 0.098), "plate", el),
            part("sleeve_trim_" + side, mesh_torus(0.090, 0.017), "plate_lt", el,
                 loc=(0, 0, -0.100)),
        ]
    # --- the skirt: a cone from the hips to the ankle. It hangs from the hips
    # rather than from the legs, so a walk swings the feet out from under the
    # hem instead of bending the cloth at the knee.
    g[ARM_LEGS] += [
        part("robe_skirt", mesh_frustum(0.172, 0.272, 0.360, squash_y=0.86), "plate", skirt,
             loc=(0, 0, 0.0)),
        part("robe_hem", mesh_torus(0.262, 0.022), "plate_lt", skirt, loc=(0, 0, -0.350)),
        part("robe_band", mesh_torus(0.222, 0.014), "plate_dk", skirt, loc=(0, 0, -0.180)),
    ]
    for side in ("r", "l"):
        kn = joints["knee_" + side]
        g[ARM_LEGS].append(
            part("slipper_" + side, mesh_ellipsoid(0.076, 0.112, 0.052), "plate_dk", kn,
                 loc=(0, -0.040, -0.156)))
    # --- the hat: a dome on the crown, a brim, and a cone leaning back. A
    # capsule hangs down from where it is put, so half a turn about Y stands it
    # up, and the turn about X before that leans the point backwards. The brim
    # is set back and kept narrow at the front: the hat is not cut by the head,
    # and a brim as wide in front as it is behind came down over the eyes.
    g[ARM_HEAD] += [
        # Seen from the side the near half of anything round the head drops
        # down the screen by most of its radius, so the dome and the brim are
        # kept close to the skull and set high, and the height is all in a
        # narrow cone: a wider hat was a purple ball where the face should be.
        part("hat_dome", mesh_ellipsoid(0.268, 0.210, 0.078), "plate", head_tilt,
             loc=(0, 0.055, head_c + 0.222)),
        part("hat_brim", mesh_ellipsoid(0.312, 0.292, 0.022), "plate_dk", head_tilt,
             loc=(0, 0.045, head_c + 0.140)),
        part("hat_cone", mesh_capsule(0.158, 0.020, 0.430), "plate", head_tilt,
             loc=(0, 0.055, head_c + 0.262), rot=(rad(20), rad(180), 0)),
        part("hat_band", mesh_torus(0.168, 0.024), "plate_lt", head_tilt,
             loc=(0, 0.056, head_c + 0.262)),
    ]


def build_shadow():
    ob = part("shadow", mesh_ellipsoid(0.27, 0.19, 0.004), "shadow", None, loc=(0, 0, 0.004))
    return ob


# --- poses -------------------------------------------------------------------
# Angles are in friendly terms and converted in apply_pose:
#   leg_*      forward swing of the thigh, degrees (+ is toward the facing)
#   knee_*     flex, degrees (+ folds the foot back)
#   arm_*      forward swing of the upper arm
#   flare_*    how far the arm is held out from the side
#   elbow_*    flex (+ folds the forearm forward)
#   lean       chest pitch forward; hips_lean likewise for the pelvis
#   twist      chest yaw, degrees; hips_twist likewise
#   nod        neck pitch (+ looks down)
#   bob        root height, units; lunge moves forward; tip pitches everything
#   scarf, scarf2  how far back the scarf tail streams
#   hair       how far the hair sways back
#   skirt      tunic skirt swing
#   blink      eyes closed 0..1
#   sword      extra pitch of the grip, degrees
#   turn       yaw of the whole body, degrees: a spin
# `t` runs 0..1 across the clip and wraps for loops, so a cycle is a sine.

def pose_idle(t):
    b = math.sin(t * math.tau)
    return {
        "lean": 2 + b * 1.5, "nod": -1 - b * 1.2, "bob": 0.006 * b,
        "arm_l": 4 + b * 2, "arm_r": 6 - b * 2, "flare_l": 10, "flare_r": 10,
        "elbow_l": 16, "elbow_r": 22,
        "leg_l": -2, "leg_r": 3, "knee_l": 4, "knee_r": 3,
        "scarf": 14 + b * 4, "scarf2": 8 + b * 5, "hair": b * 1.5,
        # One blink per loop: a shut frame and a half-shut one either side.
        "blink": 1.0 if 0.70 <= t < 0.80 else (0.5 if 0.80 <= t < 0.90 else 0.0),
    }


def gait(t, stride, knee_swing, knee_base, arm, elbow, lean, bob, bounce_floor=0.0):
    """A cycle both legs share half a turn apart. The knee bends most while
    the leg swings through, the body is lowest at footfall and highest as the
    legs pass, and the arms swing against the legs."""
    p = t * math.tau
    s, c = math.sin(p), math.cos(p)
    return {
        "leg_l": stride * s, "leg_r": -stride * s,
        "knee_l": knee_base + knee_swing * max(0.0, c) ** 1.2,
        "knee_r": knee_base + knee_swing * max(0.0, -c) ** 1.2,
        "arm_l": -arm * s, "arm_r": arm * s,
        "elbow_l": elbow + 10 * max(0.0, -s), "elbow_r": elbow + 10 * max(0.0, s),
        "flare_l": 8, "flare_r": 8,
        "lean": lean, "twist": -7 * s, "hips_twist": 6 * s,
        "bob": bob * math.cos(2 * p) + bounce_floor,
        "skirt": 5 * math.sin(2 * p),
        "hair": 2 + 2 * math.cos(2 * p),
    }


def pose_walk(t):
    v = gait(t, stride=30, knee_swing=40, knee_base=6, arm=26, elbow=18, lean=4, bob=0.016)
    p = t * math.tau
    v.update({"nod": -2, "scarf": 22 + 4 * math.sin(2 * p - 1), "scarf2": 14 + 6 * math.sin(2 * p - 2)})
    return v


def pose_run(t):
    v = gait(t, stride=44, knee_swing=70, knee_base=14, arm=40, elbow=70, lean=12,
             bob=0.03, bounce_floor=0.012)
    p = t * math.tau
    v.update({"hips_lean": 4, "nod": -8, "scarf": 50 + 6 * math.sin(2 * p - 1),
              "scarf2": 30 + 10 * math.sin(2 * p - 2), "hair": 6 + 3 * math.cos(2 * p)})
    return v


def pose_sprint(t):
    # Built for crossing the world, and exaggerated so it reads as a different
    # gear from the run at twenty-five pixels: a hard lean, the trailing leg
    # driven out straight behind, the leading knee high, both feet off the
    # ground between strides, fists pumping with the elbows locked at a right
    # angle, and the scarf streaming out flat behind.
    p = t * math.tau
    s, c = math.sin(p), math.cos(p)
    v = {
        "leg_l": 58 * s + 10, "leg_r": -58 * s + 10,
        # Trailing leg straight, swinging leg folded right up under the body.
        "knee_l": 10 + 105 * max(0.0, c) ** 1.1,
        "knee_r": 10 + 105 * max(0.0, -c) ** 1.1,
        "arm_l": -62 * s, "arm_r": 62 * s,
        "elbow_l": 92, "elbow_r": 92, "flare_l": 12, "flare_r": 12,
        "lean": 30, "hips_lean": 12, "nod": -4, "lunge": 0.06,
        "twist": -10 * s, "hips_twist": 8 * s,
        # Two flights a cycle, so the body rises twice.
        "bob": 0.05 * math.cos(2 * p) + 0.04,
        # Streaming out behind and lifting a little, snapping at the tip.
        "scarf": 96 + 5 * math.sin(4 * p), "scarf2": 6 + 12 * math.sin(4 * p + 1),
        "hair": 14 + 4 * math.cos(2 * p),
        "skirt": -18 + 6 * math.sin(2 * p),
    }
    return v


def pose_attack(t):
    # Anticipation, a fast swing with the body turning into it and a step
    # forward, a held beat at full extension, and a settle.
    def ease(k):
        return k * k * (3 - 2 * k)
    if t < 0.30:
        k = ease(t / 0.30)
        arm, elbow, twist, lean, step, sword = 150 * k, 60 * k, 28 * k, -6 * k, 0.0, 0.0
    elif t < 0.55:
        k = ease((t - 0.30) / 0.25)
        arm = 150 - 175 * k
        elbow = 60 - 50 * k
        twist = 28 - 62 * k
        lean = -6 + 20 * k
        step = 0.08 * k
        sword = 20 * k
    else:
        k = ease((t - 0.55) / 0.45)
        arm = -25 + 30 * k
        elbow = 10 + 12 * k
        twist = -34 + 30 * k
        lean = 14 - 11 * k
        step = 0.08 - 0.06 * k
        sword = 20 - 20 * k
    return {
        "arm_r": arm, "elbow_r": elbow, "flare_r": 18,
        "arm_l": -18 + twist * 0.3, "elbow_l": 40, "flare_l": 14,
        "twist": twist, "lean": lean, "nod": -lean * 0.4,
        "leg_l": 16 * (step / 0.08), "leg_r": -12 * (step / 0.08),
        "knee_l": 14, "knee_r": 10, "lunge": step, "sword": sword,
        "scarf": 20 + 30 * (step / 0.08), "scarf2": 20, "hair": 4,
    }


def pose_thrust(t):
    # A spear's strike: the hand drawn back to the hip with the shaft level,
    # then driven straight out along the facing with a step in behind it, a
    # held beat at full stretch, and back. The chest does not turn into it: a
    # turn swings the shaft across the body. The grip is turned against the arm
    # every frame so the shaft stays level the whole way -- a thrust that dips
    # and rises reads as a swing.
    def ease(k):
        return k * k * (3 - 2 * k)
    if t < 0.30:
        k = ease(t / 0.30)
        arm, elbow, twist, lean, step = 20 + 10 * k, 30 + 65 * k, 0.0, -4 * k, 0.0
    elif t < 0.55:
        k = ease((t - 0.30) / 0.25)
        arm = 30 + 52 * k
        elbow = 95 - 91 * k
        twist = 0.0
        lean = -4 + 16 * k
        step = 0.12 * k
    else:
        k = ease((t - 0.55) / 0.45)
        arm = 82 - 52 * k
        elbow = 4 + 36 * k
        twist = 0.0
        lean = 12 - 9 * k
        step = 0.12 - 0.09 * k
    return {
        "arm_r": arm, "elbow_r": elbow, "flare_r": 2,
        "arm_l": 30 + step * 150, "elbow_l": 55, "flare_l": 10,
        "twist": twist, "lean": lean, "nod": -lean * 0.4,
        "leg_l": 18 * (step / 0.12), "leg_r": -14 * (step / 0.12),
        "knee_l": 14, "knee_r": 10, "lunge": step,
        # Level: the grip pitches with the arm, the chest's lean, a fifth of
        # the elbow and eight degrees, plus this.
        "sword": 90 - arm - lean - 0.2 * elbow - 8,
        "scarf": 20 + 30 * (step / 0.12), "scarf2": 20, "hair": 4,
    }


def pose_jump(t):
    if t < 0.2:
        k = t / 0.2
        lift, tuck, arms = -0.06 * k, 30 * k, 20 * k
    elif t < 0.75:
        k = (t - 0.2) / 0.55
        arc = math.sin(k * math.pi)
        lift = -0.06 + 0.60 * arc + 0.06 * k
        tuck = 30 + 40 * arc
        arms = 20 + 110 * arc
    else:
        k = (t - 0.75) / 0.25
        lift = 0.02 * (1.0 - k)
        tuck = 20 + 26 * math.sin(k * math.pi)
        arms = 60 * (1.0 - k)
    return {
        "leg_l": tuck * 0.8, "leg_r": tuck * 0.3, "knee_l": tuck * 1.5, "knee_r": tuck * 1.2,
        "arm_l": arms, "arm_r": arms * 0.85, "flare_l": 20, "flare_r": 20,
        "elbow_l": 30, "elbow_r": 34, "lean": 6, "nod": -4, "bob": lift,
        "scarf": 20 + 60 * max(0.0, lift), "scarf2": 30, "hair": -10 * max(0.0, lift),
        "skirt": -20 * max(0.0, lift),
    }


def pose_hurt(t):
    k = math.sin(min(1.0, t * 1.4) * math.pi)
    return {
        "lean": -22 * k, "twist": 12 * k, "nod": 14 * k,
        "arm_l": 30 * k, "arm_r": 26 * k, "flare_l": 10 + 22 * k, "flare_r": 10 + 18 * k,
        "elbow_l": 40, "elbow_r": 44, "leg_l": 10 * k, "leg_r": -8 * k,
        "knee_l": 10, "knee_r": 8, "bob": -0.04 * k, "lunge": -0.05 * k,
        "blink": 1.0 if k > 0.6 else 0.0, "scarf": 40 * k, "scarf2": 20, "hair": 10 * k,
    }


def pose_rush(t):
    """Rushing Strike: a running leap into a downward blow.

    Gather on the back foot with the blade drawn back, spring with the knees
    tucked and the weapon lifted over the head, and bring it down as the feet
    land, the whole body following it through. The distance is the world's --
    it carries the character forward and lifts them through an arc -- so the
    pose only has to say which part of the leap this is."""
    keys = [
        # t     the pose at that moment
        (0.00, dict(lean=10, leg_l=12, leg_r=-10, knee_l=16, knee_r=14, arm_r=40, elbow_r=40,
                    arm_l=20, elbow_l=30, flare_l=14, flare_r=12, sword=10, bob=0.0)),
        (0.18, dict(lean=22, leg_l=30, leg_r=-24, knee_l=48, knee_r=42, arm_r=110, elbow_r=50,
                    arm_l=-20, elbow_l=40, flare_l=16, flare_r=14, sword=-10, bob=-0.07, nod=6)),
        # In the air the blade is raised up and in front of the face, not
        # straight overhead: straight up it passes behind the head and helm,
        # which cut it out of the frame, and the leap read as someone jumping
        # with empty hands.
        (0.45, dict(lean=-4, leg_l=62, leg_r=18, knee_l=104, knee_r=92, arm_r=138, elbow_r=18,
                    arm_l=-30, elbow_l=34, flare_l=22, flare_r=10, sword=-32, bob=0.05, nod=-8,
                    scarf=44, scarf2=30, hair=10)),
        (0.64, dict(lean=30, leg_l=34, leg_r=-26, knee_l=40, knee_r=26, arm_r=-34, elbow_r=4,
                    arm_l=30, elbow_l=36, flare_l=18, flare_r=14, sword=34, bob=-0.06, lunge=0.08, nod=14,
                    scarf=30, scarf2=26, hair=6)),
        (1.00, dict(lean=6, leg_l=10, leg_r=-8, knee_l=12, knee_r=10, arm_r=10, elbow_r=30,
                    arm_l=10, elbow_l=26, flare_l=12, flare_r=12, sword=16, bob=0.0, lunge=0.02,
                    scarf=16, scarf2=10)),
    ]
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            k = (t - t0) / max(1e-6, t1 - t0)
            k = k * k * (3 - 2 * k)
            names = set(a) | set(b)
            v = {n: a.get(n, 0.0) + (b.get(n, 0.0) - a.get(n, 0.0)) * k for n in names}
            v["blink"] = 1.0 if 0.60 <= t < 0.70 else 0.0
            return v
    return dict(keys[-1][1])


def _keyed(keys, t):
    """A pose read off a list of (t, values) keys, eased between them."""
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            k = (t - t0) / max(1e-6, t1 - t0)
            k = k * k * (3 - 2 * k)
            names = set(a) | set(b)
            return {n: a.get(n, 0.0) + (b.get(n, 0.0) - a.get(n, 0.0)) * k for n in names}
    return dict(keys[-1][1])


def pose_crush(t):
    """Crushing Blow: a heavy mixed in after one light. The blade goes up over
    the head with the body leaning back off it, then comes straight down with
    a stride in behind it and the whole body following, and holds there a
    beat, driven into the ground. Overhead rather than round, so it reads as
    a different swing from the chain's."""
    return _keyed([
        (0.00, dict(lean=2, arm_r=20, elbow_r=40, flare_r=16, sword=10,
                    arm_l=-10, elbow_l=36, flare_l=14, knee_l=10, knee_r=8, scarf=16, scarf2=12)),
        # Raised up and in front of the face rather than straight overhead:
        # straight up, the blade passes behind the head and is cut out of
        # the frame, and the wind-up read as a flinch with empty hands.
        (0.35, dict(lean=-12, twist=8, nod=-8, arm_r=136, elbow_r=22, flare_r=14, sword=-30,
                    arm_l=-24, elbow_l=40, flare_l=18, leg_l=-8, leg_r=6, knee_l=18, knee_r=16,
                    lunge=-0.03, scarf=8, scarf2=6, hair=-4)),
        (0.55, dict(lean=30, twist=-4, nod=18, arm_r=-34, elbow_r=8, flare_r=12, sword=46,
                    arm_l=26, elbow_l=30, flare_l=16, leg_l=22, leg_r=-16, knee_l=18, knee_r=14,
                    lunge=0.11, scarf=44, scarf2=30, hair=12, bob=-0.03)),
        (0.72, dict(lean=26, twist=-4, nod=14, arm_r=-30, elbow_r=10, flare_r=12, sword=44,
                    arm_l=22, elbow_l=30, flare_l=16, leg_l=20, leg_r=-14, knee_l=16, knee_r=12,
                    lunge=0.10, scarf=30, scarf2=22, hair=6, bob=-0.03)),
        (1.00, dict(lean=6, arm_r=4, elbow_r=30, flare_r=14, sword=14,
                    arm_l=0, elbow_l=34, flare_l=14, leg_l=8, leg_r=-6, knee_l=12, knee_r=10,
                    lunge=0.03, scarf=16, scarf2=12)),
    ], t)


def pose_cleave(t):
    """Cleave: the heavy that ends a chain of two. The chest coils away from
    the blade with the arm held out wide and the blade level, then the whole
    body turns through the sweep, the arm crossing from far out on one side
    to across the chest on the other with a stride in, and it settles out of
    the follow-through. Level and wide, where the chain's swing is a cut."""
    return _keyed([
        (0.00, dict(lean=4, twist=10, arm_r=40, elbow_r=30, flare_r=30, sword=50,
                    arm_l=-6, elbow_l=36, flare_l=16, knee_l=10, knee_r=8, scarf=16, scarf2=12)),
        (0.30, dict(lean=2, twist=44, nod=-4, arm_r=52, elbow_r=18, flare_r=72, sword=70,
                    arm_l=-28, elbow_l=40, flare_l=24, leg_l=-6, leg_r=8, knee_l=16, knee_r=14,
                    lunge=-0.02, scarf=10, scarf2=8, hair=-4)),
        (0.48, dict(lean=12, twist=-10, nod=4, arm_r=62, elbow_r=8, flare_r=20, sword=72,
                    arm_l=10, elbow_l=34, flare_l=20, leg_l=18, leg_r=-12, knee_l=16, knee_r=12,
                    lunge=0.07, scarf=36, scarf2=26, hair=8)),
        (0.62, dict(lean=16, twist=-56, nod=8, arm_r=64, elbow_r=6, flare_r=-52, sword=70,
                    arm_l=34, elbow_l=30, flare_l=22, leg_l=24, leg_r=-18, knee_l=18, knee_r=12,
                    lunge=0.11, scarf=46, scarf2=32, hair=12, bob=-0.02)),
        (0.80, dict(lean=14, twist=-44, nod=6, arm_r=50, elbow_r=14, flare_r=-36, sword=54,
                    arm_l=26, elbow_l=32, flare_l=18, leg_l=20, leg_r=-14, knee_l=16, knee_r=12,
                    lunge=0.09, scarf=30, scarf2=22, hair=6)),
        (1.00, dict(lean=6, twist=-8, arm_r=14, elbow_r=30, flare_r=12, sword=16,
                    arm_l=2, elbow_l=34, flare_l=14, leg_l=8, leg_r=-6, knee_l=12, knee_r=10,
                    lunge=0.03, scarf=16, scarf2=12)),
    ], t)


def pose_backhand(t):
    """Backhand: the light that follows a heavy. The blade is already across
    the body from the heavy's follow-through, and it comes straight back out
    the other way -- no wind-up, a short whip of the arm and a half step --
    and is level again at once, ready for the chain to go on."""
    return _keyed([
        (0.00, dict(lean=10, twist=-30, arm_r=30, elbow_r=44, flare_r=-36, sword=36,
                    arm_l=14, elbow_l=34, flare_l=18, leg_l=8, leg_r=-6, knee_l=14, knee_r=12,
                    lunge=0.02, scarf=20, scarf2=14)),
        (0.40, dict(lean=12, twist=30, nod=2, arm_r=44, elbow_r=12, flare_r=66, sword=62,
                    arm_l=-14, elbow_l=36, flare_l=16, leg_l=16, leg_r=-12, knee_l=14, knee_r=10,
                    lunge=0.07, scarf=34, scarf2=24, hair=6)),
        (0.65, dict(lean=10, twist=22, arm_r=36, elbow_r=18, flare_r=50, sword=50,
                    arm_l=-8, elbow_l=36, flare_l=16, leg_l=14, leg_r=-10, knee_l=14, knee_r=10,
                    lunge=0.06, scarf=26, scarf2=18, hair=4)),
        (1.00, dict(lean=6, twist=4, arm_r=12, elbow_r=30, flare_r=14, sword=16,
                    arm_l=0, elbow_l=34, flare_l=14, leg_l=8, leg_r=-6, knee_l=12, knee_r=10,
                    lunge=0.03, scarf=16, scarf2=12)),
    ], t)


def pose_spin(t):
    """Cross Cut: both attack buttons together. A full turn on the spot with
    the blade held out level at arm's length, crouched into it, the free arm
    out for balance and the scarf streaming. The turn is the pose's own, so
    the one clip shows the character pass through every facing; the world
    strikes everything round them at its middle."""
    k = t * t * (3 - 2 * t)
    turn = 360.0 * k
    # Lowest and fastest through the middle of the turn.
    mid = math.sin(t * math.pi)
    return {
        "turn": turn,
        "arm_r": 56 + 10 * mid, "elbow_r": 8, "flare_r": 62 + 10 * mid, "sword": 74,
        "arm_l": 30 + 16 * mid, "elbow_l": 24, "flare_l": 44 + 14 * mid,
        "lean": 8 + 8 * mid, "nod": 4 * mid,
        "leg_l": 14 * mid, "leg_r": -12 * mid, "knee_l": 14 + 12 * mid, "knee_r": 12 + 10 * mid,
        "bob": -0.05 * mid, "scarf": 20 + 40 * mid, "scarf2": 14 + 26 * mid, "hair": 12 * mid,
        "skirt": -10 * mid, "blink": 1.0 if 0.35 < t < 0.55 else 0.0,
    }


def pose_block(t):
    # Guard up: the shield arm raised across the front of the chest, the
    # weapon drawn back low and ready, feet staggered and knees bent, and a
    # slow breath under it. Held for as long as the button is.
    b = math.sin(t * math.tau)
    return {
        # Swung across the body at the shoulder rather than lifted at it. A
        # flare turns the arm about the shoulder's own up axis, which leaves
        # the shield facing where it faced; lifting the arm tips its face up at
        # the sky, and from above it read as a plate held flat. About forty-five
        # degrees of pitch altogether tilts it square to a camera looking down.
        "arm_l": 25 + b * 2, "elbow_l": 20, "flare_l": -55,
        "arm_r": -8 - b * 2, "elbow_r": 46, "flare_r": 16, "sword": 24,
        "lean": 9 + b * 1.2, "twist": -12, "nod": 4,
        "leg_l": 16, "leg_r": -12, "knee_l": 18, "knee_r": 16,
        "bob": -0.03 + 0.004 * b, "lunge": 0.01,
        "scarf": 18 + b * 3, "scarf2": 10 + b * 4, "hair": b,
    }


def pose_death(t):
    k = min(1.0, t * 1.15)
    e = k * k * (3 - 2 * k)
    return {
        # Falls forward, and slides back as it goes so the body lies across
        # the frame rather than off its bottom edge.
        "tip": -84 * e, "bob": -0.02 * e, "lunge": -0.5 * e,
        "lean": 14 * e, "nod": -20 * e,
        "arm_l": 60 * e, "arm_r": 50 * e, "flare_l": 12 + 30 * e, "flare_r": 12 + 24 * e,
        "elbow_l": 20, "elbow_r": 24,
        "leg_l": -18 * e, "leg_r": -26 * e, "knee_l": 30 * e, "knee_r": 16 * e,
        "blink": 1.0 if e > 0.5 else 0.0, "scarf": 10, "scarf2": 10,
    }


def _ease(k):
    k = max(0.0, min(1.0, k))
    return k * k * (3 - 2 * k)


def _work_cycle(t, rest, raise_, strike, hold=0.14):
    """A looping stroke: from rest up to the top of the swing, a fast strike
    down to the bottom, a beat held there, and back to rest. Each argument is
    a dict of pose values; the result blends between them."""
    keys = set(rest) | set(raise_) | set(strike)
    def mix(a, b, k):
        return {key: a.get(key, 0.0) + (b.get(key, 0.0) - a.get(key, 0.0)) * k for key in keys}
    if t < 0.42:
        return mix(rest, raise_, _ease(t / 0.42))
    if t < 0.56:
        return mix(raise_, strike, _ease((t - 0.42) / 0.14))
    if t < 0.56 + hold:
        return mix(strike, strike, 0.0)
    return mix(strike, rest, _ease((t - 0.56 - hold) / (1.0 - 0.56 - hold)))


def pose_chop(t):
    # Feet planted wide and the axe brought round from over the shoulder into
    # the trunk in front, the body turning into the blow.
    base = {"leg_l": 14, "leg_r": -10, "knee_l": 14, "knee_r": 10, "flare_l": 16, "flare_r": 22,
            "scarf": 18, "scarf2": 12, "hair": 3}
    rest = dict(base, arm_r=40, elbow_r=30, arm_l=30, elbow_l=40, twist=0, lean=4, sword=20)
    raise_ = dict(base, arm_r=145, elbow_r=65, arm_l=110, elbow_l=60, twist=30, lean=-6, nod=-6,
                  sword=10, hips_twist=8)
    strike = dict(base, arm_r=55, elbow_r=10, arm_l=45, elbow_l=20, twist=-28, lean=16, nod=8,
                  sword=55, hips_twist=-8, lunge=0.04, scarf=34)
    return _work_cycle(t, rest, raise_, strike)


def pose_mine(t):
    # The pick lifted high over the head and driven straight down into the rock,
    # the whole body folding over the stroke.
    base = {"leg_l": 16, "leg_r": -12, "knee_l": 20, "knee_r": 14, "flare_l": 12, "flare_r": 12,
            "scarf": 16, "scarf2": 10, "hair": 2}
    rest = dict(base, arm_r=35, elbow_r=30, arm_l=30, elbow_l=30, lean=6, sword=30)
    raise_ = dict(base, arm_r=175, elbow_r=40, arm_l=160, elbow_l=40, lean=-10, nod=-10,
                  sword=0, bob=0.01)
    strike = dict(base, arm_r=40, elbow_r=5, arm_l=35, elbow_l=8, lean=26, nod=14,
                  sword=20, bob=-0.03, lunge=0.03, scarf=30)
    return _work_cycle(t, rest, raise_, strike, hold=0.16)


def pose_fish(t):
    # The rod held out over the water, tip up, and the patient bob of waiting:
    # a slow sway, a twitch of the wrist now and then, and a blink.
    b = math.sin(t * math.tau)
    twitch = 1.0 if 0.40 <= t < 0.52 else 0.0
    return {
        "arm_r": 62 + 4 * b + 10 * twitch, "elbow_r": 26 - 6 * twitch, "flare_r": 12,
        "arm_l": 34 + 3 * b, "elbow_l": 50, "flare_l": 14,
        "lean": 6 + 1.5 * b, "nod": 6, "bob": 0.005 * b,
        "leg_l": 6, "leg_r": -4, "knee_l": 6, "knee_r": 4,
        "sword": 38 + 5 * b + 10 * twitch,
        "scarf": 14 + 4 * b, "scarf2": 8 + 5 * b, "hair": b * 1.5,
        "blink": 1.0 if 0.80 <= t < 0.88 else 0.0,
    }


def pose_gather(t):
    # Down on one knee at the plant, reaching in with the right hand and
    # drawing it back to the chest, the left hand steadying on the thigh. The
    # body folds a long way over, because a small lean is lost at this size.
    reach = _ease(t / 0.45) if t < 0.45 else (1.0 if t < 0.6 else 1.0 - _ease((t - 0.6) / 0.4))
    b = math.sin(t * math.tau)
    return {
        "bob": -0.14, "lean": 12 + 10 * reach, "nod": 6 + 6 * reach,
        "leg_l": 70, "knee_l": 95, "leg_r": -20, "knee_r": 120,
        "arm_r": 30 + 60 * reach, "elbow_r": 50 - 40 * reach, "flare_r": 18 + 30 * reach,
        "arm_l": 30, "elbow_l": 70, "flare_l": 18,
        "twist": -6 * reach, "sword": 30,
        "scarf": 10 + 6 * reach, "scarf2": 10, "hair": 2 * b,
        "blink": 1.0 if 0.70 <= t < 0.78 else 0.0,
    }


# clip -> (pose function, frame count, loops)
def two_handed(pose_fn, left_on=0.095):
    """A one-handed swing, made with both hands on the hilt: the combos and the
    leap, for a greatsword or a greataxe. The pose is the sword's, to the frame.
    What changes is found on the rig, in apply_pose: where that pose put the
    right hand is brought in to where the left can reach it too, the blade is
    kept pointing exactly where the sword's pointed, and the left hand goes on
    the hilt. So the swing keeps its shape and its timing, and nobody swings
    four feet of steel about with one hand."""
    def pose(t):
        v = dict(pose_fn(t))
        v["two_hands"] = left_on
        return v
    return pose


# When the blow lands. These clips are played to last exactly as long as the
# attack they belong to ("fit" in make_sprites_json.ps1), and an attack's blow is
# live from about a fifth of the way through it to about a half: a light from
# 20% to 60%, a heavy from 27% to 49%, a charged one from 20% to 45%. So the
# frame that shows the blow landing has to be one shown inside that -- the
# fourth of eight, the third of six, the second of five -- and anything thrown
# or cast leaves the hand at the fifth, which is the second frame. The first
# pass had the blow at the middle of every clip, after the monster had been hit.


def mirrored(pose_fn):
    """The same pose made with the other hand: a left-handed stab is a stab."""
    swaps = {"sword": "sword_l", "sword_l": "sword", "grip_y": "grip_y_l", "grip_y_l": "grip_y"}
    def pose(t):
        out = {}
        for k, x in pose_fn(t).items():
            if k in swaps:          out[swaps[k]] = x
            elif k.endswith("_l"):  out[k[:-2] + "_r"] = x
            elif k.endswith("_r"):  out[k[:-2] + "_l"] = x
            elif k in ("twist", "hips_twist", "turn"): out[k] = -x
            else: out[k] = x
        return out
    return pose


def pose_bash(t):
    """A mace's blow: one hand, up beside the head and straight down, with the
    knees taking it. Shorter than the Crushing Blow and with no stride: it is
    the weight of the head that does the work. Lands on the third frame of six."""
    return _keyed([
        (0.00, dict(lean=2, arm_r=24, elbow_r=44, flare_r=16, sword=8,
                    arm_l=-8, elbow_l=38, flare_l=14, knee_l=10, knee_r=8, scarf=16, scarf2=12)),
        (0.20, dict(lean=-8, twist=-14, nod=-6, arm_r=132, elbow_r=58, flare_r=20, sword=-22,
                    arm_l=-20, elbow_l=44, flare_l=18, knee_l=14, knee_r=12, scarf=10, scarf2=8, hair=-3)),
        (0.40, dict(lean=22, twist=10, nod=12, arm_r=-18, elbow_r=6, flare_r=10, sword=34,
                    arm_l=18, elbow_l=34, flare_l=16, leg_l=12, leg_r=-8, knee_l=22, knee_r=20,
                    lunge=0.05, scarf=38, scarf2=26, hair=10, bob=-0.04)),
        (0.60, dict(lean=20, twist=10, nod=10, arm_r=-16, elbow_r=8, flare_r=10, sword=32,
                    arm_l=16, elbow_l=34, flare_l=16, leg_l=12, leg_r=-8, knee_l=20, knee_r=18,
                    lunge=0.05, scarf=28, scarf2=20, hair=6, bob=-0.04)),
        (1.00, dict(lean=4, arm_r=10, elbow_r=34, flare_r=14, sword=12,
                    arm_l=-2, elbow_l=36, flare_l=14, knee_l=12, knee_r=10, lunge=0.01, scarf=16, scarf2=12)),
    ], t)


def _two_hands(hold, aim, edge=None, left_on=0.095, **rest):
    """A frame of a two-handed clip: where the right hand is (from between the
    shoulders, in the chest's axes), where the blade points and which way its
    edge faces (in the character's), and the rest of the body as usual."""
    v = dict(hold_x=hold[0], hold_y=hold[1], hold_z=hold[2], aim_x=aim[0], aim_y=aim[1], aim_z=aim[2],
             left_on=left_on, arm_r=60, elbow_r=70, cross_r=50, arm_l=60, elbow_l=70, cross_l=50, flare_l=-10)
    if edge:
        v.update(edge_x=edge[0], edge_y=edge[1], edge_z=edge[2])
    v.update(rest)
    return v


# Where a great weapon waits between blows: hands low in front of the chest,
# the blade up and well out over the right shoulder. Out, and not straight up:
# the head's sheet is drawn over the weapon's, so a blade held up in front of
# the face is a blade nobody sees from the front.
_GREAT_READY = dict(hold=(-0.04, -0.12, -0.08), aim=(-0.58, -0.12, 0.80), edge=(0, -1, 0))


def pose_sweep(t):
    """A great weapon's swing, both hands on it. The body coils away to the
    right with the blade laid back behind the shoulder, and then the whole of it
    comes round -- hips, chest, a stride, and the blade flat through the front
    on the fourth frame and far out to the left before it can be stopped and
    hauled back. The edge leads: `edge` is the way the blade is travelling."""
    def cut(ax, ay, az):
        return dict(aim=(ax, ay, az), edge=(-ay, ax, 0.0))
    return _keyed([
        (0.000, _two_hands(**_GREAT_READY, lean=4, twist=-6, knee_l=12, knee_r=10, scarf=16, scarf2=12)),
        (0.143, _two_hands((-0.09, -0.10, -0.05), **cut(-0.78, 0.30, 0.55), lean=-4, twist=-42, nod=-3,
                           leg_l=-8, leg_r=6, knee_l=16, knee_r=16, lunge=-0.03, scarf=8, scarf2=6, hair=-4)),
        (0.286, _two_hands((-0.11, -0.09, -0.03), **cut(-0.72, 0.66, 0.20), lean=-9, twist=-62, nod=-4,
                           leg_l=-12, leg_r=8, knee_l=20, knee_r=20, lunge=-0.05, scarf=4, scarf2=2, hair=-6)),
        (0.429, _two_hands((-0.03, -0.13, -0.09), **cut(-0.20, -0.98, -0.04), lean=16, twist=22, nod=6,
                           leg_l=24, leg_r=-16, knee_l=20, knee_r=14, lunge=0.12, bob=-0.03,
                           scarf=52, scarf2=38, hair=12)),
        (0.571, _two_hands((-0.02, -0.12, -0.08), **cut(0.86, -0.50, -0.06), lean=18, twist=62, nod=6,
                           leg_l=26, leg_r=-18, knee_l=18, knee_r=12, lunge=0.13, bob=-0.03,
                           scarf=42, scarf2=30, hair=10)),
        (0.714, _two_hands((-0.02, -0.12, -0.07), **cut(0.93, 0.25, 0.22), lean=12, twist=70, nod=4,
                           leg_l=18, leg_r=-12, knee_l=14, knee_r=10, lunge=0.08, bob=-0.01,
                           scarf=26, scarf2=18, hair=4)),
        (0.857, _two_hands((-0.03, -0.12, -0.07), aim=(0.35, -0.30, 0.89), edge=(0.0, -1.0, 0.0), lean=8, twist=30, nod=2,
                           leg_l=10, leg_r=-6, knee_l=12, knee_r=10, lunge=0.04, scarf=20, scarf2=14, hair=2)),
        (1.000, _two_hands(**_GREAT_READY, lean=4, twist=-6, leg_l=6, leg_r=-4, knee_l=12, knee_r=10,
                           lunge=0.02, scarf=18, scarf2=12)),
    ], t)


def pose_hew(t):
    """A greataxe's charged chop: both hands carry it up and back over the head
    with the body arched under it, and then everything comes down at once on
    the fourth frame -- a deep stride, the back bent into it, the head of the
    axe buried in front of the feet and held there two frames more before it is
    wrenched out. The bit leads all the way."""
    def chop(ay, az, show=0.0):
        # `show` turns the bit a little to the side: dead square to the blow it
        # is edge-on to anyone in front of it, and an axe seen edge-on is a stick.
        return dict(aim=(0.0, ay, az), edge=(show, -az, ay))
    return _keyed([
        (0.000, _two_hands(**_GREAT_READY, lean=4, knee_l=12, knee_r=10, scarf=16, scarf2=12)),
        (0.143, _two_hands((-0.02, -0.06, 0.12), **chop(0.45, 0.89, 0.4), lean=-14, nod=-10,
                           leg_l=-10, leg_r=8, knee_l=18, knee_r=18, lunge=-0.05, scarf=6, scarf2=4, hair=-8)),
        (0.286, _two_hands((-0.02, -0.03, 0.14), **chop(0.80, 0.60, 0.4), lean=-22, nod=-14,
                           leg_l=-14, leg_r=10, knee_l=22, knee_r=22, lunge=-0.07, bob=0.02,
                           scarf=2, scarf2=0, hair=-12)),
        (0.429, _two_hands((-0.03, -0.12, -0.11), **chop(-0.80, -0.60, 0.55), lean=32, nod=14,
                           leg_l=32, leg_r=-24, knee_l=30, knee_r=18, lunge=0.16, bob=-0.07,
                           scarf=58, scarf2=44, hair=18)),
        (0.571, _two_hands((-0.03, -0.12, -0.11), **chop(-0.78, -0.63, 0.55), lean=30, nod=12,
                           leg_l=30, leg_r=-22, knee_l=28, knee_r=16, lunge=0.15, bob=-0.07,
                           scarf=36, scarf2=26, hair=9)),
        (0.714, _two_hands((-0.03, -0.12, -0.11), **chop(-0.80, -0.60, 0.5), lean=28, nod=10,
                           leg_l=28, leg_r=-20, knee_l=26, knee_r=16, lunge=0.14, bob=-0.06,
                           scarf=26, scarf2=18, hair=5)),
        (0.857, _two_hands((-0.03, -0.12, -0.09), **chop(-0.85, 0.30, 0.3), lean=18, nod=8,
                           leg_l=20, leg_r=-14, knee_l=20, knee_r=12, lunge=0.09, bob=-0.03,
                           scarf=24, scarf2=16, hair=4)),
        (1.000, _two_hands(**_GREAT_READY, lean=6, leg_l=8, leg_r=-6, knee_l=12, knee_r=10,
                           lunge=0.03, scarf=18, scarf2=12)),
    ], t)


def _level(arm, lean, elbow):
    """What `sword` has to be for what is held to lie level: see pose_thrust."""
    return 90 - arm - lean - 0.2 * elbow - 8


def pose_shoot(t):
    """A crossbow let off: it comes up level in front of the chest with the left
    hand under the stock, goes off on the second frame, kicks up and back
    through the shoulders on the third, and is brought down again."""
    def held(hold, rise, **rest):
        return _two_hands(hold, (0.0, -1.0, rise), left_on=-0.07, left_under=0.03, aim_top=1.0,
                          twist=-6, nod=4, leg_l=12, leg_r=-10, knee_l=14, knee_r=10, **rest)
    return _keyed([
        (0.00, held((-0.02, -0.10, -0.08), -0.14, lean=4, scarf=18, scarf2=12)),
        (0.25, held((-0.02, -0.13, -0.05), 0.00, lean=6, scarf=18, scarf2=12)),
        (0.50, held((-0.02, -0.07, -0.03), 0.30, lean=-5, lunge=-0.04, scarf=32, scarf2=20, hair=-4)),
        (0.75, held((-0.02, -0.11, -0.05), 0.06, lean=3, lunge=-0.01, scarf=22, scarf2=14)),
        (1.00, held((-0.02, -0.08, -0.10), -0.24, lean=2, scarf=18, scarf2=12)),
    ], t)


def pose_reload(t):
    """Spanning it: the crossbow down in front with its nose at the ground, the
    body bent over it, and the left hand hauling the string back up the stock
    -- twice, because once does not look like work."""
    pull = 0.5 - 0.5 * math.cos(t * 4 * math.pi)
    return _two_hands((-0.02, -0.12, -0.12), (0.0, -0.30, -0.95), edge=(1.0, 0.0, 0.0),
                      left_on=-0.10 + 0.09 * pull, left_under=0.03,
                      lean=14 + 10 * pull, nod=12 + 6 * pull, twist=-4, leg_l=16, leg_r=-12, knee_l=28, knee_r=16,
                      bob=-0.05 + 0.02 * pull, scarf=30, scarf2=20, hair=8)


def pose_throw(t):
    """A knife thrown. It starts already drawn back past the ear with the off
    hand pointing where it is going -- there is no time in a throw for getting
    there -- and is whipped over the top on the second frame with a step, the
    hand finishing low across the body."""
    return _keyed([
        (0.00, dict(lean=-10, twist=-30, nod=-4, arm_r=150, elbow_r=96, flare_r=26, sword=-50,
                    arm_l=70, elbow_l=14, flare_l=6, leg_l=-8, leg_r=6, knee_l=14, knee_r=14,
                    lunge=-0.03, scarf=8, scarf2=6, hair=-4)),
        (0.20, dict(lean=18, twist=34, nod=8, arm_r=64, elbow_r=6, flare_r=4, sword=40,
                    arm_l=-10, elbow_l=40, flare_l=18, leg_l=20, leg_r=-14, knee_l=16, knee_r=12,
                    lunge=0.09, scarf=44, scarf2=30, hair=10)),
        (0.40, dict(lean=20, twist=46, nod=8, arm_r=14, elbow_r=14, flare_r=-16, sword=30,
                    arm_l=-16, elbow_l=42, flare_l=18, leg_l=18, leg_r=-12, knee_l=14, knee_r=10,
                    lunge=0.08, scarf=30, scarf2=22, hair=6)),
        (0.70, dict(lean=10, twist=20, nod=4, arm_r=16, elbow_r=30, flare_r=4, sword=14,
                    arm_l=-8, elbow_l=38, flare_l=16, leg_l=10, leg_r=-6, knee_l=12, knee_r=10,
                    lunge=0.04, scarf=20, scarf2=14, hair=2)),
        (1.00, dict(lean=4, arm_r=18, elbow_r=38, flare_r=14, sword=8,
                    arm_l=-4, elbow_l=36, flare_l=14, leg_l=6, leg_r=-4, knee_l=10, knee_r=8,
                    lunge=0.02, scarf=16, scarf2=12)),
    ], t)


def pose_flick(t):
    """A wand: nothing but the forearm. It starts drawn up beside the ear with
    the point at the sky, is snapped out at what it is for on the second frame
    with the point dead level, held a frame, and brought back."""
    def wand(ax, ay, az, **rest):
        v = dict(aim_x=ax, aim_y=ay, aim_z=az, arm_l=-6, elbow_l=36, flare_l=14, knee_l=10, knee_r=8)
        v.update(rest)
        return v
    return _keyed([
        (0.00, wand(-0.55, 0.30, 0.78, lean=-6, twist=-14, arm_r=64, elbow_r=120, flare_r=14,
                    scarf=12, scarf2=8, hair=-2)),
        (0.25, wand(0.0, -1.0, 0.0, lean=10, twist=16, nod=4, arm_r=82, elbow_r=6, flare_r=-6,
                    leg_l=10, leg_r=-8, knee_l=12, lunge=0.04, scarf=32, scarf2=22, hair=6)),
        (0.50, wand(0.0, -1.0, 0.04, lean=9, twist=16, nod=4, arm_r=80, elbow_r=10, flare_r=-6,
                    leg_l=10, leg_r=-8, knee_l=12, lunge=0.04, scarf=24, scarf2=16, hair=4)),
        (0.75, wand(-0.06, -0.85, 0.45, lean=5, twist=6, arm_r=56, elbow_r=44, flare_r=2,
                    leg_l=6, leg_r=-4, lunge=0.02, scarf=20, scarf2=14, hair=2)),
        (1.00, wand(-0.10, -0.70, 0.70, lean=2, arm_r=34, elbow_r=70, flare_r=6, scarf=16, scarf2=12)),
    ], t)


def pose_invoke(t):
    """A book or an orb: held out in front of the chest on an open palm, level
    -- so the pages, and the orb over them, are up -- while the other hand does
    the casting: it starts drawn up beside the head, is pushed out over what is
    held on the second frame, held there, and let fall."""
    keys = []
    for at, left, left_elbow, left_cross, lean, rise in ((0.0, 118, 96, -10, -6, 0.012), (0.2, 80, 8, 36, 8, 0.0),
                                                         (0.4, 76, 14, 36, 8, 0.0), (0.6, 60, 40, 24, 5, 0.0),
                                                         (0.8, 34, 60, 8, 2, 0.0), (1.0, 22, 68, 0, 0, 0.0)):
        keys.append((at, dict(lean=lean, twist=-8 if at < 0.1 else 6 if at < 0.7 else 0, nod=8 - lean * 0.3,
                              hold_x=-0.09, hold_y=-0.13, hold_z=-0.10 + rise, arm_r=30, elbow_r=86, cross_r=30,
                              aim_x=0.0, aim_y=-1.0, aim_z=0.0, aim_top=1.0,
                              arm_l=left, elbow_l=left_elbow, flare_l=6, cross_l=left_cross, knee_l=10, knee_r=8, bob=rise,
                              leg_l=6 if 0.1 < at < 0.7 else 0, leg_r=-4 if 0.1 < at < 0.7 else 0,
                              scarf=16 + lean * 2, scarf2=12, hair=lean * 0.5)))
    return _keyed(keys, t)


CLIPS = {
    "idle":   (pose_idle,   8,  True),
    "walk":   (pose_walk,   8,  True),
    "run":    (pose_run,    8,  True),
    "sprint": (pose_sprint, 8,  True),
    "attack": (pose_attack, 6,  False),
    # A spear's strike, in place of the swing.
    "thrust": (pose_thrust, 6,  False),
    # Rushing Strike, from the melee tree: a running leap into a downward blow.
    "rush":   (pose_rush,   8,  False),
    # The combos: a heavy mixed into the light chain, a light after a heavy,
    # and both buttons at once. See README, "Combos".
    "crush":    (pose_crush,    6, False),
    "cleave":   (pose_cleave,   8, False),
    "backhand": (pose_backhand, 5, False),
    "spin":     (pose_spin,     8, False),
    # The weapons that are not a sword, a spear, a bow or a staff, each with a
    # strike of its own: see README, "The armoury".
    "bash":   (pose_bash,   6, False),     # a mace
    "sweep":  (pose_sweep,  8, False),     # a greatsword, a greataxe: two hands, slow and wide
    "hew":    (pose_hew,    8, False),     # a greataxe's charged chop
    "shoot":  (pose_shoot,  5, False),     # a crossbow
    "reload": (pose_reload, 8, True),      # and spanning it again
    "throw":  (pose_throw,  6, False),     # throwing knives
    "flick":  (pose_flick,  5, False),     # a wand
    "invoke": (pose_invoke, 6, False),     # a grimoire, an orb
    # A second dagger, in the left hand: every other blow of a pair is this one.
    "offstab": (mirrored(pose_thrust), 6, False),
    # The combos and the leap, with both hands on the hilt: what a greatsword or
    # a greataxe plays in place of the sword's. See two_handed().
    "rush_2h":     (two_handed(pose_rush),     8, False),
    "crush_2h":    (two_handed(pose_crush),    6, False),
    "cleave_2h":   (two_handed(pose_cleave),   8, False),
    "backhand_2h": (two_handed(pose_backhand), 5, False),
    "spin_2h":     (two_handed(pose_spin),     8, False),
    "jump":   (pose_jump,   6,  False),
    "hurt":   (pose_hurt,   4,  False),
    # Holding a shield up. Looped: a guard lasts as long as the button is held.
    "block":  (pose_block,  4,  True),
    "death":  (pose_death,  6,  False),
    # The work: looped for as long as the gathering goes on.
    "chop":   (pose_chop,   8,  True),
    "mine":   (pose_mine,   8,  True),
    "fish":   (pose_fish,   8,  True),
    "gather": (pose_gather, 8,  True),
}

# Rows in the order every sheet in this project uses, and how far the
# character turns from its modelled facing toward the camera.
FACINGS = [("down", 0.0), ("left", 270.0), ("right", 90.0), ("up", 180.0)]


# --- both hands on it ---------------------------------------------------------------
# The arms on this rig are short -- 0.235 from a shoulder that is 0.19 out from
# the middle -- so two hands meet on a hilt only in a small pocket in front of
# the chest, and an arm's angles set by eye miss it. So a two-handed pose does
# not give angles. It says where the right hand is (`hold_x/y/z`: from the point
# between the shoulders, in the chest's own axes, so a lean or a twist carries
# the hands with it and what could be reached still can be), where the thing
# held points (`aim_x/y/z`, in the character's axes: forward is -Y, up is +Z,
# their right is -X), and how far along it the left hand sits (`left_on`: toward
# the pommel, or negative, toward the nose -- under a crossbow's stock). The
# angles a pose does give are where each search starts, which is what decides
# which way the elbow points. Found once for each frame of a clip and kept: the
# four facings and every sheet of a clip are the same pose.
_RIGHT_HAND = {}
_LEFT_HAND = {}
_AIMED = {}
SHOULDER_MID = Vector((0.0, 0.0, 0.27))      # between the shoulders, in the chest's space
ARM_STRETCH = 1.30                           # and how far an arm may cheat


def _pose_key(v):
    return tuple(sorted((k, round(float(x), 3)) for k, x in v.items()))


def _descend(start, lo, hi, cost, step=24.0, good=0.003):
    """Turn a few angles, one at a time and by less and less, until `cost` is small."""
    best = [min(h, max(l, x)) for x, l, h in zip(start, lo, hi)]
    err = cost(*best)
    while step > 0.6 and err > good:
        moved = False
        for i in range(len(best)):
            for sign in (1.0, -1.0):
                trial = list(best)
                trial[i] = min(hi[i], max(lo[i], trial[i] + sign * step))
                e = cost(*trial)
                if e < err - 1e-6:
                    best, err, moved = trial, e, True
        if not moved:
            step *= 0.5
    return best, err


def _set_arm(joints, side, arm, elbow, flare, cross):
    out = 1.0 if side == "l" else -1.0
    joints["shoulder_" + side].rotation_euler = Euler((rad(-arm), rad(-out * flare), rad(-out * cross)), "XYZ")
    joints["elbow_" + side].rotation_euler = Euler((rad(-elbow), 0, 0), "XYZ")


def _place_right_hand(joints, v):
    key = _pose_key(v)
    if key not in _RIGHT_HAND:
        want = SHOULDER_MID + Vector((v.get("hold_x", 0.0), v["hold_y"], v.get("hold_z", 0.0)))

        def miss(arm, elbow, flare, cross):
            _set_arm(joints, "r", arm, elbow, flare, cross)
            bpy.context.view_layer.update()
            return (joints["hand_r"].matrix_world.translation - joints["chest"].matrix_world @ want).length

        start = [v.get("arm_r", 50.0), v.get("elbow_r", 60.0), v.get("flare_r", 0.0), v.get("cross_r", 45.0)]
        _RIGHT_HAND[key] = _descend(start, (-60.0, 0.0, -50.0, -30.0), (175.0, 135.0, 50.0, 100.0), miss)
    (arm, elbow, flare, cross), err = _RIGHT_HAND[key]
    _set_arm(joints, "r", arm, elbow, flare, cross)
    return err


def _aim_grip(joints, v):
    """A pose that gives `aim_x`, `aim_y`, `aim_z` says where what is held points
    -- the blade, the crossbow's nose, the wand -- in the character's own space
    (forward is -Y, up is +Z, their right is -X), and the wrist is turned until
    it does. `aim_top` asks besides that the thing's top stay up: a crossbow's
    prod level, a book's pages to the sky. The arm's angles put the hand
    somewhere; which way the thing in it points from there is three more, and
    working them out by hand for an arm that is raised, bent and swung across
    the chest is how a crossbow came to be aimed at the man's own elbow."""
    key = _pose_key(v)
    want = Vector((v.get("aim_x", 0.0), v.get("aim_y", -1.0), v.get("aim_z", 0.0)))
    if want.length < 1e-6:
        return
    want.normalize()
    top = v.get("aim_top", 0.0) > 0.5
    # `edge_x/y/z`: which way the thing's own +X faces, as nearly as it can while
    # pointing where it is told -- an axe's bit into the swing, not flat to it.
    edge = Vector((v.get("edge_x", 0.0), v.get("edge_y", 0.0), v.get("edge_z", 0.0)))
    edge = edge - want * edge.dot(want)
    edge = edge.normalized() if edge.length > 1e-3 else None
    grip = joints["grip"]
    if key not in _AIMED:
        def miss(rx, ry, rz):
            grip.rotation_euler = Euler((rad(rx), rad(ry), rad(rz)), "XYZ")
            bpy.context.view_layer.update()
            local = joints["root"].matrix_world.to_3x3().inverted() @ grip.matrix_world.to_3x3()
            nose = (local @ Vector((0, 0, -1))).normalized()
            err = 1.0 - nose.dot(want)
            if top:
                err += 0.5 * (1.0 - (local @ Vector((0, -1, 0))).normalized().dot(Vector((0, 0, 1))))
            if edge is not None:
                err += 0.5 * (1.0 - (local @ Vector((1, 0, 0))).normalized().dot(edge))
            return err

        best, err = None, 1e9
        # A few places to start from: the turn is three angles and has valleys.
        for start in ((0, 18, 0), (-90, 0, 0), (90, 0, 0), (0, -70, 0), (0, 0, 90), (180, 0, 0), (-90, 0, 90), (-90, -60, 0)):
            cur, e = list(start), miss(*start)
            step = 40.0
            while step > 0.5 and e > 1e-4:
                moved = False
                for i in range(3):
                    for sign in (1.0, -1.0):
                        trial = list(cur)
                        trial[i] += sign * step
                        te = miss(*trial)
                        if te < e - 1e-7:
                            cur, e, moved = trial, te, True
                if not moved:
                    step *= 0.5
            if e < err:
                best, err = cur, e
        _AIMED[key] = best
    rx, ry, rz = _AIMED[key]
    grip.rotation_euler = Euler((rad(rx), rad(ry), rad(rz)), "XYZ")


def _left_mark(joints, v):
    g = joints["grip"].matrix_world
    return g.translation + (g.to_3x3() @ Vector((v.get("left_out", 0.0), v.get("left_under", 0.0), v["left_on"])))


def _place_left_hand(joints, v):
    """The left hand onto what the right holds. If the mark is further off than
    the arm is long, the arm is let out to meet it -- up to ARM_STRETCH, which at
    forty pixels tall is a pixel of forearm and cannot be seen, where a hand
    floating beside a hilt can. Returns how far short it still fell."""
    key = _pose_key(v)
    if key not in _LEFT_HAND:
        joints["shoulder_l"].scale = (1.0, 1.0, 1.0)

        def miss(arm, elbow, flare, cross):
            _set_arm(joints, "l", arm, elbow, flare, cross)
            bpy.context.view_layer.update()
            return (joints["hand_l"].matrix_world.translation - _left_mark(joints, v)).length

        start = [v.get("arm_l", 50.0), v.get("elbow_l", 60.0), v.get("flare_l", 0.0), v.get("cross_l", 45.0)]
        angles, err = _descend(start, (-60.0, 0.0, -80.0, -30.0), (175.0, 135.0, 50.0, 100.0), miss)
        stretch = 1.0
        if err > 0.006:
            # Out of reach: point the straightened arm at the mark and let it out.
            _set_arm(joints, "l", *angles)
            bpy.context.view_layer.update()
            shoulder = joints["shoulder_l"].matrix_world.translation
            have = (joints["hand_l"].matrix_world.translation - shoulder).length
            need = (_left_mark(joints, v) - shoulder).length
            stretch = min(ARM_STRETCH, max(1.0, need / max(have, 1e-6)))
            joints["shoulder_l"].scale = (stretch, stretch, stretch)
            angles, err = _descend(angles, (-60.0, 0.0, -80.0, -30.0), (175.0, 135.0, 50.0, 100.0), miss, step=8.0)
        _LEFT_HAND[key] = (angles, stretch, err)
    (arm, elbow, flare, cross), stretch, err = _LEFT_HAND[key]
    joints["shoulder_l"].scale = (stretch, stretch, stretch)
    _set_arm(joints, "l", arm, elbow, flare, cross)
    joints["grip_l"].rotation_euler = Euler((rad(elbow * 0.8 - 8 - v.get("sword_l", 0.0)), rad(-v.get("grip_y_l", 18.0)), 0), "XYZ")
    return err


def _both_hands_on(joints, v):
    bpy.context.view_layer.update()
    left_on = v["two_hands"]
    chest = joints["chest"].matrix_world
    hand = chest.inverted() @ joints["hand_r"].matrix_world.translation - SHOULDER_MID
    root = joints["root"].matrix_world.to_3x3().inverted()
    grip = joints["grip"].matrix_world.to_3x3()
    nose = (root @ grip @ Vector((0, 0, -1))).normalized()
    edge = (root @ grip @ Vector((1, 0, 0))).normalized()
    # In the chest's own space, which way the pommel lies: the left hand's mark
    # is that way from the right, and both have to be within an arm of a shoulder.
    pommel = -(chest.to_3x3().inverted() @ joints["root"].matrix_world.to_3x3() @ nose).normalized() * left_on
    right, left = Vector((-0.19, 0.0, 0.0)), Vector((0.19, 0.0, 0.0))
    pocket = Vector((-0.03, -0.11, -0.04))
    hold = Vector((min(0.05, max(-0.12, hand.x)), hand.y, hand.z))
    for _ in range(16):
        if (hold - right).length <= 0.225 and (hold + pommel - left).length <= 0.235 * ARM_STRETCH * 0.96:
            break
        hold = hold.lerp(pocket, 0.18)
    out = dict(v)
    out.update(hold_x=hold.x, hold_y=hold.y, hold_z=hold.z, aim_x=nose.x, aim_y=nose.y, aim_z=nose.z,
               edge_x=edge.x, edge_y=edge.y, edge_z=edge.z, left_on=left_on)
    out.setdefault("cross_r", 40.0)
    out.setdefault("cross_l", 45.0)
    return out


def apply_pose(joints, extras, v):
    for name, joint in joints.items():
        if name in ("grip", "grip_l"):
            continue
        joint.rotation_euler = Euler((0, 0, 0), "XYZ")
    joints["move"].location = Vector((0, 0, 0))

    get = lambda k: v.get(k, 0.0)
    # Legs and arms swing about X; positive X rotation carries a limb that
    # hangs down toward +Y, which is behind the character, hence the signs.
    for side in ("l", "r"):
        out = 1.0 if side == "l" else -1.0
        joints["hip_" + side].rotation_euler = Euler(
            (rad(-get("leg_" + side) + get("hips_lean")), 0, 0), "XYZ")
        joints["knee_" + side].rotation_euler = Euler((rad(get("knee_" + side)), 0, 0), "XYZ")
        # `cross` swings a raised arm across the chest, about the upright: a
        # flare only rolls an arm that is already pointing forward, and two
        # hands cannot meet on a hilt without it.
        joints["shoulder_" + side].rotation_euler = Euler(
            (rad(-get("arm_" + side)), rad(-out * get("flare_" + side)), rad(-out * get("cross_" + side))), "XYZ")
        joints["elbow_" + side].rotation_euler = Euler((rad(-get("elbow_" + side)), 0, 0), "XYZ")

    joints["hips"].rotation_euler = Euler((rad(get("hips_lean")), 0, rad(get("hips_twist"))), "XYZ")
    joints["chest"].rotation_euler = Euler((rad(get("lean")), 0, rad(get("twist"))), "XYZ")
    joints["neck"].rotation_euler = Euler((rad(get("nod") - get("lean") * 0.35), 0,
                                           rad(-get("twist") * 0.4)), "XYZ")
    joints["hair"].rotation_euler = Euler((rad(get("hair")), 0, 0), "XYZ")
    joints["skirt"].rotation_euler = Euler((rad(get("skirt")), 0, 0), "XYZ")
    joints["scarf1"].rotation_euler = Euler((rad(get("scarf")), 0, rad(8)), "XYZ")
    joints["scarf2"].rotation_euler = Euler((rad(get("scarf2")), 0, 0), "XYZ")
    # The tip lags the middle by the same again, so it whips.
    joints["scarf3"].rotation_euler = Euler((rad(get("scarf2") * 1.2), 0, 0), "XYZ")
    # At rest the blade hangs down and a little out, the way the CraftPix rigs
    # carry theirs; an attack pitches it up into line with the swing.
    # The elbow's bend is undone at the wrist, or a relaxed arm points the
    # blade straight ahead -- invisible facing down, a lance facing sideways.
    # `grip_y` turns what is held about the hand's own upright: eighteen degrees
    # out is how a sword hangs, and a crossbow brought up to the eye wants to
    # point where the man is looking, not off past his elbow.
    joints["grip"].rotation_euler = Euler((rad(get("elbow_r") * 0.8 - 8 - get("sword")),
                                           rad(v.get("grip_y", 18.0)), 0), "XYZ")
    joints["grip_l"].rotation_euler = Euler((rad(get("elbow_l") * 0.8 - 8 - get("sword_l")),
                                             rad(-v.get("grip_y_l", 18.0)), 0), "XYZ")

    joints["move"].location = Vector((0, -get("lunge"), get("bob")))
    # `turn` yaws the whole body about its own centre: a spin is the one
    # pose that turns through every facing inside a single clip.
    joints["move"].rotation_euler = Euler((rad(-get("tip")), 0, rad(get("turn"))), "XYZ")

    shut = get("blink")
    for eye in extras["eyes"]:
        eye.scale = (1.0, 1.0, max(0.12, 1.0 - shut))

    # What is held with both hands, or has to point somewhere: the right hand is
    # put where the pose wants it, what it holds is aimed, and then the left is
    # put on it. In that order -- each stands on the one before.
    joints["shoulder_l"].scale = (1.0, 1.0, 1.0)
    if v.get("two_hands", 0.0) > 0.0 and "hold_y" not in v:
        v = _both_hands_on(joints, v)
    if "hold_y" in v:
        _place_right_hand(joints, v)
    if "aim_y" in v or "aim_x" in v or "aim_z" in v:
        _aim_grip(joints, v)
    if "left_on" in v:
        _place_left_hand(joints, v)


# --- rendering ---------------------------------------------------------------


def setup_world():
    scene = bpy.context.scene
    world = bpy.data.worlds[0] if bpy.data.worlds else bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Strength"].default_value = 0.0

    # One sun, from the upper left and a little in front, fixed in the world
    # so every facing is lit from the same screen side the scenery is.
    sun = bpy.data.lights.new("key", "SUN")
    sun.energy = math.pi      # N.L of 1 comes out as 1 through Shader to RGB
    sun.use_shadow = False    # self-shadow speckles; the bands do the work
    ob = bpy.data.objects.new("key", sun)
    link(ob)
    travel = Vector((0.75, 0.9, -1.3)).normalized()
    ob.rotation_euler = Vector((0, 0, -1)).rotation_difference(travel).to_euler()


def camera_basis():
    elev = math.radians(CAMERA_ELEVATION)
    right = Vector((1.0, 0.0, 0.0))
    up = Vector((0.0, math.sin(elev), math.cos(elev)))
    return right, up


def setup_camera(cols, rows):
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    # ortho_scale is the width of the view, always. Left on AUTO it is the
    # width of whichever side of the image is longer, so a sheet with fewer
    # columns than rows -- every three-frame hurt clip, against four facings --
    # was rendered at the wrong scale: the grid no longer lined up with the
    # cells, and each row of the sheet was drawn further down its cell than the
    # one above it until the bottom row's feet hung out of the frame.
    cam_data.sensor_fit = "HORIZONTAL"
    cam_data.ortho_scale = FRAME_SPAN * cols
    cam = bpy.data.objects.new("cam", cam_data)
    link(cam)
    bpy.context.scene.camera = cam
    right, up = camera_basis()
    centre = (right * (FRAME_SPAN * (cols - 1) / 2.0)
              - up * (FRAME_SPAN * (rows - 1) / 2.0)
              + Vector((0.0, 0.0, FRAME_SPAN * 0.30)))
    elev = math.radians(CAMERA_ELEVATION)
    back = Vector((0.0, -math.cos(elev), math.sin(elev))) * (FRAME_SPAN * cols * 3.0)
    cam.location = centre + back
    cam.rotation_euler = (math.radians(90.0 - CAMERA_ELEVATION), 0.0, 0.0)
    cam_data.clip_end = FRAME_SPAN * cols * 8.0


def setup_render(cols, rows):
    scene = bpy.context.scene
    for name in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            scene.render.engine = name
            break
        except TypeError:
            continue
    try:
        scene.eevee.taa_render_samples = 1   # hard edges; the reduction does the rest
    except AttributeError:
        pass
    scene.render.resolution_x = FRAME_PX * SUPERSAMPLE * cols
    scene.render.resolution_y = FRAME_PX * SUPERSAMPLE * rows
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.filter_size = 0.0
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"


def render_to(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


# --- reduction to game pixels ----------------------------------------------------

def read_png(path):
    img = bpy.data.images.load(path)
    w, h = img.size
    buf = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    # Blender's rows run bottom-up.
    return (buf.reshape(h, w, 4)[::-1] * 255.0 + 0.5).astype(np.uint8)


def write_png(path, rgba):
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def reduce_majority(big, coverage_needed=6):
    """Each game pixel takes the most common colour among the opaque rendered
    pixels beneath it, and is opaque when enough of them are."""
    f = SUPERSAMPLE
    h, w = big.shape[0] // f, big.shape[1] // f
    blocks = big[:h * f, :w * f].reshape(h, f, w, f, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, f * f, 4)
    opaque = blocks[..., 3] > 127
    rgb = blocks[..., :3].astype(np.int32)
    key = (rgb[..., 0] // 6) * 4096 + (rgb[..., 1] // 6) * 64 + (rgb[..., 2] // 6)
    key = np.where(opaque, key, -1)
    same = (key[..., :, None] == key[..., None, :]) & opaque[..., None, :]
    votes = np.where(opaque, same.sum(-1), -1)
    best = votes.argmax(-1)
    out = np.zeros((h, w, 4), np.uint8)
    pick = np.take_along_axis(blocks[..., :3], best[..., None, None].repeat(3, -1), axis=2)[:, :, 0]
    covered = opaque.sum(-1) >= coverage_needed
    out[..., :3] = np.where(covered[..., None], pick, 0)
    out[..., 3] = np.where(covered, 255, 0)
    return out


def outline(img, strength=0.42):
    """One pixel round the silhouette, in a darkened version of whatever it
    touches -- a selective outline rather than a flat black line."""
    a = img[..., 3] > 0
    rgb = img[..., :3].astype(np.float32)
    acc = np.zeros_like(rgb)
    cnt = np.zeros(a.shape, np.float32)
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        na = np.roll(a, (dy, dx), axis=(0, 1))
        nr = np.roll(rgb, (dy, dx), axis=(0, 1))
        acc += nr * na[..., None]
        cnt += na
    edge = (~a) & (cnt > 0)
    mean = acc / np.maximum(cnt, 1)[..., None]
    # Darker, and pushed toward a deep purple-brown so it sits with the art.
    dark = mean * strength + np.array([22, 12, 26], np.float32) * (1 - strength)
    out = img.copy()
    out[edge, :3] = np.clip(dark[edge], 0, 255).astype(np.uint8)
    out[edge, 3] = 255
    return out


def build_sheet(clip_name, out_dir):
    pose_fn, frames, loops = CLIPS[clip_name]
    cols, rows = frames, len(FACINGS)

    clear_scene()
    _materials.clear()
    _meshes.clear()
    setup_world()

    right, up = camera_basis()
    layers = {"shadow": [], "body": [], "head": [], "weapon_front": []}
    if ARMOUR_ON:
        for key in ARMOUR_GROUPS:
            layers[key] = []

    for row, (facing, turn) in enumerate(FACINGS):
        for col in range(frames):
            t = col / float(frames) if loops else col / float(frames - 1)
            joints, groups, extras = build_character()
            values = pose_fn(t)
            # Seen from above, leaning toward or away from the camera only
            # slides the head down over the body until the character is a head
            # with feet. Side on, the lean is the whole read. So the vertical
            # rows keep a fraction of it.
            if facing in ("down", "up"):
                for k in ("lean", "hips_lean", "lunge"):
                    if k in values and clip_name in ("run", "sprint"):
                        values[k] *= 0.4
            apply_pose(joints, extras, values)
            shadow = build_shadow()

            offset = right * (FRAME_SPAN * col) - up * (FRAME_SPAN * row)
            joints["root"].rotation_euler.z = math.radians(turn)
            joints["root"].location = offset
            shadow.location = offset + Vector((0, 0, 0.004))

            layers["shadow"].append(shadow)
            layers["body"] += groups[BODY]
            layers["head"] += groups[HEAD]
            layers["weapon_front"] += groups[WEAPON]
            if ARMOUR_ON:
                for key in ARMOUR_GROUPS:
                    layers[key] += groups[key]

    setup_camera(cols, rows)
    setup_render(cols, rows)

    # What each layer is cut by: the layers that are always drawn and would
    # stand in front of it. Never an optional layer -- a weapon cut into the
    # body would leave a hole whenever the hands are empty.
    occluders = {"shadow": [], "body": [], "weapon_front": ["body", "head"], "head": ["body"]}
    order = [("shadow", 1), ("body", 3), ("weapon_front", 4), ("head", 5)]
    # Plate is drawn after the character and cut by the body and the head, so a
    # forearm crossing the chest still passes in front of the cuirass and the
    # face still shows under the helm. Never cut by another piece of plate:
    # each one is optional, and a hole would be left wherever the missing piece
    # would have been.
    if ARMOUR_ON:
        for i, key in enumerate(ARMOUR_GROUPS):
            # The helm is worn over the hair, so the head must not cut it: the
            # hair cap is as wide as the helm and was slicing the crown off,
            # which left a steel bowl sitting over the face. Everything else is
            # worn under the head -- a forearm crossing the chest passes in
            # front of the cuirass -- so it keeps both holdouts.
            occluders[key] = ["body"] if key == ARM_HEAD else ["body", "head"]
            order.append((key, 6 + i))

    # The other two cuts only replace the armour: the body, the head and the
    # weapon underneath them are the same sheets whatever is worn over them.
    alt = ARMOUR_STYLE != "plate"
    suffix = "_" + ARMOUR_STYLE if alt else ""
    if alt:
        order = [(l, i) for l, i in order if l in ARMOUR_GROUPS]
    # Hides and robes are head, body and legs: there is no hide gauntlet to draw.
    if ARMOUR_STYLE in SOFT_STYLES:
        order = [(l, i) for l, i in order if l in (ARM_LEGS, ARM_BODY, ARM_HEAD)]

    written = []
    for layer, index in order:
        for name, objs in layers.items():
            for ob in objs:
                ob.hide_render = not (name == layer or name in occluders[layer])
                ob.is_holdout = name in occluders[layer]
        raw_path = os.path.join(RENDER_DIR, "%s_%d_%s%s.png" % (clip_name, index, layer, suffix))
        render_to(raw_path)

        big = read_png(raw_path)
        if layer == "shadow":
            small = reduce_majority(big, coverage_needed=8)
            small[..., :3] = 0
            small[..., 3] = np.where(small[..., 3] > 0, 72, 0)
        else:
            small = outline(reduce_majority(big))
        path = os.path.join(out_dir, "layers",
                            "%s_%d_%s%s.png" % (clip_name, index, layer, suffix))
        write_png(path, small)
        written.append(small)

    if alt:
        print("sheet %-7s %s armour only" % (clip_name, ARMOUR_STYLE))
        return

    # A flattened sheet beside the layers, for the character-select preview,
    # which shows the character as authored rather than wearing anything.
    flat = np.zeros_like(written[0])
    for im in written[:4]:
        a = im[..., 3:4].astype(np.float32) / 255.0
        flat[..., :3] = (im[..., :3] * a + flat[..., :3] * (1 - a)).astype(np.uint8)
        flat[..., 3] = np.maximum(flat[..., 3], im[..., 3])
    write_png(os.path.join(out_dir, "%s.png" % clip_name), flat)

    print("sheet %-7s %d frames x %d rows" % (clip_name, frames, rows))


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out_dir = OUT_DIR
    if "--look" in args:
        i = args.index("--look")
        apply_look(args[i + 1])
        del args[i:i + 2]
    if "--style" in args:
        i = args.index("--style")
        global ARMOUR_STYLE
        ARMOUR_STYLE = args[i + 1]
        assert ARMOUR_STYLE in ARMOUR_STYLES, ARMOUR_STYLE
        del args[i:i + 2]
    if "--out" in args:
        i = args.index("--out")
        out_dir = args[i + 1]
        del args[i:i + 2]
    wanted = [c for c in args if c in CLIPS] or list(CLIPS)
    for name in wanted:
        build_sheet(name, out_dir)


if __name__ == "__main__":
    main()
