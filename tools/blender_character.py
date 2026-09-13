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
    "steel":    (0.80, 0.84, 0.88),
    "grip":     (0.38, 0.25, 0.17),
    "eye":      (0.13, 0.11, 0.18),
    "shadow":   (0.00, 0.00, 0.00),
}

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
    """A tapered lock of hair from root to tip."""
    root, tip = Vector(root), Vector(tip)
    d = tip - root
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


def build_character():
    """Returns (joints, groups, extras)."""
    g = {BODY: [], HEAD: [], WEAPON: []}

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
    g[BODY].append(part("scarf_wrap", mesh_torus(0.125, 0.03), "scarf", chest, loc=(0, 0, 0.25)))
    g[HEAD] += [
        part("scarf_tail1", mesh_capsule(0.046, 0.042, 0.10, squash_y=0.6), "scarf", scarf1),
        part("scarf_tail2", mesh_capsule(0.042, 0.038, 0.10, squash_y=0.6), "scarf", scarf2),
        part("scarf_tail3", mesh_capsule(0.038, 0.024, 0.10, squash_y=0.6), "scarf", scarf3),
    ]

    # --- the sword, in the right hand: screen left when facing down, where the
    # CraftPix rigs carry theirs --------------------------------------------
    grip = empty("grip", (0, -0.01, -0.02), joints["hand_r"])
    grip.rotation_euler = Euler((rad(10), rad(18), 0), "XYZ")
    g[WEAPON] += [
        part("pommel", mesh_ellipsoid(0.028, 0.028, 0.028), "gold", grip, loc=(0, 0, 0.09)),
        part("hilt", mesh_capsule(0.02, 0.02, 0.1), "grip", grip, loc=(0, 0, 0.06)),
        part("guard", mesh_ellipsoid(0.085, 0.03, 0.026), "gold", grip, loc=(0, 0, -0.05)),
        part("blade", mesh_capsule(0.036, 0.012, 0.40, squash_y=0.35), "steel", grip,
             loc=(0, 0, -0.07)),
    ]

    joints.update({
        "root": root, "move": move, "hips": hips, "chest": chest, "neck": neck,
        "hair": hair, "skirt": skirt, "scarf1": scarf1, "scarf2": scarf2, "scarf3": scarf3,
        "grip": grip,
    })
    return joints, g, {"eyes": eyes}


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


# clip -> (pose function, frame count, loops)
CLIPS = {
    "idle":   (pose_idle,   8,  True),
    "walk":   (pose_walk,   8,  True),
    "run":    (pose_run,    8,  True),
    "sprint": (pose_sprint, 8,  True),
    "attack": (pose_attack, 6,  False),
    "jump":   (pose_jump,   6,  False),
    "hurt":   (pose_hurt,   4,  False),
    "death":  (pose_death,  6,  False),
    # The work: looped for as long as the gathering goes on.
    "chop":   (pose_chop,   8,  True),
    "mine":   (pose_mine,   8,  True),
    "fish":   (pose_fish,   8,  True),
}

# Rows in the order every sheet in this project uses, and how far the
# character turns from its modelled facing toward the camera.
FACINGS = [("down", 0.0), ("left", 270.0), ("right", 90.0), ("up", 180.0)]


def apply_pose(joints, extras, v):
    for name, joint in joints.items():
        if name == "grip":
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
        joints["shoulder_" + side].rotation_euler = Euler(
            (rad(-get("arm_" + side)), rad(-out * get("flare_" + side)), 0), "XYZ")
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
    joints["grip"].rotation_euler = Euler((rad(get("elbow_r") * 0.8 - 8 - get("sword")), rad(18), 0), "XYZ")

    joints["move"].location = Vector((0, -get("lunge"), get("bob")))
    joints["move"].rotation_euler = Euler((rad(-get("tip")), 0, 0), "XYZ")

    shut = get("blink")
    for eye in extras["eyes"]:
        eye.scale = (1.0, 1.0, max(0.12, 1.0 - shut))


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

    setup_camera(cols, rows)
    setup_render(cols, rows)

    # What each layer is cut by: the layers that are always drawn and would
    # stand in front of it. Never an optional layer -- a weapon cut into the
    # body would leave a hole whenever the hands are empty.
    occluders = {"shadow": [], "body": [], "weapon_front": ["body", "head"], "head": ["body"]}
    order = [("shadow", 1), ("body", 3), ("weapon_front", 4), ("head", 5)]

    written = []
    for layer, index in order:
        for name, objs in layers.items():
            for ob in objs:
                ob.hide_render = not (name == layer or name in occluders[layer])
                ob.is_holdout = name in occluders[layer]
        raw_path = os.path.join(RENDER_DIR, "%s_%d_%s.png" % (clip_name, index, layer))
        render_to(raw_path)

        big = read_png(raw_path)
        if layer == "shadow":
            small = reduce_majority(big, coverage_needed=8)
            small[..., :3] = 0
            small[..., 3] = np.where(small[..., 3] > 0, 72, 0)
        else:
            small = outline(reduce_majority(big))
        path = os.path.join(out_dir, "layers", "%s_%d_%s.png" % (clip_name, index, layer))
        write_png(path, small)
        written.append(small)

    # A flattened sheet beside the layers, for the character-select preview.
    flat = np.zeros_like(written[0])
    for im in written:
        a = im[..., 3:4].astype(np.float32) / 255.0
        flat[..., :3] = (im[..., :3] * a + flat[..., :3] * (1 - a)).astype(np.uint8)
        flat[..., 3] = np.maximum(flat[..., 3], im[..., 3])
    write_png(os.path.join(out_dir, "%s.png" % clip_name), flat)

    print("sheet %-7s %d frames x %d rows" % (clip_name, frames, rows))


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out_dir = OUT_DIR
    if "--out" in args:
        i = args.index("--out")
        out_dir = args[i + 1]
        del args[i:i + 2]
    wanted = [c for c in args if c in CLIPS] or list(CLIPS)
    for name in wanted:
        build_sheet(name, out_dir)


if __name__ == "__main__":
    main()
